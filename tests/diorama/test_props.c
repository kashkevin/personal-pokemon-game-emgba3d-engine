// World detail objects (diorama/props.h): the catalogue, the art each prop
// stands up and the instances a snapshot's cells give.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/metatile_atlas.h"
#include "diorama/props.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static int sChecks;

static void Check(bool condition, const char *expression, int line)
{
    sChecks++;
    if (!condition)
    {
        fprintf(stderr, "test_props.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

/* Layout ids (include/constants/layouts.h): General + Rustboro, General +
 * Petalburg. */
#define LAYOUT_PETALBURG_CITY 1
#define LAYOUT_SLATEPORT_CITY 2
#define LAYOUT_RUSTBORO_CITY 4
#define LAYOUT_FORTREE_CITY 5
#define LAYOUT_LITTLEROOT_TOWN 10
#define LAYOUT_DEWFORD_TOWN 12
#define LAYOUT_LAVARIDGE_TOWN 13
#define LAYOUT_ROUTE110 26
#define LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST 265
#define LAYOUT_ROUTE124 40
#define LAYOUT_ROUTE126 42
#define LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST 345

#define GRASS 0x001
#define FLOWERS 0x004
#define SIGN 0x003
#define WOODEN_SIGN 0x01B
#define TALL_GRASS 0x00D
#define ROCK 0x0E2
#define LAMP_HEAD 0x212
#define LAMP_POST 0x21A
#define IRON_FENCE 0x2A7
#define IRON_FENCE_NS 0x318

static struct DioramaSceneSnapshot sSnapshot;
static struct DioramaResolvedCell sResolved[DIORAMA_MAX_VISIBLE_CELLS];
static struct DioramaPropInstance sInstances[DIORAMA_MAX_VISIBLE_CELLS * 2];

static bool Near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void TestCatalogue(void)
{
    unsigned count;
    const struct DioramaPropTileset *tilesets = DioramaProps_Tilesets(&count);
    unsigned t;

    CHECK(count >= 2);
    for (t = 0; t < count; t++)
    {
        unsigned i;
        size_t r;
        bool known = false;

        /* Every tileset is one the rules know. */
        for (r = 0; r < gDioramaTilesetV2Count; r++)
            known |= gDioramaTilesetsV2[r].symbol != NULL
                  && strcmp(gDioramaTilesetsV2[r].symbol, tilesets[t].symbol) == 0;
        CHECK(known);
        for (i = 0; i < tilesets[t].count; i++)
        {
            const struct DioramaProp *prop = &tilesets[t].props[i];
            unsigned j;

            CHECK(prop->metatileId < DIORAMA_TILE_COUNT);
            CHECK(prop->kind > DIORAMA_PROP_KIND_NONE && prop->kind < DIORAMA_PROP_KIND_COUNT);
            CHECK(prop->form <= DIORAMA_PROP_RELIEF);
            CHECK(prop->layer <= 1);
            CHECK(prop->ground <= DIORAMA_PROP_GROUND_NEIGHBOUR);
            CHECK(prop->artTop < DioramaProps_ArtBottom(prop) && DioramaProps_ArtBottom(prop) <= 16);
            /* A slab is at most a tile thick; a block or a mound at most
             * three deep (a stall's roof). */
            CHECK(prop->depth <= (prop->form == DIORAMA_PROP_BLOCK || prop->form == DIORAMA_PROP_MOUND
                                  ? 48 : 16));
            /* Lifted at most four tiles; only a block or a mound slopes. */
            CHECK(prop->lift16 <= 64);
            CHECK(prop->rise16 == 0 || prop->form == DIORAMA_PROP_BLOCK || prop->form == DIORAMA_PROP_MOUND);
            CHECK(prop->rise16 <= 32);
            CHECK(prop->facing <= 1);
            CHECK(prop->standOn <= 2);
            CHECK(prop->keyIndex < 16 && prop->shadowIndex < 16 && prop->shadowFromRow <= 16);
            /* The primary tileset's props are in its first 512 metatiles, a
             * secondary's from 512. */
            CHECK(strcmp(tilesets[t].symbol, "gTileset_General") == 0 ? prop->metatileId < 0x200
                                                                       : prop->metatileId >= 0x200);
            for (j = 0; j < i; j++)
                CHECK(tilesets[t].props[j].metatileId != prop->metatileId);
            CHECK(strcmp(DioramaProps_KindName(prop->kind), "?") != 0);
        }
    }
}

static void TestFind(void)
{
    const struct DioramaProp *prop;

    DioramaProps_SetEnabled(true);
    CHECK(DioramaProps_KnownLayout(LAYOUT_LITTLEROOT_TOWN));
    CHECK(!DioramaProps_KnownLayout(0));
    prop = DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, FLOWERS);
    CHECK(prop != NULL && prop->kind == DIORAMA_PROP_KIND_FLOWERS && prop->form == DIORAMA_PROP_CARDS);
    CHECK(DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, GRASS) == NULL);
    prop = DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, TALL_GRASS);
    CHECK(prop != NULL && prop->kind == DIORAMA_PROP_KIND_GRASS
       && prop->ground == DIORAMA_PROP_GROUND_KEEP);
    /* The secondary's ids mean another drawing on each tileset: 0x212 is
     * Rustboro's lamp head, a house wall in Littleroot. */
    prop = DioramaProps_Find(LAYOUT_RUSTBORO_CITY, LAMP_HEAD);
    CHECK(prop != NULL && prop->kind == DIORAMA_PROP_KIND_LAMP && prop->standOn == 1
       && (prop->flags & DIORAMA_PROP_LIGHT));
    CHECK(DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, LAMP_HEAD) == NULL);
    /* The primary's are General's everywhere. */
    CHECK(DioramaProps_Find(LAYOUT_RUSTBORO_CITY, FLOWERS) == DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, FLOWERS));
    CHECK(DioramaProps_Find(0, FLOWERS) == NULL);
    CHECK(DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, 1024) == NULL);
    /* Route 110's low white wall: its stones (palette 3) stand, blocked cells only. */
    prop = DioramaProps_Find(LAYOUT_ROUTE110, 0x045);
    CHECK(prop != NULL && prop->form == DIORAMA_PROP_BLOCK && prop->onlyPalette == 4
       && (prop->flags & DIORAMA_PROP_BLOCKED) && prop->ground == DIORAMA_PROP_GROUND_KEEP);
    /* The towns' gym signs stand: the top on the board a cell south, the board
     * with its painted shadow left out - General's (Petalburg's and others')
     * and three towns' own copies (the object audit of 2026-11-14: Dewford's
     * and Fortree's lay flat, Rustboro's top behind its standing board).
     * Lavaridge's stands with its gym's front, over whose corner it is drawn. */
    {
        static const struct { uint16_t layout, top, board; } signs[] = {
            { LAYOUT_PETALBURG_CITY, 0x130, 0x131 }, { LAYOUT_RUSTBORO_CITY, 0x2CC, 0x2D4 },
            { LAYOUT_DEWFORD_TOWN, 0x208, 0x210 }, { LAYOUT_FORTREE_CITY, 0x255, 0x25D },
        };
        unsigned s;

        for (s = 0; s < sizeof(signs) / sizeof(signs[0]); s++)
        {
            const struct DioramaProp *top = DioramaProps_Find(signs[s].layout, signs[s].top);
            const struct DioramaProp *board = DioramaProps_Find(signs[s].layout, signs[s].board);

            CHECK(top != NULL && top->kind == DIORAMA_PROP_KIND_SIGN && top->form == DIORAMA_PROP_SLAB
               && top->standOn == 1 && top->layer == 1 && top->ground == DIORAMA_PROP_GROUND_LOWER);
            CHECK(board != NULL && board->kind == DIORAMA_PROP_KIND_SIGN && board->form == DIORAMA_PROP_SLAB
               && board->standOn == 0 && board->shadowIndex == 6 && board->shadowFromRow == 11
               && board->depth == top->depth);
        }
        CHECK(DioramaProps_Find(LAYOUT_LAVARIDGE_TOWN, 0x29F) == NULL
           && DioramaProps_Find(LAYOUT_LAVARIDGE_TOWN, 0x2A7) == NULL);
    }
}

/* Tile graphics: tile 1 has colour index (x + y) % 16 at each pixel, tile 2
 * index 5 everywhere. */
static uint8_t sTiles[DIORAMA_TILE_GRAPHICS_SIZE];
static uint16_t sPalette[DIORAMA_FADED_PALETTE_ENTRIES];

static void MakeTiles(void)
{
    unsigned x;
    unsigned y;

    memset(sTiles, 0, sizeof(sTiles));
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x += 2)
        {
            uint8_t low = (uint8_t)((x + y) % 16);
            uint8_t high = (uint8_t)((x + 1 + y) % 16);

            sTiles[1 * DIORAMA_TILE_BYTES + y * 4 + x / 2] = (uint8_t)(low | (high << 4));
            sTiles[2 * DIORAMA_TILE_BYTES + y * 4 + x / 2] = 0x55;
        }
    for (x = 0; x < DIORAMA_FADED_PALETTE_ENTRIES; x++)
        sPalette[x] = (uint16_t)(x & 0x7FFF);
}

static void TestComposeArt(void)
{
    struct DioramaProp prop;
    uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT];
    uint32_t pixels[16 * 16];
    unsigned i;

    MakeTiles();
    /* Upper layer: tile 1 in its four quarters, palette 3. */
    for (i = 0; i < 4; i++)
    {
        entries[i] = 2;
        entries[4 + i] = (uint16_t)(1 | (3 << 12));
    }
    memset(&prop, 0, sizeof(prop));
    prop.layer = 1;
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    /* Index 0 (x + y = 0, 8, 16...) transparent, the rest opaque in palette 3. */
    CHECK(pixels[0] == 0);
    CHECK(pixels[1] == DioramaMetatile_ConvertColor(sPalette[3 * 16 + 1], false));
    CHECK(pixels[9 * 16 + 9] == DioramaMetatile_ConvertColor(sPalette[3 * 16 + 2], false));
    /* The key colour left out. */
    prop.keyIndex = 2;
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    CHECK(pixels[2] == 0 && pixels[1 * 16 + 1] == 0 && pixels[3] != 0);
    /* The painted shadow left out only from its row down. */
    prop.keyIndex = 0;
    prop.shadowIndex = 5;
    prop.shadowFromRow = 12;
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    CHECK(pixels[0 * 16 + 5] != 0);              /* row 0, index 5: kept */
    CHECK(pixels[12 * 16 + 1] == 0);             /* row 12: tile row 4, x 1: index 5 */
    CHECK(pixels[12 * 16 + 2] != 0);             /* index 6 */
    /* Rows outside artTop .. artBottom left out. */
    prop.shadowIndex = 0;
    prop.artTop = 2;
    prop.artBottom = 10;
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    CHECK(pixels[1 * 16 + 3] == 0 && pixels[10 * 16 + 3] == 0);
    CHECK(pixels[2 * 16 + 3] != 0 && pixels[9 * 16 + 3] != 0);
    /* The lower layer: tile 2, index 5 everywhere. */
    memset(&prop, 0, sizeof(prop));
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    CHECK(pixels[0] == DioramaMetatile_ConvertColor(sPalette[5], false) && pixels[255] == pixels[0]);
    /* Only the tiles of one palette: the upper right quarter in palette 4, the rest in 3. */
    entries[5] = (uint16_t)(1 | (4 << 12));
    prop.layer = 1;
    prop.onlyPalette = 4 + 1;
    DioramaProps_ComposeArt(&prop, sTiles, sPalette, entries, pixels);
    CHECK(pixels[1 * 16 + 1] == 0 && pixels[9 * 16 + 9] == 0);
    CHECK(pixels[1 * 16 + 9] == DioramaMetatile_ConvertColor(sPalette[4 * 16 + 2], false));
}

/* A snapshot of Littleroot's (or Rustboro's) tiles round map cell (60, 60):
 * all grass, every cell on the ground at height `ground`. */
static void MakeSnapshot(uint16_t layoutId, float ground)
{
    int i;

    memset(&sSnapshot, 0, sizeof(sSnapshot));
    memset(sResolved, 0, sizeof(sResolved));
    sSnapshot.mapLayoutId = layoutId;
    sSnapshot.gridOriginX = 60 - DIORAMA_GRID_WIDTH / 2;
    sSnapshot.gridOriginY = 60 - DIORAMA_GRID_HEIGHT / 2;
    sSnapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        struct DioramaCellSnapshot *cell = &sSnapshot.cells[i];

        cell->mapX = (int16_t)(sSnapshot.gridOriginX + i % DIORAMA_GRID_WIDTH);
        cell->mapY = (int16_t)(sSnapshot.gridOriginY + i / DIORAMA_GRID_WIDTH);
        cell->metatileId = GRASS;
        cell->sourceLayoutId = layoutId;
        cell->sourceMapX = cell->mapX;
        cell->sourceMapY = cell->mapY;
        cell->flags = DIORAMA_CELL_SOURCE_VALID;
        sResolved[i].groundHeight = ground;
        sResolved[i].topHeight = ground;
    }
}

static int CellIndex(int mapX, int mapY)
{
    return (mapY - sSnapshot.gridOriginY) * DIORAMA_GRID_WIDTH + (mapX - sSnapshot.gridOriginX);
}

static void Put(int mapX, int mapY, uint16_t metatileId)
{
    sSnapshot.cells[CellIndex(mapX, mapY)].metatileId = metatileId;
}

/* A modelled building standing in the sign's cell (buildings.h). */
static bool TakeSignCell(uint16_t layout, int x, int y)
{
    return layout == LAYOUT_LITTLEROOT_TOWN && x == 58 && y == 60;
}

static void TestBuild(void)
{
    bool complete;
    unsigned count;

    MakeSnapshot(LAYOUT_LITTLEROOT_TOWN, 0.5f);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 0 && complete);
    /* Flowers: two cards on the ground, the art's top half a quarter tile
     * north of the cell's middle, its bottom half a quarter south. */
    Put(60, 61, FLOWERS);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 2 && complete);
    CHECK(sInstances[0].cellIndex == CellIndex(60, 61));
    CHECK(Near(sInstances[0].x, 60.0f) && Near(sInstances[0].y, 0.5f));
    CHECK(Near(sInstances[0].z, -61.0f + 0.25f) && sInstances[0].artTop == 0 && sInstances[0].artBottom == 8);
    CHECK(Near(sInstances[1].z, -61.0f - 0.25f) && sInstances[1].artTop == 8 && sInstances[1].artBottom == 16);
    CHECK(sInstances[0].sway > 0.0f && sInstances[0].phase != sInstances[1].phase);
    CHECK(sInstances[0].artSlot == FLOWERS);
    /* A sign: one slab, as thick as listed. */
    Put(58, 60, WOODEN_SIGN);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 3);
    CHECK(sInstances[0].prop->metatileId == WOODEN_SIGN && Near(sInstances[0].x, 58.0f)
       && Near(sInstances[0].z, -60.0f) && Near(sInstances[0].yaw, 0.0f)
       && Near(sInstances[0].depth, sInstances[0].prop->depth / 16.0f) && sInstances[0].sway == 0.0f);
    /* A rock: one relief, its back on the cell's north edge, bulging south as
     * far as listed. */
    Put(62, 60, ROCK);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 4);
    CHECK(sInstances[1].prop->metatileId == ROCK && sInstances[1].prop->form == DIORAMA_PROP_RELIEF);
    CHECK(Near(sInstances[1].x, 62.0f) && Near(sInstances[1].depth, sInstances[1].prop->depth / 16.0f)
       && Near(sInstances[1].z + sInstances[1].depth * 0.5f, -60.0f + 0.5f) && Near(sInstances[1].width, 1.0f));
    /* Too many: the build says so. */
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 3, &complete);
    CHECK(count == 2 && !complete);
    /* A cell the rules already stand up as a pixel object is left to them. */
    sResolved[CellIndex(58, 60)].pixelObjectId = 1;
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 3);
    sResolved[CellIndex(58, 60)].pixelObjectId = 0;
    /* So is a cell a modelled building stands in. */
    DioramaProps_SetCellTaken(TakeSignCell);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 3);
    DioramaProps_SetCellTaken(NULL);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 4);
    /* A storm bends them further; fog leaves them stiller. */
    {
        float calm = sInstances[sizeof(sInstances) > 0 ? 1 : 0].sway;

        sSnapshot.weather = 5; /* WEATHER_RAIN_THUNDERSTORM */
        count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
        CHECK(sInstances[count - 1].sway > calm * 2.0f);
        sSnapshot.weather = 0;
        CHECK(DioramaProps_Wind(0) == 1.0f && DioramaProps_Wind(14) <= 1.0f);
    }
    /* Someone walking through the flowers: they rustle, the others do not. */
    sSnapshot.objectCount = 1;
    sSnapshot.objects[0].active = 1;
    sSnapshot.objects[0].currentMapX = 60;
    sSnapshot.objects[0].currentMapY = 61;
    sSnapshot.objects[0].previousMapX = 60;
    sSnapshot.objects[0].previousMapY = 60;
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    {
        unsigned i;
        unsigned rustling = 0;

        for (i = 0; i < count; i++)
        {
            if (sInstances[i].rustle > 0.0f)
                rustling++;
            CHECK(sInstances[i].rustle == 0.0f || sInstances[i].cellIndex == CellIndex(60, 61));
        }
        CHECK(rustling == 2);
    }
    sSnapshot.objects[0].flags = DIORAMA_OBJECT_INVISIBLE;
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(sInstances[count - 1].rustle == 0.0f);
    sSnapshot.objectCount = 0;
    /* Switched off: nothing. */
    DioramaProps_SetEnabled(false);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 0);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(60, 61)]) == NULL);
    DioramaProps_SetEnabled(true);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(60, 61)]) != NULL);
    /* A wall stands only where its cell is blocked. */
    MakeSnapshot(LAYOUT_ROUTE110, 0.0f);
    Put(60, 61, 0x045);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(60, 61)]) == NULL);
    sSnapshot.cells[CellIndex(60, 61)].collision = 1;
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(60, 61)]) != NULL);
}

/* Tall grass is drawn flat (the terrain keeps its art) unless someone stands in it: then only its
 * front blades stand, one card a quarter tile south of the walker's cell middle. Two cards on every
 * cell over the same blades drawn flat made a field of it dense stripes (2026-10-17). */
static void TestTallGrass(void)
{
    bool complete;
    unsigned count;

    MakeSnapshot(LAYOUT_LITTLEROOT_TOWN, 0.5f);
    Put(60, 60, TALL_GRASS);
    Put(61, 60, TALL_GRASS);
    Put(62, 60, FLOWERS);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 2 && complete);                       /* the flowers' two cards, no grass card */
    CHECK(sInstances[0].prop->metatileId == FLOWERS && sInstances[1].prop->metatileId == FLOWERS);
    sSnapshot.objectCount = 1;
    sSnapshot.objects[0].active = 1;
    sSnapshot.objects[0].flags = 0;
    sSnapshot.objects[0].currentMapX = sSnapshot.objects[0].previousMapX = 61;
    sSnapshot.objects[0].currentMapY = sSnapshot.objects[0].previousMapY = 60;
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 3);
    {
        unsigned i, grass = 0;

        for (i = 0; i < count; i++)
        {
            if (sInstances[i].prop->metatileId != TALL_GRASS)
                continue;
            grass++;
            CHECK(sInstances[i].cellIndex == CellIndex(61, 60) && sInstances[i].rustle > 0.0f);
            CHECK(Near(sInstances[i].z, -60.0f - 0.25f) && sInstances[i].artTop == 8 && sInstances[i].artBottom == 16);
        }
        CHECK(grass == 1);
    }
    sSnapshot.objects[0].flags = DIORAMA_OBJECT_INVISIBLE;   /* someone hidden: no grass at their feet */
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 2);
    sSnapshot.objectCount = 0;
}

static void TestLampAndFences(void)
{
    bool complete;
    unsigned count;
    unsigned i;

    /* A street lamp: the post at (60, 61), its head the cell north. The head
     * stands on the post's cell, a tile up, on its ground. */
    MakeSnapshot(LAYOUT_RUSTBORO_CITY, 0.0f);
    Put(60, 61, LAMP_POST);
    Put(60, 60, LAMP_HEAD);
    sResolved[CellIndex(60, 61)].topHeight = 0.25f;
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 2);
    for (i = 0; i < count; i++)
    {
        CHECK(Near(sInstances[i].x, 60.0f) && Near(sInstances[i].z, -61.0f));
        if (sInstances[i].prop->metatileId == LAMP_HEAD)
            CHECK(Near(sInstances[i].y, 1.25f) && (sInstances[i].flags & DIORAMA_PROP_LIGHT));
        else
            CHECK(Near(sInstances[i].y, 0.25f));
    }
    /* A head without its post under it is left out. */
    Put(60, 61, GRASS);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 0);
    MakeSnapshot(LAYOUT_RUSTBORO_CITY, 0.0f);
    Put(60, DIORAMA_GRID_HEIGHT / 2 + 60, LAMP_HEAD); /* the grid's last row: no cell south */
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 0);
    /* An iron fence running north-south faces east along its line, with the
     * east-west railing's art. */
    MakeSnapshot(LAYOUT_RUSTBORO_CITY, 0.0f);
    Put(60, 60, IRON_FENCE_NS);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 1 && Near(sInstances[0].yaw, 1.5707963f));
    CHECK(Near(sInstances[0].x, 60.0f + sInstances[0].prop->offsetX / 16.0f) && sInstances[0].prop->offsetX > 0);
    CHECK(sInstances[0].artSlot == IRON_FENCE);
}

/* A market stall in Slateport (phase 4): the canopy's drawing lies on a roof
 * lifted over the stall's floor, sloping up to the back; its front hangs from
 * the roof's edge; the posts reach the roof. */
#define STALL_ROOF_MIDDLE 0x20E
#define STALL_FRONT_MIDDLE 0x2F9
#define STALL_POST_LEFT 0x23D

/* The Battle Frontier's stone arch: 5 x 5 cells, the pillars' feet its last row (map row 60 here). Every cell
 * of it stands on one plane, the feet row's, 5 pixels south of its middle; each lifted onto the cell under it
 * in the art (the feet stand 13 rows); the opening under the arch (0x214, nothing drawn) is the plaza's. */
static void TestFrontierArch(void)
{
    static const uint16_t arch[5][5] = {
        { 0, 0x23C, 0x23D, 0x23E, 0 },
        { 0x20E, 0x20B, 0x20C, 0x20D, 0x20F },
        { 0x216, 0x213, 0x214, 0x215, 0x217 },
        { 0x21E, 0, 0, 0, 0x21E },
        { 0x226, 0, 0, 0, 0x226 },
    };
    bool complete;
    unsigned count;
    unsigned i;
    int x;
    int y;

    MakeSnapshot(LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST, 0.5f);
    for (y = 0; y < 5; y++)
        for (x = 0; x < 5; x++)
            if (arch[y][x] != 0)
                Put(58 + x, 56 + y, arch[y][x]);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 64, &complete);
    CHECK(count == 16 && complete);
    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &sSnapshot.cells[sInstances[i].cellIndex];
        int up = 60 - cell->mapY;

        CHECK(sInstances[i].prop->metatileId != 0x214);
        CHECK(Near(sInstances[i].z, -(60.0f + 5.0f / 16.0f)) && Near(sInstances[i].x, (float)cell->mapX));
        CHECK(Near(sInstances[i].y, 0.5f + (up == 0 ? 0.0f : (16 * up - 3) / 16.0f)));
        CHECK(sInstances[i].prop->artBottom == (up == 0 ? 13 : 0) && sInstances[i].prop->depth == 8);
    }
    /* On another tileset these ids are something else. */
    CHECK(DioramaProps_Find(LAYOUT_LITTLEROOT_TOWN, 0x23C) == NULL);
}

/* The Battle Arena's gate: 7 x 5 cells, the posts' feet its last row (map row 60 here); the same, on one plane
 * 2 pixels south of the feet row's middle, the feet standing 11 rows; the path through it (General's) is left. */
static void TestArenaGate(void)
{
    static const uint16_t gate[5][7] = {
        { 0x268, 0x269, 0x269, 0x269, 0x269, 0x269, 0x26C },
        { 0x270, 0x271, 0x272, 0x272, 0x272, 0x273, 0x274 },
        { 0x27B, 0x27C, 0x263, 0x264, 0x265, 0x27B, 0x27C },
        { 0x278, 0x279, 0x1D8, 0x1D9, 0x1DA, 0x278, 0x279 },
        { 0x26A, 0x26B, 0x362, 0x1D9, 0x363, 0x26A, 0x26B },
    };
    bool complete;
    unsigned count;
    unsigned i;
    int x;
    int y;

    MakeSnapshot(LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST, 0.0f);
    for (y = 0; y < 5; y++)
        for (x = 0; x < 7; x++)
            Put(57 + x, 56 + y, gate[y][x]);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 64, &complete);
    CHECK(count == 7 + 7 + 7 + 4 + 6 && complete);
    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &sSnapshot.cells[sInstances[i].cellIndex];
        int up = 60 - cell->mapY;

        CHECK(sInstances[i].prop->metatileId >= 0x200);
        CHECK(Near(sInstances[i].z, -(60.0f + 2.0f / 16.0f)));
        CHECK(Near(sInstances[i].y, up == 0 ? 0.0f : (16 * up - 5) / 16.0f));
        CHECK(sInstances[i].prop->artBottom == (up == 0 ? 11 : 0));
    }
}

/* The sea rocks' two halves (Mossdeep's tileset): a rock two cells wide stands, its west half beside its east;
 * the same halves at a formation's corners, a cell of its top between them, stand neither (the object audit of
 * 2026-11-14: thin cards there), and their cells keep their own drawing. */
static void TestSeaRockHalves(void)
{
    bool complete;
    unsigned count;
    uint16_t metatile;
    uint8_t layer;

    MakeSnapshot(LAYOUT_ROUTE124, 0.0f);
    Put(58, 60, 0x33B);
    Put(59, 60, 0x33D);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 2 && complete);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(58, 60)]) != NULL);
    CHECK(DioramaProps_GroundMaterial(&sSnapshot, CellIndex(59, 60), &metatile, &layer));
    /* A formation's top: corner, top, corner. */
    MakeSnapshot(LAYOUT_ROUTE124, 0.0f);
    Put(58, 60, 0x33B);
    Put(59, 60, 0x33C);
    Put(60, 60, 0x33D);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 0);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(58, 60)]) == NULL);
    CHECK(DioramaProps_ForCell(&sSnapshot, &sSnapshot.cells[CellIndex(60, 60)]) == NULL);
    CHECK(!DioramaProps_GroundMaterial(&sSnapshot, CellIndex(58, 60), &metatile, &layer));
    CHECK(!DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    /* The east half alone, its west the water; the west half alone, its east the water. */
    MakeSnapshot(LAYOUT_ROUTE124, 0.0f);
    Put(58, 60, 0x33D);
    Put(62, 60, 0x33B);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 0);
    /* Two rocks side by side: each half has its other half beside it. */
    MakeSnapshot(LAYOUT_ROUTE124, 0.0f);
    Put(58, 60, 0x33B);
    Put(59, 60, 0x33D);
    Put(60, 60, 0x33B);
    Put(61, 60, 0x33D);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 4);
    /* Route 126's pale rocks the same; a half at the grid's edge, its neighbour outside, stands as before. */
    MakeSnapshot(LAYOUT_ROUTE126, 0.0f);
    Put(58, 60, 0x338);
    Put(59, 60, 0x33A);
    Put(sSnapshot.gridOriginX + DIORAMA_GRID_WIDTH - 1, 60, 0x338);
    CHECK(DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete) == 3);
}

static void TestStall(void)
{
    bool complete;
    unsigned count;
    unsigned i;
    bool roof = false;
    bool front = false;
    bool post = false;

    MakeSnapshot(LAYOUT_SLATEPORT_CITY, 0.25f);
    Put(60, 60, STALL_ROOF_MIDDLE);
    Put(60, 61, STALL_FRONT_MIDDLE);
    Put(60, 62, STALL_POST_LEFT);
    count = DioramaProps_Build(&sSnapshot, sResolved, sInstances, 16, &complete);
    CHECK(count == 3 && complete);
    for (i = 0; i < count; i++)
    {
        const struct DioramaPropInstance *instance = &sInstances[i];
        const struct DioramaProp *prop = instance->prop;

        CHECK(prop->kind == DIORAMA_PROP_KIND_CANOPY && prop->ground == DIORAMA_PROP_GROUND_LOWER);
        if (prop->metatileId == STALL_ROOF_MIDDLE)
        {
            /* Two rows deep, its middle a row and a half south: over the
             * front and the posts' row; high over the ground, its back higher. */
            roof = true;
            CHECK(prop->form == DIORAMA_PROP_BLOCK);
            CHECK(Near(instance->depth, 2.0f) && Near(instance->z, -61.5f));
            CHECK(Near(instance->y, 0.25f + prop->lift16 / 16.0f) && instance->y > 2.5f);
            CHECK(instance->rise > 0.0f && Near(instance->rise, prop->rise16 / 16.0f));
        }
        else if (prop->metatileId == STALL_FRONT_MIDDLE)
        {
            /* Standing at the roof's south edge, its top at the roof. */
            front = true;
            CHECK(prop->form == DIORAMA_PROP_SLAB && instance->rise == 0.0f);
            CHECK(instance->z < -62.4f && instance->z > -62.5f);
            CHECK(Near(instance->y + instance->height, 0.25f + 44 / 16.0f));
        }
        else
        {
            /* On the ground, as tall as the roof's front edge. */
            post = true;
            CHECK(prop->metatileId == STALL_POST_LEFT);
            CHECK(Near(instance->y, 0.25f) && Near(instance->height, 44 / 16.0f));
            CHECK(instance->z < -62.0f && instance->z > -62.5f);
        }
    }
    CHECK(roof && front && post);
}

static void TestGround(void)
{
    uint16_t metatile;
    uint8_t layer;

    MakeSnapshot(LAYOUT_LITTLEROOT_TOWN, 0.0f);
    /* No prop, or tall grass (kept as drawn): the terrain as before. */
    CHECK(!DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    Put(60, 60, TALL_GRASS);
    CHECK(!DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    /* Flowers were the upper layer: the lower one is the ground. */
    Put(60, 60, FLOWERS);
    CHECK(DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    CHECK(metatile == FLOWERS && layer == DIORAMA_MATERIAL_BASE);
    /* The metal sign fills its cell: the ground most neighbours have. */
    Put(60, 60, SIGN);
    Put(59, 60, 0x024);
    Put(61, 60, 0x024);
    CHECK(DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    CHECK(metatile == 0x024 && layer == DIORAMA_MATERIAL_FULL);
    /* ... none of them props. */
    Put(59, 60, FLOWERS);
    Put(61, 60, FLOWERS);
    CHECK(DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    CHECK(metatile == GRASS && layer == DIORAMA_MATERIAL_FULL);
    /* Switched off: as before. */
    DioramaProps_SetEnabled(false);
    CHECK(!DioramaProps_GroundMaterial(&sSnapshot, CellIndex(60, 60), &metatile, &layer));
    DioramaProps_SetEnabled(true);
}

int main(void)
{
    TestCatalogue();
    TestFind();
    TestComposeArt();
    TestBuild();
    TestTallGrass();
    TestLampAndFences();
    TestFrontierArch();
    TestArenaGate();
    TestSeaRockHalves();
    TestStall();
    TestGround();
    printf("test_props: %d checks passed\n", sChecks);
    return 0;
}
