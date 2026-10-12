#ifndef GUARD_DIORAMA_BATTLE_ENVIRONMENT_H
#define GUARD_DIORAMA_BATTLE_ENVIRONMENT_H

/*
 * The 3D battle's environment, for the systems that will draw in it or over it: animated
 * Pokemon sprites stood in the world, move effects, a new Battle HUD. Where each battler
 * stands and how deep its picture is, points to attach effects to, the camera and a matrix
 * for it, the light, the window areas the battle's own picture uses, a camera impulse, and
 * passes drawn in the world with it. Read by the render thread while a battle is drawn in
 * 3D (DioramaGLBattle_IsActive); everything is a view of what the battle already draws -
 * nothing here changes a battle, its timing or its picture. (gl_battle.c)
 *
 * The world's units are tiles: x east, y up, z north (a map's row r is z = -r, as the terrain
 * and the models are built); a battler's ground is the 3D view's ground under the point the
 * game stands its sprite on.
 */

#ifdef ENABLE_DIORAMA

#include <stdbool.h>
#include <stdint.h>

enum DioramaBattleAttach
{
    DIORAMA_BATTLE_ATTACH_FEET,    /* the ground under it */
    DIORAMA_BATTLE_ATTACH_CENTRE,  /* the middle of its picture, stood on the ground */
    DIORAMA_BATTLE_ATTACH_TOP,     /* the top of its picture */
    DIORAMA_BATTLE_ATTACH_FRONT,   /* the centre, half a tile towards the other side */
    DIORAMA_BATTLE_ATTACH_COUNT
};

struct DioramaBattleBattler
{
    bool present;
    bool opponent;
    uint8_t battler, position;    /* B_BATTLER index, B_POSITION_* */
    float window[4];              /* its 64 x 64 picture in window pixels: x0, y0, x1, y1 */
    float ground[3];              /* the world point it stands on */
    float depth;                  /* that point's view depth, tiles */
    float pixelsPerTile;          /* window pixels a tile spans across, at that depth */
    float height;                 /* its picture's height in tiles there */
    float attach[DIORAMA_BATTLE_ATTACH_COUNT][3];
};

struct DioramaBattleCamera
{
    float eye[3], target[3];
    float pitch;                  /* radians down from the horizon */
    float focal, aspect, screenOffsetY; /* the scene shaders' terms (terrain_mesh.h rig) */
    float viewProjection[16];     /* column-major: world (x, y, z, 1) to clip, as the scene draws */
    int windowW, windowH;
    float picture[4];             /* the battle's 240 x 160 picture in window pixels */
};

struct DioramaBattleLight
{
    bool indoor, cave, night;
    float fadeScale[3], fadeOffset[3]; /* the palette's fade and night tint, per channel */
    float haze[3], hazeColour[3];      /* the distance haze at the battle camera's pitch */
    float fog;                         /* the game's fog, 0-1 */
};

enum DioramaBattleKind
{
    DIORAMA_BATTLE_KIND_OUTDOOR,
    DIORAMA_BATTLE_KIND_WATER,
    DIORAMA_BATTLE_KIND_UNDERWATER,
    DIORAMA_BATTLE_KIND_CAVE,
    DIORAMA_BATTLE_KIND_INDOOR,
    DIORAMA_BATTLE_KIND_GYM,
    DIORAMA_BATTLE_KIND_LEAGUE,
    DIORAMA_BATTLE_KIND_FRONTIER,
};

/* The window areas the battle's own picture puts its interface in this frame: the
 * healthboxes, and the text and menu band; and a frame inside the window a new interface can
 * use without covering a battler (window pixels). */
#define DIORAMA_BATTLE_MAX_UI_RECTS 8
struct DioramaBattleSafeAreas
{
    unsigned count;
    float ui[DIORAMA_BATTLE_MAX_UI_RECTS][4];
    float battlers[4][4];
    unsigned battlerCount;
    float safe[4];
};

bool DioramaBattleEnv_IsActive(void);
bool DioramaBattleEnv_GetBattler(unsigned battler, struct DioramaBattleBattler *out);
bool DioramaBattleEnv_GetCamera(struct DioramaBattleCamera *out);
/* A world point in window pixels and its view depth; false behind the eye. */
bool DioramaBattleEnv_WorldToWindow(const float world[3], float window[2], float *depth);
/* Where a window pixel's line of sight meets the ground plane at height `y`. */
bool DioramaBattleEnv_WindowToGround(float wx, float wy, float y, float world[3]);
void DioramaBattleEnv_GetLight(struct DioramaBattleLight *out);
enum DioramaBattleKind DioramaBattleEnv_GetKind(void);
void DioramaBattleEnv_GetSafeAreas(struct DioramaBattleSafeAreas *out);

/* A camera impulse a move's effect may ask for: a shake of `shake` tiles and a push in of
 * `push` (0.1 = a tenth closer), dying away over `frames` frames. The view returns to the
 * battle camera's own; a battle's picture and timing are untouched. */
void DioramaBattleEnv_CameraImpulse(float shake, float push, unsigned frames);

/* Passes drawn in the world after its terrain, buildings and trees (and before the backdrop
 * and the battle's picture), depth-tested against them: props, effects stood in the world,
 * animated sprites. `order` sorts them; at most DIORAMA_BATTLE_MAX_WORLD_PASSES. */
#define DIORAMA_BATTLE_MAX_WORLD_PASSES 8
typedef void (*DioramaBattleWorldPass)(const struct DioramaBattleCamera *camera, void *user);
bool DioramaBattleEnv_AddWorldPass(DioramaBattleWorldPass pass, void *user, int order);
void DioramaBattleEnv_RemoveWorldPass(DioramaBattleWorldPass pass, void *user);

#endif // ENABLE_DIORAMA

#endif // GUARD_DIORAMA_BATTLE_ENVIRONMENT_H
