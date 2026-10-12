#if defined(ENABLE_DIORAMA) && defined(ENABLE_BATTLE_SPRITES)

/*
 * The animated battle sprites above the 3D battle's picture: include/diorama/gl_battle_sprites.h.
 * Project-owned. Each frame listed by the sprite integration (gBattleSpriteOverflow) is drawn as
 * a textured quad at the picture's scale - its position, flips or affine matrix as the GBA
 * picture draws it, its colours as faded there - cut to the box its battler covers at rest,
 * outside the picture: above it, and beside it above the text box's rows.
 */

#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_battle_sprites.h"
#include "platform/battle_sprites.h"

#define MAX_FRAME_PIXELS (256 * 256)

static const char *sVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"   /* window pixels, top-left origin */
    "layout(location = 1) in vec2 aUv;\n"
    "uniform vec2 uOut;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vUv = aUv;\n"
    "    gl_Position = vec4(aPos.x / uOut.x * 2.0 - 1.0, 1.0 - aPos.y / uOut.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char *sFragmentShader =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha;\n"
    "out vec4 oColor;\n"
    "void main() {\n"
    "    vec4 c = texture(uTex, vUv);\n"
    "    if (c.a < 0.5) discard;\n"
    "    oColor = vec4(c.rgb, uAlpha);\n"
    "}\n";

/* Buffers and textures are written in turn from RING sets, so a write never touches one that a
 * frame still being drawn reads: a driver may wait for that frame first (Mesa's llvmpipe does, for
 * a buffer or texture rewritten at the same size: this pass then waited 24 ms a frame for the
 * world's drawing in a 1834x1032 window, 1.1 ms with the rings). */
#define RING 4

static struct
{
    bool tried, ready;
    GLuint program, vao[RING], vbo[RING], textures[RING][BATTLE_SPRITE_OVERFLOW_MAX];
    GLint outLocation, texLocation, alphaLocation;
    unsigned vertexSet, textureSet;
    /* the picture whose frames the current texture set holds: drawn more often than the game's
     * 60 pictures a second (the frame-rate limit), a frame is uploaded once */
    uint32_t textureSerial;
    bool textureSetValid;
} sGL;

static uint32_t sRgba[MAX_FRAME_PIXELS];

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint compiled;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[1024];

        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("Battle sprites: shader compile failed: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool Init(void)
{
    GLuint vs, fs;
    GLint linked;
    unsigned i;

    sGL.tried = true;
    vs = Compile(GL_VERTEX_SHADER, sVertexShader);
    fs = Compile(GL_FRAGMENT_SHADER, sFragmentShader);
    if (vs == 0 || fs == 0)
        return false;
    sGL.program = dglCreateProgram();
    dglAttachShader(sGL.program, vs);
    dglAttachShader(sGL.program, fs);
    dglLinkProgram(sGL.program);
    dglDeleteShader(vs);
    dglDeleteShader(fs);
    dglGetProgramiv(sGL.program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        SDL_Log("Battle sprites: shader link failed");
        return false;
    }
    sGL.outLocation = dglGetUniformLocation(sGL.program, "uOut");
    sGL.texLocation = dglGetUniformLocation(sGL.program, "uTex");
    sGL.alphaLocation = dglGetUniformLocation(sGL.program, "uAlpha");
    dglGenVertexArrays(RING, sGL.vao);
    dglGenBuffers(RING, sGL.vbo);
    for (i = 0; i < RING; i++)
    {
        unsigned k;

        dglBindVertexArray(sGL.vao[i]);
        dglBindBuffer(GL_ARRAY_BUFFER, sGL.vbo[i]);
        dglBufferData(GL_ARRAY_BUFFER, BATTLE_SPRITE_OVERFLOW_MAX * 16 * sizeof(float), NULL, GL_STREAM_DRAW);
        dglEnableVertexAttribArray(0);
        dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        dglEnableVertexAttribArray(1);
        dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glGenTextures(BATTLE_SPRITE_OVERFLOW_MAX, sGL.textures[i]);
        for (k = 0; k < BATTLE_SPRITE_OVERFLOW_MAX; k++)
        {
            glBindTexture(GL_TEXTURE_2D, sGL.textures[i][k]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    dglBindVertexArray(0);
    sGL.ready = true;
    return true;
}

/* The frame in its colours (RGB555 -> RGBA8, index 0 see-through), with the frame's sprite
 * brightness change as gba_easy_draw.c applies it. */
static void Expand(const struct BattleSpriteOverflow *o)
{
    const struct BattleSpriteView *view = o->view;
    int x, y;

    for (y = 0; y < view->height; y++)
    {
        const uint8_t *row = view->pixels + (size_t)y * view->stride;

        for (x = 0; x < view->width; x++)
        {
            uint8_t index = row[x];
            uint16_t c;
            int rgb[3], k;

            if (index == 0)
            {
                sRgba[y * view->width + x] = 0;
                continue;
            }
            c = view->colors[index];
            for (k = 0; k < 3; k++)
            {
                rgb[k] = (c >> (5 * k)) & 31;
                if (o->brightness > 0)
                    rgb[k] += ((31 - rgb[k]) * o->brightness) >> 4;
                else if (o->brightness < 0)
                    rgb[k] -= (rgb[k] * -o->brightness) >> 4;
                rgb[k] = rgb[k] * 255 / 31;
            }
            /* bytes R, G, B, A */
            sRgba[y * view->width + x] = (uint32_t)rgb[0] | ((uint32_t)rgb[1] << 8) | ((uint32_t)rgb[2] << 16)
                                        | 0xFF000000u;
        }
    }
}

static void Rect(int *rect, int x0, int y0, int x1, int y1)
{
    rect[0] = x0;
    rect[1] = y0;
    rect[2] = x1;
    rect[3] = y1;
}

/* A point of the frame (texture-space units of the GBA box) on the screen, in GBA pixels. */
static void ToScreen(const struct BattleSpriteOverflow *o, float tx, float ty, float *sx, float *sy)
{
    if (o->affine)
    {
        float det = (float)o->pa * o->pd - (float)o->pb * o->pc;
        float dx = tx - 32.0f, dy = ty - 32.0f;

        *sx = o->x + (o->pd * dx - o->pb * dy) * 256.0f / det;
        *sy = o->y + (-o->pc * dx + o->pa * dy) * 256.0f / det;
    }
    else
    {
        *sx = o->x + (o->flipX ? 64.0f - tx : tx);
        *sy = o->y + (o->flipY ? 64.0f - ty : ty);
    }
}

void DioramaGLBattleSprites_Draw(int outW, int outH, float gameX, float gameY, float scale)
{
    GLboolean blend, depth, stencil, scissor, cull;
    GLint blendSrc, blendDst, program, vao, buffer, texture, activeTexture, viewport[4];
    /* per frame drawn: its quad (4 vertices: window x, y, texture u, v), its cut-outs */
    static float sQuads[BATTLE_SPRITE_OVERFLOW_MAX][16];
    int rects[BATTLE_SPRITE_OVERFLOW_MAX][3][4], counts[BATTLE_SPRITE_OVERFLOW_MAX];
    unsigned entries[BATTLE_SPRITE_OVERFLOW_MAX], n = 0, i, j;
    bool upload;

    uint64_t t0 = SDL_GetPerformanceCounter();
    static uint64_t sSum, sMax;
    static uint32_t sDraws;

    if (gBattleSpriteOverflowCount == 0 || scale <= 0.0f
     || (gameY <= 0.0f && gameX <= 0.0f && gameX + 240.0f * scale >= outW))
        return;
    if (!sGL.tried)
    {
        Init();
        SDL_Log("Battle sprites: drawing %u frame(s) above the picture (%s)", gBattleSpriteOverflowCount,
                sGL.ready ? "ready" : "GL setup failed");
    }
    if (!sGL.ready)
        return;

    /* the frames to draw, their quads and where they may be drawn */
    for (i = 0; i < gBattleSpriteOverflowCount && i < BATTLE_SPRITE_OVERFLOW_MAX; i++)
    {
        const struct BattleSpriteOverflow *o = &gBattleSpriteOverflow[i];
        const struct BattleSpriteView *view = o->view;
        float corners[4][2];
        int k, count = 0;

        if (view == NULL || view->width <= 0 || view->height <= 0
         || (size_t)view->width * view->height > MAX_FRAME_PIXELS)
            continue;
        if (o->affine && (float)o->pa * o->pd - (float)o->pb * o->pc == 0.0f)
            continue;
        /* Where it may be drawn, in GBA pixels: its battler's box at rest, outside the picture -
         * above it, and beside it above the text box's rows (112 on; the text box is carried
         * out to the sides and stays in front). */
        if (o->restTop < 0)
            Rect(rects[n][count++], o->restLeft, o->restTop, o->restRight, 0);
        if (o->restLeft < 0)
            Rect(rects[n][count++], o->restLeft, 0, 0, o->restBottom < 112 ? o->restBottom : 112);
        if (o->restRight > 240)
            Rect(rects[n][count++], 240, 0, o->restRight, o->restBottom < 112 ? o->restBottom : 112);
        if (count == 0)
            continue;

        /* corners: top-left, top-right, bottom-right, bottom-left of the frame */
        ToScreen(o, view->left, view->top, &corners[0][0], &corners[0][1]);
        ToScreen(o, view->left + view->width, view->top, &corners[1][0], &corners[1][1]);
        ToScreen(o, view->left + view->width, view->top + view->height, &corners[2][0], &corners[2][1]);
        ToScreen(o, view->left, view->top + view->height, &corners[3][0], &corners[3][1]);
        for (k = 0; k < 4; k++)
        {
            static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

            sQuads[n][k * 4 + 0] = gameX + corners[k][0] * scale;
            sQuads[n][k * 4 + 1] = gameY + corners[k][1] * scale;
            sQuads[n][k * 4 + 2] = uv[k][0];
            sQuads[n][k * 4 + 3] = uv[k][1];
        }
        counts[n] = count;
        entries[n++] = i;
    }
    if (n == 0)
        return;

    blend = glIsEnabled(GL_BLEND);
    depth = glIsEnabled(GL_DEPTH_TEST);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    cull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDst);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    dglActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

    /* a new game picture: its frames into the next texture set (one upload per picture) */
    upload = !sGL.textureSetValid || sGL.textureSerial != gBattleSpriteOverflowSerial;
    if (upload)
    {
        sGL.textureSet = (sGL.textureSet + 1) % RING;
        sGL.textureSerial = gBattleSpriteOverflowSerial;
        sGL.textureSetValid = true;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        for (j = 0; j < n; j++)
        {
            const struct BattleSpriteOverflow *o = &gBattleSpriteOverflow[entries[j]];

            Expand(o);
            glBindTexture(GL_TEXTURE_2D, sGL.textures[sGL.textureSet][j]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, o->view->width, o->view->height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, sRgba);
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, outW, outH);
    dglUseProgram(sGL.program);
    dglUniform2f(sGL.outLocation, (float)outW, (float)outH);
    dglUniform1i(sGL.texLocation, 0);
    /* every quad in one write, into the next buffer of the ring */
    sGL.vertexSet = (sGL.vertexSet + 1) % RING;
    dglBindVertexArray(sGL.vao[sGL.vertexSet]);
    dglBindBuffer(GL_ARRAY_BUFFER, sGL.vbo[sGL.vertexSet]);
    dglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(sQuads[0])), sQuads, GL_STREAM_DRAW);

    for (j = 0; j < n; j++)
    {
        const struct BattleSpriteOverflow *o = &gBattleSpriteOverflow[entries[j]];
        int r;

        glBindTexture(GL_TEXTURE_2D, sGL.textures[sGL.textureSet][j]);
        dglUniform1f(sGL.alphaLocation, o->alpha / 16.0f);
        for (r = 0; r < counts[j]; r++)
        {
            /* GBA pixels -> window pixels, cut to the window; GL's rows run bottom up */
            int x0 = (int)(gameX + rects[j][r][0] * scale + 0.5f), y0 = (int)(gameY + rects[j][r][1] * scale + 0.5f);
            int x1 = (int)(gameX + rects[j][r][2] * scale + 0.5f), y1 = (int)(gameY + rects[j][r][3] * scale + 0.5f);

            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > outW) x1 = outW;
            if (y1 > outH) y1 = outH;
            if (x1 <= x0 || y1 <= y0)
                continue;
            glScissor(x0, outH - y1, x1 - x0, y1 - y0);
            glDrawArrays(GL_TRIANGLE_FAN, (GLint)(j * 4), 4);
        }
    }

    dglBindVertexArray((GLuint)vao);
    dglBindBuffer(GL_ARRAY_BUFFER, (GLuint)buffer);
    dglUseProgram((GLuint)program);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    dglActiveTexture((GLenum)activeTexture);
    glBlendFunc((GLenum)blendSrc, (GLenum)blendDst);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (!scissor) glDisable(GL_SCISSOR_TEST);

    /* its cost on the CPU side (PKMN_BATTLE_SPRITES_LOG), every 600 draws */
    {
        uint64_t dt = SDL_GetPerformanceCounter() - t0;
        double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
        const char *log = SDL_getenv("PKMN_BATTLE_SPRITES_LOG");

        sSum += dt;
        if (dt > sMax)
            sMax = dt;
        if (++sDraws >= 600)
        {
            if (log != NULL && log[0] == '1')
                SDL_Log("Battle sprites: above the picture %.3f ms a draw (at most %.3f)", sSum * ms / sDraws,
                        sMax * ms);
            sSum = sMax = 0;
            sDraws = 0;
        }
    }
}

#endif
