#ifdef ENABLE_NATIVE_UI
/*
 * The Hall of Fame (not one of the 31 designed screens; the design's own
 * pieces): the induction after the Champion (hall_of_fame.c) over the engine's
 * own screen. A hall of gold light under a night sky; the team stepping up one
 * Pokémon at a time where the engine moves each picture (twice the GBA's
 * screen, its pixels square, as it grows into place), the ones before dimmed
 * as the game tints them; each Pokémon's line on the design's card (its
 * number, name and kind, level and ID No.); "Welcome to the HALL OF FAME!"
 * with the confetti for as long as the game throws it; the player stepping up
 * with their record (name, ID No., time) and "LEAGUE CHAMPION!". A goes on, as
 * in the game (a click too). Project-owned.
 */
#include <math.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_field.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_HALL = UI_HIT_SCREEN,
};

/* the GBA's screen twice its size, in the middle of the canvas */
#define GBA_X(x) (320 + ((x) - 120) * 2)
#define GBA_Y(y) (180 + ((y) - 80) * 2)
#define CONFETTI_EVERY 4
#define CONFETTI_FALL 110

static const u8 sText_Continue[] = _("Continue");
static const u8 sText_Name[] = _("NAME");
static const u8 sText_IdNo[] = _("IDNo.");
static const u8 sText_Time[] = _("TIME");

static struct NativeHof sH;
static uint32_t sWelcomeAt, sLastFrame;

static const struct UiRamp sGold = {UI_HEX(0xFFF0B8), UI_HEX(0xE8C060), UI_HEX(0xB08828), UI_HEX(0x3C2808)};

/* ---------------------------------------------------------------- detection */

static bool Detect(void)
{
    struct NativeHof h;

    return HallOfFame_NativeUiState(&h);
}

static void Capture(void)
{
    uint32_t now = NativeUi_Frame();
    u8 before = sH.phase;

    HallOfFame_NativeUiState(&sH);
    for (int i = 0; i < 6; i++)
        if (sH.mons[i].shown)
            UiGfx_PrepareMonPic(sH.mons[i].species, sH.mons[i].personality, sH.mons[i].shiny,
                                sH.mons[i].species == SPECIES_EGG);
    if (sH.playerShown)
        UiGfx_PrepareTrainerPic(sH.playerPic);
    /* the confetti's clock: the frame the welcome began */
    if (sH.phase == NATIVE_HOF_WELCOME && (before != NATIVE_HOF_WELCOME || now - sLastFrame > 2))
        sWelcomeAt = now;
    sLastFrame = now;
}

/* ---------------------------------------------------------------- drawing */

static void Hall(int t)
{
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    /* the night above, the gold hall below */
    for (int k = 0; k < 12; k++)
        UiG_Rect(bx, by + k * (bh / 12), bw, bh / 12 + 1, UiMix(UI_HEX(0x0C1430), UI_HEX(0x2A2450), k / 11.0));
    for (int k = 0; k < 40; k++)
    {
        int sx = (int)((k * 7919u) % (unsigned)bw), sy = (int)((k * 104729u) % 150u);

        if (((t / 24 + k) % 5) != 0)
            UiG_Rect(bx + sx, by + sy, 1, 1, UI_HEX(0xFFF4D0));
    }
    /* the beams from the hall's lights, slowly breathing */
    for (int k = 0; k < 5; k++)
    {
        int x = 64 + k * 128, w = 44;
        double a = 0.16 + 0.06 * sin((t + k * 40) / 50.0);

        UiG_Rect(x - w / 2, 0, w, 300, UiWithAlpha(sGold.hi, (int)(a * 255)));
    }
    UiG_Rect(bx, 300, bw, by + bh - 300, UiMix(sGold.lo, sGold.ink, 0.55));
    UiG_Rect(bx, 300, bw, 2, sGold.base);
    UiG_Paint(UiM_Ellipse(520, 60), 60, 270, UiWithAlpha(sGold.hi, 70), &(struct UiPaint){UI_NONE, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
}

static void Team(void)
{
    for (int i = 0; i < 6; i++)
    {
        const struct NativeHofMon *m = &sH.mons[i];
        /* the game's wash over those already shown (towards a pale mint at 12/16) */
        static const struct UiFilter dim = {.grayscale = 0.65f, .brightness = 1.35f, .silhouette = UI_NONE};

        if (!m->shown)
            continue;
        UiGfx_MonPicAffine(m->species, m->personality, m->shiny, 0, GBA_X(m->x), GBA_Y(m->y), m->affine, 2,
                           m->dim ? &dim : NULL);
    }
}

/* the current Pokémon's line on the design's card, where the game prints it (the bottom of the screen) */
static void Line(void)
{
    int x = 96, y = 300, w = 448, h = 48;

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UiMix(sGold.ink, UI_HEX(0x101830), 0.5),
              &(struct UiPaint){sGold.base, sGold.lo, UI_NONE, 0, 2, NULL, UI_NONE});
    if (sH.number[0] != EOS)
        UiText_Draw(UI_FONT_NORMAL, x + 16, y + 6, sH.number, UI_P_TEXT_W, UI_P_INK, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 6, sH.nickname, UI_P_TEXT_W, UI_P_INK,
                sH.species[0] != EOS ? UI_ALIGN_RIGHT : UI_ALIGN_CENTER, 0);
    if (sH.species[0] != EOS)
    {
        u8 kind[24];

        kind[0] = CHAR_SLASH;
        kind[1] = EOS;
        UiText_Append(kind, sizeof(kind), sH.species);
        UiText_Draw(UI_FONT_NORMAL, x + (w >> 1), y + 6, kind, UI_P_TEXT_W, UI_P_INK, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, x + 64, y + 26, sH.level, UI_P_TEXT_W, UI_P_INK, UI_ALIGN_LEFT, 0);
        UiText_Draw(UI_FONT_NORMAL, x + 240, y + 26, sH.id, UI_P_TEXT_W, UI_P_INK, UI_ALIGN_LEFT, 0);
    }
}

/* the game's confetti: one every fourth frame until the last 110, each falling with a sway */
static void Confetti(void)
{
    static const UiRGBA sColours[4] = {UI_HEX(0xF878B0), UI_HEX(0xF04040), UI_HEX(0x58A0F8), UI_HEX(0xF8D848)};
    int elapsed = (int)(NativeUi_Frame() - sWelcomeAt), made = elapsed / CONFETTI_EVERY;

    if (sH.frames <= CONFETTI_FALL)
        made = (400 - CONFETTI_FALL) / CONFETTI_EVERY;
    for (int k = 0; k < made; k++)
    {
        int age = elapsed - k * CONFETTI_EVERY;
        unsigned seed = (unsigned)k * 2654435761u;
        int x0 = (int)(seed % 640u), speed = 2 + (int)((seed >> 8) % 3u);
        int x = x0 + (int)(12 * sin((age + (int)(seed % 50u)) / 9.0)), y = -8 + age * speed;

        if (age < 0 || y > 360)
            continue;
        UiG_Rect(x, y, 4, (age / 6) % 2 ? 6 : 3, sColours[(seed >> 16) % 4u]);
    }
}

/* the player's record, where the game puts its window (tile 2, 2) */
static void Record(void)
{
    int x = GBA_X(16), y = GBA_Y(16), w = 112 * 2 - 16, h = 104;
    u8 id[8];

    UiG_Paint(UiM_Rounded4(w, h, 4, 4, 4, 4), x, y, UI_P_CREAM,
              &(struct UiPaint){sGold.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + 14, y + 12, sText_Name, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + w - 14, y + 12, gSaveBlock2Ptr->playerName, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + 14, y + 42, sText_IdNo, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    id[0] = EOS;
    UiText_AppendNumber(id, sizeof(id), (gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8)), 5, false);
    UiText_Draw(UI_FONT_NORMAL, x + w - 14, y + 42, id, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + 14, y + 72, sText_Time, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_NORMAL, x + w - 14, y + 72, sH.time, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
}

static void Draw(int t)
{
    struct UiRamp tab = {sGold.hi, sGold.base, sGold.lo, sGold.ink};
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    Hall(t);
    UiW_TitleTab(0, 8, "HALL OF FAME", &tab, 0, NULL);
    if (sH.phase != NATIVE_HOF_PLAYER || !sH.playerRecord)
        Team();
    if (sH.phase == NATIVE_HOF_WELCOME)
    {
        Confetti();
        UiField_MessageBox(t, bx, bx + bw, by + bh, gText_WelcomeToHOF, -1, false, &sGold, NULL, 0, sGold.ink, 0);
    }
    else if (sH.phase == NATIVE_HOF_MON && sH.lineShown)
    {
        Line();
    }
    else if (sH.saving)
    {
        UiField_MessageBox(t, bx, bx + bw, by + bh, gText_SavingDontTurnOffPower, -1, false, &sGold, NULL, 0, sGold.ink, 0);
    }
    if (sH.playerShown)
        UiGfx_TrainerPicAffine(sH.playerPic, GBA_X(sH.playerX), GBA_Y(sH.playerY), 256, 2, NULL);
    if (sH.playerRecord)
    {
        struct UiHintItem h = {UI_ACT_CONFIRM, sText_Continue};

        Record();
        UiField_MessageBox(t, bx, bx + bw, by + bh, gText_LeagueChamp, -1, sH.ready, &sGold, sH.ready ? &h : NULL,
                           sH.ready ? 1 : 0, sGold.ink, 0);
    }
    UiG_Hit(HIT_HALL, 0, bx, by, bw, bh);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static bool Ready(void)
{
    return sH.ready;
}

static bool Click(const struct UiHit *hit, int button)
{
    (void)hit;
    if (button == 1 && sH.ready && !NativeUi_PressesPending())
        NativeUi_QueuePress(A_BUTTON);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_HallOfFame = {
    .name = "hall_of_fame",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
