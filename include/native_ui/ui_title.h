#ifndef GUARD_NATIVE_UI_TITLE_H
#define GUARD_NATIVE_UI_TITLE_H

/*
 * The title's pieces the main menu shares (src/native_ui/screens/ui_title.c):
 * the remaster background on the title's clock and the EMERALD wordmark.
 */
#include "native_ui/ui_kit.h"

/* glint: the gem glint's step (0..11, -1 none), or this to time it by t */
#define UI_TITLE_NO_GLINT_GIVEN (-1000)

void UiTitle_Wordmark(int x, int y, int t, int glint);
UiRGBA UiTitle_GemInk(void);
/* frames since the title opened (with the time since the last 60 Hz step) */
double UiTitle_Clock(void);
/* the remaster background over the whole canvas (version C on the title's clock) */
void UiTitle_DrawBackground(void);

#endif // GUARD_NATIVE_UI_TITLE_H
