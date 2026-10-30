#ifdef ENABLE_DIORAMA

#include <float.h>
#include <math.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/terrain_mesh.h"
#include "diorama/tree_model.generated.h"

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

#define OCCUPANCY_INPUT_COLUMNS (DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL * DIORAMA_TERRAIN_MAX_SURFACES)

static struct DioramaOccupancySpan sOccupancySpans[OCCUPANCY_INPUT_COLUMNS];
static struct DioramaShellFace sShellFaces[DIORAMA_TERRAIN_MAX_FACES];
static bool sVoxelOccupancyOnly; /* DioramaTerrain_SetVoxelOccupancyOnly */
static bool sNoGeometryHash;     /* DioramaTerrain_SetGeometryHash */

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * FNV_PRIME;
}

static uint64_t HashU16(uint64_t hash, uint16_t value)
{
    hash = HashByte(hash, value & 0xFF);
    return HashByte(hash, value >> 8);
}

static uint64_t HashU32(uint64_t hash, uint32_t value)
{
    hash = HashU16(hash, value & 0xFFFF);
    return HashU16(hash, value >> 16);
}

static uint64_t HashFloat(uint64_t hash, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return HashU32(hash, bits);
}

static const struct DioramaTerrainCell *GetCell(const struct DioramaTerrainChunkInput *input,
                                                 int x, int y)
{
    return &input->cells[y * DIORAMA_TERRAIN_INPUT_SIZE + x];
}

static bool AppendVertex(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                          uint32_t *count, float x, float y, float z,
                          float u, float v, float shade, float textureLayer,
                          float reflectionMask, uint32_t color)
{
    struct DioramaTerrainVertex *vertex;

    if (*count >= capacity)
        return false;
    vertex = &vertices[(*count)++];
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->u = u;
    vertex->v = v;
    vertex->shade = shade;
    vertex->textureLayer = textureLayer;
    vertex->reflectionMask = reflectionMask;
    vertex->color = color;
    return true;
}

static bool AppendQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                        uint32_t *count, const float positions[4][3],
                        const struct DioramaTerrainMaterial *material, float shade,
                        float reflectionMask)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t uvX[4] = {0, 1, 1, 0};
    static const uint8_t uvY[4] = {0, 0, 1, 1};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];
        uint8_t x = uvX[corner];
        uint8_t y = uvY[corner];
        uint8_t turn;
        float u;
        float v;

        if (material->flags & 1)
            x = 1 - x;
        if (material->flags & 2)
            y = 1 - y;
        for (turn = 0; turn < material->rotation; turn++)
        {
            uint8_t previousX = x;
            x = 1 - y;
            y = previousX;
        }
        u = x ? material->u1 : material->u0;
        v = y ? material->v1 : material->v0;

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          u, v, shade, material->layer, reflectionMask,
                          UINT32_C(0xFFFFFFFF)))
            return false;
    }
    return true;
}

static float ResolveRoofHeight(const struct DioramaTerrainCell *cell,
                               int pixelX, int pixelZ)
{
    float factor;
    float coordinate;
    float length;

    if (cell->structureId == 0 || cell->measuredRunLength == 0
     || (cell->profile != DIORAMA_ROOF_GABLE_X
      && cell->profile != DIORAMA_ROOF_GABLE_Z))
        return cell->visualHeight;
    coordinate = cell->measuredRunLocal
               + ((cell->measuredAxis == DIORAMA_PLANE_AXIS_Z ? pixelZ : pixelX) + 0.5f)
               / DIORAMA_VOXELS_PER_CELL;
    length = cell->measuredRunLength;
    factor = 1.0f - fabsf(2.0f * coordinate / length - 1.0f);
    if (factor < 0.0f)
        factor = 0.0f;
    if (factor > 1.0f)
        factor = 1.0f;
    return cell->groundHeight + cell->structureBodyHeight
         + cell->structureRoofHeight * factor;
}

int32_t DioramaTerrain_FloorDiv(int32_t value, int32_t divisor)
{
    int32_t quotient = value / divisor;
    int32_t remainder = value % divisor;

    if (remainder != 0 && ((remainder < 0) != (divisor < 0)))
        quotient--;
    return quotient;
}

int16_t DioramaTerrain_VisibleStructureOrigin(int16_t mapCoordinate,
                                              uint8_t structureLocalCoordinate)
{
    return mapCoordinate - structureLocalCoordinate;
}

float DioramaTerrain_NormalizeElevation(uint8_t rawElevation, uint8_t behavior)
{
    (void)rawElevation;
    return DioramaRules_DefaultGroundHeight(behavior);
}

enum DioramaElevationSemantics DioramaTerrain_GetElevationSemantics(uint8_t rawElevation)
{
    if (rawElevation == 0)
        return DIORAMA_ELEVATION_WILDCARD;
    if (rawElevation == 15)
        return DIORAMA_ELEVATION_RETAIN;
    return DIORAMA_ELEVATION_CONCRETE;
}

bool DioramaTerrain_DirtyCellAffectsChunk(int16_t mapX, int16_t mapY,
                                         int16_t chunkX, int16_t chunkY)
{
    int32_t originX = chunkX * DIORAMA_TERRAIN_CHUNK_SIZE;
    int32_t originY = chunkY * DIORAMA_TERRAIN_CHUNK_SIZE;

    return mapX >= originX - DIORAMA_TERRAIN_HALO
        && mapX < originX + DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_HALO
        && mapY >= originY - DIORAMA_TERRAIN_HALO
        && mapY < originY + DIORAMA_TERRAIN_CHUNK_SIZE + DIORAMA_TERRAIN_HALO;
}

bool DioramaTerrain_ShouldInvalidateAll(uint64_t previousSequence,
                                       uint64_t currentSequence,
                                       uint32_t previousEditGeneration,
                                       uint32_t currentEditGeneration,
                                       uint8_t dirtyCellCount,
                                       bool dirtyOverflow)
{
    if (dirtyOverflow)
        return true;
    if (previousEditGeneration == currentEditGeneration)
        return false;
    return previousSequence == 0
        || currentSequence != previousSequence + 1
        || dirtyCellCount == 0;
}

void DioramaTerrain_BuildHeightField(const struct DioramaTerrainHeightCell *cells,
                                     uint16_t width, uint16_t height,
                                     float *visualHeights)
{
    int x;
    int y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            visualHeights[y * width + x] = DioramaTerrain_NormalizeElevation(
                cells[y * width + x].rawElevation, cells[y * width + x].behavior);
}

void DioramaTerrain_BuildTerraceHeightmap(uint8_t profile,
                                          const uint16_t artworkRows[DIORAMA_VOXELS_PER_CELL],
                                          bool useArtwork,
                                          uint16_t rows[DIORAMA_VOXELS_PER_CELL],
                                          uint8_t heights[DIORAMA_VOXELS_PER_CELL
                                                          * DIORAMA_VOXELS_PER_CELL])
{
    int row;

    memset(heights, 0,
           DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL);
    for (row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
    {
        int x;
        uint16_t artwork = artworkRows[row];
        int artworkStart = 2;

        if (useArtwork)
        {
            artworkStart = 0;
            while (artworkStart < DIORAMA_VOXELS_PER_CELL
                && !(artwork & (1u << artworkStart)))
                artworkStart++;
        }
        if (row == 0 || row == DIORAMA_VOXELS_PER_CELL - 1)
            artworkStart = 2;

        rows[row] = 0;
        for (x = 0; x < DIORAMA_VOXELS_PER_CELL; x++)
        {
            int eastHeight;
            int northHeight = (DIORAMA_VOXELS_PER_CELL - 1 - row)
                            * DIORAMA_VOXELS_PER_CELL
                            / (DIORAMA_VOXELS_PER_CELL - 1);
            int height;

            if (x >= artworkStart)
            {
                int rise = x - artworkStart + 1;

                eastHeight = rise >= 6 ? DIORAMA_VOXELS_PER_CELL
                                       : rise * DIORAMA_VOXELS_PER_CELL / 6;
            }
            else
                eastHeight = 0;
            if (eastHeight > 0 && eastHeight < DIORAMA_VOXELS_PER_CELL)
            {
                eastHeight = (eastHeight * eastHeight
                            * (24 - eastHeight) + 64) / 128;
                if (eastHeight < 1)
                    eastHeight = 1;
                if (eastHeight >= DIORAMA_VOXELS_PER_CELL)
                    eastHeight = DIORAMA_VOXELS_PER_CELL - 1;
            }
            if (northHeight > 0 && northHeight < DIORAMA_VOXELS_PER_CELL)
            {
                northHeight = (northHeight * northHeight
                             * (24 - northHeight) + 64) / 128;
                if (northHeight < 1)
                    northHeight = 1;
                if (northHeight >= DIORAMA_VOXELS_PER_CELL)
                    northHeight = DIORAMA_VOXELS_PER_CELL - 1;
            }
            if (profile == DIORAMA_TERRACE_HORIZONTAL)
                height = northHeight;
            else if (profile == DIORAMA_TERRACE_VERTICAL)
                height = eastHeight;
            else if (profile == DIORAMA_TERRACE_INNER)
                height = (eastHeight * northHeight
                        + DIORAMA_VOXELS_PER_CELL - 1)
                       / DIORAMA_VOXELS_PER_CELL;
            else if (profile == DIORAMA_TERRACE_OUTER)
                height = DIORAMA_VOXELS_PER_CELL
                       - ((DIORAMA_VOXELS_PER_CELL - eastHeight)
                        * (DIORAMA_VOXELS_PER_CELL - northHeight)
                        + DIORAMA_VOXELS_PER_CELL - 1)
                       / DIORAMA_VOXELS_PER_CELL;
            else
                height = 0;
            if (height > DIORAMA_VOXELS_PER_CELL)
                height = DIORAMA_VOXELS_PER_CELL;

            if (height > 0)
            {
                rows[row] |= 1u << x;
                heights[row * DIORAMA_VOXELS_PER_CELL + x] = height;
            }
        }
    }
}

uint64_t DioramaTerrain_ChunkSignature(const struct DioramaTerrainChunkInput *input)
{
    uint64_t hash = FNV_OFFSET;
    unsigned i;

    hash = HashU16(hash, input->chunkX);
    hash = HashU16(hash, input->chunkY);
    hash = HashU32(hash, input->rulesGeneration);
    for (i = 0; i < DIORAMA_TERRAIN_INPUT_SIZE * DIORAMA_TERRAIN_INPUT_SIZE; i++)
    {
        const struct DioramaTerrainCell *cell = &input->cells[i];

        hash = HashByte(hash, cell->present);
        hash = HashU16(hash, cell->mapX);
        hash = HashU16(hash, cell->mapY);
        hash = HashU16(hash, cell->metatileId);
        hash = HashByte(hash, cell->behavior);
        hash = HashByte(hash, cell->layerType);
        hash = HashByte(hash, cell->reflective);
        hash = HashByte(hash, cell->shape);
        hash = HashByte(hash, cell->archetype);
        hash = HashByte(hash, cell->terrainClass);
        hash = HashByte(hash, cell->semanticProfile);
        hash = HashByte(hash, cell->profile);
        hash = HashByte(hash, cell->planeAxis);
        hash = HashByte(hash, cell->effectiveElevation);
        hash = HashByte(hash, cell->surfaceCount);
        hash = HashU16(hash, cell->structureId);
        hash = HashU16(hash, cell->rulePriority);
        hash = HashU16(hash, cell->structureTemplateId);
        hash = HashU16(hash, cell->structureX);
        hash = HashU16(hash, cell->structureY);
        hash = HashByte(hash, cell->structureWidth);
        hash = HashByte(hash, cell->structureHeight);
        hash = HashByte(hash, cell->treeHullReady);
        hash = HashByte(hash, cell->treeMaskDirect);
        hash = HashByte(hash, cell->rockMass);
        hash = HashByte(hash, cell->structureRoofRows);
        hash = HashByte(hash, cell->structureLocalX);
        hash = HashByte(hash, cell->structureLocalY);
        hash = HashByte(hash, cell->southFacadeCount);
        hash = HashByte(hash, cell->measuredAxis);
        hash = HashU16(hash, cell->measuredExtentBands);
        hash = HashByte(hash, cell->measuredPeriodBands);
        hash = HashByte(hash, cell->measuredRoofBands);
        hash = HashU16(hash, cell->measuredRunLocal);
        hash = HashU16(hash, cell->measuredRunLength);
        hash = HashByte(hash, cell->measuredFlags);
        hash = HashByte(hash, cell->measuredBandCount);
        hash = HashByte(hash, cell->cliffEdgeMask);
        hash = HashByte(hash, cell->cliffBaseMask);
        hash = HashByte(hash, cell->cliffTransitionMask);
        hash = HashByte(hash, cell->cliffCornerMask);
        hash = HashByte(hash, cell->terraceProfile);
        hash = HashFloat(hash, cell->groundHeight);
        hash = HashFloat(hash, cell->visualHeight);
        hash = HashFloat(hash, cell->featureHeight);
        hash = HashFloat(hash, cell->ledgeTakeoffHeight);
        hash = HashFloat(hash, cell->ledgeLandingHeight);
        hash = HashFloat(hash, cell->structureBodyHeight);
        hash = HashFloat(hash, cell->structureRoofHeight);
        hash = HashFloat(hash, cell->southFacadeUnitHeight);
        hash = HashFloat(hash, cell->measuredConfidence);
        for (unsigned surface = 0; surface < cell->surfaceCount; surface++)
        {
            hash = HashFloat(hash, cell->surfaces[surface].bottomHeight);
            hash = HashFloat(hash, cell->surfaces[surface].topHeight);
            hash = HashByte(hash, cell->surfaces[surface].gameplayElevation);
        }
        for (unsigned face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
        {
            hash = HashU16(hash, cell->materials[face].metatileId);
            hash = HashByte(hash, cell->materials[face].layer);
            hash = HashByte(hash, cell->materials[face].rotation);
            hash = HashByte(hash, cell->materials[face].flags);
            hash = HashFloat(hash, cell->materials[face].u0);
            hash = HashFloat(hash, cell->materials[face].v0);
            hash = HashFloat(hash, cell->materials[face].u1);
            hash = HashFloat(hash, cell->materials[face].v1);
            hash = HashU16(hash, cell->underlayMaterials[face].metatileId);
            hash = HashByte(hash, cell->underlayMaterials[face].layer);
            hash = HashFloat(hash, cell->underlayMaterials[face].u0);
            hash = HashFloat(hash, cell->underlayMaterials[face].v0);
            hash = HashFloat(hash, cell->underlayMaterials[face].u1);
            hash = HashFloat(hash, cell->underlayMaterials[face].v1);
        }
        for (unsigned row = 0; row < DIORAMA_VOXELS_PER_CELL; row++)
            hash = HashU16(hash, cell->foregroundAlpha[row]);
        if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
            for (unsigned pixel = 0;
                 pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                hash = HashByte(hash, cell->mountainHeight[pixel]);
        if (cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
         || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
        {
            hash = HashU16(hash, cell->treeMaterial.metatileId);
            hash = HashByte(hash, cell->treeMaterial.layer);
            hash = HashFloat(hash, cell->treeMaterial.u0);
            hash = HashFloat(hash, cell->treeMaterial.v0);
            hash = HashFloat(hash, cell->treeMaterial.u1);
            hash = HashFloat(hash, cell->treeMaterial.v1);
            for (unsigned pixel = 0;
                 pixel < DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL; pixel++)
                hash = HashByte(hash, cell->treeShade[pixel]);
        }
        for (unsigned row = 0; row < cell->southFacadeCount; row++)
        {
            hash = HashU16(hash, cell->southFacadeMaterials[row].metatileId);
            hash = HashByte(hash, cell->southFacadeMaterials[row].layer);
        }
        for (unsigned band = 0; band < cell->measuredBandCount; band++)
        {
            hash = HashU16(hash, cell->measuredBands[band].metatileId);
            hash = HashByte(hash, cell->measuredBands[band].layer);
            hash = HashFloat(hash, cell->measuredBands[band].u0);
            hash = HashFloat(hash, cell->measuredBands[band].v0);
            hash = HashFloat(hash, cell->measuredBands[band].u1);
            hash = HashFloat(hash, cell->measuredBands[band].v1);
        }
    }
    hash = HashU32(hash, input->pixelCount);
    for (i = 0; i < input->pixelCount && i < DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES; i++)
    {
        const struct DioramaTerrainPixelPrimitive *pixel = &input->pixels[i];

        hash = HashU32(hash, pixel->xQ32);
        hash = HashU32(hash, pixel->yQ32);
        hash = HashU32(hash, pixel->zQ32);
        hash = HashU32(hash, pixel->rgba);
        hash = HashU32(hash, pixel->sourceCellOffset);
        hash = HashU16(hash, pixel->objectId);
        hash = HashU16(hash, pixel->structureId);
        hash = HashU16(hash, pixel->expectedMetatile);
        hash = HashU16(hash, pixel->expectedTileEntry);
        hash = HashByte(hash, pixel->sizeXQ32);
        hash = HashByte(hash, pixel->sizeYQ32);
        hash = HashByte(hash, pixel->sizeZQ32);
        hash = HashByte(hash, pixel->sourceLayer);
        hash = HashByte(hash, pixel->sourceX);
        hash = HashByte(hash, pixel->sourceY);
        hash = HashByte(hash, pixel->kind);
    }
    return hash;
}

static int16_t HeightToVoxel(float height)
{
    float scaled = height * DIORAMA_VOXELS_PER_CELL;

    return (int16_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

static uint16_t MaterialId(int cellIndex, int surface, int face)
{
    return (uint16_t)((cellIndex * DIORAMA_TERRAIN_MAX_SURFACES + surface)
                    * DIORAMA_MATERIAL_FACE_COUNT + face + 1);
}

/*
 * A jump ledge (the owner's Request C, 2026-10-27): a bank across its cell, as
 * its drawing has it - the grass on the side it is jumped from, the rock face
 * the GBA draws over the half on the side it lands in. The grass rises from the
 * take-off ground to a crest across the middle, DIORAMA_TERRAIN_LEDGE_RISE over
 * the higher of the two grounds, and the rock face falls from it to the
 * landing ground, a voxel a pixel as stairs climb (a ledge a level over its
 * landing: more a pixel). It was a block as high as the ledge's lip all over,
 * with the drawing squeezed on its sides: from the landing side a strip, from
 * the jumping side a wall, on a lower terrace a block a level and more high.
 * `run`: pixels from the take-off edge (0-15), the pixel's centre, rounded.
 */
static int16_t LedgeRun(int16_t takeoff, int16_t landing, int16_t crest, int run)
{
    if (run < DIORAMA_VOXELS_PER_CELL / 2)
        return takeoff + ((crest - takeoff) * (2 * run + 1) + DIORAMA_VOXELS_PER_CELL / 2)
                       / DIORAMA_VOXELS_PER_CELL;
    return landing + ((crest - landing) * (2 * (DIORAMA_VOXELS_PER_CELL - run) - 1)
                      + DIORAMA_VOXELS_PER_CELL / 2) / DIORAMA_VOXELS_PER_CELL;
}

/* A diagonal ledge, a corner of a ledge's run (the game jumps only the
 * straight ones): the higher of its two straight banks, which meets each. */
int16_t DioramaTerrain_LedgeTop(uint8_t behavior, int16_t takeoff, int16_t landing,
                                int pixelX, int pixelZ)
{
    int16_t crest = (takeoff > landing ? takeoff : landing) + DIORAMA_TERRAIN_LEDGE_RISE;
    int16_t south = LedgeRun(takeoff, landing, crest, pixelZ);
    int16_t north = LedgeRun(takeoff, landing, crest, DIORAMA_VOXELS_PER_CELL - 1 - pixelZ);
    int16_t east = LedgeRun(takeoff, landing, crest, pixelX);
    int16_t west = LedgeRun(takeoff, landing, crest, DIORAMA_VOXELS_PER_CELL - 1 - pixelX);

    switch (behavior)
    {
    case MB_JUMP_NORTH: return north;
    case MB_JUMP_EAST: return east;
    case MB_JUMP_WEST: return west;
    case MB_JUMP_NORTHEAST: return north > east ? north : east;
    case MB_JUMP_NORTHWEST: return north > west ? north : west;
    case MB_JUMP_SOUTHEAST: return south > east ? south : east;
    case MB_JUMP_SOUTHWEST: return south > west ? south : west;
    default: return south;
    }
}

int16_t DioramaTerrain_LedgeBottom(int16_t takeoff, int16_t landing)
{
    int16_t low = takeoff < landing ? takeoff : landing;

    return low > 0 ? -1 : low - 1;
}

static bool IsTreeHullColumn(const struct DioramaTerrainCell *cell);

/* A jump ledge drawn as its bank (the ground under a tree is the tree's) */
static bool IsLedgeBank(const struct DioramaTerrainCell *cell)
{
    return cell->present && cell->shape == DIORAMA_SHAPE_LEDGE && !IsTreeHullColumn(cell);
}

static int16_t LedgeBankTop(const struct DioramaTerrainCell *cell, int pixelX, int pixelZ)
{
    return DioramaTerrain_LedgeTop(cell->behavior, HeightToVoxel(cell->ledgeTakeoffHeight),
                                   HeightToVoxel(cell->ledgeLandingHeight), pixelX, pixelZ);
}

static int16_t LedgeBankBottom(const struct DioramaTerrainCell *cell)
{
    return DioramaTerrain_LedgeBottom(HeightToVoxel(cell->ledgeTakeoffHeight),
                                      HeightToVoxel(cell->ledgeLandingHeight));
}

/* Whether a side of a jump ledge's bank (axis 0: x, 2: z; sign: the way it
 * looks) looks back the way the ledge is jumped from: its grass slope's. */
static bool LedgeSideLooksBack(uint8_t behavior, uint8_t axis, int8_t sign)
{
    int dx = 0;
    int dz = 0;

    switch (behavior)
    {
    case MB_JUMP_NORTH: dz = -1; break;
    case MB_JUMP_NORTHEAST: dx = 1; dz = -1; break;
    case MB_JUMP_EAST: dx = 1; break;
    case MB_JUMP_SOUTHEAST: dx = 1; dz = 1; break;
    case MB_JUMP_SOUTHWEST: dx = -1; dz = 1; break;
    case MB_JUMP_WEST: dx = -1; break;
    case MB_JUMP_NORTHWEST: dx = -1; dz = -1; break;
    default: dz = 1; break;
    }
    return axis == 0 ? dx != 0 && sign == -dx : axis == 2 && dz != 0 && sign == -dz;
}

static int16_t StairTop(const struct DioramaTerrainCell *cell, int pixelX, int pixelZ)
{
    return HeightToVoxel(DioramaRules_ProfileHeight(
        cell->archetype, cell->behavior, cell->groundHeight, cell->featureHeight,
        pixelX, pixelZ));
}

static bool AppendSpan(uint32_t *count, const struct DioramaTerrainCell *cell,
                       int cellIndex, int pixelX, int pixelZ, int surface,
                       int16_t bottom, int16_t top)
{
    struct DioramaOccupancySpan *span;

    if (bottom >= top)
        return true;
    if (*count >= OCCUPANCY_INPUT_COLUMNS)
        return false;
    span = &sOccupancySpans[(*count)++];
    memset(span, 0, sizeof(*span));
    span->x = cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX;
    span->z = cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ;
    span->yMin = bottom;
    span->yMax = top;
    span->sourceCellX = cell->mapX;
    span->sourceCellY = cell->mapY;
    span->flags = cell->reflective;
    span->material[DIORAMA_OCCUPANCY_FACE_TOP] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_TOP);
    span->material[DIORAMA_OCCUPANCY_FACE_BOTTOM] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_TOP);
    span->material[DIORAMA_OCCUPANCY_FACE_NORTH] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_NORTH);
    span->material[DIORAMA_OCCUPANCY_FACE_EAST] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_EAST);
    span->material[DIORAMA_OCCUPANCY_FACE_SOUTH] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_SOUTH);
    span->material[DIORAMA_OCCUPANCY_FACE_WEST] = MaterialId(cellIndex, surface, DIORAMA_MATERIAL_FACE_WEST);
    return true;
}

static bool BuildOccupancy(const struct DioramaTerrainChunkInput *input,
                           uint32_t *spanCount)
{
    uint32_t count = 0;
    int cellY;
    int cellX;

    for (cellY = 0; cellY < DIORAMA_TERRAIN_INPUT_SIZE; cellY++)
    {
        for (cellX = 0; cellX < DIORAMA_TERRAIN_INPUT_SIZE; cellX++)
        {
            int cellIndex = cellY * DIORAMA_TERRAIN_INPUT_SIZE + cellX;
            const struct DioramaTerrainCell *cell = &input->cells[cellIndex];
            int pixelY;
            int pixelX;

            if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
                continue;
            for (pixelY = 0; pixelY < DIORAMA_VOXELS_PER_CELL; pixelY++)
            {
                for (pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
                {
                    float topHeight = cell->visualHeight;
                    int16_t top;
                    int16_t bottom;
                    int16_t ground = HeightToVoxel(cell->groundHeight);

                    top = HeightToVoxel(topHeight);

                    if (((cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                       || cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
                      && cell->treeHullReady)
                     || (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                      && cell->structureWidth == 2))
                    {
                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY,
                                        1, ground - 1, ground))
                            return false;
                        continue;
                    }
                    if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                    {
                        uint8_t mountainHeight = cell->mountainHeight[
                            pixelY * DIORAMA_VOXELS_PER_CELL + pixelX];
                        bool raised = mountainHeight != 0;
                        int16_t mountainTop = ground + mountainHeight;

                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY,
                                        raised ? 0 : 1,
                                        ground - 1,
                                        raised ? mountainTop : ground))
                            return false;
                        continue;
                    }
                    if (!DioramaRules_ProfileOccupies(cell->semanticProfile, pixelX, pixelY))
                        continue;
                    if ((cell->measuredFlags & DIORAMA_MEASURED_SILHOUETTE)
                     && !(cell->foregroundAlpha[pixelY] & (1u << pixelX)))
                        continue;
                    if (cell->shape == DIORAMA_SHAPE_ROOF && cell->structureId != 0)
                    {
                        topHeight = ResolveRoofHeight(cell, pixelX, pixelY);
                    }
                    top = HeightToVoxel(topHeight);
                    bottom = top > 0 ? -1 : top - 1;
                    if (cell->shape == DIORAMA_SHAPE_BRIDGE)
                    {
                        if (cell->surfaceCount > 1)
                        {
                            uint8_t surface;

                            for (surface = 0; surface < cell->surfaceCount; surface++)
                                if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, surface,
                                        HeightToVoxel(cell->surfaces[surface].bottomHeight),
                                        HeightToVoxel(cell->surfaces[surface].topHeight)))
                                    return false;
                        }
                        else if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 1, -1, 0)
                              || !AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, top - 1, top))
                            return false;
                    }
                    else if (cell->shape == DIORAMA_SHAPE_LEDGE)
                    {
                        /* a jump ledge's bank, from under the lower of its grounds */
                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0,
                                        LedgeBankBottom(cell), LedgeBankTop(cell, pixelX, pixelY)))
                            return false;
                    }
                    else if (cell->shape == DIORAMA_SHAPE_STAIRS)
                    {
                        bool descending = cell->archetype >= DIORAMA_ARCHETYPE_STAIRS_DOWN_N
                                       && cell->archetype <= DIORAMA_ARCHETYPE_STAIRS_DOWN_W;
                        int16_t stairGround = HeightToVoxel(cell->groundHeight);

                        top = StairTop(cell, pixelX, pixelY);
                        /* stairs up from a terrace's lower level stand on it,
                         * a voxel under it as the ground there is (from -1
                         * they were no column at all: a hole where they are
                         * drawn) */
                        bottom = descending
                               ? HeightToVoxel(cell->groundHeight -
                                     (cell->featureHeight != 0.0f ? cell->featureHeight : 1.0f)) - 1
                               : (stairGround > 0 ? -1 : stairGround - 1);
                        if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, bottom, top))
                            return false;
                    }
                    else if (!AppendSpan(&count, cell, cellIndex, pixelX, pixelY, 0, bottom, top))
                        return false;
                }
            }
        }
    }
    if (!DioramaOccupancy_Canonicalize(sOccupancySpans, &count))
        return false;
    *spanCount = count;
    return true;
}

/* A face of the compressed shell, reading the cell's materials of a surface
 * (BuildOccupancy's spans: 0 its own, 1 the ground under a tree). */
static bool AppendCompressedSurfaceFace(uint32_t *faceCount, uint32_t *rawFaceCount,
                                        const struct DioramaTerrainCell *cell, int cellIndex,
                                        int surface, int materialFace,
                                        int32_t plane, int32_t uMin, int32_t uMax,
                                        int16_t vMin, int16_t vMax, uint8_t axis, int8_t sign,
                                        uint32_t rawWeight)
{
    struct DioramaShellFace *face;

    if (*faceCount >= DIORAMA_TERRAIN_MAX_FACES || vMin >= vMax)
        return vMin >= vMax;
    face = &sShellFaces[(*faceCount)++];
    face->plane = plane;
    face->uMin = uMin;
    face->uMax = uMax;
    face->vMin = vMin;
    face->vMax = vMax;
    face->sourceCellX = cell->mapX;
    face->sourceCellY = cell->mapY;
    face->material = MaterialId(cellIndex, surface, materialFace);
    face->flags = cell->reflective;
    face->axis = axis;
    face->sign = sign;
    *rawFaceCount += rawWeight;
    return true;
}

static bool AppendCompressedFace(uint32_t *faceCount, uint32_t *rawFaceCount,
                                 const struct DioramaTerrainCell *cell, int cellIndex,
                                 uint8_t occupancyFace, int materialFace,
                                 int32_t plane, int32_t uMin, int32_t uMax,
                                 int16_t vMin, int16_t vMax, uint8_t axis, int8_t sign,
                                 uint32_t rawWeight)
{
    (void)occupancyFace;
    return AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0, materialFace,
                                       plane, uMin, uMax, vMin, vMax, axis, sign, rawWeight);
}

/*
 * A whole cell's column in the compressed shell: the voxels it fills,
 * [*bottom, *top), and the surface its faces read - BuildOccupancy's span for
 * every pixel of it. The ground under a tree (its hull or the voxel model is
 * drawn on its own) is a voxel thick under its ground and reads the ground's
 * materials (performance work 2026-10-24: a chunk with a tree in it went
 * through a span per pixel - 25,600 of them for the same 64 tops and
 * bottoms - about 15 ms a chunk, two chunks a frame while running through
 * Petalburg Woods).
 */
static void CompressedColumn(const struct DioramaTerrainCell *cell, int16_t *bottom, int16_t *top,
                             int *surface)
{
    if (IsTreeHullColumn(cell))
    {
        *top = HeightToVoxel(cell->groundHeight);
        *bottom = *top - 1;
        *surface = 1;
        return;
    }
    *top = HeightToVoxel(cell->visualHeight);
    *bottom = *top > 0 ? -1 : *top - 1;
    *surface = 0;
}

static bool AppendCompressedSide(uint32_t *faceCount, uint32_t *rawFaceCount,
                                 const struct DioramaTerrainCell *cell, int cellIndex,
                                 const struct DioramaTerrainCell *neighbor,
                                 int surface, int materialFace,
                                 int32_t plane, int32_t uMin, int32_t uMax,
                                 uint8_t axis, int8_t sign,
                                 int16_t bottom, int16_t top)
{
    int16_t neighborBottom;
    int16_t neighborTop;
    int neighborSurface;

    if (!neighbor->present || neighbor->shape == DIORAMA_SHAPE_HIDDEN)
        return AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex,
                                           surface, materialFace, plane, uMin, uMax,
                                           bottom, top, axis, sign, DIORAMA_VOXELS_PER_CELL);
    CompressedColumn(neighbor, &neighborBottom, &neighborTop, &neighborSurface);
    if (neighborTop <= bottom || neighborBottom >= top)
        return AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex,
                                           surface, materialFace, plane, uMin, uMax,
                                           bottom, top, axis, sign, DIORAMA_VOXELS_PER_CELL);
    if (neighborBottom > bottom
     && !AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex,
                                     surface, materialFace, plane, uMin, uMax,
                                     bottom, neighborBottom < top ? neighborBottom : top,
                                     axis, sign, DIORAMA_VOXELS_PER_CELL))
        return false;
    if (neighborTop < top
     && !AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex,
                                     surface, materialFace, plane, uMin, uMax,
                                     neighborTop > bottom ? neighborTop : bottom, top,
                                     axis, sign, DIORAMA_VOXELS_PER_CELL))
        return false;
    return true;
}

/* A pixel's column in the compressed shell, as BuildOccupancy's span there: a
 * jump ledge's bank, or its whole cell's. */
static void CompressedPixelColumn(const struct DioramaTerrainCell *cell, int pixelX, int pixelZ,
                                  int16_t *bottom, int16_t *top, int *surface)
{
    if (IsLedgeBank(cell))
    {
        *bottom = LedgeBankBottom(cell);
        *top = LedgeBankTop(cell, pixelX, pixelZ);
        *surface = 0;
        return;
    }
    CompressedColumn(cell, bottom, top, surface);
}

/* One pixel's side (0-3: north, east, south, west) of the column [bottom, top)
 * at voxel column (voxelX, voxelZ), as BuildShell's AppendSideGaps draws it:
 * where the column beside it, [neighborBottom, neighborTop) (none: `open`),
 * leaves it showing. A face a pixel wide: DioramaOccupancy_MergeFaces joins
 * them as it joins the voxel shell's. */
static bool AppendCompressedPixelSide(uint32_t *faceCount, uint32_t *rawFaceCount,
                                      const struct DioramaTerrainCell *cell, int cellIndex,
                                      int surface, int side, int32_t voxelX, int32_t voxelZ,
                                      int16_t bottom, int16_t top, bool open,
                                      int16_t neighborBottom, int16_t neighborTop)
{
    uint8_t axis = side == 1 || side == 3 ? 0 : 2;
    int8_t sign = side == 0 || side == 3 ? -1 : 1;
    int32_t plane = axis == 0 ? voxelX + (sign > 0) : voxelZ + (sign > 0);
    int32_t u = axis == 0 ? voxelZ : voxelX;

    if (open || neighborTop <= bottom || neighborBottom >= top)
        return AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, surface,
                                           side + 1, plane, u, u + 1, bottom, top, axis, sign, 1);
    if (neighborBottom > bottom
     && !AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, surface,
                                     side + 1, plane, u, u + 1, bottom,
                                     neighborBottom < top ? neighborBottom : top, axis, sign, 1))
        return false;
    if (neighborTop < top
     && !AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, surface,
                                     side + 1, plane, u, u + 1,
                                     neighborTop > bottom ? neighborTop : bottom, top, axis, sign, 1))
        return false;
    return true;
}

/* The column beside pixel (pixelX, pixelZ) of the cell at (x, y) of the input,
 * on side `side`: the cell's own or its neighbour's (false: none there). */
static bool CompressedColumnBeside(const struct DioramaTerrainChunkInput *input, int x, int y,
                                   int pixelX, int pixelZ, int side,
                                   int16_t *bottom, int16_t *top)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    int nextX = pixelX + offsets[side][0];
    int nextZ = pixelZ + offsets[side][1];
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int surface;

    if (nextX < 0 || nextZ < 0 || nextX >= DIORAMA_VOXELS_PER_CELL || nextZ >= DIORAMA_VOXELS_PER_CELL)
    {
        cell = GetCell(input, x + offsets[side][0], y + offsets[side][1]);
        if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
            return false;
        nextX = (nextX + DIORAMA_VOXELS_PER_CELL) % DIORAMA_VOXELS_PER_CELL;
        nextZ = (nextZ + DIORAMA_VOXELS_PER_CELL) % DIORAMA_VOXELS_PER_CELL;
    }
    CompressedPixelColumn(cell, nextX, nextZ, bottom, top, &surface);
    return true;
}

/* The sides of a jump ledge's bank on its cell's edges, a pixel at a time
 * (the columns beside them need not be level along them). */
static bool AppendCompressedBankEdges(const struct DioramaTerrainChunkInput *input, int x, int y,
                                      uint32_t *faceCount, uint32_t *rawFaceCount)
{
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    int16_t bottom = LedgeBankBottom(cell);
    int along;
    int side;

    for (side = 0; side < 4; side++)
        for (along = 0; along < DIORAMA_VOXELS_PER_CELL; along++)
        {
            int pixelX = side == 1 ? DIORAMA_VOXELS_PER_CELL - 1 : side == 3 ? 0 : along;
            int pixelZ = side == 0 ? 0 : side == 2 ? DIORAMA_VOXELS_PER_CELL - 1 : along;
            int16_t neighborBottom = 0;
            int16_t neighborTop = 0;
            bool open = !CompressedColumnBeside(input, x, y, pixelX, pixelZ, side,
                                                &neighborBottom, &neighborTop);

            if (!AppendCompressedPixelSide(faceCount, rawFaceCount, cell, cellIndex, 0, side,
                                           cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX,
                                           cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ,
                                           bottom, LedgeBankTop(cell, pixelX, pixelZ), open,
                                           neighborBottom, neighborTop))
                return false;
        }
    return true;
}

/*
 * A straight jump ledge's bank in the compressed shell: its height changes
 * one way only (down its rows jumped north or south, along its columns jumped
 * east or west), so between two rows or columns its side is a strip, and
 * jumped north or south each row's top is one - the faces
 * DioramaOccupancy_MergeFaces makes of its spans' (a bank a pixel at a time
 * was about 1.8 ms a chunk of ledges). Jumped east or west its tops stay a
 * pixel each: the merge joins a row's pixels first, then rows only where
 * nothing else of that height lies in them, and a column's height is the
 * same as the one across the crest from it, so it never joins them.
 */
static bool AppendCompressedStraightBank(const struct DioramaTerrainChunkInput *input, int x, int y,
                                         uint32_t *faceCount, uint32_t *rawFaceCount)
{
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    bool rows = cell->behavior == MB_JUMP_NORTH || cell->behavior == MB_JUMP_SOUTH;
    int32_t originX = cell->mapX * DIORAMA_VOXELS_PER_CELL;
    int32_t originZ = cell->mapY * DIORAMA_VOXELS_PER_CELL;
    int16_t bottom = LedgeBankBottom(cell);
    int16_t tops[DIORAMA_VOXELS_PER_CELL];
    int i;

    for (i = 0; i < DIORAMA_VOXELS_PER_CELL; i++)
        tops[i] = rows ? LedgeBankTop(cell, 0, i) : LedgeBankTop(cell, i, 0);
    if (!AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                     DIORAMA_MATERIAL_FACE_TOP, bottom,
                                     originX, originX + DIORAMA_VOXELS_PER_CELL,
                                     originZ, originZ + DIORAMA_VOXELS_PER_CELL, 1, -1,
                                     DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL))
        return false;
    for (i = 0; i < DIORAMA_VOXELS_PER_CELL; i++)
    {
        int32_t uMin = rows ? originX : originX + i;
        int32_t uMax = rows ? originX + DIORAMA_VOXELS_PER_CELL : originX + i + 1;
        int32_t vMin = rows ? originZ + i : originZ;
        int32_t vMax = rows ? originZ + i + 1 : originZ + DIORAMA_VOXELS_PER_CELL;

        if (rows)
        {
            if (!AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                             DIORAMA_MATERIAL_FACE_TOP, tops[i], uMin, uMax,
                                             vMin, vMax, 1, 1, DIORAMA_VOXELS_PER_CELL))
                return false;
        }
        else
        {
            int32_t z;

            for (z = vMin; z < vMax; z++)
                if (!AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                                 DIORAMA_MATERIAL_FACE_TOP, tops[i], uMin, uMax,
                                                 z, z + 1, 1, 1, 1))
                    return false;
        }
        if (i + 1 == DIORAMA_VOXELS_PER_CELL || tops[i] == tops[i + 1])
            continue;
        /* the step to the next row or column, on the higher one's side:
         * north and south sides run along x, east and west ones along z */
        if (!AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                         rows ? (tops[i] > tops[i + 1] ? 3 : 1)
                                              : (tops[i] > tops[i + 1] ? 2 : 4),
                                         rows ? originZ + i + 1 : originX + i + 1,
                                         rows ? originX : originZ,
                                         (rows ? originX : originZ) + DIORAMA_VOXELS_PER_CELL,
                                         tops[i] < tops[i + 1] ? tops[i] : tops[i + 1],
                                         tops[i] < tops[i + 1] ? tops[i + 1] : tops[i],
                                         rows ? 2 : 0, tops[i] > tops[i + 1] ? 1 : -1,
                                         DIORAMA_VOXELS_PER_CELL))
            return false;
    }
    return AppendCompressedBankEdges(input, x, y, faceCount, rawFaceCount);
}

/* A jump ledge's bank in the compressed shell: a pixel's top, underside and
 * sides at a time, the faces BuildShell draws for its spans (a diagonal one,
 * a corner, changes both ways). */
static bool AppendCompressedLedgeBank(const struct DioramaTerrainChunkInput *input, int x, int y,
                                      uint32_t *faceCount, uint32_t *rawFaceCount)
{
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    int16_t bottom = LedgeBankBottom(cell);
    int pixelZ;
    int pixelX;
    int side;

    if (cell->behavior == MB_JUMP_NORTH || cell->behavior == MB_JUMP_SOUTH
     || cell->behavior == MB_JUMP_EAST || cell->behavior == MB_JUMP_WEST)
        return AppendCompressedStraightBank(input, x, y, faceCount, rawFaceCount);
    for (pixelZ = 0; pixelZ < DIORAMA_VOXELS_PER_CELL; pixelZ++)
        for (pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
        {
            int32_t voxelX = cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX;
            int32_t voxelZ = cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ;
            int16_t top = LedgeBankTop(cell, pixelX, pixelZ);

            if (!AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                             DIORAMA_MATERIAL_FACE_TOP, top, voxelX, voxelX + 1,
                                             voxelZ, voxelZ + 1, 1, 1, 1)
             || !AppendCompressedSurfaceFace(faceCount, rawFaceCount, cell, cellIndex, 0,
                                             DIORAMA_MATERIAL_FACE_TOP, bottom, voxelX, voxelX + 1,
                                             voxelZ, voxelZ + 1, 1, -1, 1))
                return false;
            for (side = 0; side < 4; side++)
            {
                int16_t neighborBottom = 0;
                int16_t neighborTop = 0;
                bool open = !CompressedColumnBeside(input, x, y, pixelX, pixelZ, side,
                                                    &neighborBottom, &neighborTop);

                if (!AppendCompressedPixelSide(faceCount, rawFaceCount, cell, cellIndex, 0, side,
                                               voxelX, voxelZ, bottom, top, open,
                                               neighborBottom, neighborTop))
                    return false;
            }
        }
    return true;
}

/* A whole cell's side beside a jump ledge's bank: a pixel at a time, the
 * bank's height changing along it. */
static bool AppendCompressedSideByBank(const struct DioramaTerrainChunkInput *input, int x, int y,
                                       int side, int surface, int16_t bottom, int16_t top,
                                       uint32_t *faceCount, uint32_t *rawFaceCount)
{
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    int along;

    for (along = 0; along < DIORAMA_VOXELS_PER_CELL; along++)
    {
        int pixelX = side == 1 ? DIORAMA_VOXELS_PER_CELL - 1 : side == 3 ? 0 : along;
        int pixelZ = side == 0 ? 0 : side == 2 ? DIORAMA_VOXELS_PER_CELL - 1 : along;
        int16_t neighborBottom = 0;
        int16_t neighborTop = 0;
        bool open = !CompressedColumnBeside(input, x, y, pixelX, pixelZ, side,
                                            &neighborBottom, &neighborTop);

        if (!AppendCompressedPixelSide(faceCount, rawFaceCount, cell, cellIndex, surface, side,
                                       cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX,
                                       cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ,
                                       bottom, top, open, neighborBottom, neighborTop))
            return false;
    }
    return true;
}

static bool BuildCompressedOccupancyShell(const struct DioramaTerrainChunkInput *input,
                                          uint32_t *spanCount, uint32_t *faceCount,
                                          uint32_t *rawFaceCount)
{
    uint32_t spans = 0;
    uint32_t faces = 0;
    uint32_t rawFaces = 0;
    int y;
    int x;

    for (y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            if (cell->present && cell->shape != DIORAMA_SHAPE_HIDDEN)
            {
                /* the ground under a tree: a whole column whatever else it is */
                if (IsTreeHullColumn(cell))
                {
                    spans += DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
                    continue;
                }
                if (cell->shape == DIORAMA_SHAPE_ROOF
                 || cell->shape == DIORAMA_SHAPE_STAIRS
                 || cell->shape == DIORAMA_SHAPE_BRIDGE
                 || cell->surfaceCount > 1
                  || !DioramaRules_ProfileIsFullCell(cell->semanticProfile)
                   || (cell->measuredFlags & (DIORAMA_MEASURED_SILHOUETTE
                                            | DIORAMA_MEASURED_MOUNTAIN_ART))
                  || cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL
                  || cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
                    return false;
                spans += DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL;
            }
        }
    for (y = DIORAMA_TERRAIN_HALO;
         y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; y++)
    {
        for (x = DIORAMA_TERRAIN_HALO;
             x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
            int32_t pixelX;
            int32_t pixelZ;
            int16_t top;
            int16_t bottom;
            int surface;
            int side;

            if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
                continue;
            if (IsLedgeBank(cell))
            {
                if (!AppendCompressedLedgeBank(input, x, y, &faces, &rawFaces))
                    return false;
                continue;
            }
            pixelX = cell->mapX * DIORAMA_VOXELS_PER_CELL;
            pixelZ = cell->mapY * DIORAMA_VOXELS_PER_CELL;
            CompressedColumn(cell, &bottom, &top, &surface);
            if (!AppendCompressedSurfaceFace(&faces, &rawFaces, cell, cellIndex, surface,
                                             DIORAMA_MATERIAL_FACE_TOP, top,
                                             pixelX, pixelX + DIORAMA_VOXELS_PER_CELL,
                                             pixelZ, pixelZ + DIORAMA_VOXELS_PER_CELL,
                                             1, 1, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL)
             || !AppendCompressedSurfaceFace(&faces, &rawFaces, cell, cellIndex, surface,
                                             DIORAMA_MATERIAL_FACE_TOP, bottom,
                                             pixelX, pixelX + DIORAMA_VOXELS_PER_CELL,
                                             pixelZ, pixelZ + DIORAMA_VOXELS_PER_CELL,
                                             1, -1, DIORAMA_VOXELS_PER_CELL * DIORAMA_VOXELS_PER_CELL))
                return false;
            for (side = 0; side < 4; side++)
            {
                const struct DioramaTerrainCell *neighbor =
                    GetCell(input, x + offsets[side][0], y + offsets[side][1]);
                uint8_t axis = side == 1 || side == 3 ? 0 : 2;
                int8_t sign = side == 0 || side == 3 ? -1 : 1;
                int32_t plane = axis == 0 ? pixelX + (sign > 0) * DIORAMA_VOXELS_PER_CELL
                                          : pixelZ + (sign > 0) * DIORAMA_VOXELS_PER_CELL;
                int32_t uMin = axis == 0 ? pixelZ : pixelX;

                if (IsLedgeBank(neighbor))
                {
                    if (!AppendCompressedSideByBank(input, x, y, side, surface, bottom, top,
                                                    &faces, &rawFaces))
                        return false;
                }
                else if (!AppendCompressedSide(&faces, &rawFaces, cell, cellIndex, neighbor,
                                               surface, side + 1, plane,
                                               uMin, uMin + DIORAMA_VOXELS_PER_CELL,
                                               axis, sign, bottom, top))
                    return false;
            }
        }
    }
    *spanCount = spans;
    *faceCount = faces;
    *rawFaceCount = rawFaces;
    return true;
}

static bool IsTreeHullColumn(const struct DioramaTerrainCell *cell)
{
    return ((cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
          || cell->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
         && cell->treeHullReady)
        || (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL && cell->structureWidth == 2);
}

/* A cell drawn as one column per pixel up to one top: ground, water, a
 * raised block, a building's cell, the ground under a tree (not a roof,
 * stairs, a bridge, a ledge, a cliff, a cut-out or mountain art, which draw
 * their own sides). */
static bool StandsWhole(const struct DioramaTerrainCell *cell)
{
    if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
        return false;
    if (IsTreeHullColumn(cell))
        return true;
    if (cell->surfaceCount > 1
     || (cell->measuredFlags & (DIORAMA_MEASURED_SILHOUETTE | DIORAMA_MEASURED_MOUNTAIN_ART))
     || !DioramaRules_ProfileIsFullCell(cell->semanticProfile))
        return false;
    return cell->shape == DIORAMA_SHAPE_FLAT || cell->shape == DIORAMA_SHAPE_WATER
        || cell->shape == DIORAMA_SHAPE_EXTRUDED || cell->shape == DIORAMA_SHAPE_BUILDING_PART;
}

/* A whole cell's top (under a tree: its ground). */
static int16_t WholeTop(const struct DioramaTerrainCell *cell)
{
    return HeightToVoxel(IsTreeHullColumn(cell) ? cell->groundHeight : cell->visualHeight);
}

/* How far down a cell's own sides reach, for a cell a skirt may hang from:
 * a whole cell, or one standing on its whole cell with sides of its own (a
 * cliff, a ledge, a roof, mountain art) - a slab under anything at or above
 * the ground (under a tree or mountain art: under its ground), a voxel under
 * a cell below it; stairs up, as their column (BuildOccupancy) - the Abandoned
 * Ship's on Route 108, their ground its deck's, a level over the sea round
 * them, stood on nothing with the sky under them (step 16, the world geometry
 * overhaul). Anything else (stairs down, a bridge, a part of a cell): no skirt
 * (INT16_MIN). */
static int16_t SkirtFooting(const struct DioramaTerrainCell *cell)
{
    int16_t top;

    if (!cell->present || cell->shape == DIORAMA_SHAPE_HIDDEN)
        return INT16_MIN;
    if (IsTreeHullColumn(cell))
        return HeightToVoxel(cell->groundHeight) - 1;
    if (cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
        return HeightToVoxel(cell->groundHeight) - 1;
    if (cell->shape == DIORAMA_SHAPE_STAIRS)
    {
        if (cell->archetype >= DIORAMA_ARCHETYPE_STAIRS_DOWN_N
         && cell->archetype <= DIORAMA_ARCHETYPE_STAIRS_DOWN_W)
            return INT16_MIN;
        top = HeightToVoxel(cell->groundHeight);
        return top > 0 ? -1 : top - 1;
    }
    if (!StandsWhole(cell)
     && !((cell->shape == DIORAMA_SHAPE_CLIFF || cell->shape == DIORAMA_SHAPE_LEDGE
        || cell->shape == DIORAMA_SHAPE_ROOF)
       && cell->surfaceCount <= 1
       && !(cell->measuredFlags & DIORAMA_MEASURED_SILHOUETTE)
       && DioramaRules_ProfileIsFullCell(cell->semanticProfile)))
        return INT16_MIN;
    if (IsLedgeBank(cell))
        return LedgeBankBottom(cell);
    top = HeightToVoxel(cell->visualHeight);
    return top > 0 ? -1 : top - 1;
}

/* A skirt over a jump ledge's bank (AppendSkirtFaces): a pixel at a time, from
 * the bank's top up to the cell's footing, of the cell's ground. */
static bool AppendSkirtByBank(const struct DioramaTerrainChunkInput *input, int x, int y,
                              int side, int16_t footing,
                              uint32_t *faceCount, uint32_t *rawFaceCount)
{
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    int along;

    for (along = 0; along < DIORAMA_VOXELS_PER_CELL; along++)
    {
        int pixelX = side == 1 ? DIORAMA_VOXELS_PER_CELL - 1 : side == 3 ? 0 : along;
        int pixelZ = side == 0 ? 0 : side == 2 ? DIORAMA_VOXELS_PER_CELL - 1 : along;
        int16_t bankBottom;
        int16_t bankTop;

        if (!CompressedColumnBeside(input, x, y, pixelX, pixelZ, side, &bankBottom, &bankTop)
         || bankTop >= footing)
            continue;
        if (!AppendCompressedPixelSide(faceCount, rawFaceCount, cell, cellIndex, 1, side,
                                       cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX,
                                       cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ,
                                       bankTop, footing, true, 0, 0))
            return false;
    }
    return true;
}

/* A skirt over a mountain's rock (AppendSkirtFaces): a pixel at a time, from the top of the rock's column across the
 * edge up to the cell's footing, of the cell's ground. The rock's columns draw their sides only where they stand over
 * what is beside them, and the cell's own sides reach down only to its footing, so where the rock lay lower than the
 * cell the sky showed between them: the faces of the rock bands, a little under the plateaus they are the faces of at
 * their rounded corners (step 16, the world geometry overhaul: rock_bands.py). */
static bool AppendSkirtByRock(const struct DioramaTerrainChunkInput *input, int x, int y,
                              int side, int16_t footing,
                              uint32_t *faceCount, uint32_t *rawFaceCount)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const struct DioramaTerrainCell *cell = GetCell(input, x, y);
    const struct DioramaTerrainCell *rock = GetCell(input, x + offsets[side][0], y + offsets[side][1]);
    int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
    int16_t ground = HeightToVoxel(rock->groundHeight);
    int along;

    for (along = 0; along < DIORAMA_VOXELS_PER_CELL; along++)
    {
        int pixelX = side == 1 ? DIORAMA_VOXELS_PER_CELL - 1 : side == 3 ? 0 : along;
        int pixelZ = side == 0 ? 0 : side == 2 ? DIORAMA_VOXELS_PER_CELL - 1 : along;
        /* the rock's pixel across the edge */
        int rockX = side == 1 ? 0 : side == 3 ? DIORAMA_VOXELS_PER_CELL - 1 : along;
        int rockZ = side == 0 ? DIORAMA_VOXELS_PER_CELL - 1 : side == 2 ? 0 : along;
        uint8_t height = rock->mountainHeight[rockZ * DIORAMA_VOXELS_PER_CELL + rockX];
        int16_t rockTop = height != 0 ? ground + height : ground;

        if (rockTop >= footing)
            continue;
        if (!AppendCompressedPixelSide(faceCount, rawFaceCount, cell, cellIndex, 1, side,
                                       cell->mapX * DIORAMA_VOXELS_PER_CELL + pixelX,
                                       cell->mapY * DIORAMA_VOXELS_PER_CELL + pixelZ,
                                       rockTop, footing, true, 0, 0))
            return false;
    }
    return true;
}

/*
 * Skirts: a cell beside a lower whole cell gets a side from that cell's
 * top up to its own footing (SkirtFooting), closing the step between them. The
 * rules can leave neighbours a level apart with nothing else drawn there -
 * the sea solved a terrace below a sandbar or a rock's cell, a row of trees
 * left on the level above the walk beside it - and the gap under the higher
 * cell's footing showed the sky (graphics overhaul, 2026-10-12). The side is
 * the higher cell's ground (the lower layer of its drawing).
 */
static bool AppendSkirtFaces(const struct DioramaTerrainChunkInput *input,
                             uint32_t *faceCount, uint32_t *rawFaceCount)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    int y;
    int x;

    for (y = DIORAMA_TERRAIN_HALO;
         y < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; y++)
        for (x = DIORAMA_TERRAIN_HALO;
             x < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);
            int cellIndex = y * DIORAMA_TERRAIN_INPUT_SIZE + x;
            int16_t footing;
            int side;

            footing = SkirtFooting(cell);
            if (footing == INT16_MIN)
                continue;
            for (side = 0; side < 4; side++)
            {
                const struct DioramaTerrainCell *neighbor =
                    GetCell(input, x + offsets[side][0], y + offsets[side][1]);
                uint8_t axis = side == 1 || side == 3 ? 0 : 2;
                int8_t sign = side == 0 || side == 3 ? -1 : 1;
                int32_t pixelX = cell->mapX * DIORAMA_VOXELS_PER_CELL;
                int32_t pixelZ = cell->mapY * DIORAMA_VOXELS_PER_CELL;
                int32_t plane = axis == 0 ? pixelX + (sign > 0) * DIORAMA_VOXELS_PER_CELL
                                          : pixelZ + (sign > 0) * DIORAMA_VOXELS_PER_CELL;
                int32_t uMin = axis == 0 ? pixelZ : pixelX;
                int16_t neighborTop;

                /* down to a whole cell's top, or a jump ledge's bank's (on a
                 * lower level), a pixel at a time */
                if (IsLedgeBank(neighbor) && neighbor->surfaceCount <= 1)
                {
                    if (!AppendSkirtByBank(input, x, y, side, footing, faceCount, rawFaceCount))
                        return false;
                    continue;
                }
                /* down to a mountain's rock, a pixel at a time (the rock's own cells on its ground: one mass,
                   their sides their own; rock on a lower ground is another, the walls of a pit beside it - Mt
                   Pyre's summit, its west side, showed the sky between them) */
                if (neighbor->present && neighbor->shape != DIORAMA_SHAPE_HIDDEN
                 && (neighbor->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART) && !IsTreeHullColumn(neighbor)
                 && (!(cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                     || neighbor->groundHeight < cell->groundHeight))
                {
                    if (!AppendSkirtByRock(input, x, y, side, footing, faceCount, rawFaceCount))
                        return false;
                    continue;
                }
                if (StandsWhole(neighbor))
                    neighborTop = WholeTop(neighbor);
                /* the water under a bridge, its lower surface (buildings.c LevelUnderBridges lays it with the water
                   round it, which can lie lower than the bank: Meteor Falls' bridge, its cave pool sunk) */
                else if (neighbor->present && neighbor->shape == DIORAMA_SHAPE_BRIDGE && neighbor->surfaceCount > 1)
                    neighborTop = HeightToVoxel(neighbor->surfaces[1].topHeight);
                else
                    continue;
                if (neighborTop >= footing)
                    continue;
                if (!AppendCompressedFace(faceCount, rawFaceCount, cell, cellIndex,
                                          DIORAMA_OCCUPANCY_FACE_NORTH + side, side + 1,
                                          plane, uMin, uMin + DIORAMA_VOXELS_PER_CELL,
                                          neighborTop, footing, axis, sign,
                                          DIORAMA_VOXELS_PER_CELL))
                    return false;
                /* the cell's ground under it all (its drawing's lower layer):
                 * a ledge's sides, its lip on the upper layer, are see-through */
                sShellFaces[*faceCount - 1].material = MaterialId(cellIndex, 1, side + 1);
            }
        }
    return true;
}

static const struct DioramaTerrainCell *FaceSourceCell(
    const struct DioramaTerrainChunkInput *input, const struct DioramaShellFace *face)
{
    int x = face->sourceCellX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = face->sourceCellY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    return GetCell(input, x, y);
}

static float VoxelX(int32_t value)
{
    return (value - DIORAMA_VOXELS_PER_CELL / 2) / (float)DIORAMA_VOXELS_PER_CELL;
}

static float VoxelY(int32_t value)
{
    return value / (float)DIORAMA_VOXELS_PER_CELL;
}

static float VoxelZ(int32_t value)
{
    return -(value - DIORAMA_VOXELS_PER_CELL / 2) / (float)DIORAMA_VOXELS_PER_CELL;
}

static void FacePositions(const struct DioramaShellFace *face, float positions[4][3])
{
    if (face->axis == 1)
    {
        float left = VoxelX(face->uMin);
        float right = VoxelX(face->uMax);
        float north = VoxelZ(face->vMin);
        float south = VoxelZ(face->vMax);
        float height = VoxelY(face->plane);

        positions[0][0] = left;  positions[0][1] = height; positions[0][2] = north;
        positions[1][0] = right; positions[1][1] = height; positions[1][2] = north;
        positions[2][0] = right; positions[2][1] = height; positions[2][2] = south;
        positions[3][0] = left;  positions[3][1] = height; positions[3][2] = south;
        if (face->sign < 0)
        {
            float temporary[3];
            memcpy(temporary, positions[1], sizeof(temporary));
            memcpy(positions[1], positions[3], sizeof(temporary));
            memcpy(positions[3], temporary, sizeof(temporary));
        }
    }
    else if (face->axis == 0)
    {
        float x = VoxelX(face->plane);
        float north = VoxelZ(face->uMin);
        float south = VoxelZ(face->uMax);
        float bottom = VoxelY(face->vMin);
        float top = VoxelY(face->vMax);

        positions[0][0] = x; positions[0][1] = top;    positions[0][2] = south;
        positions[1][0] = x; positions[1][1] = top;    positions[1][2] = north;
        positions[2][0] = x; positions[2][1] = bottom; positions[2][2] = north;
        positions[3][0] = x; positions[3][1] = bottom; positions[3][2] = south;
    }
    else
    {
        float left = VoxelX(face->uMin);
        float right = VoxelX(face->uMax);
        float z = VoxelZ(face->plane);
        float bottom = VoxelY(face->vMin);
        float top = VoxelY(face->vMax);

        positions[0][0] = left;  positions[0][1] = top;    positions[0][2] = z;
        positions[1][0] = right; positions[1][1] = top;    positions[1][2] = z;
        positions[2][0] = right; positions[2][1] = bottom; positions[2][2] = z;
        positions[3][0] = left;  positions[3][1] = bottom; positions[3][2] = z;
    }
}

#define TREE_HULL_MAX_SIZE (DIORAMA_VOXELS_PER_CELL * 2)
#define TREE_HULL_MAX_PIXELS (TREE_HULL_MAX_SIZE * TREE_HULL_MAX_SIZE)

struct TreeHull
{
    int width;
    int height;
    int depth;
    int originX;
    int originZ;
    int16_t ground;
    bool directMask;
    const struct DioramaTerrainCell *sourceCells[4];
    uint8_t shade[TREE_HULL_MAX_PIXELS];
    uint8_t mask[TREE_HULL_MAX_PIXELS];
    uint8_t outside[TREE_HULL_MAX_PIXELS];
    uint8_t selected[TREE_HULL_MAX_PIXELS];
    uint8_t sourceRow[TREE_HULL_MAX_PIXELS];
    int8_t zMin[TREE_HULL_MAX_PIXELS];
    int8_t zMax[TREE_HULL_MAX_PIXELS];
    uint16_t queue[TREE_HULL_MAX_PIXELS];
};

static const struct DioramaTerrainCell *FindInputCell(
    const struct DioramaTerrainChunkInput *input, int mapX, int mapY)
{
    int x = mapX - input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    int y = mapY - input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE
          + DIORAMA_TERRAIN_HALO;
    const struct DioramaTerrainCell *cell;

    if (x < 0 || y < 0 || x >= DIORAMA_TERRAIN_INPUT_SIZE
     || y >= DIORAMA_TERRAIN_INPUT_SIZE)
        return NULL;
    cell = GetCell(input, x, y);
    if (!cell->present || cell->mapX != mapX || cell->mapY != mapY)
        return NULL;
    return cell;
}

static bool TreeShadeIsPassable(uint8_t shade, bool ditherFallback)
{
    if (ditherFallback)
        return shade == DIORAMA_TREE_SHADE_OFF
            || shade == DIORAMA_TREE_SHADE_LIGHT
            || shade == DIORAMA_TREE_SHADE_WHITE;
    return shade != DIORAMA_TREE_SHADE_BLACK;
}

static void FloodTreeOutside(struct TreeHull *hull, bool ditherFallback)
{
    int readIndex = 0;
    int writeIndex = 0;
    int x;
    int y;

    memset(hull->outside, 0, sizeof(hull->outside));
#define SEED_TREE_PIXEL(index) do { \
    int seedIndex = (index); \
    if (!hull->outside[seedIndex] \
     && TreeShadeIsPassable(hull->shade[seedIndex], ditherFallback)) \
    { \
        hull->outside[seedIndex] = 1; \
        hull->queue[writeIndex++] = seedIndex; \
    } \
} while (0)
    for (x = 0; x < hull->width; x++)
    {
        SEED_TREE_PIXEL(x);
        SEED_TREE_PIXEL((hull->height - 1) * hull->width + x);
    }
    for (y = 0; y < hull->height; y++)
    {
        SEED_TREE_PIXEL(y * hull->width);
        SEED_TREE_PIXEL(y * hull->width + hull->width - 1);
    }
    while (readIndex < writeIndex)
    {
        int index = hull->queue[readIndex++];
        int pixelX = index % hull->width;
        int neighbors[4] = {
            pixelX > 0 ? index - 1 : -1,
            pixelX + 1 < hull->width ? index + 1 : -1,
            index >= hull->width ? index - hull->width : -1,
            index + hull->width < hull->width * hull->height
                ? index + hull->width : -1,
        };

        for (int side = 0; side < 4; side++)
        {
            int neighbor = neighbors[side];

            if (neighbor >= 0 && !hull->outside[neighbor]
             && TreeShadeIsPassable(hull->shade[neighbor], ditherFallback))
            {
                hull->outside[neighbor] = 1;
                hull->queue[writeIndex++] = neighbor;
            }
        }
    }
#undef SEED_TREE_PIXEL
}

static bool KeepLargestUpperTreeComponent(struct TreeHull *hull)
{
    int pixelCount = hull->width * hull->height;
    int bestSize = 0;

    memset(hull->outside, 0, sizeof(hull->outside));
    memset(hull->selected, 0, sizeof(hull->selected));
    for (int start = 0; start < pixelCount; start++)
    {
        int readIndex = 0;
        int writeIndex = 0;
        bool upper = false;

        if (!hull->mask[start] || hull->outside[start])
            continue;
        hull->outside[start] = 1;
        hull->queue[writeIndex++] = start;
        while (readIndex < writeIndex)
        {
            int index = hull->queue[readIndex++];
            int x = index % hull->width;
            int neighbors[4] = {
                x > 0 ? index - 1 : -1,
                x + 1 < hull->width ? index + 1 : -1,
                index >= hull->width ? index - hull->width : -1,
                index + hull->width < pixelCount ? index + hull->width : -1,
            };

            upper |= index / hull->width < hull->height / 2;
            for (int side = 0; side < 4; side++)
            {
                int neighbor = neighbors[side];

                if (neighbor >= 0 && hull->mask[neighbor] && !hull->outside[neighbor])
                {
                    hull->outside[neighbor] = 1;
                    hull->queue[writeIndex++] = neighbor;
                }
            }
        }
        if (upper && writeIndex > bestSize)
        {
            memset(hull->selected, 0, sizeof(hull->selected));
            for (int index = 0; index < writeIndex; index++)
                hull->selected[hull->queue[index]] = 1;
            bestSize = writeIndex;
        }
    }
    memcpy(hull->mask, hull->selected, sizeof(hull->mask));
    return bestSize != 0;
}

static bool BuildTreeSilhouette(struct TreeHull *hull)
{
    int enclosed = 0;
    int any = 0;
    int bottomRow = -1;
    int pixelCount = hull->width * hull->height;
    int y;

    if (hull->directMask)
    {
        for (int index = 0; index < pixelCount; index++)
            hull->mask[index] = hull->shade[index] != DIORAMA_TREE_SHADE_OFF;
        if (!KeepLargestUpperTreeComponent(hull))
            return false;
    }
    else
    {
        FloodTreeOutside(hull, false);
        for (int index = 0; index < pixelCount; index++)
        {
            hull->mask[index] = !hull->outside[index];
            enclosed += hull->mask[index]
                     && hull->shade[index] != DIORAMA_TREE_SHADE_BLACK;
        }
        if (enclosed < pixelCount / 8)
        {
            FloodTreeOutside(hull, true);
            for (int index = 0; index < pixelCount; index++)
                hull->mask[index] = !hull->outside[index]
                                 && hull->shade[index] != DIORAMA_TREE_SHADE_OFF;
        }
    }
    memset(hull->zMin, -1, sizeof(hull->zMin));
    memset(hull->zMax, -1, sizeof(hull->zMax));
    memset(hull->sourceRow, 0, sizeof(hull->sourceRow));
    for (y = 0; y < hull->height; y++)
    {
        int low = -1;
        int high = -1;

        for (int x = 0; x < hull->width; x++)
            if (hull->mask[y * hull->width + x])
            {
                if (low < 0)
                    low = x;
                high = x;
            }
        if (low < 0)
            continue;
        bottomRow = y;
        any = 1;
        for (int x = low; x <= high; x++)
        {
            int index = y * hull->width + x;
            float center;
            float halfWidth;
            float dx;
            int chord = 1;

            if (!hull->mask[index])
                continue;
            center = (low + high + 1) * 0.5f;
            halfWidth = (high - low + 1) * 0.5f;
            dx = x + 0.5f - center;
            if (halfWidth * halfWidth > dx * dx)
                chord = (int)floorf(2.0f * sqrtf(halfWidth * halfWidth - dx * dx) + 0.5f);
            if (chord < 1)
                chord = 1;
            hull->zMin[index] = (int8_t)floorf(hull->depth * 0.5f - chord * 0.5f + 0.5f);
            hull->zMax[index] = hull->zMin[index] + chord;
            hull->sourceRow[index] = y;
        }
    }
    if (!any)
        return false;
    for (y = bottomRow + 1; y < hull->height; y++)
        for (int x = 0; x < hull->width; x++)
        {
            int source = bottomRow * hull->width + x;
            int target = y * hull->width + x;

            if (hull->zMin[source] < 0)
                continue;
            hull->zMin[target] = hull->zMin[source];
            hull->zMax[target] = hull->zMax[source];
            hull->sourceRow[target] = bottomRow;
        }
    return true;
}

static bool TreeSolidAt(const struct TreeHull *hull, int x, int y, int z)
{
    int index;

    if (x < 0 || y < 0 || x >= hull->width || y >= hull->height)
        return false;
    index = y * hull->width + x;
    return hull->zMin[index] >= 0 && z >= hull->zMin[index] && z < hull->zMax[index];
}

static const struct DioramaTerrainCell *TreeSourceCell(const struct TreeHull *hull,
                                                        int pixelX, int pixelY)
{
    int cellX = pixelX / DIORAMA_VOXELS_PER_CELL;
    int cellY = pixelY / DIORAMA_VOXELS_PER_CELL;
    int cellsWide = hull->width / DIORAMA_VOXELS_PER_CELL;

    return hull->sourceCells[cellY * cellsWide + cellX];
}

static struct DioramaTerrainMaterial TreePixelMaterial(const struct TreeHull *hull,
                                                        int pixelX0, int pixelX1,
                                                        int pixelY)
{
    const struct DioramaTerrainCell *cell = TreeSourceCell(hull, pixelX0, pixelY);
    struct DioramaTerrainMaterial material = cell->treeMaterial;
    int localX0 = pixelX0 % DIORAMA_VOXELS_PER_CELL;
    int localX1 = (pixelX1 - 1) % DIORAMA_VOXELS_PER_CELL;
    int localY = pixelY % DIORAMA_VOXELS_PER_CELL;
    float v = material.v0 + (material.v1 - material.v0)
            * localY / (DIORAMA_VOXELS_PER_CELL - 1);
    float originalU0 = material.u0;
    float originalU1 = material.u1;

    material.u0 = originalU0 + (originalU1 - originalU0)
                * localX0 / (DIORAMA_VOXELS_PER_CELL - 1);
    material.u1 = originalU0 + (originalU1 - originalU0)
                * localX1 / (DIORAMA_VOXELS_PER_CELL - 1);
    material.v0 = material.v1 = v;
    material.rotation = 0;
    material.flags = 0;
    return material;
}

static void TreeSideSource(const struct TreeHull *hull, int x, int y,
                           int *sourceX, int *sourceY)
{
    int row = hull->sourceRow[y * hull->width + x];
    int low = hull->width;
    int high = -1;
    int direction;

    for (int candidate = 0; candidate < hull->width; candidate++)
        if (hull->mask[row * hull->width + candidate])
        {
            if (candidate < low)
                low = candidate;
            high = candidate;
        }
    direction = x * 2 < low + high ? 1 : -1;
    for (int step = 0; step <= 3; step++)
    {
        int candidate = x + direction * step;
        int index;

        if (candidate < 0 || candidate >= hull->width)
            break;
        index = row * hull->width + candidate;
        if (!hull->mask[index])
            break;
        if (hull->shade[index] != DIORAMA_TREE_SHADE_BLACK)
        {
            *sourceX = candidate;
            *sourceY = row;
            return;
        }
    }
    *sourceX = x;
    *sourceY = row;
}

static void TreeTopSource(const struct TreeHull *hull, int x, int y,
                          int *sourceX, int *sourceY)
{
    for (int row = y + 2; row <= y + 4 && row < hull->height; row++)
    {
        int index = row * hull->width + x;

        if (hull->mask[index] && hull->shade[index] != DIORAMA_TREE_SHADE_BLACK)
        {
            *sourceX = x;
            *sourceY = row;
            return;
        }
    }
    *sourceX = x;
    *sourceY = hull->sourceRow[y * hull->width + x];
}

static bool AppendTreeFace(const struct DioramaTerrainChunkInput *input,
                           struct DioramaTerrainVertex *vertices, uint32_t capacity,
                           uint32_t *vertexCount, struct DioramaTerrainMesh *mesh,
                           const struct TreeHull *hull, int ownerX, int ownerZ,
                           int axis, int sign, int plane, int uMin, int uMax,
                           int16_t vMin, int16_t vMax,
                           int sourceX0, int sourceX1, int sourceY, float shade)
{
    int ownerMinX = input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int ownerMinZ = input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    struct DioramaShellFace face = {
        .plane = plane, .uMin = uMin, .uMax = uMax, .vMin = vMin, .vMax = vMax,
        .axis = axis, .sign = sign,
    };
    struct DioramaTerrainMaterial material;
    float positions[4][3];

    if (ownerX < ownerMinX
     || ownerX >= ownerMinX + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL
     || ownerZ < ownerMinZ
     || ownerZ >= ownerMinZ + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL)
        return true;
    material = TreePixelMaterial(hull, sourceX0, sourceX1, sourceY);
    if (material.layer == DIORAMA_MATERIAL_NONE)
        return true;
    FacePositions(&face, positions);
    if (!AppendQuad(vertices, capacity, vertexCount, positions, &material, shade, 0.0f))
        return false;
    mesh->shellFaceCount++;
    if (axis == 1 && sign > 0)
        mesh->topFaceCount++;
    else if (axis == 1)
        mesh->bottomFaceCount++;
    else
        mesh->sideFaceCount++;
    for (int corner = 0; corner < 4; corner++)
    {
        if (positions[corner][0] < mesh->bounds.minX) mesh->bounds.minX = positions[corner][0];
        if (positions[corner][0] > mesh->bounds.maxX) mesh->bounds.maxX = positions[corner][0];
        if (positions[corner][1] < mesh->bounds.minY) mesh->bounds.minY = positions[corner][1];
        if (positions[corner][1] > mesh->bounds.maxY) mesh->bounds.maxY = positions[corner][1];
        if (positions[corner][2] < mesh->bounds.minZ) mesh->bounds.minZ = positions[corner][2];
        if (positions[corner][2] > mesh->bounds.maxZ) mesh->bounds.maxZ = positions[corner][2];
    }
    return true;
}

static bool AppendTreeHull(const struct DioramaTerrainChunkInput *input,
                           struct DioramaTerrainVertex *vertices, uint32_t capacity,
                           uint32_t *vertexCount, struct DioramaTerrainMesh *mesh,
                           struct TreeHull *hull)
{
    if (!BuildTreeSilhouette(hull))
        return true;
    for (int y = 0; y < hull->height; y++)
    {
        int x = 0;

        while (x < hull->width)
        {
            int index = y * hull->width + x;
            int end = x + 1;
            int sourceY;
            int worldY;

            if (hull->zMin[index] < 0)
            {
                x++;
                continue;
            }
            sourceY = hull->sourceRow[index];
            while (end < hull->width
                && end / DIORAMA_VOXELS_PER_CELL == x / DIORAMA_VOXELS_PER_CELL
                && hull->zMin[y * hull->width + end] == hull->zMin[index]
                && hull->zMax[y * hull->width + end] == hull->zMax[index]
                && hull->sourceRow[y * hull->width + end] == sourceY)
                end++;
            worldY = hull->ground + hull->height - 1 - y;
            if (!AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                    hull->originX + x, hull->originZ + hull->zMin[index],
                    2, -1, hull->originZ + hull->zMin[index],
                    hull->originX + x, hull->originX + end, worldY, worldY + 1,
                    x, end, sourceY, 0.68f)
             || !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                    hull->originX + x, hull->originZ + hull->zMax[index] - 1,
                    2, 1, hull->originZ + hull->zMax[index],
                    hull->originX + x, hull->originX + end, worldY, worldY + 1,
                    x, end, sourceY, 1.0f))
                return false;
            x = end;
        }
        for (int x = 0; x < hull->width; x++)
        {
            int index = y * hull->width + x;
            int sourceY;
            int worldX;
            int worldY;
            int sideX;
            int sideY;

            if (hull->zMin[index] < 0)
                continue;
            sourceY = hull->sourceRow[index];
            worldX = hull->originX + x;
            worldY = hull->ground + hull->height - 1 - y;
            TreeSideSource(hull, x, y, &sideX, &sideY);
            for (int direction = -1; direction <= 1; direction += 2)
            {
                int z = hull->zMin[index];

                while (z < hull->zMax[index])
                {
                    int start;
                    int chunk;

                    while (z < hull->zMax[index]
                        && TreeSolidAt(hull, x + direction, y, z))
                        z++;
                    start = z;
                    chunk = DioramaTerrain_FloorDiv(hull->originZ + start,
                        DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL);
                    while (z < hull->zMax[index]
                        && !TreeSolidAt(hull, x + direction, y, z)
                        && DioramaTerrain_FloorDiv(hull->originZ + z,
                            DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL) == chunk)
                        z++;
                    if (start < z
                     && !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                            worldX, hull->originZ + start, 0, direction,
                            worldX + (direction > 0), hull->originZ + start,
                            hull->originZ + z, worldY, worldY + 1,
                            sideX, sideX + 1, sideY, 0.78f))
                        return false;
                }
            }
            for (int direction = -1; direction <= 1; direction += 2)
            {
                int z = hull->zMin[index];
                bool wholeExposed = true;

                for (int testZ = z; wholeExposed && testZ < hull->zMax[index]; testZ++)
                    wholeExposed = !TreeSolidAt(hull, x, y + direction, testZ);
                while (z < hull->zMax[index])
                {
                    int start;
                    int sourceX = x;
                    int sourceRow = sourceY;
                    int chunk;
                    bool deepRun;

                    while (z < hull->zMax[index]
                        && TreeSolidAt(hull, x, y + direction, z))
                        z++;
                    start = z;
                    deepRun = direction < 0 && wholeExposed
                           && hull->zMax[index] - hull->zMin[index] >= 3
                           && z > hull->zMin[index] && z + 1 < hull->zMax[index];
                    if (deepRun)
                        TreeTopSource(hull, x, y, &sourceX, &sourceRow);
                    chunk = DioramaTerrain_FloorDiv(hull->originZ + start,
                        DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL);
                    while (z < hull->zMax[index]
                        && !TreeSolidAt(hull, x, y + direction, z)
                        && DioramaTerrain_FloorDiv(hull->originZ + z,
                            DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL) == chunk)
                    {
                        bool deep = direction < 0 && wholeExposed
                                 && hull->zMax[index] - hull->zMin[index] >= 3
                                 && z > hull->zMin[index] && z + 1 < hull->zMax[index];

                        if (deep != deepRun)
                            break;
                        z++;
                    }
                    if (start < z && (direction < 0 || y + 1 < hull->height)
                     && !AppendTreeFace(input, vertices, capacity, vertexCount, mesh, hull,
                            worldX, hull->originZ + start, 1, -direction,
                            worldY + (direction < 0), worldX, worldX + 1,
                            hull->originZ + start, hull->originZ + z,
                            sourceX, sourceX + 1, sourceRow,
                            direction < 0 ? 1.0f : 0.55f))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool AppendPixelQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *count, const float positions[4][3], float shade,
                            uint32_t color);

bool DioramaTerrain_BuildTreeModel(struct DioramaTerrainVertex *vertices,
                                   uint32_t capacity, uint32_t *vertexCount,
                                   struct DioramaTerrainBounds *bounds)
{
    const int originX = 0;
    const int originZ = (DIORAMA_VOXELS_PER_CELL * 2 - DIORAMA_TREE_MODEL_SIZE_Z) / 2;
    const int ground = -DIORAMA_TREE_MODEL_MIN_Y;

    if (vertices == NULL || vertexCount == NULL || bounds == NULL)
        return false;
    *vertexCount = 0;
    bounds->minX = bounds->minY = bounds->minZ = FLT_MAX;
    bounds->maxX = bounds->maxY = bounds->maxZ = -FLT_MAX;

    for (uint32_t index = 0; index < gDioramaTreeModelFaceCount; index++)
    {
        const struct DioramaTreeModelFace *source = &gDioramaTreeModelFaces[index];
        int minimum[3] = {0, 0, 0};
        int maximum[3] = {0, 0, 0};
        int uAxis = (source->axis + 1) % 3;
        int vAxis = (source->axis + 2) % 3;
        int xMin;
        int xMax;
        int zMin;
        int zMax;
        struct DioramaShellFace face = {0};
        float positions[4][3];
        float shade;

        minimum[source->axis] = maximum[source->axis] = source->plane;
        minimum[uAxis] = source->uMin;
        maximum[uAxis] = source->uMax;
        minimum[vAxis] = source->vMin;
        maximum[vAxis] = source->vMax;
        xMin = originX + minimum[0];
        xMax = originX + maximum[0];
        zMin = originZ + minimum[1];
        zMax = originZ + maximum[1];

        if (source->axis == 0)
        {
            face.axis = 0;
            face.plane = xMin;
            face.uMin = zMin;
            face.uMax = zMax;
            face.vMin = ground + minimum[2];
            face.vMax = ground + maximum[2];
            shade = 0.82f;
        }
        else if (source->axis == 1)
        {
            face.axis = 2;
            face.plane = zMin;
            face.uMin = xMin;
            face.uMax = xMax;
            face.vMin = ground + minimum[2];
            face.vMax = ground + maximum[2];
            shade = source->sign > 0 ? 0.72f : 1.0f;
        }
        else
        {
            face.axis = 1;
            face.sign = source->sign;
            face.plane = ground + minimum[2];
            face.uMin = xMin;
            face.uMax = xMax;
            face.vMin = zMin;
            face.vMax = zMax;
            shade = source->sign > 0 ? 1.0f : 0.58f;
        }
        FacePositions(&face, positions);
        if (!AppendPixelQuad(vertices, capacity, vertexCount, positions, shade, source->rgba))
            return false;
        for (int corner = 0; corner < 4; corner++)
        {
            if (positions[corner][0] < bounds->minX) bounds->minX = positions[corner][0];
            if (positions[corner][0] > bounds->maxX) bounds->maxX = positions[corner][0];
            if (positions[corner][1] < bounds->minY) bounds->minY = positions[corner][1];
            if (positions[corner][1] > bounds->maxY) bounds->maxY = positions[corner][1];
            if (positions[corner][2] < bounds->minZ) bounds->minZ = positions[corner][2];
            if (positions[corner][2] > bounds->maxZ) bounds->maxZ = positions[corner][2];
        }
    }
    return true;
}

void DioramaTerrain_SetGeometryHash(bool on)
{
    sNoGeometryHash = !on;
}

void DioramaTerrain_SetVoxelOccupancyOnly(bool on)
{
    sVoxelOccupancyOnly = on;
}

uint32_t DioramaTerrain_TreeModelFaceCount(void)
{
    return gDioramaTreeModelFaceCount;
}

/* compile_vox.py's sign is the side of its voxel a face lies on, along the
 * face's axis: the model's x is the scene's x (VoxelX), its depth axis the
 * scene's z reversed (VoxelZ) and its height the scene's y (VoxelY), as
 * BuildTreeModel places them. */
enum DioramaTreeFacing DioramaTerrain_TreeModelFacing(uint32_t face)
{
    const struct DioramaTreeModelFace *source = &gDioramaTreeModelFaces[face];

    if (source->axis == 0)
        return source->sign > 0 ? DIORAMA_TREE_FACING_EAST : DIORAMA_TREE_FACING_WEST;
    if (source->axis == 1)
        return source->sign > 0 ? DIORAMA_TREE_FACING_SOUTH : DIORAMA_TREE_FACING_NORTH;
    return source->sign > 0 ? DIORAMA_TREE_FACING_UP : DIORAMA_TREE_FACING_DOWN;
}

static bool AppendVoxelTreeInstance(const struct DioramaTerrainChunkInput *input,
                                    const struct DioramaTerrainCell *anchor,
                                    struct DioramaTerrainMesh *mesh)
{
    int anchorY = anchor->mapY - (anchor->structureHeight == 1);
    int ownerChunkX = DioramaTerrain_FloorDiv(anchor->mapX, DIORAMA_TERRAIN_CHUNK_SIZE);
    int ownerChunkY = DioramaTerrain_FloorDiv(anchorY, DIORAMA_TERRAIN_CHUNK_SIZE);
    struct DioramaTerrainTreeInstance *instance;
    float minX;
    float maxX;
    float minY;
    float maxY;
    float minZ;
    float maxZ;

    if (ownerChunkX != input->chunkX || ownerChunkY != input->chunkY)
        return true;
    if (mesh->treeInstanceCount >= DIORAMA_TERRAIN_MAX_TREE_INSTANCES)
        return false;
    instance = &mesh->treeInstances[mesh->treeInstanceCount++];
    instance->x = anchor->mapX;
    instance->y = anchor->groundHeight;
    instance->z = -anchorY;
    minX = instance->x + (DIORAMA_TREE_MODEL_MIN_X - DIORAMA_VOXELS_PER_CELL / 2)
         / (float)DIORAMA_VOXELS_PER_CELL;
    maxX = instance->x + (DIORAMA_TREE_MODEL_MAX_X - DIORAMA_VOXELS_PER_CELL / 2)
         / (float)DIORAMA_VOXELS_PER_CELL;
    minY = instance->y;
    maxY = instance->y + (DIORAMA_TREE_MODEL_MAX_Y - DIORAMA_TREE_MODEL_MIN_Y)
         / (float)DIORAMA_VOXELS_PER_CELL;
    minZ = instance->z + (DIORAMA_VOXELS_PER_CELL / 4 - DIORAMA_TREE_MODEL_MAX_Z)
         / (float)DIORAMA_VOXELS_PER_CELL;
    maxZ = instance->z + (DIORAMA_VOXELS_PER_CELL / 4 - DIORAMA_TREE_MODEL_MIN_Z)
         / (float)DIORAMA_VOXELS_PER_CELL;
    if (minX < mesh->bounds.minX) mesh->bounds.minX = minX;
    if (maxX > mesh->bounds.maxX) mesh->bounds.maxX = maxX;
    if (minY < mesh->bounds.minY) mesh->bounds.minY = minY;
    if (maxY > mesh->bounds.maxY) mesh->bounds.maxY = maxY;
    if (minZ < mesh->bounds.minZ) mesh->bounds.minZ = minZ;
    if (maxZ > mesh->bounds.maxZ) mesh->bounds.maxZ = maxZ;
    return true;
}

static bool AssembleTreeHull(const struct DioramaTerrainChunkInput *input,
                             const struct DioramaTerrainCell *anchor,
                             struct TreeHull *hull)
{
    int cellsWide = 1;
    int cellsHigh = 1;
    int bodyStart = 0;

    memset(hull, 0, sizeof(*hull));
    if (anchor->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
    {
        cellsWide = anchor->structureWidth;
        cellsHigh = anchor->structureHeight >= 2 ? 2 : 1;
        bodyStart = anchor->structureHeight - cellsHigh;
        if (cellsWide < 1 || cellsWide > 2 || cellsHigh > 2
         || anchor->structureLocalX != 0 || anchor->structureLocalY != bodyStart
         || anchor->structureId == 0)
            return false;
    }
    hull->width = cellsWide * DIORAMA_VOXELS_PER_CELL;
    hull->height = cellsHigh * DIORAMA_VOXELS_PER_CELL;
    hull->depth = hull->width;
    hull->originX = anchor->mapX * DIORAMA_VOXELS_PER_CELL;
    hull->originZ = anchor->mapY * DIORAMA_VOXELS_PER_CELL
                  + (hull->height - hull->depth) / 2;
    hull->ground = HeightToVoxel(anchor->groundHeight);
    hull->directMask = anchor->treeMaskDirect;
    for (int cellY = 0; cellY < cellsHigh; cellY++)
        for (int cellX = 0; cellX < cellsWide; cellX++)
        {
            const struct DioramaTerrainCell *source = FindInputCell(
                input, anchor->mapX + cellX, anchor->mapY + cellY);
            int sourceIndex = cellY * cellsWide + cellX;

            if (source == NULL || source->archetype != anchor->archetype
             || (anchor->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
              && source->structureId != anchor->structureId))
                return false;
            hull->sourceCells[sourceIndex] = source;
            for (int pixelY = 0; pixelY < DIORAMA_VOXELS_PER_CELL; pixelY++)
                for (int pixelX = 0; pixelX < DIORAMA_VOXELS_PER_CELL; pixelX++)
                    hull->shade[(cellY * DIORAMA_VOXELS_PER_CELL + pixelY) * hull->width
                              + cellX * DIORAMA_VOXELS_PER_CELL + pixelX]
                        = source->treeShade[pixelY * DIORAMA_VOXELS_PER_CELL + pixelX];
        }
    return true;
}

static bool AppendTreeHulls(const struct DioramaTerrainChunkInput *input,
                            struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *vertexCount, struct DioramaTerrainMesh *mesh)
{
    struct TreeHull hull;

    for (int y = 0; y < DIORAMA_TERRAIN_INPUT_SIZE; y++)
        for (int x = 0; x < DIORAMA_TERRAIN_INPUT_SIZE; x++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, x, y);

            if (!cell->present
             || (cell->archetype != DIORAMA_ARCHETYPE_ROUND_HULL
              && cell->archetype != DIORAMA_ARCHETYPE_GROUPED_HULL)
             || !cell->treeHullReady)
                continue;
            if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
            {
                int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
                int bodyStart = cell->structureHeight - bodyRows;

                if (cell->structureLocalX != 0 || cell->structureLocalY != bodyStart)
                    continue;
            }
            {
                uint32_t vertexStart = *vertexCount;
                struct DioramaTerrainMesh meshBeforeHull = *mesh;
                bool appended;

                if (cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
                 && cell->structureWidth == 2)
                    appended = AppendVoxelTreeInstance(input, cell, mesh);
                else
                    appended = AssembleTreeHull(input, cell, &hull)
                            && AppendTreeHull(input, vertices, capacity, vertexCount, mesh, &hull);
                if (!appended)
                {
                    *vertexCount = vertexStart;
                    *mesh = meshBeforeHull;
                }
            }
        }
    return true;
}

static bool AppendPixelQuad(struct DioramaTerrainVertex *vertices, uint32_t capacity,
                            uint32_t *count, const float positions[4][3], float shade,
                            uint32_t color)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    int i;

    for (i = 0; i < 6; i++)
    {
        int corner = order[i];

        if (!AppendVertex(vertices, capacity, count,
                          positions[corner][0], positions[corner][1], positions[corner][2],
                          0.0f, 0.0f, shade, DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR,
                          0.0f, color))
            return false;
    }
    return true;
}

static bool AppendPixelPrimitives(const struct DioramaTerrainChunkInput *input,
                                  struct DioramaTerrainVertex *vertices,
                                  uint32_t capacity, uint32_t *count,
                                  struct DioramaTerrainMesh *mesh)
{
    uint32_t index;

    if (input->pixelCount > DIORAMA_TERRAIN_MAX_PIXEL_PRIMITIVES)
        return false;
    for (index = 0; index < input->pixelCount; index++)
    {
        const struct DioramaTerrainPixelPrimitive *pixel = &input->pixels[index];
        float left = (pixel->xQ32 - 16) / 32.0f;
        float right = (pixel->xQ32 + pixel->sizeXQ32 - 16) / 32.0f;
        float bottom = pixel->yQ32 / 32.0f;
        float top = (pixel->yQ32 + pixel->sizeYQ32) / 32.0f;
        float north = -(pixel->zQ32 - 16) / 32.0f;
        float south = -(pixel->zQ32 + pixel->sizeZQ32 - 16) / 32.0f;
        const float quads[6][4][3] = {
            {{left, top, north}, {right, top, north}, {right, top, south}, {left, top, south}},
            {{left, bottom, south}, {right, bottom, south}, {right, bottom, north}, {left, bottom, north}},
            {{left, top, north}, {right, top, north}, {right, bottom, north}, {left, bottom, north}},
            {{right, top, north}, {right, top, south}, {right, bottom, south}, {right, bottom, north}},
            {{right, top, south}, {left, top, south}, {left, bottom, south}, {right, bottom, south}},
            {{left, top, south}, {left, top, north}, {left, bottom, north}, {left, bottom, south}},
        };
        /* Both artwork faces preserve the source texel; only prism depth is shaded. */
        static const float shades[6] = {1.0f, 0.58f, 1.0f, 0.68f, 1.0f, 0.68f};
        int face;

        for (face = 0; face < 6; face++)
            if (!AppendPixelQuad(vertices, capacity, count, quads[face],
                                 shades[face], pixel->rgba))
                return false;
        if (left < mesh->bounds.minX) mesh->bounds.minX = left;
        if (right > mesh->bounds.maxX) mesh->bounds.maxX = right;
        if (bottom < mesh->bounds.minY) mesh->bounds.minY = bottom;
        if (top > mesh->bounds.maxY) mesh->bounds.maxY = top;
        if (south < mesh->bounds.minZ) mesh->bounds.minZ = south;
        if (north > mesh->bounds.maxZ) mesh->bounds.maxZ = north;
        mesh->featureFaceCount += 6;
    }
    return true;
}

static void FaceMaterial(const struct DioramaTerrainCell *cell, uint8_t surface,
                          const struct DioramaTerrainMaterial *source,
                          const struct DioramaShellFace *face,
                          struct DioramaTerrainMaterial *output)
{
    const float inset = 0.02f / DIORAMA_VOXELS_PER_CELL;
    float uMin = 0.0f;
    float uMax = 1.0f;
    float vMin = 0.0f;
    float vMax = 1.0f;
    int32_t sourcePixelX = face->sourceCellX * DIORAMA_VOXELS_PER_CELL;
    int32_t sourcePixelZ = face->sourceCellY * DIORAMA_VOXELS_PER_CELL;

    *output = *source;
    if (face->axis == 1)
    {
        uMin = (face->uMin - sourcePixelX) / (float)DIORAMA_VOXELS_PER_CELL;
        uMax = (face->uMax - sourcePixelX) / (float)DIORAMA_VOXELS_PER_CELL;
        if (surface == 0 && cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL)
        {
            int bodyRows = cell->structureHeight >= 2 ? 2 : 1;
            int bodyStart = cell->structureHeight - bodyRows;
            int32_t groupPixelZ = (cell->mapY - (cell->structureLocalY - bodyStart))
                                * DIORAMA_VOXELS_PER_CELL;

            vMin = (face->vMin - groupPixelZ)
                 / (float)(bodyRows * DIORAMA_VOXELS_PER_CELL);
            vMax = (face->vMax - groupPixelZ)
                 / (float)(bodyRows * DIORAMA_VOXELS_PER_CELL);
        }
        else
        {
            vMin = (face->vMin - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
            vMax = (face->vMax - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL;
        }
    }
    else
    {
        int32_t source = face->axis == 0 ? sourcePixelZ : sourcePixelX;
        uMin = (face->uMin - source) / (float)DIORAMA_VOXELS_PER_CELL;
        uMax = (face->uMax - source) / (float)DIORAMA_VOXELS_PER_CELL;
    }
    output->u0 = source->u0 + (source->u1 - source->u0) * (uMin + inset);
    output->u1 = source->u0 + (source->u1 - source->u0) * (uMax - inset);
    output->v0 = source->v0 + (source->v1 - source->v0) * (vMin + inset);
    output->v1 = source->v0 + (source->v1 - source->v0) * (vMax - inset);
    if (face->axis != 1 && ((cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
                         || (IsLedgeBank(cell) && surface == 0))
     && source->rotation == 0 && source->flags == 0)
    {
        /* A cell drawn as a height per pixel (a mountain's rock, a terrace's
         * course, a jump ledge's bank): the side of each pixel's column is that pixel's colour, so
         * a slope of such columns shows the drawing laid on it. The whole
         * drawing squeezed onto each column's side drew a terrace's face as
         * bands of stripes (Route 104's cliffs, the owner's report of
         * 2026-10-24). */
        int32_t local = face->plane - (face->axis == 0 ? sourcePixelX : sourcePixelZ);
        int32_t column = face->sign > 0 ? local - 1 : local;
        float fixed;

        if (column < 0)
            column = 0;
        if (column >= DIORAMA_VOXELS_PER_CELL)
            column = DIORAMA_VOXELS_PER_CELL - 1;
        fixed = (column + 0.5f) / DIORAMA_VOXELS_PER_CELL;
        if (face->axis == 2)
        {
            /* a north or south side: its row, the columns along it */
            output->v0 = source->v0 + (source->v1 - source->v0) * fixed;
            output->v1 = output->v0;
        }
        else
        {
            /* an east or west side: its column, the rows along it (turned:
             * AppendQuad's v runs from its south end to its north end) */
            output->rotation = 1;
            output->u0 = source->u0 + (source->u1 - source->u0) * fixed;
            output->u1 = output->u0;
            output->v0 = source->v0 + (source->v1 - source->v0)
                       * ((face->uMax - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL - inset);
            output->v1 = source->v0 + (source->v1 - source->v0)
                       * ((face->uMin - sourcePixelZ) / (float)DIORAMA_VOXELS_PER_CELL + inset);
        }
    }
}

bool DioramaTerrain_BuildChunk(const struct DioramaTerrainChunkInput *input,
                               struct DioramaTerrainVertex *vertices,
                               uint32_t vertexCapacity,
                               struct DioramaTerrainMesh *mesh)
{
    uint32_t vertexCount = 0;
    uint32_t spanCount;
    uint32_t shellFaceCount;
    uint32_t rawShellFaceCount;
    uint32_t faceIndex;
    int32_t ownerMinX = input->chunkX * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int32_t ownerMinZ = input->chunkY * DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL;
    int gridY;
    int gridX;

    memset(mesh, 0, sizeof(*mesh));
    mesh->bounds.minX = FLT_MAX;
    mesh->bounds.minY = FLT_MAX;
    mesh->bounds.minZ = FLT_MAX;
    mesh->bounds.maxX = -FLT_MAX;
    mesh->bounds.maxY = -FLT_MAX;
    mesh->bounds.maxZ = -FLT_MAX;
    for (gridY = DIORAMA_TERRAIN_HALO;
         gridY < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridY++)
        for (gridX = DIORAMA_TERRAIN_HALO;
             gridX < DIORAMA_TERRAIN_HALO + DIORAMA_TERRAIN_CHUNK_SIZE; gridX++)
        {
            const struct DioramaTerrainCell *cell = GetCell(input, gridX, gridY);
            uint8_t corners;

            if (!cell->present || cell->shape != DIORAMA_SHAPE_CLIFF)
                continue;
            corners = cell->cliffCornerMask;
            while (corners != 0)
            {
                mesh->cliffCornerCount += corners & 1;
                corners >>= 1;
            }
        }

    if (!sVoxelOccupancyOnly
     && BuildCompressedOccupancyShell(input, &spanCount, &shellFaceCount,
                                      &rawShellFaceCount))
        mesh->usedCompressedOccupancy = true;
    else
    {
        if (!BuildOccupancy(input, &spanCount)
         || !DioramaOccupancy_BuildShell(sOccupancySpans, spanCount,
                ownerMinX, ownerMinZ,
                ownerMinX + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL,
                ownerMinZ + DIORAMA_TERRAIN_CHUNK_SIZE * DIORAMA_VOXELS_PER_CELL,
                sShellFaces, DIORAMA_TERRAIN_MAX_FACES, &shellFaceCount))
            return false;
        rawShellFaceCount = shellFaceCount;
    }
    if (!AppendSkirtFaces(input, &shellFaceCount, &rawShellFaceCount))
        return false;
    mesh->occupancySpanCount = spanCount;
    mesh->shellFaceCount = rawShellFaceCount;
    /* Every underside is kept, those on a cell's floor too: the terrain is
     * drawn from both sides, and where a merged face's edge meets smaller
     * faces (a T-junction) the rasterizer can leave a crack a pixel wide; the
     * column's underside behind it is what shows there, not the sky. Dropping
     * them (V37) showed sky-coloured lines along the Lavaridge relief's steps
     * (V47, the battle session's E14). */
    shellFaceCount = DioramaOccupancy_MergeFaces(sShellFaces, shellFaceCount);
    for (faceIndex = 0; faceIndex < shellFaceCount; faceIndex++)
    {
        const struct DioramaShellFace *face = &sShellFaces[faceIndex];
        const struct DioramaTerrainCell *cell = FaceSourceCell(input, face);
        unsigned materialIndex = face->material - 1;
        unsigned materialFace = materialIndex % DIORAMA_MATERIAL_FACE_COUNT;
        unsigned surface = (materialIndex / DIORAMA_MATERIAL_FACE_COUNT)
                         % DIORAMA_TERRAIN_MAX_SURFACES;
        const struct DioramaTerrainMaterial *sourceMaterial;
        struct DioramaTerrainMaterial material;
        struct DioramaShellFace band = *face;
        int16_t bandEnd = face->vMax;

        if (cell == NULL || materialFace >= DIORAMA_MATERIAL_FACE_COUNT)
            return false;
        do
        {
            float positions[4][3];
            float shade;
            int corner;
            unsigned measuredBand = 0;

            if (face->axis != 1 && cell->measuredBandCount != 0)
            {
                int16_t boundary;

                measuredBand = band.vMin < 0 ? 0 : band.vMin / 8;
                if (measuredBand >= cell->measuredBandCount)
                    measuredBand = cell->measuredBandCount - 1;
                boundary = band.vMin < 0 ? 0 : (int16_t)((measuredBand + 1) * 8);
                band.vMax = boundary < bandEnd ? boundary : bandEnd;
                sourceMaterial = &cell->measuredBands[measuredBand];
            }
            else if (face->axis != 1 && band.vMin + DIORAMA_VOXELS_PER_CELL < bandEnd)
            {
                band.vMax = band.vMin + DIORAMA_VOXELS_PER_CELL;
                sourceMaterial = surface == 0 ? &cell->materials[materialFace]
                                              : &cell->underlayMaterials[materialFace];
            }
            else
            {
                band.vMax = bandEnd;
                sourceMaterial = surface == 0 ? &cell->materials[materialFace]
                                              : &cell->underlayMaterials[materialFace];
            }
            if (sourceMaterial->layer == DIORAMA_MATERIAL_NONE)
            {
                band.vMin = band.vMax;
                continue;
            }
            FacePositions(&band, positions);
            FaceMaterial(cell, surface, sourceMaterial, &band, &material);
            shade = face->axis == 1 ? 1.0f
                  : (face->axis == 2 && face->sign < 0 ? 0.82f : 0.68f);
            /* a height per pixel (mountain rock, a terrace's course): a slope's
             * steps are shaded nearly as its tops, so it reads as one surface,
             * not bands of light and dark */
            if (face->axis != 1
             && ((cell->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART) || IsLedgeBank(cell)))
                shade = face->axis == 2 && face->sign < 0 ? 0.92f : 0.86f;
            /* a jump ledge's grass slope, its steps as light as their tops: one
             * slope up to the crest, not stripes across it (its rock face keeps
             * the steps' shade) */
            if (face->axis != 1 && surface == 0 && IsLedgeBank(cell)
             && LedgeSideLooksBack(cell->behavior, face->axis, face->sign))
                shade = 0.98f;
            if (surface == 1 && face->axis == 1 && face->sign > 0
             && cell->archetype == DIORAMA_ARCHETYPE_GROUPED_HULL
             && cell->treeHullReady && cell->structureWidth == 2
             && cell->structureLocalY >= cell->structureHeight - 2)
                shade = 0.78f;
            /* (a jump ledge's bank marked for the shader's shadow bias:
             * DIORAMA_TERRAIN_BANK_MASK) */
            if (!AppendQuad(vertices, vertexCapacity, &vertexCount, positions, &material, shade,
                            face->axis == 1 && face->sign > 0 && (face->flags & 1) ? 1.0f
                            : surface == 0 && IsLedgeBank(cell) ? DIORAMA_TERRAIN_BANK_MASK
                            : 0.0f))
                return false;
            if (face->axis == 1 && face->sign > 0)
                mesh->topFaceCount++;
            else if (face->axis == 1)
                mesh->bottomFaceCount++;
            else if (cell->shape == DIORAMA_SHAPE_LEDGE && face->vMax > 0)
                mesh->featureFaceCount++;
            else
                mesh->sideFaceCount++;
            if (cell->shape == DIORAMA_SHAPE_CLIFF && face->axis != 1
             && materialFace >= DIORAMA_MATERIAL_FACE_NORTH
             && materialFace <= DIORAMA_MATERIAL_FACE_WEST)
            {
                uint8_t edge = 1u << (materialFace - DIORAMA_MATERIAL_FACE_NORTH);

                if ((cell->cliffBaseMask & edge)
                 && face->vMin <= HeightToVoxel(cell->groundHeight))
                    mesh->cliffBaseFaceCount++;
                if (cell->cliffTransitionMask & edge)
                    mesh->cliffTransitionFaceCount++;
            }
            for (corner = 0; corner < 4; corner++)
            {
                if (positions[corner][0] < mesh->bounds.minX) mesh->bounds.minX = positions[corner][0];
                if (positions[corner][0] > mesh->bounds.maxX) mesh->bounds.maxX = positions[corner][0];
                if (positions[corner][1] < mesh->bounds.minY) mesh->bounds.minY = positions[corner][1];
                if (positions[corner][1] > mesh->bounds.maxY) mesh->bounds.maxY = positions[corner][1];
                if (positions[corner][2] < mesh->bounds.minZ) mesh->bounds.minZ = positions[corner][2];
                if (positions[corner][2] > mesh->bounds.maxZ) mesh->bounds.maxZ = positions[corner][2];
            }
            band.vMin = band.vMax;
        } while (face->axis != 1 && band.vMin < bandEnd);
    }

    if (!AppendTreeHulls(input, vertices, vertexCapacity, &vertexCount, mesh)
     || !AppendPixelPrimitives(input, vertices, vertexCapacity, &vertexCount, mesh))
        return false;

    mesh->vertexCount = vertexCount;
    mesh->faceCount = mesh->topFaceCount + mesh->bottomFaceCount
                    + mesh->sideFaceCount + mesh->featureFaceCount;
    if (mesh->faceCount == 0 && mesh->treeInstanceCount == 0)
        memset(&mesh->bounds, 0, sizeof(mesh->bounds));
    mesh->geometryHash = 0;
    if (sNoGeometryHash)
        return true;
    mesh->geometryHash = FNV_OFFSET;
    for (uint32_t i = 0; i < vertexCount; i++)
    {
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].x);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].y);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].z);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].u);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].v);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].shade);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].textureLayer);
        mesh->geometryHash = HashFloat(mesh->geometryHash, vertices[i].reflectionMask);
        mesh->geometryHash = HashU32(mesh->geometryHash, vertices[i].color);
    }
    mesh->geometryHash = HashU16(mesh->geometryHash, mesh->treeInstanceCount);
    for (uint16_t i = 0; i < mesh->treeInstanceCount; i++)
    {
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].x);
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].y);
        mesh->geometryHash = HashFloat(mesh->geometryHash, mesh->treeInstances[i].z);
    }
    return true;
}

struct DioramaCameraRig gDioramaCameraRig;

void DioramaCamera_GetRig(float cameraPitch, float *height, float *distance,
                          float *screenOffsetY)
{
    if (gDioramaCameraRig.active)
    {
        *height = gDioramaCameraRig.height;
        *distance = gDioramaCameraRig.distance;
        *screenOffsetY = gDioramaCameraRig.screenOffsetY;
        return;
    }
    *height = DIORAMA_OVERWORLD_CAMERA_HEIGHT;
    *distance = (DIORAMA_OVERWORLD_CAMERA_HEIGHT * cosf(cameraPitch)
                 + DIORAMA_OVERWORLD_TARGET_VERTICAL) / sinf(cameraPitch);
    *screenOffsetY = DIORAMA_OVERWORLD_SCREEN_OFFSET;
}

uint32_t DioramaTerrain_FoldQuads(struct DioramaTerrainVertex *vertices, uint32_t vertexCount,
                                  uint32_t *indices, uint32_t *indexCount)
{
    uint32_t read = 0;
    uint32_t write = 0;
    uint32_t out = 0;

    while (read + 3 <= vertexCount)
    {
        /* AppendQuad's six: corners 0 1 2, then 0 2 3 */
        bool quad = read + 6 <= vertexCount
                 && memcmp(&vertices[read + 3], &vertices[read], sizeof(*vertices)) == 0
                 && memcmp(&vertices[read + 4], &vertices[read + 2], sizeof(*vertices)) == 0;

        /* write <= read: the vertices only ever move down */
        memmove(&vertices[write], &vertices[read], 3 * sizeof(*vertices));
        indices[out++] = write;
        indices[out++] = write + 1;
        indices[out++] = write + 2;
        if (quad)
        {
            vertices[write + 3] = vertices[read + 5];
            indices[out++] = write;
            indices[out++] = write + 2;
            indices[out++] = write + 3;
            write += 4;
            read += 6;
        }
        else
        {
            write += 3;
            read += 3;
        }
    }
    /* a last vertex or two that make no triangle drew nothing before either */
    *indexCount = out;
    return write;
}

bool DioramaTerrain_IsBoundsVisible(const struct DioramaTerrainBounds *bounds,
                                    float cameraX, float cameraZ,
                                    float cameraPitch, float focalLength)
{
    float cameraHeight;
    float cameraDistance;
    float screenOffsetY;
    const float pitchSin = sinf(cameraPitch);
    const float pitchCos = cosf(cameraPitch);
    const float nearDepth = 0.1f;
    const float farDepth = DIORAMA_FAR_DEPTH;
    bool outside[6] = {true, true, true, true, true, true};
    int i;

    DioramaCamera_GetRig(cameraPitch, &cameraHeight, &cameraDistance, &screenOffsetY);
    for (i = 0; i < 8; i++)
    {
        float x = (i & 1) ? bounds->maxX : bounds->minX;
        float y = (i & 2) ? bounds->maxY : bounds->minY;
        float z = (i & 4) ? bounds->maxZ : bounds->minZ;
        float relativeX = x - cameraX;
        float relativeZ = z - cameraZ;
        float depth = (cameraHeight - y) * pitchSin + (relativeZ + cameraDistance) * pitchCos;
        float vertical = -(cameraHeight - y) * pitchCos + (relativeZ + cameraDistance) * pitchSin;
        float clipX = relativeX * focalLength * 2.0f / 240.0f
                    * (gDioramaCameraRig.active ? gDioramaCameraRig.aspectCorrection : 1.0f);
        float clipY = screenOffsetY * depth + vertical * focalLength * 2.0f / 160.0f;

        if (depth >= nearDepth) outside[0] = false;
        if (depth <= farDepth) outside[1] = false;
        if (clipX >= -depth) outside[2] = false;
        if (clipX <= depth) outside[3] = false;
        if (clipY >= -depth) outside[4] = false;
        if (clipY <= depth) outside[5] = false;
    }
    for (i = 0; i < 6; i++)
        if (outside[i])
            return false;
    return true;
}

#endif
