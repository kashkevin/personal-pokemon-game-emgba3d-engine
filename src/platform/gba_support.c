// Native PC build: GBA BIOS calls and cartridge symbols that the rest of the
// platform layer (bios.c, sdl2.c) does not provide.
#include "global.h"
#include "main.h"

// ---------------------------------------------------------------------------
// Cartridge header fields read by C code (src/rom_header.s on the GBA).
const u8 RomHeaderGameCode[GAME_CODE_LENGTH] = {'B', 'P', 'E', 'E'};
const u8 RomHeaderSoftwareVersion = 0;

// ---------------------------------------------------------------------------
// BIOS SWI 0x06: signed division.
s32 Div(s32 num, s32 denom)
{
    if (denom == 0)
        return 0;
    return num / denom;
}

// BIOS SWI 0x10: BitUnPack (GBATEK "BitUnPack").
struct BitUnPackArgs
{
    u16 compressedSize;
    u8 compressedBits;
    u8 decompressedBits;
    u32 dataOffset:31;
    u32 offsetZeros:1;
};

void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *args)
{
    const u8 *in = src;
    u32 *out = dest;
    u32 srcBits = args->compressedBits;
    u32 destBits = args->decompressedBits;
    u32 srcMask = (1u << srcBits) - 1;
    u32 accumulator = 0;
    u32 accumulatorBits = 0;
    u32 i;

    if (srcBits == 0 || destBits == 0)
        return;

    for (i = 0; i < args->compressedSize; i++)
    {
        u32 byte = in[i];
        u32 shift;

        for (shift = 0; shift < 8; shift += srcBits)
        {
            u32 value = (byte >> shift) & srcMask;

            if (value != 0 || args->offsetZeros)
                value += args->dataOffset;
            accumulator |= (destBits == 32 ? value : (value & ((1u << destBits) - 1))) << accumulatorBits;
            accumulatorBits += destBits;
            if (accumulatorBits >= 32)
            {
                *out++ = accumulator;
                accumulator = 0;
                accumulatorBits = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// BIOS SWI 0x01 and crt0's EWRAM re-initialisation. Game memory reset for soft
// reset / ReloadSave is handled by the platform reset path (see sdl2.c).
void RegisterRamReset(u32 resetFlags)
{
    (void)resetFlags;
}

void ReInitializeEWRAM(void)
{
}
