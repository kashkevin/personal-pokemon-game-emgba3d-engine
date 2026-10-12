#if defined(ENABLE_DIORAMA) && defined(BATTLEFX_TEST)

/*
 * The Battle FX test driver (include/diorama/battle_fx_test.h). TEST BUILDS
 * ONLY. Plays move animations at the action menu through the game's own
 * animation system and records, per animation, frame counts, frame-by-frame
 * parity of the effect sprites (game against the Battle FX core's replay),
 * window captures and the core's cost.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_gfx_sfx_util.h"
#include "battle_util.h"
#include "main.h"
#include "m4a.h"
#include "move.h"
#include "pokeball.h"
#include "sound.h"
#include "pokemon.h"
#include "palette.h"
#include "random.h"
#include "sprite.h"
#include "task.h"
#include "constants/battle_anim.h"
#include "constants/moves.h"
#include "diorama/battle_fx.h"
#include "diorama/battle_fx_host.h"
#include "diorama/battle_fx_render.h"
#include "diorama/battle_fx_test.h"
#include "platform/png_write.h"

#define MAX_LIST 1024
#define MAX_SHOTS 16
#define SETTLE_FRAMES 24
#define MAX_ANIM_FRAMES 3000
/* t.types[]: 0 a move; 1 + ANIM_TYPE_* a battle animation; TYPE_CONTROLLER + BATTLEFX_CONTROLLER_* one of
 * the controllers' own (the driver starts it as the controller does: the game's DoPokeballSendOutAnimation
 * or TryShinyAnimation, with a shiny copy of the battler's Pokemon) */
#define TYPE_CONTROLLER 16

enum State
{
    TEST_OFF,
    TEST_WAIT,     /* the first animation after `delay` action-menu frames */
    TEST_LAUNCH,
    TEST_RUN,
    TEST_SETTLE,
    TEST_DONE,
};

/* PKMN_BFX_TEST_SE_TABLE=<file>: each sound effect's length and music player (gSongTable ms), measured by
 * playing each alone at the action menu: the frames after the one PlaySE is called in during which its player
 * has tracks playing (m4a sets them at the next vblank, MPlayStart leaves the status 0 until then). The core
 * estimates the scripts' waitsound and end from it while it plans an animation; replaying one, it reads the
 * game's own music players (bfx_game.c FeedSound). */
static struct
{
    bool on, done;
    char path[512];
    u16 songs[600];
    char names[600][40];
    int count, next, frames;
    bool playing;
    FILE *f;
} sSe;

static struct
{
    bool init;
    bool viewHd;
    bool exitAfter;
    enum State state;
    char out[512];
    char movesSpec[8192];
    u16 list[MAX_LIST];     /* move or battle animation number */
    u8 types[MAX_LIST];     /* 0: a move; else 1 + ANIM_TYPE_* of a battle animation */
    int count, next;
    char animsSpec[8192];
    char from[48];          /* PKMN_BFX_TEST_FROM: the list starts at this animation */
    int attacker, target, turn, delay, wait;
    bool autoShots;
    int shots[MAX_SHOTS], shotCount;
    struct LinkBattleAnim disable;
    /* the animation playing */
    u16 move;
    u8 type;                /* as types[] */
    char name[48];
    int curTurn, maxTurn;
    bool turnsJs, spritesJs;
    int frames;
    uint32_t serial;
    char id[48];
    /* parity */
    int compared, equal, firstBad, sprites;
    char badGame[512], badCore[512];
    struct BfxHost *host; /* the core (seen in AfterStep; the game thread's) */
    /* captures wanted (main thread reads): the animation's serial and core frames */
    SDL_atomic_t wantSerial;
    int want[MAX_SHOTS], wantCount;
    bool written[MAX_SHOTS];
    char shotNames[MAX_SHOTS][96];
    /* PKMN_BFX_TEST_TRACE=1: every frame's sprite lists (game, core) in trace_<id>_t<turn>.txt */
    bool trace;
    FILE *traceFile;
    u32 traceCtr0;
    /* frames after the core's animation ended in which the game still showed effect sprites */
    int tailFrames, tailSprites;
    int gameDoneAt;         /* a controller animation: the frame the game's flags said it was done */
    struct Pokemon shinyMon; /* CONTROLLER_SHINY: a shiny copy of the battler's Pokemon */
    char tailFirst[256];
    /* PKMN_BFX_TEST_REAL_PARITY=1: the parity of every animation the battle itself plays (no gallery),
     * a line each in real_report.jsonl */
    bool realParity;
    uint32_t rpSerial;
    int rpCompared, rpEqual, rpFirstBad, rpSprites;
    char rpId[48], rpBadGame[512], rpBadCore[512];
    FILE *rpFile;
    /* PKMN_BFX_TEST_REAL_SHOTS=1: captures of every animation the battle itself plays */
    bool realShots;
    uint32_t realSerial;
    int realWant[3];
    bool realWritten[3];
    /* results */
    int played, failedCount, parityFailures;
    FILE *report;
} t;

static const char kSpritesJs[] =
    "globalThis.BFX_TEST_RNG_CTR = function () { const w = BFX_RT.world(); return w && w.gameRng2 ? (w.gameRng2[3] >>> 0) : 0; };\n"
    "globalThis.BFX_TEST_STATE = function () { const w = BFX_RT.world(); if (!w) return ''; let t = 0; for (const k of w.tasks) if (k && k.isActive) t++;\n"
    "  let n = 0; for (let i = 0; i < 64; i++) if (w.sprites[i] && w.sprites[i].inUse) n++;\n"
    "  return 'visual ' + w.visualCount + ' sound ' + w.soundCount + ' tasks ' + t + ' fade ' + (w.fade.active ? 1 : 0) + ' y ' + w.fade.y + ' sprites ' + n; };\n"
    /* the core's sprites as the game's are listed (GameSprites): x + x2 (+ the coordinate offset), no subsprite
     * set offset (the core's snapshot adds one for its renderer) */
    "globalThis.BFX_TEST_SPRITES = function () {\n"
    "  const W = BFX_RT.world(); const out = []; if (!W) return '';\n"
    "  for (const s of W.sprites) {\n"
    "    if (!s || !s.inUse || s.invisible) continue; const g = s.tag | 0;\n"
    "    if (!((g >= 10000 && g < 10000 + 413) || (g >= 55000 && g <= 55027) || (g >= 65030 && g <= 65057))) continue;\n"
    "    const off = s.coordOffsetEnabled;\n"
    "    out.push(g + ':' + ((s.x + s.x2 + (off ? W.coordOffsetX : 0)) | 0) + ':' + ((s.y + s.y2 + (off ? W.coordOffsetY : 0)) | 0) + (s.subspriteTables ? 's' : ''));\n"
    "  }\n"
    "  out.sort(); return out.join(' ');\n"
    "};\n";

/* The highest gAnimMoveTurn a move's script branches on, from the core's data: a move with a turn branch
 * plays every turn from 0 up (two-turn moves: 0, the charge, and 1, the attack - the data's default timeline). */
static const char kTurnsJs[] =
    "globalThis.BFX_TEST_TURN_LIST = null;\n"
    "globalThis.BFX_TEST_MAX_TURN = function () {\n"
    "  const id = globalThis.BFX_TEST_MOVE_ID; if (!BFX_TEST_TURN_LIST) { BFX_TEST_TURN_LIST = {};\n"
    "    for (const m of BFX_BRIDGE.moves()) { let n = 0, any = false; for (const v of m.variants || []) if (v.kind === 'turn') { any = true; if ((v.value | 0) > n) n = v.value | 0; }\n"
    "      BFX_TEST_TURN_LIST[m.id] = any ? Math.max(n, 1) : 0; } }\n"
    "  return BFX_TEST_TURN_LIST[id] | 0;\n"
    "};\n";

/* The battle animations (no controller ones) and each one's first preset situation (the package's,
 * RT.GENERAL_ANIMS): "attacker target animArg ballThrowCase". */
static const char kAnimsJs[] =
    "globalThis.BFX_TEST_ALL_ANIMS = function () { return BFX_BRIDGE.generalAnims().filter(a => a.kind !== 'controller').map(a => a.id).join(','); };\n"
    "globalThis.BFX_TEST_PRESET = function () { const a = BFX_BRIDGE.generalAnims().find(x => x.id === globalThis.BFX_TEST_ANIM_ID);\n"
    "  const p = a && a.presets && a.presets[0] ? a.presets[0] : { attacker: 0, target: 0, inputs: {} }; const i = p.inputs || {};\n"
    "  return (p.attacker | 0) + ' ' + (p.target | 0) + ' ' + (i.animArg | 0) + ' ' + (i.ballThrowCase | 0); };\n";

/* The sound effects in the core's data: "number name;..." (SE_* songs). */
static const char kSeListJs[] =
    "globalThis.BFX_TEST_SE_LIST = function () { const D = BFX_RT.data; const out = [];\n"
    "  for (const [v, n] of Object.entries(D.seNames || {})) if (String(n).startsWith('SE_')) out.push(v + ' ' + n);\n"
    "  return out.join(';'); };\n";

static int EnvInt(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return v && *v ? atoi(v) : fallback;
}

static int CompareStrings(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* The game's effect sprites on screen, as the core's snapshot lists them. */
static void GameSprites(char *out, size_t size)
{
    static char items[MAX_SPRITES][32];
    const char *ptr[MAX_SPRITES];
    int n = 0;
    size_t len = 0;

    for (int i = 0; i < MAX_SPRITES; i++)
    {
        const struct Sprite *sp = &gSprites[i];
        int x, y;

        /* the graphics shown (the sheet's tag), as the 3D view decides what to leave out (bfx_game.c) */
        if (!sp->inUse || sp->invisible || !BattleFx_IsEffectSprite(sp))
            continue;
        x = sp->x + sp->x2 + (sp->coordOffsetEnabled ? gSpriteCoordOffsetX : 0);
        y = sp->y + sp->y2 + (sp->coordOffsetEnabled ? gSpriteCoordOffsetY : 0);
        snprintf(items[n], sizeof(items[n]), "%u:%d:%d%s", BattleFx_SpriteGfxTag(sp), x, y, sp->subspriteTables ? "s" : "");
        ptr[n] = items[n];
        n++;
    }
    qsort(ptr, (size_t)n, sizeof(ptr[0]), CompareStrings);
    out[0] = '\0';
    for (int i = 0; i < n && len + 34 < size; i++)
        len += (size_t)snprintf(out + len, size - len, "%s%s", i ? " " : "", ptr[i]);
}

static void FlushRealParity(void);

static void Init(void)
{
    const char *moves, *shots, *view;
    struct BfxHost *host = NULL;

    t.init = true;
    atexit(FlushRealParity);
    t.realShots = EnvInt("PKMN_BFX_TEST_REAL_SHOTS", 0) != 0;
    t.realParity = EnvInt("PKMN_BFX_TEST_REAL_PARITY", 0) != 0;
    if (SDL_getenv("PKMN_BFX_TEST_SE_TABLE") && SDL_getenv("PKMN_BFX_TEST_SE_TABLE")[0])
    {
        sSe.on = true;
        snprintf(sSe.path, sizeof(sSe.path), "%s", SDL_getenv("PKMN_BFX_TEST_SE_TABLE"));
        t.exitAfter = EnvInt("PKMN_BFX_TEST_EXIT", 0) != 0;
    }
    t.trace = EnvInt("PKMN_BFX_TEST_TRACE", 0) != 0;
    snprintf(t.out, sizeof(t.out), "%s", SDL_getenv("PKMN_BFX_TEST_OUT") ? SDL_getenv("PKMN_BFX_TEST_OUT") : "bfx_test");
    if (t.realShots || t.realParity)
    {
#ifdef _WIN32
        mkdir(t.out);
#else
        mkdir(t.out, 0755);
#endif
    }
    moves = SDL_getenv("PKMN_BFX_TEST_MOVES");
    snprintf(t.animsSpec, sizeof(t.animsSpec), "%s", SDL_getenv("PKMN_BFX_TEST_ANIMS") ? SDL_getenv("PKMN_BFX_TEST_ANIMS") : "");
    if ((moves == NULL || moves[0] == '\0') && t.animsSpec[0] == '\0')
    {
        t.state = TEST_OFF;
        return;
    }
    snprintf(t.movesSpec, sizeof(t.movesSpec), "%s", moves ? moves : "");
    snprintf(t.out, sizeof(t.out), "%s", SDL_getenv("PKMN_BFX_TEST_OUT") ? SDL_getenv("PKMN_BFX_TEST_OUT") : "bfx_test");
#ifdef _WIN32
    mkdir(t.out);
#else
    mkdir(t.out, 0755);
#endif
    view = SDL_getenv("PKMN_BFX_TEST_VIEW");
    t.viewHd = !(view && strcmp(view, "gba") == 0);
    t.exitAfter = EnvInt("PKMN_BFX_TEST_EXIT", 0) != 0;
    t.attacker = EnvInt("PKMN_BFX_TEST_ATTACKER", 0);
    t.target = EnvInt("PKMN_BFX_TEST_TARGET", 1);
    t.turn = EnvInt("PKMN_BFX_TEST_TURN", 0);
    t.delay = EnvInt("PKMN_BFX_TEST_DELAY", 40);
    snprintf(t.from, sizeof(t.from), "%s", SDL_getenv("PKMN_BFX_TEST_FROM") ? SDL_getenv("PKMN_BFX_TEST_FROM") : "");
    shots = SDL_getenv("PKMN_BFX_TEST_SHOTS");
    t.autoShots = shots == NULL || strcmp(shots, "auto") == 0;
    if (shots && strcmp(shots, "none") != 0 && !t.autoShots)
    {
        char buf[256], *tok;
        snprintf(buf, sizeof(buf), "%s", shots);
        for (tok = strtok(buf, ","); tok && t.shotCount < MAX_SHOTS; tok = strtok(NULL, ","))
            t.shots[t.shotCount++] = atoi(tok);
    }
    t.state = TEST_WAIT;
    t.wait = t.delay;
    SDL_Log("Battle FX test: %s, view %s, attacker %d, target %d, out %s", t.movesSpec, t.viewHd ? "hd" : "gba",
            t.attacker, t.target, t.out);
    (void)host;
}

/* The id of entry (type, number) of the list. */
static const char *TypeName(struct BfxHost *host, u8 type, u16 num)
{
    if (type == 0)
        return BattleFx_CoreMoveId(num);
    if (type >= TYPE_CONTROLLER)
        return type == TYPE_CONTROLLER + BATTLEFX_CONTROLLER_SHINY ? "CONTROLLER_SHINY" : "CONTROLLER_SEND_OUT";
    return BfxHost_AnimId(host, type == 1 + ANIM_TYPE_STATUS ? "status" : type == 1 + ANIM_TYPE_SPECIAL ? "special" : "general",
                          num);
}

/* The list, once the core is ready (move names -> numbers through its data). */
static void AddAnim(struct BfxHost *host, const char *id)
{
    const char *kind;
    int num;

    if (t.count >= MAX_LIST || !BfxHost_AnimKindNumber(host, id, &kind, &num))
    {
        SDL_Log("Battle FX test: %s is not a battle animation in the core's data", id);
        return;
    }
    if (strcmp(kind, "controller") == 0)
    {
        t.list[t.count] = (u16)num;
        t.types[t.count++] = (u8)(TYPE_CONTROLLER
                                  + (strcmp(id, "CONTROLLER_SHINY") == 0 ? BATTLEFX_CONTROLLER_SHINY : BATTLEFX_CONTROLLER_SEND_OUT));
        return;
    }
    t.list[t.count] = (u16)num;
    t.types[t.count++] = (u8)(1 + (strcmp(kind, "status") == 0 ? ANIM_TYPE_STATUS
                                   : strcmp(kind, "special") == 0 ? ANIM_TYPE_SPECIAL : ANIM_TYPE_GENERAL));
}

/* The list, once the core is ready (names -> numbers through its data). */
static bool BuildList(void)
{
    struct BfxHost *host = BattleFx_TestHost();
    char buf[8192], *tok;

    t.count = 0;
    if (host == NULL)
        return false;
    if (strcmp(t.movesSpec, "all") == 0)
    {
        for (u32 m = 1; m < MOVES_COUNT && t.count < MAX_LIST; m++)
            if (BattleFx_CoreMoveId(m) != NULL)
            {
                t.types[t.count] = 0;
                t.list[t.count++] = (u16)m;
            }
    }
    else if (t.movesSpec[0])
    {
        snprintf(buf, sizeof(buf), "%s", t.movesSpec);
        for (tok = strtok(buf, ","); tok && t.count < MAX_LIST; tok = strtok(NULL, ","))
        {
            bool found = false;
            for (u32 m = 1; m < MOVES_COUNT; m++)
            {
                const char *id = BattleFx_CoreMoveId(m);
                if (id != NULL && strcmp(id, tok) == 0)
                {
                    t.types[t.count] = 0;
                    t.list[t.count++] = (u16)m;
                    found = true;
                    break;
                }
            }
            if (!found)
                SDL_Log("Battle FX test: %s is not in the core's data", tok);
        }
    }
    if (t.animsSpec[0])
    {
        char *all = NULL;
        BfxHost_Eval(host, kAnimsJs, sizeof(kAnimsJs) - 1, "bfx_test_anims.js");
        if (strcmp(t.animsSpec, "all") == 0 && (all = BfxHost_CallGlobal(host, "BFX_TEST_ALL_ANIMS", NULL)) != NULL)
        {
            snprintf(buf, sizeof(buf), "%s", all);
            free(all);
        }
        else
            snprintf(buf, sizeof(buf), "%s", t.animsSpec);
        for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
            AddAnim(host, tok);
    }
    if (t.from[0])
    {
        /* "ID" starts at ID, "ID+" just after it (the runner resuming after ID crashed the game) */
        size_t fromLen = strlen(t.from);
        bool after = fromLen > 0 && t.from[fromLen - 1] == '+';
        int start = -1;
        if (after)
            t.from[fromLen - 1] = '\0';
        for (int i = 0; i < t.count && start < 0; i++)
        {
            const char *id = TypeName(host, t.types[i], t.list[i]);
            if (id != NULL && strcmp(id, t.from) == 0)
                start = i;
        }
        if (start < 0)
            SDL_Log("Battle FX test: %s (PKMN_BFX_TEST_FROM) is not in the list; playing all of it", t.from);
        else
        {
            if (after)
                start++;
            memmove(t.list, t.list + start, (size_t)(t.count - start) * sizeof(t.list[0]));
            memmove(t.types, t.types + start, (size_t)(t.count - start) * sizeof(t.types[0]));
            t.count -= start;
        }
    }
    return t.count > 0;
}

/* A battle animation as the engine launches it (TryHandleLaunchBattleTableAnimation,
 * LaunchStatusAnimation, InitAndLaunchSpecialAnimation): the battlers, the argument. */
static void LaunchAnim(u8 type, u16 num)
{
    struct BfxHost *host = BattleFx_TestHost();
    int attacker = t.attacker, target = t.attacker, arg = 0, throwCase = 0;
    char js[128];
    char *preset;

    snprintf(js, sizeof(js), "globalThis.BFX_TEST_ANIM_ID = '%s';", t.name);
    if (host != NULL && BfxHost_Eval(host, js, strlen(js), "bfx_test_anim.js")
     && (preset = BfxHost_CallGlobal(host, "BFX_TEST_PRESET", NULL)) != NULL)
    {
        sscanf(preset, "%d %d %d %d", &attacker, &target, &arg, &throwCase);
        free(preset);
    }
    if (attacker < 0 || attacker >= gBattlersCount)
        attacker = 0;
    if (target < 0 || target >= gBattlersCount)
        target = attacker;
    gBattleAnimAttacker = (u8)attacker;
    gBattleAnimTarget = (u8)target;
    if (gBattleSpritesDataPtr != NULL && gBattleSpritesDataPtr->animationData != NULL)
    {
        gBattleSpritesDataPtr->animationData->animArg = (u16)arg;
        gBattleSpritesDataPtr->animationData->ballThrowCaseId = (u8)throwCase;
    }
    memset(&t.disable, 0, sizeof(t.disable));
    gAnimDisableStructPtr = &t.disable;
    LaunchBattleAnimation(type, num);
}

static void Launch(u16 move)
{
    u8 attacker = gBattlerAttacker, target = gBattlerTarget;

    memset(&t.disable, 0, sizeof(t.disable));
    gBattlerAttacker = (u8)t.attacker;
    gBattlerTarget = (u8)t.target;
    gAnimMoveTurn = (u8)t.curTurn;
    gAnimMovePower = gMovesInfo[move].power;
    gAnimMoveDmg = 40;
    gAnimFriendship = 255;
    gWeatherMoveAnim = 0;
    gAnimDisableStructPtr = &t.disable;
    SetBattlerSpriteAffineMode(ST_OAM_AFFINE_OFF);
    DoMoveAnim(move);
    gBattlerAttacker = attacker;
    gBattlerTarget = target;
}

/* One of the controllers' own animations, started as the controller starts it, for battler
 * PKMN_BFX_TEST_ATTACKER: a send-out (the battler's sprite hidden first, as StartSendOutAnim leaves
 * it; it comes out of the ball again) or the stars of a shiny Pokemon (a shiny copy of the battler's
 * Pokemon; the battle's own is untouched). Test only: it plays over the battle at its action menu. */
static void LaunchController(u8 kind)
{
    u32 b = (u32)t.attacker;

    if (b >= gBattlersCount || gBattleSpritesDataPtr == NULL)
        return;
    if (kind == BATTLEFX_CONTROLLER_SEND_OUT)
    {
        /* battle_controllers.c StartSendOutAnim: a new sprite for the battler (its graphics are loaded),
         * hidden until the ball releases it */
        u16 species = GetBattlerVisualSpecies(b);
        u8 id;

        FreeSpriteOamMatrix(&gSprites[gBattlerSpriteIds[b]]);
        DestroySprite(&gSprites[gBattlerSpriteIds[b]]);
        SetMultiuseSpriteTemplateToPokemon(species, GetBattlerPosition(b));
        id = CreateSprite(&gMultiuseSpriteTemplate, GetBattlerSpriteCoord(b, BATTLER_COORD_X_2), GetBattlerSpriteDefault_Y(b),
                          GetBattlerSpriteSubpriority(b));
        gBattlerSpriteIds[b] = id;
        gSprites[id].data[0] = b;
        gSprites[id].data[2] = species;
        gSprites[id].oam.paletteNum = b;
        StartSpriteAnim(&gSprites[id], 0);
        gSprites[id].invisible = TRUE;
        gSprites[id].callback = SpriteCallbackDummy;
        DoPokeballSendOutAnimation(b, 0, IsOnPlayerSide(b) ? POKEBALL_PLAYER_SENDOUT : POKEBALL_OPPONENT_SENDOUT);
    }
    else
    {
        bool32 shiny = TRUE;
        t.shinyMon = *GetBattlerMon(b);
        SetMonData(&t.shinyMon, MON_DATA_IS_SHINY, &shiny);
        gBattleSpritesDataPtr->healthBoxesData[b].finishedShinyMonAnim = FALSE;
        TryShinyAnimation(b, &t.shinyMon);
    }
}

/* Between two animations (test only): the battlers as a turn leaves them - visible, at home -
 * whatever the last script did (a two-turn move's first half hides its user until the second). */
static void ResetBattlers(void)
{
    for (u32 b = 0; b < gBattlersCount; b++)
    {
        struct Sprite *sp;
        if (!IsBattlerSpritePresent(b) || gBattlerSpriteIds[b] >= MAX_SPRITES)
            continue;
        sp = &gSprites[gBattlerSpriteIds[b]];
        sp->invisible = FALSE;
        sp->x2 = 0;
        sp->y2 = 0;
        if (gBattleSpritesDataPtr != NULL)
            gBattleSpritesDataPtr->battlerData[b].invisible = 0;
    }
}

static void WriteResult(void)
{
    struct BattleFxAnimInfo info;

    BattleFx_AnimInfo(&info);
    if (t.report == NULL)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/report.jsonl", t.out);
        t.report = fopen(path, "a"); /* appended: a run resumed after a crash (PKMN_BFX_TEST_FROM) adds to it */
    }
    if (info.failed)
        t.failedCount++;
    if (t.compared > 0 && t.equal != t.compared)
        t.parityFailures++;
    if (t.report)
    {
        fprintf(t.report,
                "{\"move\":%u,\"turn\":%d,\"id\":\"%s\",\"core\":\"%s\",\"gameFrames\":%d,\"coreFrames\":%d,\"planned\":%d,"
                "\"failed\":%s,\"parity\":{\"compared\":%d,\"equal\":%d,\"firstBad\":%d,\"sprites\":%d,"
                "\"game\":\"%s\",\"core\":\"%s\"},\"tail\":{\"frames\":%d,\"withSprites\":%d,\"first\":\"%s\"},"
                "\"scriptFrames\":%d,\"gameDoneAt\":%d,\"gcSteps\":%d,\"gcInBegin\":%s,\"stepMsWorstPlain\":%.3f,"
                "\"beginMs\":%.2f,\"stepMs\":%.3f,\"stepMsWorst\":%.3f,\"shots\":[",
                t.move, t.curTurn, t.name, info.serial == t.serial ? info.id : "",
                t.frames, info.serial == t.serial ? info.coreFrames : 0, info.serial == t.serial ? info.plannedFrames : 0,
                info.failed ? "true" : "false", t.compared, t.equal, t.firstBad, t.sprites, t.badGame, t.badCore,
                t.tailFrames, t.tailSprites, t.tailFirst, info.serial == t.serial ? info.scriptFrames : 0, t.gameDoneAt,
                info.gcSteps, info.gcInBegin ? "true" : "false", info.stepMsWorstPlain, info.beginMs, info.coreFrames ? info.stepMsTotal / info.coreFrames : 0.0, info.stepMsWorst);
        for (int i = 0; i < t.wantCount; i++)
            fprintf(t.report, "%s{\"frame\":%d,\"file\":\"%s\"}", i ? "," : "", t.want[i],
                    t.written[i] ? t.shotNames[i] : "");
        {
            struct BattleFxRenderStats rs;
            BattleFxGL_Stats(&rs); /* the main thread's (test only: read across threads) */
            if (rs.serial == t.serial)
                fprintf(t.report, "],\"render\":{\"frames\":%u,\"layers\":%u,\"gpuSamples\":%u,\"gpuMs\":%.4f,"
                                  "\"gpuMsWorst\":%.4f,\"cpuMs\":%.4f,\"cpuMsWorst\":%.4f}}\n",
                        rs.frames, rs.layers, rs.gpuSamples, rs.gpuSamples ? rs.gpuMsTotal / rs.gpuSamples : 0.0,
                        rs.gpuMsWorst, rs.layers ? rs.cpuMsTotal / rs.layers : 0.0, rs.cpuMsWorst);
            else
                fprintf(t.report, "]}\n");
        }
        fflush(t.report);
    }
    t.played++;
}

static void Finish(void)
{
    char path[600];
    FILE *f;

    t.state = TEST_DONE;
    snprintf(path, sizeof(path), "%s/summary.json", t.out);
    f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "{\"played\":%d,\"failed\":%d,\"parityFailures\":%d,\"view\":\"%s\",\"failuresLogged\":%u}\n", t.played,
                t.failedCount, t.parityFailures, t.viewHd ? "hd" : "gba", BattleFx_Failures());
        fclose(f);
    }
    if (t.report)
        fclose(t.report);
    t.report = NULL;
    SDL_Log("Battle FX test: done, %d animations, %d failed, %d with parity differences", t.played, t.failedCount,
            t.parityFailures);
    if (t.exitAfter)
        exit(t.failedCount ? 6 : 0);
}

static void StopSoundEffects(void)
{
    m4aMPlayStop(&gMPlayInfo_SE1);
    m4aMPlayStop(&gMPlayInfo_SE2);
    m4aMPlayStop(&gMPlayInfo_SE3);
}

/* true while the sound effects are being measured (the menu waits). */
static bool MeasureSoundEffects(void)
{
    struct BfxHost *host;

    if (!sSe.on || sSe.done)
        return false;
    if (sSe.count == 0)
    {
        char *list, *tok, *save = NULL;
        host = BattleFx_TestHost();
        if (host == NULL)
            return true; /* the core is loading */
        BfxHost_Eval(host, kSeListJs, sizeof(kSeListJs) - 1, "bfx_test_se.js");
        list = BfxHost_CallGlobal(host, "BFX_TEST_SE_LIST", NULL);
        for (tok = list ? SDL_strtokr(list, ";", &save) : NULL; tok && sSe.count < 600; tok = SDL_strtokr(NULL, ";", &save))
        {
            int n = 0;
            char name[40];
            if (sscanf(tok, "%d %39s", &n, name) == 2 && n > 0)
            {
                sSe.songs[sSe.count] = (u16)n;
                snprintf(sSe.names[sSe.count++], sizeof(sSe.names[0]), "%s", name);
            }
        }
        free(list);
        sSe.f = fopen(sSe.path, "w");
        if (sSe.f == NULL || sSe.count == 0)
        {
            SDL_Log("Battle FX test: cannot measure sound effects (%s)", sSe.path);
            sSe.done = true;
            return false;
        }
        fprintf(sSe.f, "{\n");
        StopSoundEffects();
        sSe.next = 0;
        sSe.playing = false;
        return true;
    }
    if (!sSe.playing)
    {
        if (sSe.next >= sSe.count)
        {
            fprintf(sSe.f, "  \"_measured\": \"[frames after the one PlaySE is called in during which its music player has "
                           "tracks (checked once a frame, at the battle controller), the music player (gSongTable ms)]\"\n}\n");
            fclose(sSe.f);
            sSe.done = true;
            SDL_Log("Battle FX test: %d sound effects measured into %s", sSe.count, sSe.path);
            if (t.exitAfter && t.state == TEST_OFF)
                exit(0);
            return false;
        }
        StopSoundEffects();
        PlaySE(sSe.songs[sSe.next]);
        sSe.frames = 0;
        sSe.playing = true;
        return true;
    }
    {
        u32 ms = gSongTable[sSe.songs[sSe.next]].ms;
        const struct MusicPlayerInfo *player = gMPlayTable[ms].info;
        bool tracks = player && (player->status & MUSICPLAYER_STATUS_TRACK) && !(player->status & MUSICPLAYER_STATUS_PAUSE);

        if (tracks && sSe.frames < 900)
        {
            sSe.frames++;
            return true;
        }
        fprintf(sSe.f, "  \"%s\": [%d, %u],\n", sSe.names[sSe.next], sSe.frames, (unsigned)ms);
        StopSoundEffects();
        sSe.next++;
        sSe.playing = false;
    }
    return true;
}

bool BattleFxTest_OnActionMenu(void)
{
    if (!t.init)
        Init();
    if (MeasureSoundEffects())
        return true;
    switch (t.state)
    {
    case TEST_OFF:
    case TEST_DONE:
        return false;
    case TEST_WAIT:
        if (t.wait-- > 0)
            return true;
        if (!BuildList())
        {
            if (t.wait < -600) /* the core never came: give up */
            {
                SDL_Log("Battle FX test: no core, nothing to play");
                Finish();
                return false;
            }
            return true;
        }
        t.next = 0;
        t.state = TEST_LAUNCH;
        return true;
    case TEST_LAUNCH:
    {
        struct BattleFxAnimInfo info;
        int total;

        if (t.curTurn < t.maxTurn)
            t.curTurn++; /* the same move's next turn (a two-turn move's second half) */
        else
        {
            if (t.next >= t.count)
            {
                Finish();
                return false;
            }
            t.type = t.types[t.next];
            t.move = t.list[t.next++];
            t.curTurn = t.turn;
            t.maxTurn = t.turn;
            if (t.host == NULL)
                t.host = BattleFx_TestHost();
            {
                const char *id = TypeName(t.host, t.type, t.move);
                snprintf(t.name, sizeof(t.name), "%s", id ? id : "?");
            }
            if (t.type == 0 && t.turn == 0 && t.host != NULL)
            {
                char js[96];
                if (!t.turnsJs)
                    t.turnsJs = BfxHost_Eval(t.host, kTurnsJs, sizeof(kTurnsJs) - 1, "bfx_test_turns.js");
                snprintf(js, sizeof(js), "globalThis.BFX_TEST_MOVE_ID = '%s';",
                         BattleFx_CoreMoveId(t.move) ? BattleFx_CoreMoveId(t.move) : "");
                BfxHost_Eval(t.host, js, strlen(js), "bfx_test_id.js");
                t.maxTurn = BfxHost_CallGlobalInt(t.host, "BFX_TEST_MAX_TURN", 0);
            }
        }
        SDL_Log("Battle FX test: playing %s (turn %d)", t.name, t.curTurn); /* the runner resumes after it on a crash */
        t.frames = 0;
        t.compared = t.equal = t.sprites = 0;
        t.firstBad = -1;
        t.badGame[0] = t.badCore[0] = '\0';
        t.tailFrames = t.tailSprites = 0;
        t.gameDoneAt = -1;
        t.tailFirst[0] = '\0';
        if (t.trace)
        {
            char path[700];
            if (t.traceFile)
                fclose(t.traceFile);
            snprintf(path, sizeof(path), "%s/trace_%s_t%d.txt", t.out, t.name, t.curTurn);
            t.traceFile = fopen(path, "w");
            t.traceCtr0 = gRng2Value.ctr; /* the game's Random2 before the animation */
        }
        if (t.type == 0)
            Launch(t.move);
        else if (t.type >= TYPE_CONTROLLER)
            LaunchController((u8)(t.type - TYPE_CONTROLLER));
        else
            LaunchAnim((u8)(t.type - 1), t.move);
        BattleFx_AnimInfo(&info);
        /* a controller animation's replay begins at the end of this frame (bfx_game.c) */
        t.serial = t.type >= TYPE_CONTROLLER ? info.serial + 1 : info.serial;
        total = info.plannedFrames;
        /* captures wanted */
        t.wantCount = 0;
        if (t.autoShots && total > 0)
        {
            t.want[t.wantCount++] = total / 4 > 0 ? total / 4 : 1;
            t.want[t.wantCount++] = total / 2 > 0 ? total / 2 : 1;
            t.want[t.wantCount++] = total * 3 / 4 > 0 ? total * 3 / 4 : 1;
        }
        else
            for (int i = 0; i < t.shotCount; i++)
                t.want[t.wantCount++] = t.shots[i];
        for (int i = 0; i < t.wantCount; i++)
        {
            t.written[i] = false;
            snprintf(t.shotNames[i], sizeof(t.shotNames[i]), "%s_t%d_%03d_%s.png", t.name, t.curTurn, t.want[i],
                     t.viewHd ? "hd" : "gba");
        }
        SDL_AtomicSet(&t.wantSerial, (int)t.serial);
        t.state = TEST_RUN;
        return true;
    }
    case TEST_RUN:
        if (t.type >= TYPE_CONTROLLER)
        {
            /* the controllers' own: their sprites and tasks run in the battle's main loop; done when the
             * game's flags say so and the core's replay has ended */
            struct BattleFxAnimInfo info;
            u32 b = (u32)t.attacker;
            bool gameDone = t.type == TYPE_CONTROLLER + BATTLEFX_CONTROLLER_SEND_OUT
                ? !gBattleSpritesDataPtr->healthBoxesData[b].ballAnimActive && !gBattleSpritesDataPtr->animationData->numBallParticles
                : gBattleSpritesDataPtr->healthBoxesData[b].finishedShinyMonAnim;
            BattleFx_AnimInfo(&info);
            t.frames++;
            if (gameDone && t.gameDoneAt < 0)
                t.gameDoneAt = t.frames;
            if ((gameDone && !(info.serial == t.serial && info.active)) || t.frames > MAX_ANIM_FRAMES)
            {
                t.state = TEST_SETTLE;
                t.wait = SETTLE_FRAMES;
            }
            return true;
        }
        gAnimScriptCallback();
        t.frames++;
        {
            /* The core's animation over (BattleFx_AnimInfo: the core frames stopped at its plan) while the game's
             * goes on: effect sprites the game shows then are drawn as GBA pixel art (nothing HD hides them). */
            struct BattleFxAnimInfo info;
            BattleFx_AnimInfo(&info);
            if (info.serial == t.serial && info.plannedFrames > 0 && info.coreFrames >= info.plannedFrames)
            {
                char game[1024];
                GameSprites(game, sizeof(game));
                t.tailFrames++;
                if (game[0])
                {
                    if (t.tailSprites++ == 0)
                        snprintf(t.tailFirst, sizeof(t.tailFirst), "%d: %.200s", t.frames, game);
                }
                if (t.traceFile)
                    fprintf(t.traceFile, "game frame %d (core ended): game [%s]\n", t.frames, game);
            }
        }
        if (!gAnimScriptActive || t.frames > MAX_ANIM_FRAMES)
        {
            SetBattlerSpriteAffineMode(ST_OAM_AFFINE_NORMAL);
            t.state = TEST_SETTLE;
            t.wait = SETTLE_FRAMES;
        }
        return true;
    case TEST_SETTLE:
        if (t.wait-- > 0)
            return true;
        if (t.traceFile)
        {
            fclose(t.traceFile);
            t.traceFile = NULL;
        }
        WriteResult();
        if (t.curTurn >= t.maxTurn)
            ResetBattlers();
        t.state = TEST_LAUNCH;
        return true;
    }
    return false;
}

/* The real battle's animation `t.rpSerial`, done: its line in real_report.jsonl. */
static void FlushRealParity(void)
{
    struct BattleFxAnimInfo info;

    if (t.rpSerial == 0 || t.rpCompared == 0)
        return;
    if (t.rpFile == NULL)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/real_report.jsonl", t.out);
        t.rpFile = fopen(path, "a");
        if (t.rpFile == NULL)
            return;
    }
    BattleFx_AnimInfo(&info);
    fprintf(t.rpFile, "{\"serial\":%u,\"id\":\"%s\",\"compared\":%d,\"equal\":%d,\"firstBad\":%d,\"sprites\":%d,"
                      "\"game\":\"%s\",\"core\":\"%s\"}\n",
            (unsigned)t.rpSerial, t.rpId, t.rpCompared, t.rpEqual, t.rpFirstBad, t.rpSprites, t.rpBadGame, t.rpBadCore);
    fflush(t.rpFile);
    t.rpSerial = 0;
}

void BattleFxTest_OnAnimEnd(uint32_t serial)
{
    if (t.realParity && serial == t.rpSerial)
        FlushRealParity();
}

/* A real battle's animation (no gallery running): its frame compared, as the gallery compares. */
static void RealParity(struct BfxHost *host, uint32_t serial, int coreFrame)
{
    char game[1024];
    char *core;

    if (serial != t.rpSerial)
    {
        struct BattleFxAnimInfo info;
        FlushRealParity();
        BattleFx_AnimInfo(&info);
        t.rpSerial = serial;
        t.rpCompared = t.rpEqual = t.rpSprites = 0;
        t.rpFirstBad = -1;
        t.rpBadGame[0] = t.rpBadCore[0] = '\0';
        snprintf(t.rpId, sizeof(t.rpId), "%s", info.id);
        if (!t.spritesJs)
            t.spritesJs = BfxHost_Eval(host, kSpritesJs, sizeof(kSpritesJs) - 1, "bfx_test_sprites.js");
    }
    core = BfxHost_CallGlobal(host, "BFX_TEST_SPRITES", NULL);
    if (core == NULL)
        return;
    GameSprites(game, sizeof(game));
    t.rpCompared++;
    if (game[0])
        t.rpSprites++;
    if (strcmp(core, game) == 0)
        t.rpEqual++;
    else if (t.rpFirstBad < 0)
    {
        t.rpFirstBad = coreFrame;
        snprintf(t.rpBadGame, sizeof(t.rpBadGame), "%.400s", game);
        snprintf(t.rpBadCore, sizeof(t.rpBadCore), "%.400s", core);
    }
    free(core);
}

void BattleFxTest_AfterStep(struct BfxHost *host, uint32_t serial, int coreFrame)
{
    char game[1024];
    char *core;

    if (!t.init)
        Init();
    t.host = host;
    if (t.realParity && (t.state == TEST_OFF || t.state == TEST_DONE))
    {
        RealParity(host, serial, coreFrame);
        return;
    }
    /* TEST_SETTLE too: the core's step for the game frame in which the game's animation ended */
    if ((t.state != TEST_RUN && t.state != TEST_SETTLE) || serial != t.serial)
        return;
    if (!t.spritesJs)
        t.spritesJs = BfxHost_Eval(host, kSpritesJs, sizeof(kSpritesJs) - 1, "bfx_test_sprites.js");
    core = BfxHost_CallGlobal(host, "BFX_TEST_SPRITES", NULL);
    GameSprites(game, sizeof(game));
    if (core == NULL)
        return;
    t.compared++;
    if (t.traceFile)
    {
        /* Random2 calls so far (SFC32's counter advances 29 a call): the game's, the core's */
        unsigned coreCtr = (unsigned)BfxHost_CallGlobalInt(host, "BFX_TEST_RNG_CTR", 0);
        char *state = BfxHost_CallGlobal(host, "BFX_TEST_STATE", NULL);
        int tasks = 0, sprites = 0;
        for (int i = 0; i < NUM_TASKS; i++)
            tasks += gTasks[i].isActive;
        for (int i = 0; i < MAX_SPRITES; i++)
            sprites += gSprites[i].inUse;
        fprintf(t.traceFile, "core frame %d%s: Random2 calls game %u core %u; game: visual %d sound %d tasks %d fade %d y %d sprites %d; "
                             "core: %s\n  game [%s]\n  core [%s]\n", coreFrame,
                strcmp(core, game) ? " DIFF" : "", (unsigned)(gRng2Value.ctr - t.traceCtr0) / 29u,
                (coreCtr - (unsigned)t.traceCtr0) / 29u, gAnimVisualTaskCount, gAnimSoundTaskCount, tasks,
                gPaletteFade.active, gPaletteFade.y, sprites, state ? state : "", game, core);
        free(state);
    }
    if (game[0])
        t.sprites++;
    if (strcmp(core, game) == 0)
        t.equal++;
    else if (t.firstBad < 0)
    {
        t.firstBad = coreFrame;
        snprintf(t.badGame, sizeof(t.badGame), "%.400s", game);
        snprintf(t.badCore, sizeof(t.badCore), "%.400s", core);
    }
    free(core);
}

static void Capture(const char *name, int outW, int outH)
{
    char path[700];
    uint8_t *pixels = malloc((size_t)outW * outH * 4);

    if (pixels == NULL)
        return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    snprintf(path, sizeof(path), "%s/%s", t.out, name);
    PngWrite_RGBA(path, pixels, outW, outH, true);
    free(pixels);
}

void BattleFxTest_AfterDraw(uint32_t serial, int frame, bool showing, int outW, int outH)
{
    if (!t.init)
        Init();
    if (t.realShots && showing)
    {
        struct BattleFxAnimInfo info;
        BattleFx_AnimInfo(&info); /* the game thread's (test only: read across threads) */
        if (serial != t.realSerial && info.serial == serial && info.plannedFrames > 0)
        {
            t.realSerial = serial;
            for (int i = 0; i < 3; i++)
            {
                t.realWant[i] = info.plannedFrames * (i + 1) / 4 > 0 ? info.plannedFrames * (i + 1) / 4 : 1;
                t.realWritten[i] = false;
            }
        }
        for (int i = 0; i < 3 && serial == t.realSerial; i++)
            if (!t.realWritten[i] && t.realWant[i] == frame)
            {
                char name[128];
                snprintf(name, sizeof(name), "real_%04u_%s_%03d.png", (unsigned)serial, info.id, frame);
                Capture(name, outW, outH);
                t.realWritten[i] = true;
            }
    }
    if (!showing || (int)serial != SDL_AtomicGet(&t.wantSerial))
        return;
    for (int i = 0; i < t.wantCount; i++)
        if (!t.written[i] && t.want[i] == frame)
        {
            Capture(t.shotNames[i], outW, outH);
            t.written[i] = true;
        }
}

bool BattleFxTest_ViewHd(void)
{
    if (!t.init)
        return true;
    return t.viewHd;
}

#endif
