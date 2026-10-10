#ifdef ENABLE_DIORAMA

/*
 * The 3D battle's stage (include/diorama/battle_stage.h): a port of
 * ZallaxDev/pokeemerald-3Ds-dualscreen's 3ds_port/src/voxel/voxel_battle.c
 * (MIT; Copyright (c) 2026 ZallaxDev and Pokemon Emerald 3Ds Dual Screen
 * contributors), its scoring unchanged. What differs: the camera is written
 * in GBA picture terms (the 3DS project's in its 400x240 screen's, with the
 * battle picture at CTR_BATTLE_ZOOM), the cells come through a callback
 * instead of the voxel world, and the whole search runs at once (a PC does
 * it in well under a millisecond).
 */

#include <math.h>
#include <string.h>

#include "diorama/battle_scene.h"
#include "diorama/battle_stage.h"

#define DIR_SOUTH 1
#define DIR_NORTH 2
#define DIR_WEST 3
#define DIR_EAST 4

/* ── The battle camera ─────────────────────────────────────────────────── */

/*
 * The camera looks north from the south, the target at the origin and the
 * ground there at height 0: the eye at (0, D sin p, D cos p), y growing south.
 */
bool DioramaStage_ScreenToGround(const struct DioramaBattleView *view, float gx, float gy,
                                 float *x, float *y)
{
    float sinP = sinf(view->pitch), cosP = cosf(view->pitch);
    float eyeY = view->distance * sinP, eyeZ = view->distance * cosP;
    float nx = (gx - 120.0f) / view->focal;
    float ny = (view->targetRow - gy) / view->focal;
    /* forward (0, -sin, -cos), right (1, 0, 0), up (0, cos, -sin) */
    float dx = nx;
    float dy = -sinP + ny * cosP;
    float dz = -cosP - ny * sinP;
    float t;

    if (dy >= -0.01f)
    {
        *x = dx * 100.0f;
        *y = eyeZ + dz * 100.0f;
        return false;
    }
    t = eyeY / -dy;
    *x = dx * t;
    *y = eyeZ + dz * t;
    return true;
}

bool DioramaStage_Project(const struct DioramaBattleView *view, float x, float h, float y,
                          float *gx, float *gy)
{
    float sinP = sinf(view->pitch), cosP = cosf(view->pitch);
    float ry = h - view->distance * sinP, rz = y - view->distance * cosP;
    float depth = -ry * sinP - rz * cosP;
    float up = ry * cosP - rz * sinP;

    if (depth < 0.1f)
        return false;
    *gx = 120.0f + x / depth * view->focal;
    *gy = view->targetRow - up / depth * view->focal;
    return true;
}

/* ── What the ground is, around the player ──────────────────────────────── */

/*
 * The cells the search reads, read once: around the player's spot, as far as
 * the target may move from it plus what a camera there shows and stands in
 * front of. A cell outside it reads as plain ground.
 */
#define STAGE_REACH 6
#define GRID_X0 (-STAGE_REACH - 11)
#define GRID_X1 (STAGE_REACH + 11)
#define GRID_Z0 (-STAGE_REACH - 10)
#define GRID_Z1 (STAGE_REACH + 8)
#define GRID_W (GRID_X1 - GRID_X0 + 1)
#define GRID_H (GRID_Z1 - GRID_Z0 + 1)
#define STAGE_SIDE (2 * STAGE_REACH + 1)

static struct DioramaStageCell sGrid[GRID_H][GRID_W];
static int sGridX, sGridZ;

static void ReadCell(const struct DioramaStageRequest *request, struct DioramaStageCell *cell, int x, int z)
{
    if (!request->cellAt(request->context, x, z, cell))
    {
        /* Indoors the room ends in black; outdoors in the map's border,
         * which is mostly trees. */
        memset(cell, 0, sizeof(*cell));
        cell->kind = request->indoor ? DIORAMA_GROUND_VOID : DIORAMA_GROUND_PLAIN;
        cell->top = request->indoor ? 0.0f : 2.0f;
        cell->blocked = true;
    }
}

static const struct DioramaStageCell *CellAt(float x, float z)
{
    static const struct DioramaStageCell sPlain = {DIORAMA_GROUND_PLAIN, false, false, 0.0f, 0.0f};
    int gx = (int)floorf(x) - sGridX - GRID_X0, gz = (int)floorf(z) - sGridZ - GRID_Z0;

    if (gx < 0 || gx >= GRID_W || gz < 0 || gz >= GRID_H)
        return &sPlain;
    return &sGrid[gz][gx];
}

/* ── The search ─────────────────────────────────────────────────────────── */

/*
 * Where the battlers stand on the GBA picture (sBattlerCoords, the bottom of
 * their 64x64 pictures: battle_anim_mons.c), and the part of it each side's
 * pictures cover, singles and doubles together.
 */
#define FEET_PLAYER_X 72.0f
#define FEET_PLAYER_Y 110.0f
#define FEET_ENEMY_X 176.0f
#define FEET_ENEMY_Y 70.0f
static const float sSideRect[2][4] = {
    {16.0f, 40.0f, 128.0f, 112.0f}, /* the player's, x0 y0 x1 y1 */
    {128.0f, 8.0f, 224.0f, 76.0f},  /* the opponent's */
};

struct StageSearch
{
    const struct DioramaStageRequest *request;
    float feetX[2], feetZ[2];
    float rect[2][4];
    uint8_t want[2];
};

static float Overlap(const float a[4], float x0, float y0, float x1, float y1)
{
    float w = fminf(a[2], x1) - fmaxf(a[0], x0), h = fminf(a[3], y1) - fmaxf(a[1], y0);

    return w > 0.0f && h > 0.0f ? w * h / ((a[2] - a[0]) * (a[3] - a[1])) : 0.0f;
}

static float FeetScore(const struct StageSearch *s, int side, float tx, float tz, float groundT)
{
    float fx = tx + s->feetX[side], fz = tz + s->feetZ[side];
    const struct DioramaStageCell *cell = CellAt(fx, fz);
    uint8_t want = s->want[side];
    float score = 0.0f;
    int dx, dz;

    if (cell->kind == DIORAMA_GROUND_VOID)
        return -60.0f;
    if (cell->top > 0.4f)
        score -= 16.0f;
    if (want == DIORAMA_GROUND_WATER)
        score += cell->kind == DIORAMA_GROUND_WATER ? 8.0f : -8.0f;
    else
    {
        if (cell->kind == DIORAMA_GROUND_WATER)
            score -= 12.0f;
        else if (cell->blocked)
            score -= 6.0f;
        if (cell->kind == want)
            score += 4.0f;
    }
    if (cell->slope)
        score -= 4.0f;
    score -= 3.0f * fabsf(cell->lift - groundT);
    /* The ground around the feet: the same kind, open. */
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx)
        {
            const struct DioramaStageCell *near = CellAt(fx + dx, fz + dz);

            if (dx == 0 && dz == 0)
                continue;
            if (near->kind == DIORAMA_GROUND_VOID)
                score -= 3.0f;
            else if (near->top > 0.4f)
                score -= 1.0f;
            if (near->kind == want)
                score += 0.5f;
            else if ((near->kind == DIORAMA_GROUND_WATER) != (want == DIORAMA_GROUND_WATER))
                score -= 0.8f;
            if (near->slope)
                score -= 0.4f;
        }
    return score;
}

static float StageScore(const struct StageSearch *s, float tx, float tz, float distance)
{
    const struct DioramaStageRequest *r = s->request;
    const struct DioramaBattleView *v = &r->view;
    float groundT = CellAt(tx, tz)->lift;
    float score = -0.5f * distance;
    int side, dx, dz;

    for (side = 0; side < 2; ++side)
        score += FeetScore(s, side, tx, tz, groundT);

    /*
     * Anything standing tall where a side's pictures are, at or in front of
     * its feet: the Pokemon would be drawn over a tree or a wall it should
     * stand before. What rises behind them is scenery.
     */
    for (dz = -3; dz <= 7; ++dz)
        for (dx = -6; dx <= 6; ++dx)
        {
            float cx = floorf(tx) + dx, cz = floorf(tz) + dz;
            const struct DioramaStageCell *cell = CellAt(cx, cz);
            float x0, y0, x1, y1, h;

            if (cell->top <= 0.4f)
                continue;
            h = cell->lift - groundT;
            if (!DioramaStage_Project(v, cx - tx, h + cell->top, cz + 0.5f - tz, &x0, &y0)
             || !DioramaStage_Project(v, cx + 1.0f - tx, h, cz + 1.0f - tz, &x1, &y1))
                continue;
            for (side = 0; side < 2; ++side)
                if (cz + 1.0f - tz > s->feetZ[side] - 0.5f)
                    score -= 24.0f * Overlap(s->rect[side], x0, y0, x1, y1);
        }

    /* Nothing drawn on screen: black where the room or the world ends. */
    for (dz = -10; dz <= 4; ++dz)
        for (dx = -12; dx <= 12; ++dx)
        {
            float cx = floorf(tx) + dx + 0.5f, cz = floorf(tz) + dz + 0.5f, sx, sy;

            if (CellAt(cx, cz)->kind != DIORAMA_GROUND_VOID)
                continue;
            if (!DioramaStage_Project(v, cx - tx, 0.0f, cz - tz, &sx, &sy)
             || sx < r->visibleLeft || sx > r->visibleRight
             || sy < r->visibleTop || sy > r->visibleBottom)
                continue;
            /* Low on the picture (the 3DS screen's line 90) it shows more. */
            score -= sy > 39.0f ? 1.2f : 0.5f;
        }
    return score;
}

static uint8_t KindAt(const struct DioramaStageRequest *request, int x, int z)
{
    struct DioramaStageCell cell;

    if (!request->cellAt(request->context, x, z, &cell))
        return request->indoor ? DIORAMA_GROUND_VOID : DIORAMA_GROUND_PLAIN;
    return cell.kind;
}

void DioramaStage_Choose(const struct DioramaStageRequest *request, struct DioramaStageResult *result)
{
    struct StageSearch search;
    int frontX = 0, frontZ = 0, side;
    float px = (float)request->playerX, pz = (float)request->playerY;
    float baseX, baseZ;
    uint8_t here, front;
    unsigned i;

    memset(&search, 0, sizeof(search));
    memset(result, 0, sizeof(*result));
    search.request = request;
    for (side = 0; side < 2; ++side)
    {
        float gx = side == 0 ? FEET_PLAYER_X : FEET_ENEMY_X, gy = side == 0 ? FEET_PLAYER_Y : FEET_ENEMY_Y;

        DioramaStage_ScreenToGround(&request->view, gx, gy, &search.feetX[side], &search.feetZ[side]);
        memcpy(search.rect[side], sSideRect[side], sizeof(search.rect[side]));
        result->feetX[side] = search.feetX[side];
        result->feetY[side] = search.feetZ[side];
    }

    switch (request->facing)
    {
    case DIR_NORTH: frontZ = -1; break;
    case DIR_SOUTH: frontZ = 1; break;
    case DIR_WEST: frontX = -1; break;
    case DIR_EAST: frontX = 1; break;
    }
    here = KindAt(request, request->playerX, request->playerY);
    front = KindAt(request, request->playerX + frontX, request->playerY + frontZ);
    /*
     * What each side stands on. The player's Pokemon, on what the player
     * stands on (water when surfing). The other: a wild one where it came
     * from - the grass the player walked into, or the water the player is
     * fishing in, which is in front of them on land with no grass to come
     * out of - and a trainer's on the ground in front of the player, where
     * the trainer came to stand.
     */
    search.want[0] = request->surfing ? DIORAMA_GROUND_WATER : here;
    if (request->surfing)
        search.want[1] = DIORAMA_GROUND_WATER;
    else if (request->wild)
        search.want[1] = here != DIORAMA_GROUND_GRASS && front == DIORAMA_GROUND_WATER ? DIORAMA_GROUND_WATER : here;
    else
        search.want[1] = front;
    for (side = 0; side < 2; ++side)
        if (search.want[side] == DIORAMA_GROUND_VOID)
            search.want[side] = DIORAMA_GROUND_PLAIN;

    /* The spot with the player's Pokemon on the player's own tile. */
    baseX = px + 0.5f - search.feetX[0];
    baseZ = pz + 0.5f - search.feetZ[0];
    sGridX = (int)floorf(baseX);
    sGridZ = (int)floorf(baseZ);
    for (i = 0; i < GRID_W * GRID_H; ++i)
    {
        unsigned gx = i % GRID_W, gz = i / GRID_W;

        ReadCell(request, &sGrid[gz][gx], sGridX + GRID_X0 + (int)gx, sGridZ + GRID_Z0 + (int)gz);
    }

    result->targetX = baseX;
    result->targetY = baseZ;
    result->score = -1.0e9f;
    for (i = 0; i < STAGE_SIDE * STAGE_SIDE; ++i)
    {
        int dx = (int)(i % STAGE_SIDE) - STAGE_REACH;
        int dz = (int)(i / STAGE_SIDE) - STAGE_REACH;
        float tx = baseX + dx, tz = baseZ + dz;
        float score = StageScore(&search, tx, tz, sqrtf((float)(dx * dx + dz * dz)));

        if (score > result->score)
        {
            result->score = score;
            result->targetX = tx;
            result->targetY = tz;
            result->offsetX = (float)dx;
            result->offsetY = (float)dz;
        }
    }
    result->ground = CellAt(result->targetX, result->targetY)->lift;
    result->want[0] = search.want[0];
    result->want[1] = search.want[1];
}

#endif
