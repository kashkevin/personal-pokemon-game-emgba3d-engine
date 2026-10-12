#if defined(ENABLE_DIORAMA) || defined(BFX_HOST_STANDALONE)

/*
 * Battle FX host: QuickJS-ng embedding of the Battle FX animation core
 * (include/diorama/battle_fx_host.h). A C99 port of the Battle FX
 * integration package's native/src/bfx_host.cpp (v3.3), limited to what the
 * game uses; same calls into BFX_BRIDGE, same packet fields.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BFX_HOST_STANDALONE
#include <time.h>
#else
#include <SDL2/SDL.h>
#endif

#include "quickjs.h"
#include "diorama/battle_fx_host.h"

struct BfxHost
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue bridge;
    bool ready;
    int meshKinds;
    char meshKindNames[BFX_MAX_MESH_KINDS][16];
    char **moveIds;      /* by move number */
    int moveIdCount;
    struct AnimIdEntry { char kind[16]; int num; char *id; } *animIds;
    int animIdCount;
    char error[1024];
    double lastCallMs;
    unsigned gcCount;     /* QuickJS's automatic collections seen (its threshold moves after one) */
};

static double NowMs(void)
{
#ifdef BFX_HOST_STANDALONE
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#else
    return SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency();
#endif
}

/* ── small readers (each frees what it takes) ─────────────────────────── */

static void SetError(struct BfxHost *host, const char *what, const char *detail)
{
    snprintf(host->error, sizeof(host->error), "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
}

static void SetExceptionError(struct BfxHost *host, const char *what)
{
    JSContext *ctx = host->ctx;
    JSValue e = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, e);
    JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
    const char *st = JS_IsUndefined(stack) ? NULL : JS_ToCString(ctx, stack);

    snprintf(host->error, sizeof(host->error), "%s: %s%s%s", what, s ? s : "(no message)", st ? "\n" : "", st ? st : "");
    if (st)
        JS_FreeCString(ctx, st);
    if (s)
        JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, e);
}

static double Num(JSContext *ctx, JSValueConst o, const char *k, double fallback)
{
    JSValue v = JS_GetPropertyStr(ctx, o, k);
    double x = fallback;

    if (JS_IsNumber(v) || JS_IsBool(v))
        JS_ToFloat64(ctx, &x, v);
    JS_FreeValue(ctx, v);
    return x;
}

static int Int(JSContext *ctx, JSValueConst o, const char *k, int fallback)
{
    return (int)Num(ctx, o, k, fallback);
}

static bool Flag(JSContext *ctx, JSValueConst o, const char *k)
{
    JSValue v = JS_GetPropertyStr(ctx, o, k);
    bool b = JS_ToBool(ctx, v) > 0;

    JS_FreeValue(ctx, v);
    return b;
}

static void Str(JSContext *ctx, JSValueConst o, const char *k, char *out, size_t size)
{
    JSValue v = JS_GetPropertyStr(ctx, o, k);

    out[0] = '\0';
    if (JS_IsString(v))
    {
        const char *p = JS_ToCString(ctx, v);
        if (p)
        {
            snprintf(out, size, "%s", p);
            JS_FreeCString(ctx, p);
        }
    }
    JS_FreeValue(ctx, v);
}

static void Vec(JSContext *ctx, JSValueConst o, const char *k, float *out, int n)
{
    JSValue a = JS_GetPropertyStr(ctx, o, k);

    for (int i = 0; i < n; i++)
    {
        JSValue e = JS_GetPropertyUint32(ctx, a, (uint32_t)i);
        double x = 0;

        if (JS_IsNumber(e))
            JS_ToFloat64(ctx, &x, e);
        out[i] = (float)x;
        JS_FreeValue(ctx, e);
    }
    JS_FreeValue(ctx, a);
}

/* Pointer into a typed array's storage (owned by JS; stable while the bridge keeps the array). */
static const uint8_t *Typed(JSContext *ctx, JSValueConst o, const char *k, size_t *bytes)
{
    JSValue ta = JS_GetPropertyStr(ctx, o, k);
    size_t off = 0, len = 0, bpe = 0;
    const uint8_t *p = NULL;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, ta, &off, &len, &bpe);

    if (!JS_IsException(ab))
    {
        size_t size = 0;
        uint8_t *base = JS_GetArrayBuffer(ctx, &size, ab);
        if (base)
            p = base + off;
    }
    else
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, ab);
    JS_FreeValue(ctx, ta);
    if (bytes)
        *bytes = len;
    return p;
}

static const float *F32(JSContext *ctx, JSValueConst o, const char *k)
{
    return (const float *)Typed(ctx, o, k, NULL);
}

/* ── calls ─────────────────────────────────────────────────────────────── */

/* QuickJS checks for stack overflow against a limit computed from the stack
 * position captured when the limit was last set: capture it again on entry,
 * so the guard holds for whichever thread calls and at its depth. */
static void Enter(struct BfxHost *host)
{
    if (host->rt)
        JS_UpdateStackTop(host->rt);
}

/* BFX_BRIDGE[fn](arg): arg parsed from JSON (parse) or passed as a string; the
 * result in *out (to free) or freed. */
static bool Call(struct BfxHost *host, const char *fn, const char *arg, bool parse, JSValue *out)
{
    JSContext *ctx = host->ctx;
    JSValue f, a = JS_UNDEFINED, r;
    int argc = 0;
    size_t gcBefore;

    if (out)
        *out = JS_UNDEFINED;
    if (!host->ready && strcmp(fn, "load") != 0 && strcmp(fn, "info") != 0 && strcmp(fn, "moves") != 0
     && strcmp(fn, "generalAnims") != 0)
    {
        SetError(host, fn, "the core is not loaded");
        return false;
    }
    Enter(host);
    gcBefore = JS_GetGCThreshold(host->rt);
    f = JS_GetPropertyStr(ctx, host->bridge, fn);
    if (arg != NULL)
    {
        a = parse ? JS_ParseJSON(ctx, arg, strlen(arg), "arg") : JS_NewString(ctx, arg);
        if (JS_IsException(a))
        {
            JS_FreeValue(ctx, f);
            SetExceptionError(host, fn);
            return false;
        }
        argc = 1;
    }
    r = JS_Call(ctx, f, host->bridge, argc, &a);
    JS_FreeValue(ctx, f);
    JS_FreeValue(ctx, a);
    if (JS_GetGCThreshold(host->rt) != gcBefore)
        host->gcCount++;
    if (JS_IsException(r))
    {
        SetExceptionError(host, fn);
        return false;
    }
    if (out)
        *out = r;
    else
        JS_FreeValue(ctx, r);
    return true;
}

struct BfxHost *BfxHost_Create(void)
{
    return calloc(1, sizeof(struct BfxHost));
}

static void FreeIds(struct BfxHost *host)
{
    for (int i = 0; i < host->moveIdCount; i++)
        free(host->moveIds[i]);
    free(host->moveIds);
    host->moveIds = NULL;
    host->moveIdCount = 0;
    for (int i = 0; i < host->animIdCount; i++)
        free(host->animIds[i].id);
    free(host->animIds);
    host->animIds = NULL;
    host->animIdCount = 0;
}

void BfxHost_Destroy(struct BfxHost *host)
{
    if (host == NULL)
        return;
    if (host->ctx)
    {
        Enter(host);
        JS_FreeValue(host->ctx, host->bridge);
        JS_FreeContext(host->ctx);
    }
    if (host->rt)
        JS_FreeRuntime(host->rt);
    FreeIds(host);
    free(host);
}

static char *DupString(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);

    if (d)
        memcpy(d, s, n);
    return d;
}

/* The move numbers and battle animation (kind, number) the data holds, and their ids. */
static bool ReadIds(struct BfxHost *host)
{
    JSContext *ctx = host->ctx;
    JSValue list;
    int n, maxNum = 0;
    char id[64], kind[16];

    FreeIds(host);
    if (!Call(host, "moves", NULL, false, &list))
        return false;
    n = Int(ctx, list, "length", 0);
    for (int i = 0; i < n; i++)
    {
        JSValue o = JS_GetPropertyUint32(ctx, list, (uint32_t)i);
        int num = Int(ctx, o, "num", -1);
        if (num > maxNum)
            maxNum = num;
        JS_FreeValue(ctx, o);
    }
    host->moveIds = calloc((size_t)maxNum + 1, sizeof(char *));
    host->moveIdCount = host->moveIds ? maxNum + 1 : 0;
    for (int i = 0; i < n && host->moveIds; i++)
    {
        JSValue o = JS_GetPropertyUint32(ctx, list, (uint32_t)i);
        int num = Int(ctx, o, "num", -1);
        Str(ctx, o, "id", id, sizeof(id));
        if (num >= 0 && num <= maxNum && id[0] && host->moveIds[num] == NULL)
            host->moveIds[num] = DupString(id);
        JS_FreeValue(ctx, o);
    }
    JS_FreeValue(ctx, list);

    if (!Call(host, "generalAnims", NULL, false, &list))
        return false;
    n = Int(ctx, list, "length", 0);
    host->animIds = calloc((size_t)(n > 0 ? n : 1), sizeof(*host->animIds));
    for (int i = 0; i < n && host->animIds; i++)
    {
        JSValue o = JS_GetPropertyUint32(ctx, list, (uint32_t)i);
        Str(ctx, o, "id", id, sizeof(id));
        Str(ctx, o, "kind", kind, sizeof(kind));
        snprintf(host->animIds[host->animIdCount].kind, sizeof(host->animIds[0].kind), "%s", kind);
        host->animIds[host->animIdCount].num = Int(ctx, o, "num", -1);
        host->animIds[host->animIdCount].id = DupString(id);
        host->animIdCount++;
        JS_FreeValue(ctx, o);
    }
    JS_FreeValue(ctx, list);
    return true;
}

bool BfxHost_Init(struct BfxHost *host, const char *core, size_t coreSize, const char *data, size_t dataSize,
                  size_t stackBytes)
{
    JSContext *ctx;
    JSValue r, g, d, info, sprite, kinds;
    bool layoutOk;

    if (host == NULL || host->rt != NULL)
        return false;
    host->rt = JS_NewRuntime();
    if (host->rt == NULL)
    {
        SetError(host, "init", "out of memory");
        return false;
    }
    JS_SetMaxStackSize(host->rt, stackBytes);
    host->ctx = ctx = JS_NewContext(host->rt);
    host->bridge = JS_UNDEFINED;
    if (ctx == NULL)
    {
        SetError(host, "init", "out of memory");
        return false;
    }
    Enter(host);
    /* QuickJS needs the source NUL-terminated at source[size]. */
    {
        char *copy = malloc(coreSize + 1);
        if (copy == NULL)
        {
            SetError(host, "init", "out of memory");
            return false;
        }
        memcpy(copy, core, coreSize);
        copy[coreSize] = '\0';
        r = JS_Eval(ctx, copy, coreSize, "bfx_core.js", JS_EVAL_TYPE_GLOBAL);
        free(copy);
    }
    if (JS_IsException(r))
    {
        SetExceptionError(host, "core");
        return false;
    }
    JS_FreeValue(ctx, r);
    g = JS_GetGlobalObject(ctx);
    host->bridge = JS_GetPropertyStr(ctx, g, "BFX_BRIDGE");
    JS_FreeValue(ctx, g);
    if (!JS_IsObject(host->bridge))
    {
        SetError(host, "init", "BFX_BRIDGE missing");
        return false;
    }
    {
        char *copy = malloc(dataSize + 1);
        if (copy == NULL)
        {
            SetError(host, "init", "out of memory");
            return false;
        }
        memcpy(copy, data, dataSize);
        copy[dataSize] = '\0';
        d = JS_ParseJSON(ctx, copy, dataSize, "bfx_moves.json");
        free(copy);
    }
    if (JS_IsException(d))
    {
        SetExceptionError(host, "data");
        return false;
    }
    {
        JSValue f = JS_GetPropertyStr(ctx, host->bridge, "load");
        r = JS_Call(ctx, f, host->bridge, 1, &d);
        JS_FreeValue(ctx, f);
        JS_FreeValue(ctx, d);
    }
    if (JS_IsException(r))
    {
        SetExceptionError(host, "load");
        return false;
    }
    JS_FreeValue(ctx, r);

    /* The sprite record layout compiled in here must be the bundle's. */
    sprite = JS_GetPropertyStr(ctx, host->bridge, "SPRITE");
    layoutOk = Int(ctx, sprite, "STRIDE", -1) == BFX_SP_STRIDE && Int(ctx, sprite, "BATTLER", -1) == BFX_SP_BATTLER
            && Int(ctx, sprite, "GLOW", -1) == BFX_SP_GLOW && Int(ctx, sprite, "TINTADD", -1) == BFX_SP_TINTADD
            && Int(ctx, sprite, "DEPTHW", -1) == BFX_SP_DEPTHW && Int(ctx, sprite, "GBA", -1) == BFX_SP_GBA
            && Int(ctx, sprite, "KIND", -1) == BFX_SP_KIND && Int(ctx, sprite, "TAG", -1) == BFX_SP_TAG;
    JS_FreeValue(ctx, sprite);
    if (!layoutOk)
    {
        SetError(host, "init", "the sprite record layout differs from the core's (battle_fx_host.h and bfx_core.js "
                               "from different Battle FX versions)");
        return false;
    }
    /* The mesh kinds the core packs, in packet order. */
    if (!Call(host, "info", NULL, false, &info))
        return false;
    kinds = JS_GetPropertyStr(ctx, info, "meshKinds");
    host->meshKinds = Int(ctx, kinds, "length", 0);
    if (host->meshKinds > BFX_MAX_MESH_KINDS)
        host->meshKinds = BFX_MAX_MESH_KINDS;
    for (int i = 0; i < host->meshKinds; i++)
    {
        JSValue s = JS_GetPropertyUint32(ctx, kinds, (uint32_t)i);
        const char *c = JS_ToCString(ctx, s);
        snprintf(host->meshKindNames[i], sizeof(host->meshKindNames[i]), "%s", c ? c : "");
        if (c)
            JS_FreeCString(ctx, c);
        JS_FreeValue(ctx, s);
    }
    JS_FreeValue(ctx, kinds);
    JS_FreeValue(ctx, info);
    if (!ReadIds(host))
        return false;
    host->ready = true;
    return true;
}

bool BfxHost_Settings(struct BfxHost *host, const char *json)
{
    return host && Call(host, "settings", json, true, NULL);
}

bool BfxHost_Begin(struct BfxHost *host, const char *requestJson, struct BfxBeginResult *out)
{
    JSContext *ctx;
    JSValue r;
    double t0 = NowMs();

    memset(out, 0, sizeof(*out));
    out->variant = -1;
    if (host == NULL || !Call(host, "begin", requestJson, true, &r))
        return false;
    ctx = host->ctx;
    Str(ctx, r, "move", out->move, sizeof(out->move));
    Str(ctx, r, "script", out->script, sizeof(out->script));
    Str(ctx, r, "anim", out->anim, sizeof(out->anim));
    Str(ctx, r, "kind", out->kind, sizeof(out->kind));
    Str(ctx, r, "branch", out->branch, sizeof(out->branch));
    out->frames = Int(ctx, r, "frames", 0);
    out->extraFrames = Int(ctx, r, "extraFrames", 0);
    out->totalFrames = Int(ctx, r, "totalFrames", 0);
    out->attacker = Int(ctx, r, "attacker", 0);
    out->target = Int(ctx, r, "target", 0);
    out->variant = Int(ctx, r, "variant", -1);
    JS_FreeValue(ctx, r);
    host->lastCallMs = NowMs() - t0;
    return true;
}

int BfxHost_Step(struct BfxHost *host)
{
    JSValue r;
    int32_t v = 0;

    if (host == NULL || !Call(host, "step", NULL, false, &r))
        return -1;
    JS_ToInt32(host->ctx, &v, r);
    JS_FreeValue(host->ctx, r);
    return v != 0 ? 1 : 0;
}

static void ReadQuads(JSContext *ctx, JSValueConst pk, const char *k, struct BfxQuads *q)
{
    JSValue o = JS_GetPropertyStr(ctx, pk, k);

    q->n = Int(ctx, o, "n", 0);
    q->P = F32(ctx, o, "P");
    q->S = F32(ctx, o, "S");
    q->C = F32(ctx, o, "C");
    q->X = F32(ctx, o, "X");
    JS_FreeValue(ctx, o);
}

static void ReadRibbons(JSContext *ctx, JSValueConst pk, const char *k, struct BfxRibbons *r)
{
    JSValue o = JS_GetPropertyStr(ctx, pk, k);

    r->nv = Int(ctx, o, "nv", 0);
    r->ni = Int(ctx, o, "ni", 0);
    r->pos = F32(ctx, o, "pos");
    r->uv = F32(ctx, o, "uv");
    r->col = F32(ctx, o, "col");
    r->row = F32(ctx, o, "row");
    r->idx = (const uint16_t *)Typed(ctx, o, "idx", NULL);
    JS_FreeValue(ctx, o);
}

static void ReadRing(JSContext *ctx, JSValueConst pk, const char *k, struct BfxRing *r)
{
    JSValue o = JS_GetPropertyStr(ctx, pk, k);

    r->cap = Int(ctx, o, "cap", 0);
    r->head = Int(ctx, o, "head", 0);
    r->count = Int(ctx, o, "count", 0);
    r->lo = Int(ctx, o, "lo", -1);
    r->hi = Int(ctx, o, "hi", -1);
    r->cleared = Flag(ctx, o, "cleared");
    r->add = Flag(ctx, o, "add");
    r->a = F32(ctx, o, "a");
    r->b = F32(ctx, o, "b");
    r->c = F32(ctx, o, "c");
    r->d = F32(ctx, o, "d");
    r->e = F32(ctx, o, "e");
    r->f = F32(ctx, o, "f");
    JS_FreeValue(ctx, o);
}

bool BfxHost_Frame(struct BfxHost *host, double t, struct BfxPacket *p)
{
    JSContext *ctx;
    JSValue f, arg, pk, o;
    double t0 = NowMs();

    if (host == NULL || !host->ready)
        return false;
    size_t gcBefore;

    ctx = host->ctx;
    Enter(host);
    gcBefore = JS_GetGCThreshold(host->rt);
    f = JS_GetPropertyStr(ctx, host->bridge, "frame");
    arg = t >= 0 ? JS_NewFloat64(ctx, t) : JS_UNDEFINED;
    pk = JS_Call(ctx, f, host->bridge, 1, &arg);
    JS_FreeValue(ctx, f);
    if (JS_GetGCThreshold(host->rt) != gcBefore)
        host->gcCount++;
    if (JS_IsException(pk))
    {
        SetExceptionError(host, "frame");
        return false;
    }
    if (JS_IsNull(pk) || !JS_IsObject(pk))
    {
        JS_FreeValue(ctx, pk);
        SetError(host, "frame", "no animation");
        return false;
    }
    {
        uint32_t serial = p->serial;
        uint8_t hideBg = p->hideBg;
        memset(p, 0, sizeof(*p));
        p->serial = serial;
        p->hideBg = hideBg;
    }
    p->frame = Int(ctx, pk, "frame", 0);
    p->t = Num(ctx, pk, "t", 0);
    p->done = Flag(ctx, pk, "done");
    p->cameraLock = (float)Num(ctx, pk, "cameraLock", 0);
    p->view = F32(ctx, pk, "view");
    p->camera = F32(ctx, pk, "camera");
    ReadQuads(ctx, pk, "cards", &p->cards);
    ReadQuads(ctx, pk, "cardsBack", &p->cardsBack);
    ReadQuads(ctx, pk, "decals", &p->decals);
    ReadQuads(ctx, pk, "dist", &p->dist);
    ReadRibbons(ctx, pk, "ribbons", &p->ribbons);
    ReadRibbons(ctx, pk, "ribbonsBack", &p->ribbonsBack);
    ReadRing(ctx, pk, "partsAdd", &p->partsAdd);
    ReadRing(ctx, pk, "partsAlpha", &p->partsAlpha);
    o = JS_GetPropertyStr(ctx, pk, "meshes");
    p->meshKinds = host->meshKinds;
    for (int i = 0; i < host->meshKinds; i++)
    {
        JSValue k = JS_GetPropertyStr(ctx, o, host->meshKindNames[i]);
        p->meshes[i].n = Int(ctx, k, "n", 0);
        p->meshes[i].data = F32(ctx, k, "data");
        JS_FreeValue(ctx, k);
    }
    JS_FreeValue(ctx, o);
    p->lights = F32(ctx, pk, "lights");
    p->lightCount = Int(ctx, pk, "lightCount", 0);
    p->sprites = F32(ctx, pk, "sprites");
    p->spriteCount = Int(ctx, pk, "spriteCount", 0);
    o = JS_GetPropertyStr(ctx, pk, "spriteStats");
    p->pixelEffects = Int(ctx, o, "pixelEffects", 0);
    p->objWindows = Int(ctx, o, "objWindows", 0);
    JS_FreeValue(ctx, o);
    o = JS_GetPropertyStr(ctx, pk, "battlers");
    for (int b = 0; b < 4; b++)
    {
        JSValue s = JS_GetPropertyUint32(ctx, o, (uint32_t)b);
        struct BfxBattler *B = &p->battlers[b];
        JSValue tint;
        char pic[16];

        B->present = Flag(ctx, s, "present");
        B->drawn = Flag(ctx, s, "drawn");
        B->onBgLayer = Int(ctx, s, "onBgLayer", 0);
        Vec(ctx, s, "home", B->home, 2);
        Vec(ctx, s, "homeWorld", B->homeWorld, 3);
        Vec(ctx, s, "feet", B->feet, 3);
        B->k = (float)Num(ctx, s, "k", 0);
        Vec(ctx, s, "gba", B->gba, 2);
        Vec(ctx, s, "offsetPx", B->offsetPx, 2);
        Vec(ctx, s, "world", B->world, 3);
        Vec(ctx, s, "matrix", B->matrix, 4);
        Vec(ctx, s, "scale", B->scale, 2);
        Vec(ctx, s, "flip", B->flip, 2);
        Vec(ctx, s, "blend", B->blend, 2);
        B->fade = (float)Num(ctx, s, "fade", 1);
        Vec(ctx, s, "flash", B->flash, 4);
        Vec(ctx, s, "glow", B->glow, 4);
        Vec(ctx, s, "shine", B->shine, 3);
        tint = JS_GetPropertyStr(ctx, s, "tint");
        Vec(ctx, tint, "mul", B->tintMul, 3);
        Vec(ctx, tint, "add", B->tintAdd, 3);
        B->gray = (float)Num(ctx, tint, "gray", 0);
        JS_FreeValue(ctx, tint);
        Vec(ctx, s, "light", B->light, 3);
        Vec(ctx, s, "lightAdd", B->lightAdd, 3);
        B->priority = Int(ctx, s, "priority", 2);
        B->subpriority = Int(ctx, s, "subpriority", 0);
        B->form = Int(ctx, s, "form", 0);
        Str(ctx, s, "picture", pic, sizeof(pic));
        B->picture = strcmp(pic, "substitute") == 0 ? 1 : strcmp(pic, "trainer") == 0 ? 2 : strcmp(pic, "none") == 0 ? 3 : 0;
        JS_FreeValue(ctx, s);
    }
    JS_FreeValue(ctx, o);
    o = JS_GetPropertyStr(ctx, pk, "post");
    p->post.bloom = (float)Num(ctx, o, "bloom", 0);
    p->post.threshold = (float)Num(ctx, o, "threshold", 1);
    p->post.exposure = (float)Num(ctx, o, "exposure", 1);
    p->post.distortion = (float)Num(ctx, o, "distortion", 0);
    p->post.vignette = (float)Num(ctx, o, "vignette", 0);
    p->post.sat = (float)Num(ctx, o, "sat", 1);
    Vec(ctx, o, "grade", p->post.grade, 4);
    p->post.flash = (float)Num(ctx, o, "flash", 0);
    Vec(ctx, o, "flashColor", p->post.flashColor, 3);
    JS_FreeValue(ctx, o);
    o = JS_GetPropertyStr(ctx, pk, "env");
    {
        JSValue sky = JS_GetPropertyStr(ctx, o, "sky");
        Vec(ctx, sky, "sunDir", p->sunDir, 3);
        JS_FreeValue(ctx, sky);
    }
    Vec(ctx, o, "hemiSky", p->hemiSky, 3);
    Vec(ctx, o, "hemiGround", p->hemiGround, 3);
    p->hemiI = (float)Num(ctx, o, "hemiI", 0);
    Vec(ctx, o, "sun", p->sun, 3);
    p->sunI = (float)Num(ctx, o, "sunI", 0);
    Vec(ctx, o, "fog", p->fog, 3);
    p->fogDensity = (float)Num(ctx, o, "fogDensity", 0);
    JS_FreeValue(ctx, o);
    JS_FreeValue(ctx, pk);
    /* The rings' `cleared` flag is consumed by whoever takes this packet. */
    Call(host, "ackCleared", NULL, false, NULL);
    host->lastCallMs = NowMs() - t0;
    return true;
}

int BfxHost_DrainSounds(struct BfxHost *host)
{
    JSValue r, s;
    int n = 0;

    if (host == NULL || !Call(host, "takeSounds", NULL, false, &r))
        return 0;
    s = JS_GetPropertyStr(host->ctx, r, "sounds");
    n += Int(host->ctx, s, "length", 0);
    JS_FreeValue(host->ctx, s);
    s = JS_GetPropertyStr(host->ctx, r, "cries");
    n += Int(host->ctx, s, "length", 0);
    JS_FreeValue(host->ctx, s);
    JS_FreeValue(host->ctx, r);
    return n;
}

int BfxHost_FrameIndex(struct BfxHost *host)
{
    JSValue r;
    int32_t v = 0;

    if (host == NULL || !Call(host, "frameIndex", NULL, false, &r))
        return 0;
    JS_ToInt32(host->ctx, &v, r);
    JS_FreeValue(host->ctx, r);
    return v;
}

bool BfxHost_StateHash(struct BfxHost *host, char *out, size_t outSize)
{
    JSValue r;
    const char *s;

    if (outSize)
        out[0] = '\0';
    if (host == NULL || !Call(host, "stateHash", NULL, false, &r))
        return false;
    s = JS_ToCString(host->ctx, r);
    if (s)
    {
        snprintf(out, outSize, "%s", s);
        JS_FreeCString(host->ctx, s);
    }
    JS_FreeValue(host->ctx, r);
    return s != NULL;
}

int BfxHost_MeshKinds(const struct BfxHost *host)
{
    return host ? host->meshKinds : 0;
}

bool BfxHost_Eval(struct BfxHost *host, const char *source, size_t size, const char *name)
{
    JSValue r;
    char *copy;

    if (host == NULL || host->ctx == NULL)
        return false;
    Enter(host);
    copy = malloc(size + 1);
    if (copy == NULL)
        return false;
    memcpy(copy, source, size);
    copy[size] = '\0';
    r = JS_Eval(host->ctx, copy, size, name, JS_EVAL_TYPE_GLOBAL);
    free(copy);
    if (JS_IsException(r))
    {
        SetExceptionError(host, name);
        return false;
    }
    JS_FreeValue(host->ctx, r);
    return true;
}

char *BfxHost_CallGlobal(struct BfxHost *host, const char *fn, const char *jsonArg)
{
    JSContext *ctx;
    JSValue g, f, a = JS_UNDEFINED, r;
    const char *s;
    char *out = NULL;

    if (host == NULL || host->ctx == NULL)
        return NULL;
    ctx = host->ctx;
    Enter(host);
    g = JS_GetGlobalObject(ctx);
    f = JS_GetPropertyStr(ctx, g, fn);
    JS_FreeValue(ctx, g);
    if (jsonArg != NULL)
        a = JS_ParseJSON(ctx, jsonArg, strlen(jsonArg), "arg");
    r = JS_Call(ctx, f, JS_UNDEFINED, jsonArg != NULL ? 1 : 0, &a);
    JS_FreeValue(ctx, f);
    JS_FreeValue(ctx, a);
    if (JS_IsException(r))
    {
        SetExceptionError(host, fn);
        return NULL;
    }
    s = JS_ToCString(ctx, r);
    if (s)
    {
        out = DupString(s);
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, r);
    return out;
}

int BfxHost_CallGlobalInt(struct BfxHost *host, const char *fn, int fallback)
{
    JSContext *ctx;
    JSValue g, f, r;
    int32_t v = fallback;

    if (host == NULL || host->ctx == NULL)
        return fallback;
    ctx = host->ctx;
    Enter(host);
    g = JS_GetGlobalObject(ctx);
    f = JS_GetPropertyStr(ctx, g, fn);
    JS_FreeValue(ctx, g);
    r = JS_Call(ctx, f, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, f);
    if (JS_IsException(r))
    {
        SetExceptionError(host, fn);
        return fallback;
    }
    if (JS_ToInt32(ctx, &v, r) != 0)
        v = fallback;
    JS_FreeValue(ctx, r);
    return v;
}

int BfxHost_CallGlobalIntArg(struct BfxHost *host, const char *fn, int arg, int fallback)
{
    JSContext *ctx;
    JSValue g, f, a, r;
    int32_t v = fallback;

    if (host == NULL || host->ctx == NULL)
        return fallback;
    ctx = host->ctx;
    Enter(host);
    g = JS_GetGlobalObject(ctx);
    f = JS_GetPropertyStr(ctx, g, fn);
    JS_FreeValue(ctx, g);
    a = JS_NewInt32(ctx, arg);
    r = JS_Call(ctx, f, JS_UNDEFINED, 1, &a);
    JS_FreeValue(ctx, f);
    if (JS_IsException(r))
    {
        SetExceptionError(host, fn);
        return fallback;
    }
    if (JS_ToInt32(ctx, &v, r) != 0)
        v = fallback;
    JS_FreeValue(ctx, r);
    return v;
}

const char *BfxHost_MoveId(const struct BfxHost *host, int number)
{
    if (host == NULL || number < 0 || number >= host->moveIdCount)
        return NULL;
    return host->moveIds[number];
}

const char *BfxHost_AnimId(const struct BfxHost *host, const char *kind, int number)
{
    if (host == NULL)
        return NULL;
    for (int i = 0; i < host->animIdCount; i++)
        if (host->animIds[i].num == number && strcmp(host->animIds[i].kind, kind) == 0)
            return host->animIds[i].id;
    return NULL;
}

bool BfxHost_AnimKindNumber(const struct BfxHost *host, const char *id, const char **kind, int *number)
{
    if (host == NULL)
        return false;
    for (int i = 0; i < host->animIdCount; i++)
        if (strcmp(host->animIds[i].id, id) == 0)
        {
            *kind = host->animIds[i].kind;
            *number = host->animIds[i].num;
            return true;
        }
    return false;
}

unsigned BfxHost_GcCount(const struct BfxHost *host)
{
    return host ? host->gcCount : 0;
}

double BfxHost_CollectGarbage(struct BfxHost *host)
{
    double t0 = NowMs();

    if (host == NULL || host->rt == NULL)
        return 0.0;
    Enter(host);
    JS_RunGC(host->rt);
    return NowMs() - t0;
}

const char *BfxHost_LastError(const struct BfxHost *host)
{
    return host ? host->error : "no host";
}

double BfxHost_LastCallMs(const struct BfxHost *host)
{
    return host ? host->lastCallMs : 0.0;
}

#endif // ENABLE_DIORAMA || BFX_HOST_STANDALONE
