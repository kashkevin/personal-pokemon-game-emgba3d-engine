#ifndef GUARD_DIORAMA_BATTLE_FX_HOST_H
#define GUARD_DIORAMA_BATTLE_FX_HOST_H

/*
 * Battle FX host: runs the Battle FX animation core (battlefx/bfx_core.js, the
 * same code as the Battle FX Lab and the integration package's bfx_host.cpp)
 * inside QuickJS-ng and exposes each frame as plain arrays. A C port of the
 * integration package's native/src/bfx_host.cpp (Battle FX v3.3), kept to
 * what the game uses.
 *
 * Presentation only: the core replays the game's own animation scripts to
 * place its HD effects. Nothing it returns reaches battle state, and the
 * game's own animation (battle_anim.c) keeps running and decides how long an
 * animation lasts (include/diorama/battle_fx.h).
 *
 * Not thread-safe: one thread at a time. Each call re-arms QuickJS's stack
 * guard for the calling thread (BfxHost_* may run on any one thread at a time
 * with `stackBytes` of free stack).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BFX_FPS 59.7275
#define BFX_MAX_MESH_KINDS 24

/* Sprite record layout (floats), mirrors BFX_BRIDGE.SPRITE (checked at init). */
enum
{
    BFX_SP_CENTER = 0, BFX_SP_K = 3, BFX_SP_BOX = 4, BFX_SP_M = 6, BFX_SP_FLIP = 10, BFX_SP_PALROW = 12,
    BFX_SP_BLEND = 13, BFX_SP_BRIGHT = 15, BFX_SP_FLASH = 19, BFX_SP_SHINE = 23, BFX_SP_FADE = 27,
    BFX_SP_LIGHT = 28, BFX_SP_ADD = 31, BFX_SP_KIND = 34, BFX_SP_PIC = 35, BFX_SP_BACK = 36,
    BFX_SP_ORDER = 37, BFX_SP_GLOW = 38, BFX_SP_SIZE = 42, BFX_SP_GBA = 44, BFX_SP_TILE = 46,
    BFX_SP_TAG = 47, BFX_SP_LAYER = 48, BFX_SP_SCAN = 49, BFX_SP_SCANBASE = 50, BFX_SP_TOP = 51,
    BFX_SP_BATTLER = 52, BFX_SP_TINTMUL = 53, BFX_SP_TINTADD = 56, BFX_SP_GRAY = 59, BFX_SP_DEPTHW = 60,
    BFX_SP_STRIDE = 64
};
enum { BFX_KIND_BATTLER = 0, BFX_KIND_CLONE = 1, BFX_KIND_MONPIC = 2, BFX_KIND_MONBG = 3, BFX_KIND_ANIM = 4, BFX_KIND_OBJWIN = 5 };

struct BfxQuads { int n; const float *P, *S, *C, *X; };            /* 4 floats each */
struct BfxRibbons { int nv, ni; const float *pos, *uv, *col, *row; const uint16_t *idx; };
struct BfxRing                                                       /* GPU ring of emission records */
{
    int cap, head, count, lo, hi; /* upload [lo, hi] (all when cleared) */
    bool cleared, add;
    const float *a, *b, *c, *d, *e, *f;
};
struct BfxMeshes { int n; const float *data; };                    /* 19 floats per instance */

struct BfxBattler
{
    bool present, drawn;
    float k, home[2], homeWorld[3], feet[3], gba[2], offsetPx[2], world[3], matrix[4], scale[2], flip[2];
    float blend[2], fade, flash[4], glow[4], shine[3], tintMul[3], tintAdd[3], gray, light[3], lightAdd[3];
    int priority, subpriority, onBgLayer, form, picture; /* picture: 0 mon, 1 substitute, 2 trainer, 3 none */
};

struct BfxPost
{
    float bloom, threshold, exposure, distortion, vignette, sat, grade[4], flash, flashColor[3];
};

struct BfxPacket
{
    uint32_t serial; /* set by the game side: which animation (battle_fx.h), not by the host */
    uint8_t hideBg;  /* set by the game side: BG1 / BG2 (bits 1, 2) whose effect picture an HD kit redraws */
    int frame;
    double t;
    bool done;
    const float *view;   /* 16, column-major world -> camera */
    const float *camera; /* pos3, right3, up3, back3, fovY (deg), near, far, roll */
    float cameraLock;
    struct BfxQuads cards, cardsBack, decals, dist;
    struct BfxRibbons ribbons, ribbonsBack;
    struct BfxRing partsAdd, partsAlpha;
    int meshKinds;
    struct BfxMeshes meshes[BFX_MAX_MESH_KINDS];
    const float *lights; /* 8 floats each: pos3, rgb, intensity, distance */
    int lightCount;
    const float *sprites;
    int spriteCount;
    int pixelEffects, objWindows;
    struct BfxBattler battlers[4];
    struct BfxPost post;
    /* the arena's lights (mesh lighting) */
    float hemiSky[3], hemiGround[3], hemiI, sun[3], sunI, sunDir[3], fog[3], fogDensity;
};

struct BfxBeginResult
{
    char move[48], script[64], anim[48], kind[16], branch[48];
    int frames, extraFrames, totalFrames, attacker, target, variant;
};

struct BfxHost;

struct BfxHost *BfxHost_Create(void);
void BfxHost_Destroy(struct BfxHost *host);
/* Evaluates the core and loads the animation data (both UTF-8 text, not
 * necessarily NUL-terminated). */
bool BfxHost_Init(struct BfxHost *host, const char *core, size_t coreSize, const char *data, size_t dataSize,
                  size_t stackBytes);
bool BfxHost_Settings(struct BfxHost *host, const char *json);
/* A request as JSON (BFX_BRIDGE.begin's argument). False when the core refused
 * it (unknown move, script error): BfxHost_LastError says why. */
bool BfxHost_Begin(struct BfxHost *host, const char *requestJson, struct BfxBeginResult *result);
/* One GBA frame: 1 while the animation runs, 0 once it has ended, -1 on an error. */
int BfxHost_Step(struct BfxHost *host);
/* The frame's renderer input for time t (seconds since begin; t < 0: the
 * current frame's). Its pointers point into the core's arrays: valid until the
 * next BfxHost_* call. False on an error. */
bool BfxHost_Frame(struct BfxHost *host, double t, struct BfxPacket *packet);
/* Sounds and cries the core asked for since the last call: the game plays its
 * own animation's sounds, so these are only counted. */
int BfxHost_DrainSounds(struct BfxHost *host);
int BfxHost_FrameIndex(struct BfxHost *host);
/* The runtime's state hash (multiplayer desync checks, tests). */
bool BfxHost_StateHash(struct BfxHost *host, char *out, size_t outSize);
int BfxHost_MeshKinds(const struct BfxHost *host);
/* Tests and tools: evaluate another script in the core's realm; call a global
 * function with a JSON argument (or none) returning a string (malloc'd, the
 * caller frees it; NULL on an error). */
bool BfxHost_Eval(struct BfxHost *host, const char *source, size_t size, const char *name);
char *BfxHost_CallGlobal(struct BfxHost *host, const char *fn, const char *jsonArg);
/* A global function of no arguments returning a number; `fallback` on an error. */
int BfxHost_CallGlobalInt(struct BfxHost *host, const char *fn, int fallback);
/* ... of one number argument. */
int BfxHost_CallGlobalIntArg(struct BfxHost *host, const char *fn, int arg, int fallback);
/* The move / battle animation ids the data holds, by number: "MOVE_FLAMETHROWER"
 * for move 53; a battle animation by kind ("status", "general", "special",
 * "controller") and number. NULL when absent. */
const char *BfxHost_MoveId(const struct BfxHost *host, int number);
const char *BfxHost_AnimId(const struct BfxHost *host, const char *kind, int number);
/* The other way round: a battle animation id's kind and number; false when absent. */
bool BfxHost_AnimKindNumber(const struct BfxHost *host, const char *id, const char **kind, int *number);
/* QuickJS's automatic garbage collections seen during the host's calls so far; a full collection now
 * (milliseconds it took). */
unsigned BfxHost_GcCount(const struct BfxHost *host);
double BfxHost_CollectGarbage(struct BfxHost *host);
const char *BfxHost_LastError(const struct BfxHost *host);
double BfxHost_LastCallMs(const struct BfxHost *host);

#endif // GUARD_DIORAMA_BATTLE_FX_HOST_H
