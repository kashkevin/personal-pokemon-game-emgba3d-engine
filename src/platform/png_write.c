/*
 * A small PNG writer for the 3D view's pictures (F12): 8-bit RGB, no
 * filtering, the image data deflated with the fixed Huffman codes (RFC
 * 1950/1951), so it needs no compression library. Project-owned; checked by
 * tools/pc_port/tests/test_png_write.c and check_png.py (make check-native).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "platform/png_write.h"

static uint32_t sCrcTable[256];

static void MakeCrcTable(void)
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;

        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        sCrcTable[n] = c;
    }
}

static uint32_t Crc(uint32_t crc, const uint8_t *data, size_t length)
{
    if (sCrcTable[1] == 0)
        MakeCrcTable();
    crc = ~crc;
    for (size_t i = 0; i < length; i++)
        crc = sCrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void Put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static bool WriteChunk(SDL_RWops *file, const char type[4], const uint8_t *data, uint32_t length)
{
    uint8_t header[8];
    uint8_t footer[4];
    uint32_t crc;

    Put32(header, length);
    memcpy(header + 4, type, 4);
    crc = Crc(0, (const uint8_t *)type, 4);
    crc = Crc(crc, data, length);
    Put32(footer, crc);
    return SDL_RWwrite(file, header, 1, 8) == 8
        && (length == 0 || SDL_RWwrite(file, data, 1, length) == length)
        && SDL_RWwrite(file, footer, 1, 4) == 4;
}

/* ------------------------------------------------------------ deflate
 * One block with the fixed Huffman codes (RFC 1951 3.2.6) and LZ77 matches
 * found through a hash of the next three bytes (the newest few places with
 * the same hash, within the 32K window). Nothing like zlib's ratio on
 * photographs, but the 3D view's pictures repeat themselves (nearest
 * neighbour textures, runs of one colour, rows like the row above), and it
 * needs no compression library. */

#define WINDOW_SIZE 32768
#define HASH_BITS 15
#define MAX_CHAIN 24
#define MIN_MATCH 3
#define MAX_MATCH 258

struct BitWriter
{
    uint8_t *out;
    size_t at;
    uint32_t bits;
    int count;
};

static void PutBits(struct BitWriter *writer, uint32_t value, int count)
{
    writer->bits |= value << writer->count;
    writer->count += count;
    while (writer->count >= 8)
    {
        writer->out[writer->at++] = (uint8_t)writer->bits;
        writer->bits >>= 8;
        writer->count -= 8;
    }
}

/* Huffman codes go most significant bit first into the least significant
 * first stream. */
static uint32_t Reverse(uint32_t code, int length)
{
    uint32_t reversed = 0;

    for (int i = 0; i < length; i++)
    {
        reversed = (reversed << 1) | (code & 1);
        code >>= 1;
    }
    return reversed;
}

static void PutSymbol(struct BitWriter *writer, int symbol)
{
    if (symbol < 144)
        PutBits(writer, Reverse(0x30 + symbol, 8), 8);
    else if (symbol < 256)
        PutBits(writer, Reverse(0x190 + symbol - 144, 9), 9);
    else if (symbol < 280)
        PutBits(writer, Reverse(symbol - 256, 7), 7);
    else
        PutBits(writer, Reverse(0xC0 + symbol - 280, 8), 8);
}

static const uint16_t sLengthBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint8_t sLengthExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t sDistanceBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const uint8_t sDistanceExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

static void PutMatch(struct BitWriter *writer, int length, int distance)
{
    int code = 28;
    int distanceCode = 29;

    while (sLengthBase[code] > length)
        code--;
    PutSymbol(writer, 257 + code);
    if (sLengthExtra[code] != 0)
        PutBits(writer, (uint32_t)(length - sLengthBase[code]), sLengthExtra[code]);
    while (sDistanceBase[distanceCode] > distance)
        distanceCode--;
    PutBits(writer, Reverse((uint32_t)distanceCode, 5), 5);
    if (sDistanceExtra[distanceCode] != 0)
        PutBits(writer, (uint32_t)(distance - sDistanceBase[distanceCode]), sDistanceExtra[distanceCode]);
}

static uint32_t Hash3(const uint8_t *p)
{
    return ((uint32_t)p[0] * 506832829u ^ (uint32_t)p[1] * 2654435761u ^ (uint32_t)p[2] * 40503u)
         >> (32 - HASH_BITS);
}

/* Compresses data into out (room for size * 9 / 8 + 16 bytes at least). */
static size_t Deflate(const uint8_t *data, size_t size, uint8_t *out, int32_t *head, int32_t *previous)
{
    struct BitWriter writer = {out, 0, 0, 0};
    size_t i = 0;

    for (size_t h = 0; h < ((size_t)1 << HASH_BITS); h++)
        head[h] = -1;
    PutBits(&writer, 1, 1); /* the last block */
    PutBits(&writer, 1, 2); /* fixed Huffman codes */
    while (i < size)
    {
        int bestLength = 0;
        int bestDistance = 0;

        if (i + MIN_MATCH <= size)
        {
            uint32_t hash = Hash3(data + i);
            int32_t candidate = head[hash];
            int most = size - i < MAX_MATCH ? (int)(size - i) : MAX_MATCH;

            for (int chain = 0; chain < MAX_CHAIN && candidate >= 0
                              && i - (size_t)candidate <= WINDOW_SIZE; chain++)
            {
                const uint8_t *a = data + candidate;
                const uint8_t *b = data + i;

                if (a[bestLength] == b[bestLength])
                {
                    int length = 0;

                    while (length < most && a[length] == b[length])
                        length++;
                    if (length > bestLength)
                    {
                        bestLength = length;
                        bestDistance = (int)(i - (size_t)candidate);
                        if (length == most)
                            break;
                    }
                }
                {
                    int32_t next = previous[candidate & (WINDOW_SIZE - 1)];

                    if (next >= candidate)
                        break; /* that slot was reused by a newer place */
                    candidate = next;
                }
            }
            previous[i & (WINDOW_SIZE - 1)] = head[hash];
            head[hash] = (int32_t)i;
        }
        if (bestLength >= MIN_MATCH)
        {
            PutMatch(&writer, bestLength, bestDistance);
            for (int k = 1; k < bestLength; k++)
            {
                size_t at = i + k;

                if (at + MIN_MATCH <= size)
                {
                    uint32_t hash = Hash3(data + at);

                    previous[at & (WINDOW_SIZE - 1)] = head[hash];
                    head[hash] = (int32_t)at;
                }
            }
            i += bestLength;
        }
        else
        {
            PutSymbol(&writer, data[i]);
            i++;
        }
    }
    PutSymbol(&writer, 256); /* the end of the block */
    if (writer.count > 0)
        PutBits(&writer, 0, 8 - writer.count);
    return writer.at;
}

size_t PngWrite_Encode(const uint8_t *rgba, int width, int height, bool flipRows, uint8_t **out)
{
    size_t rowBytes = (size_t)width * 3 + 1; /* filter byte + RGB */
    size_t raw = rowBytes * (size_t)height;
    uint8_t *rows = NULL;
    uint8_t *zlib = NULL;
    int32_t *head = NULL;
    int32_t *previous = NULL;
    size_t at;
    uint32_t a = 1;
    uint32_t b = 0;

    *out = NULL;
    if (width <= 0 || height <= 0)
        return 0;
    rows = malloc(raw);
    zlib = malloc(2 + raw + raw / 8 + 64 + 4);
    head = malloc(sizeof(int32_t) << HASH_BITS);
    previous = malloc(sizeof(int32_t) * WINDOW_SIZE);
    if (rows == NULL || zlib == NULL || head == NULL || previous == NULL)
    {
        free(rows);
        free(zlib);
        free(head);
        free(previous);
        return 0;
    }
    for (int y = 0; y < height; y++)
    {
        const uint8_t *source = rgba + (size_t)(flipRows ? height - 1 - y : y) * width * 4;
        uint8_t *row = rows + (size_t)y * rowBytes;

        row[0] = 0; /* no filter */
        for (int x = 0; x < width; x++)
        {
            row[1 + x * 3] = source[x * 4];
            row[2 + x * 3] = source[x * 4 + 1];
            row[3 + x * 3] = source[x * 4 + 2];
        }
    }
    for (size_t i = 0; i < raw; i++)
    {
        a += rows[i];
        if (a >= 65521)
            a -= 65521;
        b += a;
        if (b >= 65521)
            b -= 65521;
    }
    zlib[0] = 0x78; /* deflate, 32K window */
    zlib[1] = 0x01; /* no dictionary; 0x7801 % 31 == 0 */
    at = 2 + Deflate(rows, raw, zlib + 2, head, previous);
    Put32(zlib + at, (b << 16) | a);
    at += 4;
    free(rows);
    free(head);
    free(previous);
    *out = zlib;
    return at;
}

bool PngWrite_RGBA(const char *path, const uint8_t *rgba, int width, int height, bool flipRows)
{
    static const uint8_t sSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t header[13];
    uint8_t *zlib;
    size_t zlibSize = PngWrite_Encode(rgba, width, height, flipRows, &zlib);
    SDL_RWops *file;
    bool ok;

    if (zlibSize == 0 || zlibSize > 0x7FFFFFFF)
    {
        free(zlib);
        return false;
    }
    file = SDL_RWFromFile(path, "wb");
    if (file == NULL)
    {
        free(zlib);
        return false;
    }
    Put32(header, (uint32_t)width);
    Put32(header + 4, (uint32_t)height);
    header[8] = 8;   /* bits per channel */
    header[9] = 2;   /* RGB */
    header[10] = 0;  /* deflate */
    header[11] = 0;  /* adaptive filters (each row says none) */
    header[12] = 0;  /* not interlaced */
    ok = SDL_RWwrite(file, sSignature, 1, 8) == 8
      && WriteChunk(file, "IHDR", header, 13)
      && WriteChunk(file, "IDAT", zlib, (uint32_t)zlibSize)
      && WriteChunk(file, "IEND", NULL, 0);
    ok = SDL_RWclose(file) == 0 && ok;
    free(zlib);
    return ok;
}
