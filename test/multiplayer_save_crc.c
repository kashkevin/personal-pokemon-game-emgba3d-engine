#include "global.h"
#include "multiplayer_save.h"
#include "test/test.h"

static u32 BitwiseReference(u32 crc, const u8 *data, u32 size)
{
    u32 i;
    crc ^= 0xFFFFFFFF;
    while (size--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

TEST("Multiplayer save CRC: published vectors and empty seeded input")
{
    const u8 digits[] = "123456789";
    const u8 text[] = "The quick brown fox jumps over the lazy dog";
    EXPECT_EQ(MultiplayerSave_TestCrc32(0, NULL, 0), 0);
    EXPECT_EQ(MultiplayerSave_TestCrc32(0xDEADC0DE, NULL, 0), 0xDEADC0DE);
    EXPECT_EQ(MultiplayerSave_TestCrc32(0xFFFFFFFF, NULL, 0), 0xFFFFFFFF);
    EXPECT_EQ(MultiplayerSave_TestCrc32(0, digits, sizeof(digits) - 1), 0xCBF43926);
    EXPECT_EQ(MultiplayerSave_TestCrc32(0, text, sizeof(text) - 1), 0x414FA339);
}

TEST("Multiplayer save CRC: every incremental split matches a fixed known binary CRC")
{
    u8 bytes[256];
    u32 i, split, crc = 0;
    for (i = 0; i < sizeof(bytes); i++) bytes[i] = i;
    EXPECT_EQ(MultiplayerSave_TestCrc32(0, bytes, sizeof(bytes)), 0x29058C73);
    for (split = 0; split <= sizeof(bytes); split++)
    {
        u32 first = MultiplayerSave_TestCrc32(0, bytes, split);
        EXPECT_EQ(MultiplayerSave_TestCrc32(first, bytes + split, sizeof(bytes) - split), 0x29058C73);
    }
    for (i = 0; i < sizeof(bytes); i++) crc = MultiplayerSave_TestCrc32(crc, bytes + i, 1);
    EXPECT_EQ(crc, 0x29058C73);
}

TEST("Multiplayer save CRC: seeded lengths match the retained independent bitwise reference")
{
    const u32 seeds[] = {0, 1, 0xFFFFFFFF, 0xDEADC0DE};
    u8 bytes[256];
    u32 i, seed, size;
    for (i = 0; i < sizeof(bytes); i++) bytes[i] = i ^ 0xA5;
    for (seed = 0; seed < ARRAY_COUNT(seeds); seed++)
        for (size = 0; size <= sizeof(bytes); size++)
            EXPECT_EQ(MultiplayerSave_TestCrc32(seeds[seed], bytes, size), BitwiseReference(seeds[seed], bytes, size));
}
