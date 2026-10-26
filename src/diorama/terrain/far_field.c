#ifdef ENABLE_DIORAMA
/*
 * Distant scenery (see diorama/far_field.h). Project-owned (graphics
 * overhaul, 2026-10-10).
 */
#include <string.h>

#include "diorama/far_field.h"
#include "diorama/rules.h"
#include "diorama/tree_sprites.h"

/* Face shades: the top, then the sides facing south (towards the camera),
 * east or west, and north (the tree model's, terrain_mesh.c). */
#define SHADE_TOP 1.0f
#define SHADE_SOUTH 0.92f
#define SHADE_SIDE 0.82f
#define SHADE_NORTH 0.72f
/* Past the far blocks the columns' sides go down to here, under the haze. */
#define RIM_BOTTOM (-1.0f)
/* The open sea's level: the near sea's outdoors (buildings.c LevelWaterBodies, StaticWaterLevel). A far sea block at its
 * rules' height (the border block's: the land's) stood a level over the near sea, a step of water where the scene's own
 * cells end. */
#define SEA_LEVEL (-1.0f)

static DioramaFarBuildingHeight sBuildingHeight;

void DioramaFar_SetBuildingHeight(DioramaFarBuildingHeight buildingHeight)
{
    sBuildingHeight = buildingHeight;
}

static uint32_t Hash(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;

    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

static bool IsTree(uint8_t archetype)
{
    return archetype == DIORAMA_ARCHETYPE_TREE || archetype == DIORAMA_ARCHETYPE_ROUND_HULL
        || archetype == DIORAMA_ARCHETYPE_GROUPED_HULL || archetype == DIORAMA_ARCHETYPE_FOREST_WALL;
}

void DioramaFar_Column(const struct DioramaFarBlock *block, int blockX, int blockY,
                       struct DioramaFarColumn *column)
{
    float ground = 0.0f;
    float top = 0.0f;
    uint8_t archetype = DIORAMA_ARCHETYPE_GROUND;
    uint32_t hash = Hash(blockX, blockY);
    float rgb[3];
    float luma;
    int channel;

    memset(column, 0, sizeof(*column));
    if (!(block->flags & DIORAMA_FAR_VALID))
        return;
    column->present = true;
    if (block->flags & DIORAMA_FAR_SEA)
        top = SEA_LEVEL;
    else if ((block->flags & DIORAMA_FAR_SOURCE)
     && DioramaRules_StaticHeights(block->layoutId, block->mapGroup, block->mapNum,
                                   block->sourceX, block->sourceY, block->metatileId,
                                   &ground, &top, &archetype))
        column->tree = IsTree(archetype);
    else
        column->tree = DioramaTreeSprites_Part(false, block->metatileId) != DIORAMA_TREE_PART_NONE;
    if (column->tree)
    {
        /* a canopy, a little uneven from block to block */
        float canopy = ground + 1.8f + (float)(hash % 64) / 64.0f * 0.4f;

        if (top < canopy)
            top = canopy;
    }
    else if ((block->flags & DIORAMA_FAR_SOURCE) && sBuildingHeight != NULL)
    {
        float model;

        /* a modelled building's cell: as high as the model stands there */
        if (sBuildingHeight(block->layoutId, block->sourceX, block->sourceY, &model)
         && ground + model > top)
            top = ground + model;
    }
    if (top < RIM_BOTTOM)
        top = RIM_BOTTOM;
    if (top > 8.0f)
        top = 8.0f;
    column->top = top;
    for (channel = 0; channel < 3; channel++)
    {
        uint32_t value = (block->color >> (channel * 5)) & 31;

        rgb[channel] = (float)((value << 3) | (value >> 2));
    }
    /* a fifth of the way to grey: the far art seen through the air; a
     * tree's average takes its dark outline in, the crowns read lighter */
    luma = rgb[0] * 0.299f + rgb[1] * 0.587f + rgb[2] * 0.114f;
    for (channel = 0; channel < 3; channel++)
    {
        float value = (rgb[channel] + (luma - rgb[channel]) * 0.2f) * (column->tree ? 1.15f : 1.0f);

        if (value > 255.0f)
            value = 255.0f;
        column->rgba |= (uint32_t)(value + 0.5f) << (channel * 8);
    }
    column->rgba |= UINT32_C(0xFF000000);
}

static int FloorDiv2(int value)
{
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

struct Builder
{
    struct DioramaTerrainVertex *vertices;
    uint32_t capacity;
    uint32_t count;
    bool complete;
};

static void Vertex(struct Builder *builder, float x, float y, float z, float shade, uint32_t rgba)
{
    struct DioramaTerrainVertex *vertex = &builder->vertices[builder->count++];

    memset(vertex, 0, sizeof(*vertex));
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->shade = shade;
    vertex->textureLayer = DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR;
    vertex->color = rgba;
}

static bool Room(struct Builder *builder, uint32_t vertices)
{
    if (builder->count + vertices <= builder->capacity)
        return true;
    builder->complete = false;
    return false;
}

static void Quad(struct Builder *builder, const float corners[4][3], float shade, uint32_t rgba)
{
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    int i;

    if (!Room(builder, 6))
        return;
    for (i = 0; i < 6; i++)
        Vertex(builder, corners[order[i]][0], corners[order[i]][1], corners[order[i]][2], shade, rgba);
}

/* A colour a little lighter or darker. */
static uint32_t Tint(uint32_t rgba, float brightness)
{
    uint32_t result = rgba & UINT32_C(0xFF000000);
    int channel;

    for (channel = 0; channel < 3; channel++)
    {
        float value = (float)((rgba >> (channel * 8)) & 0xFF) * brightness;

        if (value > 255.0f)
            value = 255.0f;
        result |= (uint32_t)(value + 0.5f) << (channel * 8);
    }
    return result;
}

/* A flat top over cells x0..x1, y0..y1 (map coordinates, inclusive); cell
 * (x, y)'s centre is (x, -y) in the scene. */
static void Top(struct Builder *builder, int x0, int y0, int x1, int y1, float top, uint32_t rgba)
{
    float corners[4][3] = {
        {x0 - 0.5f, top, -(y0 - 0.5f)},
        {x1 + 0.5f, top, -(y0 - 0.5f)},
        {x1 + 0.5f, top, -(y1 + 0.5f)},
        {x0 - 0.5f, top, -(y1 + 0.5f)},
    };

    Quad(builder, corners, SHADE_TOP, rgba);
}

/* A tree cell's top: a low pyramid, its four faces shaded as if lit from
 * the south west, so a forest reads as a bumpy canopy and not a table. */
static void Canopy(struct Builder *builder, int x, int y, float top, uint32_t rgba)
{
    static const float sShades[4] = {SHADE_NORTH + 0.08f, SHADE_TOP, 1.0f - 0.04f, SHADE_SIDE};
    float north = -(y - 0.5f);
    float south = -(y + 0.5f);
    float corners[4][2] = {{x - 0.5f, north}, {x + 0.5f, north}, {x + 0.5f, south}, {x - 0.5f, south}};
    float peak = top + 0.35f;
    int face;

    if (!Room(builder, 12))
        return;
    /* faces: north, east, south, west (corner pairs 0-1, 1-2, 2-3, 3-0) */
    for (face = 0; face < 4; face++)
    {
        const float *a = corners[face];
        const float *b = corners[(face + 1) % 4];

        Vertex(builder, a[0], top, a[1], sShades[face], rgba);
        Vertex(builder, b[0], top, b[1], sShades[face], rgba);
        Vertex(builder, (float)x, peak, -(float)y, sShades[face], rgba);
    }
}

/* The side of cell (x, y) towards (dx, dy), from `bottom` up to `top`. */
static void Side(struct Builder *builder, int x, int y, int dx, int dy, float bottom, float top,
                 uint32_t rgba)
{
    float corners[4][3];
    float shade;
    float ax;
    float az;
    float bx;
    float bz;

    if (dy != 0)
    {
        float z = dy > 0 ? -(y + 0.5f) : -(y - 0.5f);

        ax = x - 0.5f; bx = x + 0.5f;
        az = bz = z;
        shade = dy > 0 ? SHADE_SOUTH : SHADE_NORTH;
    }
    else
    {
        float sideX = dx > 0 ? x + 0.5f : x - 0.5f;

        ax = bx = sideX;
        az = -(y - 0.5f); bz = -(y + 0.5f);
        shade = SHADE_SIDE;
    }
    corners[0][0] = ax; corners[0][1] = top; corners[0][2] = az;
    corners[1][0] = bx; corners[1][1] = top; corners[1][2] = bz;
    corners[2][0] = bx; corners[2][1] = bottom; corners[2][2] = bz;
    corners[3][0] = ax; corners[3][1] = bottom; corners[3][2] = az;
    Quad(builder, corners, shade, rgba);
}

struct Field
{
    const struct DioramaSceneSnapshot *snapshot;
    const struct DioramaFarColumn *columns;
    int innerLeft;
    int innerTop;
    int innerRight;  /* exclusive */
    int innerBottom;
};

static bool InScene(const struct Field *field, int x, int y)
{
    return x >= field->innerLeft && x < field->innerRight
        && y >= field->innerTop && y < field->innerBottom;
}

/* A far cell's column, NULL past the far blocks or for a missing block. */
static const struct DioramaFarColumn *CellColumn(const struct Field *field, int x, int y)
{
    int i = FloorDiv2(x - field->snapshot->farOriginX);
    int j = FloorDiv2(y - field->snapshot->farOriginY);
    const struct DioramaFarColumn *column;

    if (i < 0 || j < 0 || i >= DIORAMA_FAR_BLOCKS || j >= DIORAMA_FAR_BLOCKS)
        return NULL;
    column = &field->columns[j * DIORAMA_FAR_BLOCKS + i];
    return column->present ? column : NULL;
}

/* A far cell's own top: a canopy's cells each a little higher or lower. */
static float CellTop(const struct DioramaFarColumn *column, int x, int y)
{
    if (!column->tree)
        return column->top;
    return column->top + ((float)(Hash(x, y) % 32) / 32.0f - 0.4f) * 0.4f;
}

/* A canopy's cells a little lighter or darker; ground and water even (a
 * pattern there reads as a grid laid over the sea). */
static uint32_t CellColour(const struct DioramaFarColumn *column, int x, int y)
{
    if (!column->tree)
        return column->rgba;
    return Tint(column->rgba, 0.94f + (float)(Hash(y, x) % 32) / 32.0f * 0.12f);
}

uint32_t DioramaFar_BuildMesh(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaTerrainVertex *vertices, uint32_t capacity,
                              struct DioramaFarSector *sectors, bool *complete)
{
    static struct DioramaFarColumn sColumns[DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS];
    static const int8_t sDirections[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    struct Builder builder = {vertices, capacity, 0, true};
    struct Field field;
    int sector;
    int i;
    int j;

    if (complete != NULL)
        *complete = true;
    if (sectors != NULL)
        memset(sectors, 0, DIORAMA_FAR_SECTORS * sizeof(*sectors));
    if (!snapshot->farOn)
        return 0;
    field.snapshot = snapshot;
    field.columns = sColumns;
    field.innerLeft = snapshot->gridOriginX;
    field.innerTop = snapshot->gridOriginY;
    field.innerRight = field.innerLeft + DIORAMA_GRID_WIDTH;
    field.innerBottom = field.innerTop + DIORAMA_GRID_HEIGHT;
    for (j = 0; j < DIORAMA_FAR_BLOCKS; j++)
        for (i = 0; i < DIORAMA_FAR_BLOCKS; i++)
            DioramaFar_Column(&snapshot->farBlocks[j * DIORAMA_FAR_BLOCKS + i],
                              FloorDiv2(snapshot->farOriginX) + i, FloorDiv2(snapshot->farOriginY) + j,
                              &sColumns[j * DIORAMA_FAR_BLOCKS + i]);
    for (sector = 0; sector < DIORAMA_FAR_SECTORS; sector++)
    {
        int sectorI = sector % DIORAMA_FAR_SECTORS_ACROSS * DIORAMA_FAR_SECTOR_BLOCKS;
        int sectorJ = sector / DIORAMA_FAR_SECTORS_ACROSS * DIORAMA_FAR_SECTOR_BLOCKS;
        uint32_t first = builder.count;

        for (j = sectorJ; j < sectorJ + DIORAMA_FAR_SECTOR_BLOCKS; j++)
            for (i = sectorI; i < sectorI + DIORAMA_FAR_SECTOR_BLOCKS; i++)
            {
                const struct DioramaFarColumn *column = &sColumns[j * DIORAMA_FAR_BLOCKS + i];
                int x0 = snapshot->farOriginX + i * DIORAMA_FAR_BLOCK_CELLS;
                int y0 = snapshot->farOriginY + j * DIORAMA_FAR_BLOCK_CELLS;
                int outsideCount = 0;
                int cell;

                if (!column->present)
                    continue;
                for (cell = 0; cell < 4; cell++)
                    outsideCount += !InScene(&field, x0 + (cell & 1), y0 + (cell >> 1));
                if (outsideCount == 0)
                    continue;
                /* a flat block whole: one top */
                if (outsideCount == 4 && !column->tree)
                    Top(&builder, x0, y0, x0 + 1, y0 + 1, column->top, CellColour(column, x0, y0));
                for (cell = 0; cell < 4; cell++)
                {
                    int x = x0 + (cell & 1);
                    int y = y0 + (cell >> 1);
                    float top;
                    uint32_t colour;
                    int direction;

                    if (InScene(&field, x, y))
                        continue;
                    top = CellTop(column, x, y);
                    colour = CellColour(column, x, y);
                    if (column->tree)
                        Canopy(&builder, x, y, top, colour);
                    else if (outsideCount != 4)
                        Top(&builder, x, y, x, y, top, colour);
                    for (direction = 0; direction < 4; direction++)
                    {
                        int nx = x + sDirections[direction][0];
                        int ny = y + sDirections[direction][1];
                        const struct DioramaFarColumn *neighbour;
                        float bottom;

                        /* next to the scene's own cells: down to its ground,
                         * else the sky shows through the column's open side */
                        if (InScene(&field, nx, ny))
                            bottom = 0.0f;
                        else if ((neighbour = CellColumn(&field, nx, ny)) != NULL)
                            bottom = CellTop(neighbour, nx, ny);
                        else
                            bottom = RIM_BOTTOM;
                        if (bottom < top - 0.01f)
                            Side(&builder, x, y, sDirections[direction][0], sDirections[direction][1],
                                 bottom, top, Tint(colour, 0.9f));
                    }
                }
            }
        if (sectors != NULL)
        {
            struct DioramaFarSector *out = &sectors[sector];
            uint32_t v;

            out->first = first;
            out->count = builder.count - first;
            for (v = first; v < builder.count; v++)
            {
                const struct DioramaTerrainVertex *vertex = &vertices[v];

                if (v == first || vertex->x < out->bounds.minX) out->bounds.minX = vertex->x;
                if (v == first || vertex->y < out->bounds.minY) out->bounds.minY = vertex->y;
                if (v == first || vertex->z < out->bounds.minZ) out->bounds.minZ = vertex->z;
                if (v == first || vertex->x > out->bounds.maxX) out->bounds.maxX = vertex->x;
                if (v == first || vertex->y > out->bounds.maxY) out->bounds.maxY = vertex->y;
                if (v == first || vertex->z > out->bounds.maxZ) out->bounds.maxZ = vertex->z;
            }
        }
    }
    if (complete != NULL)
        *complete = builder.complete;
    return builder.count;
}

#endif
