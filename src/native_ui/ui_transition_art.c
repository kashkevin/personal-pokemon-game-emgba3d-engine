#ifdef ENABLE_NATIVE_UI
/*
 * MS-108: a battle transition's own GBA art drawn as smooth shapes at the window's resolution (see
 * native_ui/ui_transition_art.h). One fragment shader does it all, at every window pixel the layer covers: where that
 * pixel is in the art (the GBA screen position plus the layer's scroll on that GBA line - across, the line's own, as
 * the GBA moves each line; up and down, from one line's middle to the next where neighbouring lines differ by a few
 * pixels, so a wave that stretches the art shows the GBA's rows at the lines' middles without a saw-tooth between
 * them), the weights of the 4 x 4 art pixels round it (each pixel a square blurred by a Gaussian of SIGMA art pixels:
 * per axis the difference of two normal distribution values), the weight of each palette slot among them (thin
 * strokes' slots weighing more, BOOST at most: a slot's mean width over the art, twice its area over its outline, of
 * 1.5 pixels or less weighs BOOST, of 3.5 or more 1), then the two strongest slots coloured from the frame's palette
 * and blended across half a window pixel either side of their edge, and the GBA's blend with what is under it. Past
 * the picture's edges over the 3D field a background is carried out as the compositor carries the picture out. Test
 * switches: PKMN_TRANSITION_ART=0 (the GBA picture of the art instead), PKMN_TRANSITION_ART_SIGMA (the Gaussian's
 * width), PKMN_TRANSITION_ART_BOOST (the strokes' weight). Project-owned.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "native_ui/ui_transition_art.h"

#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_RGB 0x80C8
#endif

#define SIGMA 0.75f        /* the Gaussian's width in art pixels (the 4 x 4 pixels round a point hold 97 % of it) */
#define BOOST 1.5f         /* a thin stroke's weight against the slots round it */
#define MAX_SIDE 512

static const char sVertexShader[] =
    "#version 330 core\n"
    "void main()\n"
    "{\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char sFragmentShader[] =
    "#version 330 core\n"
    "uniform usampler2D uSlots;\n"   /* the layer's art pixels: palette slots */
    "uniform sampler2D uPal;\n"      /* 512 x 1: the frame's colours */
    "uniform float uScreenH;\n"      /* the window's height (gl_FragCoord counts rows from the bottom) */
    "uniform vec3 uPic;\n"           /* the GBA picture's left and top in window pixels, window pixels a GBA pixel */
    "uniform int uBackground;\n"     /* 1: a background (repeats; carried out by clamping) */
    "uniform int uCarry;\n"          /* 1: a background carried out past the picture's edges */
    "uniform ivec2 uSize;\n"         /* the art's size in pixels (powers of two) */
    "uniform vec2 uOrigin;\n"        /* the art's (0, 0) on the GBA screen */
    "uniform int uPalBase;\n"
    "uniform vec4 uLines[80];\n"     /* the scroll by GBA line: x, y of an even line, then of the odd one */
    "uniform vec4 uBoost[64];\n"     /* each slot's weight */
    "uniform int uMode;\n"           /* 0 as it is, 1 alpha blend, 2 lighten, 3 darken */
    "uniform vec3 uEffect;\n"        /* eva, evb, y (0..1) */
    "uniform float uSigma;\n"
    "uniform float uBand;\n"
    "out vec4 fragColor;\n"
    "\n"
    "float Cdf(float x)\n"           /* the normal distribution: erf by Abramowitz and Stegun 7.1.26 */
    "{\n"
    "    float z = abs(x) * 0.70710678;\n"
    "    float t = 1.0 / (1.0 + 0.3275911 * z);\n"
    "    float e = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t\n"
    "                     + 0.254829592) * t * exp(-z * z);\n"
    "    return 0.5 + 0.5 * (x < 0.0 ? -e : e);\n"
    "}\n"
    "\n"
    /* an art pixel: a background's as the GBA screen shows it - one off the screen's edge (scroll the line's) takes
     * the edge's, as what lies past it on the map is never seen (and the compositor carries the edge out) - and
     * repeating past its map's edge; nothing outside a sprite */
    "uint Slot(ivec2 q, vec2 scroll)\n"
    "{\n"
    "    if (uBackground != 0)\n"
    "    {\n"
    "        vec2 screen = clamp(vec2(q) + 0.5 - scroll, vec2(0.5), vec2(239.5, 159.5));\n"
    "        q = ivec2(floor(screen + scroll));\n"
    "        q = ivec2(q.x & (uSize.x - 1), q.y & (uSize.y - 1));\n"
    "    }\n"
    "    else if (q.x < 0 || q.y < 0 || q.x >= uSize.x || q.y >= uSize.y)\n"
    "        return 0u;\n"
    "    return texelFetch(uSlots, q, 0).r;\n"
    "}\n"
    "\n"
    "vec2 Scroll(int line)\n"
    "{\n"
    "    vec4 l = uLines[line >> 1];\n"
    "    return (line & 1) == 0 ? l.xy : l.zw;\n"
    "}\n"
    "\n"
    "vec4 Colour(uint slot)\n"       /* premultiplied; colour 0 of a palette is see-through */
    "{\n"
    "    if ((slot & 15u) == 0u)\n"
    "        return vec4(0.0);\n"
    "    return vec4(texelFetch(uPal, ivec2(uPalBase + int(slot), 0), 0).rgb, 1.0);\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 g = (vec2(gl_FragCoord.x, uScreenH - gl_FragCoord.y) - uPic.xy) / uPic.z;\n"
    /* carried out as the compositor carries the picture out (gl_battle.c DrawCarriedOut): each line's edge pixel to the
     * sides, the first line's corner pixels above (the left half the left one), the last line's below */
    "    if (uCarry != 0 && (g.x < 0.0 || g.x >= 240.0 || g.y < 0.0 || g.y >= 160.0))\n"
    "    {\n"
    "        if (g.y < 0.0 || g.y >= 160.0)\n"
    "            g.x = g.x < 120.0 ? 0.5 : 239.5;\n"
    "        g = clamp(g, vec2(0.5), vec2(239.5, 159.5));\n"
    "    }\n"
    /* the scroll of the GBA line the pixel is on: across the line's width as the GBA moves it (the weave's comb); up
     * and down from one line's middle to the next where they differ by a few pixels (a wave: the GBA's rows at the
     * lines' middles, no saw-tooth where it stretches the art), else the line's own */
    "    int line = int(clamp(floor(g.y), 0.0, 159.0));\n"
    "    float fy = clamp(g.y - 0.5, 0.0, 159.0);\n"
    "    int l0 = int(floor(fy)), l1 = min(l0 + 1, 159);\n"
    "    vec2 own = Scroll(line), s0 = Scroll(l0), s1 = Scroll(l1);\n"
    "    float sy = abs(s1.y - s0.y) <= 8.0 ? mix(s0.y, s1.y, fy - float(l0)) : own.y;\n"
    "    vec2 scroll = vec2(own.x, sy);\n"
    "    vec2 p = g + scroll - uOrigin;\n"
    "    ivec2 c = ivec2(floor(p - 0.5)) - 1;\n"
    "    float wx[4], wy[4];\n"
    "    for (int i = 0; i < 4; i++)\n"
    "    {\n"
    "        float x0 = float(c.x + i), y0 = float(c.y + i);\n"
    "        wx[i] = Cdf((p.x - x0) / uSigma) - Cdf((p.x - x0 - 1.0) / uSigma);\n"
    "        wy[i] = Cdf((p.y - y0) / uSigma) - Cdf((p.y - y0 - 1.0) / uSigma);\n"
    "    }\n"
    "    uint slots[16];\n"
    "    float field[16];\n"
    "    int n = 0;\n"
    "    for (int j = 0; j < 4; j++)\n"
    "        for (int i = 0; i < 4; i++)\n"
    "        {\n"
    "            uint s = Slot(c + ivec2(i, j), scroll);\n"
    "            int k = 0;\n"
    "            while (k < n && slots[k] != s)\n"
    "                k++;\n"
    "            if (k == n)\n"
    "            {\n"
    "                slots[n] = s;\n"
    "                field[n] = 0.0;\n"
    "                n++;\n"
    "            }\n"
    "            field[k] += wx[i] * wy[j];\n"
    "        }\n"
    "    int a = -1, b = -1;\n"
    "    float fa = -1.0, fb = -1.0;\n"
    "    for (int k = 0; k < n; k++)\n"
    "    {\n"
    "        float f = field[k] * uBoost[int(slots[k]) >> 2][int(slots[k]) & 3];\n"
    "        if (f > fa)\n"
    "        {\n"
    "            b = a;\n"
    "            fb = fa;\n"
    "            a = k;\n"
    "            fa = f;\n"
    "        }\n"
    "        else if (f > fb)\n"
    "        {\n"
    "            b = k;\n"
    "            fb = f;\n"
    "        }\n"
    "    }\n"
    "    vec4 col = Colour(slots[a]);\n"
    "    if (b >= 0)\n"
    "        col = mix(Colour(slots[b]), col, 0.5 + 0.5 * clamp((fa - fb) * uBand, 0.0, 1.0));\n"
    "    if (uMode == 1)\n"
    "        col = vec4(col.rgb * uEffect.x, col.a * (1.0 - uEffect.y));\n"
    "    else if (uMode == 2)\n"
    "        col.rgb += (col.a - col.rgb) * uEffect.z;\n"
    "    else if (uMode == 3)\n"
    "        col.rgb *= 1.0 - uEffect.z;\n"
    "    if (col.a <= 0.0 && col.r <= 0.0 && col.g <= 0.0 && col.b <= 0.0)\n"
    "        discard;\n"
    "    fragColor = col;\n"
    "}\n";

enum
{
    U_SLOTS, U_PAL, U_SCREEN_H, U_PIC, U_BACKGROUND, U_CARRY, U_SIZE, U_ORIGIN, U_PAL_BASE, U_LINES, U_BOOST, U_MODE,
    U_EFFECT, U_SIGMA, U_BAND, U_COUNT
};
static const char *const sUniformNames[U_COUNT] = {
    "uSlots", "uPal", "uScreenH", "uPic", "uBackground", "uCarry", "uSize", "uOrigin", "uPalBase", "uLines[0]",
    "uBoost[0]", "uMode", "uEffect", "uSigma", "uBand",
};

typedef void (APIENTRY *PfnUniform2i)(GLint location, GLint v0, GLint v1);

struct Layer
{
    GLuint texture;
    int w, h;
    bool background;
    uint8_t *pixels;        /* the art last uploaded (to see a change) */
    float boost[256];
};

static bool sTried, sReady, sOff;
static GLuint sProgram, sVertexArray, sPalTexture;
static GLint sUniforms[U_COUNT];
static PfnUniform2i sUniform2i;
static struct Layer sLayers[UI_ART_LAYER_COUNT];
static uint8_t sPalette[512 * 4];
static bool sPaletteDirty;
static float sSigma = SIGMA, sBoost = BOOST;

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
        SDL_Log("Native UI: the transition art's shader: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool Init(void)
{
    const char *value;
    GLuint vs, fs;
    GLint ok = 0;

    sTried = true;
    if ((value = SDL_getenv("PKMN_TRANSITION_ART")) != NULL && strcmp(value, "0") == 0)
    {
        sOff = true;
        SDL_Log("Native UI: the transitions' art as the GBA draws it (PKMN_TRANSITION_ART=0)");
        return false;
    }
    if ((value = SDL_getenv("PKMN_TRANSITION_ART_SIGMA")) != NULL && atof(value) > 0.1)
        sSigma = (float)atof(value);
    if ((value = SDL_getenv("PKMN_TRANSITION_ART_BOOST")) != NULL && atof(value) >= 1.0)
        sBoost = (float)atof(value);
    sUniform2i = (PfnUniform2i)SDL_GL_GetProcAddress("glUniform2i");
    if (sUniform2i == NULL)
        return false;
    vs = Compile(GL_VERTEX_SHADER, sVertexShader);
    fs = Compile(GL_FRAGMENT_SHADER, sFragmentShader);
    if (vs == 0 || fs == 0)
    {
        if (vs != 0)
            dglDeleteShader(vs);
        if (fs != 0)
            dglDeleteShader(fs);
        return false;
    }
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vs);
    dglAttachShader(sProgram, fs);
    dglLinkProgram(sProgram);
    dglDeleteShader(vs);
    dglDeleteShader(fs);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        SDL_Log("Native UI: the transition art's shader did not link");
        dglDeleteProgram(sProgram);
        sProgram = 0;
        return false;
    }
    for (int i = 0; i < U_COUNT; i++)
        sUniforms[i] = dglGetUniformLocation(sProgram, sUniformNames[i]);
    glGenTextures(1, &sPalTexture);
    glBindTexture(GL_TEXTURE_2D, sPalTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, sPalette);
    glBindTexture(GL_TEXTURE_2D, 0);
    dglGenVertexArrays(1, &sVertexArray);
    SDL_Log("Native UI: the transitions' art drawn smooth at the window's resolution (sigma %.2f, strokes x%.2f)",
            sSigma, sBoost);
    return true;
}

bool UiArt_Ready(void)
{
    if (!sTried)
        sReady = Init();
    return sReady;
}

void UiArt_SetPalette(const uint16_t *colours)
{
    uint8_t rgba[512 * 4];

    for (int i = 0; i < 512; i++)
    {
        int r = colours[i] & 31, g = (colours[i] >> 5) & 31, b = (colours[i] >> 10) & 31;

        rgba[i * 4 + 0] = (uint8_t)((r << 3) | (r >> 2));
        rgba[i * 4 + 1] = (uint8_t)((g << 3) | (g >> 2));
        rgba[i * 4 + 2] = (uint8_t)((b << 3) | (b >> 2));
        rgba[i * 4 + 3] = 255;
    }
    if (memcmp(rgba, sPalette, sizeof(rgba)) != 0)
    {
        memcpy(sPalette, rgba, sizeof(rgba));
        sPaletteDirty = true;
    }
}

/* Each slot's weight from its mean width over the art: twice its pixels over its outline's edges. */
static void Boosts(struct Layer *layer)
{
    static uint32_t area[256], edges[256];
    const uint8_t *px = layer->pixels;
    int w = layer->w, h = layer->h;

    memset(area, 0, sizeof(area));
    memset(edges, 0, sizeof(edges));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint8_t s = px[y * w + x], right, down;

            area[s]++;
            /* a background repeats; outside a sprite is see-through (slot 0) */
            right = x + 1 < w ? px[y * w + x + 1] : layer->background ? px[y * w] : 0;
            down = y + 1 < h ? px[(y + 1) * w + x] : layer->background ? px[x] : 0;
            if (right != s)
                edges[s]++, edges[right]++;
            if (down != s)
                edges[s]++, edges[down]++;
            if (!layer->background && x == 0 && s != 0)
                edges[s]++;
            if (!layer->background && y == 0 && s != 0)
                edges[s]++;
        }
    for (int s = 0; s < 256; s++)
    {
        double width = edges[s] != 0 ? 2.0 * area[s] / edges[s] : 1e9, k = (3.5 - width) / 2.0;

        k = k < 0.0 ? 0.0 : k > 1.0 ? 1.0 : k;
        layer->boost[s] = (s & 15) == 0 ? 1.0f : (float)(1.0 + (sBoost - 1.0) * k);
    }
}

/* the layer's art on the GPU (again only when it changed) */
static bool Upload(struct Layer *layer, const struct UiArtLayer *art)
{
    size_t size = (size_t)art->w * (size_t)art->h;
    GLint unpack = 4;

    if (layer->texture != 0 && layer->w == art->w && layer->h == art->h && layer->background == art->background
     && memcmp(layer->pixels, art->slots, size) == 0)
        return true;
    if (layer->w != art->w || layer->h != art->h || layer->pixels == NULL)
    {
        free(layer->pixels);
        layer->pixels = malloc(size);
        if (layer->pixels == NULL)
        {
            layer->w = layer->h = 0;
            return false;
        }
    }
    memcpy(layer->pixels, art->slots, size);
    layer->w = art->w;
    layer->h = art->h;
    layer->background = art->background;
    Boosts(layer);
    if (layer->texture == 0)
    {
        glGenTextures(1, &layer->texture);
        glBindTexture(GL_TEXTURE_2D, layer->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
        glBindTexture(GL_TEXTURE_2D, layer->texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, art->w, art->h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, layer->pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    return true;
}

static bool PowerOfTwo(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

bool UiArt_Draw(int id, const struct UiArtLayer *art, int outputWidth, int outputHeight, int px, int py, int k,
                bool carry)
{
    struct Layer *layer;
    float lines[80 * 4], boost[256];
    GLint program = 0, vertexArray = 0, activeTexture = 0, texture0 = 0, texture1 = 0, viewport[4], scissorBox[4];
    GLint srcRgb = GL_ONE, dstRgb = GL_ZERO;
    GLboolean blend, scissor, depth, cull, stencil;
    int x0, y0, x1, y1;

    if (!UiArt_Ready() || id < 0 || id >= UI_ART_LAYER_COUNT || art == NULL || art->slots == NULL || k <= 0
     || !PowerOfTwo(art->w) || !PowerOfTwo(art->h) || art->w > MAX_SIDE || art->h > MAX_SIDE
     || outputWidth <= 0 || outputHeight <= 0)
        return false;
    /* where it may draw: the whole window over the 3D field, else the picture; a sprite only round itself (the
     * smoothing reaches two art pixels past it) */
    x0 = carry ? 0 : px;
    y0 = carry ? 0 : py;
    x1 = carry ? outputWidth : px + 240 * k;
    y1 = carry ? outputHeight : py + 160 * k;
    if (!art->background)
    {
        x0 = SDL_max(x0, px + (art->originX - 2) * k);
        y0 = SDL_max(y0, py + (art->originY - 2) * k);
        x1 = SDL_min(x1, px + (art->originX + art->w + 2) * k);
        y1 = SDL_min(y1, py + (art->originY + art->h + 2) * k);
    }
    x0 = SDL_max(x0, 0);
    y0 = SDL_max(y0, 0);
    x1 = SDL_min(x1, outputWidth);
    y1 = SDL_min(y1, outputHeight);
    if (x0 >= x1 || y0 >= y1)
        return true;

    /* the state the rest of the frame expects, given back at the end */
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    blend = glIsEnabled(GL_BLEND);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    depth = glIsEnabled(GL_DEPTH_TEST);
    cull = glIsEnabled(GL_CULL_FACE);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    dglActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
    dglActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);

    layer = &sLayers[id];
    if (!Upload(layer, art))
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint)texture0);
        dglActiveTexture((GLenum)activeTexture);
        return false;
    }
    if (sPaletteDirty)
    {
        glBindTexture(GL_TEXTURE_2D, sPalTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RGBA, GL_UNSIGNED_BYTE, sPalette);
        sPaletteDirty = false;
    }
    for (int i = 0; i < 160; i++)
    {
        lines[i * 2] = art->scrollX != NULL ? (float)art->scrollX[i] : 0.0f;
        lines[i * 2 + 1] = art->scrollY != NULL ? (float)art->scrollY[i] : 0.0f;
    }
    memcpy(boost, layer->boost, sizeof(boost));

    glViewport(0, 0, outputWidth, outputHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, outputHeight - y1, x1 - x0, y1 - y0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer->texture);
    dglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sPalTexture);
    dglUniform1i(sUniforms[U_SLOTS], 0);
    dglUniform1i(sUniforms[U_PAL], 1);
    dglUniform1f(sUniforms[U_SCREEN_H], (float)outputHeight);
    dglUniform3f(sUniforms[U_PIC], (float)px, (float)py, (float)k);
    dglUniform1i(sUniforms[U_BACKGROUND], art->background ? 1 : 0);
    dglUniform1i(sUniforms[U_CARRY], art->background && carry ? 1 : 0);
    sUniform2i(sUniforms[U_SIZE], art->w, art->h);
    dglUniform2f(sUniforms[U_ORIGIN], (float)art->originX, (float)art->originY);
    dglUniform1i(sUniforms[U_PAL_BASE], art->palBase);
    dglUniform4fv(sUniforms[U_LINES], 80, lines);
    dglUniform4fv(sUniforms[U_BOOST], 64, boost);
    dglUniform1i(sUniforms[U_MODE], (int)art->blend);
    dglUniform3f(sUniforms[U_EFFECT], SDL_min(art->eva, 16) / 16.0f, SDL_min(art->evb, 16) / 16.0f,
                 SDL_min(art->y, 16) / 16.0f);
    dglUniform1f(sUniforms[U_SIGMA], sSigma);
    /* the two strongest slots blended over one window pixel: at an edge their weights part by 0.8 / sigma an art
     * pixel */
    dglUniform1f(sUniforms[U_BAND], 2.5f * sSigma * (float)k);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    dglBindVertexArray((GLuint)vertexArray);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture1);
    dglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture0);
    dglActiveTexture((GLenum)activeTexture);
    dglUseProgram((GLuint)program);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    glBlendFunc((GLenum)srcRgb, (GLenum)dstRgb); /* as UiDraw_End gives it back (1.4's separate one is not loaded) */
    if (!blend)
        glDisable(GL_BLEND);
    if (!scissor)
        glDisable(GL_SCISSOR_TEST);
    if (depth)
        glEnable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
    if (stencil)
        glEnable(GL_STENCIL_TEST);
    return true;
}

void UiArt_Shutdown(void)
{
    for (int i = 0; i < UI_ART_LAYER_COUNT; i++)
    {
        if (sLayers[i].texture != 0)
            glDeleteTextures(1, &sLayers[i].texture);
        free(sLayers[i].pixels);
        memset(&sLayers[i], 0, sizeof(sLayers[i]));
    }
    if (sReady)
    {
        glDeleteTextures(1, &sPalTexture);
        dglDeleteVertexArrays(1, &sVertexArray);
        dglDeleteProgram(sProgram);
    }
    sPalTexture = sVertexArray = sProgram = 0;
    sReady = sTried = sOff = false;
    memset(sPalette, 0, sizeof(sPalette));
    sPaletteDirty = false;
}
#endif // ENABLE_NATIVE_UI
