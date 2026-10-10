#ifdef ENABLE_DIORAMA
/*
 * The PC's own interface over the game: see diorama/gl_pc_overlay.h.
 * Project-owned. Drawn the way the UI art direction asks (the UI session's
 * artifacts/ui-art-direction/docs/ART_DIRECTION.md, version 2): the engine's
 * own bitmap fonts with their built-in shadow, light panels with a 1-pixel
 * outline and palette bevels, stepped corners, the Emerald green family, the
 * red cursor arrow, key caps in the original Route Caps face, on a 640x360
 * logical canvas that is drawn on the CPU and shown once with a whole-number
 * scale and nearest-neighbour sampling (x2 at 720p, x3 at 1080p, x4 at 1440p,
 * x6 at 4K). Colours are that guide's tokens (its tools/pkui/palette.py).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "gba/types.h"
#include "fonts.h"
#include "diorama/gl_loader.h"
#include "diorama/gl_pc_overlay.h"

#define CANVAS_WIDTH 640
#define CANVAS_HEIGHT 360
#define CANVAS_MAX_WIDTH 1920   /* a 1x canvas on an 8K-wide window */
#define CANVAS_MAX_HEIGHT 1080
#define GLYPH_CELL 16
#define LINE_HEIGHT 16

/* Colours: 0xAABBGGRR, the byte order GL_RGBA reads on a little-endian PC.
 * (Windows' wingdi.h has an RGB of its own, unused here.) */
#undef RGB
#define RGB(hex) (0xFF000000u | (((hex) & 0xFFu) << 16) | ((hex) & 0xFF00u) | (((hex) >> 16) & 0xFFu))
#define CLEAR_PIXEL 0u

enum
{
    TEXT = RGB(0x383838),
    TEXT_SH = RGB(0xD8D8D8),
    TEXT_W = RGB(0xF8F8F8),
    TEXT_MUTED = RGB(0x707070),
    PAPER = RGB(0xF8F8F8),
    PAPER_LO = RGB(0xD8D8D0),
    CREAM = RGB(0xF8F0D8),
    CREAM_LO = RGB(0xE0D0A8),
    CREAM_HI = RGB(0xFFFAEA),
    CREAM_STRIPE = RGB(0xF2E6C6),
    SEL = RGB(0xF87028),
    SEL_LO = RGB(0xC04010),
    CURSOR = RGB(0xE83820),
    CURSOR_SH = RGB(0xF8B0A0),
    FIELD_HI = RGB(0x98E0A8),
    FIELD_BASE = RGB(0x48B070),
    FIELD_LO = RGB(0x2F8A58),
    FIELD_INK = RGB(0x1C4430),
    KEY_HI = RGB(0xFFFFFF),
    KEY_BASE = RGB(0xE8E8E0),
    KEY_LO = RGB(0xB8B8B0),
    KEY_INK = RGB(0x383840),
};

enum { FONT_NORMAL, FONT_SMALL, FONT_COUNT };

/* Where the canvas is shown: its logical size, the whole-number scale and the
 * window pixel its top left lands on. */
struct CanvasView
{
    int width;
    int height;
    int scale;
    int originX;
    int originY;
};

static const char *const sVertexSource =
    "#version 330 core\n"
    "layout(location = 0) in vec2 position;\n" /* window pixels from the top left */
    "layout(location = 1) in vec2 uv;\n"
    "uniform vec2 screen;\n"
    "out vec2 vUv;\n"
    "void main()\n"
    "{\n"
    "  vUv = uv;\n"
    "  gl_Position = vec4(position.x * 2.0 / screen.x - 1.0, 1.0 - position.y * 2.0 / screen.y, 0.0, 1.0);\n"
    "}\n";

static const char *const sFragmentSource =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "uniform sampler2D canvas;\n"
    "out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "  vec4 texel = texture(canvas, vUv);\n"
    "  if (texel.a < 0.5) discard;\n"
    "  fragColor = texel;\n"
    "}\n";

static bool sTried;
static bool sReady;
static GLuint sProgram;
static GLuint sVertexArray;
static GLuint sVertexBuffer;
static GLuint sTexture;
static int sTextureWidth;
static int sTextureHeight;
static GLint sScreenLocation;
static GLint sCanvasLocation;
static uint32_t *sPixels;
static int sWidth;   /* the canvas being drawn */
static int sHeight;
/* 1 ink, 2 shadow; 3 is the glyph's own background within its width and 0
 * the cell past it, both left clear here. */
static uint8_t sGlyphs[FONT_COUNT][256][GLYPH_CELL][GLYPH_CELL];
static bool sGlyphsDecoded;

/* ---------------------------------------------------------------- fonts */

/*
 * The engine's latin fonts (src/fonts.c): 0x20 halfwords a glyph, four 8x8
 * tiles (top left, top right, bottom left, bottom right) of 2 bits a pixel,
 * the first pixel in a row's top bits - as text.c's DecompressGlyphTile reads
 * them through its half-row table.
 */
static void UiDecodeFont(int font, const u16 *glyphs)
{
    for (int code = 0; code < 256; code++)
    {
        const u16 *glyph = glyphs + 0x20 * code;

        for (int tile = 0; tile < 4; tile++)
        {
            int ox = (tile & 1) * 8;
            int oy = (tile >> 1) * 8;

            for (int row = 0; row < 8; row++)
            {
                u16 bits = glyph[tile * 8 + row];

                for (int px = 0; px < 8; px++)
                    sGlyphs[font][code][oy + row][ox + px] = (uint8_t)((bits >> (14 - 2 * px)) & 3);
            }
        }
    }
}

static const u8 *UiFontWidths(int font)
{
    return font == FONT_SMALL ? gFontSmallLatinGlyphWidths : gFontNormalLatinGlyphWidths;
}

/* The engine's code for an ASCII character (charmap.txt), 0 (space) when it
 * has none. An 'x' between digits is the multiplication sign (1920x1080). */
static unsigned UiEngineCode(const char *text, int i)
{
    unsigned char c = (unsigned char)text[i];

    if (c >= 'A' && c <= 'Z')
        return 0xBB + (c - 'A');
    if (c >= 'a' && c <= 'z')
    {
        if (c == 'x' && i > 0 && text[i - 1] >= '0' && text[i - 1] <= '9'
         && text[i + 1] >= '0' && text[i + 1] <= '9')
            return 0xB9;
        return 0xD5 + (c - 'a');
    }
    if (c >= '0' && c <= '9')
        return 0xA1 + (c - '0');
    switch (c)
    {
    case '&': return 0x2D;
    case '+': return 0x2E;
    case '=': return 0x35;
    case ';': return 0x36;
    case '%': return 0x5B;
    case '(': return 0x5C;
    case ')': return 0x5D;
    case '<': return 0x85;
    case '>': return 0x86;
    case '!': return 0xAB;
    case '?': return 0xAC;
    case '.': return 0xAD;
    case '-': return 0xAE;
    case ',': return 0xB8;
    case '/': return 0xBA;
    case '\'': return 0xB4;
    case ':': return 0xF0;
    default: return 0x00;
    }
}

static int UiTextWidth(int font, const char *text)
{
    const u8 *widths = UiFontWidths(font);
    int width = 0;

    for (int i = 0; text[i] != '\0'; i++)
        width += widths[UiEngineCode(text, i)];
    return width;
}

/*
 * Route Caps: the UI direction's original 5x7 bold capitals (its
 * tools/pkui/fonts.py _CAPS), for the title tab, key caps and small readouts.
 * '{' '}' '^' '_' stand for the left, right, up and down triangles, '*' for x.
 */
struct CapsGlyph
{
    char ch;
    const char *rows[7];
};

static const struct CapsGlyph sCaps[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {'/', {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}},
    {'.', {"..", "..", "..", "..", "..", "##", "##"}},
    {'-', {"...", "...", "...", "###", "...", "...", "..."}},
    {'=', {"...", "...", "###", "...", "###", "...", "..."}},
    {',', {"..", "..", "..", "..", "##", "##", "#."}},
    {'<', {"...#", "..#.", ".#..", "#...", ".#..", "..#.", "...#"}},
    {'>', {"#...", ".#..", "..#.", "...#", "..#.", ".#..", "#..."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {':', {".", ".", "#", ".", ".", "#", "."}},
    {'%', {"##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'(', {".#", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", ".#", ".#", "#."}},
    {'*', {".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"}},
    {'}', {"#...", "##..", "###.", "####", "###.", "##..", "#..."}},
    {'{', {"...#", "..##", ".###", "####", ".###", "..##", "...#"}},
    {'^', {".......", ".......", "...#...", "..###..", ".#####.", "#######", "......."}},
    {'_', {".......", "#######", ".#####.", "..###..", "...#...", ".......", "......."}},
    {' ', {"...", "...", "...", "...", "...", "...", "..."}},
};

static const struct CapsGlyph *UiCapsGlyphFor(char c)
{
    for (size_t i = 0; i < SDL_arraysize(sCaps); i++)
        if (sCaps[i].ch == c)
            return &sCaps[i];
    return &sCaps[SDL_arraysize(sCaps) - 1];
}

static int UiCapsWidth(const char *text)
{
    int width = 0;

    for (; *text != '\0'; text++)
        width += (int)strlen(UiCapsGlyphFor(*text)->rows[0]) + 1;
    return width > 0 ? width - 1 : 0;
}

/* ---------------------------------------------------------------- canvas */

static void UiPlot(int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < sWidth && y < sHeight)
        sPixels[y * sWidth + x] = color;
}

static void UiFillRect(int x, int y, int width, int height, uint32_t color)
{
    for (int yy = y; yy < y + height; yy++)
        for (int xx = x; xx < x + width; xx++)
            UiPlot(xx, yy, color);
}

/*
 * A rectangle with hand-stepped corners (the guide's mask_rounded and
 * corner_profile): each corner's profile is how far in each of its rows
 * starts, from the corner inwards.
 */
struct Shape
{
    int width;
    int height;
    const int8_t *corner[4]; /* top left, top right, bottom left, bottom right; NULL: square */
    int cornerRows[4];
};

static const int8_t sCorner1[] = {1};
static const int8_t sCorner2[] = {2, 1};
static const int8_t sCorner3[] = {3, 1, 1};
static const int8_t sChamfer6[] = {6, 5, 4, 3, 2, 1};

static void UiShapeCorners(struct Shape *shape, const int8_t *profile, int rows)
{
    for (int i = 0; i < 4; i++)
    {
        shape->corner[i] = profile;
        shape->cornerRows[i] = rows;
    }
}

static bool UiInShape(const struct Shape *shape, int x, int y)
{
    if (x < 0 || y < 0 || x >= shape->width || y >= shape->height)
        return false;
    for (int i = 0; i < 4; i++)
    {
        int row = (i < 2) ? y : shape->height - 1 - y;
        int inset;

        if (shape->corner[i] == NULL || row >= shape->cornerRows[i])
            continue;
        inset = shape->corner[i][row];
        if ((i & 1) == 0 ? x < inset : x >= shape->width - inset)
            return false;
    }
    return true;
}

static bool UiInInner(const struct Shape *shape, int x, int y)
{
    return UiInShape(shape, x, y) && UiInShape(shape, x - 1, y) && UiInShape(shape, x + 1, y)
        && UiInShape(shape, x, y - 1) && UiInShape(shape, x, y + 1);
}

/* The guide's paint(): a 1 px outline in the ink, then the body with a
 * highlight on its top and left inner edges and a shade on its bottom and
 * right ones (loWidth rows of shade). A colour of 0 is left out. */
static void UiPaint(const struct Shape *shape, int x, int y, uint32_t body, uint32_t ink,
                  uint32_t hi, uint32_t lo, int loWidth)
{
    for (int yy = 0; yy < shape->height; yy++)
        for (int xx = 0; xx < shape->width; xx++)
        {
            uint32_t color;

            if (!UiInShape(shape, xx, yy))
                continue;
            if (ink != 0 && !UiInInner(shape, xx, yy))
                color = ink;
            else
            {
                bool (*inner)(const struct Shape *, int, int) = ink != 0 ? UiInInner : UiInShape;

                color = body;
                if (hi != 0 && (!inner(shape, xx, yy - 1) || !inner(shape, xx - 1, yy)))
                    color = hi;
                for (int k = 1; lo != 0 && k <= loWidth; k++)
                    if (!inner(shape, xx, yy + k) || !inner(shape, xx + k, yy))
                        color = lo;
            }
            UiPlot(x + xx, y + yy, color);
        }
}

/* Engine text with its own shadow (0: none). Returns the right edge. */
static int UiText(int font, int x, int y, const char *text, uint32_t ink, uint32_t shadow)
{
    const u8 *widths = UiFontWidths(font);

    for (int i = 0; text[i] != '\0'; i++)
    {
        unsigned code = UiEngineCode(text, i);

        for (int yy = 0; yy < GLYPH_CELL; yy++)
            for (int xx = 0; xx < GLYPH_CELL; xx++)
            {
                uint8_t value = sGlyphs[font][code][yy][xx];

                if (value == 1)
                    UiPlot(x + xx, y + yy, ink);
                else if (value == 2 && shadow != 0)
                    UiPlot(x + xx, y + yy, shadow);
            }
        x += widths[code];
    }
    return x;
}

static void UiTextRight(int font, int right, int y, const char *text, uint32_t ink, uint32_t shadow)
{
    UiText(font, right - UiTextWidth(font, text), y, text, ink, shadow);
}

/* One engine glyph by its code (the cursor arrow). */
static void UiGlyph(int font, int x, int y, unsigned code, uint32_t ink, uint32_t shadow)
{
    for (int yy = 0; yy < GLYPH_CELL; yy++)
        for (int xx = 0; xx < GLYPH_CELL; xx++)
        {
            uint8_t value = sGlyphs[font][code][yy][xx];

            if (value == 1)
                UiPlot(x + xx, y + yy, ink);
            else if (value == 2 && shadow != 0)
                UiPlot(x + xx, y + yy, shadow);
        }
}

/* Route Caps, plain or with a 1 px outline all round (outline != 0). */
static int UiCaps(int x, int y, const char *text, uint32_t color, uint32_t outline)
{
    for (int pass = outline != 0 ? 0 : 1; pass < 2; pass++)
    {
        int penX = x;

        for (const char *p = text; *p != '\0'; p++)
        {
            const struct CapsGlyph *glyph = UiCapsGlyphFor(*p);
            int width = (int)strlen(glyph->rows[0]);

            for (int yy = 0; yy < 7; yy++)
                for (int xx = 0; xx < width; xx++)
                {
                    if (glyph->rows[yy][xx] != '#')
                        continue;
                    if (pass == 0)
                    {
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++)
                                UiPlot(penX + xx + dx, y + yy + dy, outline);
                    }
                    else
                        UiPlot(penX + xx, y + yy, color);
                }
            penX += width + 1;
        }
        if (pass == 1)
            return penX - 1;
    }
    return x;
}

/* A key cap (the guide's keycap): returns its right edge. */
static int UiKeyCap(int x, int y, const char *label)
{
    struct Shape shape = {0};
    int width = UiCapsWidth(label) + 6;

    if (width < 11)
        width = 11;
    shape.width = width;
    shape.height = 11;
    UiShapeCorners(&shape, sCorner1, 1);
    UiPaint(&shape, x, y, KEY_BASE, KEY_INK, KEY_HI, KEY_LO, 2);
    UiCaps(x + (width - UiCapsWidth(label)) / 2, y + 2, label, KEY_INK, 0);
    return x + width;
}

/* [key][key] Label (the guide's hint): returns the right edge. */
static int UiHint(int x, int y, const char *const keys[], int keyCount, const char *label)
{
    for (int i = 0; i < keyCount; i++)
        x = UiKeyCap(x, y, keys[i]) + 2;
    return UiText(FONT_NORMAL, x + 1, y - 3, label, TEXT, TEXT_SH);
}

static int UiHintWidth(const char *const keys[], int keyCount, const char *label)
{
    int width = 0;

    for (int i = 0; i < keyCount; i++)
    {
        int cap = UiCapsWidth(keys[i]) + 6;

        width += (cap < 11 ? 11 : cap) + 2;
    }
    return width + 1 + UiTextWidth(FONT_NORMAL, label);
}

/* The Emerald message frame (the guide's dialog_box): an ink rim, a three-row
 * green band with its two notches, an inner ink line and the body. Returns
 * the body's inner rectangle. */
static void UiFieldFrame(int x, int y, int width, int height, uint32_t body, uint32_t bodyLo, int inner[4])
{
    struct Shape outer = {0};
    struct Shape core = {0};

    outer.width = width;
    outer.height = height;
    UiShapeCorners(&outer, sCorner3, 3);
    UiPaint(&outer, x, y, FIELD_BASE, FIELD_INK, FIELD_HI, FIELD_LO, 1);
    core.width = width - 8;
    core.height = height - 8;
    UiShapeCorners(&core, sCorner2, 2);
    UiPaint(&core, x + 4, y + 4, body, FIELD_INK, 0, bodyLo, 1);
    UiFillRect(x + width / 2 - 6, y + 1, 2, 2, FIELD_HI);
    UiFillRect(x + width / 2 + 4, y + 1, 2, 2, FIELD_HI);
    inner[0] = x + 5;
    inner[1] = y + 5;
    inner[2] = width - 10;
    inner[3] = height - 10;
}

/* The title plate: flat left, chamfered right, white caps with an ink outline. */
static void UiTitleTab(int x, int y, const char *label)
{
    struct Shape shape = {0};

    shape.width = UiCapsWidth(label) + 22;
    shape.height = 17;
    shape.corner[0] = sCorner1;
    shape.cornerRows[0] = 1;
    shape.corner[2] = sCorner1;
    shape.cornerRows[2] = 1;
    shape.corner[1] = sChamfer6;
    shape.cornerRows[1] = 6;
    shape.corner[3] = sChamfer6;
    shape.cornerRows[3] = 6;
    UiPaint(&shape, x, y, FIELD_BASE, FIELD_INK, FIELD_HI, FIELD_LO, 1);
    UiCaps(x + 7, y + 5, label, TEXT_W, FIELD_INK);
}

/* ---------------------------------------------------------------- GL */

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = dglCreateShader(type);
    GLint ok = 0;

    dglShaderSource(shader, 1, &source, NULL);
    dglCompileShader(shader);
    dglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];

        dglGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SDL_Log("PC overlay shader: %s", log);
        dglDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void Init(void)
{
    GLuint vertex;
    GLuint fragment;
    GLint linked = 0;

    sTried = true;
    if (!sGlyphsDecoded)
    {
        UiDecodeFont(FONT_NORMAL, gFontNormalLatinGlyphs);
        UiDecodeFont(FONT_SMALL, gFontSmallLatinGlyphs);
        sGlyphsDecoded = true;
    }
    sPixels = malloc((size_t)CANVAS_MAX_WIDTH * CANVAS_MAX_HEIGHT * sizeof(*sPixels));
    if (sPixels == NULL)
        return;
    vertex = Compile(GL_VERTEX_SHADER, sVertexSource);
    fragment = Compile(GL_FRAGMENT_SHADER, sFragmentSource);
    if (vertex == 0 || fragment == 0)
        return;
    sProgram = dglCreateProgram();
    dglAttachShader(sProgram, vertex);
    dglAttachShader(sProgram, fragment);
    dglLinkProgram(sProgram);
    dglDeleteShader(vertex);
    dglDeleteShader(fragment);
    dglGetProgramiv(sProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        SDL_Log("PC overlay: shader program did not link");
        dglDeleteProgram(sProgram);
        sProgram = 0;
        return;
    }
    sScreenLocation = dglGetUniformLocation(sProgram, "screen");
    sCanvasLocation = dglGetUniformLocation(sProgram, "canvas");
    dglGenVertexArrays(1, &sVertexArray);
    dglGenBuffers(1, &sVertexBuffer);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglEnableVertexAttribArray(0);
    dglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);
    dglEnableVertexAttribArray(1);
    dglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    sReady = true;
}

/* The canvas for this window: 640x360 logical pixels at the largest whole
 * scale that fits; a window of another shape gets a canvas longer on its long
 * side (nothing is stretched or letterboxed). */
static bool BeginCanvas(int outputWidth, int outputHeight, struct CanvasView *view)
{
    int scaleX = outputWidth / CANVAS_WIDTH;
    int scaleY = outputHeight / CANVAS_HEIGHT;

    if (!sTried)
        Init();
    if (!sReady || outputWidth <= 0 || outputHeight <= 0)
        return false;
    view->scale = scaleX < scaleY ? scaleX : scaleY;
    if (view->scale < 1)
        view->scale = 1;
    view->width = outputWidth / view->scale;
    view->height = outputHeight / view->scale;
    if (view->width > CANVAS_MAX_WIDTH)
        view->width = CANVAS_MAX_WIDTH;
    if (view->height > CANVAS_MAX_HEIGHT)
        view->height = CANVAS_MAX_HEIGHT;
    view->originX = (outputWidth - view->width * view->scale) / 2;
    view->originY = (outputHeight - view->height * view->scale) / 2;
    sWidth = view->width;
    sHeight = view->height;
    return true;
}

/* Shows the canvas once, scaled by its whole number with nearest sampling. */
static void PresentCanvas(const struct CanvasView *view, int outputWidth, int outputHeight, bool upload)
{
    float x0 = (float)view->originX;
    float y0 = (float)view->originY;
    float x1 = x0 + (float)(view->width * view->scale);
    float y1 = y0 + (float)(view->height * view->scale);
    const float vertices[] = {
        x0, y0, 0, 0,  x1, y0, 1, 0,  x1, y1, 1, 1,
        x0, y0, 0, 0,  x1, y1, 1, 1,  x0, y1, 0, 1,
    };

    if (sTexture == 0)
        glGenTextures(1, &sTexture);
    glBindTexture(GL_TEXTURE_2D, sTexture);
    if (sTextureWidth != view->width || sTextureHeight != view->height)
    {
        upload = true;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, view->width, view->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        sTextureWidth = view->width;
        sTextureHeight = view->height;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (upload)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, view->width, view->height, GL_RGBA, GL_UNSIGNED_BYTE, sPixels);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    dglUseProgram(sProgram);
    dglBindVertexArray(sVertexArray);
    dglBindBuffer(GL_ARRAY_BUFFER, sVertexBuffer);
    dglUniform2f(sScreenLocation, (float)outputWidth, (float)outputHeight);
    dglUniform1i(sCanvasLocation, 0);
    dglActiveTexture(GL_TEXTURE0);
    dglBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ---------------------------------------------------------------- screens */

/* Frames at 60 a second, for the stepped motion (the cursor's nudge). */
static unsigned FrameCount(void)
{
    return SDL_GetTicks() * 60u / 1000u;
}

/* The upper-case copy of a title for Route Caps. */
static void UiUpper(char *out, size_t size, const char *text)
{
    size_t i = 0;

    for (; text[i] != '\0' && i + 1 < size; i++)
        out[i] = (text[i] >= 'a' && text[i] <= 'z') ? (char)(text[i] - 'a' + 'A') : text[i];
    out[i] = '\0';
}

static void DrawSettingsSheet(const char *title, const struct DioramaOverlayRow *rows, int count,
                              int confirmSeconds)
{
    static const char *const sChooseKeys[] = {"^", "_"};
    static const char *const sChangeKeys[] = {"{", "}"};
    static const char *const sBackKeys[] = {"ESC"};
    static const char *const sKeepKeys[] = {"ENTER"};
    static const char *const sUndoKeys[] = {"ESC"};
    char caps[64];
    int labelWidth = 0;
    int valueWidth = 0;
    int width;
    int height;
    int x;
    int y;
    int body[4];
    int rowY;
    bool nudge = (FrameCount() / 16u) % 2u == 1u;

    for (int i = 0; i < count; i++)
    {
        int label = UiTextWidth(FONT_NORMAL, rows[i].label);
        int value = UiTextWidth(FONT_NORMAL, rows[i].value);

        if (label > labelWidth)
            labelWidth = label;
        if (value > valueWidth)
            valueWidth = value;
    }
    /* Cursor column, label, a gap, the value between its arrows. */
    width = 10 + 14 + labelWidth + 24 + 10 + valueWidth + 10 + 8;
    if (width < 300)
        width = 300;
    if (width > sWidth - 16)
        width = sWidth - 16;
    height = 10 + 6 + count * LINE_HEIGHT + 6;
    x = (sWidth - width) / 2;
    y = (sHeight - height - 26) / 2 + 6;
    if (y < 14)
        y = 14;

    UiFieldFrame(x, y, width, height, CREAM, CREAM_LO, body);
    UiUpper(caps, sizeof(caps), title);
    UiTitleTab(x + 8, y - 9, caps);

    rowY = body[1] + 6;
    for (int i = 0; i < count; i++, rowY += LINE_HEIGHT)
    {
        const struct DioramaOverlayRow *row = &rows[i];
        int right = body[0] + body[2] - 8;

        if (row->selected)
            UiFillRect(body[0] + 1, rowY, body[2] - 2, LINE_HEIGHT, CREAM_HI);
        else if (i % 2 == 1)
            UiFillRect(body[0] + 1, rowY, body[2] - 2, LINE_HEIGHT, CREAM_STRIPE);
        if (row->selected)
            UiGlyph(FONT_NORMAL, body[0] + 3 + (nudge ? 1 : 0), rowY, 0xEF, CURSOR, CURSOR_SH);
        UiText(FONT_NORMAL, body[0] + 14, rowY, row->label, TEXT, TEXT_SH);
        if (row->value[0] == '\0')
            continue;
        if (row->selected && row->adjustable)
        {
            int valueRight = right - 8;
            int valueLeft = valueRight - UiTextWidth(FONT_NORMAL, row->value);

            UiCaps(right - 4, rowY + 4, "}", SEL, 0);
            UiTextRight(FONT_NORMAL, valueRight, rowY, row->value, TEXT, TEXT_SH);
            UiCaps(valueLeft - 7, rowY + 4, "{", SEL, 0);
        }
        else
            UiTextRight(FONT_NORMAL, right - 8, rowY, row->value, TEXT, TEXT_SH);
    }

    /* A display change waiting to be kept: the question in a message box
     * under the sheet (the menu takes no other key meanwhile). */
    if (confirmSeconds >= 0)
    {
        char line[64];
        int boxWidth = width < 360 ? 360 : width;
        int boxHeight = 50;
        int boxX;
        int boxY = y + height + 4;
        int inner[4];
        int penX;

        if (boxWidth > sWidth - 16)
            boxWidth = sWidth - 16;
        boxX = (sWidth - boxWidth) / 2;
        if (boxY + boxHeight > sHeight - 4)
            boxY = sHeight - 4 - boxHeight;
        UiFieldFrame(boxX, boxY, boxWidth, boxHeight, PAPER, PAPER_LO, inner);
        UiText(FONT_NORMAL, inner[0] + 6, inner[1] + 2, "Keep this display setting?", TEXT, TEXT_SH);
        SDL_snprintf(line, sizeof(line), "It changes back in %d s.", confirmSeconds);
        UiText(FONT_NORMAL, inner[0] + 6, inner[1] + 2 + LINE_HEIGHT, line, TEXT_MUTED, TEXT_SH);
        penX = inner[0] + inner[2] - 6 - UiHintWidth(sKeepKeys, 1, "Keep") - 8 - UiHintWidth(sUndoKeys, 1, "Undo");
        penX = UiHint(penX, inner[1] + 2 + LINE_HEIGHT + 3, sKeepKeys, 1, "Keep") + 8;
        UiHint(penX, inner[1] + 2 + LINE_HEIGHT + 3, sUndoKeys, 1, "Undo");
        return;
    }

    /* The prompts in their own plate under the sheet, right-aligned. */
    {
        struct Shape plate = {0};
        int hints = UiHintWidth(sChooseKeys, 2, "Choose") + 8 + UiHintWidth(sChangeKeys, 2, "Change")
                  + 8 + UiHintWidth(sBackKeys, 1, "Close");
        int plateX;
        int plateY = y + height + 4;
        int penX;

        plate.width = hints + 12;
        plate.height = 19;
        UiShapeCorners(&plate, sCorner2, 2);
        plateX = x + width - plate.width;
        UiPaint(&plate, plateX, plateY, PAPER, FIELD_INK, 0, PAPER_LO, 1);
        penX = UiHint(plateX + 6, plateY + 4, sChooseKeys, 2, "Choose") + 8;
        penX = UiHint(penX, plateY + 4, sChangeKeys, 2, "Change") + 8;
        UiHint(penX, plateY + 4, sBackKeys, 1, "Close");
    }
}

static void DrawCorner(const char *text)
{
    struct Shape plate = {0};
    char caps[96];

    UiUpper(caps, sizeof(caps), text);
    for (char *p = caps; *p != '\0'; p++)
        if (*p == 'X' && p > caps && p[-1] >= '0' && p[-1] <= '9' && p[1] >= '0' && p[1] <= '9')
            *p = '*';
    plate.width = UiCapsWidth(caps) + 10;
    plate.height = 13;
    UiShapeCorners(&plate, sCorner1, 1);
    UiPaint(&plate, 6, 6, KEY_BASE, KEY_INK, KEY_HI, KEY_LO, 1);
    UiCaps(11, 9, caps, KEY_INK, 0);
}

/* What the canvas shows, to draw it again only when that changes (the cursor's
 * nudge changes it four times a second, a frame rate readout twice). */
static void UiSignature(char *out, size_t size, const struct DioramaOverlayMenu *menu, const char *corner,
                        const struct DioramaOverlayExtras *extras, const struct CanvasView *view)
{
    size_t used = (size_t)SDL_snprintf(out, size, "%dx%d %s|%s|%d|", view->width, view->height,
                                       corner != NULL ? corner : "",
                                       extras != NULL && extras->notice != NULL ? extras->notice : "",
                                       extras != NULL && extras->photoHints);

    if (menu == NULL || used >= size)
        return;
    used += (size_t)SDL_snprintf(out + used, size - used, "%s %d %u|", menu->title, menu->confirmSeconds,
                                 (FrameCount() / 16u) % 2u);
    for (int i = 0; i < menu->count && used < size; i++)
        used += (size_t)SDL_snprintf(out + used, size - used, "%s=%s%c%c|", menu->rows[i].label,
                                     menu->rows[i].value, menu->rows[i].selected ? '*' : ' ',
                                     menu->rows[i].adjustable ? '+' : ' ');
}

/* Photo mode (F9): its keys along the bottom of the picture. */
static void DrawPhotoHints(void)
{
    static const char *const sMoveKeys[] = {"{", "^", "_", "}"};
    static const char *const sZoomKeys[] = {"-", "="};
    static const char *const sTiltKeys[] = {",", "."};
    static const char *const sSaveKeys[] = {"F12"};
    static const char *const sBackKeys[] = {"ESC"};
    struct Shape plate = {0};
    int width = UiHintWidth(sMoveKeys, 4, "Move") + 8 + UiHintWidth(sZoomKeys, 2, "Zoom") + 8
              + UiHintWidth(sTiltKeys, 2, "Tilt") + 8 + UiHintWidth(sSaveKeys, 1, "Picture") + 8
              + UiHintWidth(sBackKeys, 1, "Back");
    int x;
    int y = sHeight - 27;
    int penX;

    plate.width = width + 12;
    plate.height = 19;
    UiShapeCorners(&plate, sCorner2, 2);
    x = (sWidth - plate.width) / 2;
    UiPaint(&plate, x, y, PAPER, FIELD_INK, 0, PAPER_LO, 1);
    UiTitleTab(x + 6, y - 14, "PHOTO");
    penX = UiHint(x + 6, y + 4, sMoveKeys, 4, "Move") + 8;
    penX = UiHint(penX, y + 4, sZoomKeys, 2, "Zoom") + 8;
    penX = UiHint(penX, y + 4, sTiltKeys, 2, "Tilt") + 8;
    penX = UiHint(penX, y + 4, sSaveKeys, 1, "Picture") + 8;
    UiHint(penX, y + 4, sBackKeys, 1, "Back");
}

/* A line in a message box at the bottom (a picture saved). */
static void DrawNotice(const char *text, bool aboveHints)
{
    int inner[4];
    int width = UiTextWidth(FONT_NORMAL, text) + 28;
    int height = 34;
    int x;
    int y = sHeight - height - (aboveHints ? 46 : 8);

    if (width > sWidth - 16)
        width = sWidth - 16;
    x = (sWidth - width) / 2;
    UiFieldFrame(x, y, width, height, PAPER, PAPER_LO, inner);
    UiText(FONT_NORMAL, inner[0] + 9, inner[1] + 4, text, TEXT, TEXT_SH);
}

void DioramaPCOverlay_Draw(const struct DioramaOverlayMenu *menu, const char *corner,
                           const struct DioramaOverlayExtras *extras,
                           int outputWidth, int outputHeight)
{
    static char sShown[2048];
    char signature[2048];
    struct CanvasView view;

    bool hints = extras != NULL && extras->photoHints;
    const char *notice = extras != NULL ? extras->notice : NULL;

    if (menu == NULL && corner == NULL && !hints && notice == NULL)
        return;
    if (!BeginCanvas(outputWidth, outputHeight, &view))
        return;
    UiSignature(signature, sizeof(signature), menu, corner, extras, &view);
    if (strcmp(signature, sShown) != 0 || sTexture == 0)
    {
        memset(sPixels, 0, (size_t)sWidth * sHeight * sizeof(*sPixels));
        if (hints)
            DrawPhotoHints();
        if (notice != NULL)
            DrawNotice(notice, hints);
        if (menu != NULL)
            DrawSettingsSheet(menu->title, menu->rows, menu->count, menu->confirmSeconds);
        if (corner != NULL)
            DrawCorner(corner);
        SDL_strlcpy(sShown, signature, sizeof(sShown));
        PresentCanvas(&view, outputWidth, outputHeight, true);
    }
    else
        PresentCanvas(&view, outputWidth, outputHeight, false);
}

void DioramaPCOverlay_Shutdown(void)
{
    if (sTexture != 0)
        glDeleteTextures(1, &sTexture);
    if (sVertexBuffer != 0)
        dglDeleteBuffers(1, &sVertexBuffer);
    if (sVertexArray != 0)
        dglDeleteVertexArrays(1, &sVertexArray);
    if (sProgram != 0)
        dglDeleteProgram(sProgram);
    sTexture = sVertexBuffer = sVertexArray = sProgram = 0;
    sTextureWidth = sTextureHeight = 0;
    free(sPixels);
    sPixels = NULL;
    sReady = false;
    sTried = false;
}
#endif // ENABLE_DIORAMA
