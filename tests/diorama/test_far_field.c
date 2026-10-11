// Distant scenery (diorama/far_field.h): the far blocks' columns and mesh,
// and the rules' static heights it reads.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/far_field.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"
#include "diorama/tree_sprites.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static int sChecks;

static void Check(bool condition, const char *expression, int line)
{
    sChecks++;
    if (!condition)
    {
        fprintf(stderr, "test_far_field.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define GROUND_METATILE 0x001
#define TREE_METATILE 0x1D4
#define GREEN 0x0300  /* BGR555: green 24 */

static struct DioramaSceneSnapshot sSnapshot;
static struct DioramaTerrainVertex sVertices[DIORAMA_FAR_MAX_VERTICES];

static struct DioramaFarBlock *Block(int i, int j)
{
    return &sSnapshot.farBlocks[j * DIORAMA_FAR_BLOCKS + i];
}

static void MakeGround(void)
{
    int i;

    memset(&sSnapshot, 0, sizeof(sSnapshot));
    sSnapshot.farOn = 1;
    sSnapshot.farGeneration = 1;
    /* the camera at map cell (60, 60): the grid 36..84, the far blocks from 12 */
    sSnapshot.gridOriginX = 60 - DIORAMA_GRID_WIDTH / 2;
    sSnapshot.gridOriginY = 60 - DIORAMA_GRID_HEIGHT / 2;
    sSnapshot.farOriginX = 12;
    sSnapshot.farOriginY = 12;
    for (i = 0; i < DIORAMA_FAR_BLOCKS * DIORAMA_FAR_BLOCKS; i++)
    {
        sSnapshot.farBlocks[i].metatileId = GROUND_METATILE;
        sSnapshot.farBlocks[i].color = GREEN;
        sSnapshot.farBlocks[i].flags = DIORAMA_FAR_VALID;
    }
}

static bool InsideGrid(float x, float z)
{
    /* cell (x, y) spans x - 0.5 .. x + 0.5 and -(y + 0.5) .. -(y - 0.5) */
    float left = sSnapshot.gridOriginX - 0.5f;
    float right = sSnapshot.gridOriginX + DIORAMA_GRID_WIDTH - 0.5f;
    float north = -(sSnapshot.gridOriginY - 0.5f);
    float south = -(sSnapshot.gridOriginY + DIORAMA_GRID_HEIGHT - 0.5f);

    return x > left + 0.01f && x < right - 0.01f && z < north - 0.01f && z > south + 0.01f;
}

static void TestColumns(void)
{
    struct DioramaFarBlock block;
    struct DioramaFarColumn column;
    int x;
    float lowest = 100.0f;
    float highest = -100.0f;

    memset(&block, 0, sizeof(block));
    DioramaFar_Column(&block, 0, 0, &column);
    CHECK(!column.present);

    block.flags = DIORAMA_FAR_VALID;
    block.metatileId = GROUND_METATILE;
    block.color = 0x7FFF;
    DioramaFar_Column(&block, 3, 4, &column);
    CHECK(column.present);
    CHECK(column.top == 0.0f);
    CHECK(column.rgba == 0xFFFFFFFFu);
    block.color = 31; /* red: a fifth of the way to grey */
    DioramaFar_Column(&block, 3, 4, &column);
    CHECK((column.rgba & 0xFF) > 200 && ((column.rgba >> 8) & 0xFF) < 30
          && ((column.rgba >> 16) & 0xFF) < 30 && (column.rgba >> 24) == 0xFF);
    CHECK(!column.tree);

    /* a tree, by its metatile (no map cell): a canopy, a little uneven */
    CHECK(DioramaTreeSprites_Part(false, TREE_METATILE) != DIORAMA_TREE_PART_NONE);
    block.metatileId = TREE_METATILE;
    block.color = GREEN;
    for (x = 0; x < 64; x++)
    {
        DioramaFar_Column(&block, x, 7, &column);
        CHECK(column.tree);
        CHECK(column.top >= 1.7f && column.top <= 2.3f);
        if (column.top < lowest) lowest = column.top;
        if (column.top > highest) highest = column.top;
    }
    CHECK(highest - lowest > 0.2f);
    /* the same block, the same column */
    {
        struct DioramaFarColumn again;

        DioramaFar_Column(&block, 5, 7, &column);
        DioramaFar_Column(&block, 5, 7, &again);
        CHECK(column.top == again.top && column.rgba == again.rgba);
    }
}

static void TestMesh(void)
{
    bool complete;
    uint32_t count;
    uint32_t i;
    uint32_t tops = 0;
    uint32_t treeSides = 0;
    uint32_t rim = 0;

    MakeGround();
    count = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete);
    CHECK(complete);
    CHECK(count > 0 && count % 6 == 0);
    for (i = 0; i < count; i++)
    {
        const struct DioramaTerrainVertex *v = &sVertices[i];

        CHECK(v->textureLayer == DIORAMA_TERRAIN_TEXTURE_VERTEX_COLOR);
        CHECK(!InsideGrid(v->x, v->z));
        if (v->y == 0.0f && v->shade == 1.0f)
            tops++;
        if (v->y < -0.5f)
            rim++;
    }
    CHECK(tops > 0);
    CHECK(rim > 0); /* the outer edge goes down under the haze */
    {
        static struct DioramaFarSector sectors[DIORAMA_FAR_SECTORS];
        uint32_t total = 0;
        uint32_t again = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES,
                                              sectors, &complete);
        int sector;

        CHECK(again == count);
        for (sector = 0; sector < DIORAMA_FAR_SECTORS; sector++)
        {
            CHECK(sectors[sector].first == total);
            total += sectors[sector].count;
            for (i = sectors[sector].first; i < sectors[sector].first + sectors[sector].count; i++)
                CHECK(sVertices[i].x >= sectors[sector].bounds.minX && sVertices[i].x <= sectors[sector].bounds.maxX
                      && sVertices[i].y >= sectors[sector].bounds.minY && sVertices[i].y <= sectors[sector].bounds.maxY
                      && sVertices[i].z >= sectors[sector].bounds.minZ && sVertices[i].z <= sectors[sector].bounds.maxZ);
        }
        CHECK(total == count);
        /* the middle sector is the scene's own: nothing there */
        CHECK(sectors[DIORAMA_FAR_SECTORS / 2].count == 0);
    }

    /* a tree block in the ring: sides down to the ground round it */
    Block(2, 2)->metatileId = TREE_METATILE;
    count = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete);
    CHECK(complete);
    for (i = 0; i < count; i++)
    {
        const struct DioramaTerrainVertex *v = &sVertices[i];
        /* block (2, 2) covers cells 16..17: x 15.5..17.5, z -17.5..-15.5 */
        bool onBlock = v->x >= 15.5f && v->x <= 17.5f && v->z >= -17.5f && v->z <= -15.5f;

        if (onBlock && v->y == 0.0f && v->shade != 1.0f)
            treeSides++;
    }
    CHECK(treeSides == 4 * 2 * 3); /* 4 sides of 2 cells, 3 vertices at the ground each */

    /* a tree block against the scene's own cells: its side there goes down
     * to the ground (else the sky shows through), the rest of the ring's
     * cells stay outside the scene */
    MakeGround();
    {
        /* the grid's left edge is x 36 (cells 36..84): block i = 11 covers 34..35 */
        int found = 0;

        Block(11, 20)->metatileId = TREE_METATILE;
        count = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete);
        CHECK(complete);
        for (i = 0; i < count; i++)
            if (sVertices[i].x == 35.5f && sVertices[i].y == 0.0f && sVertices[i].shade == 0.82f)
                found++; /* an east-facing side's foot */
        CHECK(found == 2 * 3); /* both cells' sides towards the scene */
    }

    /* a missing block leaves a hole, its neighbours' sides go down */
    MakeGround();
    Block(5, 5)->flags = 0;
    count = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete);
    rim = 0;
    for (i = 0; i < count; i++)
        if (sVertices[i].y < -0.5f && sVertices[i].x > 21.0f && sVertices[i].x < 24.0f
         && sVertices[i].z < -21.0f && sVertices[i].z > -24.0f)
            rim++;
    CHECK(rim > 0);

    /* too little room: as much as fits, and says so */
    count = DioramaFar_BuildMesh(&sSnapshot, sVertices, 60, NULL, &complete);
    CHECK(!complete);
    CHECK(count <= 60);

    /* no far blocks: nothing */
    sSnapshot.farOn = 0;
    CHECK(DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete) == 0);
    CHECK(complete);

    /* the grid one cell off the blocks' even origin: the cells it holds are
     * still left out, the others drawn */
    MakeGround();
    sSnapshot.gridOriginX++;
    count = DioramaFar_BuildMesh(&sSnapshot, sVertices, DIORAMA_FAR_MAX_VERTICES, NULL, &complete);
    CHECK(complete);
    for (i = 0; i < count; i++)
        CHECK(!InsideGrid(sVertices[i].x, sVertices[i].z));
}

static void TestStaticHeights(void)
{
    size_t layoutIndex;
    bool found = false;
    float ground;
    float top;
    uint8_t archetype;

    CHECK(!DioramaRules_StaticHeights(0, 0, 0, 0, 0, 0, &ground, &top, &archetype));
    for (layoutIndex = 0; layoutIndex < gDioramaLayoutV2Count && !found; layoutIndex++)
    {
        const struct DioramaGeneratedLayoutV2 *layout = &gDioramaLayoutsV2[layoutIndex];
        const struct DioramaGeneratedTerrainV2 *record;

        if (layout->terrainRecordCount == 0)
            continue;
        record = &gDioramaTerrainV2[layout->terrainRecordOffset];
        CHECK(DioramaRules_StaticHeights(layout->id, record->mapGroup, record->mapNumber,
                                         record->cellOffset % layout->width,
                                         record->cellOffset / layout->width,
                                         record->expectedMetatile, &ground, &top, &archetype));
        CHECK(archetype == record->archetype);
        CHECK(top >= ground || record->shape == DIORAMA_SHAPE_LEDGE);
        /* another metatile there (the map changed): no record */
        CHECK(!DioramaRules_StaticHeights(layout->id, record->mapGroup, record->mapNumber,
                                          record->cellOffset % layout->width,
                                          record->cellOffset / layout->width,
                                          (uint16_t)(record->expectedMetatile + 1),
                                          &ground, &top, &archetype));
        /* outside the layout */
        CHECK(!DioramaRules_StaticHeights(layout->id, record->mapGroup, record->mapNumber,
                                          (int16_t)layout->width, 0, record->expectedMetatile,
                                          &ground, &top, &archetype));
        found = true;
    }
    CHECK(found);
}

int main(void)
{
    TestColumns();
    TestMesh();
    TestStaticHeights();
    printf("far field tests passed (%d checks)\n", sChecks);
    return EXIT_SUCCESS;
}
