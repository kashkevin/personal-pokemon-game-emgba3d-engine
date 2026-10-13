#ifdef ENABLE_NATIVE_UI
/*
 * The Items group's shared pieces (the design's PK.items in
 * preview/js/screens/shop.js): money, price tags, signs in big Route Caps, the
 * Game Corner coin. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "item.h"
#include "move.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

void UiItems_Money(u8 *dst, int size, u32 amount)
{
    UiText_FromAscii(dst, size, "$");
    UiText_AppendNumber(dst, size, (long)amount, 1, true);
}

void UiItems_ItemName(u8 *dst, int size, u16 item)
{
    dst[0] = EOS;
    UiText_Append(dst, size, GetItemName(item));
    if (GetItemPocket(item) == POCKET_TM_HM)
    {
        UiText_AppendAscii(dst, size, " ");
        UiText_Append(dst, size, GetMoveName(GetItemTMHMMoveId(item)));
    }
}

struct BigCapsCtx
{
    const struct UiMask *m;
    UiRGBA fg;
    UiRGBA ink;
};

static void BuildBigCaps(struct UiBitmap *bm, const void *ctx)
{
    const struct BigCapsCtx *c = ctx;
    const struct UiMask *big = UiM_Pad(c->m, 1, 1, 1, 1);

    if (c->ink != UI_NONE)
        UiBitmap_FillMask(bm, UiM_Sub(UiM_Dilate8(big), big), 0, 0, c->ink);
    UiBitmap_FillMask(bm, c->m, 1, 1, c->fg);
}

int UiItems_BigCaps(int x, int y, const char *text, int scale, UiRGBA fg, UiRGBA ink, int align)
{
    struct BigCapsCtx c = {UiCaps_Mask(text, false), fg, ink};
    int w = c.m->w + 2, h = c.m->h + 2, tw = (w + 1) * scale;
    char key[96];

    if (align == UI_ALIGN_CENTER)
        x -= tw / 2;
    else if (align == UI_ALIGN_RIGHT)
        x -= tw;
    snprintf(key, sizeof(key), "bigcaps:%08x:%d:%08x:%08x", (unsigned)c.m->hash, (int)c.m->w, (unsigned)fg, (unsigned)ink);
    UiG_BlitPart(UiG_Cached(key, w + 1, h + 1, BuildBigCaps, &c), 0, 0, w + 1, h + 1, x, y, scale, false, 1.0);
    return x + tw;
}

int UiItems_PriceTag(int x, int y, int w, const u8 *text, UiRGBA ink, bool dim)
{
    int h = 15, c = (h - 1) >> 1;
    UiRGBA body = dim ? UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.7) : UI_P_PAPER;

    UiG_Paint(UiM_Rounded(w, h, UiCornerChamfer(c), UiCornerR(2), UiCornerChamfer(c), UiCornerR(2)), x, y, body,
              &(struct UiPaint){ink, dim ? UI_NONE : UI_HEX(0xFFFFFF), UiMix(body, UI_P_PAPER_LO, 0.8), 0, 0, NULL, UI_NONE});
    UiG_Rect(x + 8, y + 6, 3, 3, UiMix(ink, body, 0.35));
    UiG_Put(x + 9, y + 7, UiMix(ink, body, 0.65));
    UiG_HLine(x + 8, y + 9, 3, UI_HEX(0xFFFFFF));
    UiText_Draw(UI_FONT_NORMAL, x + w - 5, y - 2, text, dim ? UI_HEX(0x909090) : UI_P_TEXT, dim ? UI_NONE : UI_P_TEXT_SH,
                UI_ALIGN_RIGHT, 0);
    return x + w;
}

void UiItems_TagString(int x0, int y0, int x1, int y1, UiRGBA c)
{
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);

    if (n < 1)
        n = 1;
    for (int s = 0; s <= n; s++)
        UiG_Put(x0 + (int)((x1 - x0) * (double)s / n + (x1 >= x0 ? 0.5 : -0.5)),
                y0 + (int)((y1 - y0) * (double)s / n + (y1 >= y0 ? 0.5 : -0.5)), c);
}

void UiItems_MoneyBox(int x, int y, int w, const char *label, const u8 *value, UiRGBA ink)
{
    UiG_Paint(UiM_Rounded4(w, 22, 2, 2, 2, 2), x, y, UI_P_PAPER, &(struct UiPaint){ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + 7, y + 8, label, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_TextEx(x + w - 7, y + 2, value, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
}

int UiItems_Coin(int x, int y)
{
    static const char *const rows[9] = {
        "..aaaaa..", ".abbbbba.", "abbcccbba", "abcbbbdba", "abcbbbdba", "abcbbbdba", "abbdddbba", ".abbbbba.", "..aaaaa..",
    };
    struct UiRamp r = UiRampOf(UI_P_POCKETS_KEY, 0.5, 0.25, 0.62);
    struct UiPixPal pal[4] = {{'a', r.ink}, {'b', r.base}, {'c', UiMix(r.hi, UI_P_PAPER, 0.5)}, {'d', r.lo}};

    return UiW_Pix("gc_coin", rows, 9, pal, 4, x, y, false);
}
#endif // ENABLE_NATIVE_UI
