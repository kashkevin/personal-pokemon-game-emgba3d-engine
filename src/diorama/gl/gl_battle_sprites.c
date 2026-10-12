#if defined(ENABLE_DIORAMA) && defined(ENABLE_BATTLE_SPRITES)

/*
 * The animated battle sprites above the 3D battle's picture: include/diorama/gl_battle_sprites.h.
 * Project-owned. Each frame listed by the sprite integration (gBattleSpriteOverflow) is drawn as
 * a textured quad at the picture's scale - its position, flips or affine matrix as the GBA
 * picture draws it, its colours as faded there - cut to the rows its battler covers at rest above
 * the picture and to the picture's columns.
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

static struct
{
    bool tried, ready;
    GLuint program, vao, vbo, textures[BATTLE_SPRITE_OVERFLOW_MAX];
    GLint outLocation, texLocation, alphaLocation;
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
    dglGenVertexArrays(1, &sGL.vao);
    dglGenBuffers(1, &sGL.vbo);
    dglBindVertexArray(sGL.vao);
    dglBindBuffer(GL_ARRAY_BUFFER, sGL.vbo);
    dglBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    dglBindVertexArray(0);
    glGenTextures(BATTLE_SPRITE_OVERFLOW_MAX, sGL.textures);
    for (i = 0; i < BATTLE_SPRITE_OVERFLOW_MAX; i++)
    {
        glBindTexture(GL_TEXTURE_2D, sGL.textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
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
    GLint blendSrc, blendDst, program, vao, texture, activeTexture;
    unsigned i;

    if (gBattleSpriteOverflowCount == 0 || gameY <= 0.0f || scale <= 0.0f)
        return;
    if (!sGL.tried)
    {
        Init();
        SDL_Log("Battle sprites: drawing %u frame(s) above the picture (%s)", gBattleSpriteOverflowCount,
                sGL.ready ? "ready" : "GL setup failed");
    }
    if (!sGL.ready)
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
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    dglActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

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
    dglBindVertexArray(sGL.vao);
    dglBindBuffer(GL_ARRAY_BUFFER, sGL.vbo);

    for (i = 0; i < gBattleSpriteOverflowCount; i++)
    {
        const struct BattleSpriteOverflow *o = &gBattleSpriteOverflow[i];
        const struct BattleSpriteView *view = o->view;
        float corners[4][2], quad[16];
        float top, bottom;
        int k, sx0, sy0, sx1, sy1;

        if (view == NULL || view->width <= 0 || view->height <= 0
         || (size_t)view->width * view->height > MAX_FRAME_PIXELS)
            continue;
        if (o->affine && (float)o->pa * o->pd - (float)o->pb * o->pc == 0.0f)
            continue;
        /* rows from where its battler reaches at rest to the picture's top; the picture's columns */
        top = gameY + o->restTop * scale;
        bottom = gameY;
        if (top < 0.0f)
            top = 0.0f;
        if (bottom <= top)
            continue;
        sx0 = (int)(gameX + 0.5f);
        sx1 = (int)(gameX + 240.0f * scale + 0.5f);
        sy0 = (int)(top + 0.5f);
        sy1 = (int)(bottom + 0.5f);
        if (sx0 < 0) sx0 = 0;
        if (sx1 > outW) sx1 = outW;
        if (sx1 <= sx0 || sy1 <= sy0)
            continue;
        glScissor(sx0, outH - sy1, sx1 - sx0, sy1 - sy0);

        Expand(o);
        glBindTexture(GL_TEXTURE_2D, sGL.textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, view->width, view->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sRgba);

        /* corners: top-left, top-right, bottom-right, bottom-left of the frame */
        ToScreen(o, view->left, view->top, &corners[0][0], &corners[0][1]);
        ToScreen(o, view->left + view->width, view->top, &corners[1][0], &corners[1][1]);
        ToScreen(o, view->left + view->width, view->top + view->height, &corners[2][0], &corners[2][1]);
        ToScreen(o, view->left, view->top + view->height, &corners[3][0], &corners[3][1]);
        for (k = 0; k < 4; k++)
        {
            static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

            quad[k * 4 + 0] = gameX + corners[k][0] * scale;
            quad[k * 4 + 1] = gameY + corners[k][1] * scale;
            quad[k * 4 + 2] = uv[k][0];
            quad[k * 4 + 3] = uv[k][1];
        }
        dglBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        dglUniform1f(sGL.alphaLocation, o->alpha / 16.0f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    dglBindVertexArray((GLuint)vao);
    dglUseProgram((GLuint)program);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    dglActiveTexture((GLenum)activeTexture);
    glBlendFunc((GLenum)blendSrc, (GLenum)blendDst);
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (!scissor) glDisable(GL_SCISSOR_TEST);
}

#endif
