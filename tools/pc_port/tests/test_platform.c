// Unit tests for project-owned native PC platform code.
// Built and run by: make -f Makefile_pc NATIVE_LINUX=1 check-native
#include "global.h"
#include "agb_flash.h"
#include "gba/flash_internal.h"
#include "random.h"
#include "siirtc.h"
#include "minitest.h"

#include <limits.h>
#undef abs // global.h defines abs() as a macro; restore the libc one for <stdlib.h>
#include <stdlib.h>

extern volatile bool8 gPlatformFlashDirty;
// siirtc_pc.c reads the save's clock offset for PKMN_CLOCK_HOUR.
static struct SaveBlock2 sTestSaveBlock2;
struct SaveBlock2 *gSaveBlock2Ptr = &sTestSaveBlock2;
s32 Div(s32 num, s32 denom);
struct BitUnPackArgs { u16 compressedSize; u8 compressedBits; u8 decompressedBits; u32 dataOffset:31; u32 offsetZeros:1; };
void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *args);
void FaultCompat_Init(void);
u32 ProgramFlashSectorAndVerifyNBytes(u16 sectorNum, void *dataSrc, u32 n);
u32 VerifyFlashSector(u16 sectorNum, u8 *src);
u32 VerifyFlashSectorNBytes(u16 sectorNum, u8 *src, u32 n);

MT_TEST(arm_shift_semantics)
{
    volatile u32 n32 = 32, n31 = 31, n0 = 0;
    MT_EQ(ARM_LSL32(1, n32), 0);
    MT_EQ(ARM_LSR32(0xFFFFFFFFu, n32), 0);
    MT_EQ(ARM_LSL32(1, n31), 0x80000000u);
    MT_EQ(ARM_LSR32(0x80000000u, n31), 1);
    MT_EQ(ARM_LSL32(0x1234, n0), 0x1234);
    MT_EQ(ARM_LSL32(1, 8 * 4) - 1, 0xFFFFFFFFu); // glyph width mask for an 8 px glyph
}

MT_TEST(flash_driver)
{
    static u8 sector[4096], readBack[4096];
    u32 i;

    MT_EQ(IdentifyFlash(), 0);
    MT_EQ(gFlash->sector.size, 4096);
    MT_EQ(gFlash->sector.count, 32);
    for (i = 0; i < sizeof(sector); i++)
        sector[i] = (u8)(i * 7 + 3);
    gPlatformFlashDirty = FALSE;
    MT_EQ(ProgramFlashSectorAndVerify(5, sector), 0);
    MT_CHECK(gPlatformFlashDirty);
    ReadFlash(5, 0, readBack, sizeof(readBack));
    MT_CHECK(memcmp(sector, readBack, sizeof(sector)) == 0);
    MT_EQ(VerifyFlashSector(5, sector), 0);
    sector[100] ^= 0xFF;
    MT_CHECK(VerifyFlashSector(5, sector) != 0);
    MT_EQ(VerifyFlashSectorNBytes(5, sector, 100), 0);
    MT_EQ(EraseFlashSector(5), 0);
    ReadFlash(5, 10, readBack, 16);
    for (i = 0; i < 16; i++)
        MT_EQ(readBack[i], 0xFF);
    MT_EQ(ProgramFlashByte(31, 4095, 0x5A), 0);
    MT_EQ(FLASH_BASE[131071], 0x5A);
    MT_CHECK(ProgramFlashSector(32, sector) != 0); // out of range is refused
    ReadFlash(31, 4090, readBack, 100);             // clamps at the end of flash
    MT_EQ(readBack[5], 0x5A);
}

static bool32 IsBcd(u8 v, u8 max)
{
    return (v & 0xF) <= 9 && (v >> 4) <= 9 && ((v >> 4) * 10 + (v & 0xF)) <= max;
}

MT_TEST(host_rtc)
{
    struct SiiRtcInfo rtc;
    memset(&rtc, 0, sizeof(rtc));
    MT_EQ(SiiRtcProbe() & 0xF, 1);
    MT_CHECK(SiiRtcGetStatus(&rtc));
    MT_CHECK(rtc.status & SIIRTCINFO_24HOUR);
    MT_CHECK(SiiRtcGetDateTime(&rtc));
    MT_CHECK(IsBcd(rtc.year, 99) && IsBcd(rtc.month, 12) && rtc.month != 0);
    MT_CHECK(IsBcd(rtc.day, 31) && rtc.day != 0 && IsBcd(rtc.dayOfWeek, 6));
    MT_CHECK(IsBcd(rtc.hour, 23) && IsBcd(rtc.minute, 59) && IsBcd(rtc.second, 59));
    SiiRtcProtect();
    MT_CHECK(!SiiRtcGetTime(&rtc)); // locked like the hardware driver
    SiiRtcUnprotect();
    MT_CHECK(SiiRtcGetTime(&rtc));
}

MT_TEST(bios_helpers)
{
    static const u8 src[2] = {0x81, 0x03};
    u32 out[4] = {0};
    struct BitUnPackArgs args = {.compressedSize = 2, .compressedBits = 1, .decompressedBits = 4};

    MT_EQ(Div(7, 2), 3);
    MT_EQ(Div(-7, 2), -3);
    BitUnPack(src, out, &args);
    MT_EQ(out[0], 0x10000001u); // bits 0 and 7 of 0x81 -> nibbles 0 and 7
    MT_EQ(out[1], 0x00000011u); // bits 0 and 1 of 0x03
}

static volatile int sZero = 0, sMinusOne = -1, sIntMin = INT_MIN;
static volatile unsigned sUZero = 0;

MT_TEST(division_matches_gba)
{
    volatile int pos = 7, neg = -7, zero = 0;
    volatile unsigned upos = 9;
    FaultCompat_Init();
    MT_EQ(pos / sZero, INT_MAX);
    MT_EQ(neg / sZero, INT_MIN);
    MT_EQ(zero / sZero, 0);
    MT_EQ(pos % sZero, INT_MAX);
    // Store through volatiles: for x % y the compiler may assume y != 0 and fold
    // a comparison against values >= y, so compare the stored result instead.
    volatile unsigned uq = upos / sUZero, ur = upos % sUZero;
    MT_EQ(uq, 0xFFFFFFFFu);
    MT_EQ(ur, 0xFFFFFFFFu);
    MT_EQ(sIntMin / sMinusOne, INT_MIN);
    MT_EQ(sIntMin % sMinusOne, 0);
}

// Reference SFC32 step (same as the GBA's Thumb Random32 routine).
static u32 RefSfc32(u32 s[4])
{
    u32 result = s[0] + s[1] + s[3];
    s[3] += 1;
    s[0] = s[1] ^ (s[1] >> 9);
    s[1] = s[2] * 9;
    s[2] = result + ((s[2] << 21) | (s[2] >> 11));
    return result;
}

MT_TEST(random_matches_gba_sequence)
{
    u32 ref[4];
    int i;
    SeedRng(0x12345678);
    memcpy(ref, &gRngValue, sizeof(ref));
    for (i = 0; i < 1000; i++)
        MT_EQ(Random32(), RefSfc32(ref));
}

// dma.c reads the emulated DMA registers; in the game bios.c owns this array.
unsigned char REG_BASE[0x400] __attribute__((aligned(4)));
#include "platform/dma.h"

MT_TEST(hblank_dma_runaway_is_stopped)
{
    static u16 lines[DISPLAY_HEIGHT * 4];
    static u16 reg;
    const u32 control = ((DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | DMA_SRC_INC
                          | DMA_DEST_RELOAD | DMA_16BIT) << 16) | 1;
    int frame, i;

    for (i = 0; i < (int)ARRAY_COUNT(lines); i++)
        lines[i] = i;
    // Re-armed every frame, as a scanline effect does in its VBlank callback.
    for (frame = 0; frame < 5; frame++)
    {
        DmaSet(0, lines, &reg, control);
        for (i = 0; i < DISPLAY_HEIGHT; i++)
            RunDMAs(DMA_HBLANK);
        MT_EQ(reg, DISPLAY_HEIGHT - 1);
    }
    // Left running: stopped after three frames' worth of lines, before reading
    // further past the buffer.
    DmaSet(0, lines, &reg, control);
    for (i = 0; i < DISPLAY_HEIGHT * 4; i++)
        RunDMAs(DMA_HBLANK);
    MT_EQ(reg, 3 * DISPLAY_HEIGHT - 1);
}

int main(void)
{
    MT_RUN(arm_shift_semantics);
    MT_RUN(flash_driver);
    MT_RUN(host_rtc);
    MT_RUN(bios_helpers);
    MT_RUN(division_matches_gba);
    MT_RUN(random_matches_gba_sequence);
    MT_RUN(hblank_dma_runaway_is_stopped);
    return MT_REPORT();
}

// random.c's range helpers assert through the crash screen; not exercised here.
void AssertfCrashScreen(const void *return0, const char *fmt, ...)
{
    fprintf(stderr, "assert: %s\n", fmt);
    abort();
}
