#ifdef ENABLE_NATIVE_UI
/*
 * Move switching (MOVES in the party menu, the summary's relearner, the Move Reminder): the native screen over the
 * engine's move relearner (move_relearner.c), which keeps every rule: which moves each list offers (level-up moves to
 * the Pokemon's level, each earlier form's to the level it evolved at; egg moves of a Pokemon that hatched; the TMs and
 * HMs the player has; the one-time tutors used), SELECT / L / R between the lists, teaching (chooseboxmon.c LearnMove:
 * full PP, no TM used up), the move to forget (the summary's move list underneath, drawn here as the replace step), the
 * warnings before a move goes with the cursor on NO, the HM rule, back to the party menu on the same Pokemon.
 *
 * The Pokemon on the left (its picture, name, level, types and its four moves with PP, each marked when switching it
 * out would cost something), the lists on the right as tabs with their counts, each row with where its move comes
 * from ("LV 12", "TM24", "HM03", "EGG", "TUTOR"), its type, category, power, accuracy and PP, the selected move's
 * details below; teaching a move over four turns the right side into the replace step (the new move against the one
 * to forget). Keys stay the engine's; clicks and the wheel become its presses. Project-owned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "contest_effect.h"
#include "main.h"
#include "move.h"
#include "move_relearner.h"
#include "pokemon.h"
#include "pokemon_summary_screen.h"
#include "strings.h"
#include "constants/characters.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_ROW = UI_HIT_SCREEN,  /* a list row (index: the row; count: CANCEL) */
    HIT_TAB,                  /* a list's tab (index: enum MoveRelearnerStates) */
    HIT_KNOWN,                /* a known move (index: slot; 4: don't learn) */
    HIT_MESSAGE,
    HIT_DETAILS,              /* the details' BATTLE / CONTEST switch */
};

/* the design's geometry (content coordinates) */
#define LX 8          /* the Pokemon's pane */
#define LY 32
#define LW 208
#define LH 262
#define RX 224        /* the lists */
#define RW 408
#define TAB_Y 32
#define TAB_H 21
#define LIST_Y 52
#define LIST_H 164
#define ROW_H 19
#define VIS 7
#define DET_Y 220
#define DET_H 74
#define KNOWN_Y (LY + 146) /* the first known move's row */
#define KNOWN_H 22
#define MSG_X 8
#define MSG_Y 300
#define MSG_W 392
#define MSG_H 54
#define HINT_X 408
#define HINT_W 224

static const u8 sText_Teach[] = _("Teach");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Lists[] = _("Lists");
static const u8 sText_Details[] = _("Battle/Contest");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_No[] = _("No");
static const u8 sText_Next[] = _("Next");
static const u8 sText_Forget[] = _("Forget");
static const u8 sText_DontLearn[] = _("Don't learn");
static const u8 sText_Wait[] = _("Please wait…");
static const u8 sText_Empty[] = _("No moves to learn here.");
static const u8 sText_EmptyMore[] = _("Another list has some: SELECT, L or R.");
static const u8 sText_EmptyNone[] = _("It knows every move it could switch to now.");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_CancelLine[] = _("Go back without teaching a move.");
static const u8 sText_ChooseFor1[] = _("Choose a move for ");
static const u8 sText_ChooseFor2[] = _(" to learn.");
static const u8 sText_ForgetFor1[] = _("Choose the move ");
static const u8 sText_ForgetFor2[] = _(" should forget to learn ");
static const u8 sText_Period[] = _(".");
static const u8 sText_LearnsAt1[] = _(" learns it at ");
static const u8 sText_OnEvolving[] = _("Learned on evolving into ");
static const u8 sText_TmLine[] = _("Your TM: TMs are never used up.");
static const u8 sText_HmLine[] = _("Your HM: HMs are never used up.");
static const u8 sText_EggLine1[] = _("Egg move");
static const u8 sText_EggLine2[] = _(": it hatched from an Egg.");
static const u8 sText_TutorLine[] = _("Taught by the tutor in ");
static const u8 sText_TutorAny[] = _("A tutor's move.");
static const u8 sText_FormLine[] = _(" learns it in this form.");
static const u8 sText_ComesBack[] = _("It can be switched back in later.");
static const u8 sText_GoneLine[] = _("It can't be switched back in later.");
static const u8 sText_PpUpsLine[] = _("Its PP Ups will be lost.");
static const u8 sText_HmLockedLine[] = _("HM moves can't be forgotten now.");
static const u8 sText_KeepAll[] = _("Keep all four moves: ");
static const u8 sText_KeepAll2[] = _(" is not learned.");
static const u8 sText_ChooseAbove[] = _("Choose one of its four moves on the left.");

/* ---------------------------------------------------------------- captured state */

struct RowMove
{
    u16 move;
    const u8 *name;
    const u8 *desc;
    s8 type;                /* enum UiTypeId */
    u8 category;            /* 0 physical, 1 special, 2 status */
    u16 power;
    u8 accuracy;
    s8 priority;
    u8 maxPP;
    u8 contestCategory;
    u8 appeal;
    u8 jam;
    const u8 *contestDesc;
};

static struct NativeMoveRelearner sR;      /* the engine's relearner (FORGET: its list from sLast) */
static struct NativeMoveRelearner sLast;   /* the last frame the relearner had its list (the step to forget frees it) */
static struct NativeSummary sSum;          /* FORGET: the summary's move list */
static bool sHaveSum;
static u32 sDetectFrame = 0xFFFFFFFF;      /* the frame Detect read sR in (Capture uses it) */
static struct UiMon sMon;
static struct RowMove sList[NATIVE_RELEARNER_MAX_MOVES];
static struct RowMove sKnown[MAX_MON_MOVES];
static struct RowMove sTeach;
static u8 sSource[NATIVE_RELEARNER_MAX_MOVES][8];  /* each row's tag (ASCII caps) */
static u8 sSourceLine[160];                        /* the selected move's source, in a sentence (engine text) */
static u8 sPrompt[160];                            /* the screen's own line when the engine prints none */
static u8 sMsg[UI_MESSAGE_COPY];
static bool sHasMsg;
static u32 sMsgPrinted;
static bool sMsgPrinting;
static bool sMsgWaiting;
static int sFirst;                         /* the first list row shown */
static int sMonT;                          /* frames since the Pokemon came up (its front animation) */
static u16 sMonSpecies;
static u32 sMonPersonality;
static bool sVeil;                         /* the engine's fade is the screen's (coming in, going out) */
static bool sInternalFade;                 /* the fade between the relearner and the move to forget: not shown */
/* a mouse goal: the engine's own presses until its cursor gets there, then A */
enum
{
    GOAL_NONE,
    GOAL_ROW,                  /* the list's cursor */
    GOAL_KNOWN,                /* the summary's move cursor (the move to forget) */
    GOAL_YESNO,
};
static int sGoal;
static int sGoalValue;
static int sGoalSteps;

static bool Forgetting(void);

/* ---------------------------------------------------------------- capture */

static void Info(struct RowMove *v, u16 move)
{
    memset(v, 0, sizeof(*v));
    v->move = move;
    if (move == MOVE_NONE || move >= MOVES_COUNT)
        return;
    v->name = GetMoveName(move);
    v->desc = GetMoveDescription(move);
    v->type = (s8)UiMon_Type(GetMoveType(move));
    v->category = GetMoveCategory(move) == DAMAGE_CATEGORY_PHYSICAL ? 0
                : GetMoveCategory(move) == DAMAGE_CATEGORY_SPECIAL ? 1 : 2;
    v->power = GetMovePower(move);
    v->accuracy = GetMoveAccuracy(move);
    v->priority = (s8)GetMovePriority(move);
    v->maxPP = GetMovePP(move);
    v->contestCategory = GetMoveContestCategory(move);
    v->appeal = gContestEffects[GetMoveContestEffect(move)].appeal;
    v->jam = gContestEffects[GetMoveContestEffect(move)].jam;
    v->contestDesc = gContestEffects[GetMoveContestEffect(move)].description;
}

static void TagText(char *dst, int size, const struct NativeRelearnerMove *m)
{
    switch (m->source)
    {
    case MOVE_SOURCE_LEVEL:
        if (m->number == 0)
            snprintf(dst, size, "EVO");
        else
            snprintf(dst, size, "LV %u", (unsigned)m->number);
        break;
    case MOVE_SOURCE_TM: snprintf(dst, size, "TM%02u", (unsigned)m->number); break;
    case MOVE_SOURCE_HM: snprintf(dst, size, "HM%02u", (unsigned)m->number); break;
    case MOVE_SOURCE_EGG: snprintf(dst, size, "EGG"); break;
    case MOVE_SOURCE_TUTOR: snprintf(dst, size, "TUTOR"); break;
    case MOVE_SOURCE_FORM: snprintf(dst, size, "FORM"); break;
    default: dst[0] = '\0'; break;
    }
}

/* the selected list move's source in a sentence: "Charmander learns it at {LV}12." */
static void SourceLine(const struct NativeRelearnerMove *m)
{
    u8 number[8];

    sSourceLine[0] = EOS;
    switch (m->source)
    {
    case MOVE_SOURCE_LEVEL:
        if (m->number == 0)
        {
            UiText_Append(sSourceLine, sizeof(sSourceLine), sText_OnEvolving);
            UiText_Append(sSourceLine, sizeof(sSourceLine), GetSpeciesName(m->species));
            UiText_Append(sSourceLine, sizeof(sSourceLine), sText_Period);
            break;
        }
        UiText_Append(sSourceLine, sizeof(sSourceLine), GetSpeciesName(m->species));
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_LearnsAt1);
        UiMon_LevelText(number, sizeof(number), m->number);
        UiText_Append(sSourceLine, sizeof(sSourceLine), number);
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_Period);
        break;
    case MOVE_SOURCE_TM:
    case MOVE_SOURCE_HM:
        UiText_Append(sSourceLine, sizeof(sSourceLine), m->source == MOVE_SOURCE_TM ? sText_TmLine : sText_HmLine);
        break;
    case MOVE_SOURCE_EGG:
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_EggLine1);
        if (m->species != SPECIES_NONE)
        {
            UiText_AppendAscii(sSourceLine, sizeof(sSourceLine), " (");
            UiText_Append(sSourceLine, sizeof(sSourceLine), GetSpeciesName(m->species));
            UiText_AppendAscii(sSourceLine, sizeof(sSourceLine), ")");
        }
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_EggLine2);
        break;
    case MOVE_SOURCE_TUTOR:
        if (m->number == 0xFF)
        {
            UiText_Append(sSourceLine, sizeof(sSourceLine), sText_TutorAny);
            break;
        }
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_TutorLine);
        {
            u8 place[32];

            MoveRelearner_TutorPlace(m->number, place);
            UiText_Append(sSourceLine, sizeof(sSourceLine), place);
        }
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_Period);
        break;
    case MOVE_SOURCE_FORM:
        UiText_Append(sSourceLine, sizeof(sSourceLine), GetSpeciesName(m->species));
        UiText_Append(sSourceLine, sizeof(sSourceLine), sText_FormLine);
        break;
    }
}

static bool Detect(void)
{
    if (!MoveRelearner_NativeUiState(&sR) || sR.phase == NATIVE_RELEARNER_NONE)
        return false;
    sDetectFrame = NativeUi_Frame();
    return true;
}

/* the list the screen shows: the relearner's, or (the move to forget) the last one it had */
static const struct NativeMoveRelearner *ListState(void)
{
    return sR.phase == NATIVE_RELEARNER_FORGET || (sR.phase == NATIVE_RELEARNER_LOADING && sR.returning && sR.count == 0)
         ? &sLast : &sR;
}

static int SummaryCursor(void)
{
    return sHaveSum ? sSum.moveCursor : 0;
}

static void GoalStep(void)
{
    int current;
    bool ready;

    if (sGoal == GOAL_NONE || NativeUi_PressesPending())
        return;
    switch (sGoal)
    {
    case GOAL_ROW:
        ready = sR.phase == NATIVE_RELEARNER_BROWSE && sR.ready;
        current = sR.cursor;
        break;
    case GOAL_KNOWN:
        ready = sHaveSum && sSum.ready
             && (sSum.phase == NATIVE_SUMMARY_REPLACE || sSum.phase == NATIVE_SUMMARY_CANT_FORGET);
        current = SummaryCursor();
        break;
    default:
        ready = sR.yesNo && sR.ready;
        current = sR.yesNoCursor;
        break;
    }
    if (!ready)
    {
        if (++sGoalSteps > 120)
            sGoal = GOAL_NONE; /* the engine went elsewhere */
        return;
    }
    if (current == sGoalValue)
    {
        NativeUi_QueuePress(A_BUTTON);
        sGoal = GOAL_NONE;
        return;
    }
    if (++sGoalSteps > 200)
    {
        sGoal = GOAL_NONE;
        return;
    }
    NativeUi_QueuePress(sGoalValue > current ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    const struct NativeMoveRelearner *list;
    u8 was = sR.phase;
    bool8 active;
    int selected;

    if (sDetectFrame != NativeUi_Frame())
        MoveRelearner_NativeUiState(&sR);
    sDetectFrame = 0xFFFFFFFF;
    if (sR.phase != was && sGoal != GOAL_KNOWN && sGoal != GOAL_YESNO)
        sGoal = GOAL_NONE;
    /* the list kept for the step to forget (the relearner's is freed while the summary is up) */
    if (sR.phase != NATIVE_RELEARNER_FORGET && (sR.count > 0 || sR.phase == NATIVE_RELEARNER_BROWSE))
        sLast = sR;
    sHaveSum = sR.phase == NATIVE_RELEARNER_FORGET && Summary_NativeUiState(&sSum);
    if (!sHaveSum)
        memset(&sSum, 0, sizeof(sSum));
    UiMon_FromPokemon(&sMon, &sR.mon);
    if (sMon.species != sMonSpecies || sMon.personality != sMonPersonality)
    {
        sMonSpecies = sMon.species;
        sMonPersonality = sMon.personality;
        sMonT = 0;
    }
    else
    {
        sMonT++;
    }
    UiGfx_PrepareMonPic(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg);
    for (int i = 0; i < MAX_MON_MOVES; i++)
        Info(&sKnown[i], i < sMon.moveCount ? sMon.moves[i] : MOVE_NONE);
    Info(&sTeach, sR.teaching);
    list = ListState();
    for (int i = 0; i < list->count && i < NATIVE_RELEARNER_MAX_MOVES; i++)
    {
        Info(&sList[i], list->moves[i].move);
        TagText((char *)sSource[i], sizeof(sSource[i]), &list->moves[i]);
    }
    selected = list->cursor < list->count ? list->cursor : -1;
    sSourceLine[0] = EOS;
    if (selected >= 0)
        SourceLine(&list->moves[selected]);
    /* the rows shown: the engine's cursor stays in view */
    if (list->cursor < sFirst)
        sFirst = list->cursor;
    if (list->cursor >= sFirst + VIS)
        sFirst = list->cursor - VIS + 1;
    if (sFirst > list->count + 1 - VIS)
        sFirst = list->count + 1 - VIS;
    if (sFirst < 0)
        sFirst = 0;
    /* the engine's message (its prompt and the SELECT hint are the screen's own: sPrompt) */
    sHasMsg = false;
    sMsgWaiting = false;
    if ((sR.phase == NATIVE_RELEARNER_TEACHING || sR.phase == NATIVE_RELEARNER_GIVE_UP) && !sR.msgStale)
    {
        sHasMsg = TextPrinter_NativeUiWindowText(sR.msgWindow, sMsg, sizeof(sMsg), &sMsgPrinted, &active);
        sMsgPrinting = sHasMsg && active;
        sMsgWaiting = sHasMsg && TextPrinter_NativeUiWindowWaiting(sR.msgWindow);
    }
    sPrompt[0] = EOS;
    if (Forgetting())
    {
        UiText_Append(sPrompt, sizeof(sPrompt), sText_ForgetFor1);
        UiText_Append(sPrompt, sizeof(sPrompt), sMon.nickname);
        UiText_Append(sPrompt, sizeof(sPrompt), sText_ForgetFor2);
        UiText_Append(sPrompt, sizeof(sPrompt), sTeach.name != NULL ? sTeach.name : sText_Period);
        UiText_Append(sPrompt, sizeof(sPrompt), sText_Period);
    }
    else
    {
        UiText_Append(sPrompt, sizeof(sPrompt), sText_ChooseFor1);
        UiText_Append(sPrompt, sizeof(sPrompt), sMon.nickname);
        UiText_Append(sPrompt, sizeof(sPrompt), sText_ChooseFor2);
    }
    /* the engine's fades: coming in and going out are the screen's; the ones between the relearner and the summary's
     * move list (the step to forget) are not, the same screen stays up */
    sInternalFade = sR.phase == NATIVE_RELEARNER_FORGET || (sR.phase == NATIVE_RELEARNER_LOADING && sR.returning);
    sVeil = !sInternalFade;
    GoalStep();
}

/* ---------------------------------------------------------------- which view */

/* the replace step: the new move against the one to forget (from "Should a move be deleted?" until it is gone) */
static bool Forgetting(void)
{
    if (sR.phase == NATIVE_RELEARNER_FORGET || (sR.phase == NATIVE_RELEARNER_LOADING && sR.returning))
        return true;
    if (sR.phase != NATIVE_RELEARNER_TEACHING)
        return false;
    return sR.step == NATIVE_LEARN_REPLACE_ASK || sR.step == NATIVE_LEARN_WHICH || sR.step == NATIVE_LEARN_WARN
        || sR.step == NATIVE_LEARN_STOP_ASK || sR.step == NATIVE_LEARN_FORGOT;
}

/* the known move the replace step points at (-1: none; MAX_MON_MOVES: don't learn) */
static int ForgetRow(void)
{
    if (sR.phase == NATIVE_RELEARNER_FORGET)
        return sHaveSum && (sSum.phase == NATIVE_SUMMARY_REPLACE || sSum.phase == NATIVE_SUMMARY_CANT_FORGET
                            || sSum.phase == NATIVE_SUMMARY_CLOSING) ? SummaryCursor() : -1;
    if (sR.forgetSlot < MAX_MON_MOVES)
        return sR.forgetSlot;
    return -1;
}

/* the known moves take the cursor (the summary's move list) */
static bool KnownCursor(void)
{
    return sR.phase == NATIVE_RELEARNER_FORGET && sHaveSum
        && (sSum.phase == NATIVE_SUMMARY_REPLACE || sSum.phase == NATIVE_SUMMARY_CANT_FORGET);
}

static bool ContestView(void)
{
    if (sR.phase == NATIVE_RELEARNER_FORGET && sHaveSum)
        return sSum.page == PSS_PAGE_CONTEST_MOVES;
    return sR.contest;
}

/* ---------------------------------------------------------------- pieces */

static UiRGBA KindColour(int list)
{
    switch (list)
    {
    case MOVE_RELEARNER_EGG_MOVES: return UI_HEX(0xD8A838);
    case MOVE_RELEARNER_TM_MOVES: return UI_P_POCKETS_TMS;
    case MOVE_RELEARNER_TUTOR_MOVES: return UI_HEX(0xE07830);
    default: return UI_P_CMD_POKEMON;
    }
}

static UiRGBA SourceColour(int source)
{
    switch (source)
    {
    case MOVE_SOURCE_EGG: return KindColour(MOVE_RELEARNER_EGG_MOVES);
    case MOVE_SOURCE_TM: return KindColour(MOVE_RELEARNER_TM_MOVES);
    case MOVE_SOURCE_HM: return UI_P_TYPES_GHOST;
    case MOVE_SOURCE_TUTOR: return KindColour(MOVE_RELEARNER_TUTOR_MOVES);
    default: return KindColour(MOVE_RELEARNER_LEVEL_UP_MOVES);
    }
}

static void Background(int t)
{
    int x0, y0, w, h;
    struct UiPattern diag = UiPat_Diag(24, 10, 0, 1);

    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, UI_P_PARTY_BG);
    UiG_FillPattern(&diag, x0, y0, w, h, UI_P_PARTY_BG_STRIPE, t / 5, 0);
}

/* a source plate: "LV 12", "TM24" in its colour */
static void SourceTag(int x, int y, int w, const char *label, int source, bool dim)
{
    struct UiRamp r = UiRampOf(SourceColour(source), 0.4, 0.25, 0.6);

    if (dim)
        r = UiRampOf(UiMix(SourceColour(source), UI_HEX(0xB0B0B0), 0.55), 0.4, 0.25, 0.6);
    UiG_Paint(UiM_Rounded4(w, 13, 2, 2, 2, 2), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, 0});
    UiCaps_Draw(x + (w >> 1), y + 3, label, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
}

static void Number3(char *dst, int size, int value, bool dashes)
{
    if (dashes)
        snprintf(dst, size, "---");
    else
        snprintf(dst, size, "%d", value);
}

/* an engine name in its room: the normal font, else the narrow one */
static int NameText(int x, int y, const u8 *name, int room, UiRGBA fg, UiRGBA sh)
{
    int font = UiText_Width(UI_FONT_NORMAL, name, 0) <= room ? UI_FONT_NORMAL : UI_FONT_NARROW;

    return UiText_Draw(font, x, y, name, fg, sh, UI_ALIGN_LEFT, 0);
}

static void Hearts(int x, int y, int count, UiRGBA colour)
{
    static const char *const sHeart[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};

    for (int i = 0; i < 8; i++)
        UiG_Mask(UiM_Rows(sHeart, 6, NULL), x + i * 9, y, i < count ? colour : UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3));
}

static const char *const sContestLabels[] = {"COOL", "BEAUTY", "CUTE", "SMART", "TOUGH"};
static const UiRGBA sContestColours[] = {UI_HEX(0xE84838), UI_HEX(0x3878D8), UI_HEX(0xE890B8), UI_HEX(0x48A848),
                                         UI_HEX(0xE8B020)};

/* a move's description, its own line breaks kept (two lines) */
static void DescLines(int x, int y, const u8 *desc, int lines)
{
    const u8 *p = desc;

    for (int line = 0; p != NULL && line < lines && *p != EOS; line++, y += 15)
    {
        u8 ln[80];
        int n = 0;

        while (p[n] != EOS && p[n] != CHAR_NEWLINE && n < (int)sizeof(ln) - 1)
            n++;
        UiText_CopyN(ln, sizeof(ln), p, n);
        UiG_Text(x, y, ln);
        p += n;
        if (*p == CHAR_NEWLINE)
            p++;
    }
}

/* ---------------------------------------------------------------- the Pokemon's pane */

static struct UiRamp Ink(void)
{
    return (struct UiRamp){UI_R_SLOT_HI, UI_R_SLOT_BASE, UI_R_SLOT_LO, UI_R_SLOT_INK};
}

static void KnownRow(int k, int y, bool cursorRow, bool forgetRow, int t)
{
    const struct RowMove *m = &sKnown[k];
    int x = LX + 6, w = LW - 12;
    bool newMove = k < MAX_MON_MOVES && sR.phase == NATIVE_RELEARNER_TEACHING && sR.step == NATIVE_LEARN_DONE
                && m->move != MOVE_NONE && m->move == sR.teaching;

    if (forgetRow || cursorRow || newMove)
    {
        UiRGBA body = newMove ? UiMix(UI_P_HP_GREEN_0, UI_HEX(0xFFFFFF), 0.4)
                    : forgetRow && !cursorRow ? UiMix(UI_P_TEXT_RED_SH, UI_HEX(0xFFFFFF), 0.3)
                    : UiMix(UI_R_SLOT_SEL_BASE, UI_HEX(0xFFFFFF), 0.45);
        UiRGBA ink = forgetRow ? UI_P_TEXT_RED : UI_R_SLOT_SEL_INK;

        UiG_Paint(UiM_Rounded(w, KNOWN_H - 1, UiCornerR(2), UiCornerChamfer(4), UiCornerR(2), UiCornerR(2)), x, y, body,
                  &(struct UiPaint){ink, UiMix(body, UI_HEX(0xFFFFFF), 0.6), UI_NONE, 0, 0, NULL, 0});
    }
    if (cursorRow)
        UiW_Cursor(x - 5, y + 5, UiW_Nudge(t), UI_NONE);
    if (k == MAX_MON_MOVES)
    {
        /* the summary's fifth row: the new move itself, not learned */
        UiGfx_Icon("return_blue", x + 4, y + 3, 0);
        UiG_Text(x + 24, y + 3, sText_DontLearn);
        if (sTeach.name != NULL)
            NameText(x + 26 + UiText_Width(UI_FONT_NORMAL, sText_DontLearn, 0), y + 3, sTeach.name, 70, UI_P_TEXT_MUTED,
                     UI_P_TEXT_SH);
        UiG_Hit(HIT_KNOWN, k, x, y, w, KNOWN_H);
        return;
    }
    if (m->move == MOVE_NONE)
    {
        UiText_Draw(UI_FONT_NORMAL, x + 48, y + 3, (const u8 *)gText_ThreeDashes, UI_P_TEXT_MUTED, UI_P_TEXT_SH,
                    UI_ALIGN_LEFT, 0);
        return;
    }
    {
        char pp[16];
        int nx, px;
        bool low = sMon.maxPP[k] > 0 && sMon.pp[k] * 4 <= sMon.maxPP[k];

        UiW_TypeBadge(x + 4, y + 4, m->type >= 0 ? m->type : UI_TYPE_NORMAL, 0);
        nx = NameText(x + 48, y + 3, m->name, 86, UI_P_TEXT, UI_P_TEXT_SH);
        snprintf(pp, sizeof(pp), "%u/%u", (unsigned)sMon.pp[k], (unsigned)sMon.maxPP[k]);
        px = x + w - 6 - UiCaps_Width(pp);
        UiCaps_Draw(x + w - 6, y + 7, pp, low ? UI_P_TEXT_RED : UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        /* what switching it out would cost: the marks of the warnings */
        if (newMove)
            UiMon_Tag(nx + 3, y + 5, "NEW", UI_P_SEL);
        else if (sR.hmLocked[k])
            UiMon_Tag(nx + 3, y + 5, "HM", UI_P_TYPES_STEEL);
        else if (!sR.canComeBack[k] && sR.switching)
            UiMon_Tag(nx + 3, y + 5, "!", UI_P_TEXT_RED);
        if (sR.ppUps[k] && sR.switching && px - 24 > nx)
            UiCaps_Draw(px - 4, y + 7, "PP+", UI_P_SEL_LO, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
    UiG_Hit(HIT_KNOWN, k, x, y, w, KNOWN_H);
}

static void MonPane(int t)
{
    struct UiRamp ink = Ink();
    struct UiPaint edge = {ink.ink, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, 0, NULL, 0};
    int sx = LX + 6, sy = LY + 26, sw = LW - 12, sh = 82, nx, tx, frame, cursor = -1, forget = ForgetRow();
    const struct UiRamp *t0 = &gUiTypeRamps[sMon.types[0] >= 0 ? sMon.types[0] : UI_TYPE_NORMAL];
    UiRGBA light = UiMix(t0->hi, UI_HEX(0xFFFFFF), 0.55);
    struct UiPattern diag = UiPat_Diag(16, 6, 0, 1);
    struct UiRamp floor = UiRampDefault(UiMix(t0->base, UI_HEX(0x808080), 0.35));
    bool forgetting = Forgetting();
    u8 lv[16];

    UiG_Paint(UiM_Rounded(LW, LH, UiCornerR(3), UiCornerChamfer(10), UiCornerR(3), UiCornerR(3)), LX, LY, UI_P_CREAM, &edge);
    UiG_Paint(UiM_Rounded(LW - 6, 20, UiCornerR(2), UiCornerChamfer(7), UiCornerR(0), UiCornerR(0)), LX + 3, LY + 3,
              ink.lo, &(struct UiPaint){UI_NONE, ink.base, UI_NONE, 0, 0, NULL, 0});
    nx = UiText_Draw(UI_FONT_NORMAL, LX + 10, LY + 5, sMon.nickname, UI_P_TEXT_W, ink.ink, UI_ALIGN_LEFT, 0);
    UiW_Gender(nx + 1, LY + 5, sMon.gender);
    UiMon_LevelText(lv, sizeof(lv), sMon.level);
    UiText_Draw(UI_FONT_NORMAL, LX + LW - 16, LY + 5, lv, UI_P_TEXT_W, ink.ink, UI_ALIGN_RIGHT, 0);
    /* the photo: the game's own front picture with its own animation as it comes up */
    UiG_Paint(UiM_Rounded4(sw, sh, 2, 2, 2, 2), sx, sy, light,
              &(struct UiPaint){UiMix(t0->lo, UI_P_INK, 0.3), UI_NONE, UI_NONE, 0, 0, &diag, UiMix(t0->hi, UI_HEX(0xFFFFFF), 0.25)});
    UiG_Paint(UiM_Ellipse(84, 18), sx + (sw >> 1) - 42, sy + sh - 22, floor.base,
              &(struct UiPaint){floor.ink, floor.hi, floor.lo, 0, 0, NULL, 0});
    UiG_Save();
    UiG_Clip(sx + 1, sy + 1, sw - 2, sh - 2);
    frame = UiGfx_MonPicFrameAt(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg, sMonT);
    UiGfx_MonPic(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg, frame, sx + (sw >> 1), sy + sh - 12, NULL);
    UiG_Restore();
    if (sMon.shiny)
        UiCaps_Draw(sx + 5, sy + 5, UI_CAPS_STAR, UI_P_SEL, UI_P_INK, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    tx = LX + 8;
    for (int i = 0; i < 2; i++)
        if (sMon.types[i] >= 0)
            tx = UiW_TypeBadge(tx, LY + 114, sMon.types[i], 0) + 3;
    if (sR.isBoxMon)
        UiMon_Tag(LX + LW - 38, LY + 115, "BOX", UI_R_SLOT_LO);
    /* its moves: what the new move would replace */
    UiG_HLine(LX + 6, LY + 132, LW - 12, UI_P_CREAM_LO);
    UiCaps_Draw(LX + 10, LY + 136, forgetting ? "FORGET A MOVE" : "KNOWN MOVES", forgetting ? UI_P_TEXT_RED : ink.lo,
                UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(LX + LW - 12, LY + 136, "PP", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    if (KnownCursor())
        cursor = SummaryCursor();
    for (int k = 0; k < MAX_MON_MOVES; k++)
        KnownRow(k, KNOWN_Y + k * KNOWN_H, cursor == k, forget == k, t);
    if (sR.phase == NATIVE_RELEARNER_FORGET)
        KnownRow(MAX_MON_MOVES, KNOWN_Y + MAX_MON_MOVES * KNOWN_H + 2, cursor == MAX_MON_MOVES, false, t);
}

/* ---------------------------------------------------------------- the lists */

static const char *const sTabLabels[MOVE_RELEARNER_COUNT] = {
    [MOVE_RELEARNER_LEVEL_UP_MOVES] = "LEVEL-UP",
    [MOVE_RELEARNER_EGG_MOVES] = "EGG",
    [MOVE_RELEARNER_TM_MOVES] = "TM/HM",
    [MOVE_RELEARNER_TUTOR_MOVES] = "TUTOR",
};

static void Tabs(void)
{
    const struct NativeMoveRelearner *l = ListState();
    int tw = (RW - 3 * 4) / MOVE_RELEARNER_COUNT;

    for (int k = 0; k < MOVE_RELEARNER_COUNT; k++)
    {
        int x = RX + k * (tw + 4);
        bool on = k == l->list, any = l->listOn[k] && l->listCount[k] > 0;
        UiRGBA base = any || on ? KindColour(k) : UiMix(KindColour(k), UI_HEX(0x9098A0), 0.7);
        struct UiRamp r = UiRampOf(base, 0.4, 0.25, 0.62);
        char count[8];

        if (!l->listOn[k])
            continue;
        if (on)
            UiG_Paint(UiM_Rounded4(tw, TAB_H + 3, 4, 4, 0, 0), x, TAB_Y, r.base,
                      &(struct UiPaint){r.ink, r.hi, UI_NONE, 0, 0, NULL, 0});
        else
            UiG_Paint(UiM_Rounded4(tw, TAB_H - 3, 4, 4, 0, 0), x, TAB_Y + 3, UiMix(r.base, UI_P_INK, 0.18),
                      &(struct UiPaint){r.ink, UiMix(r.hi, UI_P_INK, 0.18), UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(x + 8, TAB_Y + (on ? 7 : 9), sTabLabels[k], on ? UI_P_TEXT_W : UiMix(UI_P_TEXT_W, r.base, 0.3), r.ink,
                    UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        snprintf(count, sizeof(count), "%u", (unsigned)l->listCount[k]);
        {
            int cw = UiCaps_Width(count) + 8;

            UiG_Paint(UiM_Capsule(cw, 11, true, true), x + tw - cw - 6, TAB_Y + (on ? 5 : 7), on ? UI_P_PAPER : UiMix(r.base, UI_P_PAPER, 0.5),
                      &(struct UiPaint){r.ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
            UiCaps_Draw(x + tw - 6 - (cw >> 1), TAB_Y + (on ? 7 : 9), count, any ? r.ink : UiMix(r.ink, UI_P_PAPER, 0.4),
                        UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        }
        if (any && !on)
            UiG_Hit(HIT_TAB, k, x, TAB_Y, tw, TAB_H);
    }
}

static void ListRow(const struct NativeMoveRelearner *l, int idx, int y, bool sel, bool dim, int t)
{
    int x = RX + 8;
    const struct RowMove *m = &sList[idx];
    UiRGBA fg = dim ? UI_P_TEXT_MUTED : UI_P_TEXT;
    char num[8];

    if (sel)
    {
        struct UiRamp r = UiRampOf(KindColour(l->list), 0.4, 0.25, 0.62);
        UiRGBA body = UiMix(r.hi, UI_HEX(0xFFFFFF), 0.55);

        UiG_Paint(UiM_Rounded(RW - 30, ROW_H, UiCornerR(2), UiCornerChamfer(4), UiCornerR(2), UiCornerR(2)), x + 2, y, body,
                  &(struct UiPaint){r.lo, UiMix(body, UI_HEX(0xFFFFFF), 0.6), UI_NONE, 0, 0, NULL, 0});
        if (sR.phase == NATIVE_RELEARNER_BROWSE)
            UiW_Cursor(x - 6, y + 4, UiW_Nudge(t), UI_NONE);
    }
    if (idx >= l->count)
    {
        UiGfx_Icon("return_blue", x + 14, y + 2, 0);
        UiText_Draw(UI_FONT_NORMAL, x + 36, y + 2, sText_Cancel, fg, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiG_Hit(HIT_ROW, idx, x, y, RW - 28, ROW_H);
        return;
    }
    SourceTag(x + 8, y + 3, 38, (const char *)sSource[idx], l->moves[idx].source, dim);
    UiW_TypeBadge(x + 50, y + 3, m->type >= 0 ? m->type : UI_TYPE_NORMAL, 0);
    NameText(x + 94, y + 2, m->name, 110, fg, UI_P_TEXT_SH);
    UiW_CategoryIcon(x + 210, y + 3, m->category);
    Number3(num, sizeof(num), m->power, m->power <= 1);
    UiCaps_Draw(x + 268, y + 6, num, fg, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    Number3(num, sizeof(num), m->accuracy, m->accuracy == 0);
    UiCaps_Draw(x + 306, y + 6, num, fg, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    Number3(num, sizeof(num), m->maxPP, false);
    UiCaps_Draw(x + 340, y + 6, num, fg, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_Hit(HIT_ROW, idx, x, y, RW - 28, ROW_H);
}

static void ListPanel(int t)
{
    const struct NativeMoveRelearner *l = ListState();
    struct UiRamp r = UiRampOf(KindColour(l->list), 0.4, 0.25, 0.62);
    struct UiRamp ink = Ink();
    int x = RX, y = LIST_Y, rows = l->count + 1, hy = y + 5;
    bool dim = sR.phase != NATIVE_RELEARNER_BROWSE && sR.phase != NATIVE_RELEARNER_LOADING;

    UiG_Paint(UiM_Rounded4(RW, LIST_H, 2, 2, 3, 3), x, y, UI_P_CREAM,
              &(struct UiPaint){r.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    UiG_Rect(x + 1, y + 1, RW - 2, 2, r.base);
    /* the columns */
    UiCaps_Draw(x + 35, hy, "FROM", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiCaps_Draw(x + 78, hy, "TYPE", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiCaps_Draw(x + 102, hy, "MOVE", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(x + 230, hy, "CAT", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiCaps_Draw(x + 276, hy, "POW", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiCaps_Draw(x + 314, hy, "ACC", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiCaps_Draw(x + 348, hy, "PP", ink.lo, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiG_HLine(x + 6, y + 13, RW - 12, UI_P_CREAM_LO);
    for (int i = 0; i < VIS && sFirst + i < rows; i++)
    {
        int idx = sFirst + i, ry = y + 16 + i * ROW_H;

        ListRow(l, idx, ry, idx == l->cursor, dim && idx != l->cursor, t);
        if (idx != l->cursor && idx + 1 != l->cursor && i < VIS - 1 && idx + 1 < rows)
            for (int xx = x + 14; xx < x + RW - 22; xx += 3)
                UiG_Put(xx, ry + ROW_H - 1, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.35));
    }
    if (l->count == 0 && sR.phase != NATIVE_RELEARNER_LOADING)
    {
        bool more = false;

        for (int k = 0; k < MOVE_RELEARNER_COUNT; k++)
            more |= k != l->list && l->listOn[k] && l->listCount[k] > 0;
        UiG_Text(x + 46, y + 16 + ROW_H + 10, sText_Empty);
        UiText_Draw(UI_FONT_SMALL, x + 46, y + 16 + ROW_H + 28, more ? sText_EmptyMore : sText_EmptyNone,
                    UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    {
        struct UiRamp sr = {UiMix(r.hi, UI_P_PAPER, 0.6), r.hi, r.lo, r.ink};

        UiW_Scrollbar(x + RW - 14, y + 16, VIS * ROW_H, sFirst, rows, VIS, &sr);
    }
}

/* the battle data of a move: category, type, name, PP; power, accuracy, priority; its description */
static void BattleCard(int x, int y, int w, const struct RowMove *m, int pp, int maxPP, bool fullStats)
{
    static const u8 sPhysical[] = _("Physical");
    static const u8 sSpecial[] = _("Special");
    static const u8 sStatus[] = _("Status");
    const u8 *const cats[3] = {sPhysical, sSpecial, sStatus};
    char buf[24];
    u8 text[24];
    int nx;

    UiW_CategoryIcon(x, y + 2, m->category);
    UiW_TypeBadge(x + 28, y + 2, m->type >= 0 ? m->type : UI_TYPE_NORMAL, 0);
    nx = NameText(x + 74, y, m->name, w - 74 - 130, UI_P_TEXT, UI_P_TEXT_SH);
    UiText_Draw(UI_FONT_SMALL, nx + 6, y + 2, cats[m->category], UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    if (pp >= 0)
        snprintf(buf, sizeof(buf), "PP %d/%d", pp, maxPP);
    else
        snprintf(buf, sizeof(buf), "PP %d", maxPP);
    UiText_FromAscii(text, sizeof(text), buf);
    UiText_Draw(UI_FONT_NORMAL, x + w, y, text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
    if (fullStats)
    {
        const char *labels[3] = {"POWER", "ACCURACY", "PRIORITY"};
        char values[3][8];
        int cw = w / 3;

        Number3(values[0], sizeof(values[0]), m->power, m->power <= 1);
        Number3(values[1], sizeof(values[1]), m->accuracy, m->accuracy == 0);
        snprintf(values[2], sizeof(values[2]), "%s%d", m->priority > 0 ? "+" : "", m->priority);
        UiG_HLine(x, y + 17, w, UI_P_PAPER_LO);
        for (int i = 0; i < 3; i++)
        {
            UiCaps_Draw(x + i * cw, y + 23, labels[i], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            UiText_FromAscii(text, sizeof(text), values[i]);
            UiText_Draw(UI_FONT_NORMAL, x + i * cw + cw - 12, y + 18, text, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        UiG_HLine(x, y + 35, w, UI_P_PAPER_LO);
        DescLines(x, y + 37, m->desc, 2);
    }
    else
    {
        DescLines(x, y + 18, m->desc, 2);
    }
}

static void ContestCard(int x, int y, int w, const struct RowMove *m, bool fullStats)
{
    int c = m->contestCategory < 5 ? m->contestCategory : 0;

    UiMon_Tag(x, y + 3, sContestLabels[c], sContestColours[c]);
    NameText(x + 56, y, m->name, fullStats ? w - 56 : w - 56 - 210, UI_P_TEXT, UI_P_TEXT_SH);
    if (!fullStats)
    {
        /* the details' panel: the hearts on the name's line, the description under it */
        UiCaps_Draw(x + w - 206, y + 4, "APPEAL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        Hearts(x + w - 172, y + 3, m->appeal / 10, UI_HEX(0xE84838));
        UiCaps_Draw(x + w - 96, y + 4, "JAM", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        Hearts(x + w - 74, y + 3, m->jam / 10, UI_P_INK);
        if (m->contestDesc != NULL)
            DescLines(x, y + 18, m->contestDesc, 2);
        return;
    }
    UiG_HLine(x, y + 17, w, UI_P_PAPER_LO);
    UiCaps_Draw(x, y + 23, "APPEAL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    Hearts(x + 46, y + 22, m->appeal / 10, UI_HEX(0xE84838));
    UiCaps_Draw(x + 130, y + 23, "JAM", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    Hearts(x + 152, y + 22, m->jam / 10, UI_P_INK);
    UiG_HLine(x, y + 35, w, UI_P_PAPER_LO);
    if (m->contestDesc != NULL)
        DescLines(x, y + 37, m->contestDesc, 2);
}

static void DetailsPanel(void)
{
    const struct NativeMoveRelearner *l = ListState();
    struct UiRamp r = UiRampOf(KindColour(l->list), 0.4, 0.25, 0.62);
    int x = RX, y = DET_Y, cx = x + 14, cw = RW - 28;

    UiG_Paint(UiM_Rounded4(RW, DET_H, 3, 3, 3, 3), x, y, UI_P_PAPER,
              &(struct UiPaint){UiMix(r.ink, UI_P_INK, 0.4), UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiG_Rect(x + 1, y + 1, 4, DET_H - 2, r.base);
    if (l->count == 0 || l->cursor >= l->count)
    {
        UiW_MessagePage(l->count == 0 ? sText_Empty : sText_CancelLine, -1, cx, y + 8, cw, 2);
        return;
    }
    {
        const struct RowMove *m = &sList[l->cursor];

        if (ContestView())
            ContestCard(cx, y + 6, cw, m, false);
        else
            BattleCard(cx, y + 6, cw, m, -1, m->maxPP, false);
        if (sSourceLine[0] != EOS)
        {
            SourceTag(cx, y + DET_H - 18, 38, (const char *)sSource[l->cursor], l->moves[l->cursor].source, false);
            UiText_Draw(UI_FONT_SMALL, cx + 44, y + DET_H - 19, sSourceLine, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        if (l->contestData)
        {
            UiCaps_Draw(x + RW - 12, y + DET_H - 15, ContestView() ? "CONTEST" : "BATTLE", r.lo, UI_NONE, UI_CAPS_PLAIN,
                        UI_ALIGN_RIGHT, UI_NONE);
            UiG_Hit(HIT_DETAILS, 0, x + RW - 70, y + DET_H - 20, 66, 16);
        }
    }
}

/* ---------------------------------------------------------------- the replace step */

static void CardFrame(int x, int y, int w, int h, const char *label, UiRGBA colour)
{
    struct UiRamp r = UiRampOf(colour, 0.4, 0.25, 0.62);

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_PAPER,
              &(struct UiPaint){r.ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(w - 4, 14, 2, 2, 0, 0), x + 2, y + 2, r.base, &(struct UiPaint){UI_NONE, r.hi, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(x + 8, y + 5, label, UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
}

static void ReplaceView(int t)
{
    int x = RX, cx = x + 12, cw = RW - 24, row = ForgetRow();
    int ly = 56, lh = 104, fy = 178, fh = 116;
    struct UiRamp tab = UiRampOf(UI_P_TEXT_RED, 0.4, 0.25, 0.62);
    bool contest = ContestView();

    /* the banner over the lists' tabs */
    UiG_Paint(UiM_Rounded4(RW, TAB_H, 4, 4, 0, 0), x, TAB_Y, tab.base, &(struct UiPaint){tab.ink, tab.hi, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(x + 10, TAB_Y + 7, "TEACH A FIFTH MOVE: FORGET ONE", UI_P_TEXT_W, tab.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Rect(x, TAB_Y + TAB_H - 1, RW, 2, tab.ink);
    /* the new move */
    CardFrame(x, ly, RW, lh, "LEARN", UI_P_CMD_POKEMON);
    if (sTeach.move != MOVE_NONE)
    {
        if (contest)
            ContestCard(cx, ly + 22, cw, &sTeach, true);
        else
            BattleCard(cx, ly + 22, cw, &sTeach, sTeach.maxPP, sTeach.maxPP, true);
    }
    /* the arrow from one to the other */
    {
        static const char *const sArrow[] = {"..###..", "..###..", "..###..", "#######", ".#####.", "..###..", "...#..."};
        int ax = x + (RW >> 1) - 3 + 0 * (t & 0), ay = ly + lh + 2 + ((t / 16) % 2);

        UiG_Mask(UiM_Rows(sArrow, 7, NULL), ax + 1, ay + 1, UI_P_INK);
        UiG_Mask(UiM_Rows(sArrow, 7, NULL), ax, ay, UI_P_PAPER);
        UiCaps_Draw(ax + 14, ly + lh + 4, "IN PLACE OF", UI_P_TEXT_W, UI_P_INK, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    }
    /* the move to forget */
    CardFrame(x, fy, RW, fh, "FORGET", UI_P_TEXT_RED);
    if (row >= 0 && row < MAX_MON_MOVES && sKnown[row].move != MOVE_NONE)
    {
        const struct RowMove *m = &sKnown[row];
        int wy = fy + 22 + 72;

        if (contest)
            ContestCard(cx, fy + 22, cw, m, true);
        else
            BattleCard(cx, fy + 22, cw, m, sMon.pp[row], sMon.maxPP[row], true);
        /* what forgetting it costs (the engine asks before it goes: the cursor on NO) */
        if (sR.hmLocked[row])
        {
            UiText_Draw(UI_FONT_SMALL, cx, wy, sText_HmLockedLine, UI_P_TEXT_RED, UI_P_TEXT_RED_SH, UI_ALIGN_LEFT, 0);
        }
        else if (sR.switching)
        {
            int wx = UiText_Draw(UI_FONT_SMALL, cx, wy, sR.canComeBack[row] ? sText_ComesBack : sText_GoneLine,
                                 sR.canComeBack[row] ? UI_P_TEXT_MUTED : UI_P_TEXT_RED,
                                 sR.canComeBack[row] ? UI_P_TEXT_SH : UI_P_TEXT_RED_SH, UI_ALIGN_LEFT, 0);

            if (sR.ppUps[row])
                UiText_Draw(UI_FONT_SMALL, wx + 6, wy, sText_PpUpsLine, UI_P_TEXT_RED, UI_P_TEXT_RED_SH, UI_ALIGN_LEFT, 0);
        }
    }
    else if (row == MAX_MON_MOVES)
    {
        u8 line[96];

        line[0] = EOS;
        UiText_Append(line, sizeof(line), sText_KeepAll);
        UiText_Append(line, sizeof(line), sTeach.name != NULL ? sTeach.name : sText_Period);
        UiText_Append(line, sizeof(line), sText_KeepAll2);
        UiW_MessagePage(line, -1, cx, fy + 26, cw, 2);
    }
    else
    {
        UiW_MessagePage(sText_ChooseAbove, -1, cx, fy + 26, cw, 2);
    }
}

/* ---------------------------------------------------------------- message and prompts */

static void MessageBox(int t)
{
    int b[4];

    UiW_DialogBox(MSG_X, MSG_Y, MSG_W, MSG_H, NULL, UI_NONE, b);
    if (sHasMsg)
        UiW_MessagePage(sMsg, sMsgPrinting ? (int)sMsgPrinted : -1, b[0] + 2, b[1] + 1, b[2] - 8, 2);
    else
        UiW_MessagePage(sPrompt, -1, b[0] + 2, b[1] + 1, b[2] - 8, 2);
    if (sHasMsg && sMsgWaiting && !sR.yesNo)
        UiW_DownArrow(b[0] + b[2] - 10, b[1] + 26 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_MESSAGE, 0, MSG_X, MSG_Y, MSG_W, MSG_H);
}

static int ListsWithMoves(void)
{
    const struct NativeMoveRelearner *l = ListState();
    int n = 0;

    for (int k = 0; k < MOVE_RELEARNER_COUNT; k++)
        n += l->listOn[k] && l->listCount[k] > 0;
    return n;
}

static void Prompts(int t)
{
    struct UiRamp ink = Ink();
    struct UiHintItem rows[3][2];
    int counts[3] = {0, 0, 0};
    bool lists = false;

    UiG_Paint(UiM_Rounded4(HINT_W, MSG_H, 3, 3, 3, 3), HINT_X, MSG_Y, UI_P_CREAM,
              &(struct UiPaint){ink.ink, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    if (sR.yesNo)
    {
        /* the engine's YES / NO (its cursor; the warnings' starts on NO), with what its buttons do */
        struct UiHintItem a = {UI_ACT_CONFIRM, sText_Choose}, b = {UI_ACT_CANCEL, sText_No};

        UiW_YesNo(HINT_X + 4, MSG_Y + 5, sR.yesNoCursor, t, UI_ANCHOR_TOPLEFT, NULL);
        UiW_Hints(HINT_X + 92, MSG_Y + 14, &a, 1, false, 10);
        UiW_Hints(HINT_X + 92, MSG_Y + 32, &b, 1, false, 10);
        return;
    }
    switch (sR.phase)
    {
    case NATIVE_RELEARNER_BROWSE:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Teach};
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        lists = sR.switching && ListsWithMoves() - (sR.count > 0 ? 1 : 0) > 0; /* another list to go to */
        if (sR.contestData)
            rows[2][counts[2]++] = (struct UiHintItem){UI_ACT_MOVE, sText_Details};
        break;
    case NATIVE_RELEARNER_FORGET:
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Forget};
        rows[1][counts[1]++] = (struct UiHintItem){UI_ACT_CANCEL, sText_DontLearn};
        if (sR.contestData)
            rows[2][counts[2]++] = (struct UiHintItem){UI_ACT_MOVE, sText_Details};
        break;
    default:
        if (sMsgWaiting)
        {
            rows[0][counts[0]++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Next};
        }
        else
        {
            UiText_Draw(UI_FONT_SMALL, HINT_X + 10, MSG_Y + 6, sText_Wait, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
        break;
    }
    for (int i = 0; i < 3; i++)
        if (counts[i] > 0)
            UiW_Hints(HINT_X + 10, MSG_Y + 8 + i * 16, rows[i], counts[i], false, 10);
    if (lists)
    {
        /* SELECT or L / R: the lists */
        int hx = UiW_Hint(HINT_X + 10, MSG_Y + 24, UI_ACT_SELECT, (const u8 *)"\xFF", false);

        UiW_Hint(hx + 2, MSG_Y + 24, UI_ACT_TABS, sText_Lists, false);
    }
}

/* ---------------------------------------------------------------- the screen */

static void Draw(int t)
{
    struct UiRamp tab = {UI_R_SLOT_HI, UI_R_SLOT_LO, UI_HEX(0x2E6098), UI_R_SLOT_INK};

    if (sMon.species == SPECIES_NONE)
    {
        UiG_Veil(UI_RGBA(0, 0, 0, 255));
        return;
    }
    Background(t);
    UiW_TitleTab(0, 8, "MOVES", &tab, 0, NULL);
    MonPane(t);
    if (Forgetting())
    {
        ReplaceView(t);
    }
    else
    {
        Tabs();
        ListPanel(t);
        DetailsPanel();
    }
    MessageBox(t);
    Prompts(t);
    if (sVeil)
        UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static void SetGoal(int kind, int value)
{
    sGoal = kind;
    sGoalValue = value;
    sGoalSteps = 0;
    GoalStep();
}

static bool Ready(void)
{
    if (sR.phase == NATIVE_RELEARNER_FORGET)
        return sHaveSum && sSum.ready;
    return sR.ready;
}

/* SELECT / R presses from the list shown to list k (the engine skips the empty ones) */
static int StepsToList(int k)
{
    const struct NativeMoveRelearner *l = ListState();
    int steps = 0;

    for (int i = 1; i < MOVE_RELEARNER_COUNT; i++)
    {
        int s = (l->list + i) % MOVE_RELEARNER_COUNT;

        if (!l->listOn[s] || l->listCount[s] == 0)
            continue;
        steps++;
        if (s == k)
            return steps;
    }
    return 0;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (button == 3)
    {
        sGoal = GOAL_NONE;
        if (!NativeUi_PressesPending())
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    if (NativeUi_PressesPending() || sGoal != GOAL_NONE)
        return hit->id >= UI_HIT_SCREEN || hit->id == UI_HIT_YESNO;
    if (hit->id == UI_HIT_YESNO && sR.yesNo)
    {
        SetGoal(GOAL_YESNO, hit->index);
        return true;
    }
    switch (hit->id)
    {
    case HIT_ROW:
        if (sR.phase == NATIVE_RELEARNER_BROWSE)
        {
            if (hit->index >= sR.count)
                NativeUi_QueuePress(B_BUTTON); /* CANCEL: the engine's B, wherever its cursor is */
            else
                SetGoal(GOAL_ROW, hit->index);
        }
        return true;
    case HIT_TAB:
        if (sR.phase == NATIVE_RELEARNER_BROWSE && sR.switching)
            NativeUi_QueuePresses(R_BUTTON, StepsToList(hit->index));
        return true;
    case HIT_KNOWN:
        if (KnownCursor())
            SetGoal(GOAL_KNOWN, hit->index);
        return true;
    case HIT_DETAILS:
        if ((sR.phase == NATIVE_RELEARNER_BROWSE || KnownCursor()) && sR.contestData)
            NativeUi_QueuePress(DPAD_RIGHT);
        return true;
    case HIT_MESSAGE:
        if (sMsgWaiting && !sR.yesNo && sR.ready)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (dy == 0 || NativeUi_PressesPending() || sGoal != GOAL_NONE)
        return true;
    if ((sR.phase == NATIVE_RELEARNER_BROWSE && sR.ready) || (KnownCursor() && sSum.ready) || (sR.yesNo && sR.ready))
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_MoveSwitch = {
    .name = "moves",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
