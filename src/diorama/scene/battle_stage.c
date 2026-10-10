#ifdef ENABLE_DIORAMA

/*
 * The 3D battle's stage (include/diorama/battle_stage.h): a port of
 * ZallaxDev/pokeemerald-3Ds-dualscreen's 3ds_port/src/voxel/voxel_battle.c
 * (MIT; Copyright (c) 2026 ZallaxDev and Pokemon Emerald 3Ds Dual Screen
 * contributors), its scoring kept. What differs: the camera is written in GBA
 * picture terms (the 3DS project's in its 400x240 screen's, with the battle
 * picture at CTR_BATTLE_ZOOM), the cells come through a callback instead of
 * the voxel world, and the whole search runs at once (a PC does it in a few
 * milliseconds).
 *
 * Our own additions: each candidate is also scored by what the battle camera
 * sees from it. Rays from the eye through the picture find the black past a
 * room or the scene (the 3DS project counted every black cell on the ground,
 * also those a wall hides) and walls pressed close behind a battler, and
 * whatever stands in front of a battler is measured against that battler's
 * own picture (the column's eight corners projected), singles or doubles.
 */

#include <math.h>
#include <string.h>

#include "diorama/battle_scene.h"
#include "diorama/battle_stage.h"

#define DIR_SOUTH 1
#define DIR_NORTH 2
#define DIR_WEST 3
#define DIR_EAST 4

/* Taller than this (tiles) is something standing on a cell. */
#define TALL 0.4f

/* What the camera's view costs a stage: per battler picture wholly covered
 * by something in front of it, per picture wholly black, per battler with a
 * wall right behind it. The 3DS project's own terms are worth up to about 8 a
 * side for fitting ground, and half a point a tile of distance. Weighed on the
 * stages chosen from every spot of 44 maps' rooms, caves and gyms (dumped by
 * the game's stage survey): against the 3DS scoring alone, 59% fewer stages
 * with something over a battler, 47% fewer with a wall pressed behind one and
 * 2% fewer showing black. */
#ifndef OVERLAP_WEIGHT /* (a test tool may set them) */
#define OVERLAP_WEIGHT 120.0f
#define VOID_WEIGHT 160.0f
#define CRAMPED_WEIGHT 25.0f
#endif

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

/* ── What the ground is, around the stage ───────────────────────────────── */

/*
 * The cells the search reads, read once: around the stage the search starts
 * from, as far as the target may move from it plus what a camera there shows
 * and stands in front of. A cell outside it reads as nothing drawn.
 */
#define STAGE_REACH 6
#define GRID_X0 (-STAGE_REACH - 14)
#define GRID_X1 (STAGE_REACH + 14)
#define GRID_Z0 (-STAGE_REACH - 16)
#define GRID_Z1 (STAGE_REACH + 10)
#define GRID_W (GRID_X1 - GRID_X0 + 1)
#define GRID_H (GRID_Z1 - GRID_Z0 + 1)
#define STAGE_SIDE (2 * STAGE_REACH + 1)

static struct DioramaStageCell sGrid[GRID_H][GRID_W];
static int sGridX, sGridZ;
static const struct DioramaStageCell sNothing = {DIORAMA_GROUND_VOID, true, false, 0.0f, 0.0f, 0.0f};

static void ReadCell(const struct DioramaStageRequest *request, struct DioramaStageCell *cell, int x, int z)
{
    if (request->cellAt(request->context, x, z, cell))
        return;
    memset(cell, 0, sizeof(*cell));
    cell->blocked = true;
    if (request->legacy && !request->indoor)
    {
        /* The 3DS world held the whole map: outdoors, past it, the map's
         * border, mostly trees. */
        cell->kind = DIORAMA_GROUND_PLAIN;
        cell->top = 2.0f;
    }
    else
    {
        /* Nothing is drawn past the cells the renderer holds: black, as past
         * a room. */
        cell->kind = DIORAMA_GROUND_VOID;
    }
}

static void LoadGrid(const struct DioramaStageRequest *request, float baseX, float baseZ)
{
    unsigned i;

    sGridX = (int)floorf(baseX);
    sGridZ = (int)floorf(baseZ);
    for (i = 0; i < GRID_W * GRID_H; ++i)
    {
        unsigned gx = i % GRID_W, gz = i / GRID_W;

        ReadCell(request, &sGrid[gz][gx], sGridX + GRID_X0 + (int)gx, sGridZ + GRID_Z0 + (int)gz);
    }
}

static const struct DioramaStageCell *GridCell(int x, int z)
{
    int gx = x - sGridX - GRID_X0, gz = z - sGridZ - GRID_Z0;

    if (gx < 0 || gx >= GRID_W || gz < 0 || gz >= GRID_H)
        return &sNothing;
    return &sGrid[gz][gx];
}

static const struct DioramaStageCell *CellAt(float x, float z)
{
    return GridCell((int)floorf(x), (int)floorf(z));
}

/* ── The battlers ───────────────────────────────────────────────────────── */

/*
 * Where the battlers' 64x64 pictures are on the GBA picture (sBattlerCoords,
 * battle_anim_mons.c: the middle of each picture) and where on it each one
 * stands, cut off by the text box at row 112.
 */
struct Battler
{
    float x0, y0, x1, y1;
    float feetX, feetY;
};

static const struct Battler sSingles[2] = {
    {40.0f, 48.0f, 104.0f, 112.0f, 72.0f, 110.0f},   /* the player's */
    {144.0f, 8.0f, 208.0f, 72.0f, 176.0f, 70.0f},    /* the opponent's */
};

static const struct Battler sDoubles[4] = {
    {0.0f, 48.0f, 64.0f, 112.0f, 32.0f, 110.0f},     /* the player's left */
    {58.0f, 52.0f, 122.0f, 112.0f, 90.0f, 112.0f},   /* the player's right */
    {168.0f, 8.0f, 232.0f, 72.0f, 200.0f, 70.0f},    /* the opponents' left */
    {120.0f, 0.0f, 184.0f, 64.0f, 152.0f, 62.0f},    /* the opponents' right */
};

/* ── The search ─────────────────────────────────────────────────────────── */

/*
 * The 3DS project's two sides: each side's feet (the bottom of its 64x64
 * pictures, singles) and the part of the picture each side covers, singles
 * and doubles together.
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
    const struct Battler *battlers;
    unsigned battlerCount;
    float battlerX[4], battlerZ[4]; /* each battler's feet on the ground, tiles from the target */
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
    if (cell->top > TALL)
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
            else if (near->top > TALL)
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

/* The 3DS project's view terms (legacy): anything tall at or in front of a
 * side's feet where that side's pictures are, and every black cell on the
 * ground the picture would show. */
static float LegacyViewScore(const struct StageSearch *s, float tx, float tz, float groundT)
{
    const struct DioramaStageRequest *r = s->request;
    const struct DioramaBattleView *v = &r->view;
    float score = 0.0f;
    int side, dx, dz;

    for (dz = -3; dz <= 7; ++dz)
        for (dx = -6; dx <= 6; ++dx)
        {
            float cx = floorf(tx) + dx, cz = floorf(tz) + dz;
            const struct DioramaStageCell *cell = CellAt(cx, cz);
            float x0, y0, x1, y1, h;

            if (cell->top <= TALL)
                continue;
            h = cell->lift - groundT;
            if (!DioramaStage_Project(v, cx - tx, h + cell->top, cz + 0.5f - tz, &x0, &y0)
             || !DioramaStage_Project(v, cx + 1.0f - tx, h, cz + 1.0f - tz, &x1, &y1))
                continue;
            for (side = 0; side < 2; ++side)
                if (cz + 1.0f - tz > s->feetZ[side] - 0.5f)
                    score -= 24.0f * Overlap(s->rect[side], x0, y0, x1, y1);
        }
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

/* ── What the camera sees ───────────────────────────────────────────────── */

enum { SEE_GROUND, SEE_TALL, SEE_VOID };

struct Hit
{
    int what;
    float x, z; /* where the ray came into the cell it met, tiles from the target */
};

/*
 * What a ray from the eye through picture point (gx, gy) meets first, the
 * camera's target at (tx, tz) on ground groundT: the cells it crosses, one by
 * one from the eye (Amanatides and Woo), each a column from nothing up to its
 * ground and what stands on it; the falling ray meets the first whose top it
 * is at or below on its way out. Over a cell where nothing is drawn it meets
 * nothing, and once it is below the target's ground there it sees black.
 */
static struct Hit Trace(const struct DioramaBattleView *v, float tx, float tz, float groundT, float gx, float gy)
{
    float sinP = sinf(v->pitch), cosP = cosf(v->pitch);
    float nx = (gx - 120.0f) / v->focal, ny = (v->targetRow - gy) / v->focal;
    float dx = nx, dh = -sinP + ny * cosP, dz = -cosP - ny * sinP;
    float ox = tx, oh = groundT + v->distance * sinP, oz = tz + v->distance * cosP;
    int cx = (int)floorf(ox), cz = (int)floorf(oz);
    int stepX = dx > 0.0f ? 1 : -1, stepZ = dz > 0.0f ? 1 : -1;
    float tDeltaX = fabsf(dx) > 1e-6f ? 1.0f / fabsf(dx) : 1e30f;
    float tDeltaZ = fabsf(dz) > 1e-6f ? 1.0f / fabsf(dz) : 1e30f;
    float tMaxX = fabsf(dx) > 1e-6f ? (dx > 0.0f ? (float)cx + 1.0f - ox : ox - (float)cx) * tDeltaX : 1e30f;
    float tMaxZ = fabsf(dz) > 1e-6f ? (dz > 0.0f ? (float)cz + 1.0f - oz : oz - (float)cz) * tDeltaZ : 1e30f;
    struct Hit hit = {SEE_VOID, 0.0f, 0.0f};
    float t0 = 0.0f;
    int i;

    for (i = 0; i < 160; ++i)
    {
        float t1 = fminf(tMaxX, tMaxZ);
        float h = oh + dh * t1;
        const struct DioramaStageCell *cell = GridCell(cx, cz);

        hit.x = ox + dx * t0 - tx;
        hit.z = oz + dz * t0 - tz;
        t0 = t1;
        if (cell->kind == DIORAMA_GROUND_VOID)
        {
            if (h < groundT - 0.25f)
                return hit;
        }
        else if (h <= cell->lift + cell->top)
        {
            hit.what = cell->top > TALL ? SEE_TALL : SEE_GROUND;
            return hit;
        }
        if (tMaxX < tMaxZ)
        {
            cx += stepX;
            tMaxX += tDeltaX;
        }
        else
        {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }
    }
    hit.what = SEE_VOID;
    return hit;
}

/* The share of the picture above the text box (a grid of rays) that would
 * show nothing drawn. */
#define SEE_COLUMNS 16
#define SEE_ROWS 8
#define SEE_BOTTOM 104.0f

static float VoidFraction(const struct DioramaStageRequest *r, float tx, float tz, float groundT)
{
    float top = r->visibleTop, bottom = fminf(r->visibleBottom, SEE_BOTTOM);
    int voids = 0, i, j;

    if (bottom <= top)
        return 0.0f;
    for (j = 0; j < SEE_ROWS; ++j)
        for (i = 0; i < SEE_COLUMNS; ++i)
        {
            float gx = r->visibleLeft + (r->visibleRight - r->visibleLeft) * ((float)i + 0.5f) / SEE_COLUMNS;
            float gy = top + (bottom - top) * ((float)j + 0.5f) / SEE_ROWS;

            if (Trace(&r->view, tx, tz, groundT, gx, gy).what == SEE_VOID)
                ++voids;
        }
    return (float)voids / (float)(SEE_ROWS * SEE_COLUMNS);
}

/* A battler's picture box covered by a box, as a share of the picture. */
static float BattlerCover(const struct Battler *b, const float box[4])
{
    float w = fminf(box[2], b->x1) - fmaxf(box[0], b->x0), h = fminf(box[3], b->y1) - fmaxf(box[1], b->y0);

    return w > 0.0f && h > 0.0f ? w * h / ((b->x1 - b->x0) * (b->y1 - b->y0)) : 0.0f;
}

/*
 * Whatever stands in front of a battler's feet where its picture is: the
 * battlers are drawn over the world, so a tree or a wall nearer the camera
 * than a battler would be drawn behind it. Each tall cell's column is
 * projected corner by corner (stretched north as far as a leaning crown
 * reaches); one reaching past the eye covers the bottom of the picture. Returns the sum over the battlers of the share of each one's
 * picture covered (at most one each); *worst the largest.
 */
static float FrontCover(const struct StageSearch *s, float tx, float tz, float groundT, float *worst)
{
    const struct DioramaBattleView *v = &s->request->view;
    float cover[4] = {0.0f, 0.0f, 0.0f, 0.0f}, total = 0.0f;
    unsigned b;
    int dx, dz;

    for (dz = -4; dz <= 9; ++dz)
        for (dx = -8; dx <= 8; ++dx)
        {
            int cx = (int)floorf(tx) + dx, cz = (int)floorf(tz) + dz;
            const struct DioramaStageCell *cell = GridCell(cx, cz);
            float box[4] = {1e9f, 1e9f, -1e9f, -1e9f};
            bool pastEye = false;
            int corner;

            if (cell->kind == DIORAMA_GROUND_VOID || cell->top <= TALL)
                continue;
            for (corner = 0; corner < 8; ++corner)
            {
                float x = (float)cx + (float)(corner & 1) - tx;
                float z = (corner & 2) ? (float)cz + 1.0f - tz : (float)cz - cell->north - tz;
                float h = cell->lift - groundT + ((corner & 4) ? cell->top : 0.0f);
                float gx, gy;

                if (!DioramaStage_Project(v, x, h, z, &gx, &gy))
                {
                    pastEye = true;
                    continue;
                }
                box[0] = fminf(box[0], gx);
                box[1] = fminf(box[1], gy);
                box[2] = fmaxf(box[2], gx);
                box[3] = fmaxf(box[3], gy);
            }
            if (pastEye)
            {
                box[0] = -1e9f;
                box[2] = 1e9f;
                box[3] = 1e9f;
                if (box[1] > 1e8f)
                    box[1] = -1e9f;
            }
            for (b = 0; b < s->battlerCount; ++b)
                if ((float)cz + 1.0f - tz > s->battlerZ[b] + 0.1f)
                    cover[b] += BattlerCover(&s->battlers[b], box);
        }
    *worst = 0.0f;
    for (b = 0; b < s->battlerCount; ++b)
    {
        float c = fminf(cover[b], 1.0f);

        total += c;
        *worst = fmaxf(*worst, c);
    }
    return total;
}

/*
 * A wall or a tree right behind a battler: the ray through the middle of its
 * picture meets something tall less than two tiles behind its feet, and the
 * battler looks pressed against it. Returns the sum over the battlers, each
 * 0..1 (1 within half a tile); *worst the largest.
 */
static float Cramped(const struct StageSearch *s, float tx, float tz, float groundT, float *worst)
{
    float total = 0.0f;
    unsigned b;

    *worst = 0.0f;
    for (b = 0; b < s->battlerCount; ++b)
    {
        const struct Battler *battler = &s->battlers[b];
        struct Hit hit = Trace(&s->request->view, tx, tz, groundT, battler->feetX,
                               (battler->y0 + battler->feetY) * 0.5f);
        float behind, c;

        if (hit.what != SEE_TALL)
            continue;
        behind = s->battlerZ[b] - hit.z;
        c = behind < 0.5f ? 1.0f : behind < 2.0f ? (2.0f - behind) / 1.5f : 0.0f;
        total += c;
        *worst = fmaxf(*worst, c);
    }
    return total;
}

static float StageScore(const struct StageSearch *s, float tx, float tz, float distance,
                        struct DioramaStageView *seen)
{
    float groundT = CellAt(tx, tz)->lift;
    float score = -0.5f * distance;
    float cover, voids, cramped, worstCover, worstCramped;
    int side;

    for (side = 0; side < 2; ++side)
        score += FeetScore(s, side, tx, tz, groundT);
    if (s->request->legacy)
        return score + LegacyViewScore(s, tx, tz, groundT);
    cover = FrontCover(s, tx, tz, groundT, &worstCover);
    voids = VoidFraction(s->request, tx, tz, groundT);
    cramped = Cramped(s, tx, tz, groundT, &worstCramped);
    score -= OVERLAP_WEIGHT * cover + VOID_WEIGHT * voids + CRAMPED_WEIGHT * cramped;
    if (seen != NULL)
    {
        seen->voidFraction = voids;
        seen->overlap = worstCover;
        seen->cramped = worstCramped;
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

static void InitSearch(struct StageSearch *search, const struct DioramaStageRequest *request)
{
    unsigned side, b;

    memset(search, 0, sizeof(*search));
    search->request = request;
    for (side = 0; side < 2; ++side)
    {
        float gx = side == 0 ? FEET_PLAYER_X : FEET_ENEMY_X, gy = side == 0 ? FEET_PLAYER_Y : FEET_ENEMY_Y;

        DioramaStage_ScreenToGround(&request->view, gx, gy, &search->feetX[side], &search->feetZ[side]);
        memcpy(search->rect[side], sSideRect[side], sizeof(search->rect[side]));
    }
    search->battlers = request->doubles ? sDoubles : sSingles;
    search->battlerCount = request->doubles ? 4 : 2;
    for (b = 0; b < search->battlerCount; ++b)
        DioramaStage_ScreenToGround(&request->view, search->battlers[b].feetX, search->battlers[b].feetY,
                                    &search->battlerX[b], &search->battlerZ[b]);
}

void DioramaStage_Choose(const struct DioramaStageRequest *request, struct DioramaStageResult *result)
{
    struct StageSearch search;
    int frontX = 0, frontZ = 0, side;
    float px = (float)request->playerX, pz = (float)request->playerY;
    float baseX, baseZ;
    uint8_t here, front;
    unsigned i;

    InitSearch(&search, request);
    memset(result, 0, sizeof(*result));
    for (side = 0; side < 2; ++side)
    {
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
    LoadGrid(request, baseX, baseZ);

    result->targetX = baseX;
    result->targetY = baseZ;
    result->score = -1.0e9f;
    for (i = 0; i < STAGE_SIDE * STAGE_SIDE; ++i)
    {
        int dx = (int)(i % STAGE_SIDE) - STAGE_REACH;
        int dz = (int)(i / STAGE_SIDE) - STAGE_REACH;
        float tx = baseX + dx, tz = baseZ + dz;
        struct DioramaStageView seen = {0.0f, 0.0f, 0.0f};
        float score = StageScore(&search, tx, tz, sqrtf((float)(dx * dx + dz * dz)), &seen);

        if (score > result->score)
        {
            result->score = score;
            result->targetX = tx;
            result->targetY = tz;
            result->offsetX = (float)dx;
            result->offsetY = (float)dz;
            result->seen = seen;
        }
    }
    result->ground = CellAt(result->targetX, result->targetY)->lift;
    result->want[0] = search.want[0];
    result->want[1] = search.want[1];
}

void DioramaStage_Measure(const struct DioramaStageRequest *request, float targetX, float targetY,
                          struct DioramaStageView *seen)
{
    struct DioramaStageRequest r = *request;
    struct StageSearch search;
    float groundT;

    r.legacy = false;
    InitSearch(&search, &r);
    LoadGrid(&r, targetX, targetY);
    groundT = CellAt(targetX, targetY)->lift;
    FrontCover(&search, targetX, targetY, groundT, &seen->overlap);
    seen->voidFraction = VoidFraction(&r, targetX, targetY, groundT);
    Cramped(&search, targetX, targetY, groundT, &seen->cramped);
}

#endif
