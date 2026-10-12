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

/* Settings and the core (loaded once, on a thread of its own). */
static struct
{
    bool settingsRead;
    bool enabled;
    bool log;
    char dir[1024];
    SDL_atomic_t state; /* enum CoreState */
    SDL_Thread *loader;
    struct BfxHost *host;
    double loadMs;
} sCore;

/* The animation in progress (game thread). */
static struct
{
    bool active;
    bool hdEnded;       /* the core's animation ended (the game's may go on) */
    bool ticked;        /* the game's animation advanced this frame */
    bool packetValid;   /* `packet` holds the core's latest frame */
    bool failed;        /* the last animation failed */
    uint32_t animType, animId;
    uint8_t controller;     /* BATTLEFX_CONTROLLER_*: stepped every frame, ends with the core */
    bool aligning;          /* stepping a controller animation's core up to the game's frame */
    char id[48];
    struct BfxBeginResult begun;
    double beginMs, stepMs, worstStepMs;
    double worstPlainStepMs;   /* ... of the steps without a QuickJS garbage collection */
    int gcSteps;               /* steps during which QuickJS collected garbage */
    bool gcInBegin;
    int gameFrames, hdFrames;
    struct BfxPacket packet;
    uint32_t serial;
} sAnim;

/* What the game thread published for the frame boundary: the core's frame for
 * the game frame that just ended, if any. */
static struct
{
    bool valid;
    struct BfxPacket packet;
} sPublished;

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
    "    return !(playing(w, this.frame) && (this.seEndWait = (this.seEndWait | 0) + 1) <= 90);\n"
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
    "  B.begin = function (req) { taken = req && req.spritesInUse != null ? req.spritesInUse | 0 : null; return begin.call(this, req); };\n"
    "  function effect(g) { const D = RT.data; let top = 10000; if (D && D.tagIds) for (const k in D.tagIds) { const v = D.tagIds[k] | 0; if (v < 20000 && v > top) top = v; }\n"
    "    return (g >= 10000 && g <= top) || (g >= 55000 && g <= 55027) || (g >= 65030 && g <= 65057); }\n"
    "  P.reset = function () { reset.apply(this, arguments); const w = this.w; if (!w || taken == null || !w.sprites[0]) return;\n"
    "    const Sprite = w.sprites[0].constructor; let n = 0;\n"
    "    for (let i = 0; i < 64; i++) { const s = w.sprites[i]; if (s && s.inUse && !effect(s.tag | 0)) n++; }\n"
    "    for (let i = 63, k = taken - n; i >= 0 && k > 0; i--) { if (w.sprites[i] && w.sprites[i].inUse) continue;\n"
    "      const s = new Sprite(); s.id = i; s.uid = -1 - i; s.kind = 'reserved'; s.invisible = true; w.sprites[i] = s; k--; } };\n"
    "  P.activeSprites = function () { let n = active.apply(this, arguments); const w = this.w;\n"
    "    if (w) for (let i = 0; i < 64; i++) { const s = w.sprites[i]; if (s && s.inUse && s.kind === 'reserved') n--; } return n; };\n"
    "})();\n"
    /* A move's script branches in the core's data, in its order: "kind|task|arg|value;..." */
    "globalThis.BFX_GAME_VARIANTS = function (id) {\n"
    "  const D = globalThis.BFX_RT && globalThis.BFX_RT.data; if (!D) return '';\n"
    "  const m = (D.moves || []).find((x) => x.id === id); if (!m || !m.var) return '';\n"
    "  return m.var.map((v) => [v.kind, v.task || '', v.arg == null ? -1 : v.arg, v.value == null ? 0 : v.value].join('|')).join(';');\n"
    "};\n";

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
    sCore.host = host;
    sCore.loadMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    SDL_Log("Battle FX: core ready in %.0f ms (%s, %u + %u bytes)", sCore.loadMs, sCore.dir,
            (unsigned)coreSize, (unsigned)dataSize);
    SDL_AtomicSet(&sCore.state, CORE_READY);
    return 0;
}

static void StartLoading(void)
{
    ReadSettings();
    if (!sCore.enabled || SDL_AtomicGet(&sCore.state) != CORE_IDLE)
        return;
    SDL_AtomicSet(&sCore.state, CORE_LOADING);
    sCore.loader = SDL_CreateThreadWithStackSize(LoadCore, "BattleFxLoad", LOADER_STACK_BYTES, NULL);
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
    return len;
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
    len = Append(buf, len, size, ",\"safari\":%s,\"safariReaction\":%u",
                 (gBattleTypeFlags & BATTLE_TYPE_SAFARI) ? "true" : "false",
                 gBattleCommunication[MULTISTRING_CHOOSER] < NUM_SAFARI_REACTIONS
                     ? (unsigned)gBattleCommunication[MULTISTRING_CHOOSER] : 0u);
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
 * instead of three) is the one case no variant matches exactly. */
#define MAX_VARIANT_MOVES 1024
static char *sVariants[MAX_VARIANT_MOVES];

static int GameVariant(struct BfxHost *host, u32 move, const char *id)
{
    char arg[64], *list, *entry, *save = NULL;
    int index = 0, chosen = -1;
    bool unknown = false;

    if (move >= MAX_VARIANT_MOVES)
        return -1;
    if (sVariants[move] == NULL)
    {
        snprintf(arg, sizeof(arg), "\"%s\"", id);
        sVariants[move] = BfxHost_CallGlobal(host, "BFX_GAME_VARIANTS", arg);
        if (sVariants[move] == NULL)
            sVariants[move] = SDL_strdup("");
    }
    if (sVariants[move] == NULL || sVariants[move][0] == '\0')
        return -1;
    list = SDL_strdup(sVariants[move]);
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

static void EndAnimation(const char *why)
{
    if (!sAnim.active)
        return;
    sAnim.active = false;
#ifdef BATTLEFX_TEST
    BattleFxTest_OnAnimEnd(sAnim.serial);
#endif
    if (sCore.log)
        SDL_Log("Battle FX: %s ended (%s): the game's animation %d frames, the core's %d of %d%s; begin %.1f ms%s, "
                "a frame %.2f ms on average, %.2f ms at worst (%.2f without a garbage collection; %d with one)",
                sAnim.id, why, sAnim.gameFrames, sAnim.hdFrames, sAnim.begun.totalFrames,
                sAnim.hdEnded ? " (it had ended)" : "", sAnim.beginMs, sAnim.gcInBegin ? " (with a garbage collection)" : "",
                sAnim.hdFrames ? sAnim.stepMs / sAnim.hdFrames : 0.0, sAnim.worstStepMs, sAnim.worstPlainStepMs,
                sAnim.gcSteps);
}

static void FailAnimation(const char *what)
{
    struct BfxHost *host = ReadyHost();

    sFailures++;
    SDL_Log("Battle FX: %s failed at frame %d (%s: %s); the rest of it plays as on the GBA", sAnim.id,
            sAnim.hdFrames, what, host ? BfxHost_LastError(host) : "no core");
    sAnim.hdEnded = true;
    sAnim.packetValid = false;
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
static int sSoundSent = -1;

void BattleFx_OnFrameBegin(void)
{
    u32 se1, se2;

    if (!gMain.inBattle)
        return;
    se1 = gMPlayInfo_SE1.status;
    se2 = gMPlayInfo_SE2.status;
    sSoundAtFrameBegin = (IsCryPlaying() ? 1u : 0u)
                       | ((se1 & MUSICPLAYER_STATUS_PAUSE) ? 2u : 0u) | ((se1 & MUSICPLAYER_STATUS_TRACK) ? 4u : 0u)
                       | ((se2 & MUSICPLAYER_STATUS_PAUSE) ? 8u : 0u) | ((se2 & MUSICPLAYER_STATUS_TRACK) ? 16u : 0u);
}

static void FeedSound(struct BfxHost *host)
{
    if ((int)sSoundAtFrameBegin == sSoundSent)
        return;
    BfxHost_CallGlobalIntArg(host, "BFX_GAME_SOUND", (int)sSoundAtFrameBegin, 0);
    sSoundSent = (int)sSoundAtFrameBegin;
}

#define BFX_REQUEST_MAX 4096
static bool BeginRequest(struct BfxHost *host, const char *id, const char *request, int len, uint32_t animType,
                         uint32_t animId, uint8_t controller);

void BattleFx_OnLaunch(uint32_t animType, uint32_t animId, const void *script)
{
    struct BfxHost *host;
    char request[BFX_REQUEST_MAX];
    int len = 0;
    const char *id;

    EndAnimation("another animation started");
    sAnim.packetValid = false;
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
        if (gAnimDisableStructPtr != NULL)
            len = Append(request, len, sizeof(request), ",\"rolloutTimer\":%u", (unsigned)gAnimDisableStructPtr->rolloutTimer);
        if (CalledByMirrorMove(animId))
            len = Append(request, len, sizeof(request), ",\"calledBy\":\"MOVE_MIRROR_MOVE\"");
        {
            int variant = GameVariant(host, animId, id);
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
    len = Append(request, len, sizeof(request), "}");
    BeginRequest(host, id, request, len, animType, animId, 0);
}

/* Begins the core's replay of `id` (the request built): the animation in
 * progress from now on. */
static bool BeginRequest(struct BfxHost *host, const char *id, const char *request, int len, uint32_t animType,
                         uint32_t animId, uint8_t controller)
{
    Uint64 t0;
    unsigned gc0;

    if (len >= BFX_REQUEST_MAX - 1)
    {
        NoteRefused(id, "request too long");
        return false;
    }
    if (Refused(id))
        return false;
    t0 = SDL_GetPerformanceCounter();
    gc0 = BfxHost_GcCount(host);
    if (!BfxHost_Begin(host, request, &sAnim.begun))
    {
        NoteRefused(id, BfxHost_LastError(host));
        return false;
    }
    sAnim.gcInBegin = BfxHost_GcCount(host) != gc0;
    memset(&sAnim.packet, 0, sizeof(sAnim.packet));
    sAnim.active = true;
    sAnim.failed = false;
    sAnim.hdEnded = false;
    sAnim.ticked = false;
    sAnim.animType = animType;
    sAnim.animId = animId;
    sAnim.controller = controller;
    snprintf(sAnim.id, sizeof(sAnim.id), "%s", id);
    sAnim.beginMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    sAnim.stepMs = 0;
    sAnim.worstStepMs = 0;
    sAnim.worstPlainStepMs = 0;
    sAnim.gcSteps = 0;
    sAnim.gameFrames = 0;
    sAnim.hdFrames = 0;
    sAnim.serial++;
    sSoundSent = -1; /* the core's next step gets the sound state again */
    if (sCore.log)
        SDL_Log("Battle FX: %s (battler %d -> %d%s) begins: %d frames planned, begin %.1f ms", id,
                sAnim.begun.attacker, sAnim.begun.target, IsDoubleBattle() ? ", double battle" : "",
                sAnim.begun.totalFrames, sAnim.beginMs);
    return true;
}

void BattleFx_AnimInfo(struct BattleFxAnimInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->serial = sAnim.serial;
    info->active = sAnim.active;
    info->failed = sAnim.failed;
    snprintf(info->id, sizeof(info->id), "%s", sAnim.id);
    info->plannedFrames = sAnim.begun.totalFrames;
    info->scriptFrames = sAnim.begun.frames;
    info->gameFrames = sAnim.gameFrames;
    info->coreFrames = sAnim.hdFrames;
    info->beginMs = sAnim.beginMs;
    info->stepMsTotal = sAnim.stepMs;
    info->stepMsWorst = sAnim.worstStepMs;
    info->stepMsWorstPlain = sAnim.worstPlainStepMs;
    info->gcSteps = sAnim.gcSteps;
    info->gcInBegin = sAnim.gcInBegin;
}

unsigned BattleFx_Failures(void)
{
    return sFailures;
}

#ifdef BATTLEFX_TEST
struct BfxHost *BattleFx_TestHost(void)
{
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

/* One step of the core and its frame, for the game frame that just ended
 * (game thread). false: the animation failed (its GBA effects show). */
static bool StepCore(struct BfxHost *host)
{
    Uint64 t0, t1 = 0, t2 = 0;
    unsigned gc0;
    int r;
    double ms;

    FeedSound(host);
    t0 = SDL_GetPerformanceCounter();
    gc0 = BfxHost_GcCount(host);
    r = BfxHost_Step(host);

    t1 = SDL_GetPerformanceCounter();

    if (r < 0)
    {
        FailAnimation("step");
        return false;
    }
    if (r == 0)
    {
        sAnim.hdEnded = true;
        sAnim.packetValid = false;
    }
    else
    {
        sAnim.packet.serial = sAnim.serial;
        if (!BfxHost_Frame(host, -1, &sAnim.packet))
        {
            FailAnimation("frame");
            return false;
        }
        t2 = SDL_GetPerformanceCounter();
        sAnim.packet.hideBg = (uint8_t)(BfxHost_CallGlobalInt(host, "BFX_GAME_HIDDEN_BGS", 0) & 0x06);
        sAnim.hdFrames++;
        sAnim.packetValid = true;
        BfxHost_DrainSounds(host);
#ifdef BATTLEFX_TEST
        if (!sAnim.aligning)
            BattleFxTest_AfterStep(host, sAnim.serial, sAnim.hdFrames);
#endif
    }
    ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    sAnim.stepMs += ms;
    if (ms > sAnim.worstStepMs)
        sAnim.worstStepMs = ms;
    if (sCore.log && ms > 20.0)
    {
        double f = 1000.0 / SDL_GetPerformanceFrequency();
        SDL_Log("Battle FX: %s frame %d took %.1f ms (step %.1f, frame packet %.1f%s)", sAnim.id, sAnim.hdFrames, ms,
                (t1 - t0) * f, t2 ? (t2 - t1) * f : 0.0, BfxHost_GcCount(host) != gc0 ? ", with a garbage collection" : "");
    }
    if (BfxHost_GcCount(host) != gc0)
        sAnim.gcSteps++;
    else if (ms > sAnim.worstPlainStepMs)
        sAnim.worstPlainStepMs = ms;
    return true;
}

/* ── The controllers' own animations ─────────────────────────────────── */

#define CONTROLLER_MAX_FRAMES 600
#define CONTROLLER_ALIGN_MAX 8

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
        sAnim.packetValid = false;
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
    b = sControllerPending[0].battler;
    id = sControllerPending[0].kind == BATTLEFX_CONTROLLER_SEND_OUT ? "CONTROLLER_SEND_OUT" : "CONTROLLER_SHINY";
    len = Append(request, len, sizeof(request), "{\"anim\":\"%s\",\"inputs\":{\"ball\":%u,\"intro\":%s}", id,
                 (unsigned)sControllerPending[0].ball, sControllerPending[0].intro ? "true" : "false");
    len = AppendSetup(request, len, sizeof(request), (u32)b, (u32)b);
    len = Append(request, len, sizeof(request), "}");
    if (!BeginRequest(host, id, request, len, 0, sControllerPending[0].kind, sControllerPending[0].kind))
        return false;
    /* in step: the core's frames up to this one */
    sAnim.aligning = true;
    for (int i = 0; i < CONTROLLER_ALIGN_MAX && !sAnim.hdEnded; i++)
    {
        if (!StepCore(host))
        {
            sAnim.aligning = false;
            return false;
        }
        if (sAnim.controller != BATTLEFX_CONTROLLER_SEND_OUT || BfxHost_CallGlobalInt(host, "BFX_GAME_HAS_BALL", 0))
            break;
    }
    sAnim.aligning = false;
#ifdef BATTLEFX_TEST
    if (sAnim.packetValid)
        BattleFxTest_AfterStep(host, sAnim.serial, sAnim.hdFrames);
#endif
    return true;
}

void BattleFx_OnFrameEnd(void)
{
    struct BfxHost *host;
    bool begunNow = false;

    NoteRng1();
    if (SDL_AtomicGet(&sCore.state) == CORE_IDLE)
        StartLoading();
    if (sControllerPendingCount > 0)
        begunNow = TakeControllerRequests();
    if (!sAnim.active)
    {
        sPublished.valid = false;
        return;
    }
    host = ReadyHost();
    if (!gMain.inBattle || host == NULL)
    {
        EndAnimation("the battle ended");
        sPublished.valid = false;
        return;
    }
    if (sAnim.controller && !begunNow)
        sAnim.ticked = true; /* the controllers' animations advance every frame */
    if (sAnim.ticked)
    {
        sAnim.ticked = false;
        sAnim.gameFrames++;
        if (!sAnim.hdEnded && !StepCore(host))
        {
            sPublished.valid = false;
            return;
        }
    }
    sPublished.valid = sAnim.packetValid;
    if (sAnim.packetValid)
        sPublished.packet = sAnim.packet;
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
     * still published, for the picture of this frame). */
    else if (!gAnimScriptActive)
        EndAnimation(sAnim.hdEnded ? "both ended" : "the game's animation ended first");
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

uint16_t BattleFx_SpriteGfxTag(const struct Sprite *sprite)
{
    /* sprite.c CreateSpriteAt: a sprite of a sheet keeps its first tile (sheetTileStart); the sheets
     * loaded keep their tags (GetSpriteTileTagByTileStart). A sprite without one owns its tiles. */
    if (sprite == NULL || !sprite->usingSheet)
        return TAG_NONE;
    return GetSpriteTileTagByTileStart(sprite->sheetTileStart);
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

void BattleFx_FrameBoundary(bool view3d, uint32_t skipObj[4], uint8_t *hideBg)
{
    BattleFxGL_TakePacket(sPublished.valid ? &sPublished.packet : NULL);
    if (!view3d || !BattleFxGL_Showing())
        return;
#ifdef BATTLEFX_TEST
    if (!BattleFxTest_ViewHd())
        return;
#endif
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
