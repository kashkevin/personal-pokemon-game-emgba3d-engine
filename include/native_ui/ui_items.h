#ifndef GUARD_NATIVE_UI_ITEMS_H
#define GUARD_NATIVE_UI_ITEMS_H

/*
 * The Items group's shared pieces (PK.items in the design's preview/js/screens/shop.js):
 * the money format, the money window, luggage-style price tags and their
 * strings, Route Caps at a whole scale for signs and marquees, the Game Corner
 * coin. Used by the Poké Mart (ui_shop.c) and the Game Corner (ui_game_corner.c).
 */
#include <stdbool.h>

#include "global.h"
#include "native_ui/ui_kit.h"

/* "¥1,234" in engine bytes */
void UiItems_Money(u8 *dst, int size, u32 amount);
/* an item's name as the bag lists it: a TM or HM with its move */
void UiItems_ItemName(u8 *dst, int size, u16 item);
/* Route Caps at a whole scale with an ink ring (UI_NONE: none); x is the left,
 * centre or right edge by align (enum UiAlign); returns the right edge */
int UiItems_BigCaps(int x, int y, const char *text, int scale, UiRGBA fg, UiRGBA ink, int align);
/* a luggage-style price tag pointing left with a punched hole; returns the right edge */
int UiItems_PriceTag(int x, int y, int w, const u8 *text, UiRGBA ink, bool dim);
/* a 1 px string from a point to a tag's hole */
void UiItems_TagString(int x0, int y0, int x1, int y1, UiRGBA c);
/* the Gen 3 money window: a caps label, the value right-aligned */
void UiItems_MoneyBox(int x, int y, int w, const char *label, const u8 *value, UiRGBA ink);
/* the original 9x9 Game Corner coin; returns the right edge */
int UiItems_Coin(int x, int y);

#endif // GUARD_NATIVE_UI_ITEMS_H
