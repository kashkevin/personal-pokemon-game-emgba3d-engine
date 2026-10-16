#ifdef ENABLE_NATIVE_UI
/*
 * 3 · Battle HUD (the approved design's preview/js/screens/battle.js): the
 * databoxes, the party pips, the message plate, the commands, the moves with
 * their PP, type and description, the Mega toggle, the targets of a double
 * battle and the YES / NO boxes, drawn over the battle's own picture. The
 * battle engine keeps every rule: its controllers read the buttons, its cursors
 * are the ones shown, the mouse presses its buttons. What a healthbox shows
 * (HP while a bar moves, level, status) is what the game draws there, noted as
 * it draws it (battle_interface.c). The GBA picture keeps the battlers, the
 * scenery and the move animations; its message box, menus (BG0) and the
 * interface sprites (healthboxes, bars, trays, triggers) are left out.
 *
 * The Safari Zone: its own commands (BALL, POKéBLOCK, GO NEAR, RUN) on the same
 * grid and the Safari Balls left in the player's plate. Wally's catching
 * tutorial: his choices as his controller makes them (the cursor on FIGHT, then
 * BAG; nothing to press but the messages). Kept as the game's picture: the
 * Battle Arena (its menus differ), ability pop-ups. The level-up box and a trainer battle's rewards are screen
 * 26's (ui_results.c).
 * Project-owned.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_ffa.h"
#include "battle_gimmick.h"
#include "battle_interface.h"
#include "battle_main.h"
#include "battle_util.h"
#include "battle_message.h"
#include "bg.h"
#include "graphics.h"
#include "item.h"
#include "main.h"
#include "move.h"
#include "party_menu.h"
#include "pokedex.h"
#include "pokemon.h"
#include "safari_zone.h"
#include "string_util.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/party_menu.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_results.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

enum
{
    HIT_CMD = UI_HIT_SCREEN,
    HIT_MOVE,
    HIT_GIMMICK,
    HIT_TARGET,
    HIT_MESSAGE,
    HIT_BALL,
};

enum Page
{
    PAGE_MESSAGE,   /* BG0 at rest: the message box (and a YES / NO box) */
    PAGE_ACTION,    /* BG0 one screen down: the action prompt and menu */
    PAGE_MOVES,     /* two screens down: the moves */
};

/* the engine's action cursor: 0 FIGHT (top left), 1 BAG, 2 POKéMON, 3 RUN */
enum
{
    CMD_FIGHT,
    CMD_BAG,
    CMD_POKEMON,
    CMD_RUN,
};

/* the move window's effectiveness mark (B_SHOW_EFFECTIVENESS) */
enum
{
    EFF_NONE,
    EFF_NORMAL,
    EFF_SUPER,
    EFF_EXTREME,
    EFF_NOT_VERY,
    EFF_MOSTLY_NOT,
    EFF_IMMUNE,
};

struct Box
{
    bool on;           /* the healthbox is shown */
    u8 battler;
    u8 position;       /* B_POSITION_* */
    bool foe;
    bool mine;         /* the player's own (not a partner's: Steven's, another link player's) */
    struct UiMon mon;  /* who it shows (an Illusion shows its disguise) */
    u8 level;
    s32 hp;
    s32 maxHp;
    double hpFrac;
    bool exp;          /* the player's singles box has an EXP bar */
    double expFrac;
    u8 ailment;        /* AILMENT_* of the status it shows */
    bool toxic;
    bool mega;
    bool caught;       /* a wild Pokémon whose species was caught (the game's ball) */
    int slide;         /* the healthbox's slide (x2), GBA pixels */
    int bob;           /* its bounce (y2) */
    char owner[24];    /* its trainer's name in capitals, as the messages name them ("" wild) */
};

static struct
{
    bool safari;                   /* the Safari Zone: its own commands and the Safari Balls left */
    bool wallyChooses;             /* Wally's catching tutorial: his controller chooses (nothing to press) */
    u8 safariBalls;
    bool doubles;
    bool partnerIsMine;            /* both player-side battlers are the player's (not a multi battle) */
    u8 atPosition[MAX_BATTLERS_COUNT]; /* the battler at each B_POSITION_* (MAX_BATTLERS_COUNT: none) */
    bool trainer;
    bool link;
    bool wild;
    u8 page;
    struct Box box[MAX_BATTLERS_COUNT];
    /* a free-for-all (1v1v1v1): who each battler's trainer is, and whether that trainer is out of Pokémon (their box is gone) */
    bool ffa;
    char ffaName[MAX_BATTLERS_COUNT][24];
    bool ffaOut[MAX_BATTLERS_COUNT];
    /* the foe trainer's party tray (singles trainer battles): 0 none, 1 able, 2 fainted, 3 status */
    u8 foePips[PARTY_SIZE];
    bool foePipsOn;
    /* the battler whose choice the player controller waits for */
    s8 chooser;
    u8 input;          /* enum NativeBattleInput */
    u8 actionCursor;
    u8 moveCursor;
    u8 moveCount;
    u16 moves[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES];
    u8 maxPp[MAX_MON_MOVES];
    s8 moveTypes[MAX_MON_MOVES];   /* enum UiTypeId */
    u8 targetCursor;               /* gMultiUsePlayerCursor */
    u8 switchCursor;               /* SELECT reordering: the second move */
    u8 effect;
    bool descOpen;                 /* the game's L description is open */
    u8 gimmick;                    /* the usable gimmick, GIMMICK_NONE */
    bool gimmickOn;
    u16 leadSpecies;
    u32 leadPersonality;
    /* texts as the game's windows show them */
    u8 msg[256];
    u32 msgPrinted;
    bool hasMsg;
    bool msgPrinting;
    bool msgWaiting;
    u8 prompt[128];
    bool hasPrompt;
    bool yesNo;
    u8 yesNoCursor;
    bool shownAsTarget[MAX_BATTLERS_COUNT]; /* a spread move's targets (the game blinks them) */
    bool userTargetable;           /* the chosen move may target its user (TARGET_USER_OR_ALLY) */
    bool ballOn;                   /* the last used Ball (R throws it) */
    u16 ball;
    bool ready;
} sB;

/* a mouse goal on a double battle's targets: the engine's own presses until its cursor is there */
static s8 sTargetGoal = -1;
static int sTargetSteps;
/* the design's command grid (FIGHT over BAG, POKéMON, RUN) over the engine's 2x2 */
static s8 sCmdGoal = -1;
static bool sCmdGoalThenA;

/* ---------------------------------------------------------------- detection and capture */

static bool Supported(void)
{
    u32 player = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    if (gBattleTypeFlags & (BATTLE_TYPE_ARENA | BATTLE_TYPE_POKEDUDE))
        return false;
    /* the catching tutorial: Wally's (his controller on the player's side) */
    if ((gBattleTypeFlags & BATTLE_TYPE_CATCH_TUTORIAL)
     && (player >= MAX_BATTLERS_COUNT || gBattlerBattleController[player] != BATTLE_CONTROLLER_WALLY))
        return false;
    return true;
}

/* The end of a battle: HandleEndTurn_FinishBattle frees the battle's data and starts the fade to black in the same step, and the
 * engine stays in BattleMainCB2 until the fade is done. The picture still holds the battle's last frame (its message window,
 * its healthboxes), so while this screen's detection depended on the battle's data it let go at the first frame of the fade and
 * the original GBA windows showed over the 3D scene until the field came back. The screen now stays up through that fade, drawing
 * nothing and hiding the GBA windows (sTeardown). */
static bool sSeenLive;
static bool sTeardown;

static bool Detect(void)
{
    if (gMain.callback2 == BattleMainCB2 && gBattleStruct != NULL && gBattleSpritesDataPtr != NULL
     && gBattleResources != NULL && Supported())
    {
        sSeenLive = true;
        sTeardown = false;
        return true;
    }
    sTeardown = sSeenLive && gMain.callback2 == BattleMainCB2;
    if (!sTeardown)
        sSeenLive = false;
    return sTeardown;
}

/* a battler's trainer's name in capitals, as the messages name them ("" wild) */
static void OwnerName(u32 battler, char *dst, size_t size)
{
    u8 text[32];
    const u8 *owner = Battle_NativeUiTrainerName(battler, text);

    dst[0] = '\0';
    if (owner == NULL)
        return;
    UiText_ToAscii(dst, size, owner);
    for (char *c = dst; *c != '\0'; c++)
        *c = toupper((unsigned char)*c);
}

static void CaptureBox(u32 battler)
{
    struct Box *b = &sB.box[battler];
    struct NativeBattleBox nb;
    struct Pokemon *mon, *shown;
    u32 status;

    memset(b, 0, sizeof(*b));
    if (!Battle_NativeUiBox(battler, &nb) || !nb.exists || !nb.visible)
        return;
    mon = GetBattlerMon(battler);
    shown = GetIllusionMonPtr(battler);
    if (shown == NULL)
        shown = mon;
    b->on = true;
    b->battler = battler;
    OwnerName(battler, b->owner, sizeof(b->owner));
    b->position = GetBattlerPosition(battler);
    b->foe = !IsOnPlayerSide(battler);
    b->mine = !b->foe && GetBattlerTrainer(battler) == B_TRAINER_PLAYER;
    UiMon_FromPokemon(&b->mon, shown);
    b->level = nb.level != 0 ? nb.level : b->mon.level;
    b->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
    b->hp = nb.hpShown ? nb.hp : (s32)GetMonData(mon, MON_DATA_HP);
    b->hpFrac = nb.hpShown && nb.hpDen > 0 ? (double)nb.hpNum / nb.hpDen : (b->maxHp > 0 ? (double)b->hp / b->maxHp : 0);
    if (b->hpFrac < 0)
        b->hpFrac = 0;
    if (b->hpFrac > 1)
        b->hpFrac = 1;
    b->exp = !b->foe;
    if (nb.expShown && nb.expDen > 0 && !sB.doubles)
        b->expFrac = (double)nb.expNum / nb.expDen;
    else
        b->expFrac = b->mon.expFrac;
    status = nb.statusShown ? nb.status : GetMonData(mon, MON_DATA_STATUS);
    b->ailment = GetAilmentFromStatus(status);
    b->toxic = (status & STATUS1_TOXIC_POISON) != 0;
    b->mega = GetActiveGimmick(battler) == GIMMICK_MEGA;
    b->caught = b->foe && sB.wild && b->ailment == AILMENT_NONE
             && GetSetPokedexFlag(SpeciesToNationalPokedexNum(GetMonData(mon, MON_DATA_SPECIES)), FLAG_GET_CAUGHT);
    b->slide = nb.x2;
    b->bob = nb.y2;
}

static void CaptureFoePips(void)
{
    struct Pokemon *party;
    u32 foe = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);

    sB.foePipsOn = false;
    if (!sB.trainer || sB.doubles || foe >= gBattlersCount)
        return;
    party = GetBattlerParty(foe);
    if (party == NULL)
        return;
    sB.foePipsOn = true;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        u16 species = GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG);

        if (species == SPECIES_NONE || species == SPECIES_EGG)
            sB.foePips[i] = 0;
        else if (GetMonData(&party[i], MON_DATA_HP) == 0)
            sB.foePips[i] = 2;
        else if (GetMonData(&party[i], MON_DATA_STATUS) != 0)
            sB.foePips[i] = 3;
        else
            sB.foePips[i] = 1;
    }
}

/* the type the move window prints for the chosen move ("TYPE/Fire"): dynamic types included */
static int PrintedMoveType(int fallback)
{
    u8 text[64];
    u32 printed;
    bool8 active;
    const u8 *p;

    if (!TextPrinter_NativeUiWindowText(B_WIN_MOVE_TYPE, text, sizeof(text), &printed, &active))
        return fallback;
    /* past "TYPE/" (gText_MoveInterfaceType) and any font change the game put in front of the name */
    for (p = text; *p != EOS && *p != CHAR_SLASH; p++)
        ;
    if (*p == EOS)
        return fallback;
    p++;
    while (*p == EXT_CTRL_CODE_BEGIN)
        p += 1 + GetExtCtrlCodeLength(p[1]);
    for (int t = 0; t < NUMBER_OF_MON_TYPES; t++)
    {
        const u8 *name = gTypesInfo[t].name;
        int i = 0;

        while (name[i] != EOS && p[i] == name[i])
            i++;
        if (name[i] == EOS && (p[i] == EOS || p[i] == CHAR_SPACE))
            return UiMon_Type(t) >= 0 ? UiMon_Type(t) : fallback;
    }
    return fallback;
}

/* the effectiveness mark the move window prints after "PP" */
static u8 PrintedEffect(void)
{
    u8 text[32];
    u32 printed;
    bool8 active;

    if (!TextPrinter_NativeUiWindowText(B_WIN_PP, text, sizeof(text), &printed, &active))
        return EFF_NONE;
    for (int i = 0; text[i] != EOS && i < 30; i++)
        if (text[i] == CHAR_EXTRA_SYMBOL)
            switch (text[i + 1])
            {
            case 0x15: return EFF_SUPER;      /* CIRCLE_DOT */
            case 0x16: return EFF_NOT_VERY;   /* TRIANGLE */
            case 0x17: return EFF_IMMUNE;     /* BIG_MULT_X */
            case 0x18: return EFF_NORMAL;     /* CIRCLE_HOLLOW */
            case 0x19: return EFF_EXTREME;    /* STAR */
            case 0x1A: return EFF_MOSTLY_NOT; /* TRIANGLE_UPSIDE_DOWN */
            default: return EFF_NONE;
            }
    return EFF_NONE;
}

static void CaptureChooser(void)
{
    u8 wallyCursor = 0;

    sB.chooser = -1;
    sB.input = NATIVE_BATTLE_INPUT_NONE;
    sB.wallyChooses = false;
    for (u32 i = 0; i < gBattlersCount; i++)
    {
        u32 in;

        if (!IsOnPlayerSide(i))
            continue;
        if (sB.safari)
            in = Battle_NativeUiSafariInput(i);
        else if ((in = Battle_NativeUiWallyInput(i, &wallyCursor)) != NATIVE_BATTLE_INPUT_NONE)
            sB.wallyChooses = true;
        else
            in = Battle_NativeUiPlayerInput(i);
        if (in != NATIVE_BATTLE_INPUT_NONE)
        {
            sB.chooser = i;
            sB.input = in;
            break;
        }
    }
    if (sB.chooser < 0)
        return;
    {
        u32 c = sB.chooser;
        struct ChooseMoveStruct *moveInfo = (struct ChooseMoveStruct *)(&gBattleResources->bufferA[c][4]);
        struct Pokemon *lead = GetBattlerMon(c);

        sB.actionCursor = sB.wallyChooses ? wallyCursor : gActionSelectionCursor[c] & 3;
        sB.moveCursor = gMoveSelectionCursor[c] & 3;
        sB.targetCursor = gMultiUsePlayerCursor;
        sB.switchCursor = gMultiUsePlayerCursor & 3;
        sB.leadSpecies = GetMonData(lead, MON_DATA_SPECIES);
        sB.leadPersonality = GetMonData(lead, MON_DATA_PERSONALITY);
        UiGfx_PrepareItemIcon(ITEM_POKE_BALL);
        if (sB.input >= NATIVE_BATTLE_INPUT_MOVE_WAIT && sB.input <= NATIVE_BATTLE_INPUT_TARGETS_SHOWN)
        {
            sB.moveCount = gNumberOfMovesToChoose;
            for (int m = 0; m < MAX_MON_MOVES; m++)
            {
                int ty = UiMon_Type(GetMoveType(moveInfo->moves[m]));

                sB.moves[m] = moveInfo->moves[m];
                sB.pp[m] = moveInfo->currentPP[m];
                sB.maxPp[m] = moveInfo->maxPP[m];
                sB.moveTypes[m] = ty >= 0 ? ty : UI_TYPE_NORMAL;
            }
            sB.moveTypes[sB.moveCursor] = PrintedMoveType(sB.moveTypes[sB.moveCursor]);
            sB.effect = PrintedEffect();
            sB.descOpen = gBattleStruct->descriptionSubmenu;
            sB.gimmick = gBattleStruct->gimmick.usableGimmick[c];
            if (sB.gimmick != GIMMICK_NONE && HasTrainerUsedGimmick(c, sB.gimmick))
                sB.gimmick = GIMMICK_NONE;
            sB.gimmickOn = gBattleStruct->gimmick.playerSelect;
        }
        for (u32 i = 0; i < MAX_BATTLERS_COUNT; i++)
            sB.shownAsTarget[i] = i < gBattlersCount && sB.input == NATIVE_BATTLE_INPUT_TARGETS_SHOWN && ShouldHideBattler(i);
        sB.userTargetable = sB.input == NATIVE_BATTLE_INPUT_TARGET
                         && GetBattlerMoveSelectionTargetType(c, sB.moves[sB.moveCursor]) == TARGET_USER_OR_ALLY;
        sB.ballOn = !sB.wallyChooses && sB.input == NATIVE_BATTLE_INPUT_ACTION && gLastUsedBallMenuPresent && CanThrowLastUsedBall();
        sB.ball = gBallToDisplay;
        if (sB.ballOn)
            UiGfx_PrepareItemIcon(sB.ball);
    }
}

/* the YES / NO box the battle draws on its message page (HandleBattleWindow) and its cursor */
static void CaptureYesNo(void)
{
    const u16 *map = GetBgTilemapBuffer(0);

    sB.yesNo = false;
    if (map == NULL || sB.page != PAGE_MESSAGE)
        return;
    /* YESNOBOX_X_Y: the frame's top left tile at (24, 8); the cursor at (25, 9 + 2 * n) */
    if ((map[8 * 32 + 24] & 0x3FF) != 0x022)
        return;
    sB.yesNo = true;
    sB.yesNoCursor = (map[11 * 32 + 25] & 0x3FF) == 1 ? 1 : 0;
}

static void TargetGoalStep(void);
static void CmdGoalStep(void);

static void Capture(void)
{
    bool8 active;

    if (sTeardown)
    {
        /* nothing of the battle is left to read; keep the screen up and empty */
        for (u32 i = 0; i < MAX_BATTLERS_COUNT; i++)
            sB.box[i].on = false;
        sB.chooser = -1;
        sB.input = NATIVE_BATTLE_INPUT_NONE;
        sB.hasMsg = sB.hasPrompt = sB.yesNo = sB.msgWaiting = sB.msgPrinting = false;
        sB.ready = false;
        sB.ffa = false;
        return;
    }

    sB.safari = (gBattleTypeFlags & BATTLE_TYPE_SAFARI) != 0;
    sB.safariBalls = gNumSafariBalls;
    if (sB.safari)
    {
        UiGfx_PrepareItemIcon(ITEM_SAFARI_BALL);
        UiGfx_PrepareItemIcon(ITEM_POKEBLOCK_CASE);
    }
    sB.doubles = IsDoubleBattle();
    sB.partnerIsMine = !(gBattleTypeFlags & BATTLE_TYPE_PLAYER_HAS_PARTNER);
    for (u32 p = 0; p < MAX_BATTLERS_COUNT; p++)
    {
        u32 battler = GetBattlerAtPosition(p);

        sB.atPosition[p] = battler < gBattlersCount ? battler : MAX_BATTLERS_COUNT;
    }
    sB.trainer = (gBattleTypeFlags & BATTLE_TYPE_TRAINER) != 0;
    sB.link = (gBattleTypeFlags & BATTLE_TYPE_LINK) != 0;
    sB.wild = !(gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED_LINK));
    sB.page = gBattle_BG0_Y >= DISPLAY_HEIGHT * 2 ? PAGE_MOVES : gBattle_BG0_Y >= DISPLAY_HEIGHT ? PAGE_ACTION : PAGE_MESSAGE;
    for (u32 i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        if (i < gBattlersCount)
            CaptureBox(i);
        else
            sB.box[i].on = false;
    }
    sB.ffa = IsFreeForAllBattle();
    for (u32 i = 0; i < MAX_BATTLERS_COUNT; i++)
    {
        sB.ffaName[i][0] = '\0';
        sB.ffaOut[i] = false;
        if (sB.ffa && i < gBattlersCount)
        {
            OwnerName(i, sB.ffaName[i], sizeof(sB.ffaName[i]));
            sB.ffaOut[i] = FFA_IsEliminated(i);
        }
    }
    CaptureFoePips();
    CaptureChooser();
    CaptureYesNo();
    sB.hasMsg = TextPrinter_NativeUiWindowText(B_WIN_MSG, sB.msg, sizeof(sB.msg), &sB.msgPrinted, &active);
    sB.msgPrinting = sB.hasMsg && active;
    sB.msgWaiting = sB.hasMsg && TextPrinter_NativeUiWindowWaiting(B_WIN_MSG);
    {
        u32 printed;

        sB.hasPrompt = TextPrinter_NativeUiWindowText(B_WIN_ACTION_PROMPT, sB.prompt, sizeof(sB.prompt), &printed, &active);
    }
    UiResults_Capture();
    sB.ready = sB.msgWaiting || sB.yesNo || UiResults_LevelUpWaiting()
            || (sB.chooser >= 0 && !sB.wallyChooses && sB.input != NATIVE_BATTLE_INPUT_ACTION_WAIT
                && sB.input != NATIVE_BATTLE_INPUT_MOVE_WAIT && sB.input != NATIVE_BATTLE_INPUT_PARTY
                && sB.input != NATIVE_BATTLE_INPUT_BAG);
    if (NativeUi_LogEnabled())
    {
        static int sLastPage = -1, sLastInput = -1, sLastYesNo = -1, sLastCursor = -1;
        int cursor = sB.input == NATIVE_BATTLE_INPUT_ACTION ? sB.actionCursor : -1;

        if (sB.page != sLastPage || sB.input != sLastInput || sB.yesNo != sLastYesNo || cursor != sLastCursor)
            NativeUi_Logf("battle: page %d input %d yesno %d chooser %d cursor %d%s", sB.page, sB.input, sB.yesNo,
                          sB.chooser, cursor, sB.wallyChooses ? " (Wally)" : "");
        sLastPage = sB.page;
        sLastInput = sB.input;
        sLastYesNo = sB.yesNo;
        sLastCursor = cursor;
    }
    if (sB.input != NATIVE_BATTLE_INPUT_TARGET)
        sTargetGoal = -1;
    if (sB.input != NATIVE_BATTLE_INPUT_ACTION)
        sCmdGoal = -1;
    TargetGoalStep();
    CmdGoalStep();
}

/* the interface sprites the HUD draws itself: by the OBJ tiles their sheets hold */
static void HideObjects(uint32_t mask[4])
{
    u32 tiles[32];
    const struct OamData *oam = (const struct OamData *)OAM;

    Battle_NativeUiHudTiles(tiles);
    for (int i = 0; i < 128; i++)
    {
        u32 t = oam[i].tileNum;

        if ((tiles[t >> 5] >> (t & 31)) & 1)
            mask[i >> 5] |= 1u << (i & 31);
    }
}

/* ---------------------------------------------------------------- pieces */

static const UiRGBA sPlateHi = UI_HEX(0xFFFFFF), sPlateBase = UI_HEX(0xF4F4E6), sPlateLo = UI_HEX(0xCFCFBE);

/* the HP rail: a dark arrow that leaves the plate toward the action */
static void Rail(int x0, int x1, int y, bool foe, UiRGBA ink)
{
    int L = x1 - x0;
    int foePts[] = {0, 0, L - 6, 0, L, 5, L - 6, 10, 0, 10};
    int mePts[] = {6, 0, L, 0, L, 10, 6, 10, 0, 5};

    UiG_Paint(UiM_Poly(L + 1, 11, foe ? foePts : mePts, 5), x0, y, UI_P_HP_TRACK_LO,
              &(struct UiPaint){ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
}

static int Level(int xRight, int y, int level)
{
    u8 lv[12];

    UiMon_LevelText(lv, sizeof(lv), level);
    return UiG_Text(xRight - UiText_Width(UI_FONT_NORMAL, lv, 0), y, lv);
}

struct BallCtx
{
    u32 kind;
};

static void BuildBall(struct UiBitmap *bm, const void *ctx)
{
    const struct BallCtx *c = ctx;

    UiGfx_Decode4bpp(bm->d, 8, Battle_NativeUiBallTile(c->kind), gBattleInterface_BallDisplayPal, 8, 8);
}

/* one of the game's 8x8 tray balls (0 full, 1 empty, 2 fainted, 3 statused, 4 caught) */
static void Ball(int x, int y, u32 kind)
{
    struct BallCtx c = {kind};
    char key[24];

    snprintf(key, sizeof(key), "btlball:%u", (unsigned)kind);
    UiG_Blit(UiG_Cached(key, 8, 8, BuildBall, &c), x, y);
}

static int Status(int x, int y, const struct Box *b)
{
    struct UiMon m = b->mon;
    UiRGBA c;
    const char *label;

    m.ailment = b->ailment;
    m.toxic = b->toxic;
    label = UiMon_StatusLabel(&m, &c);
    if (label == NULL || b->ailment == AILMENT_NONE)
        return x;
    return UiW_StatusBadge(x, y, label, c, 0) + 3;
}

/* the singles databox (battle.py) */
static void Databox(int x, int y, const struct Box *b)
{
    UiRGBA ink = UI_P_DATABOX_INK;
    bool foe = b->foe;
    int w = foe ? 152 : 168, h = foe ? 34 : 46, nx0 = foe ? x + 8 : x + 12, nx, ry = y + 20, bx;
    const struct UiMask *m = foe ? UiM_Rounded4(w, h, 0, 4, 0, 4) : UiM_Rounded4(w, h, 4, 0, 4, 0);

    UiG_Paint(m, x, y, sPlateBase, &(struct UiPaint){ink, sPlateHi, sPlateLo, 0, 2, NULL, 0});
    UiG_HLine(x + (foe ? 2 : 4), y + 17, w - 6, sPlateLo);
    nx = UiG_Text(nx0, y + 1, b->mon.nickname);
    nx = UiW_Gender(nx + 1, y + 1, b->mon.gender == UI_MON_MALE ? 0 : b->mon.gender == UI_MON_FEMALE ? 1 : -1);
    if (b->mega)
        UiGfx_Icon("gem", nx + 2, y + 4, 0);
    Level(x + w - 8, y + 1, b->level);
    if (foe)
        Rail(x + 4, x + w + 20, ry, true, ink);
    else
        Rail(x - 20, x + w - 4, ry, false, ink);
    bx = Status(nx0 - 2, ry, b);
    if (b->caught && bx == nx0 - 2)
        Ball(nx0 - 2, ry + 2, 4), bx = nx0 + 9;
    UiW_HpBar(bx, ry + 2, x + w - 10 - bx - 13, b->hpFrac, true, ink);
    if (!foe)
    {
        char hp[24];

        snprintf(hp, sizeof(hp), "%d/%d", (int)b->hp, (int)b->maxHp);
        UiCaps_Draw(x + w - 10, y + 34, hp, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        UiW_ExpBar(x + 12, y + 36, 82, b->expFrac, true);
    }
}

/* the player's plate in the Safari Zone: the engine's "Safari Balls" and "Left:" with the count */
static void SafariBox(int x, int y)
{
    UiRGBA ink = UI_P_DATABOX_INK;
    int w = 168, h = 46;
    u8 left[24];

    UiG_Paint(UiM_Rounded4(w, h, 4, 0, 4, 0), x, y, sPlateBase, &(struct UiPaint){ink, sPlateHi, sPlateLo, 0, 2, NULL, 0});
    UiG_HLine(x + 4, y + 17, w - 6, sPlateLo);
    UiG_Text(x + 12, y + 1, gText_SafariBalls);
    UiGfx_ItemIcon(ITEM_SAFARI_BALL, x + w - 34, y + 18, NULL);
    left[0] = EOS;
    UiText_Append(left, sizeof(left), gText_SafariBallLeft);
    UiText_AppendNumber(left, sizeof(left), sB.safariBalls, 1, false);
    UiG_Text(x + 12, y + 22, left);
}

/* the 2v2 databox (battle2v2.py) without the multiplayer owner tags */
static void Databox2(int x, int y, const struct Box *b, bool mine)
{
    UiRGBA ink = UI_P_DATABOX_INK;
    bool foe = b->foe;
    int w = mine ? 176 : 156, h = mine ? 48 : 36, nx0 = foe ? x + 10 : x + 12, nx, ry = y + 20, bx, lvr;
    const struct UiMask *m = foe ? UiM_Rounded4(w, h, 0, 4, 0, 4) : UiM_Rounded4(w, h, 4, 0, 4, 0);

    UiG_Paint(m, x, y, sPlateBase, &(struct UiPaint){ink, sPlateHi, sPlateLo, 0, 2, NULL, 0});
    nx = UiG_Text(nx0, y + 1, b->mon.nickname);
    nx = UiW_Gender(nx + 1, y + 1, b->mon.gender == UI_MON_MALE ? 0 : b->mon.gender == UI_MON_FEMALE ? 1 : -1);
    if (b->mega)
        UiGfx_Icon("gem", nx + 2, y + 4, 0);
    lvr = foe ? x + w - 8 : x + w - 12;
    Level(lvr, y + 1, b->level);
    UiG_HLine(x + 6, y + 17, w - 14, sPlateLo);
    if (foe)
        Rail(x + 6, x + w + 18, ry, true, ink);
    else
        Rail(x - 18, x + w - 10, ry, false, ink);
    bx = Status(nx0 - 2, ry, b);
    if (b->caught && bx == nx0 - 2)
        Ball(nx0 - 2, ry + 2, 4), bx = nx0 + 9;
    UiW_HpBar(bx, ry + 2, x + w - (foe ? 10 : 14) - bx - 13, b->hpFrac, true, ink);
    /* the HP numbers and the Exp. bar on the player's own boxes only, as the
     * design's (a partner's box is the short one: its numbers would fall off it) */
    if (mine)
    {
        char hp[24];

        snprintf(hp, sizeof(hp), "%d/%d", (int)b->hp, (int)b->maxHp);
        UiCaps_Draw(x + w - 14, y + 34, hp, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        UiW_ExpBar(x + 12, y + 36, 80, b->expFrac, true);
    }
}

/* a free-for-all's trainer colours: you, then the three others; the same on the boxes and on the target plates */
static UiRGBA TrainerTint(u32 battler)
{
    switch (battler & 3)
    {
    case 0:
        return UI_HEX(0x3C78C8);
    case 1:
        return UI_HEX(0xC85048);
    case 2:
        return UI_HEX(0x48A060);
    default:
        return UI_HEX(0xB88820);
    }
}

/* the trainer's name tag on a free-for-all box: "BOB" in the trainer's colour, "ALICE (YOU)" on your own, greyed with
 * "OUT" once the trainer has no Pokémon left. Right-aligned boxes hang it from their right edge. */
static void FfaTag(int x, int y, int w, bool right, u32 battler, bool out)
{
    char text[40];
    int tw;
    UiRGBA tint = out ? UI_HEX(0x787878) : TrainerTint(battler);

    if (battler >= MAX_BATTLERS_COUNT || sB.ffaName[battler][0] == '\0')
        return;
    snprintf(text, sizeof(text), "%s%s", sB.ffaName[battler],
             out ? " - OUT" : FFA_IsLocalBattler((enum BattlerId)battler) ? " (YOU)" : "");
    tw = UiCaps_Width(text) + 12;
    if (right)
        x += w - tw;
    UiG_Paint(UiM_Rounded4(tw, 12, 3, 3, 0, 0), x, y - 11, tint, &(struct UiPaint){UI_P_DATABOX_INK, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(x + 6, y - 9, text, UI_P_TEXT_W, UI_HEX(0x101018), UI_CAPS_SHADOW, UI_ALIGN_LEFT, UI_NONE);
}

static const struct Box *BoxAt(u32 position);

/* the free-for-all's four boxes: the other three trainers' Pokémon and yours, each under its trainer's name tag. The
 * foes stand top left (the second's Pokémon is drawn on your column, so its box is the short one above yours). */
static void DrawBoxesFfa(int L, int R, int T, int dyB)
{
    static const u8 sPos[4] = {B_POSITION_OPPONENT_LEFT, B_POSITION_OPPONENT_RIGHT, B_POSITION_PLAYER_RIGHT, B_POSITION_PLAYER_LEFT};

    for (int i = 0; i < 4; i++)
    {
        const struct Box *b = BoxAt(sPos[i]);
        u32 battler = sB.atPosition[sPos[i]];
        bool mine = sPos[i] == B_POSITION_PLAYER_LEFT, right = i >= 2;
        int w = mine ? 176 : 156, x = right ? R - w : L, y = i == 0 ? T + 22 : i == 1 ? T + 74 : i == 2 ? 178 + dyB : 228 + dyB;

        if (b != NULL)
        {
            Databox2(x + b->slide * 2, y + b->bob, b, mine);
            if (b->slide == 0)
                FfaTag(x, y + b->bob, w, right, battler, false);
        }
        else if (battler < MAX_BATTLERS_COUNT && sB.ffaOut[battler])
            FfaTag(x, y, w, right, battler, true);
    }
}

/* the translucent dark message plate (the only flat alpha in the UI) */
static void MessagePlate(int x, int y, int w, int h)
{
    const struct UiMask *m = UiM_Rounded4(w, h, 0, 4, 0, 4);
    const struct UiMask *inner = UiM_Erode4(m);
    const struct UiMask *ring2 = UiM_Sub(inner, UiM_Erode4(inner));

    UiG_Mask(m, x, y, UI_P_MSG_BG);
    UiG_Mask(UiM_Sub(m, inner), x, y, UI_P_MSG_INK);
    UiG_Mask(UiM_Window(ring2, 0, 0, w, h >> 1), x, y, UI_P_MSG_RIM);
}

/* white text on the plate: the page the printer is on, up to what is printed, the last two lines */
static void MessageText(int x, int y, int width, const u8 *text, int printed)
{
    const u8 *starts[8];
    int lengths[8];
    u8 page[256];
    int len = 0, n, total = 0, pageStart = 0;

    while (text[total] != EOS && (printed < 0 || total < printed))
        total++;
    for (int i = 0; i < total; i++)
        if ((text[i] == CHAR_PROMPT_SCROLL || text[i] == CHAR_PROMPT_CLEAR) && i + 1 < total)
            pageStart = i + 1;
    for (int i = pageStart; i < total && text[i] != CHAR_PROMPT_SCROLL && text[i] != CHAR_PROMPT_CLEAR && len < 254; i++)
        page[len++] = text[i];
    page[len] = EOS;
    n = UiText_Wrap(page, width, UI_FONT_NORMAL, starts, lengths, 8);
    for (int i = n > 2 ? n - 2 : 0, row = 0; i < n; i++, row++)
    {
        u8 line[128];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_NORMAL, x, y + row * 18, line, UI_P_TEXT_W, UI_HEX(0x080A10), UI_ALIGN_LEFT, 0);
    }
}

/* FIGHT / BAG / POKéMON / RUN | GIVE UP, with FIGHT's speed stripes */
/* the Safari Zone's commands on the same grid: BALL over POKéBLOCK, GO NEAR, RUN
 * (the engine's 2x2 in the same order), with the game's Safari Ball and
 * Pokéblock case icons in their chips */
static void SafariCommands(int x, int y, int sel)
{
    static const u8 sBall[] = _("BALL"), sPokeblock[] = _("POKéBLOCK"), sGoNear[] = _("GO NEAR"), sRun[] = _("RUN");
    const struct UiMask *fm;
    struct UiRamp fr = UiRampOf(UI_P_CMD_FIGHT, 0.38, 0.28, 0.62);
    struct UiPattern diag = UiPat_Diag(12, 3, 0, -1);
    int bx = x;

    fm = UiW_DsButton(x, y, 252, 38, UI_P_CMD_FIGHT, (const u8 *)"\xFF",
                      &(struct UiDsButtonOpt){.noChip = true, .selected = sel == CMD_FIGHT, .center = true});
    UiG_Mask(UiM_Pattern(UiM_Window(UiM_Erode4(fm), 40, 4, 252, 30), &diag), x, y,
             UiMix(UI_P_CMD_FIGHT, UI_HEX(0xFFFFFF), 0.16));
    UiG_Paint(UiM_Ellipse(28, 28), x + 7, y + 3, UiMix(fr.base, UI_HEX(0xFFFFFF), 0.45),
              &(struct UiPaint){fr.ink, UI_NONE, UiMix(fr.base, UI_HEX(0xFFFFFF), 0.2), 0, 0, NULL, 0});
    UiGfx_ItemIcon(ITEM_SAFARI_BALL, x + 9, y + 5, NULL);
    UiText_Draw(UI_FONT_NORMAL, x + ((40 + 252) >> 1), y + 8, sBall, UI_P_TEXT_W, fr.ink, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_CMD, CMD_FIGHT, x, y, 252, 38);
    {
        const u8 *labels[3] = {sPokeblock, sGoNear, sRun};
        const char *icons[3] = {NULL, "footprints", "shoe"};
        int widths[3] = {100, 82, 62};
        UiRGBA colours[3] = {UI_P_CMD_BAG, UI_P_CMD_POKEMON, UI_P_CMD_RUN};
        int cmds[3] = {CMD_BAG, CMD_POKEMON, CMD_RUN};

        for (int i = 0; i < 3; i++)
        {
            int by = y + 42, bw = widths[i], bh = 32;

            UiW_DsButton(bx, by, bw, bh, colours[i], icons[i] != NULL ? labels[i] : (const u8 *)"\xFF",
                         &(struct UiDsButtonOpt){.icon = icons[i], .noChip = icons[i] == NULL, .selected = sel == cmds[i]});
            if (icons[i] == NULL)
            {
                /* the chip holds the game's Pokéblock case */
                struct UiRamp r = UiRampOf(colours[i], 0.42, 0.25, 0.62);
                int d = 26 < bh - 3 - 4 ? 26 : bh - 3 - 4, cy = by + ((bh - 3) >> 1);

                UiText_Draw(UI_FONT_NORMAL, bx + 5 + d + 6, cy - 9, labels[i], UI_P_TEXT_W, r.ink, UI_ALIGN_LEFT, 0);
                UiG_Paint(UiM_Ellipse(d, d), bx + 5, cy - (d >> 1), UiMix(r.base, UI_HEX(0xFFFFFF), 0.45),
                          &(struct UiPaint){r.ink, UI_NONE, UiMix(r.base, UI_HEX(0xFFFFFF), 0.2), 0, 0, NULL, 0});
                UiGfx_ItemIcon(ITEM_POKEBLOCK_CASE, bx + 5 + (d >> 1) - 12, cy - 12, NULL);
            }
            UiG_Hit(HIT_CMD, cmds[i], bx, by, bw, bh);
            bx += bw + 4;
        }
    }
}

static void Commands(int x, int y, int sel, int t)
{
    static const u8 sFight[] = _("FIGHT"), sBag[] = _("BAG"), sPokemon[] = _("POKéMON"), sRun[] = _("RUN"), sGiveUp[] = _("GIVE UP");
    const struct UiMask *fm;
    struct UiRamp fr = UiRampOf(UI_P_CMD_FIGHT, 0.38, 0.28, 0.62);
    struct UiPattern diag = UiPat_Diag(12, 3, 0, -1);
    int bx = x;

    fm = UiW_DsButton(x, y, 252, 38, UI_P_CMD_FIGHT, (const u8 *)"\xFF",
                      &(struct UiDsButtonOpt){.icon = "burst", .selected = sel == CMD_FIGHT, .center = true});
    UiG_Mask(UiM_Pattern(UiM_Window(UiM_Erode4(fm), 40, 4, 252, 30), &diag), x, y,
             UiMix(UI_P_CMD_FIGHT, UI_HEX(0xFFFFFF), 0.16));
    UiText_Draw(UI_FONT_NORMAL, x + ((40 + 252) >> 1), y + 8, sFight, UI_P_TEXT_W, fr.ink, UI_ALIGN_CENTER, 0);
    UiG_Hit(HIT_CMD, CMD_FIGHT, x, y, 252, 38);
    {
        const u8 *labels[3] = {sBag, sPokemon, sB.link ? sGiveUp : sRun};
        const char *icons[3] = {"bag", NULL, sB.link ? "flag" : "shoe"};
        int widths[3] = {76, 100, sB.link ? 78 : 68};
        UiRGBA colours[3] = {UI_P_CMD_BAG, UI_P_CMD_POKEMON, sB.link ? UI_HEX(0x7080A8) : UI_P_CMD_RUN};
        int cmds[3] = {CMD_BAG, CMD_POKEMON, CMD_RUN};

        for (int i = 0; i < 3; i++)
        {
            int by = y + 42, bw = widths[i], bh = 32;

            UiW_DsButton(bx, by, bw, bh, colours[i], icons[i] != NULL ? labels[i] : (const u8 *)"\xFF",
                         &(struct UiDsButtonOpt){.icon = icons[i], .noChip = icons[i] == NULL, .selected = sel == cmds[i]});
            if (icons[i] == NULL)
            {
                /* the chip holds the lead's party icon */
                struct UiRamp r = UiRampOf(colours[i], 0.42, 0.25, 0.62);
                int d = 26 < bh - 3 - 4 ? 26 : bh - 3 - 4, cy = by + ((bh - 3) >> 1), cx = bx + 5 + (d >> 1);

                UiText_Draw(UI_FONT_NORMAL, bx + 5 + d + 6, cy - 9, labels[i], UI_P_TEXT_W, r.ink, UI_ALIGN_LEFT, 0);

                UiG_Paint(UiM_Ellipse(d, d), bx + 5, cy - (d >> 1), UiMix(r.base, UI_HEX(0xFFFFFF), 0.45),
                          &(struct UiPaint){r.ink, UI_NONE, UiMix(r.base, UI_HEX(0xFFFFFF), 0.2), 0, 0, NULL, 0});
                UiG_Save();
                UiG_Clip(cx - (d >> 1) + 1, cy - (d >> 1) + 1, d - 2, d - 2);
                UiGfx_MonIcon(sB.leadSpecies, sB.leadPersonality, false, (t / 20) % 2, cx - 16, cy - 19, NULL);
                UiG_Restore();
            }
            UiG_Hit(HIT_CMD, cmds[i], bx, by, bw, bh);
            bx += bw + 4;
        }
    }
}

static void MoveButton(int x, int y, int w, int h, int i, bool selected)
{
    const struct UiRamp *tr = &gUiTypeRamps[sB.moveTypes[i]];
    UiRGBA body = UiMix(tr->base, UI_HEX(0xFFFFFF), 0.62);
    const struct UiMask *m = UiM_Rounded4(w, h, 3, 3, 3, 3);
    bool low = sB.maxPp[i] > 0 && sB.pp[i] * 4 <= sB.maxPp[i];
    char pp[12];

    if (selected)
        UiW_SelectFrame(m, x, y, UI_NONE, UI_NONE, 0);
    UiG_Paint(m, x, y, body, &(struct UiPaint){tr->ink, UI_HEX(0xFFFFFF), UiMix(tr->base, UI_HEX(0xFFFFFF), 0.25), 0, 2, NULL, 0});
    UiG_Mask(UiM_Rounded4(8, h - 4, 2, 0, 2, 0), x + 2, y + 2, tr->base);
    UiG_Text(x + 14, y + 1, GetMoveName(sB.moves[i]));
    UiW_TypeBadge(x + 14, y + h - 16, sB.moveTypes[i], 0);
    UiMicro_Draw(x + w - 52, y + h - 12, "PP", tr->ink);
    snprintf(pp, sizeof(pp), "%u/%u", (unsigned)sB.pp[i], (unsigned)sB.maxPp[i]);
    UiCaps_Draw(x + w - 8, y + h - 13, pp, low ? UI_HEX(0xE05030) : UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
}

static const char *EffectLabel(u8 e, UiRGBA *c)
{
    switch (e)
    {
    case EFF_NORMAL: *c = UI_HEX(0x808890); return "EFFECTIVE";
    case EFF_SUPER: *c = UI_HEX(0x38A048); return "SUPER EFFECTIVE";
    case EFF_EXTREME: *c = UI_HEX(0x208838); return "EXTREMELY EFFECTIVE";
    case EFF_NOT_VERY: *c = UI_HEX(0xC08030); return "NOT VERY EFFECTIVE";
    case EFF_MOSTLY_NOT: *c = UI_HEX(0xB86028); return "MOSTLY INEFFECTIVE";
    case EFF_IMMUNE: *c = UI_HEX(0x909098); return "NO EFFECT";
    default: return NULL;
    }
}

/* the chosen move's card: category, type, power, accuracy, the game's description */
static void InfoCard(int x, int y, int w, int h)
{
    u16 move = sB.moves[sB.moveCursor];
    int cat = GetMoveCategory(move) == DAMAGE_CATEGORY_PHYSICAL ? 0 : GetMoveCategory(move) == DAMAGE_CATEGORY_SPECIAL ? 1 : 2;
    char v[8];
    const u8 *starts[4];
    int lengths[4], n, yy = y + 34;
    const char *eff;
    UiRGBA ec;

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_CREAM,
              &(struct UiPaint){UI_P_DATABOX_INK, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, 0, NULL, 0});
    UiW_CategoryIcon(x + 8, y + 6, cat);
    UiW_TypeBadge(x + 36, y + 6, sB.moveTypes[sB.moveCursor], 0);
    eff = EffectLabel(sB.effect, &ec);
    if (eff != NULL)
        UiMon_Tag(x + 80, y + 7, eff, ec);
    UiCaps_Draw(x + 8, y + 25, "POWER", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(v, sizeof(v), GetMovePower(move) > 1 ? "%u" : "---", (unsigned)GetMovePower(move));
    UiCaps_Draw(x + 84, y + 25, v, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    UiCaps_Draw(x + 96, y + 25, "ACC", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    snprintf(v, sizeof(v), GetMoveAccuracy(move) > 0 ? "%u" : "---", (unsigned)GetMoveAccuracy(move));
    UiCaps_Draw(x + w - 10, y + 25, v, UI_P_TEXT, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    n = UiText_Wrap(GetMoveDescription(move), w - 14, UI_FONT_SMALL, starts, lengths, 4);
    for (int i = 0; i < n && i < 2; i++)
    {
        u8 b[96];

        UiText_CopyN(b, sizeof(b), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, x + 6, yy, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        yy += 12;
    }
}

static const char *GimmickLabel(u8 g)
{
    switch (g)
    {
    case GIMMICK_MEGA: return "MEGA EVOLVE";
    case GIMMICK_Z_MOVE: return "Z-MOVE";
    case GIMMICK_ULTRA_BURST: return "ULTRA BURST";
    case GIMMICK_DYNAMAX: return "DYNAMAX";
    case GIMMICK_TERA: return "TERASTALLIZE";
    default: return NULL;
    }
}

/* the gimmick toggle: lights the gem; once per trainer per battle (the game's rule) */
static void GimmickToggle(int x, int y, bool on)
{
    UiRGBA col = on ? UI_HEX(0xE85890) : UI_HEX(0x8878A8);
    struct UiRamp r = UiRampOf(col, 0.42, 0.25, 0.62);
    const struct UiMask *m = UiM_Rounded4(132, 18, 3, 3, 3, 3);
    const char *label = GimmickLabel(sB.gimmick);

    UiG_Paint(m, x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
    UiGfx_Icon(on ? "gem" : "gem_off", x + 4, y + 4, 0);
    UiCaps_Draw(x + 16, y + 6, label != NULL ? label : "", UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_LEFT, UI_NONE);
    UiG_Paint(UiM_Rounded4(22, 10, 2, 2, 2, 2), x + 104, y + 4, on ? UI_HEX(0xF8F0A0) : UiMix(r.base, r.ink, 0.4),
              &(struct UiPaint){r.ink, UI_NONE, UI_NONE, 0, 0, NULL, 0});
    UiCaps_Draw(x + 115, y + 6, on ? "ON" : "OFF", on ? r.ink : UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    UiG_Hit(HIT_GIMMICK, 0, x, y, 132, 18);
}

/* a double battle's target plate */
static void TargetPlate(int x, int y, int w, int h, const struct Box *b, int state, const char *role, int t)
{
    bool inv = state == 0;
    const struct UiMask *m = UiM_Rounded4(w, h, 3, 3, 3, 3);
    UiRGBA body = inv ? UI_HEX(0xC8C8C0) : sPlateBase, hi = inv ? UI_HEX(0xD8D8D0) : sPlateHi, lo = inv ? UI_HEX(0xB0B0A8) : sPlateLo;
    UiRGBA tag = strcmp(role, "FOE") == 0 ? UI_HEX(0xC85048) : strcmp(role, "ALLY") == 0 ? UI_HEX(0x48A060) : UI_HEX(0x808080);
    static const struct UiFilter sGray = {0.6f, 0, 0, 0, 0};

    if (sB.ffa && b != NULL && b->on)
        tag = TrainerTint(b->battler); /* each trainer has a colour, as on the boxes */
    if (state == 2)
        UiW_SelectFrame(m, x, y, UI_NONE, UI_NONE, 0);
    UiG_Paint(m, x, y, body, &(struct UiPaint){UI_P_DATABOX_INK, hi, lo, 0, 2, NULL, 0});
    if (b == NULL || !b->on)
        return;
    UiGfx_MonIcon(b->mon.species, b->mon.personality, false, state == 2 ? (t / 10) % 2 : 0, x + 9, y + 1, inv ? &sGray : NULL);
    UiText_Draw(UI_FONT_NORMAL, x + 46, y + 1, b->mon.nickname, inv ? UI_HEX(0x888888) : UI_P_TEXT, inv ? UI_NONE : UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
    /* whose it is (the design's trainer name; the role's colour stands for the trainer's) */
    if (b->owner[0] != '\0')
        UiCaps_Draw(x + 46, y + 20, b->owner, inv ? UI_HEX(0x909090) : UiMix(tag, UI_P_INK, 0.25), UI_NONE, UI_CAPS_PLAIN,
                    UI_ALIGN_LEFT, UI_NONE);
    UiCaps_Draw(x + w - 8, y + 20, role, inv ? UI_HEX(0x909090) : tag, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
    if (!inv)
        UiW_HpBar(x + w - 78, y + 5, 58, b->hpFrac, false, UI_NONE);
}

/* ---------------------------------------------------------------- the screen */

static const struct Box *BoxAt(u32 position)
{
    u32 battler = position < MAX_BATTLERS_COUNT ? sB.atPosition[position] : MAX_BATTLERS_COUNT;

    return battler < MAX_BATTLERS_COUNT && sB.box[battler].on ? &sB.box[battler] : NULL;
}

static void DrawBoxes(int L, int R, int T, int dyB)
{
    if (sB.ffa)
    {
        DrawBoxesFfa(L, R, T, dyB);
        return;
    }
    if (!sB.doubles)
    {
        const struct Box *foe = BoxAt(B_POSITION_OPPONENT_LEFT), *me = BoxAt(B_POSITION_PLAYER_LEFT);

        if (foe != NULL)
        {
            Databox(L + foe->slide * 2, T + 18 + foe->bob, foe);
            if (sB.foePipsOn && foe->slide == 0)
                for (int i = 0; i < PARTY_SIZE; i++)
                    if (sB.foePips[i] != 0)
                        Ball(L + 8 + i * 9, T + 58, sB.foePips[i] == 1 ? 0 : sB.foePips[i] == 2 ? 2 : 3);
        }
        if (me != NULL && sB.safari)
            SafariBox(R - 184 + me->slide * 2, 222 + dyB + me->bob);
        else if (me != NULL)
            Databox(R - 184 + me->slide * 2, 222 + dyB + me->bob, me);
        return;
    }
    {
        const struct Box *fa = BoxAt(B_POSITION_OPPONENT_LEFT), *fb = BoxAt(B_POSITION_OPPONENT_RIGHT);
        const struct Box *pa = BoxAt(B_POSITION_PLAYER_LEFT), *pb = BoxAt(B_POSITION_PLAYER_RIGHT);

        if (fa != NULL)
            Databox2(L + fa->slide * 2, T + 22 + fa->bob, fa, false);
        if (fb != NULL)
            Databox2(L + fb->slide * 2, T + 74 + fb->bob, fb, false);
        if (sB.partnerIsMine)
        {
            if (pb != NULL)
                Databox2(R - 176 + pb->slide * 2, 178 + dyB + pb->bob, pb, true);
            if (pa != NULL)
                Databox2(R - 176 + pa->slide * 2, 228 + dyB + pa->bob, pa, true);
        }
        else
        {
            /* a partner's Pokémon (Steven's, another player's) in the design's short
             * ally box above, the player's own below, whichever side each stands on:
             * in a link multi battle a team's second player stands on the right */
            const struct Box *ally = pb, *own = pa;

            if ((pa != NULL && !pa->mine) || (pb != NULL && pb->mine))
                ally = pa, own = pb;
            if (ally != NULL)
                Databox2(R - 156 + ally->slide * 2, 178 + dyB + ally->bob, ally, false);
            if (own != NULL)
                Databox2(R - 176 + own->slide * 2, 228 + dyB + own->bob, own, true);
        }
    }
}

static void DrawMessagePage(int L, int R, int dyB, int t)
{
    int w = R - L;

    MessagePlate(L, 284 + dyB, w, 70);
    if (sB.hasMsg)
        MessageText(L + 14, 293 + dyB, w - 40, sB.msg, sB.msgPrinting ? (int)sB.msgPrinted : -1);
    if (sB.msgWaiting)
        UiW_DownArrow(R - 24, 336 + dyB + ((t / 16) % 2), UI_HEX(0xF8F8F8), false);
    UiG_Hit(HIT_MESSAGE, 0, L, 284 + dyB, w, 70);
    if (sB.yesNo)
        UiW_YesNo(R - 8, 278 + dyB, sB.yesNoCursor, t, UI_ANCHOR_BOTTOMRIGHT, NULL);
}

static void DrawActionPage(int L, int R, int dyB, int t)
{
    static const u8 sChoose[] = _("Choose"), sBack[] = _("Back"), sThrow[] = _("Throw");
    struct UiHintItem h[2] = {{UI_ACT_CONFIRM, sChoose}, {UI_ACT_CANCEL, sBack}};
    int cx = R - 260;

    MessagePlate(L, 284 + dyB, cx - 8 - L, 70);
    if (sB.hasPrompt)
        MessageText(L + 14, 293 + dyB, cx - 8 - L - 28, sB.prompt, -1);
    /* the prompts while the player's own choice waits (not between turns, nor Wally's) */
    if (sB.chooser >= 0 && !sB.wallyChooses)
        UiW_Hints(L + 14, 340 + dyB, h, sB.safari ? 1 : 2, true, 8);
    if (sB.safari)
        SafariCommands(cx, 282 + dyB, sB.input == NATIVE_BATTLE_INPUT_ACTION || sB.input == NATIVE_BATTLE_INPUT_ACTION_WAIT ? sB.actionCursor : -1);
    else
        Commands(cx, 282 + dyB, sB.input == NATIVE_BATTLE_INPUT_ACTION || sB.input == NATIVE_BATTLE_INPUT_ACTION_WAIT ? sB.actionCursor : -1, t);
    if (sB.ballOn)
    {
        /* the last used Ball: R throws it (B_LAST_USED_BALL) */
        int bx = L + 8, by = 260 + dyB;
        struct UiRamp r = UiRampOf(UI_HEX(0x586070), 0.42, 0.25, 0.62);

        UiG_Paint(UiM_Rounded4(120, 20, 3, 3, 3, 3), bx, by, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
        UiGfx_ItemIcon(sB.ball, bx - 2, by - 3, NULL);
        UiW_Hint(bx + 30, by + 5, UI_ACT_R, sThrow, true);
        UiG_Hit(HIT_BALL, 0, bx, by, 120, 20);
    }
}

static void DrawMovePage(int L, int R, int dyB, int t)
{
    static const u8 sBack[] = _("Back"), sClose[] = _("Close"), sSwitch[] = _("Switch which?"), sInfo[] = _("Info");
    bool switching = sB.input == NATIVE_BATTLE_INPUT_MOVE_SWITCH;

    for (int i = 0; i < MAX_MON_MOVES; i++)
    {
        int mx = L + 8 + (i % 2) * 212, my = 284 + dyB + (i / 2) * 38;

        if (sB.moves[i] == MOVE_NONE)
        {
            UiG_Paint(UiM_Rounded4(204, 34, 3, 3, 3, 3), mx, my, UiMix(sPlateBase, sPlateLo, 0.5),
                      &(struct UiPaint){UI_P_DATABOX_INK, UI_NONE, UI_NONE, 0, 0, NULL, 0});
            UiCaps_Draw(mx + 102, my + 13, "---", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
            continue;
        }
        if (switching && i == sB.moveCursor)
            UiW_CarryFrame(UiM_Rounded4(204, 34, 3, 3, 3, 3), mx, my);
        MoveButton(mx, my, 204, 34, i, switching ? i == sB.switchCursor : i == sB.moveCursor);
        UiG_Hit(HIT_MOVE, i, mx, my, 204, 34);
    }
    InfoCard(R - 204, 284 + dyB, 196, 72);
    if (sB.chooser < 0 || sB.wallyChooses)
    {
        /* no prompts between turns, nor while Wally chooses his move himself */
    }
    else if (switching)
    {
        UiText_Draw(UI_FONT_SMALL, R - 198, 342 + dyB, sSwitch, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    else if (sB.descOpen)
    {
        struct UiHintItem h[1] = {{UI_ACT_L, sClose}};

        UiW_Hints(R - 70, 343 + dyB, h, 1, false, 8);
    }
    else
    {
        struct UiHintItem h[2] = {{UI_ACT_L, sInfo}, {UI_ACT_CANCEL, sBack}};

        UiW_Hints(R - 112, 343 + dyB, h, 2, false, 8);
    }
    if (sB.gimmick != GIMMICK_NONE && GimmickLabel(sB.gimmick) != NULL && !switching)
    {
        GimmickToggle(L + 8, 262 + dyB, sB.gimmickOn);
        UiW_Hint(L + 146, 266 + dyB, UI_ACT_START, (const u8 *)"\xFF", false);
    }
}

static void DrawTargetPage(int L, int R, int dyB, int t)
{
    static const u8 sConfirm[] = _("Confirm"), sBack[] = _("Back"), sBoth[] = _("Hits more than one."), sOne[] = _("Choose one target.");
    struct UiHintItem h[2] = {{UI_ACT_CONFIRM, sConfirm}, {UI_ACT_CANCEL, sBack}};
    static const u8 sPos[4] = {B_POSITION_OPPONENT_LEFT, B_POSITION_OPPONENT_RIGHT, B_POSITION_PLAYER_LEFT, B_POSITION_PLAYER_RIGHT};
    static const char *const sRoles[4] = {"FOE", "FOE", "YOU", "ALLY"};
    bool spread = sB.input == NATIVE_BATTLE_INPUT_TARGETS_SHOWN;
    u16 move = sB.moves[sB.moveCursor];

    for (int i = 0; i < 4; i++)
    {
        u32 battler = sB.atPosition[sPos[i]];
        const struct Box *b = BoxAt(sPos[i]);
        int px = L + 8 + (i % 2) * 208, py = 284 + dyB + (i / 2) * 38, state;
        const char *role = sRoles[i];

        if (sB.ffa)
            role = battler == (u32)sB.chooser ? "YOU" : "FOE"; /* nobody is anybody's ally */
        else if (sPos[i] == B_POSITION_PLAYER_RIGHT && battler == (u32)sB.chooser)
            role = "YOU";
        else if (sPos[i] == B_POSITION_PLAYER_LEFT && battler != (u32)sB.chooser)
            role = "ALLY";
        if (spread)
            state = b != NULL && sB.shownAsTarget[battler] ? 2 : 0;
        else if (battler == sB.targetCursor)
            state = 2;
        else
            state = b != NULL && (battler != (u32)sB.chooser || sB.userTargetable) ? 1 : 0;
        TargetPlate(px, py, 200, 34, b, state, role, t);
        if (b != NULL)
            UiG_Hit(HIT_TARGET, battler, px, py, 200, 34);
    }
    {
        int x = R - 212, y = 282 + dyB;

        UiG_Paint(UiM_Rounded4(204, 74, 3, 3, 3, 3), x, y, UI_P_CREAM,
                  &(struct UiPaint){UI_P_DATABOX_INK, UI_HEX(0xFFFFFF), UI_P_CREAM_LO, 0, 0, NULL, 0});
        UiG_Text(x + 8, y + 2, GetMoveName(move));
        UiW_CategoryIcon(x + 8, y + 20, GetMoveCategory(move) == DAMAGE_CATEGORY_PHYSICAL ? 0 : GetMoveCategory(move) == DAMAGE_CATEGORY_SPECIAL ? 1 : 2);
        UiW_TypeBadge(x + 36, y + 20, sB.moveTypes[sB.moveCursor], 0);
        UiText_Draw(UI_FONT_SMALL, x + 8, y + 37, spread ? sBoth : sOne, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiW_Hints(x + 8, y + 58, h, 2, false, 8);
    }
}

static void Draw(int t)
{
    int x0, y0, w, h, L, R, T, dyB;

    if (sTeardown)
        return;

    UiG_Bleed(&x0, &y0, &w, &h);
    L = x0;
    R = x0 + w;
    T = y0;
    dyB = y0 + h - UI_LOGICAL_HEIGHT;
    /* a trainer battle's end: the rewards ledger and the trainer's plate in the databoxes' place */
    if (!UiResults_LedgerShown())
        DrawBoxes(L, R, T, dyB);
    UiResults_Draw(L, R, T, t);
    if (sB.chooser >= 0 && (sB.input == NATIVE_BATTLE_INPUT_TARGET || sB.input == NATIVE_BATTLE_INPUT_TARGETS_SHOWN))
        DrawTargetPage(L, R, dyB, t);
    else if (sB.page == PAGE_MOVES)
        DrawMovePage(L, R, dyB, t);
    else if (sB.page == PAGE_ACTION)
        DrawActionPage(L, R, dyB, t);
    else
        DrawMessagePage(L, R, dyB, t);
    UiG_Veil(NativeUi_ScreenFade());
}

/* ---------------------------------------------------------------- input */

/* the engine's presses from one action cell to another (2x2: bit 0 right, bit 1 down) */
static void QueueCmdPath(int from, int to)
{
    if ((from ^ to) & 1)
        NativeUi_QueuePress((to & 1) ? DPAD_RIGHT : DPAD_LEFT);
    if ((from ^ to) & 2)
        NativeUi_QueuePress((to & 2) ? DPAD_DOWN : DPAD_UP);
}

static void CmdGoalStep(void)
{
    if (sCmdGoal < 0 || NativeUi_PressesPending() || sB.input != NATIVE_BATTLE_INPUT_ACTION)
        return;
    if (sB.actionCursor != sCmdGoal)
    {
        QueueCmdPath(sB.actionCursor, sCmdGoal);
        return;
    }
    if (sCmdGoalThenA)
        NativeUi_QueuePress(A_BUTTON);
    sCmdGoal = -1;
}

static void TargetGoalStep(void)
{
    if (sTargetGoal < 0 || NativeUi_PressesPending() || sB.input != NATIVE_BATTLE_INPUT_TARGET)
        return;
    if (sB.targetCursor == sTargetGoal)
    {
        NativeUi_QueuePress(A_BUTTON);
        sTargetGoal = -1;
        return;
    }
    if (++sTargetSteps > 8)
    {
        sTargetGoal = -1;
        return;
    }
    NativeUi_QueuePress(DPAD_RIGHT);
}

static void QueueMovePath(int from, int to)
{
    if ((from ^ to) & 2)
        NativeUi_QueuePress((to & 2) ? DPAD_DOWN : DPAD_UP);
    if ((from ^ to) & 1)
        NativeUi_QueuePress((to & 1) ? DPAD_RIGHT : DPAD_LEFT);
}

static bool Click(const struct UiHit *hit, int button)
{
    if (sTeardown)
        return true;
    /* Wally's choices are his controller's own: nothing to press meanwhile */
    if (NativeUi_PressesPending() || sCmdGoal >= 0 || sTargetGoal >= 0 || sB.wallyChooses)
        return true;
    if (button == 3)
    {
        if (sB.chooser >= 0 || sB.yesNo)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1)
        return false;
    /* the level-up box: any button turns its page or closes it */
    if (UiResults_LevelUpWaiting())
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    switch (hit->id)
    {
    case HIT_CMD:
        if (sB.input == NATIVE_BATTLE_INPUT_ACTION)
        {
            sCmdGoal = hit->index;
            sCmdGoalThenA = true;
            CmdGoalStep();
        }
        return true;
    case HIT_BALL:
        if (sB.input == NATIVE_BATTLE_INPUT_ACTION && sB.ballOn)
            NativeUi_QueuePress(R_BUTTON);
        return true;
    case HIT_MOVE:
        if (sB.input == NATIVE_BATTLE_INPUT_MOVE && !sB.descOpen)
        {
            QueueMovePath(sB.moveCursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        else if (sB.input == NATIVE_BATTLE_INPUT_MOVE_SWITCH)
        {
            QueueMovePath(sB.switchCursor, hit->index);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case HIT_GIMMICK:
        if (sB.input == NATIVE_BATTLE_INPUT_MOVE)
            NativeUi_QueuePress(START_BUTTON);
        return true;
    case HIT_TARGET:
        if (sB.input == NATIVE_BATTLE_INPUT_TARGET)
        {
            sTargetGoal = hit->index;
            sTargetSteps = 0;
            TargetGoalStep();
        }
        else if (sB.input == NATIVE_BATTLE_INPUT_TARGETS_SHOWN)
        {
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case UI_HIT_YESNO:
        if (sB.yesNo)
        {
            if (hit->index != sB.yesNoCursor)
                NativeUi_QueuePress(hit->index > sB.yesNoCursor ? DPAD_DOWN : DPAD_UP);
            NativeUi_QueuePress(A_BUTTON);
        }
        return true;
    case HIT_MESSAGE:
        if (sB.msgWaiting)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    default:
        return false;
    }
}

static bool Wheel(int dy)
{
    if (sTeardown)
        return true;
    if (NativeUi_PressesPending() || sB.wallyChooses)
        return true;
    if (sB.input == NATIVE_BATTLE_INPUT_MOVE && !sB.descOpen)
    {
        int next = sB.moveCursor + (dy > 0 ? -1 : 1);

        if (next >= 0 && next < sB.moveCount)
            QueueMovePath(sB.moveCursor, next);
        return true;
    }
    return true;
}

/* the design's command grid: FIGHT over BAG, POKéMON and RUN; the engine's cursor follows */
static uint16_t FilterKeys(uint16_t keys, uint16_t newKeys)
{
    int to = -1, at;

    if (sTeardown)
        return keys;

    /* not while a YES / NO box is up over it (the encounter reset), nor while R cycles the last used Ball */
    if (sB.input != NATIVE_BATTLE_INPUT_ACTION || sB.page != PAGE_ACTION || sB.yesNo || !(newKeys & DPAD_ANY)
     || (keys & R_BUTTON) || sB.wallyChooses)
        return keys;
    at = sB.actionCursor;
    if (newKeys & DPAD_UP)
        to = CMD_FIGHT;
    else if (newKeys & DPAD_DOWN)
        to = at == CMD_FIGHT ? CMD_BAG : at;
    else if (newKeys & DPAD_LEFT)
        to = at == CMD_POKEMON ? CMD_BAG : at == CMD_RUN ? CMD_POKEMON : at;
    else if (newKeys & DPAD_RIGHT)
        to = at == CMD_BAG ? CMD_POKEMON : at == CMD_POKEMON ? CMD_RUN : at;
    if (to >= 0 && to != at && !NativeUi_PressesPending())
        QueueCmdPath(at, to);
    return keys & ~DPAD_ANY;
}

static bool Ready(void)
{
    return sB.ready;
}

static enum UiPicture Picture(void)
{
    if (sTeardown)
        return UI_PICTURE_HIDE_BG012;
    return UiResults_LevelUpActive() ? UI_PICTURE_HIDE_BG012 : UI_PICTURE_HIDE_BG0;
}

const struct UiScreen gUiScreen_Battle = {
    .name = "battle",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .wheel = Wheel,
    .filterKeys = FilterKeys,
    .ready = Ready,
    .picture = Picture,
    .hideObjects = HideObjects,
};
#endif // ENABLE_NATIVE_UI
