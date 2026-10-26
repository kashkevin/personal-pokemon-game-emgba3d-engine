#ifdef ENABLE_NATIVE_UI
/*
 * An Egg hatching (egg_hatch.c, CB2_EggHatch) on the design's own stage instead of the game's 240x160 screen: a warm
 * nursery version of the evolution screen's stage (stepped spotlight, rings, a twinkling lattice), the Egg itself as the
 * game shows it each frame (its own picture, whole or cracked, shaking as the game shakes it), its shards flying as the
 * game throws them, the white flash, the Pokemon growing out of it and playing its front animation, then the game's
 * message and the nickname's YES / NO in the design's boxes. The game keeps the scene: its steps, sounds, timing and
 * the naming screen it opens on YES. Clicks answer YES / NO; a right click is NO. The owner's request of 2026-10-26:
 * every screen and animation in the current interface. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "pokemon.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define CX 320          /* the stage's centre; the game's egg is at x 120 */
#define FEET 252        /* the stage's floor line: the Egg's bottom */
#define SPOT_Y 176
#define GBA_EGG_X 120   /* egg_hatch.c EGG_X, EGG_Y: the Egg's (and the Pokemon's) centre on the game's screen */
#define GBA_EGG_Y 75

enum
{
    HIT_STAGE = UI_HIT_SCREEN,
};

static const UiRGBA sSparklePal[3] = {UI_HEX(0xF8D890), UI_HEX(0xFFF0C8), UI_HEX(0xFFFFFF)};

static struct NativeEggHatch sH;
static u8 sMsg[256];
static bool sHasMsg, sMsgPrinting, sMsgWaiting;
static u32 sMsgPrinted;
static uint32_t sLastCapture, sAnimAt, sRevealAt;
static bool sWasAnim, sWasShown;
static int sGoal = -1;   /* a clicked YES / NO, then A */
static int sGoalSteps;

/* the game's screen (GBA pixels) on the stage, twice its size, the Egg's bottom on the floor line */
static int StageX(int x)
{
    return CX + (x - GBA_EGG_X) * 2;
}

static int StageY(int y)
{
    return FEET - 32 + (y - GBA_EGG_Y) * 2;
}

/* ---------------------------------------------------------------- capture */

static bool Detect(void)
{
    struct NativeEggHatch h;

    return EggHatch_NativeUiState(&h);
}

static void GoalStep(void)
{
    if (sGoal < 0)
        return;
    if (!sH.yesNo)
    {
        sGoal = -1;
        return;
    }
    if (!sH.ready || NativeUi_PressesPending())
        return;
    if (++sGoalSteps > 6)
        sGoal = -1;
    else if (sH.yesNoCursor == sGoal)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
    }
    else
        NativeUi_QueuePress(sGoal > sH.yesNoCursor ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    bool8 active = FALSE;

    if (!EggHatch_NativeUiState(&sH))
        return;
    if (now - sLastCapture != 1)
    {
        sWasAnim = sWasShown = false;
        sAnimAt = sRevealAt = 0;
        sGoal = -1;
    }
    sLastCapture = now;
    if (sH.monAnim && !sWasAnim)
        sAnimAt = now;
    sWasAnim = sH.monAnim;
    if (sH.monShown && !sWasShown)
        sRevealAt = now;
    sWasShown = sH.monShown;
    UiGfx_PrepareMonPic(sH.species, sH.personality, sH.shiny, false);
    sHasMsg = sH.state >= 5 && TextPrinter_NativeUiWindowText(sH.windowId, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sHasMsg && !sMsgPrinting && TextPrinter_NativeUiWindowWaiting(sH.windowId);
    GoalStep();
}

/* ---------------------------------------------------------------- the nursery stage */

static UiRGBA Dusk(void)
{
    return UiMix(UI_P_INK, UI_HEX(0xE89058), 0.28);
}

static void Nursery(int t)
{
    UiRGBA base = Dusk();
    UiRGBA warm = UI_HEX(0xF8C878), dot = UiMix(base, warm, 0.45), dim = UiMix(base, warm, 0.22);
    struct UiPattern stripe = UiPat_HStripe(4, 1, 0);
    static const int radii[4] = {170, 140, 110, 80};
    static const double tones[4] = {0.08, 0.14, 0.2, 0.26};
    int x0, y0, w, h, ph = (t / 16) % 2;

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, base);
    UiG_FillPattern(&stripe, x0, y0, w, h, UiMix(base, UI_P_INK, 0.25), 0, 0);
    /* a lattice of soft dots, neighbours twinkling out of phase */
    for (int yy = y0 - (((y0 % 32) + 32) % 32); yy < y0 + h; yy += 32)
        for (int xx = x0 - (((x0 % 32) + 32) % 32) - 32; xx < x0 + w; xx += 32)
        {
            int row = (int)lround(yy / 32.0), sx = xx + (row % 2 ? 16 : 0);
            bool on = ((row + (int)lround(xx / 32.0)) % 2 == 0) ? ph : !ph;

            UiG_Rect(sx + 1, yy + 1, on ? 3 : 2, on ? 3 : 2, on ? dot : dim);
        }
    /* the stepped spotlight on the Egg */
    for (int k = 0; k < 4; k++)
    {
        int rd = radii[k];

        UiG_Mask(UiM_Ellipse(rd * 2, (int)(rd * 1.5)), CX - rd, SPOT_Y - (int)(rd * 0.75), UiMix(base, warm, tones[k]));
    }
    /* the floor's rings and the Egg's shadow */
    for (int k = 5; k >= 1; k--)
    {
        int d = 80 + k * 40;

        UiG_Mask(UiM_Outline(UiM_Ellipse(d, (int)(d * 0.3))), CX - (d >> 1), FEET - (int)(d * 0.15),
                 UiMix(base, warm, 0.16 + 0.05 * (5 - k)));
    }
    UiG_Mask(UiM_Ellipse(90, 16), CX - 45, FEET - 8, UiMix(base, UI_P_INK, 0.45));
}

/* ---------------------------------------------------------------- the Egg, its shards, the Pokemon */

struct EggCtx
{
    const u8 *tiles;
    const u16 *palette;
};

static void BuildEgg(struct UiBitmap *bm, const void *ctx)
{
    const struct EggCtx *c = ctx;

    UiGfx_Decode4bpp(bm->d, 32, c->tiles, c->palette, 32, 32);
}

static void Egg(void)
{
    struct EggCtx c = {sH.eggTiles, sH.eggPalette};
    uint32_t hash = 2166136261u;
    char key[48];

    if (!sH.eggShown)
        return;
    /* the picture as the game shows it now: whole, then each crack */
    for (u32 i = 0; i < sizeof(sH.eggTiles); i++)
        hash = (hash ^ sH.eggTiles[i]) * 16777619u;
    for (int i = 0; i < 16; i++)
        hash = (hash ^ sH.eggPalette[i]) * 16777619u;
    snprintf(key, sizeof(key), "egghatch:%08x", (unsigned)hash);
    UiG_BlitPart(UiG_Cached(key, 32, 32, BuildEgg, &c), 0, 0, 32, 32, StageX(sH.eggX) - 32, StageY(sH.eggY) - 32, 2,
                 false, 1.0);
}

/* a shard of shell: a chip in the shell's colours, turned by the game's four shard pictures */
static void Shards(void)
{
    UiRGBA shell = UI_HEX(0xF8F0D8), rim = UI_HEX(0xB8A078);

    for (int i = 0; i < sH.shardCount; i++)
    {
        int x = StageX(sH.shards[i].x), y = StageY(sH.shards[i].y);

        switch (sH.shards[i].frame & 3)
        {
        case 0:
            UiG_Rect(x - 4, y - 2, 8, 4, rim);
            UiG_Rect(x - 3, y - 1, 6, 2, shell);
            break;
        case 1:
            UiG_Rect(x - 2, y - 4, 4, 8, rim);
            UiG_Rect(x - 1, y - 3, 2, 6, shell);
            break;
        case 2:
            UiG_Rect(x - 3, y - 3, 6, 6, rim);
            UiG_Rect(x - 2, y - 2, 4, 4, shell);
            break;
        default:
            UiG_Rect(x - 4, y - 1, 6, 3, rim);
            UiG_Rect(x - 2, y - 3, 3, 3, rim);
            UiG_Rect(x - 3, y, 4, 1, shell);
            UiG_Rect(x - 1, y - 2, 1, 2, shell);
            break;
        }
    }
}

static void Mon(int t)
{
    int frame = 0;

    if (!sH.monShown)
        return;
    if (sAnimAt != 0)
        frame = UiGfx_MonPicFrameAt(sH.species, sH.personality, sH.shiny, false, (int)(NativeUi_Frame() - sAnimAt));
    UiGfx_MonPicAffine(sH.species, sH.personality, sH.shiny, frame, StageX(sH.monX), StageY(sH.monY), sH.monAffine, 2,
                       NULL);
    /* a few sparkles once it is out */
    if (sRevealAt != 0 && NativeUi_Frame() - sRevealAt > 40 && !sH.yesNo)
    {
        int k = (t / 20) % 3;

        UiMon_SparkleScaled(CX - 76, FEET - 130 + k * 6, true, sSparklePal, 2);
        UiMon_SparkleScaled(CX + 80, FEET - 104 - k * 4, false, sSparklePal, 2);
        UiMon_SparkleScaled(CX + 58, FEET - 160, k == 1, sSparklePal, 2);
    }
}

/* ---------------------------------------------------------------- the message, YES / NO */

static void Message(int t)
{
    int b[4];

    if (!sHasMsg)
        return;
    UiW_DialogBox(8, 296, 624, 58, NULL, UI_NONE, b);
    UiW_MessagePageEx(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 4, b[1], b[2] - 8, 2, 18);
    if (sMsgWaiting && !sH.yesNo)
        UiW_DownArrow(610, 336 + ((t / 16) % 2), UI_P_CURSOR, false);
    if (sH.yesNo)
        UiW_YesNo(632, 292, sH.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
}

static void Draw(int t)
{
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Hit(HIT_STAGE, 0, bx, by, bw, bh);
    Nursery(t);
    Egg();
    Mon(t);
    Shards();
    Message(t);
    /* the game's fades: in from black, the white flash as it hatches, out to black */
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sH.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (!sH.yesNo)
        return true;
    if (button == 3)
    {
        sGoal = -1;
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button == 1 && hit->id == UI_HIT_YESNO)
    {
        sGoal = hit->index;
        sGoalSteps = 0;
        GoalStep();
    }
    return true;
}

static bool Wheel(int dy)
{
    if (!sH.yesNo || !sH.ready || dy == 0 || NativeUi_PressesPending())
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_EggHatch = {
    .name = "egg_hatch",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
