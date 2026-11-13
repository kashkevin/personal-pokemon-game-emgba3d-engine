/*
 * The 3DS project's trees (include/diorama/tree_sprites.h): which General
 * tileset metatiles are which quarter of a large tree, so every tree gets its
 * whole crown. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>

#include "diorama/buildings.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"
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

/* The modelled buildings (buildings.c, not linked here): one cell of layout 7 is a building's, at (40, 41). */
bool DioramaBuildings_CellAt(uint16_t layout, int x, int y, uint16_t *ground, uint8_t *quarters, float *top)
{
    (void)ground;
    (void)quarters;
    (void)top;
    return layout == 7 && x == 40 && y == 41;
}

static struct DioramaSceneSnapshot sSnapshot;
static struct DioramaResolvedCell sResolved[DIORAMA_MAX_VISIBLE_CELLS];

static unsigned GridIndex(int x, int y)
{
    return (unsigned)(y * DIORAMA_GRID_WIDTH + x);
}

/* A large tree's quarters at grid (x, y) (quarter 0) with the tops given (quarters 0-3), of the kind first. */
static void PlaceTree(int x, int y, int first, const float tops[4])
{
    int k;

    for (k = 0; k < 4; k++)
    {
        unsigned index = GridIndex(x + (k & 1), y + (k >> 1));

        sSnapshot.cells[index].treeSprite = (uint8_t)(first + k);
        sResolved[index].topHeight = tops[k];
    }
}

static float Lift(int x, int y)
{
    return DioramaTreeSprites_Lift(&sSnapshot, sResolved, GridIndex(x, y));
}

/* KNOWN_ISSUES MS-243: a large tree stands whole at its highest quarter's height. */
static void TestLift(void)
{
    static const float sUpperHigher[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    static const float sColumn[4] = {0.0f, 3.0f, 1.0f, 3.0f};
    static const float sEven[4] = {2.0f, 2.0f, 2.0f, 2.0f};
    static const float sOneLow[4] = {-1.0f, -1.0f, -3.0f, -1.0f};
    int x;
    int y;

    sSnapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (y = 0; y < DIORAMA_GRID_HEIGHT; y++)
        for (x = 0; x < DIORAMA_GRID_WIDTH; x++)
        {
            struct DioramaCellSnapshot *cell = &sSnapshot.cells[GridIndex(x, y)];

            cell->flags = DIORAMA_CELL_SOURCE_VALID;
            cell->sourceLayoutId = 1;
            cell->sourceMapX = (int16_t)x;
            cell->sourceMapY = (int16_t)y;
            sResolved[GridIndex(x, y)].topHeight = 0.5f;
        }
    /* the upper row a level up: the lower row's quarters stand a level over their cells */
    PlaceTree(10, 10, DIORAMA_TREE_SPRITE_QUARTER, sUpperHigher);
    CHECK(Lift(10, 10) == 0.0f && Lift(11, 10) == 0.0f);
    CHECK(Lift(10, 11) == 1.0f && Lift(11, 11) == 1.0f);
    /* the east column three levels up, the west one: the west quarters lift to it */
    PlaceTree(20, 10, DIORAMA_TREE_SPRITE_QUARTER, sColumn);
    CHECK(Lift(20, 10) == 3.0f && Lift(21, 10) == 0.0f && Lift(20, 11) == 2.0f && Lift(21, 11) == 0.0f);
    /* a tree on one level: nothing lifts */
    PlaceTree(30, 10, DIORAMA_TREE_SPRITE_QUARTER, sEven);
    CHECK(Lift(30, 10) == 0.0f && Lift(31, 10) == 0.0f && Lift(30, 11) == 0.0f && Lift(31, 11) == 0.0f);
    /* the other large trees too: the ash-covered, the Lavaridge and the Mossdeep ones */
    PlaceTree(10, 20, DIORAMA_TREE_SPRITE_ASH_QUARTER, sOneLow);
    PlaceTree(20, 20, DIORAMA_TREE_SPRITE_LAVARIDGE_QUARTER, sOneLow);
    PlaceTree(30, 20, DIORAMA_TREE_SPRITE_MOSSDEEP_QUARTER, sOneLow);
    for (x = 10; x <= 30; x += 10)
    {
        CHECK(Lift(x, 20) == 0.0f && Lift(x + 1, 20) == 0.0f && Lift(x + 1, 21) == 0.0f);
        CHECK(Lift(x, 21) == 2.0f);
    }
    /* a neighbour of another kind is not a partner: a General quarter 1 east of an ash quarter 0 */
    PlaceTree(40, 20, DIORAMA_TREE_SPRITE_ASH_QUARTER, sEven);
    sSnapshot.cells[GridIndex(41, 20)].treeSprite = DIORAMA_TREE_SPRITE_QUARTER + 1;
    sResolved[GridIndex(41, 20)].topHeight = 5.0f;
    CHECK(Lift(40, 20) == 0.0f && Lift(40, 21) == 0.0f);
    /* a tree with two quarters (a wood's edge cut by a cliff): the two there */
    sSnapshot.cells[GridIndex(50, 10)].treeSprite = DIORAMA_TREE_SPRITE_QUARTER + 2;
    sResolved[GridIndex(50, 10)].topHeight = 1.0f;
    sSnapshot.cells[GridIndex(51, 10)].treeSprite = DIORAMA_TREE_SPRITE_QUARTER + 3;
    sResolved[GridIndex(51, 10)].topHeight = 2.0f;
    CHECK(Lift(50, 10) == 1.0f && Lift(51, 10) == 0.0f);
    /* a quarter a modelled building owns does not count (its model draws that cell) */
    PlaceTree(40, 40, DIORAMA_TREE_SPRITE_QUARTER, sUpperHigher);
    sSnapshot.cells[GridIndex(40, 41)].sourceLayoutId = 7;
    sResolved[GridIndex(40, 41)].topHeight = 4.0f;
    CHECK(Lift(41, 41) == 1.0f && Lift(40, 40) == 0.0f);
    /* at the grid's edge: the quarters past it are not read */
    sSnapshot.cells[GridIndex(0, 0)].treeSprite = DIORAMA_TREE_SPRITE_QUARTER + 3;
    sResolved[GridIndex(0, 0)].topHeight = 1.0f;
    CHECK(Lift(0, 0) == 0.0f);
    sSnapshot.cells[GridIndex(DIORAMA_GRID_WIDTH - 1, DIORAMA_GRID_HEIGHT - 1)].treeSprite =
        DIORAMA_TREE_SPRITE_QUARTER;
    CHECK(Lift(DIORAMA_GRID_WIDTH - 1, DIORAMA_GRID_HEIGHT - 1) == 0.0f);
    /* not a large tree's quarter: a small tree, no tree; and no snapshot, or a cell past it */
    sSnapshot.cells[GridIndex(60, 60)].treeSprite = DIORAMA_TREE_SPRITE_SMALL;
    CHECK(Lift(60, 60) == 0.0f && Lift(61, 60) == 0.0f);
    CHECK(DioramaTreeSprites_Lift(NULL, sResolved, 0) == 0.0f);
    CHECK(DioramaTreeSprites_Lift(&sSnapshot, sResolved, DIORAMA_MAX_VISIBLE_CELLS) == 0.0f);
    CHECK(DioramaTreeSprites_QuarterFirst(DIORAMA_TREE_SPRITE_QUARTER + 3) == DIORAMA_TREE_SPRITE_QUARTER);
    CHECK(DioramaTreeSprites_QuarterFirst(DIORAMA_TREE_SPRITE_MOSSDEEP_QUARTER + 2)
          == DIORAMA_TREE_SPRITE_MOSSDEEP_QUARTER);
    CHECK(DioramaTreeSprites_QuarterFirst(DIORAMA_TREE_SPRITE_SMALL) == DIORAMA_TREE_SPRITE_NONE);
    CHECK(DioramaTreeSprites_QuarterFirst(DIORAMA_TREE_SPRITE_LAVARIDGE_SMALL) == DIORAMA_TREE_SPRITE_NONE);
}

int main(void)
{
    /* A forest's 2x2 tree: tops, bottoms (behind other trees, beside open ground). */
    static const uint16_t sTops[][2] = {{0x1D4, 0x1D5}, {0x1D6, 0x1D7}, {0x1FC, 0x1FD} /* at a bank */};
    static const uint16_t sBottoms[][2] = {
        {0x1DC, 0x1DD}, {0x1DE, 0x1DF}, {0x1E4, 0x1E5}, {0x1E6, 0x1E7}, {0x1EC, 0x1ED},
        {0x1F3, 0x1F2}, /* Petalburg Woods, Rustboro, Route 116, the Safari Zone: 1F3 under 1D4, 1F2 under 1D5 */
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
    /* A bank tree's crown tip over the sea and over a pond: the water under it (its bottom layer's own art). */
    CHECK(DioramaTreeSprites_Part(false, 0x1F6) == DIORAMA_TREE_PART_NONE);
    CHECK(DioramaTreeSprites_GroundMetatile(false, 0x1F6) == 0x170);
    CHECK(DioramaTreeSprites_GroundMetatile(false, 0x1F7) == 0x170);
    CHECK(DioramaTreeSprites_GroundMetatile(false, 0x1FE) == 0x0A1);
    CHECK(DioramaTreeSprites_GroundMetatile(false, 0x1FF) == 0x0A1);
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
    /* The Lavaridge tileset's trees (step 16): Jagged Pass's large tree, its lower row under the next crown too;
     * the small tree's trunk on Route 112's terraces and inside its woods; each cell's ground, and a crown's top's,
     * the pink ground, the cliff's red rock or the forest floor. */
    {
        static const uint16_t sLarge[][4] = {{0x2D8, 0x2D9, 0x2E2, 0x2E3}, {0x2D8, 0x2D9, 0x2D6, 0x2D7}};
        static const uint16_t sSmall[] = {0x218, 0x219, 0x201, 0x203, 0x21A, 0x3B8};
        static const uint16_t sTops[][2] = {
            {0x210, 0x271}, {0x3B0, 0x271}, {0x2DE, 0x271}, {0x2DF, 0x271}, {0x211, 0x274}, {0x212, 0x274},
            {0x20B, 0x27A}, {0x3B1, 0x27A}, {0x2FB, 0x2EE},
        };
        unsigned j;

        for (i = 0; i < sizeof(sLarge) / sizeof(sLarge[0]); i++)
            for (j = 0; j < 4; j++)
            {
                CHECK(DioramaTreeSprites_LavaridgePart(sLarge[i][j]) == (int)j);
                CHECK(DioramaTreeSprites_LavaridgeGround(sLarge[i][j]) == 0x271);
            }
        for (i = 0; i < sizeof(sSmall) / sizeof(sSmall[0]); i++)
        {
            CHECK(DioramaTreeSprites_LavaridgePart(sSmall[i]) == DIORAMA_TREE_PART_SMALL);
            CHECK(DioramaTreeSprites_LavaridgeGround(sSmall[i]) != sSmall[i]);
        }
        for (i = 0; i < sizeof(sTops) / sizeof(sTops[0]); i++)
        {
            CHECK(DioramaTreeSprites_LavaridgePart(sTops[i][0]) == DIORAMA_TREE_PART_NONE);
            CHECK(DioramaTreeSprites_LavaridgeGround(sTops[i][0]) == sTops[i][1]);
        }
        /* the pink ground, the cliff, Mt Chimney's rock: themselves */
        CHECK(DioramaTreeSprites_LavaridgePart(0x271) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_LavaridgeGround(0x271) == 0x271);
        CHECK(DioramaTreeSprites_LavaridgeGround(0x274) == 0x274);
        CHECK(DioramaTreeSprites_LavaridgeGround(0x2CD) == 0x2CD);
    }
    /* The Mossdeep tileset's round trees (step 16): on grass, over rock, over a ledge, on the shallows; where two
     * overlap, the lower tree's upper quarter; each cell's own lower layer its ground. */
    {
        static const uint16_t sQuarters[][4] = {
            {0x31A, 0x31B, 0x322, 0x323}, {0x2F0, 0x2F1, 0x2F8, 0x2F9}, {0x31C, 0x31D, 0x322, 0x323},
            {0x31E, 0x31F, 0x322, 0x323}, {0x325, 0x324, 0x2F8, 0x2F9}, {0x356, 0x355, 0x322, 0x323},
        };
        unsigned j;

        for (i = 0; i < sizeof(sQuarters) / sizeof(sQuarters[0]); i++)
            for (j = 0; j < 4; j++)
                CHECK(DioramaTreeSprites_MossdeepPart(sQuarters[i][j]) == (int)j);
        CHECK(DioramaTreeSprites_MossdeepGround(0x31A, 0x322) == 0x001);
        CHECK(DioramaTreeSprites_MossdeepGround(0x356, 0x323) == 0x001);
        CHECK(DioramaTreeSprites_MossdeepGround(0x31C, 0x322) == 0x07C);
        CHECK(DioramaTreeSprites_MossdeepGround(0x31F, 0x323) == 0x002);
        CHECK(DioramaTreeSprites_MossdeepGround(0x2F9, 0x19E) == 0x19E);
        /* an overlap cell: the shallows in a grove, grass over the lower tree's trunk in the town */
        CHECK(DioramaTreeSprites_MossdeepGround(0x324, 0x325) == 0x19E);
        CHECK(DioramaTreeSprites_MossdeepGround(0x325, 0x2F8) == 0x19E);
        CHECK(DioramaTreeSprites_MossdeepGround(0x325, 0x322) == 0x001);
        CHECK(DioramaTreeSprites_MossdeepGround(0x324, 0) == 0x19E);
        /* the large three-wide tree, the shallows, grass: not these trees */
        CHECK(DioramaTreeSprites_MossdeepPart(0x30B) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_MossdeepPart(0x19E) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_MossdeepPart(0x001) == DIORAMA_TREE_PART_NONE);
    }
    /* The Fortree tileset's copies of the General small tree (MS-172): the trunks inside a wood on the General
     * trunks' ground, at a wood's edge on the town's; the shadowed ones under the decks, the bridges and the palms'
     * fronds stay as drawn; the crown tops over other ground give that ground back. */
    {
        static const uint16_t sWood[] = {0x22A};
        static const uint16_t sShadowed[] = {0x243, 0x244, 0x245, 0x246, 0x247};
        static const uint16_t sCrowns[][2] = {
            {0x24B, 0x261}, {0x24C, 0x261}, {0x25C, 0x241}, {0x207, 0x206}, {0x222, 0x240}, {0x2DC, 0x240},
            {0x223, 0x242}, {0x2DE, 0x242}, {0x2DD, 0x079}, {0x2E6, 0x113}, {0x227, 0x23A}, {0x276, 0x1D9},
            {0x316, 0x001}, {0x317, 0x001},
        };

        for (i = 0; i < sizeof(sWood) / sizeof(sWood[0]); i++)
        {
            CHECK(DioramaTreeSprites_FortreePart(sWood[i]) == DIORAMA_TREE_PART_SMALL);
            CHECK(DioramaTreeSprites_FortreeGround(sWood[i]) == DioramaTreeSprites_TrunkGround(false));
            CHECK(DioramaTreeSprites_FortreeCrownGround(sWood[i]) == sWood[i]);
        }
        for (i = 0; i < sizeof(sShadowed) / sizeof(sShadowed[0]); i++)
        {
            CHECK(DioramaTreeSprites_FortreePart(sShadowed[i]) == DIORAMA_TREE_PART_NONE);
            CHECK(DioramaTreeSprites_FortreeCrownGround(sShadowed[i]) == sShadowed[i]);
        }
        CHECK(DioramaTreeSprites_FortreePart(0x253) == DIORAMA_TREE_PART_SMALL);
        CHECK(DioramaTreeSprites_FortreePart(0x254) == DIORAMA_TREE_PART_SMALL);
        CHECK(DioramaTreeSprites_FortreeGround(0x253) == 0x261);
        CHECK(DioramaTreeSprites_FortreeGround(0x254) == 0x261);
        for (i = 0; i < sizeof(sCrowns) / sizeof(sCrowns[0]); i++)
        {
            CHECK(DioramaTreeSprites_FortreePart(sCrowns[i][0]) == DIORAMA_TREE_PART_NONE);
            CHECK(DioramaTreeSprites_FortreeCrownGround(sCrowns[i][0]) == sCrowns[i][1]);
        }
        /* the palm's lower trunk (no metatile draws it without the crown top), its fronds, the bridge over the wood,
         * the town's ground, a tree house: none of these */
        CHECK(DioramaTreeSprites_FortreeCrownGround(0x273) == 0x273);
        CHECK(DioramaTreeSprites_FortreePart(0x250) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_FortreePart(0x256) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_FortreePart(0x261) == DIORAMA_TREE_PART_NONE);
        CHECK(DioramaTreeSprites_FortreeCrownGround(0x261) == 0x261);
        CHECK(DioramaTreeSprites_FortreePart(0x224) == DIORAMA_TREE_PART_NONE);
    }
    TestLift();
    puts("tree sprite tests passed");
    return EXIT_SUCCESS;
}
