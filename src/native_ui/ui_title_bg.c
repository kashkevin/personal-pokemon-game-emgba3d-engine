#ifdef ENABLE_NATIVE_UI
/*
 * The Emerald title remaster's background (see native_ui/ui_title_bg.h).
 * The frame state is a line-by-line port of params(), sparkList(), ui() and
 * overlay() in the design's preview/js/title_emerald.js (via the native title
 * benchmark's title_state.c, checked frame by frame against the preview); the
 * shader is the preview's own (title_emerald_gl.js). JavaScript semantics are
 * kept where they differ from C: Math.round is floor(x + 0.5), the preview's
 * mod() is ((a % n) + n) % n. Project-owned.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_title_bg.h"

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

/* ---------------------------------------------------------------- frame state */

#define D 2
#define MAX_STAGES 32
#define MAX_SPARKS 40
static const double PI = 3.14159265358979323846;
/* the intro's timeline (frames at 60 fps), as INTRO in title_emerald.js */
static const double I_RISE_START = 40, I_RISE_END = 210, I_IGNITE = 214, I_IGNITE_SCALE = 1.05,
                    I_FLASH_START = 344, I_FLASH_PEAK = 350, I_FLASH_END = 380, I_LOGO = 350, I_PRESS = 380,
                    I_PRESS_IN = 24, I_END = UI_TITLE_INTRO_END, I_FADE_END = 32;

struct Glint
{
    int x, y, tw;
    int rgb[3];
};
struct Frame
{
    int LW, LH, ax, ay;
    int enh;
    int V, Vfar, Vnear, dy, show, alpha8;
    int markMode;
    int markCol[3];
    float stageRgba[MAX_STAGES][4];
    int glow8, flare8, glowCol[3];
    int shadow8, shadowOff[2];
    int light8, rays8, raysY, near8, hazeX, hazeY;
    float wave[64], waveFar[64], sway[168];
    int glintCount;
    struct Glint glints[MAX_SPARKS];
};

static double JsRound(double x) { return floor(x + 0.5); }
static double JsMod(double a, double n) { return fmod(fmod(a, n) + n, n); }
static double Clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static double Q16(double a) { return JsRound(Clamp(a, 0, 1) * 16) / 16; }
static int C5(int v) { return (v << 3) | (v >> 2); }
static int A8(double a) { return (int)JsRound(a * 255); }
static double RingB(double f) { return (1 - cos(2 * PI * f / 256)) / 2; }
static void RingRgb(double b, int out[3])
{
    int I = (int)JsRound((1 - Clamp(b, 0, 1)) * 256), r = 31 - ((I * 31) >> 8), g = 31 - ((I * 22) >> 8);

    out[0] = C5(r);
    out[1] = C5(g);
    out[2] = C5(12);
}
static int GridOff(int v) { return v - D * (int)ceil((double)v / D); }

/* logical pixels the front clouds have risen by frame f in the cinematic version */
static double IntroRise(double f)
{
    static double rise[405];
    static bool ready;

    if (!ready)
    {
        rise[0] = 0;
        for (int i = 0; i < 404; i++)
        {
            double u = Clamp(i / I_END, 0, 1), m = 1 + 3 * (1 - u * u * (3 - 2 * u));
            rise[i + 1] = rise[i] + 0.5 * m;
        }
        ready = true;
    }
    if (f <= I_END)
    {
        int i0 = (int)fmax(0, floor(f));
        double k = f - i0;
        return k > 0 && i0 < 404 ? rise[i0] + (rise[i0 + 1] - rise[i0]) * k : rise[i0];
    }
    return rise[404] + (f - I_END) * 0.5;
}

/* the markings' brightness when a glint was born (sparkAt) */
static double SparkAt(char v, double fi0, const double *sp)
{
    double f0;

    if (v != 'c')
        return RingB(fi0);
    f0 = fi0 - 128 + I_END;
    if (f0 >= I_END)
        return RingB(fi0);
    return f0 >= I_IGNITE + sp[4] * I_IGNITE_SCALE + 24 ? 1 : 0;
}

static void HazeRows(int rows[42])
{
    for (int i = 0; i < 42; i++)
    {
        double dens = pow(i / 42.0, 1.6) * 0.85;
        int n = 0;

        for (int k = 0; k < 16; k++)
            if ((k + 0.5) / 16 < dens)
                n++;
        rows[i] = n;
    }
}

static void FrameState(char v, double f, int LW, int LH, struct Frame *o)
{
    int stageCount = gUiTitleStageCount < MAX_STAGES ? gUiTitleStageCount : MAX_STAGES;

    memset(o, 0, sizeof(*o));
    o->LW = LW;
    o->LH = LH;
    o->ax = (int)floor((LW - 240 * D) / 2.0);
    o->ay = LH - 160 * D;
    o->enh = v != 'a';
    o->show = 1;
    o->alpha8 = 255;
    o->raysY = GridOff(o->ay);
    o->hazeX = GridOff(o->ax);
    o->hazeY = LH - 42 * D;
    if (v == 'a')
    {
        double fq, b;

        f = floor(f); /* the faithful version keeps the 60 Hz steps */
        fq = f - JsMod(f, 4);
        b = RingB(fq);
        o->V = D * (int)floor(f / 4);
        for (int i = 0; i < 64; i++)
            o->wave[i] = (float)(D * trunc(4 * sin(2 * PI * JsMod(i + f, 64) / 64)));
        o->dy = (int)JsRound(2 * (1 - cos(2 * PI * f / 512)) / 2);
        if (b > 0)
        {
            o->markMode = 1;
            RingRgb(b, o->markCol);
        }
        o->shadowOff[0] = 4;
        o->shadowOff[1] = (int)JsMod(floor(o->V / (double)D) + 6, 256);
        return;
    }
    {
        bool intro = v == 'c' && f < I_END;
        double fi = v == 'c' ? f - I_END + 128 : f;
        double b = RingB(fi);
        double rise = v == 'c' ? IntroRise(f) : f / 2;
        double env, glow, gb = -1, flare = 0, alpha = 1, nearv = 0, rays, light;
        int sway = 1;

        o->V = (int)floor(rise);
        o->Vfar = (int)floor(rise / 2);
        o->Vnear = (int)floor(rise * 2.5);
        for (int i = 0; i < 64; i++)
        {
            o->wave[i] = (float)JsRound(8 * sin(2 * PI * JsMod(i + f, 64) / 64));
            o->waveFar[i] = (float)JsRound(4 * sin(2 * PI * JsMod(i + f + 21, 64) / 64));
        }
        env = v == 'c' ? Clamp((f - I_END) / 60, 0, 1) : 1;
        o->dy = (int)JsRound(env * 3 * (1 - cos(2 * PI * fi / 512)) / 2);
        glow = b;
        rays = 0.022 + 0.018 * (0.5 + 0.5 * sin(2 * PI * fi / 512));
        light = 0.03 * (0.5 + 0.5 * sin(2 * PI * fi / 1024));
        if (intro)
        {
            double r = Clamp((f - I_RISE_START) / (I_RISE_END - I_RISE_START), 0, 1);
            double last = I_IGNITE + 122 * I_IGNITE_SCALE;

            o->show = f >= I_RISE_START;
            o->dy = (int)JsRound(110 * pow(1 - r, 3));
            alpha = Q16((f - I_RISE_START) / 90);
            sway = 0;
            glow = Clamp((f - last) / 30, 0, 1);
            gb = 1;
            if (f >= last && f < I_FLASH_END)
                flare = Q16(1 - fabs((f - I_FLASH_PEAK) / (I_FLASH_END - last)));
            rays *= Clamp(f / 200, 0, 1);
            light = 0;
            nearv = Q16(1 - Clamp((f - 150) / 120, 0, 1));
            o->markMode = 2;
            for (int j = 0; j < stageCount; j++)
            {
                double sb = Clamp((f - (I_IGNITE + gUiTitleStages[j][6] * I_IGNITE_SCALE)) / 12, 0, 1);

                if (sb > 0)
                {
                    int c[3];

                    RingRgb(sb, c);
                    o->stageRgba[j][0] = (float)c[0];
                    o->stageRgba[j][1] = (float)c[1];
                    o->stageRgba[j][2] = (float)c[2];
                    o->stageRgba[j][3] = 1;
                }
            }
        }
        else if (b > 0)
        {
            o->markMode = 1;
            RingRgb(b, o->markCol);
        }
        for (int row = 0; row < 168; row++)
            o->sway[row] = sway ? (float)(D * JsRound(env * sin(2 * PI * (fi / 512 - (row - 4) / 200.0)))) : 0;
        o->alpha8 = A8(alpha);
        if (glow > 0)
        {
            RingRgb(fmax(0.6, gb < 0 ? b : gb), o->glowCol);
            o->glow8 = A8(Q16(glow * 0.38));
            if (flare > 0)
                o->flare8 = A8(Q16(flare * 0.3));
        }
        o->shadow8 = 46;
        o->shadowOff[0] = 4;
        o->shadowOff[1] = (int)JsMod(floor(o->V / (double)D) + 6, 256);
        o->light8 = light > 0 ? A8(light) : 0;
        o->rays8 = A8(rays);
        o->near8 = nearv > 0 ? A8(5 * nearv / 16) : 0;
        /* glints (sparkList), only while Rayquaza is drawn */
        if (o->show)
        {
            int n = gUiTitleSparkCount < MAX_SPARKS ? gUiTitleSparkCount : MAX_SPARKS;

            for (int i = 0; i < n; i++)
            {
                const double *sp = gUiTitleSparks[i];
                double life = JsMod(fi + sp[2], 256), u;
                struct Glint *g;

                if (life >= 64)
                    continue;
                if (SparkAt(v, fi - life, sp) < 0.5 + 0.5 * i / n)
                    continue;
                u = life / 64;
                g = &o->glints[o->glintCount++];
                if (u < 0.08)
                {
                    g->rgb[0] = 255;
                    g->rgb[1] = 255;
                    g->rgb[2] = 232;
                }
                else
                {
                    RingRgb(u < 0.45 ? 1 : u < 0.75 ? 0.72 : 0.5, g->rgb);
                }
                g->x = o->ax + (int)(sp[0] + JsRound(sin((life + i * 13) / 14))) * D;
                g->y = o->ay + (int)(sp[1] - floor(life / 5)) * D + o->dy;
                g->tw = sp[3] != 0 && u < 0.08;
            }
        }
    }
}

void UiTitleBg_Ui(char v, int f, struct UiTitleUi *u)
{
    double fi = v == 'c' ? f - I_END + 128 : f;
    double pressPhase = v == 'c' ? f - I_PRESS : f;
    double gm = JsMod(fi - 128, 256);

    u->logo = true;
    u->press = 1;
    u->glint = gm < 12 ? (int)gm : -1;
    if (v == 'c' && f < I_END)
    {
        u->logo = f >= I_LOGO;
        u->press = f < I_PRESS ? 0 : Q16((f - I_PRESS) / I_PRESS_IN);
        u->glint = -1;
    }
    u->blinkOn = JsMod(pressPhase, 64) < 40;
    u->white = 0;
    if (v == 'c')
    {
        if (f < I_FADE_END)
            u->white = Q16(1 - f / I_FADE_END);
        else if (f >= I_FLASH_START && f < I_FLASH_PEAK)
            u->white = Q16(0.85 * (f - I_FLASH_START) / (I_FLASH_PEAK - I_FLASH_START));
        else if (f >= I_FLASH_PEAK && f < I_FLASH_END)
            u->white = Q16(0.85 * (1 - (f - I_FLASH_PEAK) / (I_FLASH_END - I_FLASH_PEAK)));
    }
}

/* ---------------------------------------------------------------- GL */

/* the design's shaders (title_emerald_gl.js, as exported for the native benchmark) */
static const char *sVertexShader =
    "#version 330 core\n"
    "void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char *sBackgroundShader =
    "#version 330 core\n"
    "precision highp float; precision highp int; precision highp sampler2D;\n"
    "uniform sampler2D uSky, uRq, uMarks, uStage, uGlow1, uGlow2, uCloud, uCloudFar, uCloudFront, uCloudNear, uRays, uPar;\n"
    "uniform ivec2 uL, uA;\n"
    "uniform int uEnh, uV, uVfar, uVnear, uShow, uDy, uAlpha8, uMarkMode, uGlow8, uFlare8, uShadow8;\n"
    "uniform ivec3 uMarkCol, uGlowCol;\n"
    "uniform ivec2 uShadowOff;\n"
    "uniform int uLight8, uRays8, uRaysY, uNear8, uHazeX, uHazeY, uSparks;\n"
    "out vec4 oCol;\n"
    "float par(int x, int row) { return texelFetch(uPar, ivec2(x, row), 0).r; }\n"
    "vec4 par4(int x, int row) { return texelFetch(uPar, ivec2(x, row), 0); }\n"
    "ivec3 rgb8(vec4 t) { return ivec3(floor(t.rgb * 255.0 + 0.5)); }\n"
    "ivec3 lighter(ivec3 d, ivec3 s, int A) { return min(ivec3(255), d + ((s * A + ivec3(255)) >> 8)); }\n"
    "ivec3 darken16(ivec3 d) { ivec3 x = d * 239; return (x + ivec3(128) + (x >> 8)) >> 8; }\n"
    "void over(inout ivec3 dc, inout int da, ivec3 sc, int sa, int A) {\n"
    "  ivec3 s = (sc * (A + 1)) >> 8; int a = (sa * (A + 1)) >> 8;\n"
    "  dc = min(ivec3(255), s + ((dc * (256 - a)) >> 8)); da = a + ((da * (256 - a)) >> 8);\n"
    "}\n"
    "ivec3 overA(ivec3 d, ivec3 s, int A) { int a = 255; over(d, a, s, 255, A); return d; }\n"
    "void overU(inout ivec3 dc, inout int da, ivec3 sc, int A) {\n"
    "  int p = 65535 - 255 * (A + 1), k = (p + (p >> 8)) >> 8;\n"
    "  dc = (sc * (A + 1) + dc * k) >> 8; da = (255 * (A + 1) + da * k) >> 8;\n"
    "}\n"
    "int bayer4(int x, int y) { int v = x ^ y; return ((v & 1) << 3) | ((y & 1) << 2) | (v & 2) | ((y >> 1) & 1); }\n"
    "bool cloudRow(sampler2D t, int x, int y, int V, int w0, out ivec3 c) {\n"
    "  int k = (y - uA.y + V) >> 1;\n"
    "  int line = (2 * k - V) >> 1;\n"
    "  int wv = int(par(w0 + (line & 63), 0));\n"
    "  int u = ((x - (uA.x - wv)) >> 1) & 255;\n"
    "  vec4 s = texelFetch(t, ivec2(u, k & 255), 0);\n"
    "  c = rgb8(s); return s.a > 0.5;\n"
    "}\n"
    "void main() {\n"
    "  int x = int(gl_FragCoord.x), y = uL.y - 1 - int(gl_FragCoord.y);\n"
    "  int ax = (x - uA.x) >> 1, ay = (y - uA.y) >> 1;\n"
    "  ivec3 c = rgb8(texelFetch(uSky, ivec2(ax & 1, clamp(ay, 0, 159)), 0));\n"
    "  ivec3 s;\n"
    "  if (uEnh == 1) {\n"
    "    if (uLight8 > 0) c = lighter(c, ivec3(255), uLight8);\n"
    "    int rx = (x - (uA.x - 160)) >> 1, ry = (y - uRaysY) >> 1;\n"
    "    if (rx >= 0 && rx < 400 && ry >= 0 && ry < 200) { vec4 r = texelFetch(uRays, ivec2(rx, ry), 0); if (r.a > 0.5) c = lighter(c, rgb8(r), uRays8); }\n"
    "    if (cloudRow(uCloudFar, x, y, uVfar, 64, s)) c = lighter(darken16(c), s, 80);\n"
    "  }\n"
    "  if (uShow == 1) {\n"
    "    int r = (y - (uA.y - 8 + uDy)) >> 1;\n"
    "    if (r >= 0 && r < 168) {\n"
    "      int sw = uEnh == 1 ? int(par(r, 1)) : 0;\n"
    "      int qx = (x - (uA.x - 8 + sw)) >> 1;\n"
    "      if (qx >= 0 && qx < 248) {\n"
    "        ivec3 col = ivec3(0); int a = 0;\n"
    "        ivec2 o = ivec2(qx - 4, r - 4);\n"
    "        bool inArt = o.x >= 0 && o.x < 240 && o.y >= 0 && o.y < 160;\n"
    "        if (inArt) { vec4 t = texelFetch(uRq, o, 0); if (t.a > 0.5) { col = rgb8(t); a = 255; } }\n"
    "        if (a == 255 && uShadow8 > 0) {\n"
    "          vec4 sh = texelFetch(uCloudFront, ivec2((qx - uShadowOff.x) & 255, (r + uShadowOff.y) & 255), 0);\n"
    "          if (sh.a > 0.5) col = ((ivec3(6, 40, 58) * uShadow8 + ivec3(255)) >> 8) + ((col * (255 - uShadow8) + ivec3(255)) >> 8);\n"
    "        }\n"
    "        if (inArt && uMarkMode == 2) {\n"
    "          int si = int(texelFetch(uStage, o, 0).r * 255.0 + 0.5);\n"
    "          if (si > 0) { vec4 st = par4(si - 1, 2); if (st.a > 0.5) { col = ivec3(st.rgb); a = 255; } }\n"
    "        } else if (inArt && uMarkMode == 1) {\n"
    "          if (texelFetch(uMarks, o, 0).a > 0.5) { col = uMarkCol; a = 255; }\n"
    "        }\n"
    "        if (uGlow8 > 0 && texelFetch(uGlow1, ivec2(qx, r), 0).a > 0.5) overU(col, a, uGlowCol, uGlow8);\n"
    "        if (uFlare8 > 0 && texelFetch(uGlow2, ivec2(qx, r), 0).a > 0.5) overU(col, a, uGlowCol, uFlare8);\n"
    "        if (a > 0) { int ca = 255; over(c, ca, col, a, uAlpha8); }\n"
    "      }\n"
    "    }\n"
    "    if (uSparks > 0) {\n"
    "      int bits0 = -1, bits1 = -1;\n"
    "      if (y >= 0 && y < 512) { vec4 m = par4(y & 255, 5 + (y >> 8)); bits0 = int(m.r); bits1 = int(m.g); }\n"
    "      for (int h = 0; h < 2; h++) {\n"
    "        int b = h == 0 ? bits0 : bits1;\n"
    "        for (int k = 0; k < 20 && b != 0; k++, b >>= 1) {\n"
    "          int i = h * 20 + k;\n"
    "          if ((b & 1) == 0 || i >= uSparks) continue;\n"
    "          vec4 p = par4(2 * i, 3);\n"
    "          int gx = int(p.x), gy = int(p.y);\n"
    "          bool hit = x >= gx && x < gx + 2 && y >= gy && y < gy + 2;\n"
    "          if (!hit && p.z > 0.5) hit = (y >= gy && y < gy + 2 && x >= gx - 2 && x < gx + 4) || (x >= gx && x < gx + 2 && y >= gy - 2 && y < gy + 4);\n"
    "          if (hit) c = ivec3(par4(2 * i + 1, 3).rgb);\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  bool front = uEnh == 1 ? cloudRow(uCloudFront, x, y, uV, 0, s) : cloudRow(uCloud, x, y, uV, 0, s);\n"
    "  if (front) c = lighter(darken16(c), s, 96);\n"
    "  if (uEnh == 1) {\n"
    "    if (uNear8 > 0) {\n"
    "      int x0 = ((uA.x + 101) & 511) - 512, y0 = ((uA.y - uVnear) & 511) - 512;\n"
    "      vec4 n = texelFetch(uCloudNear, ivec2(((x - x0) >> 1) & 255, ((y - y0) >> 1) & 255), 0);\n"
    "      if (n.a > 0.5) c = lighter(c, rgb8(n), uNear8);\n"
    "    }\n"
    "    int hy = (y - uHazeY) >> 1;\n"
    "    if (hy >= 0 && hy < 42) {\n"
    "      int hx = (x - uHazeX) >> 1;\n"
    "      if (bayer4(hx & 3, hy & 3) < int(par(hy, 4))) c = overA(c, ivec3(181, 231, 173), 26);\n"
    "    }\n"
    "  }\n"
    "  oCol = vec4(vec3(c) / 255.0, 1.0);\n"
    "}\n";

static const char *const sSamplers[UI_TITLE_LAYER_COUNT] = {
    "uSky", "uRq", "uMarks", "uStage", "uGlow1", "uGlow2", "uCloud", "uCloudFar", "uCloudFront", "uCloudNear", "uRays",
};

typedef void (APIENTRY *PfnUniform2i)(GLint, GLint, GLint);
typedef void (APIENTRY *PfnUniform3i)(GLint, GLint, GLint, GLint);
static PfnUniform2i sUniform2i;
static PfnUniform3i sUniform3i;

static bool sTried;
static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sLayers[UI_TITLE_LAYER_COUNT];
static GLuint sParTexture;
static GLuint sFramebuffer;
static GLuint sTarget;
static int sTargetW;
static int sTargetH;
static float sPar[256 * 7 * 4]; /* the parameter texture, rows as in title_emerald_gl.js */
static int sGlintRows[512 * 2];
static int sHaze[42];

enum
{
    U_L, U_A, U_ENH, U_V, U_VFAR, U_VNEAR, U_SHOW, U_DY, U_ALPHA8, U_MARKMODE, U_MARKCOL, U_GLOW8, U_FLARE8,
    U_GLOWCOL, U_SHADOW8, U_SHADOWOFF, U_LIGHT8, U_RAYS8, U_RAYSY, U_NEAR8, U_HAZEX, U_HAZEY, U_SPARKS, U_PAR,
    U_COUNT
};
static const char *const sUniformNames[U_COUNT] = {
    "uL", "uA", "uEnh", "uV", "uVfar", "uVnear", "uShow", "uDy", "uAlpha8", "uMarkMode", "uMarkCol", "uGlow8",
    "uFlare8", "uGlowCol", "uShadow8", "uShadowOff", "uLight8", "uRays8", "uRaysY", "uNear8", "uHazeX", "uHazeY",
    "uSparks", "uPar",
};
static GLint sUniforms[U_COUNT];

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint ok = 0;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];

        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("Native UI: title shader: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint MakeTexture(int w, int h, GLint internal, GLenum format, GLenum type, const void *data)
{
    GLuint texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    return texture;
}

static bool Init(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    GLint unpack = 4;

    sTried = true;
    sUniform2i = (PfnUniform2i)SDL_GL_GetProcAddress("glUniform2i");
    sUniform3i = (PfnUniform3i)SDL_GL_GetProcAddress("glUniform3i");
    if (sUniform2i == NULL || sUniform3i == NULL)
        return false;
    vs = Compile(GL_VERTEX_SHADER, sVertexShader);
    fs = Compile(GL_FRAGMENT_SHADER, sBackgroundShader);
    if (vs == 0 || fs == 0)
        return false;
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vs);
    dglAttachShader(sProgram, fs);
    dglLinkProgram(sProgram);
    dglDeleteShader(vs);
    dglDeleteShader(fs);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        SDL_Log("Native UI: the title shader did not link");
        return false;
    }
    for (int i = 0; i < U_COUNT; i++)
        sUniforms[i] = dglGetUniformLocation(sProgram, sUniformNames[i]);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < UI_TITLE_LAYER_COUNT; i++)
    {
        int w, h;
        uint8_t *rgba = UiPng_Decode(gUiTitleLayerPngs[i].data, (int)gUiTitleLayerPngs[i].size, &w, &h);

        if (rgba == NULL)
        {
            SDL_Log("Native UI: the title layer %s could not be read", gUiTitleLayerPngs[i].name);
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
            return false;
        }
        sLayers[i] = MakeTexture(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    }
    memset(sPar, 0, sizeof(sPar));
    sParTexture = MakeTexture(256, 7, GL_RGBA32F, GL_RGBA, GL_FLOAT, sPar);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    dglGenVertexArrays(1, &sVertexArray);
    dglGenFramebuffers(1, &sFramebuffer);
    HazeRows(sHaze);
    return true;
}

static bool EnsureTarget(int w, int h)
{
    if (sTarget != 0 && sTargetW == w && sTargetH == h)
        return true;
    if (sTarget != 0)
        glDeleteTextures(1, &sTarget);
    sTarget = MakeTexture(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
    dglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sTarget, 0);
    if (dglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("Native UI: the title's %dx%d framebuffer is incomplete", w, h);
        return false;
    }
    sTargetW = w;
    sTargetH = h;
    return true;
}

static void UploadParameters(const struct Frame *fr)
{
    float *row;
    int stageCount = gUiTitleStageCount < MAX_STAGES ? gUiTitleStageCount : MAX_STAGES;

    memset(sPar, 0, sizeof(sPar));
    for (int i = 0; i < 64; i++)
    {
        sPar[i * 4] = fr->wave[i];
        sPar[(64 + i) * 4] = fr->waveFar[i];
    }
    row = sPar + 256 * 4;
    for (int i = 0; i < 168; i++)
        row[i * 4] = fr->sway[i];
    row = sPar + 2 * 256 * 4;
    for (int j = 0; j < stageCount; j++)
        memcpy(row + j * 4, fr->stageRgba[j], 4 * sizeof(float));
    row = sPar + 3 * 256 * 4;
    memset(sGlintRows, 0, sizeof(sGlintRows));
    for (int k = 0; k < fr->glintCount; k++)
    {
        const struct Glint *g = &fr->glints[k];
        int y0 = g->tw ? g->y - 2 : g->y, y1 = g->tw ? g->y + 3 : g->y + 1; /* rows it covers */

        row[k * 8] = (float)g->x;
        row[k * 8 + 1] = (float)g->y;
        row[k * 8 + 2] = (float)g->tw;
        row[k * 8 + 4] = (float)g->rgb[0];
        row[k * 8 + 5] = (float)g->rgb[1];
        row[k * 8 + 6] = (float)g->rgb[2];
        for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < 512; y++)
            sGlintRows[y * 2 + (k < 20 ? 0 : 1)] |= 1 << (k % 20);
    }
    row = sPar + 4 * 256 * 4;
    for (int i = 0; i < 42; i++)
        row[i * 4] = (float)sHaze[i];
    for (int y = 0; y < 512; y++) /* rows 5-6: the glints touching each screen row */
    {
        float *t = sPar + ((5 + (y >> 8)) * 256 + (y & 255)) * 4;

        t[0] = (float)sGlintRows[y * 2];
        t[1] = (float)sGlintRows[y * 2 + 1];
    }
    glBindTexture(GL_TEXTURE_2D, sParTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 7, GL_RGBA, GL_FLOAT, sPar);
}

bool UiTitleBg_Draw(char version, double f)
{
    const struct UiView *view = UiDraw_View();
    int LW = view->canvasWidth, LH = view->canvasHeight, s = view->scale;
    struct Frame fr;
    GLint drawFb = 0, readFb = 0, viewport[4], program = 0, activeTexture = 0;
    GLboolean scissor, blend;

    if (!sTried && !(sReady = Init()))
        SDL_Log("Native UI: the title background is off (a plain colour instead)");
    if (!sReady || LW <= 0 || LH <= 0 || LW > 4096 || LH > 4096)
        return false;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    blend = glIsEnabled(GL_BLEND);

    if (!EnsureTarget(LW, LH))
    {
        dglBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
        return false;
    }
    FrameState(version, f, LW, LH, &fr);
    UploadParameters(&fr);

    /* pass 1: the background at the logical size */
    dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, LW, LH);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    for (int i = 0; i < UI_TITLE_LAYER_COUNT; i++)
    {
        dglActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, sLayers[i]);
        dglUniform1i(dglGetUniformLocation(sProgram, sSamplers[i]), i);
    }
    dglActiveTexture(GL_TEXTURE0 + UI_TITLE_LAYER_COUNT);
    glBindTexture(GL_TEXTURE_2D, sParTexture);
    dglUniform1i(sUniforms[U_PAR], UI_TITLE_LAYER_COUNT);
    sUniform2i(sUniforms[U_L], LW, LH);
    sUniform2i(sUniforms[U_A], fr.ax, fr.ay);
    dglUniform1i(sUniforms[U_ENH], fr.enh);
    dglUniform1i(sUniforms[U_V], fr.V);
    dglUniform1i(sUniforms[U_VFAR], fr.Vfar);
    dglUniform1i(sUniforms[U_VNEAR], fr.Vnear);
    dglUniform1i(sUniforms[U_SHOW], fr.show);
    dglUniform1i(sUniforms[U_DY], fr.dy);
    dglUniform1i(sUniforms[U_ALPHA8], fr.alpha8);
    dglUniform1i(sUniforms[U_MARKMODE], fr.markMode);
    sUniform3i(sUniforms[U_MARKCOL], fr.markCol[0], fr.markCol[1], fr.markCol[2]);
    dglUniform1i(sUniforms[U_GLOW8], fr.glow8);
    dglUniform1i(sUniforms[U_FLARE8], fr.flare8);
    sUniform3i(sUniforms[U_GLOWCOL], fr.glowCol[0], fr.glowCol[1], fr.glowCol[2]);
    dglUniform1i(sUniforms[U_SHADOW8], fr.shadow8);
    sUniform2i(sUniforms[U_SHADOWOFF], fr.shadowOff[0], fr.shadowOff[1]);
    dglUniform1i(sUniforms[U_LIGHT8], fr.light8);
    dglUniform1i(sUniforms[U_RAYS8], fr.rays8);
    dglUniform1i(sUniforms[U_RAYSY], fr.raysY);
    dglUniform1i(sUniforms[U_NEAR8], fr.near8);
    dglUniform1i(sUniforms[U_HAZEX], fr.hazeX);
    dglUniform1i(sUniforms[U_HAZEY], fr.hazeY);
    dglUniform1i(sUniforms[U_SPARKS], fr.glintCount);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* pass 2: shown at the UI's whole-number scale (nearest), at the canvas origin */
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, sFramebuffer);
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFb);
    {
        int x0 = view->originX;
        int y0 = view->outputHeight - view->originY - LH * s; /* GL rows count from the bottom */

        dglBlitFramebuffer(0, 0, LW, LH, x0, y0, x0 + LW * s, y0 + LH * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    /* the state the rest of the frame expects */
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFb);
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFb);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    for (int i = UI_TITLE_LAYER_COUNT; i >= 0; i--)
    {
        dglActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    dglActiveTexture((GLenum)activeTexture);
    dglUseProgram((GLuint)program);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (blend)
        glEnable(GL_BLEND);
    return true;
}

void UiTitleBg_Shutdown(void)
{
    if (!sReady)
    {
        sTried = false;
        return;
    }
    glDeleteTextures(UI_TITLE_LAYER_COUNT, sLayers);
    glDeleteTextures(1, &sParTexture);
    if (sTarget != 0)
        glDeleteTextures(1, &sTarget);
    dglDeleteFramebuffers(1, &sFramebuffer);
    dglDeleteVertexArrays(1, &sVertexArray);
    dglDeleteProgram(sProgram);
    sTarget = 0;
    sTargetW = sTargetH = 0;
    sReady = false;
    sTried = false;
}
#endif // ENABLE_NATIVE_UI
