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
#include "main.h"
#include "move.h"
#include "sprite.h"
#include "constants/battle_anim.h"
#include "constants/moves.h"
#include "diorama/battle_fx.h"
#include "diorama/battle_fx_host.h"
#include "diorama/battle_fx_test.h"
#include "platform/png_write.h"

#define MAX_LIST 1024
#define MAX_SHOTS 16
#define SETTLE_FRAMES 24
#define MAX_ANIM_FRAMES 3000

enum State
{
    TEST_OFF,
    TEST_WAIT,     /* the first animation after `delay` action-menu frames */
    TEST_LAUNCH,
    TEST_RUN,
    TEST_SETTLE,
    TEST_DONE,
};

static struct
{
    bool init;
    bool viewHd;
    bool exitAfter;
    enum State state;
    char out[512];
    char movesSpec[8192];
    u16 list[MAX_LIST];
    int count, next;
    int attacker, target, turn, delay, wait;
    bool autoShots;
    int shots[MAX_SHOTS], shotCount;
    struct LinkBattleAnim disable;
    /* the animation playing */
    u16 move;
    int frames;
    uint32_t serial;
    char id[48];
    /* parity */
    int compared, equal, firstBad, sprites;
    char badGame[512], badCore[512];
    /* captures wanted (main thread reads): the animation's serial and core frames */
    SDL_atomic_t wantSerial;
    int want[MAX_SHOTS], wantCount;
    bool written[MAX_SHOTS];
    char shotNames[MAX_SHOTS][96];
    /* results */
    int played, failedCount, parityFailures;
    FILE *report;
} t;

static const char kSpritesJs[] =
    "globalThis.BFX_TEST_SPRITES = function () {\n"
    "  const out = [];\n"
    "  for (const r of BFX_RT.snapshot()) {\n"
    "    const g = r.tag | 0;\n"
    "    if (!((g >= 10000 && g < 10000 + 413) || (g >= 55000 && g <= 55027) || (g >= 65030 && g <= 65057))) continue;\n"
    "    out.push(g + ':' + (r.x | 0) + ':' + (r.y | 0) + (r.sub ? 's' : ''));\n"
    "  }\n"
    "  out.sort(); return out.join(' ');\n"
    "};\n";

static int EnvInt(const char *name, int fallback)
{
    const char *v = SDL_getenv(name);
    return v && *v ? atoi(v) : fallback;
}

static bool IsEffectTag(u16 tag)
{
    return (tag >= ANIM_SPRITES_START && tag < ANIM_SPRITES_START + ANIM_TAG_COUNT) || (tag >= 55000 && tag <= 55027)
        || (tag >= 65030 && tag <= 65057);
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

        if (!sp->inUse || sp->invisible || sp->template == NULL || !IsEffectTag(sp->template->tileTag))
            continue;
        x = sp->x + sp->x2 + (sp->coordOffsetEnabled ? gSpriteCoordOffsetX : 0);
        y = sp->y + sp->y2 + (sp->coordOffsetEnabled ? gSpriteCoordOffsetY : 0);
        snprintf(items[n], sizeof(items[n]), "%u:%d:%d%s", sp->template->tileTag, x, y, sp->subspriteTables ? "s" : "");
        ptr[n] = items[n];
        n++;
    }
    qsort(ptr, (size_t)n, sizeof(ptr[0]), CompareStrings);
    out[0] = '\0';
    for (int i = 0; i < n && len + 34 < size; i++)
        len += (size_t)snprintf(out + len, size - len, "%s%s", i ? " " : "", ptr[i]);
}

static void Init(void)
{
    const char *moves, *shots, *view;
    struct BfxHost *host = NULL;

    t.init = true;
    moves = SDL_getenv("PKMN_BFX_TEST_MOVES");
    if (moves == NULL || moves[0] == '\0')
    {
        t.state = TEST_OFF;
        return;
    }
    snprintf(t.movesSpec, sizeof(t.movesSpec), "%s", moves);
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

/* The list, once the core is ready (move names -> numbers through its data). */
static bool BuildList(void)
{
    struct BattleFxAnimInfo info;
    char buf[8192], *tok;

    (void)info;
    t.count = 0;
    if (strcmp(t.movesSpec, "all") == 0)
    {
        for (u32 m = 1; m < MOVES_COUNT && t.count < MAX_LIST; m++)
            if (BattleFx_CoreMoveId(m) != NULL)
                t.list[t.count++] = (u16)m;
        return t.count > 0;
    }
    snprintf(buf, sizeof(buf), "%s", t.movesSpec);
    for (tok = strtok(buf, ","); tok && t.count < MAX_LIST; tok = strtok(NULL, ","))
    {
        bool found = false;
        for (u32 m = 1; m < MOVES_COUNT; m++)
        {
            const char *id = BattleFx_CoreMoveId(m);
            if (id != NULL && strcmp(id, tok) == 0)
            {
                t.list[t.count++] = (u16)m;
                found = true;
                break;
            }
        }
        if (!found)
            SDL_Log("Battle FX test: %s is not in the core's data", tok);
    }
    return t.count > 0;
}

static void Launch(u16 move)
{
    u8 attacker = gBattlerAttacker, target = gBattlerTarget;

    memset(&t.disable, 0, sizeof(t.disable));
    gBattlerAttacker = (u8)t.attacker;
    gBattlerTarget = (u8)t.target;
    gAnimMoveTurn = (u8)t.turn;
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

static void WriteResult(void)
{
    struct BattleFxAnimInfo info;

    BattleFx_AnimInfo(&info);
    if (t.report == NULL)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/report.jsonl", t.out);
        t.report = fopen(path, "w");
    }
    if (info.failed)
        t.failedCount++;
    if (t.compared > 0 && t.equal != t.compared)
        t.parityFailures++;
    if (t.report)
    {
        fprintf(t.report,
                "{\"move\":%u,\"id\":\"%s\",\"core\":\"%s\",\"gameFrames\":%d,\"coreFrames\":%d,\"planned\":%d,"
                "\"failed\":%s,\"parity\":{\"compared\":%d,\"equal\":%d,\"firstBad\":%d,\"sprites\":%d,"
                "\"game\":\"%s\",\"core\":\"%s\"},\"beginMs\":%.2f,\"stepMs\":%.3f,\"stepMsWorst\":%.3f,\"shots\":[",
                t.move, BattleFx_CoreMoveId(t.move) ? BattleFx_CoreMoveId(t.move) : "?", info.serial == t.serial ? info.id : "",
                t.frames, info.serial == t.serial ? info.coreFrames : 0, info.serial == t.serial ? info.plannedFrames : 0,
                info.failed ? "true" : "false", t.compared, t.equal, t.firstBad, t.sprites, t.badGame, t.badCore,
                info.beginMs, info.coreFrames ? info.stepMsTotal / info.coreFrames : 0.0, info.stepMsWorst);
        for (int i = 0; i < t.wantCount; i++)
            fprintf(t.report, "%s{\"frame\":%d,\"file\":\"%s\"}", i ? "," : "", t.want[i],
                    t.written[i] ? t.shotNames[i] : "");
        fprintf(t.report, "]}\n");
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

bool BattleFxTest_OnActionMenu(void)
{
    if (!t.init)
        Init();
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

        if (t.next >= t.count)
        {
            Finish();
            return false;
        }
        t.move = t.list[t.next++];
        t.frames = 0;
        t.compared = t.equal = t.sprites = 0;
        t.firstBad = -1;
        t.badGame[0] = t.badCore[0] = '\0';
        Launch(t.move);
        BattleFx_AnimInfo(&info);
        t.serial = info.serial;
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
            snprintf(t.shotNames[i], sizeof(t.shotNames[i]), "%s_%03d_%s.png",
                     BattleFx_CoreMoveId(t.move) ? BattleFx_CoreMoveId(t.move) : "MOVE", t.want[i], t.viewHd ? "hd" : "gba");
        }
        SDL_AtomicSet(&t.wantSerial, (int)t.serial);
        t.state = TEST_RUN;
        return true;
    }
    case TEST_RUN:
        gAnimScriptCallback();
        t.frames++;
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
        WriteResult();
        t.state = TEST_LAUNCH;
        return true;
    }
    return false;
}

void BattleFxTest_AfterStep(struct BfxHost *host, uint32_t serial, int coreFrame)
{
    char game[1024];
    char *core;

    if (t.state != TEST_RUN || serial != t.serial)
        return;
    if (coreFrame == 1)
        BfxHost_Eval(host, kSpritesJs, sizeof(kSpritesJs) - 1, "bfx_test_sprites.js");
    core = BfxHost_CallGlobal(host, "BFX_TEST_SPRITES", NULL);
    GameSprites(game, sizeof(game));
    if (core == NULL)
        return;
    t.compared++;
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

void BattleFxTest_AfterDraw(uint32_t serial, int frame, bool showing, int outW, int outH)
{
    if (!showing || (int)serial != SDL_AtomicGet(&t.wantSerial))
        return;
    for (int i = 0; i < t.wantCount; i++)
        if (!t.written[i] && t.want[i] == frame)
        {
            char path[700];
            uint8_t *pixels = malloc((size_t)outW * outH * 4);

            if (pixels == NULL)
                return;
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            snprintf(path, sizeof(path), "%s/%s", t.out, t.shotNames[i]);
            PngWrite_RGBA(path, pixels, outW, outH, true);
            free(pixels);
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
