#ifndef GUARD_NATIVE_UI_TITLE_BG_H
#define GUARD_NATIVE_UI_TITLE_BG_H

/*
 * The Emerald title remaster's background for the native UI: versions A
 * (faithful), B (enhanced) and C (cinematic: an intro, then B's loop). A port
 * of the approved design's GPU title renderer (artifacts/ui-art-direction/
 * preview/js/title_emerald_gl.js, through its native benchmark native/
 * title_bench): one shader pass at the UI's logical canvas size, shown at the
 * UI's whole-number scale. The layers are made from the game's own title art.
 */
#include <stdbool.h>

#define UI_TITLE_LAYER_COUNT 11
/* version C: the intro ends here (title_emerald.js INTRO.end); the loop follows */
#define UI_TITLE_INTRO_END 404

struct UiTitleLayerPng
{
    const char *name;
    const unsigned char *data;
    unsigned size;
};
extern const struct UiTitleLayerPng gUiTitleLayerPngs[UI_TITLE_LAYER_COUNT];
extern const int gUiTitleStageCount;
extern const double gUiTitleStages[][7];
extern const int gUiTitleSparkCount;
extern const double gUiTitleSparks[][5];

/* What the interface over the background shows at a frame (title_emerald.js ui()). */
struct UiTitleUi
{
    bool logo;      /* the wordmark is shown */
    int glint;      /* the gem glint's step 0..11, or -1 */
    double press;   /* opacity of PRESS START, its hint and the corner lines */
    bool blinkOn;   /* PRESS START is in the visible part of its blink */
    double white;   /* the white veil over everything (C: fade-in and flash) */
};
/* version 'a', 'b' or 'c'; f: frames since the title opened */
void UiTitleBg_Ui(char version, int f, struct UiTitleUi *out);

/* Draws the background over the whole UI canvas (UiDraw_View) at frame f (B
 * and C may be fractional: smooth between 60 Hz steps), straight into the
 * framebuffer bound for the UI, before the UI's own quads. FALSE: not drawn
 * (no GL resources); the caller then draws a plain fill. */
bool UiTitleBg_Draw(char version, double f);
void UiTitleBg_Shutdown(void);

#endif // GUARD_NATIVE_UI_TITLE_BG_H
