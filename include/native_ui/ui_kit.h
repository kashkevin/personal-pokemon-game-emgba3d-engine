#ifndef GUARD_NATIVE_UI_KIT_H
#define GUARD_NATIVE_UI_KIT_H

/*
 * The native UI's pixel-art kit: a C port of the approved UI design's renderer
 * (artifacts/ui-art-direction/preview/js/core.js, itself a port of tools/pkui):
 * colours and ramps, masks (stepped corners, capsules, ellipses, outlines,
 * patterns) and the palette bevel "paint". Shapes are rasterised on the CPU
 * once, cached by key, and drawn from the GPU atlas afterwards (ui_draw.h).
 * Every coordinate is a logical pixel of the 640x360 UI canvas.
 */
#include <stdbool.h>
#include <stdint.h>

/* Colours: 0xAABBGGRR (the byte order GL_RGBA reads on a little-endian PC). */
typedef uint32_t UiRGBA;
#define UI_RGBA(r, g, b, a) ((UiRGBA)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r)))
#define UI_RGB(r, g, b) UI_RGBA(r, g, b, 255)
#define UI_HEX(hex) UI_RGB(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)
#define UI_HEXA(hex, a) UI_RGBA(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF, a)
#define UI_R(c) ((int)((c) & 0xFF))
#define UI_G(c) ((int)(((c) >> 8) & 0xFF))
#define UI_B(c) ((int)(((c) >> 16) & 0xFF))
#define UI_A(c) ((int)(((c) >> 24) & 0xFF))
#define UI_NONE ((UiRGBA)0)   /* "no colour" (alpha 0 is never drawn) */

#include "native_ui/data/ui_palette.h"

struct UiRamp
{
    UiRGBA hi;
    UiRGBA base;
    UiRGBA lo;
    UiRGBA ink;
};

/* Python's round(): halves go to the even neighbour (parity with pkui). */
int UiPyRound(double x);
UiRGBA UiMix(UiRGBA c1, UiRGBA c2, double t);
UiRGBA UiWithAlpha(UiRGBA c, int a);
/* PK.ramp(base, hi_t, lo_t, ink_t) with the default warm and cool. */
struct UiRamp UiRampOf(UiRGBA base, double hiT, double loT, double inkT);
/* ... with a warm and cool of its own (UI_NONE: the default) */
struct UiRamp UiRampOf2(UiRGBA base, double hiT, double loT, double inkT, UiRGBA warm, UiRGBA cool);
#define UiRampDefault(base) UiRampOf(base, 0.38, 0.28, 0.62)

/* ------------------------------------------------------------------ masks */

/* A w x h grid of 0/1, cached by the key that names how it was built. Masks
 * returned by the constructors stay valid until the end of the frame. */
struct UiMask
{
    uint16_t w;
    uint16_t h;
    uint32_t hash;
    const char *key;
    uint8_t *d;
};

/* A corner: how far in each row starts, from the corner inwards. */
struct UiCorner
{
    int8_t n;
    int8_t p[16];
};
struct UiCorner UiCornerR(int r);                 /* PK.cornerProfile(r) */
struct UiCorner UiCornerP(int n, const int8_t *profile);
struct UiCorner UiCornerChamfer(int n);           /* n, n-1, ..., 1 */

const struct UiMask *UiM_Rect(int w, int h);
const struct UiMask *UiM_Rounded(int w, int h, struct UiCorner tl, struct UiCorner tr,
                                 struct UiCorner bl, struct UiCorner br);
const struct UiMask *UiM_RoundedR(int w, int h, int r);        /* the same radius at each corner */
const struct UiMask *UiM_Rounded4(int w, int h, int tl, int tr, int bl, int br);
const struct UiMask *UiM_Capsule(int w, int h, bool left, bool right);
const struct UiMask *UiM_Ellipse(int w, int h);
const struct UiMask *UiM_Circle(int d);
/* points: n pairs (x, y) */
const struct UiMask *UiM_Poly(int w, int h, const int *points, int n);
/* rows of characters; any char in `on` (NULL: "#") is set */
const struct UiMask *UiM_Rows(const char *const *rows, int count, const char *on);
const struct UiMask *UiM_Erode4(const struct UiMask *a);
const struct UiMask *UiM_Dilate4(const struct UiMask *a);
const struct UiMask *UiM_Dilate8(const struct UiMask *a);
const struct UiMask *UiM_Shift(const struct UiMask *a, int dx, int dy);
const struct UiMask *UiM_Pad(const struct UiMask *a, int t, int b, int l, int r);
const struct UiMask *UiM_Place(const struct UiMask *a, const struct UiMask *b, int x, int y);
const struct UiMask *UiM_And(const struct UiMask *a, const struct UiMask *b);
const struct UiMask *UiM_Or(const struct UiMask *a, const struct UiMask *b);
const struct UiMask *UiM_Sub(const struct UiMask *a, const struct UiMask *b);
const struct UiMask *UiM_Not(const struct UiMask *a);
const struct UiMask *UiM_Window(const struct UiMask *a, int x0, int y0, int x1, int y1);
const struct UiMask *UiM_Outline(const struct UiMask *a);
/* cuts: count triples (row, fromRight, count); row < 0 counts from the bottom */
const struct UiMask *UiM_Carve(const struct UiMask *a, const int *cuts, int count, const char *tag);
/* M.fn: a w x h mask set where fn says so; the tag must name everything fn depends on */
typedef bool (*UiMaskFn)(int x, int y, const void *ctx);
const struct UiMask *UiM_Fn(int w, int h, const char *tag, UiMaskFn fn, const void *ctx);

/* ------------------------------------------------------------------ patterns */

enum UiPatternKind
{
    UI_PAT_NONE,
    UI_PAT_DIAG,     /* (x + dir * y + phase) mod period < width */
    UI_PAT_HSTRIPE,  /* (y + phase) mod period < width */
    UI_PAT_VSTRIPE,  /* (x + phase) mod period < width */
    UI_PAT_DOTS,     /* staggered dots */
    UI_PAT_CHECKER,
    UI_PAT_BELOW,    /* y > y0 */
};

struct UiPattern
{
    uint8_t kind;
    int16_t a, b, c, d, e;   /* the kind's parameters, as in PK.pat */
    const struct UiPattern *and_; /* and with another pattern (NULL: none) */
};
struct UiPattern UiPat_Diag(int period, int width, int phase, int dir);
struct UiPattern UiPat_HStripe(int period, int width, int phase);
struct UiPattern UiPat_VStripe(int period, int width, int phase);
struct UiPattern UiPat_Dots(int step, int size, int ox, int oy, bool stagger);
struct UiPattern UiPat_Checker(int size, int phase);
struct UiPattern UiPat_Below(int y0);
bool UiPat_At(const struct UiPattern *p, int x, int y);
const struct UiMask *UiM_Pattern(const struct UiMask *a, const struct UiPattern *p);
/* the pattern's period as a tile (PK.pat.tileSize) */
void UiPat_TileSize(const struct UiPattern *p, int *w, int *h);
/* appends the pattern's key to buf */
void UiPat_Key(const struct UiPattern *p, char *buf, int size);

/* ------------------------------------------------------------------ bitmaps */

struct UiBitmap
{
    int w;
    int h;
    uint8_t *d;  /* RGBA, straight alpha */
};
void UiBitmap_Init(struct UiBitmap *bm, int w, int h);
void UiBitmap_Free(struct UiBitmap *bm);
void UiBitmap_FillMask(struct UiBitmap *bm, const struct UiMask *m, int x, int y, UiRGBA c);
void UiBitmap_Rect(struct UiBitmap *bm, int x, int y, int w, int h, UiRGBA c);
void UiBitmap_Put(struct UiBitmap *bm, int x, int y, UiRGBA c);

/* PK.paintInto: a 1 px ink outline and a palette bevel (light top/left, shade
 * bottom/right); a colour of UI_NONE leaves that part out. */
struct UiPaint
{
    UiRGBA ink;
    UiRGBA hi;
    UiRGBA lo;
    int hiW;       /* 0: 1 */
    int loW;       /* 0: 1 */
    const struct UiPattern *pattern;
    UiRGBA patternColor;
};
void UiBitmap_Paint(struct UiBitmap *bm, const struct UiMask *m, int x, int y, UiRGBA body,
                    const struct UiPaint *o);
/* the cache key of a painted mask (PK.paintKey) */
void UiPaintKey(char *buf, int size, const struct UiMask *m, UiRGBA body, const struct UiPaint *o);

/* ------------------------------------------------------------------ label faces */

struct UiCapsGlyph
{
    uint8_t ch;      /* ASCII, or 0x10.. for the special labels (see UI_CAPS_*) */
    uint8_t w;
    uint8_t rows[7]; /* bit x set: column x on */
};
/* Bytes the label faces read for characters outside ASCII. */
#define UI_CAPS_RIGHT "\x10"   /* ▶ */
#define UI_CAPS_LEFT "\x11"    /* ◀ */
#define UI_CAPS_UP "\x12"      /* ▲ */
#define UI_CAPS_DOWN "\x13"    /* ▼ */
#define UI_CAPS_TIMES "\x14"   /* × */
#define UI_CAPS_E_ACUTE "\x15" /* é */
#define UI_CAPS_DOT "\x16"     /* · */
#define UI_CAPS_STAR "\x17"    /* ★ */
extern const struct UiCapsGlyph gUiCapsGlyphs[];
extern const int gUiCapsGlyphsCount;
extern const struct UiCapsGlyph gUiMicroGlyphs[];
extern const int gUiMicroGlyphsCount;
extern const struct UiRamp gUiTypeRamps[UI_TYPE_COUNT];
extern const char *const gUiTypeLabels[UI_TYPE_COUNT];
extern const char *const gUiTypeKeys[UI_TYPE_COUNT];

/* ------------------------------------------------------------------ cache housekeeping */

/* Called once a frame: frees the mask cache when it grew past its budget
 * (masks from the previous frame are no longer referenced). */
void UiKit_EndFrame(void);
void UiKit_Shutdown(void);
/* FNV-1a */
uint32_t UiHash(const char *s);

#endif // GUARD_NATIVE_UI_KIT_H
