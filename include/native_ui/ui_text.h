#ifndef GUARD_NATIVE_UI_TEXT_H
#define GUARD_NATIVE_UI_TEXT_H

/*
 * Text for the native UI. Engine strings (the game's own charmap bytes, ending
 * in EOS 0xFF) are drawn with the engine's own latin fonts, decoded once from
 * the game's font data into an index texture: no conversion, every glyph the
 * game has (é, ♂, ♀, PKMN, {LV}, arrows...). Labels use the design's Route
 * Caps 5x7 and Micro 3x5 faces (ASCII strings, see UI_CAPS_* in ui_kit.h).
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_kit.h"

enum UiFont
{
    UI_FONT_NORMAL,
    UI_FONT_SMALL,
    UI_FONT_NARROW,
    UI_FONT_SHORT,
    UI_FONT_COUNT,
};

enum UiAlign
{
    UI_ALIGN_LEFT,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT,
};

enum UiCapsStyle
{
    UI_CAPS_PLAIN,
    UI_CAPS_OUTLINE,   /* 8-way ink ring */
    UI_CAPS_OUTLINE4,  /* 4-way ink ring */
    UI_CAPS_SHADOW,    /* ink right / down */
};

bool UiText_Init(void);
/* texel origin of a glyph cell in the font texture (ui_draw.c) */
void UiText_GlyphOrigin(int font, int code, int *u, int *v);
/* the font texture's indices (1 ink, 2 shadow) are uploaded by ui_draw.c */
void UiDraw_SetFontTexture(const uint8_t *indices, int width, int height);

int UiText_Advance(int font, int code);
/* width of an engine string (stops at EOS or a newline) */
int UiText_Width(int font, const uint8_t *s, int spacing);
/* draws an engine string's first line; returns the x after it */
int UiText_Draw(int font, int x, int y, const uint8_t *s, UiRGBA fg, UiRGBA sh, enum UiAlign align,
                int spacing);
/* draws up to `count` glyphs (a typewriter's progress); count < 0: all */
int UiText_DrawN(int font, int x, int y, const uint8_t *s, int count, UiRGBA fg, UiRGBA sh,
                 enum UiAlign align, int spacing);

/* G.text / G.textW of the design: normal font, dark text on light, or white */
int UiG_Text(int x, int y, const uint8_t *s);
int UiG_TextW(int x, int y, const uint8_t *s);
int UiG_TextEx(int x, int y, const uint8_t *s, int font, UiRGBA fg, UiRGBA sh, enum UiAlign align);

/* Word wrap to a pixel width: fills starts[] and lengths[] (bytes); returns the
 * line count. A newline (0xFE) or a prompt (0xFA, 0xFB) also breaks. */
int UiText_Wrap(const uint8_t *s, int width, int font, const uint8_t **starts, int *lengths, int maxLines);
/* copies n bytes and terminates with EOS */
void UiText_CopyN(uint8_t *dst, int size, const uint8_t *src, int n);

/* ASCII -> engine bytes (letters, digits, common punctuation; others as '?') */
void UiText_FromAscii(uint8_t *dst, int size, const char *ascii);
/* engine bytes -> ASCII (for logs and caps labels; unknown as '?') */
void UiText_ToAscii(char *dst, int size, const uint8_t *s);
int UiText_Length(const uint8_t *s);
/* appends an engine string / ASCII text / a number to an engine string buffer */
void UiText_Append(uint8_t *dst, int size, const uint8_t *s);
void UiText_AppendAscii(uint8_t *dst, int size, const char *ascii);
void UiText_AppendNumber(uint8_t *dst, int size, long value, int minDigits, bool thousands);
/* a place's name in title case from the game's capitals ("OF" inside it small); dropStop drops a final period */
void UiText_TitleCase(uint8_t *dst, int size, const uint8_t *src, bool dropStop);

/* Route Caps 5x7 and Micro 3x5 (pieces cached by text and colours) */
int UiCaps_Width(const char *s);
int UiCaps_Draw(int x, int y, const char *s, UiRGBA fg, UiRGBA ink, enum UiCapsStyle style,
                enum UiAlign align, UiRGBA shadow);
int UiMicro_Width(const char *s);
int UiMicro_Draw(int x, int y, const char *s, UiRGBA fg);
/* G.caps: white with an ink outline by default */
int UiG_Caps(int x, int y, const char *s, UiRGBA fg, UiRGBA ink, enum UiCapsStyle style, enum UiAlign align);
/* the caps text as a mask (for shapes built around a label) */
const struct UiMask *UiCaps_Mask(const char *s, bool micro);

#endif // GUARD_NATIVE_UI_TEXT_H
