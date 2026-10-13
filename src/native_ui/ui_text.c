#ifdef ENABLE_NATIVE_UI
/*
 * Text for the native UI (see native_ui/ui_text.h). Project-owned.
 * Engine fonts: decoded from the game's own font data into one index texture
 * (each font a 256x512 sheet of 16x16 cells, glyph codes 0x000-0x1FF; 0x1xx
 * are the CHAR_EXTRA_SYMBOL glyphs), drawn as glyph quads coloured in the
 * shader, as the design's preview draws them (core.js EngineFont).
 * Label faces: Route Caps and Micro, rasterised as masks (core.js CapsFont).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/types.h"
#include "fonts.h"
#include "string_util.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_text.h"

#define CELL 16
#define SHEET_W 256
#define SHEET_H 512
#define GLYPH_COUNT 0x200

#define EOS 0xFF
#define CHAR_NEWLINE 0xFE
#define CHAR_PROMPT_SCROLL 0xFA
#define CHAR_PROMPT_CLEAR 0xFB
#define EXT_CTRL_CODE_BEGIN 0xFC
#define PLACEHOLDER_BEGIN 0xFD
#define CHAR_EXTRA_SYMBOL 0xF9
#define CHAR_KEYPAD_ICON 0xF8
#define CHAR_DYNAMIC 0xF7
#define CHAR_CURRENCY 0xB7 /* the game's money sign */

static const u16 *sGlyphData[UI_FONT_COUNT];
static const u8 *sWidthData[UI_FONT_COUNT];
static bool sReady;

void UiText_GlyphOrigin(int font, int code, int *u, int *v)
{
    code &= GLYPH_COUNT - 1;
    *u = font * SHEET_W + (code % 16) * CELL;
    *v = (code / 16) * CELL;
}

bool UiText_Init(void)
{
    uint8_t *indices;
    int width = SHEET_W * UI_FONT_COUNT;

    if (sReady)
        return true;
    sGlyphData[UI_FONT_NORMAL] = gFontNormalLatinGlyphs;
    sWidthData[UI_FONT_NORMAL] = gFontNormalLatinGlyphWidths;
    sGlyphData[UI_FONT_SMALL] = gFontSmallLatinGlyphs;
    sWidthData[UI_FONT_SMALL] = gFontSmallLatinGlyphWidths;
    sGlyphData[UI_FONT_NARROW] = gFontNarrowLatinGlyphs;
    sWidthData[UI_FONT_NARROW] = gFontNarrowLatinGlyphWidths;
    sGlyphData[UI_FONT_SHORT] = gFontShortLatinGlyphs;
    sWidthData[UI_FONT_SHORT] = gFontShortLatinGlyphWidths;

    indices = calloc((size_t)width * SHEET_H, 1);
    if (indices == NULL)
        return false;
    for (int font = 0; font < UI_FONT_COUNT; font++)
        for (int code = 0; code < GLYPH_COUNT; code++)
        {
            /* 0x20 halfwords a glyph: four 8x8 tiles (top left, top right,
             * bottom left, bottom right), 2 bits a pixel, the first pixel in
             * a row's top bits (text.c DecompressGlyphTile). */
            const u16 *glyph = sGlyphData[font] + 0x20 * code;
            int u, v;

            UiText_GlyphOrigin(font, code, &u, &v);
            for (int tile = 0; tile < 4; tile++)
            {
                int ox = (tile & 1) * 8;
                int oy = (tile >> 1) * 8;

                for (int row = 0; row < 8; row++)
                {
                    u16 bits = glyph[tile * 8 + row];

                    for (int px = 0; px < 8; px++)
                    {
                        uint8_t value = (uint8_t)((bits >> (14 - 2 * px)) & 3);

                        indices[(v + oy + row) * width + u + ox + px] = (value == 1 || value == 2) ? value : 0;
                    }
                }
            }
        }
    UiDraw_SetFontTexture(indices, width, SHEET_H);
    free(indices);
    sReady = true;
    return true;
}

int UiText_Advance(int font, int code)
{
    if (font < 0 || font >= UI_FONT_COUNT || sWidthData[font] == NULL)
        return 8;
    return code < GLYPH_COUNT ? sWidthData[font][code] : 8;
}

/* The next glyph of an engine string: returns its code (0..0x1FF), -1 at the
 * end of the line, and advances *s past it (control codes skipped). */
static int NextGlyph(const uint8_t **s)
{
    for (;;)
    {
        uint8_t c = **s;

        switch (c)
        {
        case EOS:
        case CHAR_NEWLINE:
        case CHAR_PROMPT_SCROLL:
        case CHAR_PROMPT_CLEAR:
            return -1;
        case CHAR_EXTRA_SYMBOL:
            (*s)++;
            if (**s == EOS)
                return -1;
            return 0x100 | *(*s)++;
        case CHAR_KEYPAD_ICON:
        case PLACEHOLDER_BEGIN:
        case CHAR_DYNAMIC:
            (*s)++;
            if (**s != EOS)
                (*s)++;
            continue;
        case EXT_CTRL_CODE_BEGIN:
        {
            /* skip the control code and its arguments, as the engine does */
            uint8_t code;

            (*s)++;
            code = **s;
            if (code == EOS)
                return -1;
            *s += GetExtCtrlCodeLength(code) > 0 ? GetExtCtrlCodeLength(code) : 1;
            continue;
        }
        default:
            (*s)++;
            return c;
        }
    }
}

int UiText_Width(int font, const uint8_t *s, int spacing)
{
    int width = 0;
    int count = 0;
    int code;

    if (s == NULL)
        return 0;
    while ((code = NextGlyph(&s)) >= 0)
    {
        width += UiText_Advance(font, code) + spacing;
        count++;
    }
    return count > 0 ? width - spacing : 0;
}

/* The engine's colour codes in a string: red and blue (the summary's memo,
 * the nature colours) become the design's red and blue, any other colour the
 * caller's own. Returns how many bytes the codes at s take (0: none). */
static int ColourCodes(const uint8_t *s, UiRGBA fg0, UiRGBA sh0, UiRGBA *fg, UiRGBA *sh)
{
    int n = 0;

    while (s[n] == EXT_CTRL_CODE_BEGIN && (s[n + 1] == 0x01 || s[n + 1] == 0x04) && s[n + 2] != EOS)
    {
        switch (s[n + 2])
        {
        case 0x4: case 0x5: /* TEXT_COLOR_RED, LIGHT_RED */
            *fg = UI_P_TEXT_RED;
            *sh = UI_P_TEXT_RED_SH;
            break;
        case 0x8: case 0x9: /* TEXT_COLOR_BLUE, LIGHT_BLUE */
            *fg = UI_P_MALE;
            *sh = UI_P_MALE_SH;
            break;
        default:
            *fg = fg0;
            *sh = sh0;
            break;
        }
        n += 1 + (GetExtCtrlCodeLength(s[n + 1]) > 0 ? GetExtCtrlCodeLength(s[n + 1]) : 1);
    }
    return n;
}

int UiText_DrawN(int font, int x, int y, const uint8_t *s, int count, UiRGBA fg, UiRGBA sh,
                 enum UiAlign align, int spacing)
{
    int code;
    UiRGBA fg0 = fg, sh0 = sh;

    if (s == NULL)
        return x;
    if (align == UI_ALIGN_RIGHT || align == UI_ALIGN_CENTER)
    {
        int w = UiText_Width(font, s, spacing);

        x = align == UI_ALIGN_RIGHT ? x - w : x - w / 2;
    }
    while (count != 0)
    {
        int advance;

        s += ColourCodes(s, fg0, sh0, &fg, &sh);
        if ((code = NextGlyph(&s)) < 0)
            break;
        advance = UiText_Advance(font, code);
        /* the engine copies only the glyph's width (CopyGlyphToWindow) */
        int cell = advance < CELL ? advance : CELL;

        if (cell > 0)
            UiG_GlyphQuad(x, y, font, code, cell, CELL, fg, sh);
        x += advance + spacing;
        if (count > 0)
            count--;
    }
    return x;
}

int UiText_Draw(int font, int x, int y, const uint8_t *s, UiRGBA fg, UiRGBA sh, enum UiAlign align,
                int spacing)
{
    return UiText_DrawN(font, x, y, s, -1, fg, sh, align, spacing);
}

int UiG_Text(int x, int y, const uint8_t *s)
{
    return UiText_Draw(UI_FONT_NORMAL, x, y, s, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

int UiG_TextW(int x, int y, const uint8_t *s)
{
    return UiText_Draw(UI_FONT_NORMAL, x, y, s, UI_P_TEXT_W, UI_P_TEXT_W_SH, UI_ALIGN_LEFT, 0);
}

int UiG_TextEx(int x, int y, const uint8_t *s, int font, UiRGBA fg, UiRGBA sh, enum UiAlign align)
{
    return UiText_Draw(font, x, y, s, fg, sh, align, 0);
}

int UiText_Length(const uint8_t *s)
{
    int n = 0;

    if (s == NULL)
        return 0;
    while (s[n] != EOS)
        n++;
    return n;
}

void UiText_CopyN(uint8_t *dst, int size, const uint8_t *src, int n)
{
    if (size <= 0)
        return;
    if (n > size - 1)
        n = size - 1;
    if (n < 0)
        n = 0;
    memcpy(dst, src, (size_t)n);
    dst[n] = EOS;
}

/* PK.wrap: words joined while they fit; a word that does not fit starts a line. */
int UiText_Wrap(const uint8_t *s, int width, int font, const uint8_t **starts, int *lengths, int maxLines)
{
    int lines = 0;

    if (s == NULL || maxLines <= 0)
        return 0;
    while (lines < maxLines)
    {
        const uint8_t *lineStart = s;
        const uint8_t *lastFit = NULL;   /* end of the last word that fit */
        const uint8_t *p = s;
        uint8_t tmp[512];

        for (;;)
        {
            const uint8_t *wordEnd = p;
            int w;

            while (*wordEnd != EOS && *wordEnd != 0x00 && *wordEnd != CHAR_NEWLINE
                && *wordEnd != CHAR_PROMPT_SCROLL && *wordEnd != CHAR_PROMPT_CLEAR)
                wordEnd++;
            UiText_CopyN(tmp, sizeof(tmp), lineStart, (int)(wordEnd - lineStart));
            w = UiText_Width(font, tmp, 0);
            if (w > width && lastFit != NULL)
            {
                starts[lines] = lineStart;
                lengths[lines] = (int)(lastFit - lineStart);
                lines++;
                s = lastFit + 1; /* past the space */
                break;
            }
            lastFit = wordEnd;
            if (*wordEnd == 0x00)
            {
                p = wordEnd + 1;
                continue;
            }
            starts[lines] = lineStart;
            lengths[lines] = (int)(wordEnd - lineStart);
            lines++;
            if (*wordEnd == EOS)
                return lines;
            s = wordEnd + 1;
            break;
        }
        if (*s == EOS && lines > 0 && s[-1] != CHAR_NEWLINE)
            return lines;
    }
    return lines;
}

/* ---------------------------------------------------------------- ASCII and numbers */

static uint8_t AsciiToEngine(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(0xBB + (c - 'A'));
    if (c >= 'a' && c <= 'z')
        return (uint8_t)(0xD5 + (c - 'a'));
    if (c >= '0' && c <= '9')
        return (uint8_t)(0xA1 + (c - '0'));
    switch (c)
    {
    case ' ': return 0x00;
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
    case '"': return 0xB1;
    case '\'': return 0xB4;
    case ',': return 0xB8;
    case '/': return 0xBA;
    case ':': return 0xF0;
    case '$': return CHAR_CURRENCY;
    case '\n': return CHAR_NEWLINE;
    default: return 0xAC;
    }
}

void UiText_FromAscii(uint8_t *dst, int size, const char *ascii)
{
    int n = 0;

    if (size <= 0)
        return;
    for (; ascii != NULL && *ascii != '\0' && n < size - 1; ascii++)
        dst[n++] = AsciiToEngine(*ascii);
    dst[n] = EOS;
}

void UiText_ToAscii(char *dst, int size, const uint8_t *s)
{
    int n = 0;

    if (size <= 0)
        return;
    for (; s != NULL && *s != EOS && n < size - 1; s++)
    {
        uint8_t c = *s;
        char out = '?';

        if (c >= 0xBB && c <= 0xD4) out = (char)('A' + (c - 0xBB));
        else if (c >= 0xD5 && c <= 0xEE) out = (char)('a' + (c - 0xD5));
        else if (c >= 0xA1 && c <= 0xAA) out = (char)('0' + (c - 0xA1));
        else if (c == 0x00) out = ' ';
        else if (c == 0xAB) out = '!';
        else if (c == 0xAD) out = '.';
        else if (c == 0xAE) out = '-';
        else if (c == 0xB3 || c == 0xB4) out = '\'';
        else if (c == 0xB1 || c == 0xB2) out = '"';
        /* the rest of AsciiToEngine's punctuation, back ("GINA & MIA") */
        else if (c == 0x2D) out = '&';
        else if (c == 0x2E) out = '+';
        else if (c == 0x35) out = '=';
        else if (c == 0x36) out = ';';
        else if (c == 0x5B) out = '%';
        else if (c == 0x5C) out = '(';
        else if (c == 0x5D) out = ')';
        else if (c == 0x85) out = '<';
        else if (c == 0x86) out = '>';
        else if (c == 0xB8) out = ',';
        else if (c == 0xBA) out = '/';
        else if (c == CHAR_CURRENCY) out = '$';
        else if (c == 0xF0) out = ':';
        else if (c == 0x1B) out = 'e';
        else if (c == CHAR_NEWLINE) out = ' ';
        else if (c >= 0xF7) { s++; if (*s == EOS) break; continue; }
        dst[n++] = out;
    }
    dst[n] = '\0';
}

void UiText_Append(uint8_t *dst, int size, const uint8_t *s)
{
    int n = UiText_Length(dst);

    for (; s != NULL && *s != EOS && n < size - 1; s++)
        dst[n++] = *s;
    dst[n] = EOS;
}

void UiText_AppendAscii(uint8_t *dst, int size, const char *ascii)
{
    int n = UiText_Length(dst);

    if (n < size)
        UiText_FromAscii(dst + n, size - n, ascii);
}

void UiText_AppendNumber(uint8_t *dst, int size, long value, int minDigits, bool thousands)
{
    char digits[32];
    char out[48];
    int len;
    int o = 0;
    bool negative = value < 0;

    if (negative)
        value = -value;
    len = snprintf(digits, sizeof(digits), "%0*ld", minDigits > 0 ? minDigits : 1, value);
    if (negative)
        out[o++] = '-';
    for (int i = 0; i < len; i++)
    {
        if (thousands && i > 0 && (len - i) % 3 == 0)
            out[o++] = ',';
        out[o++] = digits[i];
    }
    out[o] = '\0';
    UiText_AppendAscii(dst, size, out);
}

/* ---------------------------------------------------------------- label faces */

static const struct UiCapsGlyph *CapsGlyph(char c, bool micro)
{
    const struct UiCapsGlyph *table = micro ? gUiMicroGlyphs : gUiCapsGlyphs;
    int count = micro ? gUiMicroGlyphsCount : gUiCapsGlyphsCount;
    const struct UiCapsGlyph *space = NULL;

    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (int i = 0; i < count; i++)
    {
        if (table[i].ch == (uint8_t)c)
            return &table[i];
        if (table[i].ch == ' ')
            space = &table[i];
    }
    return space != NULL ? space : &table[0];
}

static int FaceWidth(const char *s, bool micro)
{
    int w = 0;

    for (; s != NULL && *s != '\0'; s++)
        w += CapsGlyph(*s, micro)->w + 1;
    return w > 0 ? w - 1 : 0;
}

int UiCaps_Width(const char *s)
{
    return FaceWidth(s, false);
}

int UiMicro_Width(const char *s)
{
    return FaceWidth(s, true);
}

const struct UiMask *UiCaps_Mask(const char *s, bool micro)
{
    /* the text rows as '#'/'.' strings, so the mask cache keys it like M.rows */
    int h = micro ? 5 : 7;
    int w = FaceWidth(s, micro);
    char *rows[7];
    const char *rowPtrs[7];
    const struct UiMask *m;

    if (w < 1)
        w = 1;
    for (int y = 0; y < h; y++)
    {
        int x = 0;

        rows[y] = malloc((size_t)w + 1);
        memset(rows[y], '.', (size_t)w);
        rows[y][w] = '\0';
        for (const char *p = s; p != NULL && *p != '\0'; p++)
        {
            const struct UiCapsGlyph *g = CapsGlyph(*p, micro);

            for (int gx = 0; gx < g->w; gx++)
                if (g->rows[y] & (1 << gx))
                    rows[y][x + gx] = '#';
            x += g->w + 1;
        }
        rowPtrs[y] = rows[y];
    }
    m = UiM_Rows(rowPtrs, h, NULL);
    for (int y = 0; y < h; y++)
        free(rows[y]);
    return m;
}

struct CapsBuild
{
    const struct UiMask *m;
    UiRGBA fg;
    UiRGBA ink;
    UiRGBA shadow;
    enum UiCapsStyle style;
};

static void BuildCaps(struct UiBitmap *bm, const void *ctx)
{
    const struct CapsBuild *b = ctx;
    const struct UiMask *m = b->m;

    if (b->style == UI_CAPS_OUTLINE && b->ink != UI_NONE)
    {
        const struct UiMask *big = UiM_Pad(m, 1, 1, 1, 1);

        if (b->shadow != UI_NONE)
            UiBitmap_FillMask(bm, UiM_Sub(UiM_Shift(UiM_Dilate8(big), 1, 1), UiM_Dilate8(big)), 0, 0, b->shadow);
        UiBitmap_FillMask(bm, UiM_Sub(UiM_Dilate8(big), big), 0, 0, b->ink);
        UiBitmap_FillMask(bm, m, 1, 1, b->fg);
    }
    else if (b->style == UI_CAPS_OUTLINE4 && b->ink != UI_NONE)
    {
        const struct UiMask *big = UiM_Pad(m, 1, 1, 1, 1);

        UiBitmap_FillMask(bm, UiM_Sub(UiM_Dilate4(big), big), 0, 0, b->ink);
        UiBitmap_FillMask(bm, m, 1, 1, b->fg);
    }
    else if (b->style == UI_CAPS_SHADOW && b->ink != UI_NONE)
    {
        const struct UiMask *big = UiM_Pad(m, 0, 1, 0, 1);

        UiBitmap_FillMask(bm, UiM_Sub(UiM_Or(UiM_Or(UiM_Shift(big, 1, 0), UiM_Shift(big, 0, 1)), UiM_Shift(big, 1, 1)), big),
                          0, 0, b->ink);
        UiBitmap_FillMask(bm, m, 0, 0, b->fg);
    }
    else
        UiBitmap_FillMask(bm, m, 0, 0, b->fg);
}

static int FaceDraw(int x, int y, const char *s, UiRGBA fg, UiRGBA ink, enum UiCapsStyle style,
                    enum UiAlign align, UiRGBA shadow, bool micro)
{
    struct CapsBuild b;
    char key[1024];
    int off;
    int w;

    b.m = UiCaps_Mask(s, micro);
    b.fg = fg;
    b.ink = ink;
    b.shadow = shadow;
    b.style = style;
    w = b.m->w;
    if (align == UI_ALIGN_CENTER)
        x -= w / 2;
    else if (align == UI_ALIGN_RIGHT)
        x -= w;
    off = (style == UI_CAPS_OUTLINE || style == UI_CAPS_OUTLINE4) && ink != UI_NONE ? 1 : 0;
    snprintf(key, sizeof(key), "c%s|%08x%08x%d%08x", b.m->key, fg, ink, (int)style, shadow);
    UiG_Blit(UiG_Cached(key, w + 3, b.m->h + 3, BuildCaps, &b), x - off, y - off);
    return x + w;
}

int UiCaps_Draw(int x, int y, const char *s, UiRGBA fg, UiRGBA ink, enum UiCapsStyle style,
                enum UiAlign align, UiRGBA shadow)
{
    return FaceDraw(x, y, s, fg, ink, style, align, shadow, false);
}

int UiMicro_Draw(int x, int y, const char *s, UiRGBA fg)
{
    return FaceDraw(x, y, s, fg, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE, true);
}

int UiG_Caps(int x, int y, const char *s, UiRGBA fg, UiRGBA ink, enum UiCapsStyle style, enum UiAlign align)
{
    return UiCaps_Draw(x, y, s, fg, ink, style, align, UI_NONE);
}
#endif // ENABLE_NATIVE_UI
