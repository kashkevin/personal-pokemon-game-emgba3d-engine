#ifdef ENABLE_NATIVE_UI
/*
 * 25 · Evolution (the approved design's preview/js/screens/evolution.js): an
 * original night stage over the engine's evolution scene (evolution_scene.c),
 * which keeps every rule, message, sound and button. Its states set the
 * picture: the Pokémon as it is while "What? X is evolving!" prints, sparkles
 * circling it while the music starts, the old form (in colour) alternating
 * with the new one (a pale silhouette) as fast as the engine alternates them
 * (the larger of its two growing and shrinking sprites is the one in front;
 * blinks only, no zooms), the new form white in a burst of sparkles, its
 * colours returning as the engine's flash fades, then the new form itself.
 * B held stops an evolution the engine lets stop ("Huh? X stopped
 * evolving!"); a click or a right click is that B while it alternates. A new
 * move is the engine's: its messages, its YES / NO (clicks and the wheel move
 * the engine's cursor) and the summary's move choice (screen 3). The
 * evolution inside a trade keeps the game's picture (the trade's own
 * sequence). Project-owned.
 */
#include <math.h>
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

enum
{
    HIT_STAGE = UI_HIT_SCREEN,
};

#define CX 320     /* the Pokémon's feet, the stage's centre */
#define FEET 272
#define SPOT_Y 196 /* the spotlight's centre */

static const char *const sStarA[5] = {"..a..", "..b..", "abcba", "..b..", "..a.."};
static const char *const sStarB[5] = {".....", "..a..", ".aba.", "..a..", "....."};
static const UiRGBA sSparklePal[3] = {UI_HEX(0x9CC8F0), UI_HEX(0xE0F0FF), UI_HEX(0xFFFFFF)};

static const u8 sText_Stop[] = _("Stop");

/* ---------------------------------------------------------------- captured state */

static struct NativeEvolution sE;
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sMsgWaiting;
static uint32_t sLastCapture;
static uint32_t sPhaseAt;   /* the frame this phase began (charging and alternating share one clock) */
static uint32_t sAnimAt;    /* the frame the front Pokémon's animation began */
static u8 sWasPhase = 0xFF;
static bool sWasAnim;
static int sGoal = -1;      /* a clicked YES / NO, then A */
static int sGoalSteps;

static bool Detect(void)
{
    struct NativeEvolution e;

    return Evolution_NativeUiState(&e);
}

static void GoalStep(void)
{
    if (sGoal < 0)
        return;
    if (!sE.yesNo)
    {
        sGoal = -1;
        return;
    }
    if (!sE.ready || NativeUi_PressesPending())
        return;
    if (++sGoalSteps > 6)
        sGoal = -1;
    else if (sE.yesNoCursor == sGoal)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal = -1;
    }
    else
        NativeUi_QueuePress(sGoal > sE.yesNoCursor ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    bool8 active;

    if (!Evolution_NativeUiState(&sE))
        return;
    if (now - sLastCapture != 1)
    {
        sWasPhase = 0xFF;
        sWasAnim = false;
        sGoal = -1;
        sAnimAt = 0;
    }
    sLastCapture = now;
    if (sE.phase != sWasPhase)
    {
        NativeUi_Logf("evolution phase %d at frame %u", sE.phase, (unsigned)now);
        if (!(sE.phase == NATIVE_EVO_CYCLING && sWasPhase == NATIVE_EVO_CHARGING))
            sPhaseAt = now;
        sWasPhase = sE.phase;
    }
    if (sE.monAnim && !sWasAnim)
        sAnimAt = now;
    sWasAnim = sE.monAnim;
    if (sE.phase != NATIVE_EVO_LOADING)
    {
        UiGfx_PrepareMonPic(sE.preSpecies, sE.personality, sE.shiny, false);
        UiGfx_PrepareMonPic(sE.postSpecies, sE.personality, sE.shiny, false);
    }
    sHasMsg = sE.msgValid && TextPrinter_NativeUiWindowText(0, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
    sMsgPrinting = sHasMsg && active;
    sMsgWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(0);
    GoalStep();
}

/* ---------------------------------------------------------------- the night stage */

static bool Active(void)
{
    return sE.phase == NATIVE_EVO_CHARGING || sE.phase == NATIVE_EVO_CYCLING || sE.phase == NATIVE_EVO_FLASH;
}

static int PhaseFrames(void)
{
    return (int)(NativeUi_Frame() - sPhaseAt);
}

static UiRGBA Night(void)
{
    return UiMix(UI_P_INK, UI_P_TYPES_WATER, 0.22);
}

static void NightSky(int t)
{
    UiRGBA base = Night();
    UiRGBA c1 = UiMix(base, UI_P_TYPES_ICE, 0.5), c2 = UiMix(base, UI_P_PAPER, 0.55), c3 = UiMix(base, UI_P_PAPER, 0.85);
    struct UiPixPal lit[3] = {{'a', c1}, {'b', c2}, {'c', c3}};
    struct UiPixPal dim[2] = {{'a', UiMix(base, UI_P_TYPES_ICE, 0.3)}, {'b', c1}};
    struct UiPattern stripe = UiPat_HStripe(4, 1, 0);
    int ph = (t / 16) % 2, x0, y0, w, h;
    static const int radii[4] = {170, 140, 110, 80};
    static const double tones[4] = {0.08, 0.14, 0.2, 0.26};

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, base);
    UiG_FillPattern(&stripe, x0, y0, w, h, UiMix(base, UI_P_INK, 0.25), 0, 0);
    /* twinkling lattice: two tones swap every 16 frames, neighbours out of phase */
    for (int yy = y0 - (((y0 % 32) + 32) % 32); yy < y0 + h; yy += 32)
        for (int xx = x0 - (((x0 % 32) + 32) % 32) - 32; xx < x0 + w; xx += 32)
        {
            int row = (int)lround(yy / 32.0), sx = xx + (row % 2 ? 16 : 0);
            int on = ((row + (int)lround(xx / 32.0)) % 2 == 0) ? ph : 1 - ph;

            if (on)
                UiW_Pix("evstar1", sStarA, 5, lit, 3, sx, yy, false);
            else
                UiW_Pix("evstar0", sStarB, 5, dim, 2, sx, yy, false);
        }
    /* the stepped spotlight; its bands turn while it evolves (palette steps, no gradient) */
    for (int k = 0; k < 4; k++)
    {
        int rd = radii[k], tk = Active() ? (k + PhaseFrames() / 6) % 4 : k;

        UiG_Mask(UiM_Ellipse(rd * 2, (int)(rd * 1.5)), CX - rd, SPOT_Y - (int)(rd * 0.75), UiMix(base, UI_P_TYPES_ICE, tones[tk]));
    }
}

static void StageRings(void)
{
    UiRGBA base = Night();

    for (int k = 5; k >= 1; k--)
    {
        int d = 80 + k * 40;
        bool on = Active() && (PhaseFrames() / 6) % 5 == k - 1;
        UiRGBA c = on ? UiMix(UI_P_TYPES_ICE, UI_P_PAPER, 0.4) : UiMix(base, UI_P_TYPES_ICE, 0.18 + 0.06 * (5 - k));

        UiG_Mask(UiM_Outline(UiM_Ellipse(d, (int)(d * 0.3))), CX - (d >> 1), FEET - (int)(d * 0.15), c);
    }
    UiG_Mask(UiM_Ellipse(100, 18), CX - 50, FEET - 9, UiMix(base, UI_P_INK, 0.45));
}

/* ---------------------------------------------------------------- the Pokémon */

static void MonAt(u16 species, int frame, const struct UiFilter *filter, double alpha)
{
    UiGfx_MonPicEx(species, sE.personality, sE.shiny, false, frame, CX, FEET, 2, alpha, filter);
}

static int AnimFrame(u16 species)
{
    if (sAnimAt == 0)
        return 0;
    return UiGfx_MonPicFrameAt(species, sE.personality, sE.shiny, false, (int)(NativeUi_Frame() - sAnimAt));
}

static void Spiral(int f)
{
    int rad = 120 - 4 * (f / 16), cy = FEET - 70;

    if (rad < 40)
        rad = 40;
    for (int i = 0; i < 12; i++)
    {
        double ang = (i * 30 + (f / 2) * 9) * M_PI / 180.0;
        int rr = rad + (i % 3) * 10;

        UiMon_SparkleScaled(CX + (int)lround(cos(ang) * rr), cy + (int)lround(sin(ang) * rr * 0.7), i % 2 == 0, sSparklePal, 2);
    }
}

static void Burst(int f)
{
    /* after 12 frames of white: 16 sparkles bursting 8 px every 2 frames, a burst every 24 frames */
    int k, br, cy = FEET - 70;

    if (f < 12)
        return;
    k = (f - 12) % 24;
    if (k >= 12)
        return;
    br = 24 + 8 * (k / 2);
    for (int j = 0; j < 16; j++)
    {
        double a = j * M_PI / 8;

        UiMon_SparkleScaled(CX + (int)lround(cos(a) * br), cy + (int)lround(sin(a) * br * 0.7), j % 2 == 0, sSparklePal, 2);
    }
}

static void Mon(void)
{
    struct UiFilter pale = {0, 0, 0, 0, UiMix(UI_P_TYPES_ICE, UI_P_PAPER, 0.75)};
    struct UiFilter white = {0, 0, 0, 0, UI_HEX(0xF8F8F8)};
    u16 shown = sE.showNew ? sE.postSpecies : sE.preSpecies;
    int f = PhaseFrames();

    switch (sE.phase)
    {
    case NATIVE_EVO_CHARGING:
        MonAt(sE.preSpecies, 0, NULL, 1.0);
        Spiral(f);
        return;
    case NATIVE_EVO_CYCLING:
        if (sE.showNew)
            MonAt(sE.postSpecies, 0, &pale, 1.0);
        else
            MonAt(sE.preSpecies, 0, NULL, 1.0);
        Spiral(f);
        return;
    case NATIVE_EVO_FLASH:
        if (sE.white < 16)
            MonAt(sE.postSpecies, 0, NULL, 1.0);
        MonAt(sE.postSpecies, 0, &white, sE.white / 16.0);
        if (sE.white >= 16)
            Burst(f);
        return;
    default:
        break;
    }
    MonAt(shown, AnimFrame(shown), NULL, 1.0);
    if (sE.white > 0)
        MonAt(shown, AnimFrame(shown), &white, sE.white / 16.0);
    if (sE.phase == NATIVE_EVO_EVOLVED && !sE.yesNo && !sE.learning)
    {
        UiMon_SparkleScaled(CX - 70, FEET - 120, true, sSparklePal, 2);
        UiMon_SparkleScaled(CX + 76, FEET - 90, false, sSparklePal, 2);
        UiMon_SparkleScaled(CX + 54, FEET - 150, true, sSparklePal, 2);
    }
}

/* ---------------------------------------------------------------- the message, the prompts */

static void Message(int t)
{
    int b[4];

    UiW_DialogBox(8, 296, 624, 58, NULL, UI_NONE, b);
    if (sHasMsg)
        UiW_MessagePageEx(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 4, b[1], b[2] - 8, 2, 18);
    if (sMsgWaiting && !sE.yesNo)
        UiW_DownArrow(610, 336 + ((t / 16) % 2), UI_P_CURSOR, false);
    if (sE.phase == NATIVE_EVO_CYCLING && sE.canStop && sE.cycleActive)
    {
        struct UiHintItem h[1] = {{UI_ACT_CANCEL, sText_Stop}};

        UiW_Hints(486, 280, h, 1, true, 10);
    }
    if (sE.yesNo)
        UiW_YesNo(632, 292, sE.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
}

static void Draw(int t)
{
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    if (sE.phase == NATIVE_EVO_LOADING)
    {
        /* the screen before fades to black: black, then the stage fades in */
        UiG_Rect(bx, by, bw, bh, UI_HEX(0x000000));
        return;
    }
    UiG_Hit(HIT_STAGE, 0, bx, by, bw, bh);
    NightSky(t);
    StageRings();
    Mon();
    Message(t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sE.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        /* B: NO, the next message, or stopping the evolution */
        sGoal = -1;
        if (sE.ready)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (sE.yesNo)
    {
        if (hit->id == UI_HIT_YESNO)
        {
            sGoal = hit->index;
            sGoalSteps = 0;
            GoalStep();
        }
        return true;
    }
    if (!sE.ready)
        return true;
    /* the design: a click while it alternates stops it; else it is A */
    if (sE.phase == NATIVE_EVO_CYCLING)
        NativeUi_QueuePress(B_BUTTON);
    else if (sMsgWaiting)
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static bool Wheel(int dy)
{
    if (!sE.yesNo || !sE.ready || dy == 0 || NativeUi_PressesPending())
        return false;
    NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Evolution = {
    .name = "evolution",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
