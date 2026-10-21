#ifndef GUARD_NATIVE_UI_TOWER_H
#define GUARD_NATIVE_UI_TOWER_H

/*
 * Battle Tower: Legends' pane (screens/ui_tower.c): drawn in the Players
 * screen's link device (screens/ui_players.c) while the host's page names a
 * Tower screen (NativeMpMenu towerStyle, not 0). Captured at the frame
 * boundary, drawn into the device's LCD and card.
 */
#include <stdint.h>

struct NativeMpMenu;

/* the page (its style, argument and rows: a floor map highlights the floors its rows list) */
void UiTower_Capture(const struct NativeMpMenu *menu);
void UiTower_Lcd(int t, int x, int y, int w, int h);
void UiTower_Card(int t, int x, int y, int w, int h);

#endif // GUARD_NATIVE_UI_TOWER_H
