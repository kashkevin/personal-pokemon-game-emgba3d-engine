#ifndef GUARD_NATIVE_UI_ITEM_DETAIL_H
#define GUARD_NATIVE_UI_ITEM_DETAIL_H

/*
 * 16 · Item details (screens/ui_item_detail.c): the design's display case for
 * one item, opened from the bag (Tab, or its Details prompt). It is the UI's
 * own view: the game has no such screen, so while it is up the bag gets no
 * buttons and nothing in the game changes. Everything it shows is the game's
 * own data (prices, description, uses from the item's field / battle / hold
 * data, TM moves and the party's real compatibility, Mega forms).
 */
#include <stdbool.h>
#include <stdint.h>

#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"

/* open on the bag's item at index (its pocket's list) */
void UiItemDetail_Open(const struct NativeBag *bag, int index);
void UiItemDetail_Close(void);
bool UiItemDetail_IsOpen(void);
/* the list index of the item shown last (the bag goes back to it) */
int UiItemDetail_Index(void);
/* at the frame boundary (the game parked): follows the bag's list */
void UiItemDetail_Capture(const struct NativeBag *bag);
/* false: nothing to show yet (the bag draws itself) */
bool UiItemDetail_Draw(int t);
bool UiItemDetail_Click(const struct UiHit *hit, int button);
bool UiItemDetail_Wheel(int dy);
/* the game's buttons while it is up: they browse and close it, and none reaches the game */
uint16_t UiItemDetail_FilterKeys(uint16_t keys, uint16_t newKeys);

#endif // GUARD_NATIVE_UI_ITEM_DETAIL_H
