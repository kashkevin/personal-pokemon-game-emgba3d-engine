#ifdef ENABLE_DIORAMA

/*
 * Battle FX: the game side (include/diorama/battle_fx.h).
 *
 * Game thread: each animation battle_anim.c launches is requested from the
 * Battle FX core too, and the core is stepped once for every frame in which
 * the game's own animation advances (gAnimScriptCallback ran). The game's
 * animation decides everything - when it starts, how long it lasts, what it
 * does to the battle - and the core's frame is only drawn. When the game's
 * animation ends, so does the core's; a core that ends first just stops
 * drawing (the game's animation may wait longer, for a sound to finish).
 *
 * Main thread, at the frame boundary: the frame the game thread finished is
 * handed to the renderer (gl_battle_fx.c), and the GBA effect sprites it
 * stands in for are left out of the GBA picture drawn with it.
 *
 * Everything here reads the game; nothing writes game memory.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_gfx_sfx_util.h"
#include "battle_util.h"
#include "data.h"
#include "item.h"
#include "m4a.h"
#include "main.h"
#include "move.h"
#include "pokeball.h"
#include "pokemon.h"
#include "random.h"
#include "sound.h"
#include "sprite.h"
#include "constants/battle_anim.h"
#include "constants/battle_script_commands.h"
#include "constants/battle_string_ids.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "diorama/battle_fx.h"
#include "diorama/battle_fx_host.h"
#include "diorama/battle_fx_render.h"
#include "diorama/battle_fx_test.h"

/* battle_anim_mons.c (not declared in a header) */
u8 GetBattlerYDelta(enum BattlerId battler, enum Species species);
u8 GetBattlerElevation(enum BattlerId battler, enum Species species);

#define JS_STACK_BYTES (512 * 1024)
#define LOADER_STACK_BYTES (8 * 1024 * 1024)

enum CoreState
{
    CORE_IDLE,
    CORE_LOADING,
    CORE_READY,
    CORE_FAILED,
};

/* The player builds' drawing budget a picture for the HD layers (battlefx/settings.cfg drawBudgetMs,
 * PKMN_BATTLEFX_DRAW_BUDGET_MS; 0: none): half a 60 Hz frame, the other half left to the 3D battle. */
#define BFX_DRAW_BUDGET_MS 8

/* Settings and the core (loaded once, on a thread of its own). */
static struct
{
    bool settingsRead;
    bool enabled;
    bool log;
    bool sync;          /* the game thread waits for the core every frame (tests) */
    int drawBudgetMs;   /* the HD layers' drawing budget a picture (0: none; ShowWanted) */
    char dir[1024];
    SDL_atomic_t state; /* enum CoreState */
    SDL_Thread *loader;
    struct BfxHost *host;
    double loadMs;
} sCore;

/* The core's animation, as the core's thread has it (sRun) and as the game thread
 * last read it (sQ.status, sAnim.status): "The core's thread" below. */
struct CoreStatus
{
    uint32_t serial;
    bool begun, refused, failed, ended, over;
    int frames;  /* frames made */
    int lastSeq; /* the last step done */
    struct BfxBeginResult info;
    double beginMs, stepMs, worstStepMs, worstPlainStepMs;
    int gcSteps;
    bool gcInBegin;
    char id[48];
    char error[200];
};

/* Game frames counted (BattleFx_OnFrameBegin; game thread). */
static u32 sGameFrame;
static bool EarlierEffectSprites(void);

/* The animation in progress (game thread). */
static struct
{
    bool active;
    bool hdEnded;       /* the core's animation ended (the game's may go on) */
    bool ticked;        /* the game's animation advanced this frame */
    bool failed;        /* its GBA effects show (failed, refused, the core too far behind) */
    uint32_t animType, animId;
    uint8_t controller; /* BATTLEFX_CONTROLLER_*: stepped every frame, ends with the core */
    char id[48];
    int gameFrames;
    bool inTail;        /* the game's script has ended; effect sprites it left still move (stepped every frame) */
    int tailFrames;
    int seq;            /* the core's frames asked for */
    uint32_t serial;
    struct CoreStatus status; /* the core's, as last read */
} sAnim;

/* Effect sprites in OAM: marked while the OAM buffer is built (game thread),
 * latched as it is loaded into OAM (VBlank, main thread). */
static uint32_t sEffectOamPending[4];
static uint32_t sEffectOam[4];
/* ... and the battlers' sprites: OAM entry of battler b, 0xFF when not in OAM. */
static uint8_t sBattlerOamPending[MAX_BATTLERS_COUNT];
static uint8_t sBattlerOam[MAX_BATTLERS_COUNT];
static struct BattleFxGameBattler sGameBattlers[MAX_BATTLERS_COUNT];

/* The controllers' own animations that began this frame (BattleFx_OnControllerAnim),
 * begun or not at its end: one alone is replayed, more at once stay GBA. */
#define MAX_CONTROLLER_REQUESTS 4
static struct
{
    uint8_t kind, battler;
    uint16_t ball;
    bool intro;
} sControllerPending[MAX_CONTROLLER_REQUESTS];
static int sControllerPendingCount;

/* Ids whose request the core refused, logged once each. */
#define MAX_REFUSED 64
static char sRefused[MAX_REFUSED][48];
static int sRefusedCount;
static unsigned sFailures;

/* ── Settings ─────────────────────────────────────────────────────────── */

static bool FileReadable(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

static bool EnvFlag(const char *name, bool fallback)
{
    const char *v = SDL_getenv(name);

    if (v == NULL || v[0] == '\0')
        return fallback;
    return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

static void ReadSettingsFile(void)
{
    char path[1200];
    char line[256];
    FILE *f;

    snprintf(path, sizeof(path), "%s/settings.cfg", sCore.dir);
    f = fopen(path, "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq = strchr(line, '=');
        int value;

        if (line[0] == '#' || eq == NULL)
            continue;
        *eq = '\0';
        value = atoi(eq + 1);
        if (strcmp(line, "enabled") == 0)
            sCore.enabled = value != 0;
        else if (strcmp(line, "log") == 0)
            sCore.log = value != 0;
        else if (strcmp(line, "sync") == 0)
            sCore.sync = value != 0;
        else if (strcmp(line, "drawBudgetMs") == 0)
            sCore.drawBudgetMs = value > 0 ? value : 0;
    }
    fclose(f);
}

static void ReadSettings(void)
{
    const char *dir = SDL_getenv("PKMN_BATTLEFX_DIR");
    char probe[1200];

    if (sCore.settingsRead)
        return;
    sCore.settingsRead = true;
    sCore.enabled = true;
    sCore.log = false;
#ifdef BATTLEFX_TEST
    sCore.sync = true; /* the test driver compares the core's frames with the game's, frame for frame */
    sCore.drawBudgetMs = 0; /* the tests draw every animation, however slow the drawing (software OpenGL) */
#else
    sCore.sync = false;
    sCore.drawBudgetMs = BFX_DRAW_BUDGET_MS;
#endif
    if (dir != NULL && dir[0] != '\0')
        snprintf(sCore.dir, sizeof(sCore.dir), "%s", dir);
    else
    {
        char *base = SDL_GetBasePath();

        sCore.dir[0] = '\0';
        if (base != NULL)
        {
            snprintf(sCore.dir, sizeof(sCore.dir), "%sbattlefx", base);
            SDL_free(base);
            snprintf(probe, sizeof(probe), "%s/bfx_core.js", sCore.dir);
            if (!FileReadable(probe))
                sCore.dir[0] = '\0';
        }
        if (sCore.dir[0] == '\0')
            snprintf(sCore.dir, sizeof(sCore.dir), "battlefx");
    }
    ReadSettingsFile();
    /* The environment overrides the file. */
    sCore.enabled = EnvFlag("PKMN_BATTLEFX", sCore.enabled);
    sCore.log = EnvFlag("PKMN_BATTLEFX_LOG", sCore.log);
    sCore.sync = EnvFlag("PKMN_BATTLEFX_SYNC", sCore.sync);
    {
        const char *budget = SDL_getenv("PKMN_BATTLEFX_DRAW_BUDGET_MS");

        if (budget != NULL && budget[0] != '\0')
            sCore.drawBudgetMs = atoi(budget) > 0 ? atoi(budget) : 0;
    }
    if (!sCore.enabled)
        SDL_Log("Battle FX: off (the original GBA animations play)");
}

bool BattleFx_Enabled(void)
{
    ReadSettings();
    return sCore.enabled;
}

bool BattleFx_Drawing(void)
{
    return BattleFxGL_Showing();
}

bool BattleFx_Logging(void)
{
    ReadSettings();
    return sCore.log;
}

const char *BattleFx_DataDir(void)
{
    ReadSettings();
    return sCore.dir;
}

/* ── Loading the core (its own thread) ─────────────────────────────────── */

static char *ReadWholeFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *data = NULL;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        data = malloc((size_t)n + 1);
        if (data != NULL && fread(data, 1, (size_t)n, f) == (size_t)n)
        {
            data[n] = '\0';
            *size = (size_t)n;
        }
        else
        {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}

/* The game's own helper, evaluated in the core's realm next to it (the core is
 * the package's, unchanged): which GBA BG layers hold an effect picture that an
 * HD kit redraws - the same rule as the package's bridge (bridge.js
 * buildSprites: HD.BG_LAYERS[...].replace), so the game's picture leaves those
 * layers out while Battle FX draws (Surf's wave, Metal Sound's shine, the
 * sandstorm and fog, ...). A battler copied onto BG1 / BG2 (monbg) is never
 * left out. */
static const char kGameHelperJs[] =
    "globalThis.BFX_GAME_HIDDEN_BGS = function () {\n"
    "  const RT = globalThis.BFX_RT, HD = globalThis.BFX_HD; const W = RT && RT.world ? RT.world() : null;\n"
    "  if (!W || !W.animBg) return 0; let mask = 0;\n"
    "  for (const id of [1, 2]) {\n"
    "    const L = W.animBg[id]; if (!L || !L.on || L.mon >= 0) continue;\n"
    "    const key = L.gfx || L.tilemap; const lc = HD && HD.BG_LAYERS ? HD.BG_LAYERS[key] : null;\n"
    "    if (lc && lc.replace) mask |= 1 << id;\n"
    "  }\n"
    "  return mask;\n"
    "};\n"
    /* The GBA runtime's Random2 as the game's (random.c: SFC32, stream 29), from the game's own
     * gRng2Value when the animation began (the request's rng2): the effect sprites the core replays
     * land where the game's land. Each run of the animation (the plan, the playback) starts from the
     * same state. A request without rng2 keeps the core's own generator. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.API || !RT.Player || !RT.CB || !B) return;\n"
    "  const A = RT.API, legacy = A.random, P = RT.Player.prototype, reset = P.reset, step = P.step, begin = B.begin;\n"
    "  let s2 = null, s1 = null, perFrame = 0, rollout = null;\n"
    "  B.begin = function (req) {\n"
    "    const ok = (a) => Array.isArray(a) && a.length === 4;\n"
    "    s2 = req && ok(req.rng2) ? req.rng2.map((v) => v >>> 0) : null; s1 = req && ok(req.rng1) ? req.rng1.map((v) => v >>> 0) : null;\n"
    "    perFrame = req ? (req.rng1PerFrame | 0) : 0; rollout = req && req.rolloutTimer != null ? req.rolloutTimer | 0 : null;\n"
    "    return begin.call(this, req); };\n"
    "  P.reset = function () { reset.call(this); if (this.w) { this.w.gameRng2 = s2 ? Uint32Array.from(s2) : null;\n"
    "    this.w.gameRng1 = s1 ? Uint32Array.from(s1) : null; this.w.gameRng1PerFrame = perFrame; this.w.gameRollout = rollout; } };\n"
    /* gAnimDisableStructPtr->rolloutTimer (Rollout's, Ice Ball's counters) as the game's, not the runtime's stand-in */
    "  const dis = Object.getOwnPropertyDescriptor(A, 'gAnimDisableStructPtr');\n"
    "  if (dis && dis.get && dis.configurable) Object.defineProperty(A, 'gAnimDisableStructPtr', { configurable: true, enumerable: dis.enumerable,\n"
    "    get() { const w = RT.world(); const v = dis.get.call(A); if (w && w.gameRollout != null) v.rolloutTimer = w.gameRollout; return v; } });\n"
    "  function sfc(s, stream) {\n"
    "    const r = (s[0] + s[1] + s[3]) >>> 0;\n"
    "    s[3] = (s[3] + stream) >>> 0; s[0] = (s[1] ^ (s[1] >>> 9)) >>> 0; s[1] = Math.imul(s[2], 9) >>> 0;\n"
    "    s[2] = (r + ((s[2] << 21) | (s[2] >>> 11))) >>> 0;\n"
    "    return r;\n"
    "  }\n"
    /* Random2 (stream 29): only the animations draw from it in a battle */
    "  function random2() { const w = RT.world(), s = w && w.gameRng2; return s ? sfc(s, 29) >>> 16 : legacy(); }\n"
    /* Random (stream 1, the battle's): the vblank handlers advance it every frame too (main.c, battle_main.c
     * AdvanceRandom: the game's count a frame, measured, in rng1PerFrame); each step is a new frame */
    "  function random1() { const w = RT.world(), s = w && w.gameRng1; return s ? sfc(s, 1) >>> 16 : legacy(); }\n"
    "  P.step = function () { const s = this.w && this.w.gameRng1; if (s) for (let i = 0; i < this.w.gameRng1PerFrame; i++) sfc(s, 1);\n"
    "    return step.apply(this, arguments); };\n"
    "  A.random = random2; A.Random2 = random2; A.Random = random1;\n"
    /* the one animation callback that draws from Random (battle_anim_new.c SpriteCB_RandomCentredHits;
     * the runtime's port calls its generic random()) */
    "  const hits = RT.CB.SpriteCB_RandomCentredHits;\n"
    "  if (hits) RT.CB.SpriteCB_RandomCentredHits = function (sprite) { A.random = random1; try { return hits.call(this, sprite); } finally { A.random = random2; } };\n"
    "})();\n"
    /* battle_anim_electric.c AnimSparkElectricityFlashing: the blink test `angle % period == 0` with a period of
     * 0 (Spark's circling sparks) is the GBA's x % 0 (libgcc: 0 only for x = 0; the native build gives the same,
     * platform/fault_compat.c): such a spark blinks off when its angle comes round to 0. The runtime's port
     * never blinks it; the rest is the port's own. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.CB || !RT.API) return; const A = RT.API, spark = RT.CB.AnimSparkElectricityFlashing;\n"
    "  if (!spark) return;\n"
    "  RT.CB.AnimSparkElectricityFlashing = function (s) {\n"
    "    spark.call(this, s); if (s.data[4] !== 0 || !s.inUse) return; const step = s.callback;\n"
    "    if ((s.data[7] & 0xFF) === 0) s.invisible = !s.invisible;   /* the first step, run by the port's init */\n"
    "    s.callback = function (sp) { const before = sp.invisible; step(sp); if (sp.inUse && (sp.data[7] & 0xFF) === 0) sp.invisible = !before; };\n"
    "  };\n"
    "})();\n"
    /* palette.c BeginNormalPaletteFade: a negative delay speeds the fade up (deltaY += -delay), and deltaY is a
     * 4-bit field (struct PaletteFadeControl): delay -31 gives (2 + 31) & 15 = 1 a step (Spark's flashes), not 33 */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.API) return; const A = RT.API, begin = A.beginNormalPaletteFade; if (!begin) return;\n"
    "  A.beginNormalPaletteFade = function () { const r = begin.apply(this, arguments); const w = RT.world();\n"
    "    if (r && w && w.fade && w.fade.mode === 'normal') w.fade.delta &= 15; return r; };\n"
    "})();\n"
    /* The game's sound, which the scripts wait for, as the game has it: the core's runtime has no audio (its
     * stand-ins: any cry 40 frames, any sound effect 30). Replaying the game's animation, BFX_GAME_SOUND gives the
     * state at the start of the game frame the core's next step replays (bfx_game.c FeedSound: IsCryPlaying(), the
     * SE1 / SE2 music players' pause and track bits); the step's own starts (MPlayStart: the player's status 0 until
     * the vblank plays the song) and stops (m4aMPlayStop: paused), in their order, change it as m4a does, and
     * IsSEPlaying / IsCryPlaying (sound.c) read it. So waitsound (sound tasks, then sound effects for up to 90
     * frames, then stopped: Cmd_waitsound), the script's end (Cmd_end: its tasks, then up to 90 frames of sound
     * effects; not leftover sprites) and the cry waits are the game's, frame for frame. While the core plans an
     * animation at begin, and in the Lab, it estimates from battlefx/se_frames.json (each sound effect's length and
     * music player, measured in the game: see bfx_test.c). A sound effect a script names by number (Mystical
     * Fire's `playse 177`) is that number's; stopsound (Cmd_stopsound), which the runtime has no case for, stops
     * SE1 and SE2. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.Player || !RT.API || !B) return;\n"
    "  const P = RT.Player.prototype, A = RT.API, syncReady = P.syncReady, done = P.done, fire = P.fireSound, begin = B.begin;\n"
    "  const ownSE = A.isSEPlaying, ownCry = A.isCryPlaying, stop = A.m4aMPlayStop;\n"
    "  let SE = null, game = null, planning = false;\n"
    "  globalThis.BFX_GAME_SET_SE = function (table) { SE = table && typeof table === 'object' ? table : null; return SE ? 1 : 0; };\n"
    "  globalThis.BFX_GAME_SOUND = function (v) { game = v | 0; return 1; };\n"
    "  B.begin = function (req) { planning = true; try { return begin.call(this, req); } finally { planning = false; } };\n"
    "  const live = () => !planning && game != null;\n"
    "  function byNumber(se) { const D = RT.data; return typeof se === 'string' && /^[0-9]+$/.test(se) && D && D.seNames ? D.seNames[se] : null; }\n"
    "  function entry(se) { return SE ? (SE[se] || SE[byNumber(se)] || null) : null; }\n"
    "  function stopAll(w) { if (!w || !w.sfx) return; w.seStoppedAt = w.frame + 1; (w.seStops || (w.seStops = [])).push({ f: w.frame, at: w.sfx.length }); }\n"
    "  function estimate(w, f) {\n"
    "    if (!w || !w.sfx || !SE) return false; let s1 = null, s2 = null; const from = w.seStoppedAt != null ? w.seStoppedAt : -1;\n"
    "    for (const x of w.sfx) { if (x.f < from || x.f > f) continue; const t = entry(x.se); if (!t) continue; if (t[1] === 1) s1 = [x.f, t[0]]; else if (t[1] === 2) s2 = [x.f, t[0]]; }\n"
    "    return !!((s1 && f > s1[0] && f - s1[0] <= s1[1]) || (s2 && f > s2[0] && f - s2[0] <= s2[1]));\n"
    "  }\n"
    "  function gamePlaying(w, f) {\n"
    "    let s = game >> 1; let i = w.sfx.length; while (i > 0 && w.sfx[i - 1].f >= f) i--;\n"
    "    const stops = w.seStops || []; let k = 0; while (k < stops.length && stops[k].f < f) k++;\n"
    "    for (let j = i; ; j++) {\n"
    "      while (k < stops.length && stops[k].f === f && stops[k].at <= j) { s |= 5; k++; }\n"
    "      if (j >= w.sfx.length) break;\n"
    "      if (w.sfx[j].f !== f) continue;\n"
    "      const t = entry(w.sfx[j].se); if (!t) continue;\n"
    "      if (t[1] === 1) s &= ~3; else if (t[1] === 2) s &= ~12;\n"
    "    }\n"
    "    if ((s & 1) && (s & 4)) return false;\n"
    "    return (s & 2) !== 0 || (s & 8) !== 0;\n"
    "  }\n"
    "  function playing(w, f) { return live() ? gamePlaying(w, f) : estimate(w, f); }\n"
    "  function leftover(w) { for (let i = 0; i < 64; i++) { const sp = w.sprites[i], g = sp && sp.inUse ? sp.tag | 0 : 0;\n"
    "    if ((g >= 10000 && g < 20000) || (g >= 55000 && g <= 55027) || (g >= 65030 && g <= 65057)) return true; } return false; }\n"
    "  P.syncReady = function (op) {\n"
    "    if (op !== 'waitsound' || (!SE && !live())) return syncReady.call(this, op);\n"
    "    const w = this.w; if (w.soundCount > 0) { this.seWait = 0; return false; }\n"
    "    if (!playing(w, this.frame)) { this.seWait = 0; return true; }\n"
    "    if ((this.seWait = (this.seWait | 0) + 1) > 90) { stopAll(w); this.seWait = 0; return true; }\n"
    "    return false;\n"
    "  };\n"
    "  P.done = function () {\n"
    "    const w = this.w;\n"
    "    if (!live()) { const d = done.call(this); if (!d || !SE) return d; return !(estimate(w, this.frame) && (this.seEndWait = (this.seEndWait | 0) + 1) <= 90); }\n"
    "    if (!this.ended) return false;\n"
    "    if (w.visualCount > 0 || w.soundCount > 0 || w.bg.monbgTask[0] !== 0xFF || w.bg.monbgTask[1] !== 0xFF) { this.seEndWait = 0; return false; }\n"
    "    if (playing(w, this.frame) && (this.seEndWait = (this.seEndWait | 0) + 1) <= 90) return false;\n"
    /* the script is over; effect sprites it left moving (a caught ball's stars) go on as the game's do (bfx_game.c:
     * the tail), for up to 120 frames */
    "    if (leftover(w) && (this.leftWait = (this.leftWait | 0) + 1) <= 120) return false;\n"
    "    return true;\n"
    "  };\n"
    "  A.isSEPlaying = function () { const w = RT.world(); return (SE || live()) && w && w.sfx ? playing(w, w.frame) : ownSE.apply(this, arguments); };\n"
    "  A.m4aMPlayStop = function () { stopAll(RT.world()); return stop.apply(this, arguments); };\n"
    "  P.fireSound = function (e) { if (e && e[2] === 'stopsound') stopAll(this.w); return fire.apply(this, arguments); };\n"
    "  A.isCryPlaying = function () { const w = RT.world(); if (!live() || !w) return ownCry.apply(this, arguments); return w.cryTouched === w.frame ? false : (game & 1) === 1; };\n"
    "  for (const k of ['playCry', 'playCry_ByMode', 'playCry_DuckNoRestore', 'stopCryAndClearCrySongs']) {\n"
    "    const f = A[k]; if (typeof f !== 'function') continue;\n"
    "    A[k] = function () { const r = f.apply(this, arguments); const w = RT.world(); if (w) w.cryTouched = w.frame; return r; };\n"
    "  }\n"
    "})();\n"
    /* Whether the core's world has a Poke Ball sprite (a send-out's ball made). */
    "globalThis.BFX_GAME_HAS_BALL = function () {\n"
    "  for (const r of globalThis.BFX_RT.snapshot()) { const g = r.tag | 0; if (g >= 55000 && g <= 55027) return 1; }\n"
    "  return 0;\n"
    "};\n"
    /* panse, panse_adjustnone, panse_adjustall (battle_anim.c) run a sound task that steps the pan to its target,
     * counted in gAnimSoundTaskCount: waitsound and the script's end wait for it (Bestow's end, 64 frames after its
     * panse). The runtime records only the sound; with the data's pan step and delay (tools/battlefx/convert_data.py)
     * the core runs the task as Task_PanFromInitialToTarget does. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.Player || !RT.API) return; const P = RT.Player.prototype, A = RT.API, fire = P.fireSound;\n"
    "  if (!fire || !A.createTask || !A.destroyAnimSoundTask) return;\n"
    "  const s8 = (v) => (v << 24) >> 24;\n"
    "  function tok(v) { if (typeof v === 'number') return v | 0; const t = String(v == null ? '' : v).trim();\n"
    "    if (/^[-+]?[0-9]+$/.test(t)) return parseInt(t, 10); if (t === 'SOUND_PAN_ATTACKER') return -64; if (t === 'SOUND_PAN_TARGET') return 63; return 0; }\n"
    "  function pan(id) { const w = RT.world(), d = w.tasks[id].data;\n"
    "    if (d[8]++ >= d[3]) { d[8] = 0; d[4] = d[4] + d[2]; const p = d[4];\n"
    "      if (d[2] === 0 || (d[0] < d[1] ? p >= d[1] : p <= d[1])) A.destroyAnimSoundTask(id); } }\n"
    "  P.fireSound = function (e) {\n"
    "    const r = fire.apply(this, arguments); const op = e[2], x = e[5];\n"
    "    if (!x || x.pan_step == null || (op !== 'panse' && op !== 'panse_adjustnone' && op !== 'panse_adjustall')) return r;\n"
    "    let from = s8(tok(x.pan_from)), to = s8(tok(x.pan_to)), inc = s8(tok(x.pan_step)); const wait = tok(x.pan_delay) & 0xFF;\n"
    "    if (op === 'panse') { from = s8(A.battleAnimAdjustPanning(from)); to = s8(A.battleAnimAdjustPanning(to)); inc = A.calculatePanIncrement(from, to, inc); }\n"
    "    else if (op === 'panse_adjustall') { from = s8(A.battleAnimAdjustPanning2(from)); to = s8(A.battleAnimAdjustPanning2(to)); inc = s8(A.battleAnimAdjustPanning2(inc)); }\n"
    "    const w = RT.world(); const id = A.createTask(pan, 1); if (id == null || id === 0xFF || !w.tasks[id]) return r;\n"
    "    const t = w.tasks[id]; t.name = 'Task_PanFromInitialToTarget'; t.sound = true;\n"
    "    t.data[0] = from; t.data[1] = to; t.data[2] = inc; t.data[3] = wait; t.data[4] = from; w.soundCount++;\n"
    "    return r; };\n"
    "})();\n"
    /* The game's sprite slots: the battle's own sprites besides effect sprites (the request's spritesInUse) take
     * slots of the 64, so an animation that fills the rest (Fairy Lock's bolts) has an effect sprite not made in the
     * game. The core's world keeps as many slots taken (inert, never drawn, not counted as the animation's), from
     * the top: its battlers stay at 0-3. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.Player || !B) return;\n"
    "  const P = RT.Player.prototype, reset = P.reset, active = P.activeSprites, begin = B.begin; let taken = null;\n"
    "  let placed = null;\n"
    "  B.begin = function (req) { taken = req && req.spritesInUse != null ? req.spritesInUse | 0 : null;\n"
    "    placed = req && Array.isArray(req.battlerSprites) ? req.battlerSprites : null; return begin.call(this, req); };\n"
    "  function effect(g) { const D = RT.data; let top = 10000; if (D && D.tagIds) for (const k in D.tagIds) { const v = D.tagIds[k] | 0; if (v < 20000 && v > top) top = v; }\n"
    "    return (g >= 10000 && g <= top) || (g >= 55000 && g <= 55027) || (g >= 65030 && g <= 65057); }\n"
    "  P.reset = function () { reset.apply(this, arguments); const w = this.w; if (!w || !w.sprites[0]) return;\n"
    "    if (placed) for (let b = 0; b < 4; b++) { const g = placed[b], s = w.sprites[b]; if (!Array.isArray(g) || !s || s.kind !== 'battler') continue;\n"
    "      s.x = g[0] | 0; s.y = g[1] | 0; s.x2 = g[2] | 0; s.y2 = g[3] | 0; s.invisible = !!g[4]; }\n"
    "    if (taken == null) return;\n"
    "    const Sprite = w.sprites[0].constructor; let n = 0;\n"
    "    for (let i = 0; i < 64; i++) { const s = w.sprites[i]; if (s && s.inUse && !effect(s.tag | 0)) n++; }\n"
    "    for (let i = 63, k = taken - n; i >= 0 && k > 0; i--) { if (w.sprites[i] && w.sprites[i].inUse) continue;\n"
    "      const s = new Sprite(); s.id = i; s.uid = -1 - i; s.kind = 'reserved'; s.invisible = true; w.sprites[i] = s; k--; } };\n"
    "  P.activeSprites = function () { let n = active.apply(this, arguments); const w = this.w;\n"
    "    if (w) for (let i = 0; i < 64; i++) { const s = w.sprites[i]; if (s && s.inUse && s.kind === 'reserved') n--; } return n; };\n"
    "})();\n"
    /* The renderer's ribbon packing (HD.GPU.packRibbons, called by BFX_BRIDGE.frame) as the package has it, with the
     * vector arithmetic written out - QuickJS interprets each call, and lightning (HD.boltRibbons: glow, beam and core
     * strips over the same points) spent most of its frame packets there - and the strips that share their points
     * built from them once (a point's side and its place along the strip depend only on the points and the camera).
     * Lengths by Math.sqrt (the package: Math.hypot). The packets are the same, value for value (every value of 10
     * moves' every frame compared; the bolt moves' frame packets about 2.2 times faster in QuickJS: tools/battlefx/bfx_perf).
     * A candidate for the package (v3.4). */
    "(function () {\n"
    "  const HD = globalThis.BFX_HD; if (!HD || !HD.GPU || !HD.GPU.packRibbons || !HD.ribRow) return;\n"
    "  const RIB_EPS = 1e-6, sqrt = Math.sqrt, min = Math.min, max = Math.max;\n"
    "  let cap = 0, SX = null, SY = null, SZ = null, U = null;\n"
    "  function grow(n) { if (n <= cap) return; cap = max(n, cap * 2, 256); SX = new Float64Array(cap); SY = new Float64Array(cap); SZ = new Float64Array(cap); U = new Float64Array(cap); }\n"
    "  HD.GPU.packRibbons = function (list, camPos, B, capV) {\n"
    "    let nv = 0, ni = 0, last = null, lastTotal = 0;\n"
    "    const P = B.pos, UV = B.uv, CO = B.col, RW = B.row, IX = B.idx, IXN = IX.length;\n"
    "    const cx = camPos[0], cy = camPos[1], cz = camPos[2];\n"
    "    for (let q = 0; q < list.length; q++) {\n"
    "      const r = list[q]; const pts = r.pts; const n = pts.length;\n"
    "      if (!n || n < 2 || nv + n * 2 > capV || ni + (n - 1) * 6 > IXN) continue;\n"
    "      let total;\n"
    "      if (pts !== last) {\n"
    "        last = null;\n"
    "        total = 0;\n"
    "        for (let i = 1; i < n; i++) { const a = pts[i - 1], b = pts[i]; const x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; total += sqrt(x * x + y * y + z * z); }\n"
    "        if (total < RIB_EPS) continue;\n"
    "        let k0 = -1, tgx = 0, tgy = 0, tgz = 0;\n"
    "        for (let i = 0; i < n && k0 < 0; i++) {\n"
    "          const a = pts[min(n - 1, i + 1)], b = pts[max(0, i - 1)]; const x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];\n"
    "          const l = sqrt(x * x + y * y + z * z); if (l >= RIB_EPS) { k0 = i; tgx = x / l; tgy = y / l; tgz = z / l; }\n"
    "        }\n"
    "        if (k0 < 0) continue;\n"
    "        grow(n);\n"
    "        let haveS = false, sgx = 0, sgy = 0, sgz = 0, acc = 0;\n"
    "        for (let i = 0; i < n; i++) {\n"
    "          const p = pts[i], px = p[0], py = p[1], pz = p[2];\n"
    "          const pa = pts[min(n - 1, i + 1)], pb = pts[max(0, i - 1)];\n"
    "          let tx = pa[0] - pb[0], ty = pa[1] - pb[1], tz = pa[2] - pb[2];\n"
    "          const tl = sqrt(tx * tx + ty * ty + tz * tz);\n"
    "          if (tl >= RIB_EPS) { tx /= tl; ty /= tl; tz /= tl; tgx = tx; tgy = ty; tgz = tz; } else { tx = tgx; ty = tgy; tz = tgz; }\n"
    "          let vx = cx - px, vy = cy - py, vz = cz - pz; const vl = sqrt(vx * vx + vy * vy + vz * vz) || 1; vx /= vl; vy /= vl; vz /= vl;\n"
    "          let sx = ty * vz - tz * vy, sy = tz * vx - tx * vz, sz = tx * vy - ty * vx;\n"
    "          const sl = sqrt(sx * sx + sy * sy + sz * sz);\n"
    "          if (sl >= RIB_EPS || !haveS) { const l = sl || 1; sx /= l; sy /= l; sz /= l; sgx = sx; sgy = sy; sgz = sz; haveS = true; } else { sx = sgx; sy = sgy; sz = sgz; }\n"
    "          if (i > 0) { const a = pts[i - 1]; const x = a[0] - px, y = a[1] - py, z = a[2] - pz; acc += sqrt(x * x + y * y + z * z); }\n"
    "          SX[i] = sx; SY[i] = sy; SZ[i] = sz; U[i] = acc / total;\n"
    "        }\n"
    "        last = pts; lastTotal = total;\n"
    "      } else\n"
    "        total = lastTotal;\n"
    "      const row = HD.ribRow(r.cell || 'beam'); const it = r.intensity != null ? r.intensity : 1; const col = r.color || [1, 1, 1];\n"
    "      const al = r.alpha != null ? r.alpha : 1; const add = r.add != null ? r.add : 1;\n"
    "      const rep = r.uvRepeat || (total / max(0.05, r.width * 4)); const base = nv;\n"
    "      const c0 = col[0] * it, c1 = col[1] * it, c2 = col[2] * it;\n"
    "      const halfW = r.width * 0.5, taper = r.taper, widths = r.widths, fadeEnds = r.fadeEnds, scroll = r.scroll || 0;\n"
    "      for (let i = 0; i < n; i++) {\n"
    "        const p = pts[i], px = p[0], py = p[1], pz = p[2], sx = SX[i], sy = SY[i], sz = SZ[i], u = U[i];\n"
    "        let w = halfW; if (taper) w *= min(1, min(u, 1 - u) / taper + 0.05); if (widths) w = widths[i] * 0.5;\n"
    "        const o = nv * 3; P[o] = px + sx * w; P[o + 1] = py + sy * w; P[o + 2] = pz + sz * w; P[o + 3] = px - sx * w; P[o + 4] = py - sy * w; P[o + 5] = pz - sz * w;\n"
    "        const uu = u * rep - scroll; const ou = nv * 2; UV[ou] = uu; UV[ou + 1] = 0; UV[ou + 2] = uu; UV[ou + 3] = 1;\n"
    "        const a4 = al * (fadeEnds ? min(1, min(u, 1 - u) * fadeEnds) : 1);\n"
    "        const oc = nv * 4; CO[oc] = c0; CO[oc + 1] = c1; CO[oc + 2] = c2; CO[oc + 3] = a4; CO[oc + 4] = c0; CO[oc + 5] = c1; CO[oc + 6] = c2; CO[oc + 7] = a4;\n"
    "        const orr = nv * 2; RW[orr] = row; RW[orr + 1] = add; RW[orr + 2] = row; RW[orr + 3] = add;\n"
    "        nv += 2;\n"
    "      }\n"
    "      for (let i = 0; i < n - 1; i++) { const v0 = base + i * 2; IX[ni++] = v0; IX[ni++] = v0 + 1; IX[ni++] = v0 + 2; IX[ni++] = v0 + 1; IX[ni++] = v0 + 3; IX[ni++] = v0 + 2; }\n"
    "    }\n"
    "    return { nv, ni };\n"
    "  };\n"
    "})();\n"
    /* battle_anim_utility_funcs.c AnimTask_SetAnimTargetToBattlerTarget: gBattleAnimTarget = gBattlerTarget (the
     * request's battlerTarget; the battle engine does not change it while an animation plays). The runtime takes the
     * task for one that only computes a branch's value (LOGIC_TASKS: run at once, as the game's destroys itself at
     * once) and left the target as it was. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.LOGIC_TASKS || !RT.API || !B) return;\n"
    "  const A = RT.API, begin = B.begin; let target = null;\n"
    "  B.begin = function (req) { target = req && req.battlerTarget != null ? req.battlerTarget | 0 : null; return begin.call(this, req); };\n"
    "  RT.LOGIC_TASKS.AnimTask_SetAnimTargetToBattlerTarget = function () { if (target != null && A.W) A.gBattleAnimTarget = target; return null; };\n"
    "})();\n"
    /* The battle's attacker. Expansion's AnimSuperpowerOrb and AnimSuperpowerFireball (battle_anim_fight.c) start at
     * gBattlerAttacker, the battle's attacker, and AnimDragonDanceOrb (battle_anim_dragon.c) takes its radius from it,
     * where the package's ports read the animation's attacker: the same battler in a move's animation (DoMoveAnim) and
     * when a gimmick plays its animation (TryActivateGimmick sets both), not when another battler plays one. The
     * request's battlerAttacker is gBattlerAttacker at the launch (the engine does not change it while an animation
     * plays); a sprite made as the game makes it is moved to where the game's is. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.CB || !RT.API || !RT.C || !B) return;\n"
    "  const A = RT.API, CB = RT.CB, C = RT.C, begin = B.begin; let atk = null;\n"
    "  B.begin = function (req) { atk = req && req.battlerAttacker != null ? req.battlerAttacker | 0 : null; return begin.call(this, req); };\n"
    "  const other = () => atk != null && A.W != null && atk !== A.gBattleAnimAttacker;\n"
    "  const orb = CB.AnimSuperpowerOrb, fireball = CB.AnimSuperpowerFireball, dance = CB.AnimDragonDanceOrb;\n"
    "  if (orb) CB.AnimSuperpowerOrb = function (sprite) {\n"
    "    const fromAttacker = A.gBattleAnimArgs[0] === C.ANIM_ATTACKER; orb.call(this, sprite);\n"
    "    if (!fromAttacker || !other()) return;\n"
    "    sprite.x = A.getBattlerSpriteCoord(atk, C.BATTLER_COORD_X_2); sprite.y = A.getBattlerSpriteCoord(atk, C.BATTLER_COORD_Y_PIC_OFFSET); };\n"
    "  if (fireball) CB.AnimSuperpowerFireball = function (sprite) {\n"
    "    const fromAttacker = A.gBattleAnimArgs[0] === C.ANIM_ATTACKER; fireball.call(this, sprite);\n"
    "    if (!fromAttacker || !other()) return;\n"
    "    const b = A.gBattleAnimTarget;\n"
    "    sprite.x = A.getBattlerSpriteCoord(atk, C.BATTLER_COORD_X_2); sprite.y = A.getBattlerSpriteCoord(atk, C.BATTLER_COORD_Y_PIC_OFFSET);\n"
    "    sprite.data[0] = 16; sprite.data[1] = sprite.x; sprite.data[2] = A.getBattlerSpriteCoord(b, C.BATTLER_COORD_X_2);\n"
    "    sprite.data[3] = sprite.y; sprite.data[4] = A.getBattlerSpriteCoord(b, C.BATTLER_COORD_Y_PIC_OFFSET);\n"
    "    A.initAnimLinearTranslation(sprite); };\n"
    "  if (dance) CB.AnimDragonDanceOrb = function (sprite) {\n"
    "    dance.call(this, sprite); if (!other()) return;\n"
    "    const h = A.getBattlerSpriteCoordAttr(atk, C.BATTLER_COORD_ATTR_HEIGHT) & 0xFFFF, w = A.getBattlerSpriteCoordAttr(atk, C.BATTLER_COORD_ATTR_WIDTH) & 0xFFFF;\n"
    "    sprite.data[7] = h > w ? (h / 2) | 0 : (w / 2) | 0;\n"
    "    sprite.x2 = A.Cos(sprite.data[6], sprite.data[7]); sprite.y2 = A.Sin(sprite.data[6], sprite.data[7]); };\n"
    "})();\n"
    /* The trainer's throwing frames: the request's trainerAnims (the back picture's own rest and throw, read from
     * the game's sprite) in place of the package's Hoenn frames, set up after the package's Safari setup. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.onWorldSetup || !RT.C) return;\n"
    "  const C = RT.C;\n"
    "  RT.onWorldSetup(function (w, o) {\n"
    "    const g = (o && o.general) || {}; if (!g.safari || !Array.isArray(g.trainerAnims) || g.trainerAnims.length < 2) return;\n"
    "    const s = w.sprites[C.B_POSITION_PLAYER_LEFT]; if (s) s.anims = g.trainerAnims; });\n"
    "})();\n"
    /* The catch tutorial's throw (battle_anim_throw.c AnimTask_ThrowBall_StandingTrainer): Wally's ball starts at
     * (32 + 32, 11 | 80), the Safari Zone's at (23 + 32, 5 | 80) - the package's port has the Safari Zone's. The
     * request's catchTutorial is BATTLE_TYPE_CATCH_TUTORIAL; the ball (the task's data[0]) has not moved yet. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.TK || !RT.API) return;\n"
    "  const A = RT.API, TK = RT.TK, standing = TK.AnimTask_ThrowBall_StandingTrainer; if (!standing) return;\n"
    "  TK.AnimTask_ThrowBall_StandingTrainer = function (taskId) {\n"
    "    standing.call(this, taskId);\n"
    "    const g = A.general(); if (!g || !g.catchTutorial) return;\n"
    "    const t = A.gTasks[taskId], s = t ? A.gSprites[t.data[0]] : null; if (s && s.inUse) { s.x = 32 + 32; s.y = 11 | 80; } };\n"
    "})();\n"
    /* The tasks' priorities. The game runs its tasks in priority order (task.c RunTasks; equal priorities in the order
     * made): createvisualtask's priority is the script's (the data's task rows carry it as their 8th field,
     * tools/battlefx/convert_data.py), createsoundtask's is 1 (battle_anim.c Cmd_createsoundtask). The runtime makes
     * every one with priority 2: a script's tasks run here in the game's order instead (a priority-10 palette blend
     * after the task that runs a battle animation's script, in the same frame, as in the game). */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT; if (!RT || !RT.Player || !RT.EV || !RT.API) return;\n"
    "  const P = RT.Player.prototype, EV = RT.EV, A = RT.API, fire = P.fire, startTask = P.startTask, fireSound = P.fireSound;\n"
    "  if (!fire || !startTask || !fireSound) return;\n"
    "  const HEAD = 0xFE, TAIL = 0xFF;\n"
    "  P.fire = function (e) { const before = this.taskPrio; this.taskPrio = e && e[1] === EV.TASK && typeof e[7] === 'number' ? e[7] : null;\n"
    "    try { return fire.apply(this, arguments); } finally { this.taskPrio = before; } };\n"
    "  P.startTask = function (name, prio, est, sound) { return startTask.call(this, name, this.taskPrio != null ? this.taskPrio : prio, est, sound); };\n"
    /* a task already in the list given another priority: taken out and put in again as InsertTask puts a new one */
    "  function relink(T, id, prio) {\n"
    "    const t = T[id]; if (!t || !t.isActive || t.priority === prio) return;\n"
    "    if (t.prev === HEAD) { if (t.next !== TAIL) T[t.next].prev = HEAD; } else if (t.next === TAIL) T[t.prev].next = TAIL;\n"
    "    else { T[t.prev].next = t.next; T[t.next].prev = t.prev; }\n"
    "    t.priority = prio; t.prev = HEAD; t.next = TAIL;\n"
    "    let cur = -1; for (let i = 0; i < T.length; i++) { const u = T[i]; if (u && u.isActive && i !== id && u.prev === HEAD) { cur = i; break; } }\n"
    "    if (cur < 0) return;\n"
    "    for (;;) { const u = T[cur];\n"
    "      if (prio < u.priority) { t.prev = u.prev; t.next = cur; if (u.prev !== HEAD) T[u.prev].next = id; u.prev = id; return; }\n"
    "      if (u.next === TAIL) { t.prev = cur; t.next = TAIL; u.next = id; return; }\n"
    "      cur = u.next; } }\n"
    "  P.fireSound = function (e) {\n"
    "    if (!e || e[2] !== 'createsoundtask') return fireSound.apply(this, arguments);\n"
    "    const T = A.gTasks, had = T.map((t) => !!(t && t.isActive)); const r = fireSound.apply(this, arguments);\n"
    "    for (let i = 0; i < T.length; i++) if (T[i] && T[i].isActive && !had[i] && T[i].sound) relink(T, i, 1);\n"
    "    return r; };\n"
    "})();\n"
    /* Which loop runs the script. A move's animation is run by its battler's controller (Controller_DoMoveAnimation,
     * in BattleMainCB1): each frame the script first, then AnimateSprites, BuildOamBuffer, UpdatePaletteFade and
     * RunTasks (BattleMainCB2) - the runtime's own order. A status, battle or special animation (LaunchStatusAnimation,
     * TryHandleLaunchBattleTableAnimation, InitAndLaunchSpecialAnimation) is run by a task of priority 10 made at its
     * launch: its script runs inside RunTasks, after AnimateSprites and BuildOamBuffer and after the tasks of a lower
     * priority number. A sprite it makes moves, and shows, from the next frame; a task under priority 10 it makes runs
     * from the next frame; its waits see this frame's sprites and those tasks already run. The request's order "task"
     * runs the core's script the same way: from a task of priority 10 made as the animation begins. */
    "(function () {\n"
    "  const RT = globalThis.BFX_RT, B = globalThis.BFX_BRIDGE; if (!RT || !RT.Player || !RT.API || !B) return;\n"
    "  const P = RT.Player.prototype, A = RT.API, reset = P.reset, runScript = P.runScript, begin = B.begin;\n"
    "  if (!runScript || !A.createTask || !A.destroyTask) return;\n"
    "  let order = '';\n"
    "  B.begin = function (req) { order = req && req.order === 'task' ? 'task' : ''; return begin.call(this, req); };\n"
    "  P.reset = function () {\n"
    "    reset.call(this); this.taskOrder = order === 'task'; this.inDriver = false;\n"
    "    if (!this.taskOrder || !this.w) return;\n"
    "    const player = this;\n"
    "    A.createTask(function Task_RunAnimScript(taskId) {\n"
    "      if (!player.scriptDone) { player.inDriver = true; try { runScript.call(player); } finally { player.inDriver = false; } }\n"
    "      if (player.scriptDone) A.destroyTask(taskId);\n"
    "    }, 10);\n"
    "  };\n"
    "  P.runScript = function () { if (this.taskOrder && !this.inDriver) return; return runScript.apply(this, arguments); };\n"
    "})();\n"
    /* A move's script branches in the core's data, in its order: "kind|task|arg|value;..." */
    "globalThis.BFX_GAME_VARIANTS = function (id) {\n"
    "  const D = globalThis.BFX_RT && globalThis.BFX_RT.data; if (!D) return '';\n"
    "  const m = (D.moves || []).find((x) => x.id === id); if (!m || !m.var) return '';\n"
    "  return m.var.map((v) => [v.kind, v.task || '', v.arg == null ? -1 : v.arg, v.value == null ? 0 : v.value].join('|')).join(';');\n"
    "};\n";

static void LoadVariants(struct BfxHost *host);
static bool CoreInit(void);
static void CoreLoop(struct BfxHost *host);

static int LoadCore(void *unused)
{
    char path[1200];
    char *core, *data;
    size_t coreSize = 0, dataSize = 0;
    struct BfxHost *host;
    Uint64 t0 = SDL_GetPerformanceCounter();

    (void)unused;
    snprintf(path, sizeof(path), "%s/bfx_core.js", sCore.dir);
    core = ReadWholeFile(path, &coreSize);
    snprintf(path, sizeof(path), "%s/bfx_moves.json", sCore.dir);
    data = ReadWholeFile(path, &dataSize);
    if (core == NULL || data == NULL)
    {
        SDL_Log("Battle FX: cannot read %s/bfx_core.js and bfx_moves.json; the original GBA animations play",
                sCore.dir);
        free(core);
        free(data);
        SDL_AtomicSet(&sCore.state, CORE_FAILED);
        return 0;
    }
    host = BfxHost_Create();
    if (host == NULL || !BfxHost_Init(host, core, coreSize, data, dataSize, JS_STACK_BYTES)
     || !BfxHost_Settings(host, "{\"camera\":\"off\",\"palettes\":false,\"overlays\":false}")
     || !BfxHost_Eval(host, kGameHelperJs, sizeof(kGameHelperJs) - 1, "battle_fx_game.js"))
    {
        SDL_Log("Battle FX: the core did not start (%s); the original GBA animations play",
                host ? BfxHost_LastError(host) : "out of memory");
        BfxHost_Destroy(host);
        free(core);
        free(data);
        SDL_AtomicSet(&sCore.state, CORE_FAILED);
        return 0;
    }
    free(core);
    free(data);
    /* the game's sound effect lengths (optional) */
    snprintf(path, sizeof(path), "%s/se_frames.json", sCore.dir);
    {
        size_t seSize = 0;
        char *se = ReadWholeFile(path, &seSize);
        if (se != NULL)
        {
            char *r = BfxHost_CallGlobal(host, "BFX_GAME_SET_SE", se);
            if (r == NULL || strcmp(r, "1") != 0)
                SDL_Log("Battle FX: %s could not be read; the core keeps its own sound timing", path);
            free(r);
            free(se);
        }
    }
    LoadVariants(host);
    sCore.host = host;
    sCore.loadMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    SDL_Log("Battle FX: core ready in %.0f ms (%s, %u + %u bytes%s)", sCore.loadMs, sCore.dir,
            (unsigned)coreSize, (unsigned)dataSize, sCore.sync ? "; the game waits for it every frame" : "");
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&sCore.state, CORE_READY);
    CoreLoop(host); /* this thread runs the core from now on */
    return 0;
}

static void StartLoading(void)
{
    ReadSettings();
    if (!sCore.enabled || SDL_AtomicGet(&sCore.state) != CORE_IDLE)
        return;
    if (!CoreInit())
    {
        SDL_Log("Battle FX: no lock for the core's thread (%s); the original GBA animations play", SDL_GetError());
        SDL_AtomicSet(&sCore.state, CORE_FAILED);
        return;
    }
    SDL_AtomicSet(&sCore.state, CORE_LOADING);
    sCore.loader = SDL_CreateThreadWithStackSize(LoadCore, "BattleFxCore", LOADER_STACK_BYTES, NULL);
    if (sCore.loader == NULL)
    {
        SDL_Log("Battle FX: no thread to load the core on (%s); the original GBA animations play", SDL_GetError());
        SDL_AtomicSet(&sCore.state, CORE_FAILED);
        return;
    }
    SDL_DetachThread(sCore.loader);
    sCore.loader = NULL;
}

static struct BfxHost *ReadyHost(void)
{
    if (SDL_AtomicGet(&sCore.state) != CORE_READY)
        return NULL;
    SDL_MemoryBarrierAcquire();
    return sCore.host;
}

/* ── Requests ─────────────────────────────────────────────────────────── */

static bool Refused(const char *id)
{
    for (int i = 0; i < sRefusedCount; i++)
        if (strcmp(sRefused[i], id) == 0)
            return true;
    return false;
}

static void NoteRefused(const char *id, const char *why)
{
    sFailures++;
    if (Refused(id))
        return;
    if (sRefusedCount < MAX_REFUSED)
        snprintf(sRefused[sRefusedCount++], sizeof(sRefused[0]), "%s", id);
    SDL_Log("Battle FX: %s plays as on the GBA (%s)", id, why);
}

/* The species a battler's sprite shows, as GetBattlerSpriteCoord reads it. */
static u16 BattlerSpecies(u32 battler)
{
    struct Pokemon *mon = GetBattlerMon(battler);
    struct Pokemon *illusion = GetIllusionMonPtr(battler);

    if (gBattleSpritesDataPtr != NULL && gBattleSpritesDataPtr->battlerData[battler].transformSpecies)
        return gBattleSpritesDataPtr->battlerData[battler].transformSpecies;
    if (illusion != NULL)
        mon = illusion;
    return GetMonData(mon, MON_DATA_SPECIES);
}

static int Append(char *buf, int len, int size, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (len >= size)
        return len;
    va_start(ap, fmt);
    n = vsnprintf(buf + len, (size_t)(size - len), fmt, ap);
    va_end(ap);
    return n < 0 ? len : len + n;
}

/* Random's numbers taken a frame, outside the animations (the vblank handlers' AdvanceRandom): the
 * counter of gRngValue at each frame's end, read only. */
#define RNG1_WINDOW 8
static u32 sRng1LastCtr;
static u8 sRng1Deltas[RNG1_WINDOW];
static unsigned sRng1Frames;

static void NoteRng1(void)
{
    u32 d = gRngValue.ctr - sRng1LastCtr;
    sRng1LastCtr = gRngValue.ctr;
    sRng1Deltas[sRng1Frames++ % RNG1_WINDOW] = (u8)(d > 255 ? 255 : d);
}

static u32 Rng1PerFrame(void)
{
    u32 n = sRng1Frames < RNG1_WINDOW ? sRng1Frames : RNG1_WINDOW, least = 2;
    for (u32 i = 0; i < n; i++)
        if (i == 0 || sRng1Deltas[i] < least)
            least = sRng1Deltas[i];
    return n ? least : 2;
}

/* The request the core needs to replay the game's own animation: the battlers
 * as the scripts see them (who is present, each sprite's picture metrics) and
 * what the battle engine handed the animation. */
static int AppendSetup(char *buf, int len, int size, u32 attacker, u32 target)
{
    bool doubles = IsDoubleBattle();

    len = Append(buf, len, size, ",\"attacker\":%u,\"target\":%u,\"double\":%s,\"present\":[",
                 (unsigned)attacker, (unsigned)target, doubles ? "true" : "false");
    for (u32 b = 0; b < MAX_BATTLERS_COUNT; b++)
        len = Append(buf, len, size, "%s%s", b ? "," : "",
                     b < gBattlersCount && IsBattlerSpritePresent(b) ? "true" : "false");
    len = Append(buf, len, size, "],\"mons\":[");
    for (u32 b = 0; b < MAX_BATTLERS_COUNT; b++)
    {
        int picW = 48, picH = 48, yOffset = 0, elevation = 0;
        bool shiny = false;

        if (b < gBattlersCount)
        {
            u16 species = BattlerSpecies(b);

            picW = GetBattlerSpriteCoordAttr(b, BATTLER_COORD_ATTR_WIDTH);
            picH = GetBattlerSpriteCoordAttr(b, BATTLER_COORD_ATTR_HEIGHT);
            yOffset = GetBattlerYDelta(b, species);
            elevation = GetBattlerElevation(b, species);
            shiny = IsMonShiny(GetBattlerMon(b));
        }
        len = Append(buf, len, size, "%s{\"picW\":%d,\"picH\":%d,\"yOffset\":%d,\"elevation\":%d,\"shiny\":%s}",
                     b ? "," : "", picW, picH, yOffset, elevation, shiny ? "true" : "false");
    }
    len = Append(buf, len, size, "]");
    /* The same on every client of a battle: presentation only (particle variety). */
    len = Append(buf, len, size, ",\"seed\":%u",
                 (unsigned)((gBattleResults.battleTurnCounter * 2654435761u) ^ (gAnimMoveIndex << 8)
                            ^ (attacker << 4) ^ target));
    /* the states the game's Random2 and Random start this animation from (read only), and how many
     * numbers the vblank handlers take from Random a frame (the fewest of the last frames: the battle
     * engine draws some of its own in a frame now and then) */
    len = Append(buf, len, size, ",\"rng2\":[%u,%u,%u,%u],\"rng1\":[%u,%u,%u,%u],\"rng1PerFrame\":%u",
                 (unsigned)gRng2Value.a, (unsigned)gRng2Value.b, (unsigned)gRng2Value.c, (unsigned)gRng2Value.ctr,
                 (unsigned)gRngValue.a, (unsigned)gRngValue.b, (unsigned)gRngValue.c, (unsigned)gRngValue.ctr,
                 (unsigned)Rng1PerFrame());
    /* the sprites the battle has besides effect sprites (battlers, healthboxes, the last used ball's icon,
     * ...): an animation gets only the rest of the 64, and an effect sprite that finds none free is not
     * made (Fairy Lock's bolts fill them) - the core keeps as many taken */
    {
        int others = 0;
        for (int i = 0; i < MAX_SPRITES; i++)
            if (gSprites[i].inUse && !BattleFx_IsEffectSprite(&gSprites[i]))
                others++;
        len = Append(buf, len, size, ",\"spritesInUse\":%d", others);
    }
    /* the battlers' sprites where the game has them (an animation reads their x / y: Eruption's rocks leave
     * from the attacker's), which is not always their home (a battler a move has moved and not yet brought
     * back, a semi-invulnerable one) */
    len = Append(buf, len, size, ",\"battlerSprites\":[");
    for (u32 b = 0; b < MAX_BATTLERS_COUNT; b++)
    {
        const struct Sprite *sp = b < gBattlersCount && gBattlerSpriteIds[b] < MAX_SPRITES ? &gSprites[gBattlerSpriteIds[b]] : NULL;

        if (sp == NULL || !sp->inUse)
            len = Append(buf, len, size, "%snull", b ? "," : "");
        else
            len = Append(buf, len, size, "%s[%d,%d,%d,%d,%d]", b ? "," : "", sp->x, sp->y, sp->x2, sp->y2, sp->invisible ? 1 : 0);
    }
    len = Append(buf, len, size, "]");
    return len;
}

/* The player's left battler is its trainer's back picture, which throws (battle_anim_throw.c
 * AnimTask_ThrowBall_StandingTrainer and the Pokeblock throw move it): always in the Safari Zone; in the
 * catch tutorial once Wally has come back out (BattleScript_ActionWallyThrow: returnatktoball,
 * trainerslidein), when its sprite is no longer a Pokemon's picture. */
static bool PlayerLeftIsTrainer(void)
{
    u32 b = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    const struct Sprite *sp;

    if (gBattleTypeFlags & BATTLE_TYPE_SAFARI)
        return true;
    if (!(gBattleTypeFlags & BATTLE_TYPE_CATCH_TUTORIAL) || b >= gBattlersCount || gBattlerSpriteIds[b] >= MAX_SPRITES)
        return false;
    sp = &gSprites[gBattlerSpriteIds[b]];
    return sp->inUse && sp->anims != gAnims_MonPic;
}

/* The trainer's back picture's own frames, its rest and its throw (anims[0] and [1]): the throw's arm comes
 * up at the throw's second command, when the ball or the Pokeblock leaves (AnimTask_ThrowBall_StandingTrainer,
 * SpriteCB_PokeBlock_LiftArm), so the core times the throw by the picture the game shows - Brendan's, May's and
 * Wally's are the Hoenn frames the package knows; another back picture (a custom character's) has its own.
 * As the core's animation commands: [0, image, duration, hFlip, vFlip], [-1] end, [-2, target] jump,
 * [-3, count] loop. */
static int AppendTrainerAnims(char *buf, int len, int size)
{
    u32 b = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    const struct Sprite *sp;

    if (b >= gBattlersCount || gBattlerSpriteIds[b] >= MAX_SPRITES)
        return len;
    sp = &gSprites[gBattlerSpriteIds[b]];
    if (!sp->inUse || sp->anims == NULL || sp->anims == gAnims_MonPic || sp->anims[0] == NULL || sp->anims[1] == NULL)
        return len;
    len = Append(buf, len, size, ",\"trainerAnims\":[");
    for (u32 a = 0; a < 2; a++)
    {
        const union AnimCmd *cmd = sp->anims[a];

        len = Append(buf, len, size, "%s[", a ? "," : "");
        for (u32 i = 0; i < 32; i++)
        {
            const char *sep = i ? "," : "";

            if (cmd[i].type == -1)
            {
                len = Append(buf, len, size, "%s[-1]", sep);
                break;
            }
            if (cmd[i].type == -2)
                len = Append(buf, len, size, "%s[-2,%u]", sep, (unsigned)cmd[i].jump.target);
            else if (cmd[i].type == -3)
                len = Append(buf, len, size, "%s[-3,%u]", sep, (unsigned)cmd[i].loop.count);
            else
                len = Append(buf, len, size, "%s[0,%u,%u,%u,%u]", sep, (unsigned)cmd[i].frame.imageValue,
                             (unsigned)cmd[i].frame.duration, (unsigned)cmd[i].frame.hFlip, (unsigned)cmd[i].frame.vFlip);
        }
        len = Append(buf, len, size, "]");
    }
    return Append(buf, len, size, "]");
}

/* What the battle engine hands a battle animation, read where the game's own
 * scripts and tasks read it (battle_anim_throw.c, battle_anim_new.c,
 * battle_anim_utility_funcs.c), as the core's inputs (docs: the package's
 * INTEGRATION_GUIDE.md section 3b). */
static int AppendInputs(char *buf, int len, int size, struct BfxHost *host, const char *id)
{
    const struct BattleAnimationInfo *ad = gBattleSpritesDataPtr ? gBattleSpritesDataPtr->animationData : NULL;
    u32 attacker = gBattleAnimAttacker, ball, weather = 0;
    u16 item = attacker < gBattlersCount ? gBattleMons[attacker].item : ITEM_NONE;

    if (strncmp(id, "B_ANIM_SWITCH_OUT", 17) == 0)
        ball = attacker < gBattlersCount ? GetMonData(GetBattlerMon(attacker), MON_DATA_POKEBALL) : BALL_POKE;
    else
        ball = ItemIdToBallId(gLastUsedItem);
    if (ball >= POKEBALL_COUNT)
        ball = BALL_POKE;
    if (MoveHasAdditionalEffect(gCurrentMove, MOVE_EFFECT_SUN))
        weather = ANIM_WEATHER_SUN;
    else if (MoveHasAdditionalEffect(gCurrentMove, MOVE_EFFECT_RAIN))
        weather = ANIM_WEATHER_RAIN;
    else if (MoveHasAdditionalEffect(gCurrentMove, MOVE_EFFECT_SANDSTORM))
        weather = ANIM_WEATHER_SANDSTORM;
    else if (MoveHasAdditionalEffect(gCurrentMove, MOVE_EFFECT_HAIL))
        weather = ANIM_WEATHER_HAIL;
    len = Append(buf, len, size, "\"ball\":%u,\"weatherToSet\":%u", (unsigned)ball, (unsigned)weather);
    if (ad != NULL)
    {
        const char *trap = BfxHost_MoveId(host, (int)ad->animArg);
        len = Append(buf, len, size, ",\"animArg\":%u,\"ballThrowCase\":%u,\"criticalCapture\":%s,"
                     "\"criticalCaptureSuccess\":%s",
                     (unsigned)ad->animArg, (unsigned)ad->ballThrowCaseId, ad->isCriticalCapture ? "true" : "false",
                     ad->criticalCaptureSuccess ? "true" : "false");
        if (strcmp(id, "B_ANIM_TURN_TRAP") == 0 && trap != NULL)
            len = Append(buf, len, size, ",\"trapMove\":\"%s\"", trap);
    }
    len = Append(buf, len, size, ",\"substitute\":[");
    for (u32 b = 0; b < MAX_BATTLERS_COUNT; b++)
        len = Append(buf, len, size, "%s%s", b ? "," : "",
                     gBattleSpritesDataPtr && b < gBattlersCount && gBattleSpritesDataPtr->battlerData[b].behindSubstitute
                         ? "true" : "false");
    len = Append(buf, len, size, "],\"effectAttacker\":%u,\"effectBattler\":%u,\"effectTarget\":%u",
                 (unsigned)gBattlerAttacker, (unsigned)gEffectBattler, (unsigned)gBattlerTarget);
    if (item == ITEM_RED_ORB || item == ITEM_BLUE_ORB)
        len = Append(buf, len, size, ",\"item\":\"%s\"", item == ITEM_RED_ORB ? "ITEM_RED_ORB" : "ITEM_BLUE_ORB");
    if (gBattleAnimTarget < gBattlersCount)
        len = Append(buf, len, size, ",\"affection\":%u", (unsigned)GetBattlerAffectionHearts(gBattleAnimTarget));
    if (gAnimDisableStructPtr != NULL)
        len = Append(buf, len, size, ",\"syrupShiny\":%s", gAnimDisableStructPtr->syrupBombIsShiny ? "true" : "false");
    len = Append(buf, len, size, ",\"safari\":%s,\"catchTutorial\":%s,\"safariReaction\":%u",
                 PlayerLeftIsTrainer() ? "true" : "false",
                 (gBattleTypeFlags & BATTLE_TYPE_CATCH_TUTORIAL) ? "true" : "false",
                 gBattleCommunication[MULTISTRING_CHOOSER] < NUM_SAFARI_REACTIONS
                     ? (unsigned)gBattleCommunication[MULTISTRING_CHOOSER] : 0u);
    if (PlayerLeftIsTrainer())
        len = AppendTrainerAnims(buf, len, size);
    return len;
}

/* How the core presents the hit (its impact and reaction strength; presentation only): what the
 * battle engine has already worked out for the move's target before its animation plays (critcalc,
 * typecalc come before attackanimation). */
static const char *HitPresentation(u32 move)
{
    u32 target = gBattleAnimTarget;
    u32 flags;

    if (move >= MOVES_COUNT || GetMoveCategory(move) == DAMAGE_CATEGORY_STATUS || target >= MAX_BATTLERS_COUNT
     || gBattleStruct == NULL)
        return "none";
    flags = gBattleStruct->moveResultFlags[target];
    if (gSpecialStatuses[target].criticalHit)
        return "crit";
    if (flags & MOVE_RESULT_SUPER_EFFECTIVE)
        return "super";
    if (flags & MOVE_RESULT_NOT_VERY_EFFECTIVE)
        return "weak";
    return "normal";
}

/* Mirror Move playing the move it copied: the core's optional mirror cue (off unless its
 * mirrorCue setting is on). */
static bool CalledByMirrorMove(u32 move)
{
    return move != MOVE_MIRROR_MOVE && gBattlerAttacker < MAX_BATTLERS_COUNT
        && gChosenMoveByBattler[gBattlerAttacker] == MOVE_MIRROR_MOVE;
}

/* ── Script branches ──────────────────────────────────────────────────── */

/* The value a deciding task of the game's animation script gives the jump after
 * it, computed from what the game's task reads, the way it reads it (the tasks
 * are in battle_anim_*.c). The core's data has each script branch as a timeline
 * variant; this picks the one the game's script takes. false: a task not known
 * here (the core then chooses, from the battle setup it was given). */
static bool BranchValue(const char *move, const char *task, int arg, int *value)
{
    u32 atk = gBattleAnimAttacker, tgt = gBattleAnimTarget;
    const struct LinkBattleAnim *d = gAnimDisableStructPtr;

    if (task[0] == '\0')
    {
        /* a jump on a script argument a task set: Techno Blast's Drive (AnimTask_TechnoBlast) */
        if (strcmp(move, "MOVE_TECHNO_BLAST") == 0 && arg == 0 && atk < gBattlersCount)
        {
            u16 item = gBattleMons[atk].item;
            *value = GetItemHoldEffect(item) == HOLD_EFFECT_DRIVE ? (int)GetItemSecondaryId(item) : 0;
            return true;
        }
        return false;
    }
    if (strcmp(task, "AnimTask_IsTargetPlayerSide") == 0)
        *value = IsOnPlayerSide(tgt) ? TRUE : FALSE;
    else if (strcmp(task, "AnimTask_IsAttackerPlayerSide") == 0)
        *value = IsOnPlayerSide(atk) ? 1 : 0;
    else if (strcmp(task, "AnimTask_GetAttackerSide") == 0)
        *value = GetBattlerSide(atk);
    else if (strcmp(task, "AnimTask_GetTargetSide") == 0)
        *value = GetBattlerSide(tgt);
    else if (strcmp(task, "AnimTask_IsDoubleBattle") == 0)
        *value = IsDoubleBattle() && !IsContest();
    else if (strcmp(task, "AnimTask_IsContest") == 0)
        *value = IsContest() ? TRUE : FALSE;
    else if (strcmp(task, "AnimTask_IsTargetSameSide") == 0)
        *value = IsBattlerAlly(atk, tgt) ? TRUE : FALSE;
    else if (strcmp(task, "AnimTask_GetTargetIsAttackerPartner") == 0)
        *value = GetPartnerBattler(atk) == tgt;
    else if (strcmp(task, "AnimTask_IsHealingMove") == 0)
        *value = gAnimMoveDmg > 0 ? FALSE : TRUE;
    else if (strcmp(task, "AnimTask_IsPowerOver99") == 0)
        *value = gAnimMovePower > 99;
    else if (strcmp(task, "AnimTask_GetReturnPowerLevel") == 0)
    {
        *value = 0;
        if (gAnimFriendship > 60 && gAnimFriendship < 92)
            *value = 1;
        if (gAnimFriendship > 91 && gAnimFriendship < 201)
            *value = 2;
        if (gAnimFriendship > 200)
            *value = 3;
    }
    else if (strcmp(task, "AnimTask_GetFrustrationPowerLevel") == 0)
        *value = gAnimFriendship <= 30 ? 0 : gAnimFriendship <= 100 ? 1 : gAnimFriendship <= 200 ? 2 : 3;
    else if (strcmp(task, "AnimTask_GetSeismicTossDamageLevel") == 0)
    {
        *value = 0;
        if ((u32)gAnimMoveDmg - 33 < 33)
            *value = 1;
        if (gAnimMoveDmg > 65)
            *value = 2;
    }
    else if (strcmp(task, "AnimTask_GetIceBallCounter") == 0 && d != NULL)
        *value = d->rolloutTimer + 1;
    else if (strcmp(task, "AnimTask_GetFuryCutterHitCount") == 0 && d != NULL)
        *value = d->furyCutterCounter;
    else if (strcmp(task, "AnimTask_IsFuryCutterHitRight") == 0 && d != NULL)
        *value = d->furyCutterCounter & 1;
    else if (strcmp(task, "AnimTask_GetStockpileCounter") == 0 && d != NULL)
        *value = d->stockpileCounter;
    else if (strcmp(task, "AnimTask_GetWeather") == 0 && atk < gBattlersCount)
    {
        /* GetBattlerHoldEffect and GetBattlerAbility note the battler whose item may be named in a
         * message (gPotentialItemEffectBattler): put back, so nothing in the game changes */
        enum BattlerId potential = gPotentialItemEffectBattler;
        u32 weather = GetAttackerWeather(GetBattlerHoldEffect(atk), GetBattlerAbility(atk), gWeatherMoveAnim);

        gPotentialItemEffectBattler = potential;
        *value = ANIM_WEATHER_NONE;
        if (weather & B_WEATHER_SUN)
            *value = ANIM_WEATHER_SUN;
        else if (weather & B_WEATHER_RAIN)
            *value = ANIM_WEATHER_RAIN;
        else if (weather & B_WEATHER_SANDSTORM)
            *value = ANIM_WEATHER_SANDSTORM;
        else if (weather & B_WEATHER_HAIL)
            *value = ANIM_WEATHER_HAIL;
        else if (weather & B_WEATHER_SNOW)
            *value = ANIM_WEATHER_SNOW;
        else if (weather & B_WEATHER_FOG)
            *value = ANIM_WEATHER_FOG;
    }
    else
        return false;
    return true;
}

/* The core's variant for the branch the game's own script takes: the first of the
 * move's variants (they are in script order) whose condition holds - its turn
 * (gAnimMoveTurn) or its task's value; -1 (the core's default branch) when none
 * does or a condition is unknown here. Fury Cutter has two conditions and the data
 * one variant per condition: its second use in a row (left slice, one dimming
 * instead of three) is the one case no variant matches exactly. The variant lists
 * are read from the core once it has loaded (LoadVariants, on its thread), by the
 * core's move number. */
#define MAX_VARIANT_MOVES 1024
static char *sVariants[MAX_VARIANT_MOVES];

static void LoadVariants(struct BfxHost *host)
{
    char arg[64];

    for (int m = 0; m < MAX_VARIANT_MOVES; m++)
    {
        const char *id = BfxHost_MoveId(host, m);

        if (id == NULL)
            continue;
        snprintf(arg, sizeof(arg), "\"%s\"", id);
        sVariants[m] = BfxHost_CallGlobal(host, "BFX_GAME_VARIANTS", arg);
    }
}

static int GameVariant(struct BfxHost *host, const char *id)
{
    char *list, *entry, *save = NULL;
    const char *variants = NULL;
    int index = 0, chosen = -1;
    bool unknown = false;

    /* the core's ids are its own strings: the move whose id this is */
    for (int m = 0; m < MAX_VARIANT_MOVES && variants == NULL; m++)
        if (BfxHost_MoveId(host, m) == id)
            variants = sVariants[m];
    if (variants == NULL || variants[0] == '\0')
        return -1;
    list = SDL_strdup(variants);
    if (list == NULL)
        return -1;
    for (entry = SDL_strtokr(list, ";", &save); entry != NULL && chosen < 0; entry = SDL_strtokr(NULL, ";", &save), index++)
    {
        char kind[8] = "", task[64] = "";
        int a = -1, want = 0, have = 0;
        char *f1 = strchr(entry, '|'), *f2 = f1 ? strchr(f1 + 1, '|') : NULL;

        if (f1 == NULL || f2 == NULL)
            continue;
        snprintf(kind, sizeof(kind), "%.*s", (int)(f1 - entry), entry);
        snprintf(task, sizeof(task), "%.*s", (int)(f2 - f1 - 1), f1 + 1);
        if (sscanf(f2 + 1, "%d|%d", &a, &want) != 2)
            continue;
        if (strcmp(kind, "turn") == 0)
        {
            if (want == gAnimMoveTurn)
                chosen = index;
        }
        else if (BranchValue(id, task, a, &have))
        {
            if (have == want)
                chosen = index;
        }
        else
            unknown = true;
    }
    SDL_free(list);
    return unknown && chosen < 0 ? -1 : chosen;
}

/* The move whose animation script `script` is: the requested move when it is
 * its own, otherwise the first move with that script (Secret Power's swap). */
static const char *ScriptMove(struct BfxHost *host, u32 move, const void *script)
{
    const char *id = BfxHost_MoveId(host, (int)move);

    if (script == NULL || (move < MOVES_COUNT && gMovesInfo[move].battleAnimScript == script))
        return id;
    for (u32 m = 1; m < MOVES_COUNT; m++)
        if (gMovesInfo[m].battleAnimScript == script && BfxHost_MoveId(host, (int)m) != NULL)
            return BfxHost_MoveId(host, (int)m);
    return id;
}

static const char *AnimKind(u32 animType)
{
    switch (animType)
    {
    case ANIM_TYPE_STATUS:
        return "status";
    case ANIM_TYPE_SPECIAL:
        return "special";
    default:
        return "general";
    }
}

/* ── The core's thread ───────────────────────────────────────────────────
 *
 * The core (QuickJS) runs on a thread of its own - the one that loaded it. The
 * game thread hands it each animation's begin and one step a frame and goes on
 * without waiting, so the core's cost (a begin's plan: tens of milliseconds for
 * a long animation; a heavy frame; a garbage collection) never holds a game
 * frame up. Each frame the core makes is copied out of its arrays into a slot
 * of its own (the particle rings into one mirror, with the ranges that changed
 * since the boundary last took them). The frame boundary shows the frame made
 * for the game frame whose OAM the picture shows - the picture drawn after game
 * frame N shows frame N - 1's sprites (the GBA loads OAM at the VBlank), so the
 * core's frame for N - 1 goes with it, as before. When the core runs behind,
 * the newest frame it has made of the animation is shown (up to CORE_MAX_LAG
 * frames old), and a frame that is done later in the picture's time replaces it
 * (BattleFx_LateFrame); an animation shows nothing HD until the core has caught
 * up with it (its GBA effects show), and one the core falls further behind on
 * plays its GBA effects from there on. With sync (the test builds,
 * PKMN_BATTLEFX_SYNC=1 / sync=1) the game thread waits for every command: the
 * core's frames pair with the game's frame for frame, as the parity tests need,
 * and the test driver may use the core between frames. */

#define CORE_QUEUE 256
#define CORE_SLOTS 4
#define CORE_MAX_LAG 6
#define CORE_START_WAIT 4 /* pictures an animation's effect sprites wait for the core's first frame of it */
#define CORE_FIRST_WAIT_MS 60
#define CORE_CATCHUP_WAIT_MS 8
#define CONTROLLER_ALIGN_MAX 8
#define DRAW_SLOW_MAX 2

enum
{
    CORE_CMD_BEGIN = 1,
    CORE_CMD_STEP,
    CORE_CMD_END,
};

struct CoreCommand
{
    uint8_t kind;
    uint8_t controller; /* BEGIN: BATTLEFX_CONTROLLER_*, stepped up to the game's frame (seq 0) */
    bool doubles;       /* BEGIN: for the log */
    uint32_t serial;
    int seq;            /* STEP: 1, 2, ... (the frame's number in the animation) */
    uint32_t sound;     /* BEGIN, STEP: the game's sound state at the frame's start (FeedSound) */
    int gameFrames;     /* END: the game's frames, for the log */
    char id[48];
    char why[64];       /* END */
    char *request;      /* BEGIN: malloc'd, freed by the core's thread */
};


/* A frame of the core, copied out of its arrays (rings: sQ.rings). */
struct CoreSlot
{
    bool valid;
    uint32_t serial;
    int seq;
    struct BfxPacket packet; /* pointers into the arrays below */
    float view[16], camera[16];
    float *quad[4][4];
    int quadCap[4];
    float *rib[2][4];
    uint16_t *ribIdx[2];
    int ribCap[2], ribIdxCap[2];
    float *mesh[BFX_MAX_MESH_KINDS];
    int meshCap[BFX_MAX_MESH_KINDS];
    float *lights;
    int lightCap;
};

/* A particle ring, whole (the core writes emission records into it in place). */
struct CoreRing
{
    int cap, head, count;
    bool add;
    float *a[6];
    int lo, hi;   /* changed since the boundary took it; -1: nothing */
    bool cleared; /* ... all of it */
};

static struct
{
    SDL_mutex *lock;
    SDL_cond *wake; /* the core's thread: a command came */
    SDL_cond *done; /* the game thread: a command was done */
    struct CoreCommand q[CORE_QUEUE];
    unsigned head, tail;
    bool busy;
    struct CoreStatus status;
    struct CoreSlot slotStore[CORE_SLOTS + 1];
    struct CoreSlot *slots[CORE_SLOTS]; /* the frames the boundary can show */
    struct CoreSlot *spare;             /* the core's thread fills it */
    struct CoreRing rings[2];           /* partsAdd, partsAlpha */
} sQ;

static struct CoreStatus sRun;   /* the core's thread's */
static int sRunSound = -1;       /* the core's thread's: the sound state the core was given last */
static SDL_atomic_t sTooSlow;    /* serial of the animation the boundary found the core too far behind on */
static unsigned sTooSlowCount;
/* The drawing over its budget (sCore.drawBudgetMs): on a PC whose graphics take longer than that a picture to draw
 * the HD layers, the 3D battle - which paces the game by its pictures - would slow the game. Such an animation
 * goes on as on the GBA (sDrawSlow: its serial, also in sTooSlow; sDrawSlowTenths: the cost measured, 0.1 ms), and
 * after DRAW_SLOW_MAX of them the HD effects are off until the battle ends (sHdOff, game thread). */
static SDL_atomic_t sDrawSlow, sDrawSlowTenths;
static unsigned sDrawSlowCount;
static bool sHdOff;

static bool CoreInit(void)
{
    sQ.lock = SDL_CreateMutex();
    sQ.wake = SDL_CreateCond();
    sQ.done = SDL_CreateCond();
    if (sQ.lock == NULL || sQ.wake == NULL || sQ.done == NULL)
        return false;
    for (int i = 0; i < CORE_SLOTS; i++)
        sQ.slots[i] = &sQ.slotStore[i];
    sQ.spare = &sQ.slotStore[CORE_SLOTS];
    sQ.rings[0].lo = sQ.rings[0].hi = sQ.rings[1].lo = sQ.rings[1].hi = -1;
    return true;
}

/* Game thread: until the core has done every command. */
static void CoreWaitIdle(void)
{
    if (sQ.lock == NULL)
        return;
    SDL_LockMutex(sQ.lock);
    while (sQ.head != sQ.tail || sQ.busy)
        SDL_CondWait(sQ.done, sQ.lock);
    SDL_UnlockMutex(sQ.lock);
}

/* Game thread: hands the core a command (with sync, waits until it is done). */
static void CorePost(const struct CoreCommand *c)
{
    SDL_LockMutex(sQ.lock);
    while (sQ.head - sQ.tail >= CORE_QUEUE) /* the core far behind: a full queue waits */
        SDL_CondWait(sQ.done, sQ.lock);
    sQ.q[sQ.head % CORE_QUEUE] = *c;
    sQ.head++;
    SDL_CondSignal(sQ.wake);
    SDL_UnlockMutex(sQ.lock);
    if (sCore.sync)
        CoreWaitIdle();
}

/* Game thread: the core's status of animation `serial` (false: none yet). */
static bool CoreStatusOf(uint32_t serial, struct CoreStatus *out)
{
    bool same;

    SDL_LockMutex(sQ.lock);
    same = sQ.status.serial == serial;
    if (same)
        *out = sQ.status;
    SDL_UnlockMutex(sQ.lock);
    return same;
}

static bool GrowFloats(float **p, int *cap, int need)
{
    float *n;

    if (need <= *cap)
        return true;
    n = realloc(*p, (size_t)need * sizeof(float));
    if (n == NULL)
        return false;
    *p = n;
    *cap = need;
    return true;
}

/* Core's thread: `frame` copied into `s` (its own arrays). */
static bool OwnFrame(struct CoreSlot *s, const struct BfxPacket *frame)
{
    struct BfxQuads *quads[4];
    struct BfxRibbons *ribbons[2];

    s->packet = *frame;
    quads[0] = &s->packet.cards;
    quads[1] = &s->packet.cardsBack;
    quads[2] = &s->packet.decals;
    quads[3] = &s->packet.dist;
    for (int k = 0; k < 4; k++)
    {
        struct BfxQuads *q = quads[k];
        const float *src[4] = {q->P, q->S, q->C, q->X};
        int n = q->n;

        if (n <= 0 || src[0] == NULL || src[1] == NULL || src[2] == NULL || src[3] == NULL)
        {
            q->n = 0;
            continue;
        }
        for (int i = 0; i < 4; i++)
        {
            int cap = s->quadCap[k];
            if (!GrowFloats(&s->quad[k][i], &cap, n * 4))
                return false;
            memcpy(s->quad[k][i], src[i], (size_t)n * 16);
        }
        s->quadCap[k] = n * 4 > s->quadCap[k] ? n * 4 : s->quadCap[k];
        q->P = s->quad[k][0];
        q->S = s->quad[k][1];
        q->C = s->quad[k][2];
        q->X = s->quad[k][3];
    }
    ribbons[0] = &s->packet.ribbons;
    ribbons[1] = &s->packet.ribbonsBack;
    for (int k = 0; k < 2; k++)
    {
        struct BfxRibbons *r = ribbons[k];
        static const int kFloats[4] = {3, 2, 4, 2};
        const float *src[4] = {r->pos, r->uv, r->col, r->row};
        int nv = r->nv, ni = r->ni;

        if (nv <= 0 || ni <= 0 || r->idx == NULL || src[0] == NULL || src[1] == NULL || src[2] == NULL || src[3] == NULL)
        {
            r->nv = r->ni = 0;
            continue;
        }
        for (int i = 0; i < 4; i++)
        {
            int cap = s->ribCap[k] * kFloats[i];
            if (!GrowFloats(&s->rib[k][i], &cap, nv * kFloats[i]))
                return false;
            memcpy(s->rib[k][i], src[i], (size_t)nv * kFloats[i] * sizeof(float));
        }
        if (nv > s->ribCap[k])
            s->ribCap[k] = nv;
        if (ni > s->ribIdxCap[k])
        {
            uint16_t *n = realloc(s->ribIdx[k], (size_t)ni * sizeof(uint16_t));
            if (n == NULL)
                return false;
            s->ribIdx[k] = n;
            s->ribIdxCap[k] = ni;
        }
        memcpy(s->ribIdx[k], r->idx, (size_t)ni * sizeof(uint16_t));
        r->pos = s->rib[k][0];
        r->uv = s->rib[k][1];
        r->col = s->rib[k][2];
        r->row = s->rib[k][3];
        r->idx = s->ribIdx[k];
    }
    for (int k = 0; k < s->packet.meshKinds && k < BFX_MAX_MESH_KINDS; k++)
    {
        struct BfxMeshes *m = &s->packet.meshes[k];

        if (m->n <= 0 || m->data == NULL)
        {
            m->n = 0;
            continue;
        }
        if (!GrowFloats(&s->mesh[k], &s->meshCap[k], m->n * 19))
            return false;
        memcpy(s->mesh[k], m->data, (size_t)m->n * 19 * sizeof(float));
        m->data = s->mesh[k];
    }
    if (s->packet.lightCount > 0 && s->packet.lights != NULL)
    {
        if (!GrowFloats(&s->lights, &s->lightCap, s->packet.lightCount * 8))
            return false;
        memcpy(s->lights, s->packet.lights, (size_t)s->packet.lightCount * 8 * sizeof(float));
        s->packet.lights = s->lights;
    }
    else
        s->packet.lightCount = 0;
    if (frame->view != NULL)
        memcpy(s->view, frame->view, sizeof(s->view));
    if (frame->camera != NULL)
        memcpy(s->camera, frame->camera, sizeof(s->camera));
    s->packet.view = frame->view != NULL ? s->view : NULL;
    s->packet.camera = frame->camera != NULL ? s->camera : NULL;
    s->packet.sprites = NULL; /* the renderer does not read them */
    s->packet.spriteCount = 0;
    /* the rings are the mirror's (sQ.rings), given at the boundary */
    s->packet.partsAdd.a = s->packet.partsAlpha.a = NULL;
    return true;
}

/* Core's thread, under sQ.lock: the core's ring `r` into the mirror `m` (its changed records; all of it
 * when it was cleared or changed size). */
static void MirrorRing(struct CoreRing *m, const struct BfxRing *r)
{
    const float *src[6] = {r->a, r->b, r->c, r->d, r->e, r->f};
    int lo = r->lo, hi = r->hi;
    bool all = r->cleared;

    if (r->a == NULL || r->cap <= 0)
        return;
    if (m->cap != r->cap)
    {
        for (int i = 0; i < 6; i++)
        {
            float *n = realloc(m->a[i], (size_t)r->cap * 16);
            if (n == NULL)
            {
                m->cap = 0;
                return;
            }
            m->a[i] = n;
        }
        m->cap = r->cap;
        all = true;
    }
    if (!all && (lo < 0 || hi < 0 || lo > hi || hi >= m->cap))
    {
        if (lo < 0 || hi < 0)
            lo = hi = -1; /* nothing new */
        else
            all = true;
    }
    if (all)
    {
        lo = 0;
        hi = m->cap - 1;
        m->cleared = true;
    }
    if (lo >= 0)
    {
        for (int i = 0; i < 6; i++)
            if (src[i] != NULL)
                memcpy(m->a[i] + (size_t)lo * 4, src[i] + (size_t)lo * 4, (size_t)(hi - lo + 1) * 16);
        if (m->lo < 0 || lo < m->lo)
            m->lo = lo;
        if (hi > m->hi)
            m->hi = hi;
    }
    m->head = r->head;
    m->count = r->count;
    m->add = r->add;
}

/* Core's thread: the frame the core just made (its arrays) published as (serial, seq). */
static void Publish(const struct BfxPacket *frame, uint32_t serial, int seq)
{
    struct CoreSlot *s = sQ.spare, *victim = NULL;
    int at = -1;

    if (!OwnFrame(s, frame))
        return;
    s->serial = serial;
    s->seq = seq;
    s->valid = true;
    SDL_LockMutex(sQ.lock);
    MirrorRing(&sQ.rings[0], &frame->partsAdd);
    MirrorRing(&sQ.rings[1], &frame->partsAlpha);
    /* the slot it takes: the same frame's, a free one, another animation's, else this one's oldest */
    for (int i = 0; i < CORE_SLOTS && at < 0; i++)
        if (sQ.slots[i]->valid && sQ.slots[i]->serial == serial && sQ.slots[i]->seq == seq)
            at = i;
    for (int i = 0; i < CORE_SLOTS && at < 0; i++)
        if (!sQ.slots[i]->valid)
            at = i;
    for (int i = 0; i < CORE_SLOTS && at < 0; i++)
        if (sQ.slots[i]->serial != serial)
            at = i;
    if (at < 0)
    {
        at = 0;
        for (int i = 1; i < CORE_SLOTS; i++)
            if (sQ.slots[i]->seq < sQ.slots[at]->seq)
                at = i;
    }
    victim = sQ.slots[at];
    sQ.slots[at] = s;
    sQ.spare = victim;
    sQ.spare->valid = false;
    SDL_UnlockMutex(sQ.lock);
}

/* Core's thread: the game's sound state for the core's next step (kGameHelperJs BFX_GAME_SOUND). */
static void FeedSound(struct BfxHost *host, uint32_t sound)
{
    if ((int)sound == sRunSound)
        return;
    BfxHost_CallGlobalIntArg(host, "BFX_GAME_SOUND", (int)sound, 0);
    sRunSound = (int)sound;
}

/* Core's thread: one step of the core and its frame, published as `seq`. false: the animation failed. */
static bool CoreStepOnce(struct BfxHost *host, uint32_t sound, int seq)
{
    struct BfxPacket frame;
    Uint64 t0, t1, t2 = 0;
    unsigned gc0;
    int r;
    double ms;

    FeedSound(host, sound);
    t0 = SDL_GetPerformanceCounter();
    gc0 = BfxHost_GcCount(host);
    r = BfxHost_Step(host);
    t1 = SDL_GetPerformanceCounter();
    if (r < 0)
    {
        sRun.failed = true;
        snprintf(sRun.error, sizeof(sRun.error), "step: %s", BfxHost_LastError(host));
        return false;
    }
    if (r == 0)
    {
        sRun.ended = true;
        return true;
    }
    memset(&frame, 0, sizeof(frame));
    if (!BfxHost_Frame(host, -1, &frame))
    {
        sRun.failed = true;
        snprintf(sRun.error, sizeof(sRun.error), "frame: %s", BfxHost_LastError(host));
        return false;
    }
    t2 = SDL_GetPerformanceCounter();
    frame.serial = sRun.serial;
    frame.hideBg = (uint8_t)(BfxHost_CallGlobalInt(host, "BFX_GAME_HIDDEN_BGS", 0) & 0x06);
    Publish(&frame, sRun.serial, seq);
    BfxHost_DrainSounds(host);
    sRun.frames++;
    ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    sRun.stepMs += ms;
    if (ms > sRun.worstStepMs)
        sRun.worstStepMs = ms;
    if (sCore.log && ms > 20.0)
    {
        double f = 1000.0 / SDL_GetPerformanceFrequency();
        SDL_Log("Battle FX: %s frame %d took %.1f ms (step %.1f, frame packet %.1f%s)", sRun.id, sRun.frames, ms,
                (t1 - t0) * f, (t2 - t1) * f, BfxHost_GcCount(host) != gc0 ? ", with a garbage collection" : "");
    }
    if (BfxHost_GcCount(host) != gc0)
        sRun.gcSteps++;
    else if (ms > sRun.worstPlainStepMs)
        sRun.worstPlainStepMs = ms;
    return true;
}

static void CoreBegin(struct BfxHost *host, const struct CoreCommand *c)
{
    Uint64 t0 = SDL_GetPerformanceCounter();
    unsigned gc0 = BfxHost_GcCount(host);

    memset(&sRun, 0, sizeof(sRun));
    sRun.serial = c->serial;
    snprintf(sRun.id, sizeof(sRun.id), "%s", c->id);
    if (!BfxHost_Begin(host, c->request, &sRun.info))
    {
        sRun.refused = true;
        sRun.over = true;
        snprintf(sRun.error, sizeof(sRun.error), "%s", BfxHost_LastError(host));
        return;
    }
    sRun.begun = true;
    sRun.gcInBegin = BfxHost_GcCount(host) != gc0;
    sRun.beginMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    sRunSound = -1; /* the core's next step gets the sound state again */
    if (sCore.log)
        SDL_Log("Battle FX: %s (battler %d -> %d%s) begins: %d frames planned, begin %.1f ms%s", c->id,
                sRun.info.attacker, sRun.info.target, c->doubles ? ", double battle" : "", sRun.info.totalFrames,
                sRun.beginMs, sRun.gcInBegin ? " (with a garbage collection)" : "");
    if (c->controller)
    {
        /* in step with the game: the core's frames up to the game's (the send-out: until its ball exists),
         * the last one published as seq 0 */
        for (int i = 0; i < CONTROLLER_ALIGN_MAX && !sRun.ended; i++)
        {
            if (!CoreStepOnce(host, c->sound, 0))
                return;
            if (c->controller != BATTLEFX_CONTROLLER_SEND_OUT || BfxHost_CallGlobalInt(host, "BFX_GAME_HAS_BALL", 0))
                break;
        }
    }
}

static void CoreStep(struct BfxHost *host, const struct CoreCommand *c)
{
    if (c->serial != sRun.serial || !sRun.begun || sRun.ended || sRun.failed || sRun.over)
        return;
    CoreStepOnce(host, c->sound, c->seq);
    sRun.lastSeq = c->seq;
}

static void CoreEnd(const struct CoreCommand *c)
{
    if (c->serial != sRun.serial || sRun.over)
        return;
    sRun.over = true;
    if (sCore.log && sRun.begun)
        SDL_Log("Battle FX: %s ended (%s): the game's animation %d frames, the core's %d of %d%s; begin %.1f ms%s, "
                "a frame %.2f ms on average, %.2f ms at worst (%.2f without a garbage collection; %d with one)",
                sRun.id, c->why, c->gameFrames, sRun.frames, sRun.info.totalFrames, sRun.ended ? " (it had ended)" : "",
                sRun.beginMs, sRun.gcInBegin ? " (with a garbage collection)" : "",
                sRun.frames ? sRun.stepMs / sRun.frames : 0.0, sRun.worstStepMs, sRun.worstPlainStepMs, sRun.gcSteps);
}

/* The core's thread from the moment the core is ready: one command at a time, in order. */
static void CoreLoop(struct BfxHost *host)
{
    for (;;)
    {
        struct CoreCommand c;

        SDL_LockMutex(sQ.lock);
        while (sQ.head == sQ.tail)
        {
            sQ.busy = false;
            SDL_CondBroadcast(sQ.done);
            SDL_CondWait(sQ.wake, sQ.lock);
        }
        c = sQ.q[sQ.tail % CORE_QUEUE];
        sQ.tail++;
        sQ.busy = true;
        SDL_UnlockMutex(sQ.lock);
        switch (c.kind)
        {
        case CORE_CMD_BEGIN:
            CoreBegin(host, &c);
            free(c.request);
            break;
        case CORE_CMD_STEP:
            CoreStep(host, &c);
            break;
        case CORE_CMD_END:
            CoreEnd(&c);
            break;
        }
        SDL_LockMutex(sQ.lock);
        sQ.status = sRun;
        SDL_CondBroadcast(sQ.done);
        SDL_UnlockMutex(sQ.lock);
    }
}

/* ── The animation in progress (game thread) ──────────────────────────── */

static void EndAnimation(const char *why)
{
    struct CoreCommand c;

    if (!sAnim.active)
        return;
    sAnim.active = false;
#ifdef BATTLEFX_TEST
    BattleFxTest_OnAnimEnd(sAnim.serial);
#endif
    memset(&c, 0, sizeof(c));
    c.kind = CORE_CMD_END;
    c.serial = sAnim.serial;
    c.gameFrames = sAnim.gameFrames;
    snprintf(c.why, sizeof(c.why), "%s", why);
    CorePost(&c);
}

static void FailAnimation(const char *what)
{
    sFailures++;
    SDL_Log("Battle FX: %s failed at frame %d (%s); the rest of it plays as on the GBA", sAnim.id,
            sAnim.status.frames, what);
    sAnim.failed = true;
    EndAnimation("failed");
}

/* The sound state this frame's tasks see until one of them starts or stops a sound: sound advances only in
 * the vblank (main.c VBlankIntr, m4aSoundMain), after the frame's callbacks. The core's runtime has no audio;
 * its waits read the game's instead (kGameHelperJs, BFX_GAME_SOUND): IsCryPlaying() (the double cries of
 * Chatter, Hyper Voice, Roar, Uproar; a send-out's cry) and the SE1 / SE2 music players' status, which
 * IsSEPlaying() reads (waitsound, the script's end). Bit 0: a cry plays; 1: SE1 paused, 2: SE1 has tracks;
 * 3: SE2 paused, 4: SE2 has tracks. */
static u32 sSoundAtFrameBegin;

void BattleFx_OnFrameBegin(void)
{
    u32 se1, se2;

    sGameFrame++;
    if (!gMain.inBattle)
    {
        sHdOff = false; /* the next battle tries the HD effects again */
        sDrawSlowCount = 0;
        return;
    }
    se1 = gMPlayInfo_SE1.status;
    se2 = gMPlayInfo_SE2.status;
    sSoundAtFrameBegin = (IsCryPlaying() ? 1u : 0u)
                       | ((se1 & MUSICPLAYER_STATUS_PAUSE) ? 2u : 0u) | ((se1 & MUSICPLAYER_STATUS_TRACK) ? 4u : 0u)
                       | ((se2 & MUSICPLAYER_STATUS_PAUSE) ? 8u : 0u) | ((se2 & MUSICPLAYER_STATUS_TRACK) ? 16u : 0u);
}

#define BFX_REQUEST_MAX 4096
static bool BeginRequest(const char *id, const char *request, int len, uint32_t animType, uint32_t animId,
                         uint8_t controller);

void BattleFx_OnLaunch(uint32_t animType, uint32_t animId, const void *script)
{
    struct BfxHost *host;
    char request[BFX_REQUEST_MAX];
    int len = 0;
    const char *id;

    EndAnimation("another animation started");
    if (!gMain.inBattle || IsContest() || (host = ReadyHost()) == NULL)
        return;
    if (animType == ANIM_TYPE_MOVE)
    {
        id = ScriptMove(host, animId, script);
        if (id == NULL)
        {
            char name[48];
            snprintf(name, sizeof(name), "move %u", (unsigned)animId);
            NoteRefused(name, "not in the Battle FX data");
            return;
        }
        len = Append(request, len, sizeof(request), "{\"move\":\"%s\"", id);
        len = Append(request, len, sizeof(request),
                     ",\"turn\":%u,\"movePower\":%u,\"damage\":%d,\"friendship\":%u,\"weather\":%u,\"hit\":\"%s\"",
                     (unsigned)gAnimMoveTurn, (unsigned)gAnimMovePower, (int)gAnimMoveDmg,
                     (unsigned)gAnimFriendship, (unsigned)gWeatherMoveAnim, HitPresentation(animId));
        /* the battle's target (gBattlerTarget), which Future Sight's and Doom Desire's hit take again as the
         * animation's (AnimTask_SetAnimTargetToBattlerTarget; DoMoveAnim may have changed it for a spread move) */
        if (gBattlerTarget < MAX_BATTLERS_COUNT)
            len = Append(request, len, sizeof(request), ",\"battlerTarget\":%u", (unsigned)gBattlerTarget);
        if (gAnimDisableStructPtr != NULL)
            len = Append(request, len, sizeof(request), ",\"rolloutTimer\":%u", (unsigned)gAnimDisableStructPtr->rolloutTimer);
        if (CalledByMirrorMove(animId))
            len = Append(request, len, sizeof(request), ",\"calledBy\":\"MOVE_MIRROR_MOVE\"");
        {
            int variant = GameVariant(host, id);
            if (variant >= 0)
                len = Append(request, len, sizeof(request), ",\"variant\":%d", variant);
        }
    }
    else
    {
        id = BfxHost_AnimId(host, AnimKind(animType), (int)animId);
        if (id == NULL)
        {
            char name[48];
            snprintf(name, sizeof(name), "%s animation %u", AnimKind(animType), (unsigned)animId);
            NoteRefused(name, "not in the Battle FX data");
            return;
        }
        len = Append(request, len, sizeof(request), "{\"anim\":\"%s\",\"inputs\":{", id);
        len = AppendInputs(request, len, sizeof(request), host, id);
        len = Append(request, len, sizeof(request), "}");
    }
    len = AppendSetup(request, len, sizeof(request), gBattleAnimAttacker, gBattleAnimTarget);
    /* the battle's attacker (gBattlerAttacker), which three of the game's sprite callbacks read in place of the
     * animation's (kGameHelperJs: the battle's attacker) */
    if (gBattlerAttacker < MAX_BATTLERS_COUNT)
        len = Append(request, len, sizeof(request), ",\"battlerAttacker\":%u", (unsigned)gBattlerAttacker);
    /* a status, battle or special animation's script runs in a task (kGameHelperJs: which loop runs the script) */
    if (animType != ANIM_TYPE_MOVE)
        len = Append(request, len, sizeof(request), ",\"order\":\"task\"");
    len = Append(request, len, sizeof(request), "}");
    BeginRequest(id, request, len, animType, animId, 0);
}

/* Hands the core `id`'s begin (the request built): the animation in progress
 * from now on. Its result comes back with the frames (OnFrameEnd). */
static bool BeginRequest(const char *id, const char *request, int len, uint32_t animType, uint32_t animId,
                         uint8_t controller)
{
    struct CoreCommand c;

    if (sHdOff)
        return false; /* the drawing was over its budget: the original animations until the battle ends */

    if (len >= BFX_REQUEST_MAX - 1)
    {
        NoteRefused(id, "request too long");
        return false;
    }
    if (Refused(id))
        return false;
    memset(&c, 0, sizeof(c));
    c.request = malloc((size_t)len + 1);
    if (c.request == NULL)
        return false;
    memcpy(c.request, request, (size_t)len);
    c.request[len] = '\0';
    memset(&sAnim.status, 0, sizeof(sAnim.status));
    sAnim.active = true;
    sAnim.failed = false;
    sAnim.hdEnded = false;
    sAnim.ticked = false;
    sAnim.animType = animType;
    sAnim.animId = animId;
    sAnim.controller = controller;
    snprintf(sAnim.id, sizeof(sAnim.id), "%s", id);
    sAnim.gameFrames = 0;
    sAnim.inTail = false;
    sAnim.tailFrames = 0;
    sAnim.seq = 0;
    sAnim.serial++;
    c.kind = CORE_CMD_BEGIN;
    c.controller = controller;
    c.doubles = IsDoubleBattle();
    c.serial = sAnim.serial;
    c.sound = sSoundAtFrameBegin;
    snprintf(c.id, sizeof(c.id), "%s", id);
    CorePost(&c);
    return true;
}

void BattleFx_AnimInfo(struct BattleFxAnimInfo *info)
{
    struct CoreStatus now;
    const struct CoreStatus *st = &sAnim.status;

    if (sAnim.serial != 0 && sQ.lock != NULL && CoreStatusOf(sAnim.serial, &now))
        st = &now; /* the core's latest (its thread may be ahead of the frame end's) */

    memset(info, 0, sizeof(*info));
    info->serial = sAnim.serial;
    info->active = sAnim.active;
    info->failed = sAnim.failed;
    snprintf(info->id, sizeof(info->id), "%s", sAnim.id);
    info->plannedFrames = st->info.totalFrames;
    info->scriptFrames = st->info.frames;
    info->gameFrames = sAnim.gameFrames;
    info->tailFrames = sAnim.tailFrames;
    info->coreFrames = st->frames;
    info->beginMs = st->beginMs;
    info->stepMsTotal = st->stepMs;
    info->stepMsWorst = st->worstStepMs;
    info->stepMsWorstPlain = st->worstPlainStepMs;
    info->gcSteps = st->gcSteps;
    info->gcInBegin = st->gcInBegin;
}

unsigned BattleFx_Failures(void)
{
    return sFailures;
}

#ifdef BATTLEFX_TEST
struct BfxHost *BattleFx_TestHost(void)
{
    CoreWaitIdle(); /* the core's thread waits for a command: the host is the caller's until it posts one */
    return ReadyHost();
}
#endif

const char *BattleFx_CoreMoveId(uint32_t move)
{
    struct BfxHost *host = ReadyHost();
    return host ? BfxHost_MoveId(host, (int)move) : NULL;
}

void BattleFx_OnAnimTick(void)
{
    sAnim.ticked = true;
}

/* The core's news of the animation in progress (game thread). false: it is over for the HD layer
 * (refused, failed, or the core fell too far behind): its GBA effects show. */
static bool TakeCoreStatus(void)
{
    struct CoreStatus st;

    if (!sAnim.active || sAnim.failed)
        return false;
    if (CoreStatusOf(sAnim.serial, &st))
    {
        sAnim.status = st;
        if (st.refused)
        {
            NoteRefused(sAnim.id, st.error);
            sAnim.failed = true;
            EndAnimation("refused");
            return false;
        }
        if (st.failed)
        {
            FailAnimation(st.error);
            return false;
        }
        sAnim.hdEnded = st.ended;
    }
    if ((uint32_t)SDL_AtomicGet(&sTooSlow) == sAnim.serial && (uint32_t)SDL_AtomicGet(&sDrawSlow) == sAnim.serial)
    {
        sDrawSlowCount++;
        SDL_Log("Battle FX: %s: drawing its HD effects took %.1f ms a picture on this PC (the budget %d ms, battlefx "
                "settings.cfg drawBudgetMs: more would slow the battle); the rest of it plays as on the GBA", sAnim.id,
                SDL_AtomicGet(&sDrawSlowTenths) / 10.0, sCore.drawBudgetMs);
        if (sDrawSlowCount >= DRAW_SLOW_MAX && !sHdOff)
        {
            sHdOff = true;
            SDL_Log("Battle FX: the HD effects are off until this battle ends (%u animations over the drawing budget); "
                    "the original animations play", sDrawSlowCount);
        }
        sAnim.failed = true;
        EndAnimation("the drawing over its budget");
        return false;
    }
    if ((uint32_t)SDL_AtomicGet(&sTooSlow) == sAnim.serial)
    {
        sTooSlowCount++;
        SDL_Log("Battle FX: %s: the core fell more than %d frames behind the game (frame %d of the game's, %d of "
                "the core's); the rest of it plays as on the GBA", sAnim.id, CORE_MAX_LAG, sAnim.gameFrames,
                sAnim.status.frames);
        sAnim.failed = true;
        EndAnimation("the core fell behind");
        return false;
    }
    return true;
}

/* Game thread: the core's next frame for this game frame. */
static void PostStep(void)
{
    struct CoreCommand c;

    memset(&c, 0, sizeof(c));
    c.kind = CORE_CMD_STEP;
    c.serial = sAnim.serial;
    c.seq = ++sAnim.seq;
    c.sound = sSoundAtFrameBegin;
    CorePost(&c);
}

/* ── The controllers' own animations ─────────────────────────────────── */

#define CONTROLLER_MAX_FRAMES 600

void BattleFx_OnControllerAnim(uint32_t kind, uint32_t battler, uint32_t arg)
{
    if (!gMain.inBattle || battler >= MAX_BATTLERS_COUNT || !BattleFx_Enabled())
        return;
    if (sControllerPendingCount < MAX_CONTROLLER_REQUESTS)
    {
        sControllerPending[sControllerPendingCount].kind = (uint8_t)kind;
        sControllerPending[sControllerPendingCount].battler = (uint8_t)battler;
        sControllerPending[sControllerPendingCount].ball = (uint16_t)arg;
        sControllerPending[sControllerPendingCount].intro = gBattleSpritesDataPtr != NULL
            && gBattleSpritesDataPtr->animationData != NULL && gBattleSpritesDataPtr->animationData->introAnimActive;
    }
    sControllerPendingCount++;
}

/* The end of the frame in which the game began controller animations (the ball made / the stars'
 * tasks started): one alone is replayed from here, in step with the game's - the core is stepped
 * until its world is where the game's is at the end of this frame (the send-out: until its ball
 * exists too); more at once, or one while another plays, stay GBA. true: one began. */
static bool TakeControllerRequests(void)
{
    struct BfxHost *host = ReadyHost();
    int count = sControllerPendingCount;
    char request[BFX_REQUEST_MAX];
    const char *id;
    int len = 0, b;

    sControllerPendingCount = 0;
    if (host == NULL || IsContest())
        return false;
    if (sAnim.active && sAnim.controller)
    {
        if (sCore.log)
            SDL_Log("Battle FX: another controller animation began: %s stops; the rest plays as on the GBA", sAnim.id);
        sAnim.failed = true;
        EndAnimation("another controller animation began");
        return false;
    }
    if (sAnim.active)
        return false; /* a battle animation plays: these stay GBA */
    if (count > 1)
    {
        if (sCore.log)
            SDL_Log("Battle FX: %d controller animations began together; they play as on the GBA", count);
        return false;
    }
    if (EarlierEffectSprites())
    {
        /* another animation's sprites still move (the send-outs of a double battle's opening, played as on the
         * GBA): drawing this one would leave them out of the picture with it */
        if (sCore.log)
            SDL_Log("Battle FX: %s began while another animation's effect sprites are on screen; it plays as on the GBA",
                    sControllerPending[0].kind == BATTLEFX_CONTROLLER_SEND_OUT ? "CONTROLLER_SEND_OUT" : "CONTROLLER_SHINY");
        return false;
    }
    b = sControllerPending[0].battler;
    id = sControllerPending[0].kind == BATTLEFX_CONTROLLER_SEND_OUT ? "CONTROLLER_SEND_OUT" : "CONTROLLER_SHINY";
    len = Append(request, len, sizeof(request), "{\"anim\":\"%s\",\"inputs\":{\"ball\":%u,\"intro\":%s}", id,
                 (unsigned)sControllerPending[0].ball, sControllerPending[0].intro ? "true" : "false");
    len = AppendSetup(request, len, sizeof(request), (u32)b, (u32)b);
    len = Append(request, len, sizeof(request), "}");
    if (!BeginRequest(id, request, len, 0, sControllerPending[0].kind, sControllerPending[0].kind))
        return false;
#ifdef BATTLEFX_TEST
    if (sCore.sync && TakeCoreStatus() && sAnim.status.frames > 0)
        BattleFxTest_AfterStep(host, sAnim.serial, sAnim.status.frames);
#endif
    return true;
}

/* The frame the picture drawn at a frame boundary goes with: the game frame's record (the animation and
 * its frame number), kept for the boundary after the next one (records[1]: the picture shows the frame
 * before). Game thread writes, the boundary reads (the game thread parked). */
struct ShowRecord
{
    uint32_t serial; /* 0: none */
    int seq;
};
static struct ShowRecord sRecords[2];

/* Frames the core goes on replaying the effect sprites a script left moving after its end (the game's stars of a
 * caught ball last about 30). */
#define TAIL_MAX_FRAMES 120

/* Whether the game has an effect sprite (one the HD layer stands in for) in use. */
static bool GameHasEffectSprites(void)
{
    for (int i = 0; i < MAX_SPRITES; i++)
        if (gSprites[i].inUse && BattleFx_IsEffectSprite(&gSprites[i]))
            return true;
    return false;
}

void BattleFx_OnFrameEnd(void)
{
    struct BfxHost *host;
    bool begunNow = false, live;

    NoteRng1();
    sRecords[1] = sRecords[0];
    sRecords[0].serial = 0;
    sRecords[0].seq = 0;
    if (SDL_AtomicGet(&sCore.state) == CORE_IDLE)
        StartLoading();
    if (sControllerPendingCount > 0)
        begunNow = TakeControllerRequests();
    if (!sAnim.active)
        return;
    host = ReadyHost();
    if (!gMain.inBattle || host == NULL)
    {
        EndAnimation("the battle ended");
        return;
    }
    live = TakeCoreStatus();
    if (sAnim.controller && !begunNow)
        sAnim.ticked = true; /* the controllers' animations advance every frame */
    else if (sAnim.inTail)
    {
        sAnim.ticked = true; /* so do the sprites a script left moving */
        sAnim.tailFrames++;
    }
    if (sAnim.ticked)
    {
        sAnim.ticked = false;
        sAnim.gameFrames++;
        if (live && !sAnim.hdEnded)
        {
            PostStep();
#ifdef BATTLEFX_TEST
            if (sCore.sync)
            {
                live = TakeCoreStatus();
                if (live && sAnim.status.lastSeq == sAnim.seq && !sAnim.hdEnded)
                    BattleFxTest_AfterStep(host, sAnim.serial, sAnim.status.frames);
            }
#endif
        }
    }
    if (live)
    {
        sRecords[0].serial = sAnim.serial;
        sRecords[0].seq = sAnim.seq;
    }
    if (!sAnim.active)
        return;
    if (sAnim.controller)
    {
        /* Nothing in the game waits for these: they end with the core's (or at a
         * limit, should the game's send-out stall). */
        if (sAnim.hdEnded)
            EndAnimation("the core's animation ended");
        else if (sAnim.gameFrames > CONTROLLER_MAX_FRAMES)
            EndAnimation("too long");
    }
    /* The game's animation has ended: so has the core's (its last frame is
     * still shown with the next picture, the one of this frame) - unless the
     * script left effect sprites moving (a caught ball's stars): the core
     * replays them too, frame for frame, until the game's are gone. */
    else if (!gAnimScriptActive)
    {
        if (live && !sAnim.hdEnded && sAnim.tailFrames < TAIL_MAX_FRAMES && GameHasEffectSprites())
            sAnim.inTail = true;
        else if (sAnim.inTail)
            EndAnimation(sAnim.hdEnded ? "both ended, after the sprites the script left" : "the sprites the script left are gone");
        else
            EndAnimation(sAnim.hdEnded ? "both ended" : "the game's animation ended first");
    }
}

/* ── OAM: the effect sprites ─────────────────────────────────────────── */

void BattleFx_BeginOam(void)
{
    memset(sEffectOamPending, 0, sizeof(sEffectOamPending));
    memset(sBattlerOamPending, 0xFF, sizeof(sBattlerOamPending));
}

/* The graphics an animation's effect sprites use, all drawn by Battle FX's
 * kits (the core's data names them by these numbers, checked against the
 * game's constants by tools/battlefx/check_data.py): the ANIM_TAG_* sheets,
 * the Poke Balls of the ball throws (pokeball.c GFX_TAG_*_BALL, 55000-55027;
 * 55019 is also Smokescreen's cloud, battle_anim_smokescreen.c) and the balls'
 * opening particles (battle_anim_throw.c TAG_PARTICLES_*_BALL, 65030-65057).
 * Battler pictures and their copies (TAG_MOVE_EFFECT_MON_*, clones) stay:
 * Battle FX does not draw the Pokemon. Only left out while Battle FX shows a
 * frame of the animation playing. */
#define BALL_GFX_TAGS_FIRST 55000
#define BALL_GFX_TAGS_LAST 55027
#define BALL_PARTICLE_TAGS_FIRST 65030
#define BALL_PARTICLE_TAGS_LAST 65057

static bool IsEffectTag(u16 tag)
{
    return (tag >= ANIM_SPRITES_START && tag < ANIM_SPRITES_START + ANIM_TAG_COUNT)
        || (tag >= BALL_GFX_TAGS_FIRST && tag <= BALL_GFX_TAGS_LAST)
        || (tag >= BALL_PARTICLE_TAGS_FIRST && tag <= BALL_PARTICLE_TAGS_LAST);
}

/* Each sprite slot: the template its sprite was made with and that template's tile tag then (game thread). A
 * sprite whose template is not the one it was made with (a copy of another sprite put in the slot) has none. */
static struct
{
    const struct SpriteTemplate *template;
    u16 tag;
    u32 frame; /* sGameFrame when it was made */
} sSpriteMade[MAX_SPRITES + 1];

void BattleFx_OnSpriteMade(uint32_t index, const struct SpriteTemplate *template)
{
    if (index <= MAX_SPRITES && template != NULL)
    {
        sSpriteMade[index].template = template;
        sSpriteMade[index].tag = template->tileTag;
        sSpriteMade[index].frame = sGameFrame;
    }
}

/* Whether an effect sprite made before this game frame is in use: another animation's (a double battle's
 * send-outs, played as on the GBA, still throwing their balls' particles). */
static bool EarlierEffectSprites(void)
{
    for (int i = 0; i < MAX_SPRITES; i++)
        if (gSprites[i].inUse && BattleFx_IsEffectSprite(&gSprites[i]) && sSpriteMade[i].frame != sGameFrame)
            return true;
    return false;
}

uint16_t BattleFx_SpriteGfxTag(const struct Sprite *sprite)
{
    ptrdiff_t index;

    if (sprite == NULL)
        return TAG_NONE;
    index = sprite - gSprites;
    if (index < 0 || index > MAX_SPRITES || sSpriteMade[index].template != sprite->template)
        return TAG_NONE;
    return sSpriteMade[index].tag;
}

bool BattleFx_IsEffectSprite(const struct Sprite *sprite)
{
    return IsEffectTag(BattleFx_SpriteGfxTag(sprite));
}

void BattleFx_MarkOam(const struct Sprite *sprite, uint8_t first, uint8_t end)
{
    if (!gMain.inBattle)
        return;
    if (end == first + 1)
        for (u32 b = 0; b < gBattlersCount && b < MAX_BATTLERS_COUNT; b++)
            if (sprite == &gSprites[gBattlerSpriteIds[b]])
                sBattlerOamPending[b] = first;
    if (!BattleFx_IsEffectSprite(sprite))
        return;
    for (unsigned i = first; i < end && i < 128; ++i)
        sEffectOamPending[i >> 5] |= 1u << (i & 31);
}

void BattleFx_LatchOam(void)
{
    memcpy(sEffectOam, sEffectOamPending, sizeof(sEffectOam));
    memcpy(sBattlerOam, sBattlerOamPending, sizeof(sBattlerOam));
}

/* A battler's sprite as the GBA renderer draws it from OAM entry `index`
 * (gba_easy_draw.c DrawSprites: the same size, matrix and tile rules). */
static void DecodeBattler(struct BattleFxGameBattler *out, int index)
{
    const struct OamData *oam = &((const struct OamData *)OAM)[index];
    static const uint8_t sizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
    };
    const uint8_t *tiles = (const uint8_t *)VRAM_ + 0x10000;
    const uint16_t *pal;
    int w, h;

    out->shown = false;
    if (oam->shape > 2 || oam->objMode == ST_OAM_OBJ_WINDOW || oam->bpp
     || (!(oam->affineMode & 1) && (oam->affineMode & 2)) || !(REG_DISPCNT & DISPCNT_OBJ_ON)
     || !(REG_DISPCNT & DISPCNT_OBJ_1D_MAP))
        return;
    w = sizes[oam->shape][oam->size][0];
    h = sizes[oam->shape][oam->size][1];
    if (w > BFX_GAME_SPRITE_MAX || h > BFX_GAME_SPRITE_MAX)
        return;
    out->w = w;
    out->h = h;
    out->x = oam->x >= DISPLAY_WIDTH ? (int)oam->x - 512 : (int)oam->x;
    out->y = oam->y >= DISPLAY_HEIGHT ? (int)oam->y - 256 : (int)oam->y;
    out->affine = oam->affineMode & 1;
    out->doubleSize = out->affine && (oam->affineMode & 2);
    out->hflip = !out->affine && ((oam->matrixNum >> 3) & 1);
    out->vflip = !out->affine && ((oam->matrixNum >> 4) & 1);
    if (out->affine)
    {
        const struct OamData *mat = (const struct OamData *)OAM + oam->matrixNum * 4;
        out->m[0] = (int16_t)mat[0].affineParam;
        out->m[1] = (int16_t)mat[1].affineParam;
        out->m[2] = (int16_t)mat[2].affineParam;
        out->m[3] = (int16_t)mat[3].affineParam;
    }
    else
    {
        out->m[0] = 0x100;
        out->m[1] = 0;
        out->m[2] = 0;
        out->m[3] = 0x100;
    }
    out->objMode = oam->objMode;
    out->priority = oam->priority;
    pal = (const uint16_t *)(PLTT + 0x200) + oam->paletteNum * 16;
    for (int ty = 0; ty < h; ty++)
        for (int tx = 0; tx < w; tx++)
        {
            int block = (ty / 8) * (w / 8) + tx / 8;
            uint8_t byte = tiles[((block + oam->tileNum) * 32 + (ty % 8) * 4 + (tx % 8) / 2) & 0x7FFF];
            int idx = (tx & 1) ? byte >> 4 : byte & 0xF;
            uint8_t *px = &out->rgba[(ty * BFX_GAME_SPRITE_MAX + tx) * 4];

            if (idx == 0)
            {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            px[0] = (uint8_t)((pal[idx] & 0x1F) * 255 / 31);
            px[1] = (uint8_t)(((pal[idx] >> 5) & 0x1F) * 255 / 31);
            px[2] = (uint8_t)(((pal[idx] >> 10) & 0x1F) * 255 / 31);
            px[3] = 255;
        }
    out->shown = true;
}

/* ── Frame boundary ───────────────────────────────────────────────────── */

/* ── The frame boundary (main thread, the game thread parked) ───────── */

/* What the HD layer shows (main thread). */
static struct
{
    struct ShowRecord want; /* the frame for the picture being drawn */
    uint32_t shownSerial;   /* 0: none shown */
    int shownSeq;
    uint32_t startedSerial; /* the animation the HD layer has begun showing */
    uint32_t slowSerial;    /* ... and one the core fell too far behind on */
    /* the pictures of animation `statsSerial`: with the core's frame for them, with an older one (the core
     * behind; the most frames behind), with none (its GBA effects) */
    uint32_t statsSerial;
    int pictures, exact, older, maxLag, none, late;
    int waited; /* pictures the animation's effect sprites waited for the core's first frame */
    bool firstWaitDone;
    Uint32 waitedMs; /* the boundary waited for the core */
} sShow;

/* Main thread: how the pictures of the animation that just left the screen went (the log). */
static void ShowStats(uint32_t next)
{
    if (sShow.statsSerial != 0 && sShow.statsSerial != next && sCore.log && sShow.pictures > 0)
        SDL_Log("Battle FX: animation %u on screen: %d pictures, %d with the core's frame for them, %d with an older "
                "one (up to %d frames older; %d replaced later in the picture's time), %d without (%d of them waiting "
                "for its first); the pictures waited %u ms for the core in all", (unsigned)sShow.statsSerial,
                sShow.pictures, sShow.exact, sShow.older, sShow.maxLag, sShow.late, sShow.none, sShow.waited,
                (unsigned)sShow.waitedMs);
    if (sShow.statsSerial != next)
    {
        sShow.statsSerial = next;
        sShow.pictures = sShow.exact = sShow.older = sShow.maxLag = sShow.none = sShow.late = 0;
        sShow.waited = 0;
        sShow.firstWaitDone = false;
        sShow.waitedMs = 0;
    }
}

static void ShowNone(void)
{
    if (sShow.shownSerial != 0 || BattleFxGL_Showing())
        BattleFxGL_ShowPacket(NULL, true);
    sShow.shownSerial = 0;
    sShow.shownSeq = 0;
}

/* A ring of a slot's frame as the mirror has it, with what changed since the boundary last took it. */
static void RingView(struct BfxRing *r, const struct CoreRing *m)
{
    r->cap = m->cap;
    r->head = m->head;
    r->lo = m->lo;
    r->hi = m->hi;
    r->cleared = m->cleared;
    r->add = m->add;
    r->a = m->a[0];
    r->b = m->a[1];
    r->c = m->a[2];
    r->d = m->a[3];
    r->e = m->a[4];
    r->f = m->a[5];
}

/* Main thread, under sQ.lock: the newest frame the core has made of the wanted animation, not after the
 * wanted one. */
static struct CoreSlot *NewestFrame(void)
{
    struct CoreSlot *best = NULL;

    for (int i = 0; i < CORE_SLOTS; i++)
    {
        struct CoreSlot *slot = sQ.slots[i];

        if (slot->valid && slot->serial == sShow.want.serial && slot->seq <= sShow.want.seq
         && (best == NULL || slot->seq > best->seq))
            best = slot;
    }
    return best;
}

/* Main thread, under sQ.lock: waits (the lock let go meanwhile) until the core has made a frame of the
 * wanted animation at most `within` frames before the wanted one, or `ms` have passed. */
static struct CoreSlot *WaitForFrame(int within, Uint32 ms)
{
    Uint32 start = SDL_GetTicks();
    struct CoreSlot *best;

    while ((best = NewestFrame()) == NULL || sShow.want.seq - best->seq > within)
    {
        Uint32 spent = SDL_GetTicks() - start;

        if (spent >= ms || SDL_CondWaitTimeout(sQ.done, sQ.lock, ms - spent) < 0)
            break;
    }
    sShow.waitedMs += SDL_GetTicks() - start;
    return best;
}

/* Shows the core's frame for the picture being drawn (at the boundary) or, later in the picture's time, one for
 * it that the core has finished since (not at the boundary: only in place of an older frame of the same
 * animation already shown). */
static void ShowWanted(bool boundary)
{
    struct CoreSlot *best = NULL;

    if (sQ.lock == NULL)
        return;
    /* The drawing over its budget (BattleFxGL_DrawTooSlow: the HD layers' cost a picture, measured over this
     * animation's pictures so far): its GBA effects from this picture on, as when the core is behind; the game
     * thread ends it (TakeCoreStatus). */
    if (boundary && sCore.drawBudgetMs > 0 && sShow.want.serial != 0 && sShow.want.serial != sShow.slowSerial)
    {
        double ms = 0.0;

        if (BattleFxGL_DrawTooSlow(sShow.want.serial, sCore.drawBudgetMs, &ms))
        {
            sShow.slowSerial = sShow.want.serial;
            SDL_AtomicSet(&sDrawSlowTenths, (int)(ms * 10.0 + 0.5));
            SDL_AtomicSet(&sDrawSlow, (int)sShow.want.serial);
            SDL_AtomicSet(&sTooSlow, (int)sShow.want.serial);
        }
    }
    if (sShow.want.serial == 0 || sShow.want.serial == sShow.slowSerial)
    {
        if (boundary)
            ShowNone();
        return;
    }
    if (!boundary && sShow.shownSerial != sShow.want.serial)
        return;
    SDL_LockMutex(sQ.lock);
    best = NewestFrame();
    if (boundary && sShow.slowSerial != sShow.want.serial)
    {
        /* The core behind: the picture waits a little for it (the game thread is parked anyway). Its first
         * frame of the animation once, for up to CORE_FIRST_WAIT_MS (its begin plans the whole animation); once
         * shown, CORE_CATCHUP_WAIT_MS when it is 3 or more frames behind. */
        if (sShow.startedSerial != sShow.want.serial && !sShow.firstWaitDone
         && (best == NULL || sShow.want.seq - best->seq > 1))
        {
            sShow.firstWaitDone = true;
            best = WaitForFrame(1, CORE_FIRST_WAIT_MS);
        }
        else if (sShow.startedSerial == sShow.want.serial && best != NULL && sShow.want.seq - best->seq >= 3)
            best = WaitForFrame(1, CORE_CATCHUP_WAIT_MS);
    }
    if (best != NULL)
    {
        int lag = sShow.want.seq - best->seq;

        if (sShow.startedSerial != sShow.want.serial && lag > 1)
            best = NULL; /* not shown yet: once the core has caught up */
        else if (lag > CORE_MAX_LAG)
        {
            sShow.slowSerial = sShow.want.serial; /* the game thread ends it (TakeCoreStatus) */
            SDL_AtomicSet(&sTooSlow, (int)sShow.want.serial);
            best = NULL;
        }
    }
    if (boundary && best == NULL && sShow.startedSerial != sShow.want.serial && sShow.slowSerial != sShow.want.serial
     && ++sShow.waited > CORE_START_WAIT)
    {
        /* the core's first frame of it is not done after a few pictures: its GBA effects from now on */
        sShow.slowSerial = sShow.want.serial;
        SDL_AtomicSet(&sTooSlow, (int)sShow.want.serial);
    }
    if (boundary)
    {
        sShow.pictures++;
        if (best == NULL)
            sShow.none++;
        else if (best->seq == sShow.want.seq)
            sShow.exact++;
        else
        {
            sShow.older++;
            if (sShow.want.seq - best->seq > sShow.maxLag)
                sShow.maxLag = sShow.want.seq - best->seq;
        }
    }
    else if (best != NULL && best->seq == sShow.want.seq && sShow.shownSeq != best->seq)
        sShow.late++;
    if (best == NULL)
    {
        if (boundary)
            ShowNone();
    }
    else if (best->serial != sShow.shownSerial || best->seq != sShow.shownSeq)
    {
        struct BfxPacket p = best->packet;

        RingView(&p.partsAdd, &sQ.rings[0]);
        RingView(&p.partsAlpha, &sQ.rings[1]);
        p.partsAdd.count = best->packet.partsAdd.count;
        p.partsAlpha.count = best->packet.partsAlpha.count;
        BattleFxGL_ShowPacket(&p, true);
        for (int i = 0; i < 2; i++)
        {
            sQ.rings[i].lo = sQ.rings[i].hi = -1;
            sQ.rings[i].cleared = false;
        }
        sShow.shownSerial = best->serial;
        sShow.shownSeq = best->seq;
        sShow.startedSerial = best->serial;
    }
    SDL_UnlockMutex(sQ.lock);
}

void BattleFx_LateFrame(void)
{
    if (sShow.shownSerial != 0 && sShow.shownSerial == sShow.want.serial && sShow.shownSeq < sShow.want.seq)
        ShowWanted(false);
}

void BattleFx_FrameBoundary(bool view3d, uint32_t skipObj[4], uint8_t *hideBg)
{
    /* the picture drawn now is the game's previous frame (OAM reaches the screen at the VBlank after the
     * frame that built it): the core's frame for that game frame goes with it */
    bool waiting;

    sShow.want = sRecords[1];
    ShowStats(sShow.want.serial);
    ShowWanted(true);
    waiting = sShow.want.serial != 0 && sShow.want.serial != sShow.slowSerial && sShow.startedSerial != sShow.want.serial;
    if (!view3d)
        return;
#ifdef BATTLEFX_TEST
    if (!BattleFxTest_ViewHd())
        return;
#endif
    if (!BattleFxGL_Showing())
    {
        /* the core's first frame of the animation is not done yet (its begin plans the whole animation): the
         * effect sprites wait for it - a few pictures at most, then the animation plays its GBA effects
         * (ShowWanted) - rather than show as GBA pixel art and then turn HD */
        if (waiting)
            for (int i = 0; i < 4; i++)
                skipObj[i] |= sEffectOam[i];
        return;
    }
    *hideBg |= BattleFxGL_ShowingHiddenBgs();
    for (u32 b = 0; b < MAX_BATTLERS_COUNT; b++)
        if (sBattlerOam[b] < 128)
            DecodeBattler(&sGameBattlers[b], sBattlerOam[b]);
        else
            sGameBattlers[b].shown = false;
    BattleFxGL_TakeBattlers(sGameBattlers);
    if (BattleFxGL_ShowingPixelEffects() > 0)
        return; /* the core still draws some effect sprite as GBA pixel art: the game's stay */
    for (int i = 0; i < 4; i++)
        skipObj[i] |= sEffectOam[i];
}

#endif // ENABLE_DIORAMA
