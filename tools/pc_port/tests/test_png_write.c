// The picture PNG writer (include/platform/png_write.h): the zlib stream's
// header, deflate block type and Adler-32, and written files' chunks and
// CRCs. Every file it writes (into the folder in argv[1]) is then decoded by
// Python's own zlib (check_png.py), a decoder independent of this code, and
// its pixels compared with the pattern its name gives.
#include <stdint.h>
#include <stdlib.h>

#include "minitest.h"
#include "platform/png_write.h"

enum Pattern
{
    GRADIENT,
    NOISE,
    FLAT,
};

static const char *const sPatternNames[] = {"gradient", "noise", "flat"};

static uint8_t *MakePattern(enum Pattern pattern, int width, int height)
{
    uint8_t *rgba = malloc((size_t)width * height * 4);
    uint32_t state = 2463534242u;

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            uint8_t *p = rgba + ((size_t)y * width + x) * 4;

            if (pattern == GRADIENT)
            {
                p[0] = (uint8_t)(x * 7 + y * 3);
                p[1] = (uint8_t)(x * 5 + y * 11);
                p[2] = (uint8_t)(x ^ y);
            }
            else if (pattern == NOISE)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                p[0] = (uint8_t)state;
                p[1] = (uint8_t)(state >> 8);
                p[2] = (uint8_t)(state >> 16);
            }
            else
            {
                p[0] = 40;
                p[1] = 120;
                p[2] = 200;
            }
            p[3] = (uint8_t)(x + y); // dropped
        }
    return rgba;
}

// The Adler-32 of the rows a decoder should get: a filter byte (0) and RGB.
static uint32_t ExpectedAdler(const uint8_t *rgba, int width, int height, int flipped)
{
    uint32_t a = 1;
    uint32_t b = 0;

    for (int y = 0; y < height; y++)
    {
        const uint8_t *source = rgba + (size_t)(flipped ? height - 1 - y : y) * width * 4;

        b = (b + a) % 65521; // the filter byte, 0
        for (int x = 0; x < width; x++)
            for (int c = 0; c < 3; c++)
            {
                a = (a + source[x * 4 + c]) % 65521;
                b = (b + a) % 65521;
            }
    }
    return (b << 16) | a;
}

static uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t Crc(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static const char *sFolder = "build/pc-tests";

// Encodes, checks the stream, writes the file and checks its chunks; returns
// the stream's size.
static size_t EncodeAndWrite(enum Pattern pattern, int width, int height, int flipped)
{
    static const uint8_t sSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t *rgba = MakePattern(pattern, width, height);
    uint8_t *zlib = NULL;
    size_t size = PngWrite_Encode(rgba, width, height, flipped, &zlib);
    char path[256];
    size_t fileSize = 0;
    uint8_t *file;
    FILE *in;

    MT_CHECK(zlib != NULL && size > 6);
    if (zlib == NULL || size <= 6)
    {
        free(rgba);
        return 0;
    }
    MT_EQ(zlib[0], 0x78);
    MT_EQ(((zlib[0] << 8) | zlib[1]) % 31, 0);
    MT_EQ(zlib[2] & 7, 3); // the last block, fixed Huffman codes
    MT_EQ(Get32(zlib + size - 4), ExpectedAdler(rgba, width, height, flipped));

    snprintf(path, sizeof(path), "%s/png-%s-%dx%d%s.png", sFolder, sPatternNames[pattern],
             width, height, flipped ? "-flip" : "");
    MT_CHECK(PngWrite_RGBA(path, rgba, width, height, flipped));
    file = malloc(size + 1024);
    in = fopen(path, "rb");
    MT_CHECK(in != NULL);
    if (in != NULL)
    {
        fileSize = fread(file, 1, size + 1024, in);
        fclose(in);
    }
    MT_EQ(fileSize, 8 + 25 + 12 + size + 12);
    if (fileSize == 8 + 25 + 12 + size + 12)
    {
        const uint8_t *ihdr = file + 8;
        const uint8_t *idat = ihdr + 25;
        const uint8_t *iend = idat + 12 + size;

        MT_CHECK(memcmp(file, sSignature, 8) == 0);
        MT_CHECK(Get32(ihdr) == 13 && memcmp(ihdr + 4, "IHDR", 4) == 0);
        MT_EQ(Get32(ihdr + 8), width);
        MT_EQ(Get32(ihdr + 12), height);
        MT_CHECK(memcmp(ihdr + 16, "\x08\x02\x00\x00\x00", 5) == 0);
        MT_EQ(Get32(ihdr + 21), Crc(ihdr + 4, 17));
        MT_CHECK(Get32(idat) == size && memcmp(idat + 4, "IDAT", 4) == 0);
        MT_CHECK(memcmp(idat + 8, zlib, size) == 0);
        MT_EQ(Get32(idat + 8 + size), Crc(idat + 4, size + 4));
        MT_CHECK(Get32(iend) == 0 && memcmp(iend + 4, "IEND", 4) == 0);
        MT_EQ(Get32(iend + 8), Crc(iend + 4, 4));
    }
    free(file);
    free(zlib);
    free(rgba);
    return size;
}

MT_TEST(pictures)
{
    size_t flat;
    size_t noise;

    EncodeAndWrite(GRADIENT, 3, 2, 0);
    EncodeAndWrite(GRADIENT, 3, 2, 1);
    EncodeAndWrite(GRADIENT, 37, 23, 0);
    EncodeAndWrite(GRADIENT, 1456, 15, 0);
    EncodeAndWrite(GRADIENT, 21845, 1, 0);
    EncodeAndWrite(GRADIENT, 640, 360, 1);
    noise = EncodeAndWrite(NOISE, 64, 48, 1);
    flat = EncodeAndWrite(FLAT, 640, 360, 0);
    EncodeAndWrite(FLAT, 1, 1, 0);
    // Noise does not compress (at most 9 bits a byte); one colour does.
    MT_CHECK(noise <= 2 + (size_t)(64 * 3 + 1) * 48 * 9 / 8 + 16);
    MT_CHECK(flat < (size_t)(640 * 3 + 1) * 360 / 50);
}

MT_TEST(nothing_for_no_picture)
{
    uint8_t pixel[4] = {0};
    uint8_t *zlib = (uint8_t *)1;
    char path[256];

    MT_EQ(PngWrite_Encode(pixel, 0, 1, 0, &zlib), 0);
    MT_CHECK(zlib == NULL);
    snprintf(path, sizeof(path), "%s/no-such-folder/x.png", sFolder);
    MT_CHECK(!PngWrite_RGBA(path, pixel, 1, 1, 0));
}

int main(int argc, char **argv)
{
    if (argc > 1)
        sFolder = argv[1];
    MT_RUN(pictures);
    MT_RUN(nothing_for_no_picture);
    return MT_REPORT();
}
