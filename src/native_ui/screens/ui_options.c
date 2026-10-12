#ifdef ENABLE_NATIVE_UI
/*
 * 11 · Options (the approved design's preview/js/screens/options.js): the
 * option sheet with its value boxes and arrows, the frame strip, the text
 * sample pane and the description box, showing the six options this game has
 * (text speed, battle scene, battle style, sound, button mode, frame). The
 * engine's option menu (option_menu.c) keeps running under it: its rows, its
 * cursor and values, what the buttons do, and saving the choices on the way
 * out. The design's other tabs belong to the PC's own settings menu (F10:
 * display, graphics, camera), which the second tab opens. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "main.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,
    HIT_ARROW_LEFT,
    HIT_ARROW_RIGHT,
    HIT_FRAME,
    HIT_PC_TAB,
};

#define LX 8
#define LY 56
#define LW 192
#define LH 228
#define RX 208
#define RY 56
#define RW 424
#define RH 228
#define VX 248
#define VW 160

static const uint32_t sFrameHex[20] = {0x48B070, 0x4C88E0, 0xE85848, 0xF0A830, 0x9070C8, 0x48A8A8, 0xC86090,
                                       0x8C7058, 0x6878A0, 0x58B048, 0xE07838, 0x4890C8, 0xB85870, 0xA8A050,
                                       0x7088C0, 0x50A080, 0xC07850, 0x8890A8, 0xD06888, 0x607890};
static const char *const sIconGameplay[] = {"#########", "#.......#", "#.##.##.#", "#.......#", "#########", "..##.....", ".#......."};
static const char *const sIconDisplay[] = {"#########", "#.......#", "#.......#", "#.......#", "#########", "...###...", ".#######."};
static const u8 sTextDelay[3] = {8, 4, 1}; /* text.c sTextSpeedFrameDelays: Slow, Mid, Fast */

static const u8 sLabel_TextSpeed[] = _("Text speed");
static const u8 sLabel_BattleScene[] = _("Battle scene");
static const u8 sLabel_BattleStyle[] = _("Battle style");
static const u8 sLabel_Sound[] = _("Sound");
static const u8 sLabel_ButtonMode[] = _("Button mode");
static const u8 sLabel_Frame[] = _("Frame");
static const u8 sLabel_Close[] = _("Close");
static const u8 *const sLabels[NATIVE_OPTION_ROW_COUNT] = {sLabel_TextSpeed, sLabel_BattleScene, sLabel_BattleStyle,
                                                           sLabel_Sound, sLabel_ButtonMode, sLabel_Frame, sLabel_Close};
static const u8 sValue_Slow[] = _("Slow");
static const u8 sValue_Mid[] = _("Mid");
static const u8 sValue_Fast[] = _("Fast");
static const u8 sValue_On[] = _("On");
static const u8 sValue_Off[] = _("Off");
static const u8 sValue_Shift[] = _("Shift");
static const u8 sValue_Set[] = _("Set");
static const u8 sValue_Mono[] = _("Mono");
static const u8 sValue_Stereo[] = _("Stereo");
static const u8 sValue_Normal[] = _("Normal");
static const u8 sValue_LR[] = _("LR");
static const u8 sValue_LA[] = _("L=A");

static const u8 sDesc_TextSpeed[] = _("How fast messages print: Slow 8, Mid 4 or Fast 1\nframe a letter.");
static const u8 sDesc_BattleScene[] = _("Battle animations: On plays every move's animation,\nOff skips them.");
static const u8 sDesc_BattleStyle[] = _("Shift offers a switch when a foe's Pokémon faints;\nSet doesn't.");
static const u8 sDesc_Sound[] = _("Emerald's own sound option: Mono or Stereo.");
static const u8 sDesc_ButtonMode[] = _("Normal; LR: L and R also turn pages in menus;\nL=A: L works as A.");
static const u8 sDesc_Frame[] = _("The frame of the game's own message boxes and windows,\nType 1 to Type 20.");
static const u8 sDesc_Close[] = _("Keep these options and go back.");
static const u8 *const sDescs[NATIVE_OPTION_ROW_COUNT] = {sDesc_TextSpeed, sDesc_BattleScene, sDesc_BattleStyle,
                                                          sDesc_Sound, sDesc_ButtonMode, sDesc_Frame, sDesc_Close};

static const u8 sText_Sample[] = _("Text appears at this speed.");
static const u8 sText_Own[] = _("These are Emerald's own options.");
static const u8 sText_SceneOn[] = _("Battle scene On: moves play");
static const u8 sText_SceneOff[] = _("Battle scene Off: moves skip");
static const u8 sText_SceneTail[] = _("their animations.");
static const u8 sText_StyleShift[] = _("Battle style Shift: you may");
static const u8 sText_StyleShiftTail[] = _("switch after a foe faints.");
static const u8 sText_StyleSet[] = _("Battle style Set: no switch");
static const u8 sText_StyleSetTail[] = _("offer after a faint.");
static const u8 sText_Mono[] = _("Sound Mono: one mixed channel.");
static const u8 sText_Stereo[] = _("Sound Stereo: left and right.");
static const u8 sText_Change[] = _("Change");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_Back[] = _("Back");

static struct NativeOptionMenu sOpt;
static u8 sPrevValues[NATIVE_OPTION_CANCEL];
static int sPress;     /* the arrow that moved last: -1, 1 */
static int sPressT;
static int sSampleT0;

static bool Detect(void)
{
    struct NativeOptionMenu o;

    return OptionMenu_NativeUiState(&o);
}

static void Capture(void)
{
    u8 prevCursor = sOpt.cursor;
    u8 prevPhase = sOpt.phase;

    OptionMenu_NativeUiState(&sOpt);
    if (sPressT > 0)
        sPressT--;
    if (prevPhase == sOpt.phase && sOpt.cursor == prevCursor && sOpt.cursor < NATIVE_OPTION_CANCEL
     && sOpt.values[sOpt.cursor] != sPrevValues[sOpt.cursor] && sOpt.phase == NATIVE_OPTION_INPUT)
    {
        /* which arrow: the next value or the one before (values wrap) */
        int count = sOpt.cursor == NATIVE_OPTION_TEXT_SPEED ? 3 : sOpt.cursor == NATIVE_OPTION_FRAME ? sOpt.frameCount
                  : sOpt.cursor == NATIVE_OPTION_BUTTON_MODE ? 3 : 2;
        int d = (sOpt.values[sOpt.cursor] - sPrevValues[sOpt.cursor] + count) % count;

        sPress = d == 1 ? 1 : -1;
        sPressT = 2;
        if (sOpt.cursor == NATIVE_OPTION_TEXT_SPEED)
            sSampleT0 = (int)NativeUi_Frame();
    }
    memcpy(sPrevValues, sOpt.values, sizeof(sPrevValues));
}

static const u8 *ValueText(int row, u8 *buf, int size)
{
    u8 v;

    if (row >= NATIVE_OPTION_CANCEL)
        return NULL;
    v = sOpt.values[row];

    switch (row)
    {
    case NATIVE_OPTION_TEXT_SPEED: return v == 0 ? sValue_Slow : v == 1 ? sValue_Mid : sValue_Fast;
    case NATIVE_OPTION_BATTLE_SCENE: return v ? sValue_Off : sValue_On;
    case NATIVE_OPTION_BATTLE_STYLE: return v ? sValue_Set : sValue_Shift;
    case NATIVE_OPTION_SOUND: return v ? sValue_Stereo : sValue_Mono;
    case NATIVE_OPTION_BUTTON_MODE: return v == 0 ? sValue_Normal : v == 1 ? sValue_LR : sValue_LA;
    case NATIVE_OPTION_FRAME:
        buf[0] = EOS;
        UiText_AppendAscii(buf, size, "Type ");
        UiText_AppendNumber(buf, size, v + 1, 1, false);
        return buf;
    default: return NULL;
    }
}

static void Tab(int tx, int w, bool on, const char *const *icon, int iconRows, const char *label, int hitId,
                const struct UiRamp *teal)
{
    const struct UiMask *m = UiM_Rounded4(w, 20, 3, 3, 3, 3);
    const struct UiMask *ic = UiM_Rows(icon, iconRows, "#");
    int y = on ? 31 : 32;
    int lw = UiCaps_Width(label), gx = tx + ((w - ic->w - 4 - lw) >> 1);

    if (on)
        UiG_Paint(m, tx, y, UI_HEX(0xF8F8F0), &(struct UiPaint){teal->ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 1, NULL, UI_NONE});
    else
        UiG_Paint(m, tx, y, teal->base, &(struct UiPaint){teal->ink, teal->hi, teal->lo, 0, 2, NULL, UI_NONE});
    UiG_Mask(ic, gx, y + 6 + ((7 - ic->h) >> 1), on ? teal->lo : UI_HEX(0xE8F4F4));
    UiCaps_Draw(gx + ic->w + 4, y + 7, label, on ? teal->ink : UI_P_TEXT_W, on ? UI_NONE : teal->ink,
                on ? UI_CAPS_PLAIN : UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    if (hitId != 0)
        UiG_Hit(hitId, 0, tx, 31, w, 22);
}

static void FrameStrip(int x, int y, const struct UiRamp *teal, int t)
{
    UiCaps_Draw(x, y, "FRAME TYPES", teal->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    (void)t;
    for (int i = 0; i < 20 && i < sOpt.frameCount; i++)
    {
        int fx = x + (i % 10) * 39, fy = y + 10 + (i / 10) * 24;
        struct UiRamp br = UiRampOf(UI_HEX(sFrameHex[i]), 0.42, 0.28, 0.62);
        const struct UiMask *m = UiM_Rounded4(34, 18, 2, 2, 2, 2);
        char num[4];

        if (i == sOpt.values[NATIVE_OPTION_FRAME])
            UiW_SelectFrame(m, fx, fy, UI_NONE, UI_NONE, 2);
        UiG_Paint(m, fx, fy, br.base, &(struct UiPaint){br.ink, br.hi, br.lo, 0, 0, NULL, UI_NONE});
        UiG_Paint(UiM_Rounded4(26, 10, 1, 1, 1, 1), fx + 4, fy + 4, UI_P_PAPER,
                  &(struct UiPaint){br.ink, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        snprintf(num, sizeof(num), "%d", i + 1);
        UiCaps_Draw(fx + 17, fy + 6, num, br.ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        UiG_Hit(HIT_FRAME, i, fx, fy, 34, 18);
    }
}

static void PreviewPane(int t, const struct UiRamp *teal)
{
    int px = LX + 10, py = LY + 26, pw = LW - 20;
    int frame = sOpt.values[NATIVE_OPTION_FRAME] % 20;
    struct UiRamp band = UiRampOf(UI_HEX(sFrameHex[frame]), 0.42, 0.28, 0.62);
    int mb[4];
    int delay = sTextDelay[sOpt.values[NATIVE_OPTION_TEXT_SPEED] < 3 ? sOpt.values[NATIVE_OPTION_TEXT_SPEED] : 1];
    int len = UiText_Length(sText_Sample);
    int cycle = len * delay + 60, ph = (t - sSampleT0 + cycle * 1000) % cycle;
    int n = ph / delay + 1;
    const u8 *starts[4];
    int lengths[4], lines;
    u8 shown[64];
    char caps[48];
    bool sceneOn = sOpt.values[NATIVE_OPTION_BATTLE_SCENE] == 0;
    bool shift = sOpt.values[NATIVE_OPTION_BATTLE_STYLE] == 0;

    if (n > len)
        n = len;
    UiG_Paint(UiM_Rounded4(LW, LH, 3, 3, 3, 3), LX, LY, UI_HEX(0xF4F8F8),
              &(struct UiPaint){teal->ink, UI_HEX(0xFFFFFF), UI_HEX(0xD0DCDC), 0, 0, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(LW - 6, 16, 2, 2, 0, 0), LX + 3, LY + 3, teal->lo,
              &(struct UiPaint){UI_NONE, teal->base, UI_NONE, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(LX + 9, LY + 8, "TEXT SAMPLE", UI_P_TEXT_W, teal->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiW_DialogBox(px - 4, py, pw + 8, 46, &band, UI_NONE, mb);
    UiText_CopyN(shown, sizeof(shown), sText_Sample, n);
    lines = UiText_Wrap(shown, mb[2], UI_FONT_NORMAL, starts, lengths, 4);
    for (int i = 0; i < lines && i < 2; i++)
    {
        u8 line[64];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiG_Text(mb[0] + 2, mb[1] - 1 + i * 16, line);
    }
    if (n >= len)
        UiW_DownArrow(mb[0] + mb[2] - 8, mb[1] + 24 + ((t / 16) % 2), UI_P_CURSOR, false);
    snprintf(caps, sizeof(caps), "%s: %d %s A LETTER",
             sOpt.values[NATIVE_OPTION_TEXT_SPEED] == 0 ? "SLOW" : sOpt.values[NATIVE_OPTION_TEXT_SPEED] == 1 ? "MID" : "FAST",
             delay, delay == 1 ? "FRAME" : "FRAMES");
    UiCaps_Draw(px, py + 56, caps, teal->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(caps, sizeof(caps), "TYPE %d FRAME", frame + 1);
    UiCaps_Draw(px, py + 70, caps, teal->lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiText_Draw(UI_FONT_SMALL, px, py + 92, sceneOn ? sText_SceneOn : sText_SceneOff, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px, py + 105, sText_SceneTail, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px, py + 124, shift ? sText_StyleShift : sText_StyleSet, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px, py + 137, shift ? sText_StyleShiftTail : sText_StyleSetTail, UI_P_TEXT, UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px, py + 156, sOpt.values[NATIVE_OPTION_SOUND] ? sText_Stereo : sText_Mono, UI_P_TEXT,
                UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, px, py + 178, sText_Own, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

static void Draw(int t)
{
    struct UiRamp teal = UiRampOf(UI_HEX(0x3A8E9C), 0.42, 0.28, 0.62);
    struct UiRamp tabRamp = {teal.hi, teal.lo, UiMix(teal.lo, teal.ink, 0.5), teal.ink};
    struct UiPattern dots = UiPat_Dots(8, 1, 0, 0, true);
    int bx, by, bw, bh;
    int rowH = 20, y0 = RY + 5, slots = (RH - 10) / rowH;
    int b[4];
    u8 vbuf[16];

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, UiMix(teal.lo, teal.base, 0.3));
    UiG_FillPattern(&dots, bx, by, bw, bh, UiMix(teal.base, teal.hi, 0.35), 0, 0);
    UiW_TitleTab(0, 8, "OPTIONS", &tabRamp, 0, NULL);
    Tab(8, 86, true, sIconGameplay, 7, "GAME", 0, &teal);
    Tab(8 + 89, 86, false, sIconDisplay, 7, "PC" UI_CAPS_DOT "F10", HIT_PC_TAB, &teal);

    /* the option sheet */
    UiG_Paint(UiM_Rounded4(RW, RH, 3, 3, 3, 3), RX, RY, UI_P_CREAM,
              &(struct UiPaint){teal.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    {
        const int corners[4][2] = {{RX + 3, RY + 3}, {RX + RW - 6, RY + 3}, {RX + 3, RY + RH - 6}, {RX + RW - 6, RY + RH - 6}};

        for (int i = 0; i < 4; i++)
        {
            UiG_Rect(corners[i][0], corners[i][1], 3, 3, UI_P_CREAM_LO);
            UiG_Put(corners[i][0] + 1, corners[i][1] + 1, UiMix(UI_P_CREAM_LO, teal.ink, 0.4));
        }
    }
    for (int es = NATIVE_OPTION_ROW_COUNT; es < slots; es++)
        if (es % 2)
            UiG_Rect(RX + 6, y0 + es * rowH + 1, RW - 12, rowH - 2, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.18));
    FrameStrip(RX + 18, y0 + NATIVE_OPTION_ROW_COUNT * rowH + 8, &teal, t);
    for (int i = 0; i < NATIVE_OPTION_ROW_COUNT; i++)
    {
        int ry = y0 + i * rowH, cy = ry + ((rowH - 16) >> 1);
        bool on = i == sOpt.cursor && sOpt.phase != NATIVE_OPTION_LOADING;
        const u8 *value = ValueText(i, vbuf, sizeof(vbuf));
        int vx = RX + VX;

        if (on)
            UiG_Paint(UiM_Rounded4(RW - 12, rowH, 2, 2, 2, 2), RX + 6, ry, UiMix(teal.hi, UI_HEX(0xFFFFFF), 0.6),
                      &(struct UiPaint){UiMix(teal.hi, teal.lo, 0.5), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        else if (i % 2)
            UiG_Rect(RX + 6, ry + 1, RW - 12, rowH - 2, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.3));
        UiG_Text(RX + 26, cy, sLabels[i]);
        if (on)
            UiW_Cursor(RX + 12, cy + 2, UiW_Nudge(t), UI_NONE);
        UiG_Hit(HIT_ROW, i, RX + 6, ry, RW - 12, rowH);
        if (value != NULL)
        {
            int pl = on && sPressT > 0 && sPress < 0 ? 1 : 0, pr = on && sPressT > 0 && sPress > 0 ? 1 : 0;

            UiG_Paint(UiM_Rounded4(VW - 28, 16, 2, 2, 2, 2), vx + 14, cy, UI_HEX(0xFFFFFF),
                      &(struct UiPaint){UiMix(UI_P_CREAM_LO, teal.ink, 0.35), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, UI_NONE});
            UiG_TextEx(vx + (VW >> 1), cy, value, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER);
            UiCaps_Draw(vx + 2, cy + 5 + pl, UI_CAPS_LEFT, on ? UI_P_CURSOR : teal.lo, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_LEFT, UI_NONE);
            UiCaps_Draw(vx + VW - 8, cy + 5 + pr, UI_CAPS_RIGHT, on ? UI_P_CURSOR : teal.lo, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_LEFT, UI_NONE);
            UiG_Hit(HIT_ARROW_LEFT, i, vx - 2, cy, 14, 16);
            UiG_Hit(HIT_ARROW_RIGHT, i, vx + VW - 12, cy, 14, 16);
        }
    }
    PreviewPane((int)NativeUi_Frame(), &teal);

    /* the description and the prompts */
    UiW_DialogBox(8, 290, 624, 46, NULL, UI_NONE, b);
    if (sOpt.cursor < NATIVE_OPTION_ROW_COUNT)
    {
        const u8 *starts[4];
        int lengths[4];
        int n = UiText_Wrap(sDescs[sOpt.cursor], b[2] - 4, UI_FONT_NORMAL, starts, lengths, 4);

        for (int i = 0; i < n && i < 2; i++)
        {
            u8 line[96];

            UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
            UiG_Text(b[0] + 2, b[1] - 1 + i * 15, line);
        }
    }
    {
        struct UiHintItem hints[3] = {{UI_ACT_MOVE, sText_Change}, {UI_ACT_SCROLL, sText_Choose}, {UI_ACT_CANCEL, sText_Back}};

        UiW_Hints(12, 341, hints, 3, true, 9);
    }
    UiCaps_Draw(632, 343, "KEPT WHEN YOU CLOSE", UiMix(teal.hi, UI_HEX(0xFFFFFF), 0.5), teal.ink, UI_CAPS_OUTLINE,
                UI_ALIGN_RIGHT, UI_NONE);
    UiG_Veil(NativeUi_ScreenFade());
}

/* presses that move the engine's cursor to a row (it wraps: the shorter way) */
static void MoveTo(int row)
{
    int n = NATIVE_OPTION_ROW_COUNT, moves = row - sOpt.cursor;

    if (moves > n / 2)
        moves -= n;
    else if (moves < -(n / 2))
        moves += n;
    NativeUi_QueuePresses(moves > 0 ? DPAD_DOWN : DPAD_UP, abs(moves));
}

static void PressF10(void)
{
    NativeUi_OpenPcMenu();
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (hit->id == HIT_PC_TAB)
    {
        PressF10();
        return true;
    }
    if (sOpt.phase != NATIVE_OPTION_INPUT || NativeUi_PressesPending())
        return true;
    switch (hit->id)
    {
    case HIT_ROW:
        MoveTo(hit->index);
        if (hit->index == NATIVE_OPTION_CANCEL)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    case HIT_ARROW_LEFT:
    case HIT_ARROW_RIGHT:
        MoveTo(hit->index);
        NativeUi_QueuePress(hit->id == HIT_ARROW_LEFT ? DPAD_LEFT : DPAD_RIGHT);
        return true;
    case HIT_FRAME:
    {
        int n = sOpt.frameCount, d = (hit->index - sOpt.values[NATIVE_OPTION_FRAME] + n) % n;

        MoveTo(NATIVE_OPTION_FRAME);
        if (d <= n / 2)
            NativeUi_QueuePresses(DPAD_RIGHT, d);
        else
            NativeUi_QueuePresses(DPAD_LEFT, n - d);
        return true;
    }
    }
    return false;
}

static bool Wheel(int dy)
{
    if (sOpt.phase == NATIVE_OPTION_INPUT && !NativeUi_PressesPending())
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static bool Ready(void)
{
    return sOpt.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Options = {
    .name = "options",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
