#ifndef GUARD_DIORAMA_BATTLE_FX_H
#define GUARD_DIORAMA_BATTLE_FX_H

/*
 * Battle FX in the 3D battle: the HD move and battle animations of the Battle
 * FX package (v3.3: the 621 Gen 1-6 moves and the 83 battle animations),
 * drawn in the 3D battle view in place of the GBA effect sprites.
 *
 * The game's own animation stays the authority. battle_anim.c launches and
 * runs every animation exactly as before - its script, its sprites, tasks,
 * sounds, palette blends, battler motion and its length - and the battle
 * waits for it as ever. Next to it the Battle FX core (QuickJS-ng running
 * battlefx/bfx_core.js, include/diorama/battle_fx_host.h) replays the same
 * script from the same request, one step per frame the game's animation
 * advances, to place its HD effects; it ends when the game's animation ends.
 * Its output only reaches the screen: while it draws, the 3D view leaves the
 * game's effect sprites (graphics tags ANIM_TAG_*) out of the GBA picture
 * (gDioramaSceneSkipObj) and draws the HD effects over the picture and the
 * world instead. Nothing here writes game memory or decides anything about
 * the battle, so damage, accuracy, turn order, saves and multiplayer are
 * untouched; a failure only shows the GBA effects again.
 *
 * The core runs on a thread of its own; the game never waits for it (the
 * frame boundary pairs its frames with the pictures: src/diorama/battlefx/
 * README.md).
 *
 * Settings (development): PKMN_BATTLEFX=0 plays the original GBA animations;
 * PKMN_BATTLEFX_DIR=<folder> reads the core from there (default: the
 * "battlefx" folder beside the executable, then in the working folder);
 * PKMN_BATTLEFX_LOG=1 logs every animation (the game's and the core's frame
 * counts, the core's cost, how its pictures went); PKMN_BATTLEFX_SYNC=1 makes
 * the game wait for the core every frame (the default in test builds). The
 * same keys can be put in battlefx/settings.cfg (key=value lines: enabled,
 * log, sync). Failures are always logged and the GBA animation is shown.
 *
 * Built only with DIORAMA=1 (ENABLE_DIORAMA).
 */

#include <stdbool.h>
#include <stdint.h>

/* ── Game thread (battle_anim.c, sprite.c, main.c) ── */

/* LaunchBattleAnimation, once the script is chosen (`script`: the script
 * battle_anim.c will run, Secret Power's swap included). */
void BattleFx_OnLaunch(uint32_t animType, uint32_t animId, const void *script);
/* RunAnimScriptCommand / WaitAnimFrameCount: the game's animation advanced a
 * frame (gAnimScriptCallback was called). */
void BattleFx_OnAnimTick(void);
/* The start of a game frame (main.c, before the callbacks): the sound state
 * the frame's tasks will see (sound advances only in the vblank). */
void BattleFx_OnFrameBegin(void);
/* The end of a game frame (main.c, after the callbacks). */
void BattleFx_OnFrameEnd(void);

/* The two animations the battle controllers play themselves, outside
 * battle_anim.c (the package's CONTROLLER_SEND_OUT and CONTROLLER_SHINY):
 * pokeball.c Task_DoPokeballSendOutAnim once it has made the ball sprite of a
 * send-out (`arg`: the ball, enum PokeBall), battle_anim_throw.c
 * TryShinyAnimation once it has started a shiny Pokemon's stars. The core
 * replays one at a time and only when it is alone: two that begin in the same
 * frame (a double battle's opening) or one while another plays stay GBA. */
enum
{
    BATTLEFX_CONTROLLER_SEND_OUT = 1,
    BATTLEFX_CONTROLLER_SHINY = 2,
};
void BattleFx_OnControllerAnim(uint32_t kind, uint32_t battler, uint32_t arg);

/* The effect sprites in OAM (sprite.c BuildOamBuffer / LoadOam, as the map's
 * sprites are marked for the 3D view, battle_scene.h). */
struct Sprite;
void BattleFx_BeginOam(void);
void BattleFx_MarkOam(const struct Sprite *sprite, uint8_t first, uint8_t end);
void BattleFx_LatchOam(void);
/* The graphics tag of what a sprite shows: the tag of its tile sheet (TAG_NONE
 * without one). Not sprite->template->tileTag: a template made on the heap
 * (AddItemIconSprite: the last used Poke Ball's icon on a wild battle's action
 * menu, Bestow's item) is freed once its sprite is made, and that memory then
 * holds whatever is allocated there next. */
uint16_t BattleFx_SpriteGfxTag(const struct Sprite *sprite);
/* Whether a sprite is an effect sprite the HD layer stands in for (graphics
 * ANIM_TAG_*, the Poke Balls and their particles). */
bool BattleFx_IsEffectSprite(const struct Sprite *sprite);

/* ── Frame boundary (main thread, the game thread parked; sdl2.c VDraw) ── */

/* Takes the frame the game thread finished for the picture about to be drawn
 * and, when the 3D battle view shows it (`view3d`), adds the effect sprites
 * the HD layer stands in for to `skipObj` (gDioramaSceneSkipObj) and the BG
 * layers whose effect picture it redraws to `hideBg` (gDioramaSceneHideBg). */
void BattleFx_FrameBoundary(bool view3d, uint32_t skipObj[4], uint8_t *hideBg);
/* BG layers (bit n: BGn) the GBA renderer leaves out of the frame being drawn,
 * as if DISPCNT had them off (gba_easy_draw.c): set before DrawFrame. */
extern uint8_t gDioramaSceneHideBg;

/* ── Drawing (main thread, GL context; gl_battle.c) ── */

/* Where the battle's GBA picture is on the window (gl_battle.c's Layout):
 * window size in pixels, the picture's rectangle (top-left origin) and scale. */
struct BattleFxLayout
{
    int outW, outH;
    float gameX, gameY, gameW, gameH, scale;
};

/* The effects behind the battlers (decals, back cards and ribbons, battler
 * glows): after the world, before the picture. */
void BattleFxGL_DrawBack(const struct BattleFxLayout *layout);
/* The effects in front: after the picture. */
void BattleFxGL_DrawFront(const struct BattleFxLayout *layout);
void BattleFxGL_Shutdown(void);

/* ── Diagnostics (logs, tests) ── */

struct BattleFxAnimInfo
{
    uint32_t serial;      /* counts animations begun */
    bool active;          /* the core replays the game's animation now */
    bool failed;          /* the last one failed (its GBA effects were shown) */
    char id[48];          /* MOVE_* / B_ANIM_* / CONTROLLER_* the core plays */
    int plannedFrames;    /* the core's length for it (script and its own tail) */
    int scriptFrames;     /* ... of which the game's script (the rest: the core's presentation tail) */
    int gameFrames;       /* frames the game's animation has advanced */
    int coreFrames;       /* frames the core has stepped */
    double beginMs, stepMsTotal, stepMsWorst;
    double stepMsWorstPlain; /* the worst step without a QuickJS garbage collection in it */
    int gcSteps;             /* steps with one */
    bool gcInBegin;
};
void BattleFx_AnimInfo(struct BattleFxAnimInfo *info);
unsigned BattleFx_Failures(void);
/* The core's id for a move number ("MOVE_FLAMETHROWER"), NULL if the core
 * is not ready or has no animation for it. */
const char *BattleFx_CoreMoveId(uint32_t move);

/* ── Shared between the two (battle_fx_game.c <-> gl_battle_fx.c) ── */

/* Whether Battle FX draws this frame (an animation's HD frame is ready). */
bool BattleFx_Drawing(void);
/* The settings (also used by the renderer). */
bool BattleFx_Enabled(void);
bool BattleFx_Logging(void);
const char *BattleFx_DataDir(void);

#endif // GUARD_DIORAMA_BATTLE_FX_H
