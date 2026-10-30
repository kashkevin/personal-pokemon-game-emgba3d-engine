/*
 * The 3DS project's trees (include/diorama/tree_sprites.h): which General
 * tileset metatiles are which quarter of a large tree, so every tree gets its
 * whole crown. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>

#include "diorama/tree_sprites.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_tree_sprites.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    /* A forest's 2x2 tree: tops, bottoms (behind other trees, beside open ground). */
    static const uint16_t sTops[][2] = {{0x1D4, 0x1D5}, {0x1D6, 0x1D7}};
    static const uint16_t sBottoms[][2] = {
        {0x1DC, 0x1DD}, {0x1DE, 0x1DF}, {0x1E4, 0x1E5}, {0x1E6, 0x1E7}, {0x1EC, 0x1ED},
        {0x1F2, 0x1F3}, /* Petalburg Woods, Rustboro, Route 116, the Safari Zone */
    };
    unsigned i;

    for (i = 0; i < sizeof(sTops) / sizeof(sTops[0]); i++)
    {
        CHECK(DioramaTreeSprites_Part(false, sTops[i][0]) == 0);
        CHECK(DioramaTreeSprites_Part(false, sTops[i][1]) == 1);
    }
    for (i = 0; i < sizeof(sBottoms) / sizeof(sBottoms[0]); i++)
    {
        CHECK(DioramaTreeSprites_Part(false, sBottoms[i][0]) == 2);
        CHECK(DioramaTreeSprites_Part(false, sBottoms[i][1]) == 3);
        /* A tree's own cells are ground under its trunk. */
        CHECK(DioramaTreeSprites_GroundMetatile(false, sBottoms[i][0]) == sBottoms[i][0]);
    }
    /* Small trees, Dewford's only on Dewford's tileset. */
    CHECK(DioramaTreeSprites_Part(false, 0x016) == DIORAMA_TREE_PART_SMALL);
    CHECK(DioramaTreeSprites_Part(false, 0x1F4) == DIORAMA_TREE_PART_SMALL);
    CHECK(DioramaTreeSprites_Part(true, 0x23A) == DIORAMA_TREE_PART_SMALL);
    CHECK(DioramaTreeSprites_Part(false, 0x23A) == DIORAMA_TREE_PART_NONE);
    /* Not trees: grass, paths, the canopy's tip over grass (ground again). */
    CHECK(DioramaTreeSprites_Part(false, 0x001) == DIORAMA_TREE_PART_NONE);
    CHECK(DioramaTreeSprites_Part(false, 0x1CE) == DIORAMA_TREE_PART_NONE);
    CHECK(DioramaTreeSprites_GroundMetatile(false, 0x1CE) == 0x001);
    CHECK(DioramaTreeSprites_TrunkGround(false) == 0x001);
    CHECK(DioramaTreeSprites_TrunkGround(true) == 0x124);
    /* The Fallarbor tileset's ash-covered trees (step 16): Route 113's wood, the same tree beside open ash, at a
     * wood's foot, on Route 114's volcanic sand; each cell's ground the ash or the sand. */
    {
        static const uint16_t sAsh[][4] = {
            {0x26E, 0x26D, 0x276, 0x275}, {0x205, 0x204, 0x20D, 0x20C},
            {0x26C, 0x26B, 0x274, 0x273}, {0x207, 0x206, 0x20F, 0x20E},
        };
        unsigned j;

        for (i = 0; i < sizeof(sAsh) / sizeof(sAsh[0]); i++)
            for (j = 0; j < 4; j++)
            {
                CHECK(DioramaTreeSprites_AshPart(sAsh[i][j]) == (int)j);
                CHECK(DioramaTreeSprites_AshGround(sAsh[i][j]) == (i < 2 ? 0x218 : 0x279));
            }
        /* the ash itself, the ash grass, a rock: not trees */
        CHECK(DioramaTreeSprites_AshPart(0x218) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_AshPart(0x20A) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_AshPart(0x269) == DIORAMA_TREE_PART_NONE);
    }
    puts("tree sprite tests passed");
    return EXIT_SUCCESS;
}
