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
#include "main.h"
#include "move.h"
#include "pokemon.h"
#include "sprite.h"
#include "constants/battle_anim.h"
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
    char id[48];
    struct BfxBeginResult begun;
    double beginMs, stepMs, worstStepMs;
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

/* The request the core needs to replay the game's own animation: the battlers
 * as the scripts see them (who is present, each sprite's picture metrics) and
 * what the battle engine handed the animation. */
static int AppendSetup(char *buf, int len, int size)
{
    bool doubles = IsDoubleBattle();

    len = Append(buf, len, size, ",\"attacker\":%u,\"target\":%u,\"double\":%s,\"present\":[",
                 (unsigned)gBattleAnimAttacker, (unsigned)gBattleAnimTarget, doubles ? "true" : "false");
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
                            ^ (gBattleAnimAttacker << 4) ^ gBattleAnimTarget));
    return len;
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
    if (sCore.log)
        SDL_Log("Battle FX: %s ended (%s): the game's animation %d frames, the core's %d of %d%s; begin %.1f ms, "
                "a frame %.2f ms on average, %.2f ms at worst",
                sAnim.id, why, sAnim.gameFrames, sAnim.hdFrames, sAnim.begun.totalFrames,
                sAnim.hdEnded ? " (it had ended)" : "", sAnim.beginMs,
                sAnim.hdFrames ? sAnim.stepMs / sAnim.hdFrames : 0.0, sAnim.worstStepMs);
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

void BattleFx_OnLaunch(uint32_t animType, uint32_t animId, const void *script)
{
    struct BfxHost *host;
    char request[4096];
    int len = 0;
    const char *id;
    Uint64 t0;

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
                     ",\"turn\":%u,\"movePower\":%u,\"damage\":%d,\"friendship\":%u,\"weather\":%u",
                     (unsigned)gAnimMoveTurn, (unsigned)gAnimMovePower, (int)gAnimMoveDmg,
                     (unsigned)gAnimFriendship, (unsigned)gWeatherMoveAnim);
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
        if (gBattleSpritesDataPtr != NULL && gBattleSpritesDataPtr->animationData != NULL)
            len = Append(request, len, sizeof(request), "\"animArg\":%u",
                         (unsigned)gBattleSpritesDataPtr->animationData->animArg);
        len = Append(request, len, sizeof(request), "}");
    }
    len = AppendSetup(request, len, sizeof(request));
    len = Append(request, len, sizeof(request), "}");
    if (len >= (int)sizeof(request) - 1)
    {
        NoteRefused(id, "request too long");
        return;
    }
    if (Refused(id))
        return;
    t0 = SDL_GetPerformanceCounter();
    if (!BfxHost_Begin(host, request, &sAnim.begun))
    {
        NoteRefused(id, BfxHost_LastError(host));
        return;
    }
    memset(&sAnim.packet, 0, sizeof(sAnim.packet));
    sAnim.active = true;
    sAnim.failed = false;
    sAnim.hdEnded = false;
    sAnim.ticked = false;
    sAnim.animType = animType;
    sAnim.animId = animId;
    snprintf(sAnim.id, sizeof(sAnim.id), "%s", id);
    sAnim.beginMs = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
    sAnim.stepMs = 0;
    sAnim.worstStepMs = 0;
    sAnim.gameFrames = 0;
    sAnim.hdFrames = 0;
    sAnim.serial++;
    if (sCore.log)
        SDL_Log("Battle FX: %s (battler %u -> %u%s) begins: %d frames planned, begin %.1f ms", id,
                (unsigned)gBattleAnimAttacker, (unsigned)gBattleAnimTarget, IsDoubleBattle() ? ", double battle" : "",
                sAnim.begun.totalFrames, sAnim.beginMs);
}

void BattleFx_AnimInfo(struct BattleFxAnimInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->serial = sAnim.serial;
    info->active = sAnim.active;
    info->failed = sAnim.failed;
    snprintf(info->id, sizeof(info->id), "%s", sAnim.id);
    info->plannedFrames = sAnim.begun.totalFrames;
    info->gameFrames = sAnim.gameFrames;
    info->coreFrames = sAnim.hdFrames;
    info->beginMs = sAnim.beginMs;
    info->stepMsTotal = sAnim.stepMs;
    info->stepMsWorst = sAnim.worstStepMs;
}

unsigned BattleFx_Failures(void)
{
    return sFailures;
}

const char *BattleFx_CoreMoveId(uint32_t move)
{
    struct BfxHost *host = ReadyHost();
    return host ? BfxHost_MoveId(host, (int)move) : NULL;
}

void BattleFx_OnAnimTick(void)
{
    sAnim.ticked = true;
}

void BattleFx_OnFrameEnd(void)
{
    struct BfxHost *host;

    if (SDL_AtomicGet(&sCore.state) == CORE_IDLE)
        StartLoading();
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
    if (sAnim.ticked)
    {
        sAnim.ticked = false;
        sAnim.gameFrames++;
        if (!sAnim.hdEnded)
        {
            Uint64 t0 = SDL_GetPerformanceCounter();
            int r = BfxHost_Step(host);
            double ms;

            if (r < 0)
            {
                FailAnimation("step");
                sPublished.valid = false;
                return;
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
                    sPublished.valid = false;
                    return;
                }
                sAnim.packet.hideBg = (uint8_t)(BfxHost_CallGlobalInt(host, "BFX_GAME_HIDDEN_BGS", 0) & 0x06);
                sAnim.hdFrames++;
                sAnim.packetValid = true;
                BfxHost_DrainSounds(host);
#ifdef BATTLEFX_TEST
                BattleFxTest_AfterStep(host, sAnim.serial, sAnim.hdFrames);
#endif
            }
            ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
            sAnim.stepMs += ms;
            if (ms > sAnim.worstStepMs)
                sAnim.worstStepMs = ms;
        }
    }
    sPublished.valid = sAnim.packetValid;
    if (sAnim.packetValid)
        sPublished.packet = sAnim.packet;
    /* The game's animation has ended: so has the core's (its last frame is
     * still published, for the picture of this frame). */
    if (!gAnimScriptActive)
        EndAnimation(sAnim.hdEnded ? "both ended" : "the game's animation ended first");
}

/* ── OAM: the effect sprites ─────────────────────────────────────────── */

void BattleFx_BeginOam(void)
{
    memset(sEffectOamPending, 0, sizeof(sEffectOamPending));
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

void BattleFx_MarkOam(const struct Sprite *sprite, uint8_t first, uint8_t end)
{
    if (!gMain.inBattle || sprite->template == NULL || !IsEffectTag(sprite->template->tileTag))
        return;
    for (unsigned i = first; i < end && i < 128; ++i)
        sEffectOamPending[i >> 5] |= 1u << (i & 31);
}

void BattleFx_LatchOam(void)
{
    memcpy(sEffectOam, sEffectOamPending, sizeof(sEffectOam));
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
    if (BattleFxGL_ShowingPixelEffects() > 0)
        return; /* the core still draws some effect sprite as GBA pixel art: the game's stay */
    for (int i = 0; i < 4; i++)
        skipObj[i] |= sEffectOam[i];
}

#endif // ENABLE_DIORAMA
