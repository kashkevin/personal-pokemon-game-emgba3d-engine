#ifdef ENABLE_DIORAMA

/*
 * Battle FX: the renderer (include/diorama/battle_fx.h). A C port of the
 * Battle FX integration package's reference renderer (native/src/bfx_gl.cpp,
 * v3.3) for the 3D battle: the same shaders (battlefx/shaders, the Battle FX
 * Lab's), buffers, blending and draw order for the effect layers, drawn
 * through the fixed GBA framing camera so every effect stands where the GBA
 * picture's battlers are.
 *
 * Two layers per frame, each drawn into a layer of its own (linear HDR,
 * premultiplied, transparent where nothing is) and composited over what is
 * already on the window: the back layer (decals, back cards and ribbons)
 * after the world and before the battle's picture, so the battlers cover
 * it; the front layer (meshes, particles, ribbons, cards) after the picture.
 * Each layer covers the window above the text box (the GBA's effect sprites
 * go under the text box too). The composite blends in linear light against a
 * copy of the window (GPU to GPU), adds the layer's bloom, applies the
 * frame's HD post (distortion, flash, grading) and leaves every pixel the
 * effects do not reach exactly as it was.
 *
 * The battlers as the game shows them (battle_fx_attach.h): where the animated
 * battle sprites draw a frame of their own in place of a battler's GBA
 * picture - larger or smaller, its body higher or lower - the battler's glow,
 * flash, light and depth are drawn from that frame, and the effects near the
 * battler are moved and sized for its body as shown (Attach below: in the
 * vertex shaders, a warp of the GBA screen around each battler; nothing of
 * the core's replay changes). Without such a frame everything is drawn exactly
 * as before.
 *
 * Quality tiers (BattleFxGL_SetTier): the layers drawn at a share of the
 * window's resolution, with fewer bloom levels; the pace of the pictures is
 * measured here for bfx_game.c's choice of tier (BattleFxGL_Pace).
 *
 * Main thread only. The game's renderer state is restored after each layer.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "bfx_stb_image.h"
#include "diorama/battle_fx.h"
#include "diorama/battle_fx_attach.h"
#include "diorama/battle_fx_host.h"
#include "diorama/battle_fx_render.h"
#include "diorama/battle_fx_test.h"
#include "diorama/gl_target.h"

/* ── OpenGL 1.2+ entry points (Battle FX's own; gl_loader.h is the Diorama's) ── */

#define BFX_GL_FUNCS(X)                                                    \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                               \
    X(PFNGLATTACHSHADERPROC, AttachShader)                                 \
    X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation)                     \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                                     \
    X(PFNGLBINDFRAGDATALOCATIONPROC, BindFragDataLocation)                 \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)                           \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)                         \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                           \
    X(PFNGLBLENDEQUATIONPROC, BlendEquation)                               \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate)                       \
    X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)                           \
    X(PFNGLBUFFERDATAPROC, BufferData)                                     \
    X(PFNGLBUFFERSUBDATAPROC, BufferSubData)                               \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)             \
    X(PFNGLCOMPILESHADERPROC, CompileShader)                               \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)                               \
    X(PFNGLCREATESHADERPROC, CreateShader)                                 \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                               \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)                     \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                               \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)                   \
    X(PFNGLDELETESHADERPROC, DeleteShader)                                 \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)                     \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, DrawArraysInstanced)                   \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, DrawElementsInstanced)               \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)           \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer)           \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)                 \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                                     \
    X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap)                             \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)                           \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)                         \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                           \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                       \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                                 \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)                         \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)                                   \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)                     \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)                                   \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)                   \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)                                 \
    X(PFNGLUNIFORM1FPROC, Uniform1f)                                       \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                                       \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                                       \
    X(PFNGLUNIFORM3FPROC, Uniform3f)                                       \
    X(PFNGLUNIFORM3FVPROC, Uniform3fv)                                     \
    X(PFNGLUNIFORM4FPROC, Uniform4f)                                       \
    X(PFNGLUNIFORM4FVPROC, Uniform4fv)                                     \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv)                         \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                                     \
    X(PFNGLVERTEXATTRIBDIVISORPROC, VertexAttribDivisor)                   \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer)                   \
    X(PFNGLGENQUERIESPROC, GenQueries)                                     \
    X(PFNGLBEGINQUERYPROC, BeginQuery)                                     \
    X(PFNGLENDQUERYPROC, EndQuery)                                         \
    X(PFNGLGETQUERYOBJECTIVPROC, GetQueryObjectiv)                         \
    X(PFNGLGETQUERYOBJECTUI64VPROC, GetQueryObjectui64v)

#define BFX_GL_DECLARE(type, name) static type bgl##name;
BFX_GL_FUNCS(BFX_GL_DECLARE)

static bool LoadFunctions(void)
{
#define BFX_GL_LOAD(type, name)                                                    \
    bgl##name = (type)SDL_GL_GetProcAddress("gl" #name);                           \
    if (bgl##name == NULL)                                                         \
    {                                                                              \
        SDL_Log("Battle FX: the OpenGL driver has no gl" #name "; GBA effects shown"); \
        return false;                                                              \
    }
    BFX_GL_FUNCS(BFX_GL_LOAD)
    return true;
}

/* ── The frames: copies of the core's packets ──────────────────────────── */

#define CAP_CARDS 4096
#define CAP_CARDS_BACK 1024
#define CAP_DECALS 512
#define CAP_DIST 512
#define CAP_PARTS_ADD 24576
#define CAP_PARTS_ALPHA 12288
#define CAP_RIBBON 16000
#define CAP_RIBBON_BACK 4000
#define CAP_MESH 160
#define MESH_FLOATS 19
#define MAX_LIGHTS 6

struct Quads
{
    int n;
    float *v[4]; /* P, S, C, X: cap * 4 floats each */
    int cap;
};

struct Ribbons
{
    int nv, ni;
    float *pos, *uv, *col, *row;
    uint16_t *idx;
    int cap; /* vertices; indices 3 * cap */
};

struct Frame
{
    bool valid;
    uint32_t serial;
    uint8_t hideBg;
    int frame;
    double t;
    float view[16], camera[16];
    struct Quads cards, cardsBack, decals, dist;
    struct Ribbons ribbons, ribbonsBack;
    int partsAddCount, partsAlphaCount;
    int meshKinds;
    int meshN[BFX_MAX_MESH_KINDS];
    float *meshData[BFX_MAX_MESH_KINDS];
    float lights[MAX_LIGHTS * 8];
    int lightCount;
    int pixelEffects;
    struct BfxBattler battlers[4];
    struct BfxPost post;
    float hemiSky[3], hemiGround[3], hemiI, sun[3], sunI, sunDir[3], fog[3], fogDensity;
};

/* The particle rings: every emission record the core wrote, mirrored here
 * and uploaded to the GPU in the ranges that changed (the records live on the
 * GPU; the shader places each particle at its age). */
struct Ring
{
    int cap;
    float *a[6];
    int lo, hi; /* dirty range, -1 = clean */
    GLuint vao, vbo[6];
};

/* A battler as the game shows it this picture (battle_fx_attach.h), and how the effects near it are placed for
 * that (Attach below). GBA screen pixels unless said otherwise; "texture space": the OAM entry's 64 x 64 box. */
#define SHOWN_TEX 256 /* the shown frame's texture (the pack's largest frame: 155 x 121) */
#define ATTACH_MEAN_FRAMES 64
struct Attach
{
    bool frame;                     /* drawn from its shown frame (glow, light, flash, depth) */
    bool measured;                  /* its look measured in the picture just drawn (MeasureLook) */
    bool on;                        /* the effects near it are placed for the shown body */
    struct BattleFxShownFrame view;
    float c[2], d[2], r[2], sc;     /* the GBA picture's body centre, the shift to the shown body's centre, the
                                     * region placed (half size), the scale (shown body / GBA picture's) */
    float feet[3], groundR;         /* world: its feet and the ground region's radius (decals) */
    float pivot[2];                 /* the shown body's centre now (the glow grows about it) */
    float crel[2];                  /* c from the entry's centre - where the core has the battler (gba) - for a
                                     * battler a script moved to a BG layer ("monbg"), no longer in OAM */
    /* the shown body's box (texture space), a running mean over the frames of one look: the look's frames
     * (an idle animation) move a little and the effects should not shake with them */
    uint32_t look;
    int gbaBox[4];                  /* the GBA picture's box when the look began */
    unsigned n;
    float box[4];
    /* the opaque box of the last frame scanned (frame space) */
    const uint8_t *boxPixels;
    int boxW, boxH;
    uint32_t boxStride;
    int fb[4];
    bool fbEmpty;
    /* the frame's texture: what was uploaded last */
    uint32_t texKey;
    uint32_t loggedLook;
};

/* The pace of the pictures with the HD layers (BattleFxGL_Pace). */
#define PACE_WARMUP_PICTURES 6 /* an animation's first pictures (textures uploaded, programs first used) */
struct Pace
{
    uint32_t serial;
    int tier;
    unsigned seen;            /* this animation's pictures with HD layers, warm-up included */
    unsigned gameFrames, skipped, presents, layers, gpuSamples;
    double presentMs, gpuMs, cpuMs;
    bool presented;           /* a present since the last frame boundary */
    bool lastHd;              /* the last frame boundary's picture had the HD layers */
    Uint64 lastPresentAt;
    double plainMs;           /* the battle's time between presents without HD layers (running mean) */
    unsigned plainCount;
};

static struct
{
    bool tried, ready, failed;
    struct Frame frames[2];
    int show; /* frames[show]: drawn with the picture; frames[show ^ 1]: the next one */
    Uint64 boundaryAt;
    struct Ring ringAdd, ringAlpha;
    int atlasGrid, rampRows;
    /* GL objects */
    GLuint progCard, progDist, progPart, progRib, progMesh, progBright, progDown, progUp, progComp;
    GLuint texAtlas, texRamps, texStrips;
    int rampsH;
    GLuint cornerVbo, quadIbo, fsqVao, fsqVbo;
    struct QuadGl { GLuint vao, vbo[4]; int cap; } qCards, qCardsBack, qDecals, qDist;
    struct RibGl { GLuint vao, vbo[4], ibo; int cap; } rRibbons, rRibbonsBack;
    struct MeshGl
    {
        GLuint vao, vbo, inst;
        int verts;
        float color[3], emissive[3], rim[3], roughness, metalness;
    } meshes[BFX_MAX_MESH_KINDS];
    int meshKinds;
    /* render targets (sized to the layer's region) */
    struct Target { GLuint fbo, tex, depth; int w, h; } layer, dist, bloom[5], dst;
    /* the game's battler sprites in the picture being drawn (BattleFxGL_TakeBattlers) */
    struct BattleFxGameBattler game[4];
    GLuint battlerTex[4];
    GLuint progBattler;
    /* the battlers as shown (battle_fx_attach.h): computed once a picture (attachSeq == pictureSeq) */
    struct Attach attach[4];
    GLuint shownTex[4];
    uint8_t *shownRgba; /* SHOWN_TEX x SHOWN_TEX x 4, for the uploads */
    unsigned pictureSeq, attachSeq, lookSeq;
    bool attachOn;      /* some battler's effects placed for its shown body this picture */
    bool attachInjected; /* the effect shaders took the placing code (else they draw as before) */
    float attachCam[16]; /* the GBA camera for the shaders: pos + tan(fov / 2), right, up, forward */
    /* the quality tier drawn (BattleFxGL_SetTier) and the pace measured */
    int tier;
    struct Pace pace;
    /* statistics for the log and the tests: per animation (serial), GPU time from timer queries
     * (read back a few frames later, never waited for), CPU time to submit the layers */
    unsigned layersDrawn, framesShown;
    GLuint queries[8];
    uint32_t querySerial[8];
    int queryTier[8];
    bool queryCounted[8]; /* in the pace's window (after its animation's first pictures) */
    int queryHead, queryPending;
    uint32_t statSerial;
    struct BattleFxRenderStats stats;
} s;

/* The copies are allocated at their full capacity the first time they are used. */
static void CopyQuads(struct Quads *q, const struct BfxQuads *src, int cap)
{
    int n = src->n < cap ? src->n : cap;

    q->n = 0;
    if (n <= 0 || src->P == NULL || src->S == NULL || src->C == NULL || src->X == NULL)
        return;
    if (q->cap == 0)
    {
        for (int i = 0; i < 4; i++)
            if ((q->v[i] = malloc((size_t)cap * 16)) == NULL)
                return;
        q->cap = cap;
    }
    memcpy(q->v[0], src->P, (size_t)n * 16);
    memcpy(q->v[1], src->S, (size_t)n * 16);
    memcpy(q->v[2], src->C, (size_t)n * 16);
    memcpy(q->v[3], src->X, (size_t)n * 16);
    q->n = n;
}

static void CopyRibbons(struct Ribbons *r, const struct BfxRibbons *src, int cap)
{
    int nv = src->nv < cap ? src->nv : cap;
    int ni = src->ni;

    r->nv = r->ni = 0;
    if (nv <= 0 || ni <= 0 || ni > cap * 3 || src->pos == NULL || src->uv == NULL || src->col == NULL
     || src->row == NULL || src->idx == NULL)
        return;
    if (r->cap == 0)
    {
        r->pos = malloc((size_t)cap * 3 * sizeof(float));
        r->uv = malloc((size_t)cap * 2 * sizeof(float));
        r->col = malloc((size_t)cap * 4 * sizeof(float));
        r->row = malloc((size_t)cap * 2 * sizeof(float));
        r->idx = malloc((size_t)cap * 3 * sizeof(uint16_t));
        if (!r->pos || !r->uv || !r->col || !r->row || !r->idx)
            return;
        r->cap = cap;
    }
    memcpy(r->pos, src->pos, (size_t)nv * 3 * sizeof(float));
    memcpy(r->uv, src->uv, (size_t)nv * 2 * sizeof(float));
    memcpy(r->col, src->col, (size_t)nv * 4 * sizeof(float));
    memcpy(r->row, src->row, (size_t)nv * 2 * sizeof(float));
    memcpy(r->idx, src->idx, (size_t)ni * sizeof(uint16_t));
    r->nv = nv;
    r->ni = ni;
}

static void RingTake(struct Ring *m, const struct BfxRing *r)
{
    const float *src[6] = {r->a, r->b, r->c, r->d, r->e, r->f};
    int lo = r->lo, hi = r->hi;

    if (r->a == NULL || r->cap <= 0)
        return;
    if (m->cap != r->cap)
    {
        for (int i = 0; i < 6; i++)
        {
            free(m->a[i]);
            m->a[i] = calloc((size_t)r->cap * 4, sizeof(float));
            if (m->a[i] == NULL)
            {
                m->cap = 0;
                return;
            }
        }
        m->cap = r->cap;
        m->lo = 0;
        m->hi = m->cap - 1;
    }
    if (r->cleared || lo < 0 || hi < 0 || lo > hi || hi >= m->cap)
    {
        if (!r->cleared && (lo < 0 || hi < 0))
            return; /* nothing new */
        lo = 0;
        hi = m->cap - 1;
    }
    for (int i = 0; i < 6; i++)
        memcpy(m->a[i] + (size_t)lo * 4, src[i] + (size_t)lo * 4, (size_t)(hi - lo + 1) * 16);
    if (m->lo < 0 || lo < m->lo)
        m->lo = lo;
    if (hi > m->hi)
        m->hi = hi;
}

static void CopyFrame(struct Frame *f, const struct BfxPacket *p)
{
    f->serial = p->serial;
    f->hideBg = p->hideBg;
    f->frame = p->frame;
    f->t = p->t;
    if (p->view)
        memcpy(f->view, p->view, sizeof(f->view));
    if (p->camera)
        memcpy(f->camera, p->camera, sizeof(f->camera));
    CopyQuads(&f->cards, &p->cards, CAP_CARDS);
    CopyQuads(&f->cardsBack, &p->cardsBack, CAP_CARDS_BACK);
    CopyQuads(&f->decals, &p->decals, CAP_DECALS);
    CopyQuads(&f->dist, &p->dist, CAP_DIST);
    CopyRibbons(&f->ribbons, &p->ribbons, CAP_RIBBON);
    CopyRibbons(&f->ribbonsBack, &p->ribbonsBack, CAP_RIBBON_BACK);
    f->partsAddCount = p->partsAdd.count;
    f->partsAlphaCount = p->partsAlpha.count;
    f->meshKinds = p->meshKinds;
    for (int k = 0; k < p->meshKinds; k++)
    {
        int n = p->meshes[k].n < CAP_MESH ? p->meshes[k].n : CAP_MESH;
        f->meshN[k] = 0;
        if (n <= 0 || p->meshes[k].data == NULL)
            continue;
        if (f->meshData[k] == NULL)
            f->meshData[k] = malloc((size_t)CAP_MESH * MESH_FLOATS * sizeof(float));
        if (f->meshData[k] == NULL)
            continue;
        memcpy(f->meshData[k], p->meshes[k].data, (size_t)n * MESH_FLOATS * sizeof(float));
        f->meshN[k] = n;
    }
    f->lightCount = p->lightCount < MAX_LIGHTS ? p->lightCount : MAX_LIGHTS;
    if (f->lightCount > 0 && p->lights)
        memcpy(f->lights, p->lights, (size_t)f->lightCount * 8 * sizeof(float));
    f->pixelEffects = p->pixelEffects;
    memcpy(f->battlers, p->battlers, sizeof(f->battlers));
    f->post = p->post;
    memcpy(f->hemiSky, p->hemiSky, sizeof(f->hemiSky));
    memcpy(f->hemiGround, p->hemiGround, sizeof(f->hemiGround));
    f->hemiI = p->hemiI;
    memcpy(f->sun, p->sun, sizeof(f->sun));
    f->sunI = p->sunI;
    memcpy(f->sunDir, p->sunDir, sizeof(f->sunDir));
    memcpy(f->fog, p->fog, sizeof(f->fog));
    f->fogDensity = p->fogDensity;
    f->valid = true;
}

void BattleFxGL_TakeBattlers(const struct BattleFxGameBattler battlers[4])
{
    memcpy(s.game, battlers, sizeof(s.game));
    s.pictureSeq++; /* the shown frames are read once this picture is drawn (Attach) */
}

void BattleFxGL_ShowPacket(const struct BfxPacket *packet, bool restartTime)
{
    s.show = 0;
    if (restartTime)
        s.boundaryAt = SDL_GetPerformanceCounter();
    s.frames[0].valid = false;
    if (packet == NULL || s.failed)
        return;
    CopyFrame(&s.frames[0], packet);
    RingTake(&s.ringAdd, &packet->partsAdd);
    RingTake(&s.ringAlpha, &packet->partsAlpha);
}

bool BattleFxGL_Showing(void)
{
    return !s.failed && s.frames[s.show].valid;
}

int BattleFxGL_ShowingPixelEffects(void)
{
    return s.frames[s.show].valid ? s.frames[s.show].pixelEffects : 0;
}

uint8_t BattleFxGL_ShowingHiddenBgs(void)
{
    return BattleFxGL_Showing() ? s.frames[s.show].hideBg : 0;
}

/* ── Loading: shaders, textures, meshes ───────────────────────────────── */

static char *ReadText(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *data = NULL;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0)
    {
        data = malloc((size_t)n + 1);
        if (data != NULL && fread(data, 1, (size_t)n, f) == (size_t)n)
            data[n] = '\0';
        else
        {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}

/* A shader file with its "//#include lit.glsl" line replaced by lit.glsl. */
static char *ReadShader(const char *name)
{
    char path[1200];
    char *src, *at, *lit, *out;
    static const char kInclude[] = "//#include lit.glsl";

    snprintf(path, sizeof(path), "%s/shaders/%s", BattleFx_DataDir(), name);
    src = ReadText(path);
    if (src == NULL || (at = strstr(src, kInclude)) == NULL)
        return src;
    snprintf(path, sizeof(path), "%s/shaders/lit.glsl", BattleFx_DataDir());
    lit = ReadText(path);
    if (lit == NULL)
    {
        free(src);
        return NULL;
    }
    out = malloc(strlen(src) + strlen(lit) + 1);
    if (out != NULL)
    {
        size_t head = (size_t)(at - src);
        memcpy(out, src, head);
        strcpy(out + head, lit);
        strcat(out, at + sizeof(kInclude) - 1);
    }
    free(src);
    free(lit);
    return out;
}

static GLuint Compile(GLenum type, const char *source, const char *name)
{
    GLuint sh = bglCreateShader(type);
    GLint ok = 0;

    bglShaderSource(sh, 1, &source, NULL);
    bglCompileShader(sh);
    bglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        bglGetShaderInfoLog(sh, sizeof(log), NULL, log);
        SDL_Log("Battle FX: shader %s did not compile: %s", name, log);
        bglDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Attribute locations shared by every program (bound before linking, as the
 * package's renderer does), so one vertex array serves several programs. */
static const struct { const char *name; int loc; } kAttribs[] = {
    {"position", 0}, {"corner", 1}, {"uv", 2}, {"normal", 3},
    {"iP", 4}, {"iSz", 5}, {"iCol", 6}, {"iCell", 7},
    {"iA", 4}, {"iB", 5}, {"iC", 6}, {"iD", 7}, {"iE", 8}, {"iF", 9},
    {"aUv", 4}, {"aCol", 5}, {"aRow", 6},
    {"iM0", 4}, {"iM1", 5}, {"iM2", 6}, {"iM3", 7}, {"iColor", 8}, {"vtxColor", 2},
};

static GLuint LinkSources(const char *vs, const char *fs, const char *name)
{
    GLuint v = Compile(GL_VERTEX_SHADER, vs, name), f = v ? Compile(GL_FRAGMENT_SHADER, fs, name) : 0, p;
    GLint ok = 0;

    if (!v || !f)
    {
        if (v)
            bglDeleteShader(v);
        return 0;
    }
    p = bglCreateProgram();
    bglAttachShader(p, v);
    bglAttachShader(p, f);
    for (size_t i = 0; i < sizeof(kAttribs) / sizeof(kAttribs[0]); i++)
        bglBindAttribLocation(p, (GLuint)kAttribs[i].loc, kAttribs[i].name);
    bglBindFragDataLocation(p, 0, "fragColor");
    bglLinkProgram(p);
    bglDeleteShader(v);
    bglDeleteShader(f);
    bglGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        bglGetProgramInfoLog(p, sizeof(log), NULL, log);
        SDL_Log("Battle FX: program %s did not link: %s", name, log);
        bglDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ── The effects placed for the battlers as shown (battle_fx_attach.h) ──
 *
 * The core places every effect for the GBA pictures (their positions on the GBA screen, at the battlers' depths).
 * Where a battler is shown by another frame, its body as shown has another centre and size: the effects near it
 * are moved with that body and scaled about its centre, in the GBA screen (each point lifted back at its own
 * depth, as the core lifts them), by a warp around each battler: in full inside its body's region, fading out
 * between 1 and 1.8 times the region's size, shared where two battlers' regions meet; big elements (a screen-wide
 * card) less, ground decals about the battler's feet on the ground. Code added to the effect shaders as they load
 * (the shader files are generated from the Battle FX Lab's: not edited); with uBfxOn 0 every expression is the
 * shader's own. Uniforms: uBfxCam - the GBA camera (position + tan(fov / 2), right, up, forward); per battler
 * uBfxA (body centre, shift), uBfxB (region half size, scale, on), uBfxF (feet, ground radius). */
static const char kAttachGlsl[] =
    "uniform int uBfxOn; uniform vec4 uBfxCam[4]; uniform vec4 uBfxA[4]; uniform vec4 uBfxB[4]; uniform vec4 uBfxF[4];\n"
    "vec2 bfxGba(vec3 p, out float d) { vec3 r = p - uBfxCam[0].xyz; d = dot(r, uBfxCam[3].xyz); float t = uBfxCam[0].w * max(d, 1e-3);\n"
    "  return vec2((dot(r, uBfxCam[1].xyz) / (1.5 * t) + 1.0) * 120.0, (1.0 - dot(r, uBfxCam[2].xyz) / t) * 80.0); }\n"
    "vec3 bfxWorld(vec2 g, float d) { float t = uBfxCam[0].w * d;\n"
    "  return uBfxCam[0].xyz + uBfxCam[1].xyz * ((g.x / 120.0 - 1.0) * 1.5 * t) + uBfxCam[2].xyz * ((1.0 - g.y / 80.0) * t)\n"
    "       + uBfxCam[3].xyz * d; }\n"
    "vec4 bfxWarp(vec3 p, float size) {\n"
    "  float d; vec2 g = bfxGba(p, d); if (d < 0.5) return vec4(p, 1.0);\n"
    "  float big = 1.0 - smoothstep(96.0, 176.0, size * 80.0 / (uBfxCam[0].w * d));\n"
    "  float w[4]; float sum = 0.0;\n"
    "  for (int b = 0; b < 4; b++) { w[b] = uBfxB[b].w > 0.5 ? 1.0 - smoothstep(1.0, 1.8, length((g - uBfxA[b].xy) / uBfxB[b].xy)) : 0.0;\n"
    "    sum += w[b]; }\n"
    "  float k = (sum > 1.0 ? 1.0 / sum : 1.0) * big; vec2 off = vec2(0.0); float grow = 0.0;\n"
    "  for (int b = 0; b < 4; b++) { off += w[b] * k * (uBfxA[b].zw + (uBfxB[b].z - 1.0) * (g - uBfxA[b].xy));\n"
    "    grow += w[b] * k * (uBfxB[b].z - 1.0); }\n"
    "  return vec4(bfxWorld(g + off, d), 1.0 + grow); }\n"
    "vec4 bfxGround(vec3 p) {\n"
    "  float w[4]; float sum = 0.0;\n"
    "  for (int b = 0; b < 4; b++) { vec2 e = p.xz - uBfxF[b].xz;\n"
    "    w[b] = uBfxB[b].w > 0.5 ? 1.0 - smoothstep(1.0, 1.8, length(e) / uBfxF[b].w) : 0.0; sum += w[b]; }\n"
    "  float k = sum > 1.0 ? 1.0 / sum : 1.0; vec2 off = vec2(0.0); float grow = 0.0;\n"
    "  for (int b = 0; b < 4; b++) { off += w[b] * k * (uBfxB[b].z - 1.0) * (p.xz - uBfxF[b].xz); grow += w[b] * k * (uBfxB[b].z - 1.0); }\n"
    "  return vec4(p.x + off.x, p.y, p.z + off.y, 1.0 + grow); }\n";

/* Where the placing goes in each effect shader: the line found, and what it becomes. */
static const struct
{
    const char *shader, *find, *with;
} kAttachHooks[] = {
    /* cards (and the distortion cards): the centre placed, the card sized; a ground decal about the feet */
    {"card.vert", "    gl_Position = projectionMatrix * viewMatrix * vec4(p, 1.0);",
     "    if (uBfxOn != 0) { vec4 bw = iP.w > 1.5 ? bfxGround(iP.xyz) : bfxWarp(iP.xyz, max(iSz.x, iSz.y));\n"
     "      p = bw.xyz + (iP.w > 1.5 ? vec3(q.x, 0.0, -q.y) : uRight * q.x + uUp * q.y) * bw.w; }\n"
     "    gl_Position = projectionMatrix * viewMatrix * vec4(p, 1.0);"},
    /* particles: where each is now (one flying from a battler to another leaves the one, reaches the other) */
    {"particle.vert", "    gl_Position = projectionMatrix * viewMatrix * vec4(p + R * q.x + U * q.y, 1.0);",
     "    if (uBfxOn != 0) { vec4 bw = bfxWarp(p, size); p = bw.xyz; q *= bw.w; }\n"
     "    gl_Position = projectionMatrix * viewMatrix * vec4(p + R * q.x + U * q.y, 1.0);"},
    /* ribbons: each vertex (a beam's ends follow the two bodies) */
    {"ribbon.vert", "vec4(position, 1.0)", "vec4(uBfxOn != 0 ? bfxWarp(position, 0.0).xyz : position, 1.0)"},
    /* meshes: the instance's centre placed, the mesh sized */
    {"mesh.vert", "vec4 w = M * vec4(position, 1.0);",
     "vec4 w = M * vec4(position, 1.0);\n"
     "  if (uBfxOn != 0) { vec4 bw = bfxWarp(iM3.xyz, 2.0 * max(length(iM0.xyz), max(length(iM1.xyz), length(iM2.xyz))));\n"
     "    w = vec4(bw.xyz + (w.xyz - iM3.xyz) * bw.w, 1.0); }"},
};

/* `src` with the placing code (a new string), NULL when `name` takes none or its line is not there. */
static char *WithAttach(const char *name, const char *src)
{
    const char *at, *entry;
    char *out;
    size_t n;

    for (size_t i = 0; i < sizeof(kAttachHooks) / sizeof(kAttachHooks[0]); i++)
    {
        if (strcmp(name, kAttachHooks[i].shader) != 0)
            continue;
        at = strstr(src, kAttachHooks[i].find);
        entry = strstr(src, "void main");
        if (at == NULL || entry == NULL || entry > at || strstr(at + 1, kAttachHooks[i].find) != NULL)
            return NULL;
        while (entry > src && entry[-1] != '\n')
            entry--;
        n = strlen(src) + sizeof(kAttachGlsl) + strlen(kAttachHooks[i].with) + 1;
        if ((out = malloc(n)) == NULL)
            return NULL;
        snprintf(out, n, "%.*s%s%.*s%s%s", (int)(entry - src), src, kAttachGlsl, (int)(at - entry), entry,
                 kAttachHooks[i].with, at + strlen(kAttachHooks[i].find));
        return out;
    }
    return NULL;
}

/* Whether every effect shader takes the placing code (all of them, or none: the effects must move together). */
static bool AttachFits(void)
{
    for (size_t i = 0; i < sizeof(kAttachHooks) / sizeof(kAttachHooks[0]); i++)
    {
        char *src = ReadShader(kAttachHooks[i].shader), *with = src ? WithAttach(kAttachHooks[i].shader, src) : NULL;
        bool ok = with != NULL;

        free(src);
        free(with);
        if (!ok)
        {
            SDL_Log("Battle FX: %s is not the shader the placing for the battlers as shown expects; the effects are "
                    "placed for the GBA pictures", kAttachHooks[i].shader);
            return false;
        }
    }
    return true;
}

static GLuint LoadProgram(const char *vsName, const char *fsName)
{
    char *vs = ReadShader(vsName), *fs = ReadShader(fsName);
    char name[96];
    GLuint p = 0;

    snprintf(name, sizeof(name), "%s + %s", vsName, fsName);
    if (vs != NULL && s.attachInjected)
    {
        char *with = WithAttach(vsName, vs);

        if (with != NULL)
        {
            free(vs);
            vs = with;
        }
    }
    if (vs == NULL || fs == NULL)
        SDL_Log("Battle FX: cannot read the shader %s in %s/shaders", vs == NULL ? vsName : fsName, BattleFx_DataDir());
    else
        p = LinkSources(vs, fs, name);
    free(vs);
    free(fs);
    return p;
}

/* The game's own composite (not one of the package's shaders): the layer over
 * a copy of the window, in linear light. */
static const char kCompositeVs[] =
    "#version 330 core\n"
    "in vec3 position; in vec2 uv; out vec2 vUv;\n"
    "void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }\n";
static const char kCompositeFs[] =
    "#version 330 core\n"
    "out vec4 fragColor; in vec2 vUv;\n"
    "uniform sampler2D tLayer; uniform sampler2D tBloom; uniform sampler2D tDist; uniform sampler2D tDst;\n"
    "uniform float uBloom; uniform float uDist; uniform vec2 uDistScale; uniform int uWhole;\n"
    "uniform float uSat; uniform vec4 uGrade; uniform vec3 uFlashCol; uniform float uFlash;\n"
    "vec3 toLin(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }\n"
    "vec3 toSRGB(vec3 c) { c = clamp(c, 0.0, 1.0); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }\n"
    /* Highlights over 1 roll off with their hue kept, as the Lab's knee does past 0.78 (here past 1, so
     * the picture under no effect keeps its colours exactly) */
    "vec3 roll(vec3 c) { float m = max(max(c.r, c.g), c.b); if (m <= 1.0) return c;\n"
    "  float k = 1.0 + 0.25 * (1.0 - exp(-(m - 1.0) / 0.25)); vec3 h = c * (min(k, 1.0) / m);\n"
    "  return mix(h, clamp(c, 0.0, 1.0), 0.3); }\n"
    "void main() {\n"
    "  vec2 off = texture(tDist, vUv).rg * uDist * uDistScale; vec2 uv = clamp(vUv + off, vec2(0.0005), vec2(0.9995));\n"
    "  vec4 L = texture(tLayer, uv); vec3 B = texture(tBloom, uv).rgb * uBloom;\n"
    "  if (uWhole == 0 && L.a <= 0.0 && dot(L.rgb, vec3(1.0)) <= 0.0 && dot(B, vec3(1.0)) < 0.0004 && dot(off, off) < 1e-12) discard;\n"
    "  vec3 c = L.rgb + toLin(texture(tDst, uv).rgb) * (1.0 - L.a) + B;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722)); c = mix(vec3(l), c, uSat);\n"
    "  c = mix(c, c * uGrade.rgb * 1.25, uGrade.a);\n"
    "  c += uFlashCol * uFlash;\n"
    "  fragColor = vec4(toSRGB(roll(c)), 1.0);\n"
    "}\n";

/* The game's own battler sprite, drawn over the GBA picture in GBA screen space with the GBA's
 * OBJ rules (gba_easy_draw.c: integer pixel, 8.8 matrix, size / 2 centre), for the HD battler
 * state Battle FX adds to it (the package's game_sprite shader's maths):
 *   mode 0: glow - an additive silhouette in the glow colour, scaled by uScale (behind the battler)
 *   mode 1: light and flash - the battler's own colours lit by the effects and flashed
 *   mode 2: depth only - at the battler's depth, so effect meshes behind it are hidden
 * uFrame 1: the battler as the animated battle sprites show it (battle_fx_attach.h) - their frame at uOff in the
 * entry's box, as large as it is, drawn as gba_easy_draw.c DrawLibraryObjLine draws it (no row from uClip down);
 * its glow grows about the body's centre (uPivot). uFrame 0: the GBA picture's tiles, exactly as before. */
static const char kBattlerVs[] =
    "#version 330 core\n"
    "in vec2 corner; uniform vec2 uCenter; uniform vec2 uBox; uniform vec4 uMap; out vec2 vG;\n"
    "void main() { vec2 g = uCenter + corner * uBox; vG = g; gl_Position = vec4(g * uMap.xy + uMap.zw, 0.0, 1.0); }\n";
static const char kBattlerFs[] =
    "#version 330 core\n"
    "out vec4 fragColor; in vec2 vG;\n"
    "uniform sampler2D uTex; uniform vec2 uCenter; uniform vec2 uSize; uniform vec4 uM; uniform vec2 uFlip; uniform int uMode;\n"
    "uniform float uScale; uniform vec4 uGlow; uniform vec4 uFlash; uniform vec3 uLight; uniform vec3 uLightAdd; uniform float uDepth;\n"
    "uniform int uFrame; uniform vec2 uOff; uniform vec2 uFrameSize; uniform float uTexSize; uniform float uClip; uniform vec2 uPivot;\n"
    "vec3 toLin(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }\n"
    "void main() {\n"
    "  vec2 l = uMode == 0 ? (uFrame == 0 ? (vG - uCenter) / uScale : (vG - uPivot) / uScale + (uPivot - uCenter))\n"
    "                      : floor(vG) - uCenter;\n"
    "  vec2 t = vec2(uM.x * l.x + uM.y * l.y, uM.z * l.x + uM.w * l.y) / 256.0;\n"
    "  vec2 tp = (uMode == 0 ? t : floor(t)) + uSize * 0.5;\n"
    "  vec4 c;\n"
    "  if (uFrame == 0) {\n"
    "    if (tp.x < 0.0 || tp.y < 0.0 || tp.x >= uSize.x || tp.y >= uSize.y) discard;\n"
    "    if (uFlip.x > 0.5) tp.x = uSize.x - floor(tp.x) - 1.0;\n"
    "    if (uFlip.y > 0.5) tp.y = uSize.y - floor(tp.y) - 1.0;\n"
    "    c = texture(uTex, (floor(tp) + 0.5) / 64.0);\n"
    "  } else {\n"
    "    if (floor(vG.y) >= uClip) discard;\n"
    "    if (uFlip.x > 0.5) tp.x = uSize.x - floor(tp.x) - 1.0;\n"
    "    if (uFlip.y > 0.5) tp.y = uSize.y - floor(tp.y) - 1.0;\n"
    "    vec2 fp = floor(tp) - uOff;\n"
    "    if (fp.x < 0.0 || fp.y < 0.0 || fp.x >= uFrameSize.x || fp.y >= uFrameSize.y) discard;\n"
    "    c = texture(uTex, (fp + 0.5) / uTexSize);\n"
    "  }\n"
    "  if (c.a < 0.5) discard;\n"
    "  if (uMode == 0) { fragColor = vec4(uGlow.rgb * uGlow.a, 0.0); return; }\n"
    "  gl_FragDepth = uDepth;\n"
    "  vec3 lin = mix(toLin(c.rgb), uFlash.rgb, uFlash.a);\n"
    "  lin = min(lin * uLight + uLightAdd, vec3(1.0));\n"
    "  fragColor = vec4(lin, 1.0);\n"
    "}\n";

static GLuint LoadTexture(const char *file, bool flipY, bool mips, GLenum wrapS, int *heightOut)
{
    char path[1200];
    int w = 0, h = 0;
    unsigned char *px;
    GLuint t;

    snprintf(path, sizeof(path), "%s/assets/%s", BattleFx_DataDir(), file);
    px = BfxStb_LoadPng(path, &w, &h, flipY ? 1 : 0);
    if (px == NULL)
    {
        SDL_Log("Battle FX: cannot read %s (%s)", path, BfxStb_Error());
        return 0;
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    BfxStb_Free(px);
    if (mips)
    {
        bglGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (heightOut)
        *heightOut = h;
    return t;
}

static bool LoadInfo(void)
{
    char path[1200];
    char *text, *line;

    snprintf(path, sizeof(path), "%s/assets/info.txt", BattleFx_DataDir());
    text = ReadText(path);
    if (text == NULL)
    {
        SDL_Log("Battle FX: cannot read %s", path);
        return false;
    }
    s.atlasGrid = 14;
    for (line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n"))
        if (strncmp(line, "atlasGrid ", 10) == 0)
            s.atlasGrid = atoi(line + 10);
    free(text);
    return s.atlasGrid > 0;
}

static bool LoadMeshes(void)
{
    char path[1200];
    FILE *f;
    char magic[4];
    uint32_t version = 0, kinds = 0;
    bool ok = true;

    snprintf(path, sizeof(path), "%s/assets/meshes.bin", BattleFx_DataDir());
    f = fopen(path, "rb");
    if (f == NULL)
    {
        SDL_Log("Battle FX: cannot read %s", path);
        return false;
    }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "BFXM", 4) != 0 || fread(&version, 4, 1, f) != 1
     || fread(&kinds, 4, 1, f) != 1 || version != 1 || kinds > BFX_MAX_MESH_KINDS)
    {
        SDL_Log("Battle FX: %s is not a Battle FX mesh file", path);
        fclose(f);
        return false;
    }
    s.meshKinds = (int)kinds;
    for (uint32_t k = 0; k < kinds && ok; k++)
    {
        struct MeshGl *me = &s.meshes[k];
        char name[16];
        uint32_t nv = 0;
        float mat[11];
        float *v;

        ok = fread(name, 1, 16, f) == 16 && fread(&nv, 4, 1, f) == 1 && nv > 0 && nv < 100000
          && fread(mat, 4, 11, f) == 11;
        if (!ok)
            break;
        v = malloc((size_t)nv * 9 * sizeof(float));
        ok = v != NULL && fread(v, sizeof(float) * 9, nv, f) == nv;
        if (ok)
        {
            me->verts = (int)nv;
            memcpy(me->color, mat, 3 * sizeof(float));
            memcpy(me->emissive, mat + 3, 3 * sizeof(float));
            memcpy(me->rim, mat + 6, 3 * sizeof(float));
            me->roughness = mat[9];
            me->metalness = mat[10];
            bglGenVertexArrays(1, &me->vao);
            bglBindVertexArray(me->vao);
            bglGenBuffers(1, &me->vbo);
            bglBindBuffer(GL_ARRAY_BUFFER, me->vbo);
            bglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * 36, v, GL_STATIC_DRAW);
            bglEnableVertexAttribArray(0);
            bglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void *)0);
            bglEnableVertexAttribArray(3);
            bglVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 36, (void *)12);
            bglEnableVertexAttribArray(2);
            bglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, (void *)24);
            bglGenBuffers(1, &me->inst);
            bglBindBuffer(GL_ARRAY_BUFFER, me->inst);
            bglBufferData(GL_ARRAY_BUFFER, CAP_MESH * MESH_FLOATS * 4, NULL, GL_DYNAMIC_DRAW);
            for (int c = 0; c < 4; c++)
            {
                bglEnableVertexAttribArray(4 + c);
                bglVertexAttribPointer(4 + c, 4, GL_FLOAT, GL_FALSE, MESH_FLOATS * 4, (void *)(intptr_t)(c * 16));
                bglVertexAttribDivisor(4 + c, 1);
            }
            bglEnableVertexAttribArray(8);
            bglVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, MESH_FLOATS * 4, (void *)64);
            bglVertexAttribDivisor(8, 1);
        }
        free(v);
    }
    fclose(f);
    bglBindVertexArray(0);
    if (!ok)
        SDL_Log("Battle FX: %s is damaged", path);
    return ok;
}

static void CornerVao(GLuint *vao)
{
    bglGenVertexArrays(1, vao);
    bglBindVertexArray(*vao);
    bglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.quadIbo);
    bglBindBuffer(GL_ARRAY_BUFFER, s.cornerVbo);
    bglEnableVertexAttribArray(1);
    bglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
}

static void InstanceVbo(GLuint *vbo, int loc, int cap, int comps)
{
    bglGenBuffers(1, vbo);
    bglBindBuffer(GL_ARRAY_BUFFER, *vbo);
    bglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cap * comps * 4, NULL, GL_DYNAMIC_DRAW);
    bglEnableVertexAttribArray((GLuint)loc);
    bglVertexAttribPointer((GLuint)loc, comps, GL_FLOAT, GL_FALSE, comps * 4, (void *)0);
    bglVertexAttribDivisor((GLuint)loc, 1);
}

static void MakeQuads(struct QuadGl *q, int cap)
{
    q->cap = cap;
    CornerVao(&q->vao);
    for (int i = 0; i < 4; i++)
        InstanceVbo(&q->vbo[i], 4 + i, cap, 4);
}

static void MakeRing(struct Ring *r, int cap)
{
    CornerVao(&r->vao);
    for (int i = 0; i < 6; i++)
        InstanceVbo(&r->vbo[i], 4 + i, cap, 4);
    (void)cap;
}

static void MakeRibbons(struct RibGl *r, int cap)
{
    static const int comps[4] = {3, 2, 4, 2}, locs[4] = {0, 4, 5, 6};

    r->cap = cap;
    bglGenVertexArrays(1, &r->vao);
    bglBindVertexArray(r->vao);
    for (int i = 0; i < 4; i++)
    {
        bglGenBuffers(1, &r->vbo[i]);
        bglBindBuffer(GL_ARRAY_BUFFER, r->vbo[i]);
        bglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cap * comps[i] * 4, NULL, GL_DYNAMIC_DRAW);
        bglEnableVertexAttribArray((GLuint)locs[i]);
        bglVertexAttribPointer((GLuint)locs[i], comps[i], GL_FLOAT, GL_FALSE, comps[i] * 4, (void *)0);
    }
    bglGenBuffers(1, &r->ibo);
    bglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    bglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cap * 3 * 2, NULL, GL_DYNAMIC_DRAW);
}

static bool InitGL(void)
{
    static const float corner[8] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    static const uint16_t qi[6] = {0, 1, 2, 0, 2, 3};
    static const float fsq[20] = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, 1, 1, 0, 1, 1, -1, 1, 0, 0, 1};
    Uint64 t0 = SDL_GetPerformanceCounter();

    s.tried = true;
    if (!LoadFunctions() || !LoadInfo())
        return false;
    /* the effect programs with the placing for the battlers as shown (WithAttach), or all without it */
    s.attachInjected = AttachFits();
#ifdef BATTLEFX_TEST
    if (BattleFxTest_PlainShaders())
        s.attachInjected = false;
#endif
    for (int attempt = 0; attempt < 2; attempt++)
    {
        GLuint *progs[5] = {&s.progCard, &s.progDist, &s.progPart, &s.progRib, &s.progMesh};
        bool ok;

        s.progCard = LoadProgram("card.vert", "card.frag");
        s.progDist = LoadProgram("card.vert", "dist.frag");
        s.progPart = LoadProgram("particle.vert", "particle.frag");
        s.progRib = LoadProgram("ribbon.vert", "ribbon.frag");
        s.progMesh = LoadProgram("mesh.vert", "mesh.frag");
        ok = s.progCard && s.progDist && s.progPart && s.progRib && s.progMesh;
        if (ok || !s.attachInjected)
            break;
        SDL_Log("Battle FX: the effect shaders did not build with the placing for the battlers as shown; built "
                "without it (the effects are placed for the GBA pictures)");
        for (int i = 0; i < 5; i++)
            if (*progs[i])
            {
                bglDeleteProgram(*progs[i]);
                *progs[i] = 0;
            }
        s.attachInjected = false;
    }
    s.progBright = LoadProgram("fsq.vert", "bright.frag");
    s.progDown = LoadProgram("fsq.vert", "down.frag");
    s.progUp = LoadProgram("fsq.vert", "up.frag");
    s.progComp = LinkSources(kCompositeVs, kCompositeFs, "battle_fx composite");
    s.progBattler = LinkSources(kBattlerVs, kBattlerFs, "battle_fx battler");
    if (!s.progCard || !s.progDist || !s.progPart || !s.progRib || !s.progMesh || !s.progBright || !s.progDown
     || !s.progUp || !s.progComp || !s.progBattler)
        return false;
    bglGenQueries(8, s.queries);
    glGenTextures(4, s.battlerTex);
    glGenTextures(4, s.shownTex);
    for (int b = 0; b < 8; b++)
    {
        int side = b < 4 ? BFX_GAME_SPRITE_MAX : SHOWN_TEX;

        glBindTexture(GL_TEXTURE_2D, b < 4 ? s.battlerTex[b] : s.shownTex[b - 4]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if ((s.shownRgba = malloc((size_t)SHOWN_TEX * SHOWN_TEX * 4)) == NULL)
        return false;
    /* atlas / strips as three.js uploaded them, with flipY (image row 0 at v = 1); ramps without */
    s.texAtlas = LoadTexture("atlas.png", true, true, GL_CLAMP_TO_EDGE, NULL);
    s.texRamps = LoadTexture("ramps.png", false, false, GL_CLAMP_TO_EDGE, &s.rampsH);
    s.texStrips = LoadTexture("strips.png", true, false, GL_REPEAT, NULL);
    if (!s.texAtlas || !s.texRamps || !s.texStrips)
        return false;
    bglGenBuffers(1, &s.cornerVbo);
    bglBindBuffer(GL_ARRAY_BUFFER, s.cornerVbo);
    bglBufferData(GL_ARRAY_BUFFER, sizeof(corner), corner, GL_STATIC_DRAW);
    bglGenBuffers(1, &s.quadIbo);
    bglGenVertexArrays(1, &s.fsqVao);
    bglBindVertexArray(s.fsqVao);
    bglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.quadIbo);
    bglBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qi), qi, GL_STATIC_DRAW);
    bglGenBuffers(1, &s.fsqVbo);
    bglBindBuffer(GL_ARRAY_BUFFER, s.fsqVbo);
    bglBufferData(GL_ARRAY_BUFFER, sizeof(fsq), fsq, GL_STATIC_DRAW);
    bglEnableVertexAttribArray(0);
    bglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void *)0);
    bglEnableVertexAttribArray(2);
    bglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
    MakeQuads(&s.qCards, CAP_CARDS);
    MakeQuads(&s.qCardsBack, CAP_CARDS_BACK);
    MakeQuads(&s.qDecals, CAP_DECALS);
    MakeQuads(&s.qDist, CAP_DIST);
    MakeRing(&s.ringAdd, CAP_PARTS_ADD);
    MakeRing(&s.ringAlpha, CAP_PARTS_ALPHA);
    MakeRibbons(&s.rRibbons, CAP_RIBBON);
    MakeRibbons(&s.rRibbonsBack, CAP_RIBBON_BACK);
    bglBindVertexArray(0);
    if (!LoadMeshes())
        return false;
    s.ready = true;
    SDL_Log("Battle FX: renderer ready in %.0f ms (%s)%s",
            (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency(), BattleFx_DataDir(),
            BattleFx_ShownFramesBuilt() ? (s.attachInjected ? "; effects placed for the battlers as shown"
                                                            : "; effects placed for the GBA pictures")
                                        : "");
    return true;
}

static bool Ready(void)
{
    if (s.ready)
        return true;
    if (s.failed || s.tried)
        return false;
    if (!InitGL())
    {
        s.failed = true;
        SDL_Log("Battle FX: the renderer did not start; the original GBA effects are shown");
        return false;
    }
    return true;
}

/* ── Render targets ───────────────────────────────────────────────────── */

static void MakeTarget(struct Target *t, int w, int h, bool depth, GLenum ifmt)
{
    if (t->fbo && t->w == w && t->h == h)
        return;
    if (t->fbo)
    {
        bglDeleteFramebuffers(1, &t->fbo);
        glDeleteTextures(1, &t->tex);
        if (t->depth)
            bglDeleteRenderbuffers(1, &t->depth);
        memset(t, 0, sizeof(*t));
    }
    t->w = w;
    t->h = h;
    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, GL_RGBA, ifmt == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    bglGenFramebuffers(1, &t->fbo);
    bglBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    bglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    if (depth)
    {
        bglGenRenderbuffers(1, &t->depth);
        bglBindRenderbuffer(GL_RENDERBUFFER, t->depth);
        bglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        bglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depth);
    }
}

/* The quality tiers (battle_fx_render.h): the layer's share of the window's resolution, bloom levels, distortion. */
static const struct
{
    const char *name;
    float scale;
    int bloomLevels;
    bool distortion;
} kTiers[BFX_TIER_COUNT] = {
    {"full", 1.0f, 5, true},
    {"high", 0.75f, 5, true},
    {"medium", 0.5f, 4, true},
    {"low", 0.5f, 0, false},
};

void BattleFxGL_SetTier(int tier)
{
    s.tier = tier < 0 ? 0 : tier >= BFX_TIER_COUNT ? BFX_TIER_COUNT - 1 : tier;
}

int BattleFxGL_Tier(void)
{
    return s.tier;
}

const char *BattleFxGL_TierName(int tier)
{
    return tier >= 0 && tier < BFX_TIER_COUNT ? kTiers[tier].name : "?";
}

/* The window's region w x h: the copy of the window at its size; the layer, its distortion offsets (half the
 * layer's size) and bloom chain at the tier's share of it. */
static void EnsureTargets(int w, int h)
{
    int lw = (int)(w * kTiers[s.tier].scale + 0.5f), lh = (int)(h * kTiers[s.tier].scale + 0.5f);

    lw = lw < 4 ? 4 : lw;
    lh = lh < 4 ? 4 : lh;
    MakeTarget(&s.layer, lw, lh, true, GL_RGBA16F);
    MakeTarget(&s.dist, lw > 3 ? lw >> 1 : 2, lh > 3 ? lh >> 1 : 2, false, GL_RGBA16F);
    for (int i = 0; i < 5; i++)
        MakeTarget(&s.bloom[i], (lw >> (i + 1)) > 2 ? lw >> (i + 1) : 2, (lh >> (i + 1)) > 2 ? lh >> (i + 1) : 2, false,
                   GL_RGBA16F);
    MakeTarget(&s.dst, w, h, false, GL_RGBA8);
}

/* ── Drawing ──────────────────────────────────────────────────────────── */

struct Camera
{
    float proj[16], view[16], right[3], up[3], pos[3];
};

/* The core's camera (the GBA framing: settings camera "off") with its picture
 * - a 240 x 160 GBA screen - placed where the battle's picture is on the
 * window, inside the layer's region (the window above the text box). */
static void MakeCamera(const struct Frame *f, const struct BattleFxLayout *L, int regionH, struct Camera *c)
{
    const float fov = f->camera[12] > 1.0f ? f->camera[12] : 22.0f;
    const float n = f->camera[13] > 0.0f ? f->camera[13] : 0.1f, fa = f->camera[14] > n ? f->camera[14] : 500.0f;
    const float top = n * tanf(fov * 3.14159265f / 360.0f), right = top * 1.5f;
    float P[16] = {0};
    float ax = L->gameW / L->outW, bx = (2.0f * L->gameX + L->gameW) / L->outW - 1.0f;
    float ay = L->gameH / regionH, by = 1.0f - (2.0f * L->gameY + L->gameH) / regionH;

    P[0] = n / right;
    P[5] = n / top;
    P[10] = -(fa + n) / (fa - n);
    P[11] = -1.0f;
    P[14] = -2.0f * fa * n / (fa - n);
    /* clip' = M * clip: x' = ax x + bx w, y' = ay y + by w */
    memset(c->proj, 0, sizeof(c->proj));
    for (int col = 0; col < 4; col++)
    {
        float x = P[col * 4 + 0], y = P[col * 4 + 1], z = P[col * 4 + 2], w = P[col * 4 + 3];
        c->proj[col * 4 + 0] = ax * x + bx * w;
        c->proj[col * 4 + 1] = ay * y + by * w;
        c->proj[col * 4 + 2] = z;
        c->proj[col * 4 + 3] = w;
    }
    memcpy(c->view, f->view, sizeof(c->view));
    for (int i = 0; i < 3; i++)
    {
        c->pos[i] = f->camera[i];
        c->right[i] = f->camera[3 + i];
        c->up[i] = f->camera[6 + i];
    }
}

/* ── The battlers as shown: their frames and the effects' placing ────── */

static float Smooth(float a, float b, float x)
{
    float t = (x - a) / (b - a);

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t Fnv(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

/* The inverse of an OAM entry's affine matrix (texture -> screen), false when it has none. */
static bool InverseMatrix(const struct BattleFxGameBattler *g, float inv[4])
{
    double det = (double)g->m[0] * g->m[3] - (double)g->m[1] * g->m[2];

    if (det == 0.0)
        return false;
    inv[0] = (float)(256.0 * g->m[3] / det);
    inv[1] = (float)(-256.0 * g->m[1] / det);
    inv[2] = (float)(-256.0 * g->m[2] / det);
    inv[3] = (float)(256.0 * g->m[0] / det);
    return true;
}

/* A point of an OAM entry's texture space (its box, 0 .. w) on the GBA screen, as the GBA draws the entry: flipped,
 * or through the affine matrix about the box's centre (double size: the centre w from the corner). */
static void BoxToScreen(const struct BattleFxGameBattler *g, float tx, float ty, float out[2])
{
    float inv[4];

    if (!g->affine)
    {
        out[0] = g->x + (g->hflip ? g->w - tx : tx);
        out[1] = g->y + (g->vflip ? g->h - ty : ty);
    }
    else
    {
        float cx = g->x + (g->doubleSize ? (float)g->w : g->w * 0.5f), cy = g->y + (g->doubleSize ? (float)g->h : g->h * 0.5f);
        float lx = tx - g->w * 0.5f, ly = ty - g->h * 0.5f;

        if (!InverseMatrix(g, inv))
        {
            out[0] = cx;
            out[1] = cy;
            return;
        }
        out[0] = cx + inv[0] * lx + inv[1] * ly;
        out[1] = cy + inv[2] * lx + inv[3] * ly;
    }
}

/* Half sizes rx, ry of the entry's texture space on the screen (an affine entry: the box around the turned box). */
static void ExtentToScreen(const struct BattleFxGameBattler *g, float rx, float ry, float out[2])
{
    float inv[4];

    if (!g->affine || !InverseMatrix(g, inv))
    {
        out[0] = rx;
        out[1] = ry;
        return;
    }
    out[0] = fabsf(inv[0]) * rx + fabsf(inv[1]) * ry;
    out[1] = fabsf(inv[2]) * rx + fabsf(inv[3]) * ry;
}

/* The shown frame on the screen, relative to the entry's centre (uCenter): its box lo .. hi. */
static void ShownBounds(const struct BattleFxGameBattler *g, const struct BattleFxShownFrame *v, float lo[2], float hi[2])
{
    float cx = g->x + (g->doubleSize ? (float)g->w : g->w * 0.5f), cy = g->y + (g->doubleSize ? (float)g->h : g->h * 0.5f);

    lo[0] = lo[1] = 1e9f;
    hi[0] = hi[1] = -1e9f;
    for (int k = 0; k < 4; k++)
    {
        float p[2];

        BoxToScreen(g, (float)(v->left + ((k & 1) ? v->width : 0)), (float)(v->top + ((k & 2) ? v->height : 0)), p);
        p[0] -= cx;
        p[1] -= cy;
        for (int i = 0; i < 2; i++)
        {
            lo[i] = p[i] < lo[i] ? p[i] : lo[i];
            hi[i] = p[i] > hi[i] ? p[i] : hi[i];
        }
    }
}

/* The frame's opaque box (frame space; x1, y1 exclusive), kept for the frame last scanned. */
static void ScanFrame(struct Attach *a)
{
    const struct BattleFxShownFrame *v = &a->view;
    int x0 = v->width, y0 = v->height, x1 = -1, y1 = -1;

    if (a->boxPixels == v->pixels && a->boxW == v->width && a->boxH == v->height && a->boxStride == v->stride)
        return;
    for (int y = 0; y < v->height; y++)
    {
        const uint8_t *row = v->pixels + (size_t)y * v->stride;

        for (int x = 0; x < v->width; x++)
            if (row[x] != 0)
            {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y;
            }
    }
    a->boxPixels = v->pixels;
    a->boxW = v->width;
    a->boxH = v->height;
    a->boxStride = v->stride;
    a->fbEmpty = x1 < 0;
    a->fb[0] = x0;
    a->fb[1] = y0;
    a->fb[2] = x1 + 1;
    a->fb[3] = y1 + 1;
}

/* The GBA picture's opaque box (texture space), false when it shows nothing. */
static bool GbaBox(const struct BattleFxGameBattler *g, int box[4])
{
    int x0 = g->w, y0 = g->h, x1 = -1, y1 = -1;

    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            if (g->rgba[(y * BFX_GAME_SPRITE_MAX + x) * 4 + 3] != 0)
            {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y;
            }
    if (x1 < 0)
        return false;
    box[0] = x0;
    box[1] = y0;
    box[2] = x1 + 1;
    box[3] = y1 + 1;
    return true;
}

/* Battler b in the picture just drawn (every battle picture, BattleFxGL_DrawBack): drawn from a shown frame or
 * not, and that look measured - the GBA picture's body and the shown body (a running mean over the look's
 * frames), the shift between their centres, the scale, the region placed - in GBA screen pixels; c is also kept
 * relative to the entry's centre (crel), where the core has the battler, for when a script moves it to a BG
 * layer. Needs no frame of the core's. */
static void MeasureLook(int b)
{
    struct Attach *a = &s.attach[b];
    const struct BattleFxGameBattler *g = &s.game[b];
    int gb[4];
    float cur[4], cg[2], hg[2], cs[2], hs[2], rt[2], shown[2], centre[2];
    uint32_t look = 2166136261u;

    a->frame = a->measured = false;
    if (!g->shown)
        return; /* not in OAM (a BG copy, or not on screen): the look measured last stays */
    if (!BattleFx_ShownFrame(g->oam, &a->view) || a->view.width > SHOWN_TEX || a->view.height > SHOWN_TEX)
    {
        a->n = 0; /* drawn from its GBA picture (a form the pack has not): no look of a shown frame */
        return;
    }
    a->frame = true;
    ScanFrame(a);
    if (a->fbEmpty)
        return;
    BoxToScreen(g, a->view.left + (a->fb[0] + a->fb[2]) * 0.5f, a->view.top + (a->fb[1] + a->fb[3]) * 0.5f, a->pivot);
    if (!GbaBox(g, gb))
        return;
    /* one look: the same canvas (each facing of a Pokemon has its own) and about the same GBA picture (a new
     * Pokemon, form or facing starts anew; the GBA picture's own second frame, a pixel or two off, does not) */
    look = Fnv(look, &a->view.width, sizeof(a->view.width));
    look = Fnv(look, &a->view.height, sizeof(a->view.height));
    look = Fnv(look, &a->view.left, sizeof(a->view.left));
    look = Fnv(look, &a->view.top, sizeof(a->view.top));
    if (look != a->look || a->n == 0 || abs(gb[0] - a->gbaBox[0]) > 4 || abs(gb[1] - a->gbaBox[1]) > 4
     || abs(gb[2] - a->gbaBox[2]) > 4 || abs(gb[3] - a->gbaBox[3]) > 4)
    {
        a->look = look;
        a->n = 0;
        memcpy(a->gbaBox, gb, sizeof(a->gbaBox));
    }
    cur[0] = (float)(a->view.left + a->fb[0]);
    cur[1] = (float)(a->view.top + a->fb[1]);
    cur[2] = (float)(a->view.left + a->fb[2]);
    cur[3] = (float)(a->view.top + a->fb[3]);
    if (a->n < ATTACH_MEAN_FRAMES)
        a->n++;
    for (int i = 0; i < 4; i++)
        a->box[i] = a->n == 1 ? cur[i] : a->box[i] + (cur[i] - a->box[i]) / (float)a->n;
    cg[0] = (gb[0] + gb[2]) * 0.5f;
    cg[1] = (gb[1] + gb[3]) * 0.5f;
    hg[0] = (gb[2] - gb[0]) * 0.5f;
    hg[1] = (gb[3] - gb[1]) * 0.5f;
    cs[0] = (a->box[0] + a->box[2]) * 0.5f;
    cs[1] = (a->box[1] + a->box[3]) * 0.5f;
    hs[0] = (a->box[2] - a->box[0]) * 0.5f;
    hs[1] = (a->box[3] - a->box[1]) * 0.5f;
    /* one scale for both axes (the effects keep their shapes; a body much taller than its GBA picture - a
     * back picture cut by the text box - does not stretch them), held to 0.75 .. 1.6 */
    a->sc = sqrtf((hs[0] / (hg[0] > 0.5f ? hg[0] : 0.5f)) * (hs[1] / (hg[1] > 0.5f ? hg[1] : 0.5f)));
    a->sc = a->sc < 0.75f ? 0.75f : a->sc > 1.6f ? 1.6f : a->sc;
    /* the region placed: the GBA body and what becomes the shown body, a little more; at least 12 pixels */
    for (int i = 0; i < 2; i++)
    {
        rt[i] = (hg[i] > hs[i] / a->sc ? hg[i] : hs[i] / a->sc) * 1.1f;
        rt[i] = rt[i] < 12.0f ? 12.0f : rt[i];
    }
    BoxToScreen(g, cg[0], cg[1], a->c);
    BoxToScreen(g, cs[0], cs[1], shown);
    a->d[0] = shown[0] - a->c[0];
    a->d[1] = shown[1] - a->c[1];
    ExtentToScreen(g, rt[0], rt[1], a->r);
    BoxToScreen(g, g->w * 0.5f, g->h * 0.5f, centre);
    a->crel[0] = a->c[0] - centre[0];
    a->crel[1] = a->c[1] - centre[1];
    a->measured = true;
    if (BattleFx_Logging() && a->loggedLook != a->look && a->n >= ATTACH_MEAN_FRAMES / 2)
    {
        a->loggedLook = a->look;
        SDL_Log("Battle FX: battler %d is shown %.0f x %.0f (its GBA picture %.0f x %.0f): the effects near it are "
                "moved %+.1f, %+.1f pixels and scaled %.2f", b, hs[0] * 2.0f, hs[1] * 2.0f, hg[0] * 2.0f,
                hg[1] * 2.0f, a->d[0], a->d[1], a->sc);
    }
}

/* Every battle picture, once (BattleFxGL_DrawBack): the battlers' looks, also between animations (a battler a
 * script moves to a BG layer before any HD frame was drawn keeps the look it had). */
static void MeasureLooks(void)
{
    if (s.lookSeq == s.pictureSeq || !BattleFx_Enabled())
        return;
    s.lookSeq = s.pictureSeq;
    for (int b = 0; b < 4; b++)
        MeasureLook(b);
}

/* Once a picture with an HD frame (DrawLayer): the placing for each battler shown otherwise than its GBA picture
 * - in OAM, as measured; moved to BG1 / BG2 by a script ("monbg": a copy of its picture the layer's scroll moves,
 * drawn from the animated sprites' frame as well), the look measured while it was a sprite, about where the core
 * has the copy (B->gba). */
static void ComputeAttach(const struct Frame *f)
{
    const float fov = f->camera[12] > 1.0f ? f->camera[12] : 22.0f;
    const float T = tanf(fov * 3.14159265f / 360.0f);
    const bool placing = BattleFx_Attach() && s.attachInjected;

    MeasureLooks();
    if (s.attachSeq == s.pictureSeq)
        return;
    s.attachSeq = s.pictureSeq;
    s.attachOn = false;
    /* the GBA camera: the core's packet camera (its "off" setting: the fixed GBA framing) */
    for (int i = 0; i < 3; i++)
    {
        s.attachCam[i] = f->camera[i];
        s.attachCam[4 + i] = f->camera[3 + i];
        s.attachCam[8 + i] = f->camera[6 + i];
        s.attachCam[12 + i] = -f->camera[9 + i];
    }
    s.attachCam[3] = T;
    s.attachCam[7] = s.attachCam[11] = s.attachCam[15] = 0.0f;
    for (int b = 0; b < 4; b++)
    {
        struct Attach *a = &s.attach[b];
        const struct BfxBattler *B = &f->battlers[b];
        float depth;

        a->on = false;
        if (placing && B->present && s.game[b].shown && a->measured)
            a->on = true;
        else if (placing && B->present && !s.game[b].shown && a->n > 0 && B->drawn
              && (B->onBgLayer == 1 || B->onBgLayer == 2) && BattleFx_ShownBgCopy(B->onBgLayer))
        {
            a->c[0] = B->gba[0] + a->crel[0];
            a->c[1] = B->gba[1] + a->crel[1];
            a->on = true;
        }
        if (a->on)
        {
            memcpy(a->feet, B->feet, sizeof(a->feet));
            depth = (a->feet[0] - s.attachCam[0]) * s.attachCam[12] + (a->feet[1] - s.attachCam[1]) * s.attachCam[13]
                  + (a->feet[2] - s.attachCam[2]) * s.attachCam[14];
            a->groundR = a->r[0] * T * (depth > 1.0f ? depth : 1.0f) / 80.0f;
            s.attachOn = true;
        }
#ifdef BATTLEFX_TEST
        if (BattleFxTest_AttachTrace())
            SDL_Log("Battle FX attach: anim %u frame %d battler %d: shown %d oam %d present %d drawn %d bg %d frame %d "
                    "(%dx%d at %d,%d) on %d c %.1f,%.1f d %.1f,%.1f r %.1f,%.1f sc %.2f box %.1f,%.1f..%.1f,%.1f n %u",
                    (unsigned)f->serial, f->frame, b, s.game[b].shown, s.game[b].oam, B->present, B->drawn,
                    B->onBgLayer, a->frame, a->frame ? a->view.width : 0, a->frame ? a->view.height : 0,
                    a->frame ? a->view.left : 0, a->frame ? a->view.top : 0, a->on, a->c[0], a->c[1], a->d[0], a->d[1],
                    a->r[0], a->r[1], a->sc, a->box[0], a->box[1], a->box[2], a->box[3], a->n);
#endif
    }
}

/* The placing for the effect shaders (WithAttach's uniforms). */
static void SetAttach(GLuint p)
{
    float A[16], B[16], F[16];

    if (!s.attachInjected)
        return;
    bglUniform1i(bglGetUniformLocation(p, "uBfxOn"), s.attachOn ? 1 : 0);
    if (!s.attachOn)
        return;
    for (int b = 0; b < 4; b++)
    {
        const struct Attach *a = &s.attach[b];

        A[b * 4] = a->c[0];
        A[b * 4 + 1] = a->c[1];
        A[b * 4 + 2] = a->d[0];
        A[b * 4 + 3] = a->d[1];
        B[b * 4] = a->on ? a->r[0] : 1.0f; /* (the shaders divide by these: never 0) */
        B[b * 4 + 1] = a->on ? a->r[1] : 1.0f;
        B[b * 4 + 2] = a->on ? a->sc : 1.0f;
        B[b * 4 + 3] = a->on ? 1.0f : 0.0f;
        F[b * 4] = a->feet[0];
        F[b * 4 + 1] = a->feet[1];
        F[b * 4 + 2] = a->feet[2];
        F[b * 4 + 3] = a->on && a->groundR > 0.01f ? a->groundR : 1.0f;
    }
    bglUniform4fv(bglGetUniformLocation(p, "uBfxCam"), 4, s.attachCam);
    bglUniform4fv(bglGetUniformLocation(p, "uBfxA"), 4, A);
    bglUniform4fv(bglGetUniformLocation(p, "uBfxB"), 4, B);
    bglUniform4fv(bglGetUniformLocation(p, "uBfxF"), 4, F);
}

/* The shaders' bfxWarp on the CPU (the lights that light the meshes): `out` the point placed, *grow its scale. */
static void WarpPoint(const float p[3], float size, float out[3], float *grow)
{
    const float *c = s.attachCam;
    float r[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
    float d = r[0] * c[12] + r[1] * c[13] + r[2] * c[14], t, g[2], w[4], sum = 0.0f, k, off[2] = {0.0f, 0.0f};
    float gr = 0.0f, big, lx, ly;

    memcpy(out, p, 3 * sizeof(float));
    *grow = 1.0f;
    if (!s.attachOn || d < 0.5f)
        return;
    t = c[3] * d;
    g[0] = ((r[0] * c[4] + r[1] * c[5] + r[2] * c[6]) / (1.5f * t) + 1.0f) * 120.0f;
    g[1] = (1.0f - (r[0] * c[8] + r[1] * c[9] + r[2] * c[10]) / t) * 80.0f;
    big = 1.0f - Smooth(96.0f, 176.0f, size * 80.0f / t);
    for (int b = 0; b < 4; b++)
    {
        const struct Attach *a = &s.attach[b];

        w[b] = 0.0f;
        if (a->on)
        {
            float nx = (g[0] - a->c[0]) / a->r[0], ny = (g[1] - a->c[1]) / a->r[1];
            w[b] = 1.0f - Smooth(1.0f, 1.8f, sqrtf(nx * nx + ny * ny));
        }
        sum += w[b];
    }
    k = (sum > 1.0f ? 1.0f / sum : 1.0f) * big;
    for (int b = 0; b < 4; b++)
    {
        const struct Attach *a = &s.attach[b];

        if (w[b] <= 0.0f)
            continue;
        off[0] += w[b] * k * (a->d[0] + (a->sc - 1.0f) * (g[0] - a->c[0]));
        off[1] += w[b] * k * (a->d[1] + (a->sc - 1.0f) * (g[1] - a->c[1]));
        gr += w[b] * k * (a->sc - 1.0f);
    }
    lx = ((g[0] + off[0]) / 120.0f - 1.0f) * 1.5f * t;
    ly = (1.0f - (g[1] + off[1]) / 80.0f) * t;
    for (int i = 0; i < 3; i++)
        out[i] = c[i] + c[4 + i] * lx + c[8 + i] * ly + c[12 + i] * d;
    *grow = 1.0f + gr;
}

/* The shown frame's texture (palette indices in their colours as drawn), uploaded when it changed. */
static void UploadShown(int b)
{
    struct Attach *a = &s.attach[b];
    const struct BattleFxShownFrame *v = &a->view;
    uint32_t key = 2166136261u;

    key = Fnv(key, &v->pixels, sizeof(v->pixels));
    key = Fnv(key, &v->width, sizeof(v->width));
    key = Fnv(key, &v->height, sizeof(v->height));
    key = Fnv(key, &v->stride, sizeof(v->stride));
    key = Fnv(key, v->colors, 256 * sizeof(uint16_t));
    glBindTexture(GL_TEXTURE_2D, s.shownTex[b]);
    if (key == a->texKey)
        return;
    a->texKey = key;
    for (int y = 0; y < v->height; y++)
    {
        const uint8_t *row = v->pixels + (size_t)y * v->stride;
        uint8_t *px = s.shownRgba + (size_t)y * v->width * 4;

        for (int x = 0; x < v->width; x++, px += 4)
        {
            uint16_t c = v->colors[row[x]];

            if (row[x] == 0)
            {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            px[0] = (uint8_t)((c & 0x1F) * 255 / 31);
            px[1] = (uint8_t)(((c >> 5) & 0x1F) * 255 / 31);
            px[2] = (uint8_t)(((c >> 10) & 0x1F) * 255 / 31);
            px[3] = 255;
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v->width, v->height, GL_RGBA, GL_UNSIGNED_BYTE, s.shownRgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void SetCamera(GLuint p, const struct Camera *c)
{
    bglUniformMatrix4fv(bglGetUniformLocation(p, "projectionMatrix"), 1, GL_FALSE, c->proj);
    bglUniformMatrix4fv(bglGetUniformLocation(p, "viewMatrix"), 1, GL_FALSE, c->view);
    bglUniform3fv(bglGetUniformLocation(p, "uRight"), 1, c->right);
    bglUniform3fv(bglGetUniformLocation(p, "uUp"), 1, c->up);
}

static void BindFx(GLuint p, const struct Camera *c, float time)
{
    bglUseProgram(p);
    SetCamera(p, c);
    SetAttach(p);
    bglUniform1f(bglGetUniformLocation(p, "uGrid"), (float)s.atlasGrid);
    bglUniform1f(bglGetUniformLocation(p, "uRampRows"), (float)s.rampsH);
    bglUniform1f(bglGetUniformLocation(p, "uTime"), time);
    bglUniform1i(bglGetUniformLocation(p, "uAtlas"), 0);
    bglUniform1i(bglGetUniformLocation(p, "uRamp"), 1);
    bglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.texAtlas);
    bglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s.texRamps);
}

static void BlendPremul(void)
{
    glEnable(GL_BLEND);
    bglBlendEquation(GL_FUNC_ADD);
    bglBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void BlendAdd(void)
{
    glEnable(GL_BLEND);
    bglBlendEquation(GL_FUNC_ADD);
    bglBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
}

static void BlendNormal(void)
{
    glEnable(GL_BLEND);
    bglBlendEquation(GL_FUNC_ADD);
    bglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void Depth(bool test, bool write)
{
    if (test)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(write ? GL_TRUE : GL_FALSE);
    glDepthFunc(GL_LEQUAL);
}

static void DrawQuads(struct QuadGl *q, const struct Quads *b)
{
    int n = b->n < q->cap ? b->n : q->cap;

    if (n <= 0)
        return;
    for (int i = 0; i < 4; i++)
    {
        bglBindBuffer(GL_ARRAY_BUFFER, q->vbo[i]);
        bglBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)n * 16, b->v[i]);
    }
    bglBindVertexArray(q->vao);
    bglDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL, n);
}

static void DrawRibbons(struct RibGl *r, const struct Ribbons *b)
{
    static const int comps[4] = {3, 2, 4, 2};
    const float *src[4] = {b->pos, b->uv, b->col, b->row};
    int nv = b->nv < r->cap ? b->nv : r->cap;

    if (b->ni <= 0 || nv <= 0)
        return;
    for (int i = 0; i < 4; i++)
    {
        bglBindBuffer(GL_ARRAY_BUFFER, r->vbo[i]);
        bglBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nv * comps[i] * 4, src[i]);
    }
    bglBindVertexArray(r->vao);
    bglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    bglBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)b->ni * 2, b->idx);
    glDrawElements(GL_TRIANGLES, b->ni, GL_UNSIGNED_SHORT, NULL);
}

static void DrawRibbonLayer(struct RibGl *r, const struct Ribbons *b, const struct Camera *c)
{
    if (b->ni <= 0)
        return;
    bglUseProgram(s.progRib);
    SetCamera(s.progRib, c);
    SetAttach(s.progRib);
    bglUniform1i(bglGetUniformLocation(s.progRib, "uStrip"), 0);
    bglUniform1f(bglGetUniformLocation(s.progRib, "uRows"), 8.0f);
    bglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.texStrips);
    Depth(false, false);
    BlendPremul();
    glDisable(GL_CULL_FACE);
    DrawRibbons(r, b);
}

static void UploadRing(struct Ring *r)
{
    if (r->lo < 0 || r->hi < r->lo || r->cap <= 0)
        return;
    for (int i = 0; i < 6; i++)
    {
        bglBindBuffer(GL_ARRAY_BUFFER, r->vbo[i]);
        bglBufferSubData(GL_ARRAY_BUFFER, (GLintptr)r->lo * 16, (GLsizeiptr)(r->hi - r->lo + 1) * 16,
                         r->a[i] + (size_t)r->lo * 4);
    }
    r->lo = r->hi = -1;
}

static void SetLights(GLuint p, const struct Frame *f)
{
    float pos[24] = {0}, col[24] = {0};

    bglUniform3fv(bglGetUniformLocation(p, "uHemiSky"), 1, f->hemiSky);
    bglUniform3fv(bglGetUniformLocation(p, "uHemiGround"), 1, f->hemiGround);
    bglUniform1f(bglGetUniformLocation(p, "uHemiI"), f->hemiI);
    bglUniform3fv(bglGetUniformLocation(p, "uSunDir"), 1, f->sunDir);
    bglUniform3fv(bglGetUniformLocation(p, "uSunCol"), 1, f->sun);
    bglUniform1f(bglGetUniformLocation(p, "uSunI"), f->sunI);
    for (int i = 0; i < f->lightCount; i++)
    {
        const float *L = f->lights + i * 8;
        float at[3], grow;

        WarpPoint(L, 0.0f, at, &grow); /* moved with the effects (the battlers as shown), its reach scaled */
        pos[i * 4] = at[0];
        pos[i * 4 + 1] = at[1];
        pos[i * 4 + 2] = at[2];
        pos[i * 4 + 3] = L[7] * grow;
        col[i * 4] = L[3] * L[6];
        col[i * 4 + 1] = L[4] * L[6];
        col[i * 4 + 2] = L[5] * L[6];
    }
    bglUniform4fv(bglGetUniformLocation(p, "uLightPos"), 6, pos);
    bglUniform4fv(bglGetUniformLocation(p, "uLightCol"), 6, col);
    bglUniform1i(bglGetUniformLocation(p, "uLightCount"), f->lightCount);
    bglUniform3fv(bglGetUniformLocation(p, "uFogCol"), 1, f->fog);
    bglUniform1f(bglGetUniformLocation(p, "uFogDensity"), f->fogDensity);
}

static void DrawMeshes(const struct Frame *f, const struct Camera *c)
{
    bool any = false;

    for (int k = 0; k < f->meshKinds && k < s.meshKinds; k++)
        any |= f->meshN[k] > 0;
    if (!any)
        return;
    bglUseProgram(s.progMesh);
    SetCamera(s.progMesh, c);
    SetAttach(s.progMesh);
    SetLights(s.progMesh, f);
    bglUniform3fv(bglGetUniformLocation(s.progMesh, "uCamPos"), 1, c->pos);
    Depth(true, true);
    BlendNormal();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (int k = 0; k < f->meshKinds && k < s.meshKinds; k++)
    {
        struct MeshGl *me = &s.meshes[k];
        int n = f->meshN[k];

        if (n <= 0 || me->vao == 0)
            continue;
        bglBindBuffer(GL_ARRAY_BUFFER, me->inst);
        bglBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)n * MESH_FLOATS * 4, f->meshData[k]);
        bglUniform3fv(bglGetUniformLocation(s.progMesh, "uBaseColor"), 1, me->color);
        bglUniform3fv(bglGetUniformLocation(s.progMesh, "uEmissive"), 1, me->emissive);
        bglUniform3fv(bglGetUniformLocation(s.progMesh, "uRim"), 1, me->rim);
        bglUniform1f(bglGetUniformLocation(s.progMesh, "uRoughness"), me->roughness);
        bglUniform1f(bglGetUniformLocation(s.progMesh, "uMetalness"), me->metalness);
        bglBindVertexArray(me->vao);
        bglDrawArraysInstanced(GL_TRIANGLES, 0, me->verts, n);
    }
    glDisable(GL_CULL_FACE);
}

static void FullScreenPass(GLuint prog, struct Target *t, bool clear)
{
    bglUseProgram(prog);
    bglBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    if (clear)
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

static void DrawFsq(void)
{
    bglBindVertexArray(s.fsqVao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
}

/* The bloom chain of the layer (the package's bright / down / up passes), `levels` deep (5: the package's; 0: none,
 * the chain's top cleared). */
static void Bloom(const struct Frame *f, int levels)
{
    Depth(false, false);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    if (levels <= 0)
    {
        bglBindFramebuffer(GL_FRAMEBUFFER, s.bloom[0].fbo);
        glViewport(0, 0, s.bloom[0].w, s.bloom[0].h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    levels = levels > 5 ? 5 : levels;
    FullScreenPass(s.progBright, &s.bloom[0], true);
    bglUniform1i(bglGetUniformLocation(s.progBright, "tSrc"), 0);
    bglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.layer.tex);
    bglUniform2f(bglGetUniformLocation(s.progBright, "uTexel"), 1.0f / s.layer.w, 1.0f / s.layer.h);
    bglUniform1f(bglGetUniformLocation(s.progBright, "uTh"), f->post.threshold);
    DrawFsq();
    for (int i = 1; i < levels; i++)
    {
        FullScreenPass(s.progDown, &s.bloom[i], true);
        bglUniform1i(bglGetUniformLocation(s.progDown, "tSrc"), 0);
        glBindTexture(GL_TEXTURE_2D, s.bloom[i - 1].tex);
        bglUniform2f(bglGetUniformLocation(s.progDown, "uTexel"), 1.0f / s.bloom[i - 1].w, 1.0f / s.bloom[i - 1].h);
        DrawFsq();
    }
    BlendAdd();
    for (int i = levels - 1; i > 0; i--)
    {
        FullScreenPass(s.progUp, &s.bloom[i - 1], false);
        bglUniform1i(bglGetUniformLocation(s.progUp, "tSrc"), 0);
        glBindTexture(GL_TEXTURE_2D, s.bloom[i].tex);
        bglUniform2f(bglGetUniformLocation(s.progUp, "uTexel"), 1.0f / s.bloom[i].w, 1.0f / s.bloom[i].h);
        bglUniform1f(bglGetUniformLocation(s.progUp, "uW"), 1.0f);
        DrawFsq();
    }
    glDisable(GL_BLEND);
}

enum Layer
{
    LAYER_BACK,
    LAYER_FRONT,
};

static bool BattlerLit(const struct BfxBattler *B)
{
    return B->flash[3] > 0.004f || fabsf(B->light[0] - 1.0f) > 0.004f || fabsf(B->light[1] - 1.0f) > 0.004f
        || fabsf(B->light[2] - 1.0f) > 0.004f || B->lightAdd[0] > 0.002f || B->lightAdd[1] > 0.002f
        || B->lightAdd[2] > 0.002f;
}

/* The game's battlers with the frame's HD battler state: glows (back layer), or depth and the
 * effects' light and flash (front layer). GBA pixel g lands at window (gameX + g scale, gameY + g scale);
 * the layer covers the window's rows 0 .. regionH. A battler the animated battle sprites show is drawn from the
 * frame they show (its shape and colours; ComputeAttach), any other from its GBA picture as before. */
static void DrawBattlers(const struct Frame *f, const struct BattleFxLayout *L, int regionH, const struct Camera *cam,
                         enum Layer which)
{
    float map[4] = {2.0f * L->scale / L->outW, -2.0f * L->scale / regionH, 2.0f * L->gameX / L->outW - 1.0f,
                    1.0f - 2.0f * L->gameY / regionH};
    bool uploaded[4] = {false, false, false, false};

    bglUseProgram(s.progBattler);
    bglUniform4f(bglGetUniformLocation(s.progBattler, "uMap"), map[0], map[1], map[2], map[3]);
    bglUniform1i(bglGetUniformLocation(s.progBattler, "uTex"), 0);
    bglActiveTexture(GL_TEXTURE0);
    for (int b = 0; b < 4; b++)
    {
        const struct BattleFxGameBattler *g = &s.game[b];
        const struct BfxBattler *B = &f->battlers[b];
        const struct Attach *a = &s.attach[b];
        float half[2], center[2], lo[2] = {0.0f, 0.0f}, hi[2] = {0.0f, 0.0f};
        int passes = 0;

        if (!g->shown || !B->present)
            continue;
        if (which == LAYER_BACK && B->glow[3] <= 0.01f)
            continue;
        if (!uploaded[b])
        {
            if (a->frame)
                UploadShown(b);
            else
            {
                glBindTexture(GL_TEXTURE_2D, s.battlerTex[b]);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BFX_GAME_SPRITE_MAX, BFX_GAME_SPRITE_MAX, GL_RGBA,
                                GL_UNSIGNED_BYTE, g->rgba);
            }
            uploaded[b] = true;
        }
        glBindTexture(GL_TEXTURE_2D, a->frame ? s.shownTex[b] : s.battlerTex[b]);
        half[0] = (g->doubleSize ? g->w : g->w / 2.0f);
        half[1] = (g->doubleSize ? g->h : g->h / 2.0f);
        center[0] = g->x + half[0];
        center[1] = g->y + half[1];
        bglUniform2f(bglGetUniformLocation(s.progBattler, "uCenter"), center[0], center[1]);
        bglUniform2f(bglGetUniformLocation(s.progBattler, "uSize"), (float)g->w, (float)g->h);
        bglUniform4f(bglGetUniformLocation(s.progBattler, "uM"), (float)g->m[0], (float)g->m[1], (float)g->m[2],
                     (float)g->m[3]);
        bglUniform2f(bglGetUniformLocation(s.progBattler, "uFlip"), g->hflip ? 1.0f : 0.0f, g->vflip ? 1.0f : 0.0f);
        bglUniform1i(bglGetUniformLocation(s.progBattler, "uFrame"), a->frame ? 1 : 0);
        if (a->frame)
        {
            bglUniform2f(bglGetUniformLocation(s.progBattler, "uOff"), (float)a->view.left, (float)a->view.top);
            bglUniform2f(bglGetUniformLocation(s.progBattler, "uFrameSize"), (float)a->view.width, (float)a->view.height);
            bglUniform1f(bglGetUniformLocation(s.progBattler, "uTexSize"), (float)SHOWN_TEX);
            bglUniform1f(bglGetUniformLocation(s.progBattler, "uClip"),
                         a->view.clipBottom < 100000 ? (float)a->view.clipBottom : 100000.0f);
            bglUniform2f(bglGetUniformLocation(s.progBattler, "uPivot"), a->fbEmpty ? center[0] : a->pivot[0],
                         a->fbEmpty ? center[1] : a->pivot[1]);
            ShownBounds(g, &a->view, lo, hi); /* the frame is not cut to the entry's box */
        }
        bglBindVertexArray(s.qCards.vao); /* any vertex array with the corner attribute */
        if (which == LAYER_BACK)
        {
            /* two additive silhouettes, scaled 1.07 and 1.16 with a slight pulse (the package's glow copies) */
            float t = (float)f->t;
            BlendAdd();
            Depth(false, false);
            for (int k = 0; k < 2; k++)
            {
                float sc = k == 0 ? 1.07f + 0.02f * sinf(t * 9.0f) : 1.16f + 0.03f * sinf(t * 7.0f + 1.0f);
                bglUniform1i(bglGetUniformLocation(s.progBattler, "uMode"), 0);
                bglUniform1f(bglGetUniformLocation(s.progBattler, "uScale"), sc);
                if (a->frame)
                {
                    /* the frame scaled about the body's centre: the quad around it (centred on the entry's centre) */
                    float p[2] = {(a->fbEmpty ? center[0] : a->pivot[0]) - center[0],
                                  (a->fbEmpty ? center[1] : a->pivot[1]) - center[1]}, box[2];
                    for (int i = 0; i < 2; i++)
                    {
                        float e0 = fabsf(p[i] + sc * (lo[i] - p[i])), e1 = fabsf(p[i] + sc * (hi[i] - p[i]));
                        box[i] = 2.0f * ((e0 > e1 ? e0 : e1) + 2.0f);
                    }
                    bglUniform2f(bglGetUniformLocation(s.progBattler, "uBox"), box[0], box[1]);
                }
                else
                    bglUniform2f(bglGetUniformLocation(s.progBattler, "uBox"), half[0] * 2.0f * sc, half[1] * 2.0f * sc);
                bglUniform4f(bglGetUniformLocation(s.progBattler, "uGlow"), B->glow[0], B->glow[1], B->glow[2],
                             B->glow[3] * (k == 0 ? 1.1f : 0.55f) * (B->fade > 0.0f ? B->fade : 0.0f));
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                passes++;
            }
            continue;
        }
        /* front: depth for the meshes (an opaque battler only), and its light and flash */
        {
            float clip[4], v[4], depth;
            const float *w = B->world;
            bool lit = BattlerLit(B) && g->objMode == 0 /* ST_OAM_OBJ_NORMAL */;
            for (int r = 0; r < 4; r++)
                v[r] = cam->view[r] * w[0] + cam->view[4 + r] * w[1] + cam->view[8 + r] * w[2] + cam->view[12 + r];
            for (int r = 0; r < 4; r++)
                clip[r] = cam->proj[r] * v[0] + cam->proj[4 + r] * v[1] + cam->proj[8 + r] * v[2] + cam->proj[12 + r] * v[3];
            depth = clip[3] != 0.0f ? clip[2] / clip[3] * 0.5f + 0.5f : 0.5f;
            if (depth < 0.0f)
                depth = 0.0f;
            if (depth > 1.0f)
                depth = 1.0f;
            if (g->objMode != 0 /* ST_OAM_OBJ_NORMAL */ || B->fade < 0.6f)
                continue;
            bglUniform1i(bglGetUniformLocation(s.progBattler, "uMode"), 1);
            bglUniform1f(bglGetUniformLocation(s.progBattler, "uDepth"), depth);
            if (a->frame)
            {
                float ex = (fabsf(lo[0]) > fabsf(hi[0]) ? fabsf(lo[0]) : fabsf(hi[0])) + 2.0f;
                float ey = (fabsf(lo[1]) > fabsf(hi[1]) ? fabsf(lo[1]) : fabsf(hi[1])) + 2.0f;
                bglUniform2f(bglGetUniformLocation(s.progBattler, "uBox"), ex * 2.0f, ey * 2.0f);
            }
            else
                bglUniform2f(bglGetUniformLocation(s.progBattler, "uBox"), half[0] * 2.0f, half[1] * 2.0f);
            bglUniform4f(bglGetUniformLocation(s.progBattler, "uFlash"), B->flash[0], B->flash[1], B->flash[2], B->flash[3]);
            bglUniform3f(bglGetUniformLocation(s.progBattler, "uLight"), B->light[0], B->light[1], B->light[2]);
            bglUniform3f(bglGetUniformLocation(s.progBattler, "uLightAdd"), B->lightAdd[0], B->lightAdd[1], B->lightAdd[2]);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_ALWAYS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            if (!lit)
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthFunc(GL_LEQUAL);
            passes++;
        }
        (void)passes;
    }
}

static bool HasContent(const struct Frame *f, enum Layer which)
{
    if (which == LAYER_BACK)
    {
        if (f->decals.n > 0 || f->cardsBack.n > 0 || f->ribbonsBack.ni > 0)
            return true;
        for (int b = 0; b < 4; b++)
            if (s.game[b].shown && f->battlers[b].present && f->battlers[b].glow[3] > 0.01f)
                return true;
        return false;
    }
    if (f->cards.n > 0 || f->ribbons.ni > 0 || f->partsAddCount > 0 || f->partsAlphaCount > 0 || f->dist.n > 0)
        return true;
    for (int k = 0; k < f->meshKinds; k++)
        if (f->meshN[k] > 0)
            return true;
    return f->post.flash > 0.001f || f->post.grade[3] > 0.001f || fabsf(f->post.sat / 1.05f - 1.0f) > 0.002f;
}

/* Timer query results that have arrived (never waited for). */
static void PollQueries(void)
{
    while (s.queryPending > 0)
    {
        int tail = (s.queryHead - s.queryPending + 8) % 8;
        GLint ready = 0;
        GLuint64 ns = 0;

        bglGetQueryObjectiv(s.queries[tail], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
            break;
        bglGetQueryObjectui64v(s.queries[tail], GL_QUERY_RESULT, &ns);
        s.queryPending--;
        if (s.querySerial[tail] == s.stats.serial)
        {
            double ms = ns / 1.0e6;
            s.stats.gpuSamples++;
            s.stats.gpuMsTotal += ms;
            if (ms > s.stats.gpuMsWorst)
                s.stats.gpuMsWorst = ms;
        }
        if (s.queryCounted[tail] && s.queryTier[tail] == s.pace.tier)
        {
            s.pace.gpuSamples++;
            s.pace.gpuMs += ns / 1.0e6;
        }
    }
}

static void StatsFor(uint32_t serial)
{
    if (serial == s.stats.serial)
        return;
    if (BattleFx_Logging() && s.stats.frames > 0)
        SDL_Log("Battle FX: drawn %u frames (%u layers, %s quality now); GPU %.3f ms a layer on average, %.3f at worst "
                "(%u samples); CPU %.3f ms a layer on average, %.3f at worst",
                s.stats.frames, s.stats.layers, kTiers[s.tier].name,
                s.stats.gpuSamples ? s.stats.gpuMsTotal / s.stats.gpuSamples : 0.0, s.stats.gpuMsWorst,
                s.stats.gpuSamples, s.stats.layers ? s.stats.cpuMsTotal / s.stats.layers : 0.0, s.stats.cpuMsWorst);
    memset(&s.stats, 0, sizeof(s.stats));
    s.stats.serial = serial;
}

void BattleFxGL_Stats(struct BattleFxRenderStats *stats)
{
    *stats = s.stats;
    stats->tier = s.tier;
}

/* The HD layers' drawing cost a picture for animation `serial`, against `budgetMs` (the boundary's check, this
 * thread): the layers' GPU time (timer queries, read back a few pictures later) or, from a driver that gives none,
 * the CPU time to submit them, times the layers a picture draws; once DRAW_BUDGET_PICTURES pictures of it are
 * drawn, so that one slow first picture (its textures uploaded) does not decide. */
#define DRAW_BUDGET_PICTURES 6
bool BattleFxGL_DrawTooSlow(uint32_t serial, int budgetMs, double *msPerPicture)
{
    double perLayer;

    if (budgetMs <= 0 || serial != s.stats.serial || s.stats.frames < DRAW_BUDGET_PICTURES || s.stats.layers == 0)
        return false;
    if (s.stats.gpuSamples >= 4)
        perLayer = s.stats.gpuMsTotal / s.stats.gpuSamples;
    else if (s.stats.gpuSamples == 0 && s.stats.layers >= 2 * DRAW_BUDGET_PICTURES)
        perLayer = s.stats.cpuMsTotal / s.stats.layers; /* no timer queries */
    else
        return false;
    *msPerPicture = perLayer * (double)s.stats.layers / (double)s.stats.frames;
    return *msPerPicture > (double)budgetMs;
}

static void DrawLayer(const struct BattleFxLayout *L, enum Layer which)
{
    const struct Frame *f = &s.frames[s.show];
    struct Camera cam;
    GLint drawFb = 0, readFb = 0, viewport[4];
    int regionH, regionY;
    float alpha, time, sat;
    bool whole, timed;
    Uint64 cpu0;

    if (!f->valid || !BattleFx_Enabled() || !Ready() || !HasContent(f, which))
        return;
#ifdef BATTLEFX_TEST
    if (!BattleFxTest_ViewHd())
        return;
#endif
    if (L->outW <= 0 || L->outH <= 0 || L->scale <= 0.0f)
        return;
    /* The window above the text box (top-left origin): rows 0 .. gameY + 112 scale. */
    regionH = (int)(L->gameY + 112.0f * L->scale + 0.5f);
    if (regionH > L->outH)
        regionH = L->outH;
    if (regionH < 4)
        return;
    regionY = L->outH - regionH; /* GL rows from the bottom */

    StatsFor(f->serial);
    PollQueries();
    cpu0 = SDL_GetPerformanceCounter();
    timed = s.queryPending < 8;
    if (timed)
    {
        s.querySerial[s.queryHead] = f->serial;
        s.queryTier[s.queryHead] = s.tier;
        s.queryCounted[s.queryHead] = f->serial == s.pace.serial && s.pace.seen > PACE_WARMUP_PICTURES;
        bglBeginQuery(GL_TIME_ELAPSED, s.queries[s.queryHead]);
    }
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glDisable(GL_SCISSOR_TEST);
    EnsureTargets(L->outW, regionH);
    MakeCamera(f, L, regionH, &cam);
    ComputeAttach(f);
    /* Particles move at the display's rate: their time runs on between frame boundaries. */
    alpha = (float)((SDL_GetPerformanceCounter() - s.boundaryAt) / (double)SDL_GetPerformanceFrequency() * BFX_FPS);
#ifdef BATTLEFX_TEST
    BattleFxTest_FixedAlpha(&alpha);
#endif
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    time = (float)(f->t + alpha / BFX_FPS);

    /* The window as it is now, to blend in linear light (and to distort). */
    {
        GLuint readable = DioramaGLTarget_ReadableFramebuffer((unsigned)drawFb);
        bglBindFramebuffer(GL_READ_FRAMEBUFFER, readable);
        glBindTexture(GL_TEXTURE_2D, s.dst.tex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, regionY, L->outW, regionH);
    }

    if (which == LAYER_FRONT)
    {
        UploadRing(&s.ringAdd);
        UploadRing(&s.ringAlpha);
        /* distortion offsets, half resolution */
        bglBindFramebuffer(GL_FRAMEBUFFER, s.dist.fbo);
        glViewport(0, 0, s.dist.w, s.dist.h);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (f->dist.n > 0 && kTiers[s.tier].distortion)
        {
            BindFx(s.progDist, &cam, time);
            Depth(false, false);
            BlendAdd();
            DrawQuads(&s.qDist, &f->dist);
        }
    }

    bglBindFramebuffer(GL_FRAMEBUFFER, s.layer.fbo);
    glViewport(0, 0, s.layer.w, s.layer.h);
    glClearColor(0, 0, 0, 0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    if (which == LAYER_BACK)
    {
        /* render order 40 decals, 60 back cards, 70 back ribbons (the Lab's HD view) */
        if (f->decals.n > 0)
        {
            BindFx(s.progCard, &cam, time);
            Depth(false, false);
            BlendPremul();
            DrawQuads(&s.qDecals, &f->decals);
        }
        if (f->cardsBack.n > 0)
        {
            BindFx(s.progCard, &cam, time);
            Depth(false, false);
            BlendPremul();
            DrawQuads(&s.qCardsBack, &f->cardsBack);
        }
        DrawRibbonLayer(&s.rRibbonsBack, &f->ribbonsBack, &cam);
        DrawBattlers(f, L, regionH, &cam, LAYER_BACK); /* glows: just behind the battlers */
    }
    else
    {
        /* the battlers (100 + ...): their depth and light; then 9100 meshes, 9200 alpha particles,
         * 9300 additive particles, 9400 ribbons, 9500 cards */
        DrawBattlers(f, L, regionH, &cam, LAYER_FRONT);
        DrawMeshes(f, &cam);
        if (f->partsAlphaCount > 0)
        {
            BindFx(s.progPart, &cam, time);
            Depth(false, false);
            BlendPremul();
            bglBindVertexArray(s.ringAlpha.vao);
            bglDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL,
                                     f->partsAlphaCount < CAP_PARTS_ALPHA ? f->partsAlphaCount : CAP_PARTS_ALPHA);
        }
        if (f->partsAddCount > 0)
        {
            BindFx(s.progPart, &cam, time);
            Depth(false, false);
            BlendPremul();
            bglBindVertexArray(s.ringAdd.vao);
            bglDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL,
                                     f->partsAddCount < CAP_PARTS_ADD ? f->partsAddCount : CAP_PARTS_ADD);
        }
        DrawRibbonLayer(&s.rRibbons, &f->ribbons, &cam);
        if (f->cards.n > 0)
        {
            BindFx(s.progCard, &cam, time);
            Depth(false, false);
            BlendPremul();
            DrawQuads(&s.qCards, &f->cards);
        }
    }
#ifdef BATTLEFX_TEST
    if (BattleFxTest_WantLayer(f->serial, f->frame, which == LAYER_FRONT ? 1 : 0))
    {
        float *px = malloc((size_t)s.layer.w * s.layer.h * 4 * sizeof(float));

        if (px != NULL)
        {
            bglBindFramebuffer(GL_READ_FRAMEBUFFER, s.layer.fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, s.layer.w, s.layer.h, GL_RGBA, GL_FLOAT, px);
            BattleFxTest_TakeLayer(f->serial, f->frame, which == LAYER_FRONT ? 1 : 0, s.layer.w, s.layer.h, px);
            free(px);
        }
    }
#endif
    Bloom(f, kTiers[s.tier].bloomLevels);

    /* Composite into the window's region. */
    bglBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
    glViewport(0, regionY, L->outW, regionH);
    Depth(false, false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    bglUseProgram(s.progComp);
    bglUniform1i(bglGetUniformLocation(s.progComp, "tLayer"), 0);
    bglUniform1i(bglGetUniformLocation(s.progComp, "tBloom"), 1);
    bglUniform1i(bglGetUniformLocation(s.progComp, "tDist"), 2);
    bglUniform1i(bglGetUniformLocation(s.progComp, "tDst"), 3);
    bglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.layer.tex);
    bglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s.bloom[0].tex);
    bglActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s.dist.tex);
    bglActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s.dst.tex);
    bglUniform1f(bglGetUniformLocation(s.progComp, "uBloom"), f->post.bloom);
    /* distortion only with the front layer (the Lab distorts the whole scene once) */
    bglUniform1f(bglGetUniformLocation(s.progComp, "uDist"),
                 which == LAYER_FRONT && kTiers[s.tier].distortion ? f->post.distortion : 0.0f);
    /* the Lab's offsets are fractions of its GBA-framed view; the region is wider and taller */
    bglUniform2f(bglGetUniformLocation(s.progComp, "uDistScale"), L->gameW / L->outW, L->gameH / regionH);
    /* The Lab grades its whole view at 1.05 saturation; the game's picture keeps its own colours,
     * so only a move's own grading changes it (post.sat relative to that base). */
    sat = f->post.sat / 1.05f;
    whole = which == LAYER_FRONT && (f->post.flash > 0.001f || f->post.grade[3] > 0.001f || fabsf(sat - 1.0f) > 0.002f);
    bglUniform1i(bglGetUniformLocation(s.progComp, "uWhole"), whole ? 1 : 0);
    bglUniform1f(bglGetUniformLocation(s.progComp, "uSat"), which == LAYER_FRONT ? sat : 1.0f);
    bglUniform4f(bglGetUniformLocation(s.progComp, "uGrade"), f->post.grade[0], f->post.grade[1], f->post.grade[2],
                 which == LAYER_FRONT ? f->post.grade[3] : 0.0f);
    bglUniform3fv(bglGetUniformLocation(s.progComp, "uFlashCol"), 1, f->post.flashColor);
    bglUniform1f(bglGetUniformLocation(s.progComp, "uFlash"), which == LAYER_FRONT ? f->post.flash : 0.0f);
    DrawFsq();

    /* The game's renderer state back. */
    bglBindVertexArray(0);
    bglUseProgram(0);
    bglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    bglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFb);
    bglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFb);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    if (timed)
    {
        bglEndQuery(GL_TIME_ELAPSED);
        s.queryHead = (s.queryHead + 1) % 8;
        s.queryPending++;
    }
    {
        double ms = (SDL_GetPerformanceCounter() - cpu0) * 1000.0 / SDL_GetPerformanceFrequency();
        s.stats.layers++;
        s.stats.cpuMsTotal += ms;
        if (ms > s.stats.cpuMsWorst)
            s.stats.cpuMsWorst = ms;
        if (f->serial == s.pace.serial && s.tier == s.pace.tier && s.pace.seen > PACE_WARMUP_PICTURES)
        {
            s.pace.layers++; /* (the window's presents and layers over the same pictures) */
            s.pace.cpuMs += ms;
        }
    }
    s.layersDrawn++;
}

/* ── The pace of the pictures with the HD layers ───────────────────── */

/* Whether the picture being drawn shows the HD layers (an animation's frame). */
static bool PictureHd(void)
{
    if (!s.ready || s.failed || !s.frames[s.show].valid || !BattleFx_Enabled())
        return false;
#ifdef BATTLEFX_TEST
    if (!BattleFxTest_ViewHd())
        return false;
#endif
    return true;
}

/* A new measuring window at the tier drawn (BattleFxGL_PaceReset, a tier changed). */
static void PaceWindow(void)
{
    s.pace.tier = s.tier;
    s.pace.gameFrames = s.pace.skipped = s.pace.presents = s.pace.layers = s.pace.gpuSamples = 0;
    s.pace.presentMs = s.pace.gpuMs = s.pace.cpuMs = 0.0;
}

/* The picture shows animation `serial`: a new one's first pictures are not counted (its textures uploaded, its
 * programs' first use); the window goes on across animations (a short animation counts too). */
static void PaceAnimation(uint32_t serial)
{
    if (serial != s.pace.serial)
    {
        s.pace.serial = serial;
        s.pace.seen = 0;
    }
    if (s.tier != s.pace.tier)
        PaceWindow();
}

void BattleFxGL_PaceBoundary(bool view3d)
{
    if (view3d && PictureHd())
    {
        PaceAnimation(s.frames[s.show].serial);
        if (s.pace.seen > PACE_WARMUP_PICTURES)
        {
            s.pace.gameFrames++;
            if (!s.pace.presented)
                s.pace.skipped++; /* drawn over by the next game frame before any present showed it */
        }
    }
    s.pace.presented = false;
}

/* Each present of the battle (BattleFxGL_DrawFront). */
static void PacePresent(void)
{
    Uint64 now = SDL_GetPerformanceCounter();
    double ms = s.pace.lastPresentAt ? (now - s.pace.lastPresentAt) * 1000.0 / SDL_GetPerformanceFrequency() : -1.0;

    s.pace.lastPresentAt = now;
    s.pace.presented = true;
    if (ms < 0.0 || ms > 1000.0)
        return; /* the battle's first present, or one after a pause (the window moved, the game held) */
    if (PictureHd())
    {
        PaceAnimation(s.frames[s.show].serial);
        s.pace.seen++;
        if (s.pace.seen > PACE_WARMUP_PICTURES)
        {
            s.pace.presents++;
            s.pace.presentMs += ms;
        }
    }
    else
    {
        s.pace.plainCount += s.pace.plainCount < 240 ? 1 : 0;
        s.pace.plainMs += (ms - s.pace.plainMs) / s.pace.plainCount;
    }
}

void BattleFxGL_Pace(struct BattleFxPace *pace)
{
    memset(pace, 0, sizeof(*pace));
    pace->serial = s.pace.serial;
    pace->tier = s.pace.tier;
    pace->gameFrames = s.pace.gameFrames;
    pace->skipped = s.pace.skipped;
    pace->presents = s.pace.presents;
    pace->presentMs = s.pace.presentMs;
    pace->hdMs = -1.0;
    if (s.pace.presents > 0 && s.pace.gpuSamples >= 4)
    {
        pace->hdMs = s.pace.gpuMs / s.pace.gpuSamples * (double)s.pace.layers / (double)s.pace.presents;
        pace->gpuTimed = true;
    }
    else if (s.pace.presents > 0 && s.pace.gpuSamples == 0 && s.pace.layers >= 16)
        pace->hdMs = s.pace.cpuMs / (double)s.pace.presents; /* no timer query results: the CPU's time (less) */
    pace->plainMs = s.pace.plainCount >= 30 ? s.pace.plainMs : 0.0;
}

void BattleFxGL_PaceReset(void)
{
    PaceWindow();
}

void BattleFxGL_DrawBack(const struct BattleFxLayout *layout)
{
    BattleFx_LateFrame();
    MeasureLooks(); /* every battle picture: how the battlers are shown */
    DrawLayer(layout, LAYER_BACK);
}

void BattleFxGL_DrawFront(const struct BattleFxLayout *layout)
{
    DrawLayer(layout, LAYER_FRONT);
    if (s.frames[s.show].valid && s.ready && s.frames[s.show].serial == s.stats.serial)
        s.stats.frames++;
    PacePresent();
#ifdef BATTLEFX_TEST
    BattleFxTest_AfterDraw(s.frames[s.show].serial, s.frames[s.show].frame, s.frames[s.show].valid, layout->outW,
                           layout->outH);
#endif
}

void BattleFxGL_Shutdown(void)
{
    if (!s.ready)
        return;
    /* The context goes with the window; only the CPU copies need freeing. */
    for (int i = 0; i < 2; i++)
    {
        struct Frame *f = &s.frames[i];
        struct Quads *qs[4] = {&f->cards, &f->cardsBack, &f->decals, &f->dist};
        struct Ribbons *rs[2] = {&f->ribbons, &f->ribbonsBack};
        for (int q = 0; q < 4; q++)
            for (int j = 0; j < 4; j++)
                free(qs[q]->v[j]);
        for (int r = 0; r < 2; r++)
        {
            free(rs[r]->pos);
            free(rs[r]->uv);
            free(rs[r]->col);
            free(rs[r]->row);
            free(rs[r]->idx);
        }
        for (int k = 0; k < BFX_MAX_MESH_KINDS; k++)
            free(f->meshData[k]);
    }
    for (int i = 0; i < 6; i++)
    {
        free(s.ringAdd.a[i]);
        free(s.ringAlpha.a[i]);
    }
    free(s.shownRgba);
    memset(&s, 0, sizeof(s));
}

#endif // ENABLE_DIORAMA
