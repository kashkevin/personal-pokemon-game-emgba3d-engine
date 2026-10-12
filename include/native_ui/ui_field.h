#ifndef GUARD_NATIVE_UI_FIELD_H
#define GUARD_NATIVE_UI_FIELD_H

/*
 * The field overlays' shared pieces (ui_field_text.c): the message box at the
 * bottom of the field and its prompts, as the pause menu's help box sits, for
 * the field's messages and the PC's first menus.
 */
#include <stdbool.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_widgets.h"

#define UI_FIELD_MSG_H 46
#define UI_FIELD_MSG_W 456
/* the menus over it are anchored bottom right at (R - 8, B - UI_FIELD_MENU_GAP) */
#define UI_FIELD_MENU_GAP 62

/* The message box (L, R, B: the canvas's edges): msg (engine text; NULL: empty)
 * up to printed bytes (< 0: all), the down arrow while waiting, its band (NULL:
 * the field's green); the prompts' panel beside it when there are hints. The
 * box is a hit region hitId (0: none). */
void UiField_MessageBox(int t, int L, int R, int B, const u8 *msg, int printed, bool waiting,
                        const struct UiRamp *band, const struct UiHintItem *hints, int hintCount, UiRGBA panelInk,
                        int hitId);

#endif // GUARD_NATIVE_UI_FIELD_H
