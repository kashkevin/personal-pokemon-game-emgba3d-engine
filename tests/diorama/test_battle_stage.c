/* The 3D battle's stage search (src/diorama/scene/battle_stage.c). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "diorama/battle_scene.h"
#include "diorama/battle_stage.h"

#define DEG (3.14159265358979323846f / 180.0f)

/* A synthetic map: 64x64 tiles of plain open ground unless painted. */
struct World
{
    struct DioramaStageCell cells[64][64];
    bool known[64][64];
};

static struct World sWorld;

static bool CellAt(void *context, int x, int y, struct DioramaStageCell *cell)
{
    struct World *w = context;

    if (x < 0 || y < 0 || x >= 64 || y >= 64 || !w->known[y][x])
        return false;
    *cell = w->cells[y][x];
    return true;
}

static void Fill(uint8_t kind)
{
    memset(&sWorld, 0, sizeof(sWorld));
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            sWorld.cells[y][x].kind = kind;
            sWorld.known[y][x] = true;
        }
}

static void Request(struct DioramaStageRequest *r, int px, int py)
{
    memset(r, 0, sizeof(*r));
    r->view.pitch = 30.0f * DEG;
    r->view.distance = 8.0829f;
    r->view.focal = 271.8f;
    r->view.targetRow = 60.57f;
    r->visibleLeft = -40.0f;
    r->visibleRight = 280.0f;
    r->visibleTop = -20.0f;
    r->visibleBottom = 112.0f;
    r->playerX = px;
    r->playerY = py;
    r->facing = 2; /* north */
    r->wild = true;
    r->cellAt = CellAt;
    r->context = &sWorld;
}

static void TestProjection(void)
{
    struct DioramaBattleView v = {30.0f * DEG, 8.0829f, 271.8f, 60.57f};
    static const float points[][2] = {{72, 110}, {176, 70}, {120, 60.57f}, {10, 100}, {230, 20}};
    float x, y, gx, gy;

    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
    {
        assert(DioramaStage_ScreenToGround(&v, points[i][0], points[i][1], &x, &y));
        assert(DioramaStage_Project(&v, x, 0.0f, y, &gx, &gy));
        assert(fabsf(gx - points[i][0]) < 0.01f && fabsf(gy - points[i][1]) < 0.01f);
    }
    /* The target is the middle of the view. */
    assert(DioramaStage_ScreenToGround(&v, 120.0f, 60.57f, &x, &y));
    assert(fabsf(x) < 1e-4f && fabsf(y) < 1e-4f);
    /* Above the horizon there is no ground. */
    assert(!DioramaStage_ScreenToGround(&v, 120.0f, -400.0f, &x, &y));
    /* Behind the eye nothing projects. */
    assert(!DioramaStage_Project(&v, 0.0f, 0.0f, 20.0f, &gx, &gy));
}

static void TestOpenField(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;

    Fill(DIORAMA_GROUND_GRASS);
    Request(&r, 32, 32);
    DioramaStage_Choose(&r, &result);
    /* The player's Pokemon stands south-west of the target, the other one
     * north-east of it and further away. */
    assert(result.feetX[0] < 0.0f && result.feetY[0] > 0.0f);
    assert(result.feetX[1] > 0.0f && result.feetY[1] < result.feetY[0]);
    /* Everything alike: the spot with the player's Pokemon on the player's
     * own tile wins (every other one is further). */
    assert(result.offsetX == 0.0f && result.offsetY == 0.0f);
    assert(fabsf(result.targetX + result.feetX[0] - 32.5f) < 1e-4f);
    assert(fabsf(result.targetY + result.feetY[0] - 32.5f) < 1e-4f);
    assert(result.want[0] == DIORAMA_GROUND_GRASS && result.want[1] == DIORAMA_GROUND_GRASS);
}

static void TestWallInFront(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult open, walled;
    int fx, fy;

    Fill(DIORAMA_GROUND_PLAIN);
    Request(&r, 32, 32);
    DioramaStage_Choose(&r, &open);
    /* A row of tall trees right in front of (south of) the opponent's feet at
     * the open field's spot: it would stand over the opponent's picture. */
    fx = (int)floorf(open.targetX + open.feetX[1]);
    fy = (int)floorf(open.targetY + open.feetY[1]) + 1;
    for (int x = fx - 3; x <= fx + 3; ++x)
    {
        sWorld.cells[fy][x].top = 2.4f;
        sWorld.cells[fy][x].blocked = true;
    }
    DioramaStage_Choose(&r, &walled);
    assert(walled.offsetX != 0.0f || walled.offsetY != 0.0f);
    printf("  wall in front: stage moved %+.0f,%+.0f\n", walled.offsetX, walled.offsetY);
}

static void TestSurfing(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;

    /* Land in the west half, water in the east; the player surfs at its edge. */
    Fill(DIORAMA_GROUND_PLAIN);
    for (int y = 0; y < 64; ++y)
        for (int x = 34; x < 64; ++x)
            sWorld.cells[y][x].kind = DIORAMA_GROUND_WATER;
    Request(&r, 34, 32);
    r.surfing = true;
    DioramaStage_Choose(&r, &result);
    assert(result.want[0] == DIORAMA_GROUND_WATER && result.want[1] == DIORAMA_GROUND_WATER);
    /* Both sides end up on the water. */
    assert(result.targetX + result.feetX[0] >= 34.0f);
    assert(result.targetX + result.feetX[1] >= 34.0f);
}

/* Sea everywhere one tile below the land (lift -1), dotted with banks of
 * blocked cells at land height: the battle stands at the height its two sides
 * stand on - the water's - never at a bank's. (Route 107 had the target on an
 * island's edge between the two battlers, which stood on the water: at the
 * target's height they floated a tile over it. The search avoids such targets
 * here; the survey's staging check is what caught the real one.) */
static void TestTargetOnABank(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;
    struct DioramaStageView seen;

    Fill(DIORAMA_GROUND_WATER);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            sWorld.cells[y][x].lift = -1.0f;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if ((x % 5 == 0) && (y % 3 == 0))
            {
                sWorld.cells[y][x].kind = DIORAMA_GROUND_PLAIN;
                sWorld.cells[y][x].blocked = true;
                sWorld.cells[y][x].lift = 0.0f;
            }
    Request(&r, 32, 32);
    r.surfing = true;
    DioramaStage_Choose(&r, &result);
    assert(result.ground == -1.0f);
    /* The measure of a stage chosen on the bank itself stands it at the water too. */
    for (float ty = 30.0f; ty < 34.0f; ty += 1.0f)
        for (float tx = 30.0f; tx < 36.0f; tx += 1.0f)
        {
            int cx = (int)floorf(tx), cy = (int)floorf(ty);

            if (sWorld.cells[cy][cx].lift != 0.0f)
                continue;
            DioramaStage_Measure(&r, tx + 0.5f, ty + 0.5f, &seen);
        }
    printf("  target on a bank: the battle stands at %.1f\n", result.ground);
}

static void TestIndoorVoid(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;

    /* A 12x10 room; outside it nothing is known (black, indoors). */
    memset(&sWorld, 0, sizeof(sWorld));
    for (int y = 20; y < 30; ++y)
        for (int x = 20; x < 32; ++x)
        {
            sWorld.known[y][x] = true;
            sWorld.cells[y][x].kind = DIORAMA_GROUND_PLAIN;
        }
    Request(&r, 21, 28); /* near the room's south-west corner */
    r.indoor = true;
    DioramaStage_Choose(&r, &result);
    /* Both sides' feet inside the room. */
    for (int side = 0; side < 2; ++side)
    {
        float x = result.targetX + result.feetX[side], y = result.targetY + result.feetY[side];

        assert(x >= 20.0f && x < 32.0f && y >= 20.0f && y < 30.0f);
    }
    printf("  indoor corner: stage moved %+.0f,%+.0f\n", result.offsetX, result.offsetY);
}


/* A row of tall columns (trees, a wall) from (x0, y) to (x1, y). */
static void TallRow(int x0, int x1, int y, float top)
{
    for (int x = x0; x <= x1; ++x)
    {
        sWorld.cells[y][x].top = top;
        sWorld.cells[y][x].blocked = true;
    }
}

static void TestTreesInFrontOfTheOpponent(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult open, treed;
    struct DioramaStageView seen;
    int fx, fy;

    Fill(DIORAMA_GROUND_GRASS);
    Request(&r, 32, 32);
    DioramaStage_Choose(&r, &open);
    assert(open.seen.overlap == 0.0f && open.seen.voidFraction == 0.0f);
    /* Trees two tiles in front of the opponent's feet at the open spot: at
     * that spot they stand over its picture. */
    fx = (int)floorf(open.targetX + open.feetX[1]);
    fy = (int)floorf(open.targetY + open.feetY[1]) + 2;
    TallRow(fx - 2, fx + 2, fy, 2.1f);
    DioramaStage_Measure(&r, open.targetX, open.targetY, &seen);
    assert(seen.overlap > 0.05f);
    DioramaStage_Choose(&r, &treed);
    /* Somewhere else, with nothing over either battler. */
    assert(treed.offsetX != 0.0f || treed.offsetY != 0.0f);
    assert(treed.seen.overlap == 0.0f);
    DioramaStage_Measure(&r, treed.targetX, treed.targetY, &seen);
    assert(fabsf(seen.overlap - treed.seen.overlap) < 1e-5f
        && fabsf(seen.voidFraction - treed.seen.voidFraction) < 1e-5f
        && fabsf(seen.cramped - treed.seen.cramped) < 1e-5f);
    printf("  trees in front of the opponent: stage moved %+.0f,%+.0f\n", treed.offsetX, treed.offsetY);
}

/* A crown leaning back (the 3DS trees' cut-outs) reaches past its cell's
 * north edge: what it covers is measured that far north. */
static void TestLeaningCrown(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult open;
    struct DioramaStageView upright, leaning;
    int fx, fy;

    Fill(DIORAMA_GROUND_GRASS);
    Request(&r, 32, 32);
    DioramaStage_Choose(&r, &open);
    fx = (int)floorf(open.targetX + open.feetX[1]);
    fy = (int)floorf(open.targetY + open.feetY[1]) + 3;
    TallRow(fx - 2, fx + 2, fy, 1.4f);
    DioramaStage_Measure(&r, open.targetX, open.targetY, &upright);
    for (int x = fx - 2; x <= fx + 2; ++x)
        sWorld.cells[fy][x].north = 0.46f;
    DioramaStage_Measure(&r, open.targetX, open.targetY, &leaning);
    assert(leaning.overlap > upright.overlap + 0.01f);
    printf("  leaning crown: the opponent covered %.0f%% (%.0f%% upright)\n", 100.0f * leaning.overlap,
           100.0f * upright.overlap);
}

/* The picture box of a column `top` high on tile (x, y), seen from target
 * (tx, ty): all eight corners. */
static bool ColumnBox(const struct DioramaBattleView *v, int x, int y, float top, float tx, float ty, float box[4])
{
    box[0] = box[1] = 1e9f;
    box[2] = box[3] = -1e9f;
    for (int corner = 0; corner < 8; ++corner)
    {
        float gx, gy;

        if (!DioramaStage_Project(v, x + (corner & 1) - tx, (corner & 4) ? top : 0.0f,
                                  y + ((corner >> 1) & 1) - ty, &gx, &gy))
            return false;
        box[0] = fminf(box[0], gx);
        box[1] = fminf(box[1], gy);
        box[2] = fmaxf(box[2], gx);
        box[3] = fmaxf(box[3], gy);
    }
    return true;
}

static void TestDoubles(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult singles, doubles;
    struct DioramaStageView seen;
    float box[4], fx, fy;
    int x = 0, y = 0;
    bool found = false;

    Fill(DIORAMA_GROUND_PLAIN);
    Request(&r, 32, 32);
    DioramaStage_Choose(&r, &singles);
    /* One tree in front of the doubles' left opponent (picture x 168..232,
     * feet at 200,70) and clear of the singles opponent (x 144..208). */
    assert(DioramaStage_ScreenToGround(&r.view, 200.0f, 70.0f, &fx, &fy));
    for (y = 20; y < 44 && !found; ++y)
        for (x = 20; x < 44 && !found; ++x)
            if (y + 1.0f - singles.targetY > fy + 0.1f
             && ColumnBox(&r.view, x, y, 2.1f, singles.targetX, singles.targetY, box)
             && box[0] > 210.0f && box[0] < 228.0f && box[1] < 70.0f && box[3] > 20.0f)
                found = true;
    assert(found);
    --x;
    --y;
    sWorld.cells[y][x].top = 2.1f;
    sWorld.cells[y][x].blocked = true;
    DioramaStage_Measure(&r, singles.targetX, singles.targetY, &seen);
    assert(seen.overlap == 0.0f);
    r.doubles = true;
    DioramaStage_Measure(&r, singles.targetX, singles.targetY, &seen);
    assert(seen.overlap > 0.0f);
    DioramaStage_Choose(&r, &doubles);
    assert(doubles.seen.overlap == 0.0f);
    printf("  doubles: a tree over the left opponent at the singles stage (%.0f%%), stage moved %+.0f,%+.0f\n",
           100.0f * seen.overlap, doubles.offsetX, doubles.offsetY);
}

/* A room of plain floor from (x0, y0) to (x1, y1) inside walls `wall` tiles
 * high; outside them nothing is known (black). */
static void Room(int x0, int y0, int x1, int y1, float wall)
{
    memset(&sWorld, 0, sizeof(sWorld));
    for (int y = y0 - 1; y <= y1 + 1; ++y)
        for (int x = x0 - 1; x <= x1 + 1; ++x)
        {
            sWorld.known[y][x] = true;
            sWorld.cells[y][x].kind = DIORAMA_GROUND_PLAIN;
            if (x < x0 || x > x1 || y < y0 || y > y1)
            {
                sWorld.cells[y][x].top = wall;
                sWorld.cells[y][x].blocked = true;
            }
        }
}

static void TestRoomWallsHideTheBlack(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;
    struct DioramaStageView seen;

    /* A 13x10 room inside walls three tiles high: from its middle the camera
     * sees floor and walls, never the black outside them. */
    Room(20, 20, 32, 29, 3.0f);
    Request(&r, 26, 27);
    r.indoor = true;
    DioramaStage_Choose(&r, &result);
    assert(result.seen.voidFraction == 0.0f);
    /* The 3DS project's count of black cells on the ground behind the walls
     * would have pushed the stage around for nothing; the walls hide them. */
    r.legacy = true;
    {
        struct DioramaStageResult legacy;

        DioramaStage_Choose(&r, &legacy);
        DioramaStage_Measure(&r, legacy.targetX, legacy.targetY, &seen);
        printf("  room with high walls: black seen %.0f%% (legacy stage %.0f%%)\n",
               100.0f * result.seen.voidFraction, 100.0f * seen.voidFraction);
    }
}

static void TestRoomLowWalls(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult result;
    struct DioramaStageView base;

    /* The same room with knee-high walls: some black shows over them; the
     * stage chosen shows no more of it than the one on the player's tile. */
    Room(20, 20, 32, 29, 0.5f);
    Request(&r, 26, 27);
    r.indoor = true;
    DioramaStage_Choose(&r, &result);
    DioramaStage_Measure(&r, 26.5f - result.feetX[0], 27.5f - result.feetY[0], &base);
    assert(result.seen.voidFraction <= base.voidFraction + 1e-5f);
    printf("  room with low walls: black seen %.0f%% (%.0f%% from the player's tile)\n",
           100.0f * result.seen.voidFraction, 100.0f * base.voidFraction);
}

static void TestWallRightBehind(void)
{
    struct DioramaStageRequest r;
    struct DioramaStageResult open, walled;
    struct DioramaStageView seen;
    int fy;

    Fill(DIORAMA_GROUND_PLAIN);
    Request(&r, 32, 40);
    DioramaStage_Choose(&r, &open);
    /* A long wall right behind (north of) the opponent's feet at the open
     * spot, open floor everywhere south of it. */
    fy = (int)floorf(open.targetY + open.feetY[1]) - 1;
    for (int y = 0; y <= fy; ++y)
        TallRow(0, 63, y, 2.0f);
    DioramaStage_Measure(&r, open.targetX, open.targetY, &seen);
    assert(seen.cramped > 0.5f);
    DioramaStage_Choose(&r, &walled);
    assert(walled.seen.cramped < seen.cramped);
    assert(walled.seen.voidFraction == 0.0f && walled.seen.overlap == 0.0f);
    printf("  wall right behind the opponent: pressed %.2f there, %.2f at the stage chosen (%+.0f,%+.0f)\n",
           seen.cramped, walled.seen.cramped, walled.offsetX, walled.offsetY);
}

int main(void)
{
    TestProjection();
    TestOpenField();
    TestWallInFront();
    TestSurfing();
    TestTargetOnABank();
    TestIndoorVoid();
    TestTreesInFrontOfTheOpponent();
    TestLeaningCrown();
    TestDoubles();
    TestRoomWallsHideTheBlack();
    TestRoomLowWalls();
    TestWallRightBehind();
    puts("test_battle_stage: all passed");
    return 0;
}
