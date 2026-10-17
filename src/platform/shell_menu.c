// Native PC build: the pre-game menu window (include/platform/shell_menu.h). Project-owned.
//
// Draws the application's screens (title, rows, text fields, buttons) with the game's own latin font on a plain SDL
// renderer, so it works on every machine the game opens a window on - the 3D renderer is not needed and is not used.
// Nothing here decides anything: it shows what the application describes and reports what the player chose.
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define PIPE_READ(buffer, count) _read(0, (buffer), (unsigned)(count))
#else
#include <unistd.h>
#define PIPE_READ(buffer, count) read(0, (buffer), (count))
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/types.h"
#include "fonts.h"
#include "platform/png_write.h"
#include "platform/shell_menu.h"

#define LINE_MAX_BYTES 65536
#define MAX_ITEMS 48
#define MAX_NOTES 24
#define FIELD_MAX 8192
#define GLYPH_CELL 16
#define ATLAS_COLUMNS 16
#define FALLBACK_BASE 256
#define FALLBACK_COUNT 16

enum ItemKind { ITEM_ROW, ITEM_FIELD, ITEM_BUTTON };

struct Item
{
    enum ItemKind kind;
    char id[48];
    char label[120];
    char detail[200];
    bool disabled, start, warn, accent, twoLine, digits, longValue, keyRow;
    int maxLength;
    char *value; // fields: FIELD_MAX + 1 bytes
    int length;
    int top, height; // layout, in logical pixels from the top of the item area
};

struct Screen
{
    char id[48];
    int token;
    char title[120];
    char sub[200];
    char hint[200];
    char notes[MAX_NOTES][240];
    int noteCount;
    char statusKind[8];
    char statusText[240];
    struct Item items[MAX_ITEMS];
    int itemCount;
};

static struct Screen sScreen, sPending;
static bool sPendingOpen;
static int sFocus;
static int sScroll;
static bool sLocked;     // an event was sent; wait for the application's next screen
static bool sCapturing;  // a key row (flag k) is waiting for the next key press
static char sCapturedKey[40];
static bool sTest;
static Uint32 sCommandEvent;
static void UpdateScale(void);

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sInkAtlas, *sShadowAtlas;
static uint8_t sGlyphs[FALLBACK_BASE + FALLBACK_COUNT][GLYPH_CELL][GLYPH_CELL];
static uint8_t sWidths[FALLBACK_BASE + FALLBACK_COUNT];
static int sScale = 2;
static int sLogicalWidth = 640, sLogicalHeight = 360;
static int sMouseX = -1, sMouseY = -1;
static Uint32 sStartTicks;

// ------------------------------------------------------------------ protocol helpers

static void Escape(const char *text, char *out, size_t size)
{
    size_t n = 0;

    for (; *text != '\0' && n + 3 < size; text++)
    {
        if (*text == '\\' || *text == '\t' || *text == '\n')
        {
            out[n++] = '\\';
            out[n++] = *text == '\t' ? 't' : *text == '\n' ? 'n' : '\\';
        }
        else if (*text != '\r')
            out[n++] = *text;
    }
    out[n] = '\0';
}

static void Emit(const char *text)
{
    fputs(text, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void EmitEvent(const char *kind, const char *id)
{
    char line[256], escaped[128];

    Escape(id != NULL ? id : "", escaped, sizeof(escaped));
    snprintf(line, sizeof(line), "event\t%d\t%s\t%s", sScreen.token, kind, escaped);
    // The fields' values follow, so the application never has to ask for them.
    fputs(line, stdout);
    for (int i = 0; i < sScreen.itemCount; i++)
    {
        const struct Item *item = &sScreen.items[i];

        if (item->kind != ITEM_FIELD)
            continue;
        fputc('\t', stdout);
        Escape(item->id, escaped, sizeof(escaped));
        fputs(escaped, stdout);
        fputc('\t', stdout);
        {
            static char big[FIELD_MAX * 2 + 8];

            Escape(item->value, big, sizeof(big));
            fputs(big, stdout);
        }
    }
    if (sCapturedKey[0] != '\0')
    {
        fputs("\tkey\t", stdout);
        Escape(sCapturedKey, escaped, sizeof(escaped));
        fputs(escaped, stdout);
        sCapturedKey[0] = '\0';
    }
    fputc('\n', stdout);
    fflush(stdout);
}

// ------------------------------------------------------------------ fonts

static const char *const sFallbackRows[FALLBACK_COUNT][7] = {
    {".....", ".....", ".....", ".....", ".....", ".....", "#####"},   // _
    {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."},   // #
    {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."},   // $
    {".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".###."},   // @
    {"#.#..", "#.#..", ".....", ".....", ".....", ".....", "....."},   // "
    {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."},   // [
    {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."},   // ]
    {"..##.", ".#...", ".#...", "#....", ".#...", ".#...", "..##."},   // {
    {".##..", "...#.", "...#.", "....#", "...#.", "...#.", ".##.."},   // }
    {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."},   // |
    {"#....", ".#...", ".#...", "..#..", "...#.", "...#.", "....#"},   // backslash
    {".....", ".....", ".#...", "#.#.#", "...#.", ".....", "....."},   // ~
    {"..#..", "#.#.#", ".###.", "#.#.#", "..#..", ".....", "....."},   // *
    {"..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."},   // ^
    {".#...", "..#..", ".....", ".....", ".....", ".....", "....."},   // `
    {".....", ".....", ".....", ".....", ".....", ".....", "....."},   // (unused)
};
static const char sFallbackChars[] = "_#$@\"[]{}|\\~*^`";

static void DecodeFonts(void)
{
    for (int code = 0; code < 256; code++)
    {
        const u16 *glyph = gFontNormalLatinGlyphs + 0x20 * code;

        for (int tile = 0; tile < 4; tile++)
            for (int row = 0; row < 8; row++)
            {
                u16 bits = glyph[tile * 8 + row];

                for (int px = 0; px < 8; px++)
                    sGlyphs[code][(tile >> 1) * 8 + row][(tile & 1) * 8 + px] = (uint8_t)((bits >> (14 - 2 * px)) & 3);
            }
        sWidths[code] = gFontNormalLatinGlyphWidths[code];
    }
    for (int i = 0; i < FALLBACK_COUNT; i++)
    {
        int code = FALLBACK_BASE + i;

        memset(sGlyphs[code], 0, sizeof(sGlyphs[code]));
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (sFallbackRows[i][y][x] == '#')
                {
                    sGlyphs[code][y + 3][x + 1] = 1;
                    if (sGlyphs[code][y + 4][x + 2] == 0)
                        sGlyphs[code][y + 4][x + 2] = 2;
                }
        sWidths[code] = 7;
    }
}

static SDL_Texture *BuildAtlas(int wanted)
{
    int total = FALLBACK_BASE + FALLBACK_COUNT;
    int rows = (total + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS;
    int width = ATLAS_COLUMNS * GLYPH_CELL, height = rows * GLYPH_CELL;
    uint32_t *pixels = calloc((size_t)width * height, sizeof(uint32_t));
    SDL_Texture *texture;

    if (pixels == NULL)
        return NULL;
    for (int code = 0; code < total; code++)
        for (int y = 0; y < GLYPH_CELL; y++)
            for (int x = 0; x < GLYPH_CELL; x++)
                if (sGlyphs[code][y][x] == wanted)
                    pixels[(size_t)((code / ATLAS_COLUMNS) * GLYPH_CELL + y) * width + (code % ATLAS_COLUMNS) * GLYPH_CELL + x] = 0xFFFFFFFFu;
    texture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture != NULL)
    {
        SDL_UpdateTexture(texture, NULL, pixels, width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    free(pixels);
    return texture;
}

static int CodeFor(const char *text, int i)
{
    unsigned char c = (unsigned char)text[i];

    if (c >= 'A' && c <= 'Z')
        return 0xBB + (c - 'A');
    if (c >= 'a' && c <= 'z')
        return 0xD5 + (c - 'a');
    if (c >= '0' && c <= '9')
        return 0xA1 + (c - '0');
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
    case ',': return 0xB8;
    case '/': return 0xBA;
    case '\'': return 0xB4;
    case ':': return 0xF0;
    default: break;
    }
    for (int i2 = 0; sFallbackChars[i2] != '\0'; i2++)
        if (sFallbackChars[i2] == (char)c)
            return FALLBACK_BASE + i2;
    return c < 0x20 ? 0x00 : 0xAC; // anything else shows as '?'
}

static int TextWidth(const char *text)
{
    int width = 0;

    for (int i = 0; text[i] != '\0'; i++)
        width += sWidths[CodeFor(text, i)];
    return width;
}

static void SetColor(uint32_t rgb)
{
    SDL_SetRenderDrawColor(sRenderer, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
}

static void Fill(int x, int y, int w, int h, uint32_t rgb)
{
    SDL_Rect r = {x, y, w, h};

    if (w <= 0 || h <= 0)
        return;
    SetColor(rgb);
    SDL_RenderFillRect(sRenderer, &r);
}

static void Frame(int x, int y, int w, int h, uint32_t rgb)
{
    Fill(x, y, w, 1, rgb);
    Fill(x, y + h - 1, w, 1, rgb);
    Fill(x, y, 1, h, rgb);
    Fill(x + w - 1, y, 1, h, rgb);
}

static void Glyph(SDL_Texture *atlas, int code, int x, int y, uint32_t rgb)
{
    SDL_Rect src = {(code % ATLAS_COLUMNS) * GLYPH_CELL, (code / ATLAS_COLUMNS) * GLYPH_CELL, GLYPH_CELL, GLYPH_CELL};
    SDL_Rect dst = {x, y, GLYPH_CELL, GLYPH_CELL};

    SDL_SetTextureColorMod(atlas, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    SDL_RenderCopy(sRenderer, atlas, &src, &dst);
}

// Text with an optional shadow (0: none); returns the right edge.
static int Text(int x, int y, const char *text, uint32_t ink, uint32_t shadow)
{
    for (int i = 0; text[i] != '\0'; i++)
    {
        int code = CodeFor(text, i);

        if (code != 0)
        {
            if (shadow != 0)
                Glyph(sShadowAtlas, code, x, y, shadow);
            Glyph(sInkAtlas, code, x, y, ink);
        }
        x += sWidths[code];
    }
    return x;
}

// The longest prefix of text that fits in width, with ".." when cut. Writes into out.
static void Fit(const char *text, int width, char *out, size_t size)
{
    int used = 0, n = 0;

    for (int i = 0; text[i] != '\0' && (size_t)n + 3 < size; i++)
    {
        int w = sWidths[CodeFor(text, i)];

        if (used + w > width)
        {
            while (n > 0 && used + 2 * sWidths[CodeFor("..", 0)] > width)
            {
                n--;
                used -= sWidths[CodeFor(out, n)];
            }
            out[n++] = '.';
            out[n++] = '.';
            out[n] = '\0';
            return;
        }
        out[n++] = text[i];
        used += w;
    }
    out[n] = '\0';
}

// Word wrap: writes up to maxLines lines of at most width pixels each into lines; returns the count.
static int Wrap(const char *text, int width, char lines[][240], int maxLines)
{
    int count = 0;
    const char *cursor = text;

    while (*cursor != '\0' && count < maxLines)
    {
        int used = 0, n = 0, lastSpace = -1;
        const char *start = cursor;

        while (*cursor != '\0' && *cursor != '\n')
        {
            int w = sWidths[CodeFor(cursor, 0)];

            if (used + w > width && n > 0)
                break;
            if (*cursor == ' ')
                lastSpace = n;
            if (n < 238)
                lines[count][n] = *cursor;
            n++;
            used += w;
            cursor++;
        }
        if (*cursor != '\0' && *cursor != '\n' && lastSpace > 0)
        {
            cursor = start + lastSpace + 1;
            n = lastSpace;
        }
        else if (*cursor == '\n')
            cursor++;
        if (n > 238)
            n = 238;
        lines[count][n] = '\0';
        count++;
    }
    return count;
}

// ------------------------------------------------------------------ the screen

#define COL_PAPER 0xF2F6F0
#define COL_INK 0x14261E
#define COL_MUTED 0x52635C
#define COL_HEADER 0x0E5A3F
#define COL_HEADER_DARK 0x093F2C
#define COL_ACCENT 0x1FA463
#define COL_ACCENT_DARK 0x15804C
#define COL_HIGHLIGHT 0xE3F3E8
#define COL_EDGE 0xCFE0D4
#define COL_WARN 0xB3261E
#define COL_WARN_BG 0xFCE9E7
#define COL_GOLD 0xF2C14E
#define COL_PANEL 0xFFFFFF
#define COL_RED 0xD8433A

static void DrawBall(int cx, int cy, int r)
{
    for (int y = -r; y <= r; y++)
    {
        int half = 0;

        while ((half + 1) * (half + 1) + y * y <= r * r)
            half++;
        Fill(cx - half, cy + y, 2 * half + 1, 1, y < -1 ? COL_RED : (y > 1 ? 0xFFFFFF : COL_INK));
    }
    Fill(cx - r, cy - 1, 2 * r + 1, 3, COL_INK);
    Fill(cx - 3, cy - 3, 7, 7, COL_INK);
    Fill(cx - 2, cy - 2, 5, 5, 0xFFFFFF);
}

static int ItemHeight(const struct Item *item)
{
    return item->kind == ITEM_FIELD ? 40 : item->kind == ITEM_BUTTON ? 26 : item->twoLine ? 38 : 28;
}

static void Layout(int contentWidth, int *areaTop, int *areaBottom)
{
    int y = 0;
    int notesHeight = sScreen.noteCount * 17;

    *areaTop = 72 + (sScreen.sub[0] != '\0' ? 22 : 0) + notesHeight + 8;
    *areaBottom = sLogicalHeight - (sScreen.statusText[0] != '\0' ? 52 : 28);
    (void)contentWidth;
    for (int i = 0; i < sScreen.itemCount; i++)
    {
        sScreen.items[i].top = y;
        sScreen.items[i].height = ItemHeight(&sScreen.items[i]);
        y += sScreen.items[i].height + 4;
    }
}

static int TotalHeight(void)
{
    if (sScreen.itemCount == 0)
        return 0;
    return sScreen.items[sScreen.itemCount - 1].top + sScreen.items[sScreen.itemCount - 1].height;
}

static void EnsureVisible(void)
{
    int top, bottom, contentWidth = 0;

    Layout(contentWidth, &top, &bottom);
    if (sFocus >= 0 && sFocus < sScreen.itemCount)
    {
        const struct Item *item = &sScreen.items[sFocus];
        int visible = bottom - top;

        if (item->top < sScroll)
            sScroll = item->top;
        else if (item->top + item->height > sScroll + visible)
            sScroll = item->top + item->height - visible;
    }
    {
        int maxScroll = TotalHeight() - (bottom - top);

        if (maxScroll < 0)
            maxScroll = 0;
        if (sScroll > maxScroll)
            sScroll = maxScroll;
        if (sScroll < 0)
            sScroll = 0;
    }
}

static bool Focusable(const struct Item *item)
{
    return !item->disabled;
}

static void FocusStep(int dir)
{
    int i = sFocus;

    for (int tries = 0; tries < sScreen.itemCount; tries++)
    {
        i += dir;
        if (i < 0 || i >= sScreen.itemCount)
            return; // stop at the ends
        if (Focusable(&sScreen.items[i]))
        {
            sFocus = i;
            EnsureVisible();
            return;
        }
    }
}

static void FocusFirst(void)
{
    sFocus = -1;
    for (int i = 0; i < sScreen.itemCount; i++)
        if (Focusable(&sScreen.items[i]) && (sFocus < 0 || sScreen.items[i].start))
        {
            if (sFocus < 0 || sScreen.items[i].start)
                sFocus = i;
        }
    sScroll = 0;
    EnsureVisible();
}

static void ContentBox(int *x, int *w)
{
    *w = sLogicalWidth - 40 < 560 ? sLogicalWidth - 40 : 560;
    *x = (sLogicalWidth - *w) / 2;
}

static void DrawFieldText(const struct Item *item, int x, int y, int w, bool focused)
{
    char shown[FIELD_MAX + 1];
    int textW;
    int offset = 0;

    if (item->longValue && item->length > 0)
        snprintf(shown, sizeof(shown), "%d characters pasted", item->length);
    else
        snprintf(shown, sizeof(shown), "%s", item->value);
    textW = TextWidth(shown);
    if (item->longValue && item->length > 0)
        Text(x + 4, y + 2, shown, COL_ACCENT_DARK, 0);
    else
    {
        // Show the end of a long value so the caret stays visible.
        while (textW > w - 10 && shown[offset] != '\0')
        {
            textW -= sWidths[CodeFor(shown, offset)];
            offset++;
        }
        {
            int endX = Text(x + 4, y + 2, shown + offset, COL_INK, 0);

            if (focused && ((SDL_GetTicks() - sStartTicks) / 500) % 2 == 0)
                Fill(endX + 1, y + 3, 2, 14, COL_INK);
        }
    }
}

static void DrawScreen(void)
{
    int contentX, contentW, areaTop, areaBottom;
    char line[260];

    SDL_RenderSetScale(sRenderer, (float)sScale, (float)sScale);
    SetColor(COL_PAPER);
    SDL_RenderClear(sRenderer);
    ContentBox(&contentX, &contentW);
    Layout(contentW, &areaTop, &areaBottom);
    // Items first; what scrolls out of their area is painted over below.
    {
        for (int i = 0; i < sScreen.itemCount; i++)
        {
            const struct Item *item = &sScreen.items[i];
            int y = areaTop + item->top - sScroll;
            bool focused = i == sFocus;

            if (y + item->height < areaTop || y > areaBottom)
                continue;
            if (item->kind == ITEM_ROW)
            {
                uint32_t bg = item->warn ? COL_WARN_BG : (focused ? COL_HIGHLIGHT : COL_PANEL);

                Fill(contentX, y, contentW, item->height, bg);
                Frame(contentX, y, contentW, item->height, focused ? COL_ACCENT : COL_EDGE);
                if (focused)
                {
                    Fill(contentX, y, 4, item->height, COL_ACCENT);
                    Fill(contentX + 8, y + item->height / 2 - 3, 3, 7, COL_GOLD);
                }
                Fit(item->label, contentW - 44, line, sizeof(line));
                {
                    uint32_t ink = item->disabled ? COL_MUTED : (item->warn ? COL_WARN : COL_INK);

                    if (item->twoLine)
                    {
                        Text(contentX + 18, y + 2, line, ink, 0);
                        Fit(item->detail, contentW - 36, line, sizeof(line));
                        Text(contentX + 18, y + 19, line, COL_MUTED, 0);
                    }
                    else
                    {
                        int labelW = TextWidth(line);
                        const char *detail = item->keyRow && sCapturing && focused ? "press a key (Esc cancels)" : item->detail;

                        Text(contentX + 18, y + 6, line, ink, 0);
                        if (detail[0] != '\0')
                        {
                            Fit(detail, contentW - 44 - labelW - 12, line, sizeof(line));
                            Text(contentX + contentW - 12 - TextWidth(line), y + 6, line, COL_MUTED, 0);
                        }
                    }
                }
            }
            else if (item->kind == ITEM_BUTTON)
            {
                uint32_t bg = item->disabled ? COL_EDGE : (item->accent ? (focused ? COL_ACCENT_DARK : COL_ACCENT) : (focused ? COL_HIGHLIGHT : COL_PANEL));
                uint32_t ink = item->accent && !item->disabled ? 0xFFFFFF : (item->disabled ? COL_MUTED : COL_INK);

                Fill(contentX, y, contentW, item->height, bg);
                Frame(contentX, y, contentW, item->height, focused ? COL_ACCENT : COL_EDGE);
                Fit(item->label, contentW - 20, line, sizeof(line));
                Text(contentX + (contentW - TextWidth(line)) / 2, y + 5, line, ink, 0);
            }
            else
            {
                Text(contentX, y, item->label, COL_MUTED, 0);
                Fill(contentX, y + 17, contentW, 22, COL_PANEL);
                Frame(contentX, y + 17, contentW, 22, focused ? COL_ACCENT : COL_EDGE);
                if (focused)
                    Frame(contentX + 1, y + 18, contentW - 2, 20, COL_ACCENT);
                DrawFieldText(item, contentX + 2, y + 18, contentW - 6, focused);
            }
        }
    }
    Fill(0, 0, sLogicalWidth, areaTop, COL_PAPER);
    Fill(0, areaBottom, sLogicalWidth, sLogicalHeight - areaBottom, COL_PAPER);
    // Header band.
    Fill(0, 0, sLogicalWidth, 40, COL_HEADER);
    Fill(0, 40, sLogicalWidth, 2, COL_HEADER_DARK);
    DrawBall(contentX + 14, 20, 11);
    Text(contentX + 36, 11, "EMERALDTOGETHER", 0xFFFFFF, COL_HEADER_DARK);
    // Title, subtitle, notes.
    Text(contentX, 50, sScreen.title, COL_INK, 0);
    if (sScreen.sub[0] != '\0')
    {
        Fit(sScreen.sub, contentW, line, sizeof(line));
        Text(contentX, 72, line, COL_MUTED, 0);
    }
    {
        int ny = 72 + (sScreen.sub[0] != '\0' ? 22 : 0);

        for (int i = 0; i < sScreen.noteCount; i++, ny += 17)
            Text(contentX, ny, sScreen.notes[i], COL_INK, 0);
    }
    // Scroll bar.
    if (TotalHeight() > areaBottom - areaTop)
    {
        int track = areaBottom - areaTop;
        int bar = track * track / TotalHeight();
        int pos = areaTop + (track - bar) * sScroll / (TotalHeight() - track);

        Fill(contentX + contentW + 6, areaTop, 3, track, COL_EDGE);
        Fill(contentX + contentW + 6, pos, 3, bar, COL_ACCENT);
    }
    // Status and hint.
    if (sScreen.statusText[0] != '\0')
    {
        uint32_t color = strcmp(sScreen.statusKind, "error") == 0 ? COL_WARN : strcmp(sScreen.statusKind, "warn") == 0 ? 0x8A5A00
                       : strcmp(sScreen.statusKind, "ok") == 0 ? COL_ACCENT_DARK : COL_INK;
        char shown[260];
        int dots = (int)((SDL_GetTicks() - sStartTicks) / 300) % 4;

        snprintf(shown, sizeof(shown), "%s%.*s", sScreen.statusText, strcmp(sScreen.statusKind, "busy") == 0 ? dots : 0, "...");
        Fill(contentX, sLogicalHeight - 50, contentW, 20, strcmp(sScreen.statusKind, "error") == 0 ? COL_WARN_BG : COL_HIGHLIGHT);
        Fit(shown, contentW - 12, line, sizeof(line));
        Text(contentX + 6, sLogicalHeight - 49, line, color, 0);
    }
    {
        const char *hint = sScreen.hint[0] != '\0' ? sScreen.hint : "Up/Down choose   Enter select   Esc back";

        Fit(hint, contentW, line, sizeof(line));
        Text(contentX, sLogicalHeight - 24, line, COL_MUTED, 0);
    }
    if (sLocked)
        Fill(0, 0, sLogicalWidth, 3, COL_GOLD);
    SDL_RenderSetScale(sRenderer, 1.0f, 1.0f);
    SDL_RenderPresent(sRenderer);
}

// ------------------------------------------------------------------ commands from the application

static void ItemDefaults(struct Item *item, enum ItemKind kind)
{
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->maxLength = 64;
}

static void ParseFlags(struct Item *item, const char *flags)
{
    for (const char *f = flags; *f != '\0'; f++)
    {
        switch (*f)
        {
        case 'd': item->disabled = true; break;
        case 's': item->start = true; break;
        case 'w': item->warn = true; break;
        case 'a': item->accent = true; break;
        case '2': item->twoLine = true; break;
        case 'k': item->keyRow = true; break;
        case 'n': item->digits = true; break;
        case 'm': item->longValue = true; item->maxLength = FIELD_MAX; break;
        case 'l': item->maxLength = atoi(f + 1); while (f[1] >= '0' && f[1] <= '9') f++; break;
        default: break;
        }
    }
    if (item->maxLength < 1 || item->maxLength > FIELD_MAX)
        item->maxLength = item->longValue ? FIELD_MAX : 64;
}

static void FreeItems(struct Screen *screen)
{
    for (int i = 0; i < screen->itemCount; i++)
    {
        free(screen->items[i].value);
        screen->items[i].value = NULL;
    }
    screen->itemCount = 0;
}

static void Copy(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src != NULL ? src : "");
}

static void HandleCommand(char *line)
{
    char *f[8];
    int n = ShellMenu_Split(line, f, 8);

    if (n < 1)
        return;
    if (strcmp(f[0], "screen") == 0 && n >= 3)
    {
        FreeItems(&sPending);
        memset(&sPending, 0, sizeof(sPending));
        Copy(sPending.id, sizeof(sPending.id), f[1]);
        sPending.token = atoi(f[2]);
        sPendingOpen = true;
    }
    else if (strcmp(f[0], "clipboard") == 0 && n >= 2)
        SDL_SetClipboardText(f[1]);
    else if (strcmp(f[0], "window") == 0 && n >= 5)
    {
        // window <w> <h> <x> <y>: the session window is small and out of the game's way; x < 0 centres it.
        int ww = atoi(f[1]), wh = atoi(f[2]), wx = atoi(f[3]), wy = atoi(f[4]);

        if (ww >= 320 && wh >= 240 && ww <= 7680 && wh <= 4320)
        {
            SDL_SetWindowFullscreen(sWindow, 0);
            SDL_SetWindowSize(sWindow, ww, wh);
            SDL_SetWindowPosition(sWindow, wx < 0 ? SDL_WINDOWPOS_CENTERED : wx, wy < 0 ? SDL_WINDOWPOS_CENTERED : wy);
            UpdateScale();
            EnsureVisible();
        }
    }
    else if (!sPendingOpen && strcmp(f[0], "quit") != 0 && strcmp(f[0], "inject") != 0 && strcmp(f[0], "shot") != 0
          && strcmp(f[0], "state") != 0)
        return;
    else if (strcmp(f[0], "title") == 0 && n >= 2)
        Copy(sPending.title, sizeof(sPending.title), f[1]);
    else if (strcmp(f[0], "sub") == 0 && n >= 2)
        Copy(sPending.sub, sizeof(sPending.sub), f[1]);
    else if (strcmp(f[0], "hint") == 0 && n >= 2)
        Copy(sPending.hint, sizeof(sPending.hint), f[1]);
    else if (strcmp(f[0], "note") == 0 && n >= 2)
    {
        // A long note wraps to the content width.
        char wrapped[MAX_NOTES][240];
        int lines = Wrap(f[1], 540, wrapped, MAX_NOTES);

        for (int i = 0; i < lines && sPending.noteCount < MAX_NOTES; i++)
            Copy(sPending.notes[sPending.noteCount++], sizeof(sPending.notes[0]), wrapped[i]);
    }
    else if (strcmp(f[0], "status") == 0 && n >= 3)
    {
        Copy(sPending.statusKind, sizeof(sPending.statusKind), f[1]);
        Copy(sPending.statusText, sizeof(sPending.statusText), f[2]);
    }
    else if ((strcmp(f[0], "row") == 0 || strcmp(f[0], "field") == 0 || strcmp(f[0], "button") == 0) && sPending.itemCount < MAX_ITEMS)
    {
        struct Item *item = &sPending.items[sPending.itemCount];
        enum ItemKind kind = f[0][0] == 'r' ? ITEM_ROW : f[0][0] == 'f' ? ITEM_FIELD : ITEM_BUTTON;

        ItemDefaults(item, kind);
        Copy(item->id, sizeof(item->id), n > 1 ? f[1] : "");
        Copy(item->label, sizeof(item->label), n > 2 ? f[2] : "");
        if (kind == ITEM_ROW)
        {
            Copy(item->detail, sizeof(item->detail), n > 3 ? f[3] : "");
            ParseFlags(item, n > 4 ? f[4] : "");
        }
        else if (kind == ITEM_FIELD)
        {
            ParseFlags(item, n > 4 ? f[4] : "");
            item->value = calloc(1, FIELD_MAX + 1);
            if (item->value != NULL)
            {
                Copy(item->value, (size_t)item->maxLength + 1, n > 3 ? f[3] : "");
                item->length = (int)strlen(item->value);
            }
        }
        else
            ParseFlags(item, n > 3 ? f[3] : "");
        if (kind == ITEM_BUTTON && n > 3)
            ParseFlags(item, f[3]);
        if (kind != ITEM_FIELD || item->value != NULL)
            sPending.itemCount++;
    }
    else if (strcmp(f[0], "end") == 0 && sPendingOpen)
    {
        FreeItems(&sScreen);
        sScreen = sPending;
        memset(&sPending, 0, sizeof(sPending));
        sPendingOpen = false;
        sLocked = false;
        sCapturing = false;
        FocusFirst();
        SDL_StartTextInput();
    }
    else if (strcmp(f[0], "quit") == 0)
    {
        SDL_Event quit;

        memset(&quit, 0, sizeof(quit));
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    else if (sTest && strcmp(f[0], "inject") == 0 && n >= 3)
    {
        SDL_Event e;

        memset(&e, 0, sizeof(e));
        if (strcmp(f[1], "text") == 0)
        {
            e.type = SDL_TEXTINPUT;
            snprintf(e.text.text, sizeof(e.text.text), "%s", f[2]);
            SDL_PushEvent(&e);
        }
        else if (strcmp(f[1], "key") == 0)
        {
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = SDL_GetKeyFromName(f[2]);
            e.key.keysym.mod = n >= 4 && strcmp(f[3], "ctrl") == 0 ? KMOD_CTRL : 0;
            SDL_PushEvent(&e);
        }
    }
    else if (sTest && strcmp(f[0], "shot") == 0 && n >= 2)
    {
        int w, h;
        uint8_t *pixels;

        SDL_GetRendererOutputSize(sRenderer, &w, &h);
        pixels = malloc((size_t)w * h * 4);
        if (pixels != NULL && SDL_RenderReadPixels(sRenderer, NULL, SDL_PIXELFORMAT_ABGR8888, pixels, w * 4) == 0
         && PngWrite_RGBA(f[1], pixels, w, h, false))
            Emit("shot\tok");
        else
            Emit("shot\terror");
        free(pixels);
    }
    else if (sTest && strcmp(f[0], "state") == 0)
    {
        char out[512], id[128];

        Escape(sFocus >= 0 && sFocus < sScreen.itemCount ? sScreen.items[sFocus].id : "", id, sizeof(id));
        snprintf(out, sizeof(out), "state\t%s\t%d\t%s\t%d", sScreen.id, sScreen.token, id, sLocked ? 1 : 0);
        Emit(out);
    }
}

// ------------------------------------------------------------------ input

static void Activate(void)
{
    const struct Item *item;

    if (sLocked || sFocus < 0 || sFocus >= sScreen.itemCount)
        return;
    item = &sScreen.items[sFocus];
    if (item->disabled)
        return;
    if (item->kind == ITEM_FIELD)
    {
        FocusStep(1);
        return;
    }
    if (item->keyRow)
    {
        sCapturing = true;      // the next key press is the answer (Esc cancels)
        return;
    }
    sLocked = true;
    EmitEvent("select", item->id);
}

static void Back(void)
{
    if (sLocked)
        return;
    sLocked = true;
    EmitEvent("back", "");
}

static void FieldInsert(struct Item *item, const char *text)
{
    for (const char *c = text; *c != '\0'; c++)
    {
        unsigned char ch = (unsigned char)*c;

        if (ch < 0x20 || ch == 0x7F || item->length >= item->maxLength)
            continue;
        if (item->digits && (ch < '0' || ch > '9'))
            continue;
        if (!item->longValue && ch == ' ' && item->length == 0)
            continue;
        item->value[item->length++] = (char)ch;
        item->value[item->length] = '\0';
    }
}

static void OnKey(SDL_Keycode key, Uint16 mod)
{
    struct Item *item = sFocus >= 0 && sFocus < sScreen.itemCount ? &sScreen.items[sFocus] : NULL;
    bool inField = item != NULL && item->kind == ITEM_FIELD;
    bool ctrl = (mod & KMOD_CTRL) != 0;

    if (key == SDLK_F11)
    {
        Uint32 flags = SDL_GetWindowFlags(sWindow);

        SDL_SetWindowFullscreen(sWindow, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    if (sCapturing)
    {
        sCapturing = false;
        if (key != SDLK_ESCAPE && !sLocked && item != NULL)
        {
            snprintf(sCapturedKey, sizeof(sCapturedKey), "%s", SDL_GetKeyName(key));
            sLocked = true;
            EmitEvent("select", item->id);
        }
        return;
    }
    if (sLocked && key != SDLK_ESCAPE)
        return;
    if (inField && ctrl && (key == SDLK_v))
    {
        char *clip = SDL_GetClipboardText();

        if (clip != NULL)
        {
            // An invite code arrives with the line breaks and spaces of an email: keep only what a code has.
            if (item->longValue)
            {
                char *w = clip;

                for (const char *r = clip; *r != '\0'; r++)
                    if (*r != ' ' && *r != '\n' && *r != '\r' && *r != '\t')
                        *w++ = *r;
                *w = '\0';
                item->value[0] = '\0';
                item->length = 0;
            }
            FieldInsert(item, clip);
            SDL_free(clip);
        }
        return;
    }
    if (inField && ctrl && key == SDLK_u)
    {
        item->value[0] = '\0';
        item->length = 0;
        return;
    }
    switch (key)
    {
    case SDLK_UP: FocusStep(-1); break;
    case SDLK_DOWN: FocusStep(1); break;
    case SDLK_TAB: FocusStep((mod & KMOD_SHIFT) ? -1 : 1); break;
    case SDLK_PAGEUP: for (int i = 0; i < 5; i++) FocusStep(-1); break;
    case SDLK_PAGEDOWN: for (int i = 0; i < 5; i++) FocusStep(1); break;
    case SDLK_HOME: if (!inField) { sFocus = 0; FocusStep(1); FocusStep(-1); } break;
    case SDLK_END: if (!inField) { sFocus = sScreen.itemCount - 1; EnsureVisible(); } break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: Activate(); break;
    case SDLK_SPACE: if (!inField) Activate(); break;
    case SDLK_ESCAPE: Back(); break;
    case SDLK_BACKSPACE:
        if (inField)
        {
            if (item->length > 0)
                item->value[--item->length] = '\0';
        }
        else
            Back();
        break;
    case SDLK_DELETE: if (inField) { item->value[0] = '\0'; item->length = 0; } break;
    case SDLK_z: if (!inField) Activate(); break;
    case SDLK_x: if (!inField) Back(); break;
    default: break;
    }
}

static int ItemAt(int px, int py)
{
    int contentX, contentW, areaTop, areaBottom;
    int lx = px / sScale, ly = py / sScale;

    ContentBox(&contentX, &contentW);
    Layout(contentW, &areaTop, &areaBottom);
    if (lx < contentX || lx >= contentX + contentW || ly < areaTop || ly >= areaBottom)
        return -1;
    for (int i = 0; i < sScreen.itemCount; i++)
    {
        int y = areaTop + sScreen.items[i].top - sScroll;

        if (ly >= y && ly < y + sScreen.items[i].height)
            return i;
    }
    return -1;
}

static void UpdateScale(void)
{
    int w, h;

    SDL_GetRendererOutputSize(sRenderer, &w, &h);
    sScale = h / 360;
    if (sScale < 1)
        sScale = 1;
    if (w / 640 < sScale && w / 640 >= 1)
        sScale = w / 640;
    sLogicalWidth = w / sScale;
    sLogicalHeight = h / sScale;
}

static void HandleEvent(const SDL_Event *e)
{
    if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e->window.event == SDL_WINDOWEVENT_RESIZED))
    {
        UpdateScale();
        EnsureVisible();
    }
    else if (e->type == SDL_KEYDOWN)
        OnKey(e->key.keysym.sym, e->key.keysym.mod);
    else if (e->type == SDL_TEXTINPUT)
    {
        struct Item *item = sFocus >= 0 && sFocus < sScreen.itemCount ? &sScreen.items[sFocus] : NULL;

        if (!sLocked && item != NULL && item->kind == ITEM_FIELD)
            FieldInsert(item, e->text.text);
    }
    else if (e->type == SDL_MOUSEMOTION)
    {
        int i = ItemAt(e->motion.x, e->motion.y);

        sMouseX = e->motion.x;
        sMouseY = e->motion.y;
        if (i >= 0 && Focusable(&sScreen.items[i]) && i != sFocus && !sLocked)
            sFocus = i;
    }
    else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT)
    {
        int i = ItemAt(e->button.x, e->button.y);

        if (i >= 0 && Focusable(&sScreen.items[i]) && !sLocked)
        {
            sFocus = i;
            if (sScreen.items[i].kind != ITEM_FIELD)
                Activate();
        }
    }
    else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT)
        Back();
    else if (e->type == SDL_MOUSEWHEEL)
    {
        sScroll -= e->wheel.y * 26;
        EnsureVisible();
    }
    else if (e->type == SDL_CONTROLLERBUTTONDOWN)
    {
        switch (e->cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: FocusStep(-1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: FocusStep(1); break;
        case SDL_CONTROLLER_BUTTON_A: Activate(); break;
        case SDL_CONTROLLER_BUTTON_B: Back(); break;
        default: break;
        }
    }
    else if (e->type == SDL_CONTROLLERDEVICEADDED)
        SDL_GameControllerOpen(e->cdevice.which);
}

// ------------------------------------------------------------------ the application's pipe

// Reads the application's lines with read() on the pipe itself, not stdio: a thread blocked in fgets would hold stdin's lock and
// exit() would wait for it forever.
static int ReaderThread(void *unused)
{
    char *line = malloc(LINE_MAX_BYTES);
    size_t used = 0;
    char chunk[4096];

    (void)unused;
    if (line == NULL)
        return 1;
    for (;;)
    {
        int count = (int)PIPE_READ(chunk, sizeof(chunk));

        if (count <= 0)
            break;
        for (int i = 0; i < count; i++)
        {
            if (chunk[i] == '\n')
            {
                SDL_Event e;

                while (used > 0 && line[used - 1] == '\r')
                    used--;
                line[used] = '\0';
                memset(&e, 0, sizeof(e));
                e.type = sCommandEvent;
                e.user.data1 = strdup(line);
                SDL_PushEvent(&e);
                used = 0;
            }
            else if (used + 1 < LINE_MAX_BYTES)
                line[used++] = chunk[i];
        }
    }
    // The application closed the pipe: nothing can answer the menu any more.
    {
        SDL_Event e;

        memset(&e, 0, sizeof(e));
        e.type = sCommandEvent;
        e.user.data1 = strdup("\x01gone");
        SDL_PushEvent(&e);
    }
    free(line);
    return 0;
}

int ShellMenu_Main(int argc, char **argv)
{
    int exitCode = 0;
    bool running = true;
    int w = 1280, h = 720;

    (void)argv;
#ifdef _WIN32
    _setmode(0, _O_BINARY);      // the pipe carries bytes: no CRLF changes and no Ctrl-Z end of file
    _setmode(1, _O_BINARY);
#endif
    sTest = SDL_getenv("PKMN_SHELL_TEST") != NULL && SDL_getenv("PKMN_SHELL_TEST")[0] == '1';
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--shell-size") == 0)
            sscanf(argv[i + 1], "%dx%d", &w, &h);
#if defined(_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");   // physical pixels: sharp at any display scaling
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("error\tsdl\t%s\n", SDL_GetError());
        fflush(stdout);
        return 2;
    }
    sWindow = SDL_CreateWindow("EmeraldTogether", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (sWindow == NULL)
    {
        printf("error\twindow\t%s\n", SDL_GetError());
        fflush(stdout);
        SDL_Quit();
        return 2;
    }
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (sRenderer == NULL)
        sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    if (sRenderer == NULL)
    {
        printf("error\trenderer\t%s\n", SDL_GetError());
        fflush(stdout);
        SDL_DestroyWindow(sWindow);
        SDL_Quit();
        return 2;
    }
    DecodeFonts();
    sInkAtlas = BuildAtlas(1);
    sShadowAtlas = BuildAtlas(2);
    if (sInkAtlas == NULL || sShadowAtlas == NULL)
    {
        printf("error\tatlas\t%s\n", SDL_GetError());
        fflush(stdout);
        return 2;
    }
    sCommandEvent = SDL_RegisterEvents(1);
    UpdateScale();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
    sStartTicks = SDL_GetTicks();
    // A blank first screen until the application describes one.
    snprintf(sScreen.title, sizeof(sScreen.title), "Starting");
    snprintf(sScreen.statusKind, sizeof(sScreen.statusKind), "busy");
    snprintf(sScreen.statusText, sizeof(sScreen.statusText), "Checking your game files");
    sLocked = true;
    SDL_CreateThread(ReaderThread, "shell-stdin", NULL);
    {
        char ready[64];

        snprintf(ready, sizeof(ready), "ready\t%d\t%d\t%d", SHELL_PROTOCOL, w, h);
        Emit(ready);
    }
    while (running)
    {
        SDL_Event e;

        if (SDL_WaitEventTimeout(&e, 33))
        {
            do
            {
                if (e.type == SDL_QUIT)
                {
                    EmitEvent("closed", "");
                    running = false;
                }
                else if (e.type == sCommandEvent)
                {
                    char *line = (char *)e.user.data1;

                    if (line[0] == '\x01')
                    {
                        exitCode = 3;
                        running = false;
                    }
                    else
                        HandleCommand(line);
                    free(line);
                }
                else
                    HandleEvent(&e);
            } while (running && SDL_PollEvent(&e));
        }
        if (running)
            DrawScreen();
    }
    FreeItems(&sScreen);
    FreeItems(&sPending);
    SDL_DestroyTexture(sInkAtlas);
    SDL_DestroyTexture(sShadowAtlas);
    SDL_DestroyRenderer(sRenderer);
    SDL_DestroyWindow(sWindow);
    SDL_Quit();
    fflush(stdout);
    return exitCode;
}
