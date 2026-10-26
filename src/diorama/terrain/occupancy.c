#ifdef ENABLE_DIORAMA

#include <stdlib.h>
#include <string.h>

#include "diorama/occupancy.h"

static int CompareSpans(const void *leftValue, const void *rightValue)
{
    const struct DioramaOccupancySpan *left = leftValue;
    const struct DioramaOccupancySpan *right = rightValue;

#define COMPARE_FIELD(field) do { if (left->field != right->field) return left->field < right->field ? -1 : 1; } while (0)
    COMPARE_FIELD(x);
    COMPARE_FIELD(z);
    COMPARE_FIELD(yMin);
    COMPARE_FIELD(yMax);
    COMPARE_FIELD(sourceCellX);
    COMPARE_FIELD(sourceCellY);
#undef COMPARE_FIELD
    return memcmp(left->material, right->material, sizeof(left->material));
}

static bool SameProvenance(const struct DioramaOccupancySpan *left,
                           const struct DioramaOccupancySpan *right)
{
    return left->sourceCellX == right->sourceCellX
        && left->sourceCellY == right->sourceCellY
        && left->flags == right->flags
        && memcmp(left->material, right->material, sizeof(left->material)) == 0;
}

/*
 * The sorts and the column lookups below were qsort and a binary search per
 * span and neighbour: about 80% of a town chunk's build (performance work
 * 2026-10-26, the owner's lag when running: a chunk took about 20 ms on the
 * cloud machine, 30 of them at once at a map's edge). Spans and faces now go
 * through counting and radix sorts into the same order, so every chunk is
 * built the same, byte for byte; a box too big for them falls back to qsort.
 */
#define OCCUPANCY_MAX_COLUMNS (1u << 20)
/* Below this many spans or faces qsort and the binary search cost less than
 * clearing the counting sorts' tables (a room's or a forest's small chunks). */
#define OCCUPANCY_SMALL_SORT 1536

static struct DioramaOccupancySpan *sSpanScratch;
static size_t sSpanScratchCapacity;
static uint32_t *sColumnIndex;
static size_t sColumnIndexCapacity;

static bool Reserve(void **buffer, size_t *capacity, size_t count, size_t size)
{
    void *grown;

    if (count <= *capacity)
        return true;
    grown = realloc(*buffer, count * size);
    if (grown == NULL)
        return false;
    *buffer = grown;
    *capacity = count;
    return true;
}

/* The spans' columns, x then z (CompareSpans' first two fields): a box of
 * them, depth its z extent; false when it is too big for the index. */
struct ColumnBox
{
    int32_t minX;
    int32_t minZ;
    uint32_t depth;
    uint32_t columns;
};

static bool SpanColumnBox(const struct DioramaOccupancySpan *spans, uint32_t count, int32_t margin,
                          struct ColumnBox *box)
{
    int64_t minX = spans[0].x;
    int64_t maxX = spans[0].x;
    int64_t minZ = spans[0].z;
    int64_t maxZ = spans[0].z;
    uint32_t i;

    for (i = 1; i < count; i++)
    {
        if (spans[i].x < minX) minX = spans[i].x;
        if (spans[i].x > maxX) maxX = spans[i].x;
        if (spans[i].z < minZ) minZ = spans[i].z;
        if (spans[i].z > maxZ) maxZ = spans[i].z;
    }
    minX -= margin;
    maxX += margin;
    minZ -= margin;
    maxZ += margin;
    if (maxX - minX >= 4096 || maxZ - minZ >= 4096
     || (uint64_t)(maxX - minX + 1) * (uint64_t)(maxZ - minZ + 1) > OCCUPANCY_MAX_COLUMNS)
        return false;
    box->minX = (int32_t)minX;
    box->minZ = (int32_t)minZ;
    box->depth = (uint32_t)(maxZ - minZ + 1);
    box->columns = (uint32_t)(maxX - minX + 1) * box->depth;
    return true;
}

static uint32_t ColumnOf(const struct ColumnBox *box, int32_t x, int32_t z)
{
    return (uint32_t)(x - box->minX) * box->depth + (uint32_t)(z - box->minZ);
}

/* The order qsort(CompareSpans) gives: a stable counting sort by column, then
 * an insertion sort by the whole comparison (only spans of one column can be
 * out of order). Spans that compare equal are the same span (their flags are
 * their cell's), so this is the one order there is. */
static bool SortSpans(struct DioramaOccupancySpan *spans, uint32_t count)
{
    struct ColumnBox box;
    uint32_t column;
    uint32_t i;

    if (count < 2)
        return true;
    if (count < OCCUPANCY_SMALL_SORT)
        return false;
    if (!SpanColumnBox(spans, count, 0, &box)
     || !Reserve((void **)&sColumnIndex, &sColumnIndexCapacity, box.columns + 1, sizeof(uint32_t))
     || !Reserve((void **)&sSpanScratch, &sSpanScratchCapacity, count, sizeof(*spans)))
        return false;
    memset(sColumnIndex, 0, (box.columns + 1) * sizeof(uint32_t));
    for (i = 0; i < count; i++)
        sColumnIndex[ColumnOf(&box, spans[i].x, spans[i].z) + 1]++;
    for (column = 0; column < box.columns; column++)
        sColumnIndex[column + 1] += sColumnIndex[column];
    for (i = 0; i < count; i++)
        sSpanScratch[sColumnIndex[ColumnOf(&box, spans[i].x, spans[i].z)]++] = spans[i];
    for (i = 1; i < count; i++)
    {
        uint32_t at = i;
        struct DioramaOccupancySpan moving;

        if (CompareSpans(&sSpanScratch[i - 1], &sSpanScratch[i]) <= 0)
            continue;
        moving = sSpanScratch[i];
        while (at > 0 && CompareSpans(&sSpanScratch[at - 1], &moving) > 0)
        {
            sSpanScratch[at] = sSpanScratch[at - 1];
            at--;
        }
        sSpanScratch[at] = moving;
    }
    memcpy(spans, sSpanScratch, count * sizeof(*spans));
    return true;
}

bool DioramaOccupancy_Canonicalize(struct DioramaOccupancySpan *spans,
                                   uint32_t *spanCount)
{
    uint32_t read;
    uint32_t write = 0;

    if (spans == NULL || spanCount == NULL)
        return false;
    for (read = 0; read < *spanCount; read++)
        if (spans[read].yMin >= spans[read].yMax)
            return false;
    if (!SortSpans(spans, *spanCount))
        qsort(spans, *spanCount, sizeof(*spans), CompareSpans);
    for (read = 0; read < *spanCount; read++)
    {
        /* Spans of one column that only touch keep their own materials (a cell's
         * side over the riser below it, terrain_mesh.c): the shell finds no face
         * between them. Spans that overlap must be one and the same. */
        if (write != 0
         && spans[write - 1].x == spans[read].x
         && spans[write - 1].z == spans[read].z
         && spans[read].yMin == spans[write - 1].yMax
         && !SameProvenance(&spans[write - 1], &spans[read]))
        {
            if (write != read)
                spans[write] = spans[read];
            write++;
        }
        else if (write != 0
         && spans[write - 1].x == spans[read].x
         && spans[write - 1].z == spans[read].z
         && spans[read].yMin <= spans[write - 1].yMax)
        {
            if (!SameProvenance(&spans[write - 1], &spans[read]))
                return false;
            if (spans[read].yMax > spans[write - 1].yMax)
                spans[write - 1].yMax = spans[read].yMax;
        }
        else
        {
            if (write != read)
                spans[write] = spans[read];
            write++;
        }
    }
    *spanCount = write;
    return true;
}

static void FindColumn(const struct DioramaOccupancySpan *spans, uint32_t spanCount,
                       int32_t x, int32_t z, uint32_t *begin, uint32_t *end)
{
    uint32_t low = 0;
    uint32_t high = spanCount;

    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        if (spans[middle].x < x || (spans[middle].x == x && spans[middle].z < z))
            low = middle + 1;
        else
            high = middle;
    }
    *begin = low;
    while (low < spanCount && spans[low].x == x && spans[low].z == z)
        low++;
    *end = low;
}

static bool IsOccupied(const struct DioramaOccupancySpan *spans,
                       uint32_t begin, uint32_t end, int16_t y)
{
    uint32_t i;
    for (i = begin; i < end; i++)
        if (spans[i].yMin <= y && y < spans[i].yMax)
            return true;
    return false;
}

static bool AppendFace(struct DioramaShellFace *faces, uint32_t capacity,
                       uint32_t *count, const struct DioramaOccupancySpan *span,
                       uint8_t face, int32_t plane, int32_t uMin, int32_t uMax,
                       int16_t vMin, int16_t vMax, uint8_t axis, int8_t sign)
{
    struct DioramaShellFace *output;

    if (*count >= capacity)
        return false;
    output = &faces[(*count)++];
    output->plane = plane;
    output->uMin = uMin;
    output->uMax = uMax;
    output->vMin = vMin;
    output->vMax = vMax;
    output->sourceCellX = span->sourceCellX;
    output->sourceCellY = span->sourceCellY;
    output->material = span->material[face];
    output->flags = span->flags;
    output->axis = axis;
    output->sign = sign;
    return true;
}

static bool AppendSideGaps(const struct DioramaOccupancySpan *spans,
                           uint32_t neighborBegin, uint32_t neighborEnd,
                           const struct DioramaOccupancySpan *span,
                           uint8_t face, int32_t plane, int32_t u,
                           uint8_t axis, int8_t sign,
                           struct DioramaShellFace *faces, uint32_t capacity,
                           uint32_t *count)
{
    int16_t cursor = span->yMin;
    uint32_t i;

    for (i = neighborBegin; i < neighborEnd && cursor < span->yMax; i++)
    {
        int16_t coveredMin = spans[i].yMin > cursor ? spans[i].yMin : cursor;
        int16_t coveredMax = spans[i].yMax < span->yMax ? spans[i].yMax : span->yMax;

        if (coveredMax <= cursor || coveredMin >= span->yMax)
            continue;
        if (coveredMin > cursor
         && !AppendFace(faces, capacity, count, span, face, plane, u, u + 1,
                        cursor, coveredMin, axis, sign))
            return false;
        if (coveredMax > cursor)
            cursor = coveredMax;
    }
    return cursor >= span->yMax
        || AppendFace(faces, capacity, count, span, face, plane, u, u + 1,
                      cursor, span->yMax, axis, sign);
}

/* Each column's spans, begin and end, for the shell's lookups: spans sorted
 * by column (as Canonicalize leaves them). False when they are not (the
 * binary search then), or the box is too big. */
static bool IndexColumns(const struct DioramaOccupancySpan *spans, uint32_t spanCount, struct ColumnBox *box)
{
    uint32_t i;

    if (spanCount < OCCUPANCY_SMALL_SORT || !SpanColumnBox(spans, spanCount, 1, box)
     || !Reserve((void **)&sColumnIndex, &sColumnIndexCapacity, (size_t)box->columns * 2, sizeof(uint32_t)))
        return false;
    for (i = 1; i < spanCount; i++)
        if (spans[i].x < spans[i - 1].x || (spans[i].x == spans[i - 1].x && spans[i].z < spans[i - 1].z))
            return false;
    memset(sColumnIndex, 0, (size_t)box->columns * 2 * sizeof(uint32_t));
    for (i = 0; i < spanCount; i++)
    {
        uint32_t column = ColumnOf(box, spans[i].x, spans[i].z);

        if (i == 0 || spans[i].x != spans[i - 1].x || spans[i].z != spans[i - 1].z)
            sColumnIndex[column * 2] = i;
        sColumnIndex[column * 2 + 1] = i + 1;
    }
    return true;
}

static void LookUpColumn(const struct DioramaOccupancySpan *spans, uint32_t spanCount, const struct ColumnBox *box,
                         bool indexed, int32_t x, int32_t z, uint32_t *begin, uint32_t *end)
{
    uint32_t column;

    if (!indexed)
    {
        FindColumn(spans, spanCount, x, z, begin, end);
        return;
    }
    column = ColumnOf(box, x, z);
    *begin = sColumnIndex[column * 2];
    *end = sColumnIndex[column * 2 + 1];
}

bool DioramaOccupancy_BuildShell(const struct DioramaOccupancySpan *spans,
                                 uint32_t spanCount,
                                 int32_t ownerMinX, int32_t ownerMinZ,
                                 int32_t ownerMaxX, int32_t ownerMaxZ,
                                 struct DioramaShellFace *faces,
                                 uint32_t faceCapacity, uint32_t *faceCount)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static const uint8_t faceIds[4] = {DIORAMA_OCCUPANCY_FACE_NORTH,
                                      DIORAMA_OCCUPANCY_FACE_EAST,
                                      DIORAMA_OCCUPANCY_FACE_SOUTH,
                                      DIORAMA_OCCUPANCY_FACE_WEST};
    uint32_t count = 0;
    uint32_t i;
    struct ColumnBox box;
    bool indexed;

    if (spans == NULL || faces == NULL || faceCount == NULL)
        return false;
    indexed = IndexColumns(spans, spanCount, &box);
    for (i = 0; i < spanCount; i++)
    {
        const struct DioramaOccupancySpan *span = &spans[i];
        uint32_t begin;
        uint32_t end;
        int side;

        if (span->x < ownerMinX || span->x >= ownerMaxX
         || span->z < ownerMinZ || span->z >= ownerMaxZ)
            continue;
        LookUpColumn(spans, spanCount, &box, indexed, span->x, span->z, &begin, &end);
        if (!IsOccupied(spans, begin, end, span->yMax)
         && !AppendFace(faces, faceCapacity, &count, span, DIORAMA_OCCUPANCY_FACE_TOP,
                        span->yMax, span->x, span->x + 1, span->z, span->z + 1, 1, 1))
            return false;
        if (!IsOccupied(spans, begin, end, span->yMin - 1)
         && !AppendFace(faces, faceCapacity, &count, span, DIORAMA_OCCUPANCY_FACE_BOTTOM,
                        span->yMin, span->x, span->x + 1, span->z, span->z + 1, 1, -1))
            return false;
        for (side = 0; side < 4; side++)
        {
            int32_t neighborX = span->x + offsets[side][0];
            int32_t neighborZ = span->z + offsets[side][1];
            uint8_t axis = side == 1 || side == 3 ? 0 : 2;
            int8_t sign = side == 0 || side == 3 ? -1 : 1;
            int32_t plane = axis == 0 ? span->x + (sign > 0) : span->z + (sign > 0);
            int32_t u = axis == 0 ? span->z : span->x;

            LookUpColumn(spans, spanCount, &box, indexed, neighborX, neighborZ, &begin, &end);
            if (!AppendSideGaps(spans, begin, end, span, faceIds[side], plane, u,
                                axis, sign, faces, faceCapacity, &count))
                return false;
        }
    }
    *faceCount = count;
    return true;
}

static int CompareFaces(const void *leftValue, const void *rightValue)
{
    const struct DioramaShellFace *left = leftValue;
    const struct DioramaShellFace *right = rightValue;

#define COMPARE_FIELD(field) do { if (left->field != right->field) return left->field < right->field ? -1 : 1; } while (0)
    COMPARE_FIELD(axis);
    COMPARE_FIELD(sign);
    COMPARE_FIELD(plane);
    COMPARE_FIELD(sourceCellX);
    COMPARE_FIELD(sourceCellY);
    COMPARE_FIELD(material);
    COMPARE_FIELD(flags);
    COMPARE_FIELD(vMin);
    COMPARE_FIELD(vMax);
    COMPARE_FIELD(uMin);
    COMPARE_FIELD(uMax);
#undef COMPARE_FIELD
    return 0;
}

/*
 * The order qsort(CompareFaces) gives, by a radix sort: each face's fields in
 * CompareFaces' order packed into a 128-bit key, every field as its distance
 * from the smallest value it takes (so wide as the chunk's range of it needs).
 * CompareFaces compares every field, so faces with the same key are the same
 * face and the order is the one there is.
 */
struct FaceKey
{
    uint32_t words[4]; /* the key, least significant word first */
    uint32_t index;
};

static struct FaceKey *sFaceKeys;
static struct FaceKey *sFaceKeysScratch;
static size_t sFaceKeysCapacity;
static size_t sFaceKeysScratchCapacity;
static struct DioramaShellFace *sFaceScratch;
static size_t sFaceScratchCapacity;

enum { FACE_FIELD_COUNT = 11 };

/* A face's fields in CompareFaces' order (all fit 32 bits signed). */
static void FaceFields(const struct DioramaShellFace *face, int32_t fields[FACE_FIELD_COUNT])
{
    fields[0] = face->axis;
    fields[1] = face->sign;
    fields[2] = face->plane;
    fields[3] = face->sourceCellX;
    fields[4] = face->sourceCellY;
    fields[5] = face->material;
    fields[6] = face->flags;
    fields[7] = face->vMin;
    fields[8] = face->vMax;
    fields[9] = face->uMin;
    fields[10] = face->uMax;
}

static unsigned BitsFor(uint32_t range)
{
    unsigned bits = 0;

    while (range != 0)
    {
        bits++;
        range >>= 1;
    }
    return bits;
}

/* value (bits wide, at most 32) into the key at bit offset (from the least significant bit) */
static void PutBits(struct FaceKey *key, uint32_t value, unsigned offset)
{
    unsigned word = offset >> 5;
    unsigned shift = offset & 31;

    key->words[word] |= value << shift;
    if (shift != 0 && word < 3)
        key->words[word + 1] |= value >> (32 - shift);
}

static uint32_t DigitOf(const struct FaceKey *key, unsigned offset)
{
    unsigned word = offset >> 5;
    unsigned shift = offset & 31;
    uint32_t digit = key->words[word] >> shift;

    if (shift > 16 && word < 3)
        digit |= key->words[word + 1] << (32 - shift);
    return digit & 0xFFFF;
}

static bool SortFaces(struct DioramaShellFace *faces, uint32_t count)
{
    static uint32_t counts[0x10001];
    int32_t minimum[FACE_FIELD_COUNT];
    int32_t maximum[FACE_FIELD_COUNT];
    int32_t fields[FACE_FIELD_COUNT];
    unsigned offsets[FACE_FIELD_COUNT];
    int used[FACE_FIELD_COUNT];
    int usedCount = 0;
    unsigned totalBits = 0;
    uint32_t i;
    int field;
    int u;
    unsigned offset;
    struct FaceKey *keys;
    struct FaceKey *scratch;

    if (count < 2)
        return true;
    if (count < OCCUPANCY_SMALL_SORT)
        return false;
    if (!Reserve((void **)&sFaceKeys, &sFaceKeysCapacity, count, sizeof(struct FaceKey))
     || !Reserve((void **)&sFaceKeysScratch, &sFaceKeysScratchCapacity, count, sizeof(struct FaceKey))
     || !Reserve((void **)&sFaceScratch, &sFaceScratchCapacity, count, sizeof(*faces)))
        return false;
    FaceFields(&faces[0], minimum);
    FaceFields(&faces[0], maximum);
    for (i = 1; i < count; i++)
    {
        FaceFields(&faces[i], fields);
        for (field = 0; field < FACE_FIELD_COUNT; field++)
        {
            if (fields[field] < minimum[field]) minimum[field] = fields[field];
            if (fields[field] > maximum[field]) maximum[field] = fields[field];
        }
    }
    /* the last field (uMax) the least significant; fields the same in every face are left out */
    for (field = FACE_FIELD_COUNT - 1; field >= 0; field--)
    {
        unsigned bits = BitsFor((uint32_t)maximum[field] - (uint32_t)minimum[field]);

        if (bits == 0)
            continue;
        offsets[field] = totalBits;
        totalBits += bits;
        used[usedCount++] = field;
    }
    if (totalBits > 128)
        return false;
    keys = sFaceKeys;
    scratch = sFaceKeysScratch;
    for (i = 0; i < count; i++)
    {
        memset(keys[i].words, 0, sizeof(keys[i].words));
        keys[i].index = i;
        FaceFields(&faces[i], fields);
        for (u = 0; u < usedCount; u++)
        {
            field = used[u];
            PutBits(&keys[i], (uint32_t)fields[field] - (uint32_t)minimum[field], offsets[field]);
        }
    }
    /* sixteen bits a pass, the least significant first, each a stable
     * counting pass; a digit the same in every key moves nothing */
    for (offset = 0; offset < totalBits; offset += 16)
    {
        uint32_t digit;
        bool spread = true;
        struct FaceKey *swap;

        memset(counts, 0, sizeof(counts));
        for (i = 0; i < count; i++)
            counts[DigitOf(&keys[i], offset) + 1]++;
        for (digit = 0; digit < 0x10000; digit++)
        {
            if (counts[digit + 1] == count)
                spread = false;
            counts[digit + 1] += counts[digit];
        }
        if (!spread)
            continue;
        for (i = 0; i < count; i++)
            scratch[counts[DigitOf(&keys[i], offset)]++] = keys[i];
        swap = keys;
        keys = scratch;
        scratch = swap;
    }
    for (i = 0; i < count; i++)
        sFaceScratch[i] = faces[keys[i].index];
    memcpy(faces, sFaceScratch, count * sizeof(*faces));
    return true;
}

uint32_t DioramaOccupancy_MergeFaces(struct DioramaShellFace *faces,
                                     uint32_t faceCount)
{
    uint32_t read;
    uint32_t write = 0;

    if (!SortFaces(faces, faceCount))
        qsort(faces, faceCount, sizeof(*faces), CompareFaces);
    for (read = 0; read < faceCount; read++)
    {
        struct DioramaShellFace *previous = write == 0 ? NULL : &faces[write - 1];
        struct DioramaShellFace *current = &faces[read];

        if (previous != NULL
         && previous->axis == current->axis && previous->sign == current->sign
         && previous->plane == current->plane
         && previous->sourceCellX == current->sourceCellX
         && previous->sourceCellY == current->sourceCellY
         && previous->material == current->material && previous->flags == current->flags
         && previous->vMin == current->vMin && previous->vMax == current->vMax
         && previous->uMax == current->uMin)
            previous->uMax = current->uMax;
        else
            faces[write++] = *current;
    }
    faceCount = write;
    write = 0;
    for (read = 0; read < faceCount; read++)
    {
        struct DioramaShellFace *previous = write == 0 ? NULL : &faces[write - 1];
        struct DioramaShellFace *current = &faces[read];

        if (previous != NULL
         && previous->axis == current->axis && previous->sign == current->sign
         && previous->plane == current->plane
         && previous->sourceCellX == current->sourceCellX
         && previous->sourceCellY == current->sourceCellY
         && previous->material == current->material && previous->flags == current->flags
         && previous->uMin == current->uMin && previous->uMax == current->uMax
         && previous->vMax == current->vMin)
            previous->vMax = current->vMax;
        else
            faces[write++] = *current;
    }
    return write;
}

#endif
