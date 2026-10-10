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

int main(void)
{
    TestProjection();
    TestOpenField();
    TestWallInFront();
    TestSurfing();
    TestIndoorVoid();
    puts("test_battle_stage: all passed");
    return 0;
}
