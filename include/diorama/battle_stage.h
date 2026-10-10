#ifndef GUARD_DIORAMA_BATTLE_STAGE_H
#define GUARD_DIORAMA_BATTLE_STAGE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Where in the world a 3D battle stands (include/diorama/battle_scene.h): the
 * stage search of ZallaxDev/pokeemerald-3Ds-dualscreen's
 * 3ds_port/src/voxel/voxel_battle.c (MIT, see src/diorama/README.md), in GBA
 * picture terms instead of the 3DS screen's, with what the battle camera
 * actually sees added to its scoring: the black past a room or the scene,
 * walls pressed against the battlers, and anything standing in front of a
 * battler's picture, measured against each battler's own picture. No GL, no
 * game headers: the cells come through a callback, so it is unit-tested on
 * its own (tests/diorama/test_battle_stage.c).
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

/* One cell of the world, as the stage search reads it: its ground at `lift`,
 * and what stands on it (a wall, a cliff, a tree, a building) a solid column
 * `top` tiles higher. */
struct DioramaStageCell
{
    uint8_t kind;   /* enum DioramaBattleGround */
    bool blocked;   /* collision */
    bool slope;     /* stairs, ledges, cliff edges */
    float top;      /* height of what stands on the cell, tiles (0: nothing) */
    float lift;     /* height of its ground in the world, tiles */
    float north;    /* how far past the cell's north edge its top reaches, tiles
                     * (a crown leaning back) */
};

/* Fills *cell for tile (x, y); false where nothing is known (past the cells
 * the renderer holds), which reads as nothing drawn: the black past a room,
 * or past the scene. */
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
    bool doubles;   /* four battlers' pictures instead of two */
    /* The 3DS project's scoring alone (for comparisons; the game never sets
     * it): no camera view, the two sides' shared picture boxes. */
    bool legacy;
    DioramaStageCellFn cellAt;
    void *context;
};

/* What the battle camera sees from a stage (DioramaStage_Measure). */
struct DioramaStageView
{
    float voidFraction;  /* of the picture above the text box: nothing drawn there */
    float overlap;       /* the most of any battler's picture covered by something in front of it */
    float cramped;       /* the worst wall pressed close behind a battler, 0..1 */
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
    struct DioramaStageView seen; /* from the chosen stage */
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
/* What the camera sees with its target at (targetX, targetY) (tiles, as a
 * result's): reads the world through request->cellAt. */
void DioramaStage_Measure(const struct DioramaStageRequest *request, float targetX, float targetY,
                          struct DioramaStageView *seen);

#endif // GUARD_DIORAMA_BATTLE_STAGE_H
