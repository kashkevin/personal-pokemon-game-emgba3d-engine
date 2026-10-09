// Native PC build: replacement for src/agb_flash.c, agb_flash_1m.c,
// agb_flash_mx.c and agb_flash_le.c (the GBA 128 KiB flash chip drivers).
//
// The save image lives in FLASH_BASE (src/platform/bios.c), byte-for-byte in the
// GBA flash format, so existing .sav files from the emulator build load as-is.
// Writes mark the image dirty; the platform layer persists it to disk at the
// next frame boundary (Platform_FlushSaveIfDirty in sdl2.c).
#include "global.h"
#include "gba/flash_internal.h"

#include <string.h>

#define PC_FLASH_SECTOR_SHIFT 12
#define PC_FLASH_SECTOR_SIZE  (1 << PC_FLASH_SECTOR_SHIFT)
#define PC_FLASH_SECTOR_COUNT 32

u8 gFlashTimeoutFlag;
u8 (*PollFlashStatus)(u8 *);
u16 (*WaitForFlashWrite)(u8 phase, u8 *addr, u8 lastData);
u16 (*ProgramFlashSector)(u16 sectorNum, u8 *src);
const struct FlashType *gFlash;
u16 (*ProgramFlashByte)(u16 sectorNum, u32 offset, u8 data);
u16 gFlashNumRemainingBytes;
u16 (*EraseFlashChip)();
u16 (*EraseFlashSector)(u16 sectorNum);
const u16 *gFlashMaxTime;

volatile bool8 gPlatformFlashDirty;

static const u16 sPcFlashMaxTime[] =
{
      10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
      10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
};

static const struct FlashType sPcFlashType =
{
    .romSize = PC_FLASH_SECTOR_SIZE * PC_FLASH_SECTOR_COUNT,
    .sector = {
        .size = PC_FLASH_SECTOR_SIZE,
        .shift = PC_FLASH_SECTOR_SHIFT,
        .count = PC_FLASH_SECTOR_COUNT,
        .top = 0,
    },
    .wait = {3, 1},
    .ids = {.separate = {.makerId = 0xC2, .deviceId = 0x09}}, // reported as Macronix MX29L010
};

static bool32 ValidSector(u16 sectorNum)
{
    return sectorNum < PC_FLASH_SECTOR_COUNT;
}

static u16 PcProgramFlashByte(u16 sectorNum, u32 offset, u8 data)
{
    if (!ValidSector(sectorNum) || offset >= PC_FLASH_SECTOR_SIZE)
        return 0x80FF;
    FLASH_BASE[(sectorNum << PC_FLASH_SECTOR_SHIFT) + offset] = data;
    gPlatformFlashDirty = TRUE;
    return 0;
}

static u16 PcProgramFlashSector(u16 sectorNum, u8 *src)
{
    if (!ValidSector(sectorNum))
        return 0x80FF;
    memcpy(&FLASH_BASE[sectorNum << PC_FLASH_SECTOR_SHIFT], src, PC_FLASH_SECTOR_SIZE);
    gFlashNumRemainingBytes = 0;
    gPlatformFlashDirty = TRUE;
    return 0;
}

static u16 PcEraseFlashSector(u16 sectorNum)
{
    if (!ValidSector(sectorNum))
        return 0x80FF;
    memset(&FLASH_BASE[sectorNum << PC_FLASH_SECTOR_SHIFT], 0xFF, PC_FLASH_SECTOR_SIZE);
    gPlatformFlashDirty = TRUE;
    return 0;
}

static u16 PcEraseFlashChip(void)
{
    memset(FLASH_BASE, 0xFF, PC_FLASH_SECTOR_SIZE * PC_FLASH_SECTOR_COUNT);
    gPlatformFlashDirty = TRUE;
    return 0;
}

static u16 PcWaitForFlashWrite(u8 phase, u8 *addr, u8 lastData)
{
    return 0;
}

static u8 PcPollFlashStatus(u8 *addr)
{
    return *addr;
}

u16 SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
    return 0;
}

void FlashTimerIntr(void)
{
}

void StartFlashTimer(u8 phase)
{
}

void StopFlashTimer(void)
{
}

void SwitchFlashBank(u8 bankNum)
{
}

u16 ReadFlashId(void)
{
    return (sPcFlashType.ids.separate.deviceId << 8) | sPcFlashType.ids.separate.makerId;
}

u16 IdentifyFlash(void)
{
    gFlash = &sPcFlashType;
    ProgramFlashByte = PcProgramFlashByte;
    ProgramFlashSector = PcProgramFlashSector;
    EraseFlashChip = PcEraseFlashChip;
    EraseFlashSector = PcEraseFlashSector;
    WaitForFlashWrite = PcWaitForFlashWrite;
    PollFlashStatus = PcPollFlashStatus;
    gFlashMaxTime = sPcFlashMaxTime;
    return 0;
}

void ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    u32 start = (sectorNum << PC_FLASH_SECTOR_SHIFT) + offset;

    if (start >= sizeof(FLASH_BASE))
        return;
    if (size > sizeof(FLASH_BASE) - start)
        size = sizeof(FLASH_BASE) - start;
    memcpy(dest, &FLASH_BASE[start], size);
}

u32 VerifyFlashSector(u16 sectorNum, u8 *src)
{
    if (!ValidSector(sectorNum))
        return 1;
    return memcmp(&FLASH_BASE[sectorNum << PC_FLASH_SECTOR_SHIFT], src, PC_FLASH_SECTOR_SIZE) != 0;
}

u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n)
{
    if (!ValidSector(sectorNum) || n > PC_FLASH_SECTOR_SIZE)
        return 1;
    return memcmp(&FLASH_BASE[sectorNum << PC_FLASH_SECTOR_SHIFT], src, n) != 0;
}

u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    u8 i;
    u32 result = 0;

    for (i = 0; i < 3; i++)
    {
        result = ProgramFlashSector(sectorNum, src);
        if (result != 0)
            continue;
        result = VerifyFlashSector(sectorNum, src);
        if (result == 0)
            break;
    }
    return result;
}

u32 ProgramFlashSectorAndVerifyNBytes(u16 sectorNum, void *dataSrc, u32 n)
{
    u8 i;
    u32 result = 0;

    for (i = 0; i < 3; i++)
    {
        result = ProgramFlashSector(sectorNum, dataSrc);
        if (result != 0)
            continue;
        result = VerifyFlashSectorNBytes(sectorNum, dataSrc, n);
        if (result == 0)
            break;
    }
    return result;
}
