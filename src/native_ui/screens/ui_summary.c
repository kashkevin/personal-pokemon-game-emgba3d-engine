#ifdef ENABLE_NATIVE_UI
/*
 * 5 · Summary (the approved design's preview/js/screens/summary.js): the
 * Trainer's clipboard with the clipped photo, the disc strip and the colour-coded
 * page tabs, over the engine's summary screen (pokemon_summary_screen.c),
 * which keeps every rule: which Pokémon and pages can be shown, renaming, the
 * move order, learning a move, the relearner. The game's pages are INFO,
 * SKILLS, BATTLE MOVES and CONTEST MOVES; the design's planned RIBBONS page
 * gives its tab to the game's CONTEST MOVES page (the ribbon count stays on the
 * name block). IVs and EVs: A on the SKILLS page shows them beside the stats for
 * the player's own Pokémon (the game's rule: ShouldShowIvEvPrompt).
 * Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "contest_effect.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "item.h"
#include "main.h"
#include "move.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "string_util.h"
#include "strings.h"
#include "battle_main.h"
#include "fpmath.h"
#include "pokedex.h"
#include "tv.h"
#include "constants/characters.h"
#include "constants/form_change_types.h"
#include "constants/party_menu.h"
#include "constants/pokedex.h"
#include "constants/items.h"
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
    HIT_TAB = UI_HIT_SCREEN,
    HIT_MON,
    HIT_UP,
    HIT_DOWN,
    HIT_MOVE,
};

/* the design's geometry (content coordinates) */
#define BX 56
#define BY 28
#define BW 576
#define BH 308
#define PX 64
#define PY 36
#define PW 560
#define PH 292
#define MX 246
#define CX 254
#define CR 614
#define ROW0 64
#define TAB_W 84
#define TAB_X0 270
#define ROW(k) (ROW0 + (k) * 16)

struct Page
{
    const char *tab;
    const char *title;
    UiRGBA colour;
};
static const struct Page sPages[PSS_PAGE_COUNT] = {
    [PSS_PAGE_INFO] = {"INFO", "POK" UI_CAPS_E_ACUTE "MON INFO", UI_P_CMD_FIGHT},
    [PSS_PAGE_SKILLS] = {"SKILLS", "POK" UI_CAPS_E_ACUTE "MON SKILLS", UI_P_CMD_RUN},
    [PSS_PAGE_BATTLE_MOVES] = {"MOVES", "BATTLE MOVES", UI_P_CMD_POKEMON},
    [PSS_PAGE_CONTEST_MOVES] = {"CONTEST", "CONTEST MOVES", UI_P_CMD_BAG},
};

/* ---------------------------------------------------------------- engine strings */

static const u8 sText_Hp[] = _("HP");
static const u8 sText_Attack[] = _("Attack");
static const u8 sText_Defense[] = _("Defense");
static const u8 sText_SpAtk[] = _("Sp. Atk");
static const u8 sText_SpDef[] = _("Sp. Def");
static const u8 sText_Speed[] = _("Speed");
static const u8 *const sStatNames[6] = {sText_Hp, sText_Attack, sText_Defense, sText_SpAtk, sText_SpDef, sText_Speed};
static const u8 sText_None[] = _("None");
static const u8 sText_Dash[] = _("---");
static const u8 sText_CantMega[] = _("Can't Mega Evolve.");
static const u8 sText_Holding[] = _("Holding its Mega Stone.");
static const u8 sText_OnePerBattle[] = _("One Mega per Trainer per battle.");
static const u8 sText_NeedsKeyStone[] = _("Needs a Key Stone to Mega Evolve.");
static const u8 sText_StoneInBag[] = _("Its stone is in your Bag.");
static const u8 sText_GiveToHold[] = _("Give it to hold to Mega Evolve.");
static const u8 sText_NoStone[] = _("You don't have this stone yet.");
static const u8 sText_KnowsMove[] = _("Mega Evolves knowing this move.");
static const u8 sText_TradePlanned[] = _("A record of its trades is planned.");
static const u8 sText_LuckyTitle[] = _("Lucky Pokémon");
static const u8 sText_LuckyLine[] = _("Became Lucky in a multiplayer trade.");
static const u8 sText_Raised[] = _("raised");
static const u8 sText_LoweredBy[] = _("lowered by its ");
static const u8 sText_NatureSuffix[] = _(" nature");
static const u8 sText_NoChange[] = _(" nature: no stat is raised or lowered");
static const u8 sText_MovesKnown1[] = _(" move known.");
static const u8 sText_MovesKnownN[] = _(" moves known.");
static const u8 sText_Physical[] = _("Physical");
static const u8 sText_Special[] = _("Special");
static const u8 sText_Status[] = _("Status");
static const u8 sText_NoDamaging[] = _("No damaging moves.");
static const u8 sText_ChooseMove[] = _("Choose a move to see its details or to change the order.");
static const u8 sText_ChooseMoveLocked[] = _("Choose a move to see its details.");
static const u8 sText_StatusMove[] = _("Status move: it deals no direct damage.");
static const u8 sText_NoWeak[] = _("No type is weak to it.");
static const u8 sText_Stab1[] = _("Same type as ");
static const u8 sText_Stab2[] = _(": 1.5× power.");
static const u8 sText_HmNote[] = _("An HM move: it can't be forgotten when learning a new move.");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Following[] = _("Following you");
static const u8 sText_ContestCard[] = _("Contest moves: choose one to see its appeal.");
static const u8 sText_Appeal[] = _("APPEAL");
static const u8 sText_ForgetHint[] = _("Choose a move to forget for ");
static const u8 sText_Page[] = _("Page");
static const u8 sText_Pokemon[] = _("Pokémon");
static const u8 sText_BoxPokemon[] = _("Box Pokémon");
static const u8 sText_Rename[] = _("Rename");
static const u8 sText_CheckMoves[] = _("Check moves");
static const u8 sText_Relearn[] = _("Relearn");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Choose[] = _("Choose");
static const u8 sText_MoveIt[] = _("Move");
static const u8 sText_PutHere[] = _("Put here");
static const u8 sText_Done[] = _("Done");
static const u8 sText_Forget[] = _("Forget");
static const u8 sText_DontLearn[] = _("Don't learn");
static const u8 sText_StateLabel[] = _("It is an Egg.");
static const u8 sText_ShowIvEv[] = _("Show IVs/EVs");
static const u8 sText_HideIvEv[] = _("Hide IVs/EVs");
static const u8 sText_HyperTrained[] = _("HT: Hyper Trained, counts as 31.");

/* ---------------------------------------------------------------- captured state */

struct MoveView
{
    u16 move;
    const u8 *name;
    s8 type;
    u8 category;            /* 0 physical, 1 special, 2 status */
    u16 power;
    u8 accuracy;
    s8 priority;
    const u8 *desc;
    bool8 hm;
    u8 pp;
    u8 maxPP;
    u8 contestCategory;
    u8 appeal;
    u8 jam;
    const u8 *contestDesc;
    u32 strongVs;           /* bit: an enum UiTypeId it hits for double damage */
};

static struct NativeSummary sS;
static struct UiMon sMon;
static struct MoveView sMoves[MAX_MON_MOVES + 1]; /* the four, then the move to learn */
static u16 sStats[6];
static u8 sIvs[6];               /* as the game counts them: a Hyper Trained stat is 31 */
static bool sIvTrained[6];       /* Hyper Trained */
static u16 sEvs[6];
static int sEvTotal;
static int sFriendship;
static u32 sExpPoints;
static u32 sExpToNext;
static const u8 *sSpeciesName;
static u16 sDexNum;
static int sDexDigits;
static const u8 *sAbilityDesc;
static s8 sNatureUp = -1;        /* display index (0 HP .. 5 Speed) */
static s8 sNatureDown = -1;
static const u8 *sNatureName;
static u16 sMegaStone;
static u16 sMegaMove;
static bool sHasMega;
static bool sMegaKnowsMove;
static bool sHasKeyStone;
static bool sStoneInBag;
static u8 sBoxName[16];
static bool sFollowing;
static int sSwitchT;             /* frames since this Pokémon came up (its front animation) */
static int sShownIndex = -1;
static int sCats[3];
static int sPPLeft;
static int sPPTotal;
static u32 sCoverage;
static bool sPlayerOT;
/* a mouse goal: the engine's own presses until its cursor gets there */
enum
{
    GOAL_NONE,
    GOAL_MON,
    GOAL_MOVE,
    GOAL_MOVE2,
};
static int sGoal;
static int sGoalValue;
static bool sGoalThenA;
static int sGoalSteps;

/* display index -> STAT_* */
static const u8 sStatOrder[6] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};
static const u8 sIvField[6] = {MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV,
                               MON_DATA_SPEED_IV};
static const u8 sEvField[6] = {MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV,
                               MON_DATA_SPEED_EV};
static const u8 sStatField[6] = {MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPATK, MON_DATA_SPDEF,
                                 MON_DATA_SPEED};

static int DisplayIndexOfStat(int stat)
{
    for (int i = 0; i < 6; i++)
        if (sStatOrder[i] == stat)
            return i;
    return -1;
}

/* the engine's types in the design's badge order */
static int EngineTypeOf(int uiType)
{
    static const u8 sMap[UI_TYPE_COUNT] = {
        [UI_TYPE_NORMAL] = TYPE_NORMAL, [UI_TYPE_FIRE] = TYPE_FIRE, [UI_TYPE_WATER] = TYPE_WATER,
        [UI_TYPE_ELECTRIC] = TYPE_ELECTRIC, [UI_TYPE_GRASS] = TYPE_GRASS, [UI_TYPE_ICE] = TYPE_ICE,
        [UI_TYPE_FIGHTING] = TYPE_FIGHTING, [UI_TYPE_POISON] = TYPE_POISON, [UI_TYPE_GROUND] = TYPE_GROUND,
        [UI_TYPE_FLYING] = TYPE_FLYING, [UI_TYPE_PSYCHIC] = TYPE_PSYCHIC, [UI_TYPE_BUG] = TYPE_BUG,
        [UI_TYPE_ROCK] = TYPE_ROCK, [UI_TYPE_GHOST] = TYPE_GHOST, [UI_TYPE_DRAGON] = TYPE_DRAGON,
        [UI_TYPE_DARK] = TYPE_DARK, [UI_TYPE_STEEL] = TYPE_STEEL, [UI_TYPE_FAIRY] = TYPE_FAIRY,
    };
    return uiType >= 0 && uiType < UI_TYPE_COUNT ? sMap[uiType] : TYPE_NONE;
}

/* the types the game's own chart says a type hits for double damage */
static u32 StrongVs(int engineType)
{
    u32 mask = 0;

    if (engineType <= TYPE_NONE || engineType >= NUMBER_OF_MON_TYPES)
        return 0;
    for (int t = 0; t < UI_TYPE_COUNT; t++)
        if (gTypeEffectivenessTable[engineType][EngineTypeOf(t)] >= UQ_4_12(2.0))
            mask |= 1u << t;
    return mask;
}

static void CaptureMove(struct MoveView *v, u16 move, u8 pp, u8 maxPP)
{
    memset(v, 0, sizeof(*v));
    v->move = move;
    if (move == MOVE_NONE)
        return;
    v->name = GetMoveName(move);
    v->type = (s8)UiMon_Type(GetMoveType(move));
    v->category = GetMoveCategory(move) == DAMAGE_CATEGORY_PHYSICAL ? 0
                : GetMoveCategory(move) == DAMAGE_CATEGORY_SPECIAL ? 1 : 2;
    v->power = GetMovePower(move);
    v->accuracy = GetMoveAccuracy(move);
    v->priority = (s8)GetMovePriority(move);
    v->desc = GetMoveDescription(move);
    v->hm = IsMoveHM(move);
    v->pp = pp;
    v->maxPP = maxPP;
    v->contestCategory = GetMoveContestCategory(move);
    v->appeal = gContestEffects[GetMoveContestEffect(move)].appeal;
    v->jam = gContestEffects[GetMoveContestEffect(move)].jam;
    v->contestDesc = gContestEffects[GetMoveContestEffect(move)].description;
    if (v->category != 2)
        v->strongVs = StrongVs(GetMoveType(move));
}

static void CaptureMega(void)
{
    const struct FormChange *fc = GetSpeciesFormChanges(sMon.species);

    sHasMega = sMegaKnowsMove = sStoneInBag = false;
    sMegaStone = ITEM_NONE;
    sMegaMove = MOVE_NONE;
    sHasKeyStone = CheckBagHasItem(ITEM_MEGA_RING, 1);
    for (int i = 0; fc != NULL && fc[i].method != FORM_CHANGE_TERMINATOR; i++)
    {
        if (fc[i].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM)
        {
            /* a species with two Mega forms: the one its held item gives, else the first */
            if (!sHasMega || fc[i].param1 == sMon.heldItem)
                sMegaStone = fc[i].param1;
            sHasMega = true;
        }
        else if (fc[i].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE && !sHasMega)
        {
            sMegaMove = fc[i].param1;
            sHasMega = true;
        }
    }
    if (sMegaStone != ITEM_NONE)
    {
        sStoneInBag = CheckBagHasItem(sMegaStone, 1);
        UiGfx_PrepareItemIcon(sMegaStone);
    }
    for (int i = 0; sMegaMove != MOVE_NONE && i < sMon.moveCount; i++)
        if (sMon.moves[i] == sMegaMove)
            sMegaKnowsMove = true;
}

static bool Detect(void)
{
    struct NativeSummary s;

    if (!Summary_NativeUiState(&s))
        return false;
    return s.phase != NATIVE_SUMMARY_NONE;
}

static void GoalStep(void)
{
    int current;

    if (sGoal == GOAL_NONE || NativeUi_PressesPending() || !sS.ready)
        return;
    current = sGoal == GOAL_MON ? sS.monIndex : sGoal == GOAL_MOVE ? sS.moveCursor : sS.moveCursor2;
    if (current == sGoalValue)
    {
        if (sGoalThenA)
            NativeUi_QueuePress(A_BUTTON);
        sGoal = GOAL_NONE;
        return;
    }
    if (++sGoalSteps > 8)
    {
        sGoal = GOAL_NONE; /* the engine does not go there (a locked list, an empty row) */
        return;
    }
    NativeUi_QueuePress(sGoalValue > current ? DPAD_DOWN : DPAD_UP);
}

static void Capture(void)
{
    struct Pokemon *mon;
    int up, down;

    Summary_NativeUiState(&sS);
    mon = &sS.mon;
    if (sS.phase == NATIVE_SUMMARY_LOADING && sS.listCount == 0)
        return;
    UiMon_FromPokemon(&sMon, mon);
    if (sS.monIndex != sShownIndex)
    {
        sShownIndex = sS.monIndex;
        sSwitchT = 0;
    }
    else
    {
        sSwitchT++;
    }
    sSpeciesName = GetSpeciesName(sMon.species);
    sDexNum = SpeciesToPokedexNum(sMon.species);
    sDexDigits = (NATIONAL_DEX_COUNT > 999 && IsNationalPokedexEnabled()) ? 4 : 3;
    sAbilityDesc = sMon.isEgg ? NULL : gAbilitiesInfo[sMon.ability].description;
    for (int i = 0; i < 6; i++)
    {
        sStats[i] = GetMonData(mon, sStatField[i]);
        sIvTrained[i] = GetMonData(mon, MON_DATA_HYPER_TRAINED_HP + sStatOrder[i]) != 0;
        /* the summary's own rule (pokemon_summary_screen.c GetAdjustedIvData) */
        sIvs[i] = (P_SUMMARY_SCREEN_IV_HYPERTRAIN && sIvTrained[i]) ? MAX_PER_STAT_IVS : GetMonData(mon, sIvField[i]);
        sEvs[i] = GetMonData(mon, sEvField[i]);
    }
    sEvTotal = 0;
    for (int i = 0; i < 6; i++)
        sEvTotal += sEvs[i];
    sFriendship = sMon.isEgg ? -1 : (int)GetMonData(mon, MON_DATA_FRIENDSHIP);
    up = gNaturesInfo[sS.statNature].statUp;
    down = gNaturesInfo[sS.statNature].statDown;
    sNatureUp = up != down ? (s8)DisplayIndexOfStat(up) : -1;
    sNatureDown = up != down ? (s8)DisplayIndexOfStat(down) : -1;
    sNatureName = gNaturesInfo[sS.statNature].name;
    sExpPoints = GetMonData(mon, MON_DATA_EXP);
    sExpToNext = 0;
    if (!sMon.isEgg && sMon.level < MAX_LEVEL)
    {
        u32 next = gExperienceTables[gSpeciesInfo[sMon.species].growthRate][sMon.level + 1];

        sExpToNext = next > sExpPoints ? next - sExpPoints : 0;
    }
    /* the moves as the summary has them (slot order), and the one to learn */
    memset(sMoves, 0, sizeof(sMoves));
    sCats[0] = sCats[1] = sCats[2] = 0;
    sPPLeft = sPPTotal = 0;
    sCoverage = 0;
    for (int i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);

        CaptureMove(&sMoves[i], move, GetMonData(mon, MON_DATA_PP1 + i),
                    CalculatePPWithBonus(move, GetMonData(mon, MON_DATA_PP_BONUSES), i));
        if (move == MOVE_NONE)
            continue;
        sCats[sMoves[i].category]++;
        sPPLeft += sMoves[i].pp;
        sPPTotal += sMoves[i].maxPP;
        sCoverage |= sMoves[i].strongVs;
    }
    CaptureMove(&sMoves[MAX_MON_MOVES], sS.newMove, GetMovePP(sS.newMove), GetMovePP(sS.newMove));
    if (!sMon.isEgg)
        CaptureMega();
    UiGfx_PrepareMonPic(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg);
    if (sMon.heldItem != ITEM_NONE)
        UiGfx_PrepareItemIcon(sMon.heldItem);
    if (sS.ballItem != ITEM_NONE)
        UiGfx_PrepareItemIcon(sS.ballItem);
    sBoxName[0] = EOS;
    if (sS.isBoxMon)
        UiText_CopyN(sBoxName, sizeof(sBoxName), GetBoxNamePtr(StorageGetCurrentBox()),
                     UiText_Length(GetBoxNamePtr(StorageGetCurrentBox())));
    /* the lead follows the player in the field (the party, outside battle) */
    sFollowing = false;
    if (!sS.isBoxMon && !gMain.inBattle && sS.mode == SUMMARY_MODE_NORMAL)
    {
        struct ObjectEvent *follower = GetFollowerObject();
        struct Pokemon *lead = GetFirstLiveMon();

        sFollowing = follower != NULL && !follower->invisible && lead != NULL
                  && lead == &gParties[B_TRAINER_PLAYER][sS.monIndex];
    }
    sPlayerOT = sS.otId == GetPlayerIDAsU32() && StringCompareWithoutExtCtrlCodes(sS.otName, gSaveBlock2Ptr->playerName) == 0;
    GoalStep();
}

/* ---------------------------------------------------------------- drawing */

static struct UiRamp Slate(void)
{
    return UiRampOf(UiMix(UI_P_INK, UI_P_TYPES_STEEL, 0.42), 0.35, 0.3, 0.6);
}

static void Label(int x, int y, const char *s)
{
    UiCaps_Draw(x, y + 5, s, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

static int TextC(int x, int y, const u8 *s, UiRGBA fg, UiRGBA sh)
{
    return UiText_Draw(UI_FONT_NORMAL, x, y, s, fg, sh, UI_ALIGN_LEFT, 0);
}

static int TextR(int x, int y, const u8 *s)
{
    return UiText_Draw(UI_FONT_NORMAL, x, y, s, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
}

static int Small(int x, int y, const u8 *s, UiRGBA fg)
{
    return UiText_Draw(UI_FONT_SMALL, x, y, s, fg, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

static void Number(u8 *dst, int size, long value, bool thousands)
{
    dst[0] = EOS;
    UiText_AppendNumber(dst, size, value, 1, thousands);
}

/* a thin page-tinted band with a caps heading */
static void Band(int y, const char *s, const struct UiRamp *pr)
{
    UiG_Rect(CX + 2, y, CR - CX - 2, 12, UiMix(pr->hi, UI_P_CREAM, 0.45));
    UiG_HLine(CX + 2, y + 11, CR - CX - 2, UiMix(pr->base, UI_P_CREAM, 0.55));
    UiCaps_Draw(CX + 6, y + 2, s, pr->ink, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
}

static void Background(const struct UiRamp *page)
{
    int x0, y0, w, h;
    UiRGBA pc = page->base;
    struct UiPattern v = UiPat_VStripe(16, 8, 0), hs = UiPat_HStripe(16, 8, 0), both = UiPat_VStripe(16, 8, 0);

    both.and_ = &hs;
    UiG_Bleed(&x0, &y0, &w, &h);
    UiG_Rect(x0, y0, w, h, UiMix(pc, UI_P_CREAM, 0.82));
    UiG_FillPattern(&v, x0, y0, w, h, UiMix(pc, UI_P_CREAM, 0.66), 0, 0);
    UiG_FillPattern(&hs, x0, y0, w, h, UiMix(pc, UI_P_CREAM, 0.66), 0, 0);
    UiG_FillPattern(&both, x0, y0, w, h, UiMix(pc, UI_P_CREAM, 0.5), 0, 0);
}

static void Strip(int t, const struct UiRamp *sl)
{
    int n = sS.listCount, first, y0 = 44;

    if (sS.isBoxMon)
        UiCaps_Draw(28, 18, "BOX", UI_P_TEXT_W, sl->ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
    first = sS.listPos - 2;
    if (first > n - 6)
        first = n - 6;
    if (first < 0)
        first = 0;
    UiW_DownArrow(25, 33 - ((t / 16) % 2), UI_P_CURSOR, true);
    UiG_Hit(HIT_UP, 0, 14, 26, 30, 14);
    for (int k = 0; k < 6 && first + k < n; k++)
    {
        const struct NativeSummaryEntry *e = &sS.list[first + k];
        int y = y0 + k * 46, frame;
        bool on = first + k == sS.listPos;
        struct UiRamp r;
        const struct UiMask *disc = UiM_Circle(38);
        static const struct UiFilter sGray = {0.65f, 0, 0, 0};

        if (on)
            r = (struct UiRamp){UI_R_SLOT_SEL_HI, UI_R_SLOT_SEL_BASE, UI_R_SLOT_SEL_LO, UI_R_SLOT_SEL_INK};
        else if (e->fainted)
            r = (struct UiRamp){UI_R_SLOT_FNT_HI, UI_R_SLOT_FNT_BASE, UI_R_SLOT_FNT_LO, UI_R_SLOT_FNT_INK};
        else
            r = (struct UiRamp){sl->hi, UiMix(sl->base, UI_P_CREAM, 0.5), sl->base, sl->ink};
        if (on)
            UiW_SelectFrame(disc, 9, y, UI_NONE, UI_NONE, 2);
        UiG_Paint(disc, 9, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, 0});
        frame = on ? (t / 10) % 2 : e->fainted ? 0 : ((t + (first + k) * 7) / 20) % 2;
        UiGfx_MonIcon(e->species, e->personality, e->isEgg, frame, 12, y - 1 - (on && frame ? 2 : 0),
                      e->fainted ? &sGray : NULL);
        UiG_Hit(HIT_MON, e->index, 9, y, 38, 38);
    }
    UiW_DownArrow(25, 44 + 6 * 46 - 4 + ((t / 16) % 2), UI_P_CURSOR, false);
    UiG_Hit(HIT_DOWN, 0, 14, 312, 30, 14);
}

static void ClipAndPhoto(const struct UiRamp *sl)
{
    int fx = 72, fy = 48, fw = 168, fh = 150, feet, cx, frame;
    const struct UiRamp *cs = &gUiTypeRamps[UI_TYPE_STEEL];
    u8 cap[48];
    const struct UiMask *lever;

    UiG_Rect(fx + 2, fy + fh, fw - 2, 2, UiMix(UI_P_CREAM_LO, sl->lo, 0.35));
    UiG_Paint(UiM_Rect(fw, fh), fx, fy, UI_P_PAPER,
              &(struct UiPaint){UiMix(UI_P_PAPER_LO, sl->ink, 0.5), UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiMon_Stage(fx + 6, fy + 6, fw - 12, fh - 30, sMon.types[0], sMon.types[1], 30, 70, &feet, &cx);
    UiG_Save();
    UiG_Clip(fx + 6, fy + 6, fw - 12, fh - 30);
    frame = UiGfx_MonPicFrameAt(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg, sSwitchT);
    UiGfx_MonPic(sMon.species, sMon.personality, sMon.shiny, sMon.isEgg, frame, cx, feet, NULL);
    UiG_Restore();
    if (sMon.shiny)
        UiMon_Star(fx + 10, fy + 10);
    /* the caption: the game's number (LUCKY in its place, as the game shows it) and species */
    cap[0] = EOS;
    if (sMon.isEgg)
    {
        UiText_Append(cap, sizeof(cap), sMon.nickname);
    }
    else
    {
        if (sS.isLucky)
            UiText_AppendAscii(cap, sizeof(cap), "LUCKY");
        else if (sDexNum == 0xFFFF)
            UiText_AppendAscii(cap, sizeof(cap), "No.---");
        else
        {
            UiText_AppendAscii(cap, sizeof(cap), "No.");
            UiText_AppendNumber(cap, sizeof(cap), sDexNum, sDexDigits, false);
        }
        UiText_AppendAscii(cap, sizeof(cap), " ");
        UiText_Append(cap, sizeof(cap), sSpeciesName);
    }
    UiText_Draw(UI_FONT_SMALL, fx + (fw >> 1), fy + fh - 21, cap, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
    /* the steel clip holding photo and paper */
    cx = fx + (fw >> 1);
    lever = UiM_Sub(UiM_Rounded4(64, 15, 6, 6, 1, 1), UiM_Place(UiM_Rect(64, 15), UiM_Rounded4(38, 6, 2, 2, 2, 2), 13, 4));
    UiG_Paint(lever, cx - 32, 14, cs->base, &(struct UiPaint){cs->ink, cs->hi, cs->lo, 0, 0, NULL, 0});
    UiG_Paint(UiM_Rounded4(96, 16, 3, 3, 3, 3), cx - 48, 26, cs->base,
              &(struct UiPaint){cs->ink, UiMix(cs->hi, UI_HEX(0xFFFFFF), 0.4), cs->lo, 0, 2, NULL, 0});
    UiG_Paint(UiM_Rounded4(76, 8, 0, 0, 3, 3), cx - 38, 41, UiMix(cs->base, cs->lo, 0.4),
              &(struct UiPaint){cs->ink, cs->hi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Circle(5), cx - 42, 31, cs->lo, &(struct UiPaint){cs->ink, cs->hi, UI_NONE, 0, 0, NULL, 0});
    UiG_Paint(UiM_Circle(5), cx + 38, 31, cs->lo, &(struct UiPaint){cs->ink, cs->hi, UI_NONE, 0, 0, NULL, 0});
}

static void NameBlock(int t)
{
    int x = 70, y = 202, nx, rx = 240, iy, hy, ly;
    static const char *const sRibbon[] = {".aaaaa.", "abbbbba", "abcccba", "abbbbba", ".aaaaa.", ".ab.ba.", ".ab.ba.",
                                          "ab...ba", "a.....a"};
    static const struct UiPixPal sRibbonPal[] = {{'a', UI_HEX(0x8A5A10)}, {'b', UI_HEX(0xE8B030)}, {'c', UI_HEX(0xFFF0B0)}};

    (void)t;
    if (sS.ballItem != ITEM_NONE)
        UiGfx_ItemIcon(sS.ballItem, x, y - 4, NULL);
    nx = UiG_Text(x + 26, y, sMon.nickname);
    if (!sMon.isEgg)
    {
        u8 lv[16];

        UiW_Gender(nx + 1, y, sMon.gender);
        UiMon_LevelText(lv, sizeof(lv), sMon.level);
        TextR(240, y, lv);
    }
    UiMon_Markings(x + 28, y + 23, sS.markings, UI_P_INK, UI_P_CREAM_LO);
    if (sS.ribbonCount > 0)
    {
        char n[16];

        UiW_Pix("pkminirib", sRibbon, 9, sRibbonPal, 3, x + 72, y + 22, false);
        snprintf(n, sizeof(n), UI_CAPS_TIMES "%u", (unsigned)sS.ribbonCount);
        UiCaps_Draw(x + 82, y + 24, n, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    if (!sMon.isEgg)
    {
        UiRGBA sc;
        const char *status = UiMon_StatusLabel(&sMon, &sc);

        if (status != NULL && sMon.ailment != AILMENT_PKRS)
            rx = UiW_StatusBadge(rx - 30, y + 21, status, sc, 30) - 30 - 4;
        if (sS.isLucky)
            rx = UiMon_Tag(rx - (UiCaps_Width("LUCKY") + 10), y + 21, "LUCKY", UI_P_CMD_BAG) - (UiCaps_Width("LUCKY") + 10) - 4;
        if (sMon.shiny)
        {
            UiMon_Star(rx - 10, y + 22);
            rx -= 13;
        }
    }
    /* held item */
    iy = y + 40;
    UiG_HLine(x + 2, iy - 4, 168, UI_P_CREAM_LO);
    UiCaps_Draw(x + 4, iy + 5, "ITEM", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (sMon.heldItem != ITEM_NONE)
    {
        UiGfx_ItemIcon(sMon.heldItem, x + 30, iy - 4, NULL);
        UiG_Text(x + 56, iy, sMon.heldItemName);
    }
    else
    {
        TextC(x + 34, iy, sText_None, UI_P_TEXT_MUTED, UI_P_TEXT_SH);
    }
    /* HP at a glance */
    hy = iy + 20;
    UiG_HLine(x + 2, hy - 3, 168, UI_P_CREAM_LO);
    if (!sMon.isEgg)
    {
        u8 hp[24];

        UiW_HpBar(x + 4, hy + 4, 92, sMon.maxHP > 0 ? (double)sMon.hp / sMon.maxHP : 0, true, UI_NONE);
        Number(hp, sizeof(hp), sMon.hp, false);
        UiText_AppendAscii(hp, sizeof(hp), "/");
        UiText_AppendNumber(hp, sizeof(hp), sMon.maxHP, 1, false);
        TextR(240, hy, hp);
    }
    /* where it is: following the player, in a box, in the party */
    ly = hy + 20;
    UiG_HLine(x + 2, ly - 3, 168, UI_P_CREAM_LO);
    if (sFollowing)
    {
        UiGfx_MonIcon(sMon.species, sMon.personality, false, (t / 16) % 2, x - 2, ly - 10, NULL);
        UiG_Text(x + 38, ly + 2, sText_Following);
    }
    else if (sS.isBoxMon)
    {
        char box[40];
        char name[24];

        UiText_ToAscii(name, sizeof(name), sBoxName);
        snprintf(box, sizeof(box), "BOX %s", name);
        UiCaps_Draw(x + 4, ly + 9, box, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    else
    {
        UiCaps_Draw(x + 4, ly + 9, gMain.inBattle ? "IN BATTLE" : "IN YOUR PARTY", UI_P_TEXT_MUTED, UI_NONE,
                    UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
    {
        char pos[16];

        snprintf(pos, sizeof(pos), "%d/%d", sS.listPos + 1, sS.listCount);
        UiCaps_Draw(238, ly + 9, pos, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
}

/* the memo's lines (engine text with its colour codes), one per row */
static void MemoLines(int x, int row, const u8 *text, int maxRows)
{
    const u8 *p = text;

    for (int r = 0; r < maxRows && *p != EOS; r++)
    {
        u8 line[96];
        int n = 0;

        while (p[n] != EOS && p[n] != CHAR_NEWLINE && n < (int)sizeof(line) - 1)
            n++;
        UiText_CopyN(line, sizeof(line), p, n);
        UiG_Text(x, ROW(row + r), line);
        p += n;
        if (*p == CHAR_NEWLINE)
            p++;
    }
}

static void InfoPage(const struct UiRamp *pr)
{
    int lx = CX + 6, vx = CX + 74, rx = CX + 196, my, hy, tx;
    u8 buf[32];
    static const UiRGBA sGold[3] = {UI_HEX(0xE0A020), UI_HEX(0xF8D870), UI_HEX(0xFFFFFF)};

    Label(lx, ROW(0), "DEX NO.");
    if (sMon.isEgg || sDexNum == 0xFFFF)
        UiG_Text(vx, ROW(0), sText_Dash);
    else
    {
        buf[0] = EOS;
        UiText_AppendNumber(buf, sizeof(buf), sDexNum, sDexDigits, false);
        UiG_Text(vx, ROW(0), buf);
    }
    Label(lx, ROW(1), "NAME");
    UiG_Text(vx, ROW(1), sMon.isEgg ? sMon.nickname : sSpeciesName);
    Label(lx, ROW(2), "TYPE");
    tx = vx;
    for (int i = 0; i < 2 && !sMon.isEgg; i++)
        if (sMon.types[i] >= 0)
            tx = UiW_TypeBadge(tx, ROW(2) + 1, sMon.types[i], 0) + 3;
    Label(lx, ROW(3), "OT");
    Label(lx, ROW(4), "ID NO.");
    if (sMon.isEgg || sS.hideOT)
    {
        UiG_Text(vx, ROW(3), gText_FiveMarks);
        UiG_Text(vx, ROW(4), gText_FiveMarks);
    }
    else
    {
        TextC(vx, ROW(3), sS.otName, sS.otGender == FEMALE ? UI_P_FEMALE : UI_P_MALE,
              sS.otGender == FEMALE ? UI_P_FEMALE_SH : UI_P_MALE_SH);
        buf[0] = EOS;
        UiText_AppendNumber(buf, sizeof(buf), (u16)sS.otId, 5, false);
        UiG_Text(vx, ROW(4), buf);
    }
    /* right half: Mega Evolution from the game's forms, stones and Key Stone; an Egg's state */
    UiG_VLine(rx - 8, ROW(0) + 2, 76, UI_P_CREAM_LO);
    if (sMon.isEgg)
    {
        Label(rx, ROW(0), "STATE");
        if (sS.eggState != NULL)
            UiW_MessagePage(sS.eggState, -1, rx, ROW(1), CR - rx - 4, 3);
    }
    else
    {
        Label(rx, ROW(0), "MEGA EVOLUTION");
        if (!sHasMega)
        {
            TextC(rx, ROW(1), sText_CantMega, UI_P_TEXT_MUTED, UI_P_TEXT_SH);
        }
        else if (sMegaStone != ITEM_NONE)
        {
            bool held = sMon.heldItem == sMegaStone;

            UiGfx_ItemIcon(sMegaStone, rx - 4, ROW(1) - 4, NULL);
            UiG_Text(rx + 22, ROW(1), GetItemName(sMegaStone));
            if (held)
            {
                UiMon_Tag(rx, ROW(2) + 3, sHasKeyStone ? "READY" : "NO KEY STONE",
                          sHasKeyStone ? UI_P_CMD_POKEMON : UI_HEX(0xA0A0A8));
                Small(rx, ROW(3) + 2, sText_Holding, UI_P_TEXT);
                Small(rx, ROW(4) + 1, sHasKeyStone ? sText_OnePerBattle : sText_NeedsKeyStone, UI_P_TEXT);
            }
            else if (sStoneInBag)
            {
                UiMon_Tag(rx, ROW(2) + 3, "STONE IN BAG", UI_P_CMD_BAG);
                Small(rx, ROW(3) + 2, sText_StoneInBag, UI_P_TEXT);
                Small(rx, ROW(4) + 1, sText_GiveToHold, UI_P_TEXT);
            }
            else
            {
                UiMon_Tag(rx, ROW(2) + 3, "NO STONE", UI_HEX(0xA0A0A8));
                Small(rx, ROW(3) + 2, sText_NoStone, UI_P_TEXT);
            }
        }
        else
        {
            UiG_Text(rx, ROW(1), GetMoveName(sMegaMove));
            UiMon_Tag(rx, ROW(2) + 3, sMegaKnowsMove && sHasKeyStone ? "READY" : sMegaKnowsMove ? "NO KEY STONE" : "NEEDS MOVE",
                      sMegaKnowsMove && sHasKeyStone ? UI_P_CMD_POKEMON : UI_HEX(0xA0A0A8));
            Small(rx, ROW(3) + 2, sText_KnowsMove, UI_P_TEXT);
            if (!sHasKeyStone)
                Small(rx, ROW(4) + 1, sText_NeedsKeyStone, UI_P_TEXT);
        }
    }
    /* the Trainer Memo, as the game writes it */
    my = ROW(6);
    Band(my - 14, "TRAINER MEMO", pr);
    MemoLines(lx, 6, sS.memo, 4);
    /* trade history: planned; a Lucky Pokémon is the game's own */
    hy = ROW(11);
    Band(hy - 14, "TRADE HISTORY", pr);
    UiW_Planned(CR - 58, hy - 15, NULL);
    TextC(lx, hy, sText_TradePlanned, UI_P_TEXT_MUTED, UI_P_TEXT_SH);
    if (sS.isLucky)
    {
        int ly = ROW(12) + 3;

        UiG_Paint(UiM_Rounded4(CR - CX - 8, 30, 2, 2, 2, 2), CX + 4, ly, UiMix(UI_P_CMD_BAG, UI_P_CREAM, 0.78),
                  &(struct UiPaint){UiRampDefault(UI_P_CMD_BAG).ink, UiMix(UI_P_CMD_BAG, UI_HEX(0xFFFFFF), 0.85),
                                    UiMix(UI_P_CMD_BAG, UI_P_CREAM, 0.5), 0, 0, NULL, 0});
        UiMon_Sparkle(CX + 15, ly + 9, true, sGold);
        UiMon_Sparkle(CX + 23, ly + 21, false, sGold);
        UiG_Text(CX + 32, ly + 2, sText_LuckyTitle);
        Small(CX + 136, ly + 9, sText_LuckyLine, UI_P_TEXT);
    }
}

/* the IV and EV columns: the player's own Pokémon (the game's rule), turned on with A */
static bool IvEvOn(void)
{
    return sS.ivEvShown && !sMon.isEgg && sS.skillsMode != SUMMARY_SKILLS_MODE_STATS;
}

static void SkillsPage(const struct UiRamp *pr)
{
    int lx = CX + 6, cVal = CX + 196, cIv = CX + 246, cEv = CX + 300;
    u8 buf[32];
    bool ivEv = IvEvOn(), anyTrained = false;

    Label(cVal - UiCaps_Width("STAT"), ROW(0), "STAT");
    if (ivEv)
    {
        Label(cIv - UiCaps_Width("IV"), ROW(0), "IV");
        Label(cEv - UiCaps_Width("EV"), ROW(0), "EV");
    }
    for (int i = 0; i < 6; i++)
    {
        int y = ROW(i + 1);
        UiRGBA fg = UI_P_TEXT, sh = UI_P_TEXT_SH;

        if (i == sNatureUp)
            fg = UI_P_TEXT_RED, sh = UI_P_TEXT_RED_SH;
        else if (i == sNatureDown)
            fg = UI_P_MALE, sh = UI_P_MALE_SH;
        if (i == 0)
        {
            UiG_Text(lx, y, sStatNames[0]);
            UiW_HpBar(lx + 34, y + 5, 74, sStats[0] > 0 ? (double)sMon.hp / sStats[0] : 0, false, UI_NONE);
            Number(buf, sizeof(buf), sMon.hp, false);
            UiText_AppendAscii(buf, sizeof(buf), "/");
            UiText_AppendNumber(buf, sizeof(buf), sStats[0], 1, false);
            TextR(cVal, y, buf);
        }
        else
        {
            int ex = TextC(lx, y, sStatNames[i], fg, sh);

            if (i == sNatureUp || i == sNatureDown)
                UiCaps_Draw(ex + 4, y + 5, i == sNatureUp ? UI_CAPS_UP : UI_CAPS_DOWN, fg, UI_NONE, UI_CAPS_PLAIN,
                            UI_ALIGN_LEFT, UI_NONE);
            Number(buf, sizeof(buf), sStats[i], false);
            TextR(cVal, y, buf);
        }
        if (ivEv)
        {
            int ew = (int)lrint(40.0 * (sEvs[i] < 252 ? sEvs[i] : 252) / 252.0);

            Number(buf, sizeof(buf), sIvs[i], false);
            TextR(cIv, y, buf);
            if (sIvTrained[i])
            {
                UiCaps_Draw(cIv + 3, y + 5, "HT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
                anyTrained = true;
            }
            Number(buf, sizeof(buf), sEvs[i], false);
            TextR(cEv, y, buf);
            UiG_Rect(cEv + 8, y + 6, 42, 4, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3));
            if (ew > 0)
                UiG_Rect(cEv + 9, y + 7, ew, 2, UiMix(pr->base, UI_P_CREAM, 0.15));
        }
    }
    if (ivEv)
    {
        char tot[32];

        snprintf(tot, sizeof(tot), "EV TOTAL %d/%d", sEvTotal, MAX_TOTAL_EVS);
        UiCaps_Draw(CR - 6, ROW(7) + 5, tot, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        if (anyTrained)
            Small(lx, ROW(8) + 2, sText_HyperTrained, UI_P_TEXT_MUTED);
    }
    if (sNatureUp >= 0)
    {
        int x = UiCaps_Draw(lx, ROW(7) + 5, UI_CAPS_UP, UI_P_TEXT_RED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);

        x = Small(x + 3, ROW(7) + 2, sText_Raised, UI_P_TEXT);
        x = UiCaps_Draw(x + 8, ROW(7) + 5, UI_CAPS_DOWN, UI_P_MALE, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        x = Small(x + 3, ROW(7) + 2, sText_LoweredBy, UI_P_TEXT);
        x = Small(x, ROW(7) + 2, sNatureName, UI_P_TEXT);
        Small(x, ROW(7) + 2, sText_NatureSuffix, UI_P_TEXT);
    }
    else
    {
        int x = Small(lx, ROW(7) + 2, sNatureName, UI_P_TEXT);

        Small(x, ROW(7) + 2, sText_NoChange, UI_P_TEXT);
    }
    if (sFriendship >= 0)
    {
        /* the owner (2026-10-25): the Pokemon's friendship, under its stats (the IV/EV view's HT note is on the left) */
        char fr[32];

        snprintf(fr, sizeof(fr), "FRIENDSHIP %d/%d", sFriendship, MAX_FRIENDSHIP);
        UiCaps_Draw(CR - 6, ROW(8) + 5, fr, UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    }
    Label(lx, ROW(9), "EXP. POINTS");
    Number(buf, sizeof(buf), sExpPoints, true);
    TextR(CR - 6, ROW(9), buf);
    Label(lx, ROW(10), "TO NEXT LV.");
    Number(buf, sizeof(buf), sExpToNext, true);
    TextR(CR - 6, ROW(10), buf);
    UiW_ExpBar(lx + 2, ROW(11) + 6, CR - CX - 26, sMon.expFrac, true);
    Label(lx, ROW(13), "ABILITY");
    UiG_Text(lx + 54, ROW(13), sMon.abilityName != NULL ? sMon.abilityName : sText_Dash);
    if (sAbilityDesc != NULL)
        UiW_MessagePage(sAbilityDesc, -1, lx, ROW(14), CR - lx - 6, 1);
}

static bool InMoveMode(void)
{
    return sS.phase == NATIVE_SUMMARY_MOVE_SELECT || sS.phase == NATIVE_SUMMARY_MOVE_SWITCH
        || sS.phase == NATIVE_SUMMARY_REPLACE || sS.phase == NATIVE_SUMMARY_CANT_FORGET;
}

static int SelectedRow(void)
{
    return sS.phase == NATIVE_SUMMARY_MOVE_SWITCH ? sS.moveCursor2 : sS.moveCursor;
}

static const char *const sContestLabels[] = {"COOL", "BEAUTY", "CUTE", "SMART", "TOUGH"};
static const UiRGBA sContestColours[] = {UI_HEX(0xE84838), UI_HEX(0x3878D8), UI_HEX(0xE890B8), UI_HEX(0x48A848),
                                         UI_HEX(0xE8B020)};

static void Hearts(int x, int y, int count, UiRGBA colour)
{
    static const char *const sHeart[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};

    for (int i = 0; i < 8; i++)
        UiG_Mask(UiM_Rows(sHeart, 6, NULL), x + i * 9, y, i < count ? colour : UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3));
}

static void MoveRow(const struct UiRamp *pr, int k, int y, int rh, bool contest)
{
    const struct MoveView *m = &sMoves[k];
    int lx = CX + 6;
    bool in = InMoveMode(), on = in && k == SelectedRow();
    bool carried = sS.phase == NATIVE_SUMMARY_MOVE_SWITCH && k == sS.moveCursor;
    int ty = y + (rh - 13) / 2, tyText = y + (rh - 14) / 2;

    if (on || carried)
    {
        const struct UiMask *hm = UiM_Rounded(CR - CX - 4, rh - 2, UiCornerR(2), UiCornerChamfer(5), UiCornerR(2),
                                              UiCornerR(2));

        if (carried)
            UiW_CarryFrame(hm, CX + 2, y);
        UiG_Paint(hm, CX + 2, y, UiMix(pr->base, UI_HEX(0xFFFFFF), 0.62),
                  &(struct UiPaint){pr->ink, UiMix(pr->base, UI_HEX(0xFFFFFF), 0.85), UiMix(pr->base, UI_HEX(0xFFFFFF), 0.35),
                                    0, 0, NULL, 0});
    }
    if (k == MAX_MON_MOVES && m->move == MOVE_NONE)
    {
        /* the summary's fifth row: back out of the move list */
        UiG_Text(lx + 48, tyText, sText_Cancel);
    }
    else if (m->move != MOVE_NONE)
    {
        int nx;

        if (contest)
            UiMon_Tag(lx + 2, ty + 1, sContestLabels[m->contestCategory < 5 ? m->contestCategory : 0],
                      sContestColours[m->contestCategory < 5 ? m->contestCategory : 0]);
        else
            UiW_TypeBadge(lx + 2, ty, m->type >= 0 ? m->type : UI_TYPE_NORMAL, 0);
        nx = UiG_Text(lx + 48, tyText, m->name);
        if (k == MAX_MON_MOVES)
            UiMon_Tag(nx + 4, ty + 1, "NEW", UI_P_SEL);
        else if (m->hm)
            UiMon_Tag(nx + 4, ty + 1, "HM", UI_P_TYPES_STEEL);
        if (contest)
        {
            Hearts(CR - 80, ty + 3, m->appeal / 10, UI_HEX(0xE84838));
        }
        else
        {
            u8 pp[16];
            bool low = m->maxPP > 0 && m->pp * 4 <= m->maxPP;

            UiMicro_Draw(CR - 62, ty + 4, "PP", UI_P_TEXT_MUTED);
            pp[0] = EOS;
            UiText_AppendNumber(pp, sizeof(pp), m->pp, 1, false);
            UiText_AppendAscii(pp, sizeof(pp), "/");
            UiText_AppendNumber(pp, sizeof(pp), m->maxPP, 1, false);
            UiText_Draw(UI_FONT_NORMAL, CR - 8, tyText, pp, low ? UI_P_TEXT_RED : UI_P_TEXT,
                        low ? UI_P_TEXT_RED_SH : UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        }
        UiG_Hit(HIT_MOVE, k, CX + 2, y, CR - CX - 4, rh);
    }
    else
    {
        TextC(lx + 48, tyText, sText_Dash, UI_P_TEXT_MUTED, UI_P_TEXT_SH);
    }
    if (k == MAX_MON_MOVES && m->move == MOVE_NONE)
        UiG_Hit(HIT_MOVE, k, CX + 2, y, CR - CX - 4, rh);
}

static void StrongLine(int x, int y, const struct MoveView *m)
{
    int bx = x + 58;

    if (m->category == 2)
    {
        Small(x, y + 1, sText_StatusMove, UI_P_TEXT_MUTED);
        return;
    }
    UiCaps_Draw(x, y + 3, "STRONG VS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    if (m->strongVs == 0)
        Small(bx, y + 1, sText_NoWeak, UI_P_TEXT_MUTED);
    for (int t = 0; t < UI_TYPE_COUNT; t++)
        if (m->strongVs & (1u << t))
            bx = UiW_TypeBadge(bx, y, t, 0) + 2;
    if (m->type >= 0 && (m->type == sMon.types[0] || m->type == sMon.types[1]))
    {
        int sx = Small(x, y + 18, sText_Stab1, UI_P_TEXT);

        sx = Small(sx, y + 18, sSpeciesName, UI_P_TEXT);
        Small(sx, y + 18, sText_Stab2, UI_P_TEXT);
    }
}

static void MovesPage(const struct UiRamp *pr, int t, bool contest)
{
    int lx = CX + 6, rows = InMoveMode() ? 5 : 4, rh = InMoveMode() ? 24 : 30, y0 = ROW0 - 2;
    int cy = y0 + 4 * 30 + 6, ch = PY + PH - 6 - cy, cw = CR - CX - 4, cx = CX + 16;
    int sel = SelectedRow();

    for (int k = 0; k < rows; k++)
    {
        int y = y0 + k * rh;
        bool on = InMoveMode() && k == sel;

        MoveRow(pr, k, y, rh, contest);
        if (on)
            UiW_Cursor(CX - 6, y + (rh - 11) / 2, UiW_Nudge(t), UI_NONE);
        if (!InMoveMode() && k < 3)
            for (int xx = lx; xx < CR - 6; xx += 3)
                UiG_Put(xx, y + rh - 1, UiMix(UI_P_CREAM_LO, pr->lo, 0.25));
    }
    /* the detail card */
    UiG_Paint(UiM_Rounded4(cw, ch, 3, 3, 3, 3), CX + 2, cy, UI_P_PAPER,
              &(struct UiPaint){UiMix(pr->ink, UI_P_INK, 0.4), UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    UiG_Rect(CX + 3, cy + 1, 4, ch - 2, pr->base);
    if (sS.phase == NATIVE_SUMMARY_CANT_FORGET)
    {
        UiW_MessagePage(gText_HMMovesCantBeForgotten2, -1, cx, cy + 6, cw - 24, 3);
        return;
    }
    if (!InMoveMode() || sMoves[sel].move == MOVE_NONE)
    {
        u8 buf[48];
        int x;

        if (sS.phase == NATIVE_SUMMARY_REPLACE)
        {
            x = Small(cx, cy + 6, sText_ForgetHint, UI_P_TEXT);
            Small(x, cy + 6, sMoves[MAX_MON_MOVES].name != NULL ? sMoves[MAX_MON_MOVES].name : sText_Dash, UI_P_TEXT);
            return;
        }
        if (contest)
        {
            TextC(cx, cy + 6, sText_ContestCard, UI_P_TEXT_MUTED, UI_P_TEXT_SH);
            return;
        }
        Number(buf, sizeof(buf), sMon.moveCount, false);
        UiText_Append(buf, sizeof(buf), sMon.moveCount == 1 ? sText_MovesKnown1 : sText_MovesKnownN);
        UiG_Text(cx, cy + 6, buf);
        buf[0] = EOS;
        UiText_AppendAscii(buf, sizeof(buf), "PP ");
        UiText_AppendNumber(buf, sizeof(buf), sPPLeft, 1, false);
        UiText_AppendAscii(buf, sizeof(buf), "/");
        UiText_AppendNumber(buf, sizeof(buf), sPPTotal, 1, false);
        UiText_Draw(UI_FONT_NORMAL, CR - 12, cy + 6, buf, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_RIGHT, 0);
        UiG_HLine(cx, cy + 24, cw - 24, UI_P_PAPER_LO);
        {
            const u8 *names[3] = {sText_Physical, sText_Special, sText_Status};

            for (int i = 0; i < 3; i++)
            {
                int xx = cx + i * 112;

                UiW_CategoryIcon(xx, cy + 30, i);
                buf[0] = EOS;
                UiText_Append(buf, sizeof(buf), names[i]);
                UiText_AppendAscii(buf, sizeof(buf), " ");
                UiText_AppendNumber(buf, sizeof(buf), sCats[i], 1, false);
                UiG_Text(xx + 28, cy + 28, buf);
            }
        }
        UiG_HLine(cx, cy + 47, cw - 24, UI_P_PAPER_LO);
        UiCaps_Draw(cx, cy + 56, "STRONG VS", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        {
            int bx = cx + 58, by = cy + 53;

            if (sCoverage == 0)
                Small(bx, by + 1, sText_NoDamaging, UI_P_TEXT_MUTED);
            for (int tt = 0; tt < UI_TYPE_COUNT; tt++)
            {
                if (!(sCoverage & (1u << tt)))
                    continue;
                if (bx + 42 > CR - 8)
                {
                    bx = cx + 58;
                    by += 16;
                }
                bx = UiW_TypeBadge(bx, by, tt, 0) + 2;
            }
        }
        Small(cx, cy + ch - 17, sS.lockMoves ? sText_ChooseMoveLocked : sText_ChooseMove, UI_P_TEXT_MUTED);
        return;
    }
    {
        const struct MoveView *m = &sMoves[sel];
        static const u8 *const sCatNames[3] = {sText_Physical, sText_Special, sText_Status};
        u8 buf[32];
        int gy = cy + 26;

        if (contest)
        {
            UiMon_Tag(cx, cy + 8, sContestLabels[m->contestCategory < 5 ? m->contestCategory : 0],
                      sContestColours[m->contestCategory < 5 ? m->contestCategory : 0]);
            UiG_Text(cx + 60, cy + 5, m->name);
            UiG_HLine(cx, gy - 2, cw - 24, UI_P_PAPER_LO);
            UiCaps_Draw(cx, gy + 5, "APPEAL", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            Hearts(cx + 60, gy + 4, m->appeal / 10, UI_HEX(0xE84838));
            UiCaps_Draw(cx + 150, gy + 5, "JAM", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
            Hearts(cx + 180, gy + 4, m->jam / 10, UI_P_INK);
            UiG_HLine(cx, gy + 17, cw - 24, UI_P_PAPER_LO);
            if (m->contestDesc != NULL)
                UiW_MessagePage(m->contestDesc, -1, cx, gy + 20, cw - 24, 3);
            return;
        }
        UiW_CategoryIcon(cx, cy + 7, m->category);
        UiW_TypeBadge(cx + 28, cy + 7, m->type >= 0 ? m->type : UI_TYPE_NORMAL, 0);
        UiG_Text(cx + 74, cy + 5, sCatNames[m->category]);
        buf[0] = EOS;
        UiText_AppendAscii(buf, sizeof(buf), "PP ");
        UiText_AppendNumber(buf, sizeof(buf), m->pp, 1, false);
        UiText_AppendAscii(buf, sizeof(buf), "/");
        UiText_AppendNumber(buf, sizeof(buf), m->maxPP, 1, false);
        TextR(CR - 12, cy + 5, buf);
        UiG_HLine(cx, gy - 2, cw - 24, UI_P_PAPER_LO);
        {
            const char *labels[3] = {"POWER", "ACCURACY", "PRIORITY"};
            char values[3][8];

            snprintf(values[0], sizeof(values[0]), "%s", m->power > 1 ? "" : "---");
            if (m->power > 1)
                snprintf(values[0], sizeof(values[0]), "%u", (unsigned)m->power);
            if (m->accuracy > 0)
                snprintf(values[1], sizeof(values[1]), "%u", (unsigned)m->accuracy);
            else
                snprintf(values[1], sizeof(values[1]), "---");
            snprintf(values[2], sizeof(values[2]), "%s%d", m->priority > 0 ? "+" : "", m->priority);
            for (int i = 0; i < 3; i++)
            {
                int xx = cx + i * 116;

                UiCaps_Draw(xx, gy + 5, labels[i], UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
                UiText_FromAscii(buf, sizeof(buf), values[i]);
                TextR(xx + 100, gy, buf);
            }
        }
        UiG_HLine(cx, gy + 17, cw - 24, UI_P_PAPER_LO);
        if (m->desc != NULL)
        {
            /* the game's description keeps its own line breaks */
            const u8 *p = m->desc;

            for (int line = 0, yy = gy + 20; line < 2 && *p != EOS; line++, yy += 16)
            {
                u8 ln[64];
                int n = 0;

                while (p[n] != EOS && p[n] != CHAR_NEWLINE && n < (int)sizeof(ln) - 1)
                    n++;
                UiText_CopyN(ln, sizeof(ln), p, n);
                UiG_Text(cx, yy, ln);
                p += n;
                if (*p == CHAR_NEWLINE)
                    p++;
            }
        }
        UiG_HLine(cx, gy + 54, cw - 24, UI_P_PAPER_LO);
        StrongLine(cx, gy + 58, m);
        if (m->hm && sel < MAX_MON_MOVES && !(sS.hmCanGo & (1 << sel)))
            Small(cx, cy + ch - 17, sText_HmNote, UI_P_TEXT_MUTED);
    }
}

static void Tabs(const struct UiRamp *sl, const struct UiRamp *pr)
{
    for (int k = 0; k < PSS_PAGE_COUNT; k++)
    {
        struct UiRamp r;
        int tx = TAB_X0 + k * (TAB_W + 4);
        bool available = k >= sS.minPage && k <= sS.maxPage && !sS.isEgg;

        if (k == sS.page)
            continue;
        r = UiRampOf(sPages[k].colour, 0.4, 0.25, 0.62);
        if (!available)
            r = UiRampOf(UI_HEX(0xA0A0A8), 0.4, 0.25, 0.62);
        UiG_Paint(UiM_Rounded4(TAB_W, 26, 4, 4, 0, 0), tx, 14, UiMix(r.base, sl->ink, 0.22),
                  &(struct UiPaint){r.ink, UiMix(r.hi, sl->ink, 0.22), UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(tx + (TAB_W >> 1), 20, sPages[k].tab, UiMix(UI_P_TEXT_W, r.base, 0.25), r.ink, UI_CAPS_OUTLINE,
                    UI_ALIGN_CENTER, UI_NONE);
        if (available)
            UiG_Hit(HIT_TAB, k, tx, 14, TAB_W, 22);
    }
    (void)pr;
}

static void Paper(const struct UiRamp *sl, const struct UiRamp *pr)
{
    UiRGBA rule = UiMix(UI_P_TYPES_WATER, UI_P_CREAM, 0.72);
    int atx = TAB_X0 + sS.page * (TAB_W + 4);
    const struct UiMask *head;

    UiG_Rect(PX + 4, PY + PH, PW - 6, 2, UI_P_CREAM_LO);
    UiG_Rect(PX + PW, PY + 4, 2, PH - 2, UI_P_CREAM_LO);
    UiG_HLine(PX + 6, PY + PH + 2, PW - 10, UiMix(UI_P_CREAM_LO, sl->lo, 0.5));
    UiG_Paint(UiM_Rounded4(PW, PH, 1, 1, 1, 1), PX, PY, UI_P_CREAM,
              &(struct UiPaint){UiMix(UI_P_CREAM_LO, sl->ink, 0.45), UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    for (int ry = ROW0 + 15; ry < PY + PH - 4; ry += 16)
        UiG_HLine(PX + 2, ry, PW - 4, rule);
    UiG_VLine(MX, PY + 1, PH - 2, UiMix(UI_P_TEXT_RED, UI_P_CREAM, 0.5));
    UiG_VLine(MX + 2, PY + 1, PH - 2, UiMix(UI_P_TEXT_RED, UI_P_CREAM, 0.72));
    /* the active tab joined to the page header band */
    head = UiM_Or(UiM_Place(UiM_Rect(CR - CX + 6, 72), UiM_Rounded4(TAB_W, 30, 4, 4, 0, 0), atx - (CX - 4), 0),
                  UiM_Place(UiM_Rect(CR - CX + 6, 72), UiM_Rounded4(CR - CX + 6, 20, 2, 2, 2, 2), 0, 28));
    head = UiM_Window(head, 0, 0, CR - CX + 6, 48);
    UiG_Paint(head, CX - 4, 12, pr->base, &(struct UiPaint){pr->ink, pr->hi, pr->lo, 0, 0, NULL, 0});
    UiCaps_Draw(atx + (TAB_W >> 1), 18, sPages[sS.page].tab, UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER,
                UI_NONE);
    UiCaps_Draw(CX + 6, 46, sPages[sS.page].title, UI_P_TEXT_W, pr->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    UiG_Hit(HIT_TAB, sS.page, atx, 12, TAB_W, 26);
    UiW_PageDots(CR - 44, 46, sS.maxPage - sS.minPage + 1, sS.page - sS.minPage, pr->hi, UiMix(pr->lo, pr->ink, 0.3));
}

static void Hints(void)
{
    struct UiHintItem h[5];
    int n = 0;
    bool movePage = sS.page == PSS_PAGE_BATTLE_MOVES || sS.page == PSS_PAGE_CONTEST_MOVES;

    switch (sS.phase)
    {
    case NATIVE_SUMMARY_MOVE_SELECT:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        if (!sS.lockMoves)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_MoveIt};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Done};
        break;
    case NATIVE_SUMMARY_MOVE_SWITCH:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_PutHere};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
        break;
    case NATIVE_SUMMARY_REPLACE:
    case NATIVE_SUMMARY_CANT_FORGET:
        h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sText_Choose};
        h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Page};
        h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Forget};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_DontLearn};
        break;
    default:
        if (!sS.isEgg && sS.maxPage > sS.minPage)
            h[n++] = (struct UiHintItem){UI_ACT_MOVE, sText_Page};
        if (!sS.lockMons && sS.listCount > 1)
            h[n++] = (struct UiHintItem){UI_ACT_SCROLL, sS.isBoxMon ? sText_BoxPokemon : sText_Pokemon};
        if (sS.page == PSS_PAGE_INFO && sS.canRename)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Rename};
        else if (sS.page == PSS_PAGE_SKILLS && sS.ivEvShown && !sMon.isEgg)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, IvEvOn() ? sText_HideIvEv : sText_ShowIvEv};
        else if (movePage && sMon.moveCount > 0)
            h[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_CheckMoves};
        if (movePage && sS.canRelearn)
            h[n++] = (struct UiHintItem){UI_ACT_START, sText_Relearn};
        h[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Back};
        break;
    }
    UiW_Hints(BX + 4, 344, h, n, false, 12);
}

static void Draw(int t)
{
    struct UiRamp pr = UiRampOf(sPages[sS.page < PSS_PAGE_COUNT ? sS.page : 0].colour, 0.4, 0.25, 0.62);
    struct UiRamp sl = Slate();
    struct UiPattern grain = UiPat_Diag(6, 1, 0, -1);

    if (sS.listCount == 0)
    {
        UiG_Veil(UI_RGBA(0, 0, 0, 255));
        return;
    }
    Background(&pr);
    Strip(t, &sl);
    UiG_Paint(UiM_Rounded4(BW, BH, 6, 6, 6, 6), BX, BY, sl.base,
              &(struct UiPaint){sl.ink, sl.hi, sl.lo, 0, 2, &grain, UiMix(sl.base, sl.lo, 0.35)});
    Tabs(&sl, &pr);
    Paper(&sl, &pr);
    ClipAndPhoto(&sl);
    NameBlock(t);
    switch (sS.page)
    {
    case PSS_PAGE_INFO: InfoPage(&pr); break;
    case PSS_PAGE_SKILLS: SkillsPage(&pr); break;
    case PSS_PAGE_BATTLE_MOVES: MovesPage(&pr, t, false); break;
    default: MovesPage(&pr, t, true); break;
    }
    Hints();
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

static void SetGoal(int kind, int value, bool thenA)
{
    sGoal = kind;
    sGoalValue = value;
    sGoalThenA = thenA;
    sGoalSteps = 0;
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
    if (NativeUi_PressesPending() || sGoal != GOAL_NONE)
        return true;
    switch (hit->id)
    {
    case HIT_TAB:
        if (sS.phase == NATIVE_SUMMARY_BROWSE || sS.phase == NATIVE_SUMMARY_REPLACE)
        {
            int d = hit->index - sS.page;

            NativeUi_QueuePresses(d > 0 ? DPAD_RIGHT : DPAD_LEFT, abs(d));
        }
        return true;
    case HIT_MON:
        if (sS.phase == NATIVE_SUMMARY_BROWSE)
            SetGoal(GOAL_MON, hit->index, false);
        return true;
    case HIT_UP:
    case HIT_DOWN:
        if (sS.phase == NATIVE_SUMMARY_BROWSE)
            NativeUi_QueuePress(hit->id == HIT_UP ? DPAD_UP : DPAD_DOWN);
        return true;
    case HIT_MOVE:
        if (sS.phase == NATIVE_SUMMARY_BROWSE)
        {
            /* into the move list, then to that move */
            NativeUi_QueuePress(A_BUTTON);
            SetGoal(GOAL_MOVE, hit->index, false);
        }
        else if (sS.phase == NATIVE_SUMMARY_MOVE_SELECT || sS.phase == NATIVE_SUMMARY_REPLACE
              || sS.phase == NATIVE_SUMMARY_CANT_FORGET)
        {
            SetGoal(GOAL_MOVE, hit->index, true);
        }
        else if (sS.phase == NATIVE_SUMMARY_MOVE_SWITCH)
        {
            SetGoal(GOAL_MOVE2, hit->index, true);
        }
        return true;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (NativeUi_PressesPending() || sGoal != GOAL_NONE)
        return true;
    if (sS.phase == NATIVE_SUMMARY_BROWSE)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    else if (InMoveMode())
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    return true;
}

static bool Ready(void)
{
    return sS.ready;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_Summary = {
    .name = "summary",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
