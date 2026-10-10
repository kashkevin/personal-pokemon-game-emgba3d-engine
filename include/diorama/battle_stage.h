#ifndef GUARD_DIORAMA_BATTLE_STAGE_H
#define GUARD_DIORAMA_BATTLE_STAGE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Where in the world a 3D battle stands (include/diorama/battle_scene.h): the
 * stage search of ZallaxDev/pokeemerald-3Ds-dualscreen's
 * 3ds_port/src/voxel/voxel_battle.c (MIT, see src/diorama/README.md), in GBA
 * picture terms instead of the 3DS screen's. No GL, no game headers: the
 * cells come through a callback, so it is unit-tested on its own
 * (tests/diorama/test_battle_stage.c).
 *
 * Coordinates are the map's tiles as the 3DS project counts them: tile
 * (x, y) spans x..x+1 and y..y+1, y growing south; the camera looks north.
 */

/* The battle camera, as the stage search and the renderer both see it: the
 * eye `distance` tiles from the target, `pitch` radians down, looking north;
 * `focal` GBA pixels per tile at one tile of depth; the target on the GBA
 * picture's column 120 and row `targetRow`. */
struct DioramaBattleView
{
    float pitch;
    float distance;
    float focal;
    float targetRow;
};

/* One cell of the world, as the stage search reads it. */
struct DioramaStageCell
{
    uint8_t kind;   /* enum DioramaBattleGround */
    bool blocked;   /* collision */
    bool slope;     /* stairs, ledges, cliff edges */
    float top;      /* height of what stands on the cell, tiles (0: nothing) */
    float lift;     /* height of its ground in the world, tiles */
};

/* Fills *cell for tile (x, y); false where nothing is known (past the
 * world the renderer has): outdoors that reads as the map's border (mostly
 * trees), indoors as the black past the room. */
typedef bool (*DioramaStageCellFn)(void *context, int x, int y, struct DioramaStageCell *cell);

struct DioramaStageRequest
{
    struct DioramaBattleView view;
    /* The GBA picture rows and columns the window shows above the text box
     * (wider than 0..240 on a wide window). */
    float visibleLeft, visibleRight, visibleTop, visibleBottom;
    int playerX, playerY;
    uint8_t facing; /* DIR_SOUTH 1, DIR_NORTH 2, DIR_WEST 3, DIR_EAST 4 */
    bool surfing;
    bool wild;
    bool indoor;
    DioramaStageCellFn cellAt;
    void *context;
};

struct DioramaStageResult
{
    float targetX, targetY; /* tiles, the point the camera looks at */
    float ground;           /* height of the ground there */
    float score;
    float offsetX, offsetY; /* from the spot with the player's Pokemon on the player's tile */
    uint8_t want[2];        /* the ground each side was looked for on */
    /* Each side's feet, tiles from the target (the 3DS project's feetX/Z). */
    float feetX[2], feetY[2];
};

/* Ground under GBA picture point (gx, gy), tiles from the target (y south);
 * false when the ray never meets the ground in front of the eye. */
bool DioramaStage_ScreenToGround(const struct DioramaBattleView *view, float gx, float gy,
                                 float *x, float *y);
/* GBA picture point of (x, h, y) tiles from the target, h up; false behind
 * the eye. */
bool DioramaStage_Project(const struct DioramaBattleView *view, float x, float h, float y,
                          float *gx, float *gy);

void DioramaStage_Choose(const struct DioramaStageRequest *request, struct DioramaStageResult *result);

#endif // GUARD_DIORAMA_BATTLE_STAGE_H
