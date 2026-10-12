#ifdef ENABLE_NATIVE_UI
/*
 * 26 · Battle results (the approved design's preview/js/screens/results.js),
 * drawn by the battle HUD (ui_battle.c) over the engine's own battle end:
 *
 * The level-up box: while the engine runs its box (Cmd_drawlvlupbox), the
 * design's Gen 3/4 window in its place - the Pokémon's name and level, its
 * stats' gains, then (after a button, the engine's) the new totals, from the
 * engine's stats before and now. The engine's box and banner (BG1, BG2) are
 * left out of the picture meanwhile.
 *
 * The rewards ledger: when a trainer battle is won ("REWARDS": the prize money
 * the engine pays, the money before and after, each party Pokémon's level and
 * Exp. bar with the Exp. it gained in this battle, UP for levels gained, NO EXP.
 * when fainted, TRADED for the boosted ones) or lost with money paid ("OUT OF
 * POKéMON": the money paid), with the defeated trainer's plate. The money and
 * Exp. before the battle are noted while its intro runs. The engine's messages
 * stay in the HUD's message plate. Link and multiplayer battles show no ledger
 * (their records are the host's: PLAYERS); co-op prizes are planned.
 * Project-owned.
 */
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "battle_setup.h"
#include "data.h"
#include "graphics.h"
#include "money.h"
#include "multiplayer_battle.h"
#include "pokemon.h"
#include "strings.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_items.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_results.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define LW 196
#define LH 258
#define BARW 86

struct Snap
{
    bool used;
    u32 personality;
    u32 otId;
    u32 exp;
    u8 level;
};

static const u8 sText_RightArrow[] = _("{RIGHT_ARROW}");

/* ---------------------------------------------------------------- captured state */

static struct NativeLevelUpBox sBox;
static bool sBoxActive;
static struct UiMon sBoxMon;
static bool sSnapValid;
static u32 sSnapMoney;
static struct Snap sSnap[PARTY_SIZE];
static bool sLedger;
static bool sWon;
static u32 sPrize;            /* won: the prize the engine pays; lost: the money paid */
static u32 sMoneyNow;
static u32 sPlayerOtId;
static struct UiMon sParty[PARTY_SIZE];
static u32 sExpNow[PARTY_SIZE];
static s32 sGain[PARTY_SIZE];   /* -1: not known */
static u8 sLevelsUp[PARTY_SIZE];
static bool sTraded[PARTY_SIZE];
static int sPartyCount;
static u8 sTrainerClass[32];
static u8 sTrainerName[32];
static u8 sFoePips[PARTY_SIZE]; /* the tray's balls: 0 full, 1 empty, 2 fainted, 3 statused */
static bool sFoePipsOn;

/* a battle that pays the engine's prize money (an AI trainer, not link or multiplayer) */
static bool PrizeBattle(void)
{
    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER))
        return false;
    if (gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_EREADER_TRAINER
                          | BATTLE_TYPE_TRAINER_HILL | BATTLE_TYPE_RECORDED))
        return false;
    return !MultiplayerBattle_IsActive();
}

static void Snapshot(void)
{
    sSnapMoney = GetMoney(&gSaveBlock1Ptr->money);
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        sSnap[i].used = GetMonData(mon, MON_DATA_SPECIES) != SPECIES_NONE;
        sSnap[i].personality = GetMonData(mon, MON_DATA_PERSONALITY);
        sSnap[i].otId = GetMonData(mon, MON_DATA_OT_ID);
        sSnap[i].exp = GetMonData(mon, MON_DATA_EXP);
        sSnap[i].level = GetMonData(mon, MON_DATA_LEVEL);
    }
    sSnapValid = true;
}

static const struct Snap *SnapOf(u32 personality, u32 otId)
{
    for (int i = 0; i < PARTY_SIZE; i++)
        if (sSnap[i].used && sSnap[i].personality == personality && sSnap[i].otId == otId)
            return &sSnap[i];
    return NULL;
}

static void CaptureFoe(void)
{
    u32 foe = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    u16 trainerId = TRAINER_BATTLE_PARAM.opponentA;
    struct Pokemon *party;

    sTrainerClass[0] = sTrainerName[0] = EOS;
    UiText_Append(sTrainerClass, sizeof(sTrainerClass), GetTrainerClassNameFromId(trainerId));
    UiText_Append(sTrainerName, sizeof(sTrainerName), GetTrainerNameFromId(trainerId));
    if (gBattleTypeFlags & BATTLE_TYPE_TWO_OPPONENTS)
    {
        UiText_AppendAscii(sTrainerName, sizeof(sTrainerName), " & ");
        UiText_Append(sTrainerName, sizeof(sTrainerName), GetTrainerNameFromId(TRAINER_BATTLE_PARAM.opponentB));
    }
    sFoePipsOn = false;
    if (foe >= gBattlersCount || (party = GetBattlerParty(foe)) == NULL)
        return;
    sFoePipsOn = true;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        u16 species = GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG);

        if (species == SPECIES_NONE || species == SPECIES_EGG)
            sFoePips[i] = 1;
        else if (GetMonData(&party[i], MON_DATA_HP) == 0)
            sFoePips[i] = 2;
        else if (GetMonData(&party[i], MON_DATA_STATUS) != 0)
            sFoePips[i] = 3;
        else
            sFoePips[i] = 0;
    }
}

void UiResults_Capture(void)
{
    u32 outcome = gBattleOutcome & 0x7F;

    if (Battle_NativeUiInIntro())
        Snapshot();
    sBoxActive = Battle_NativeUiLevelUpBox(&sBox);
    if (sBoxActive)
        UiMon_FromPokemon(&sBoxMon, &gParties[B_TRAINER_PLAYER][sBox.partyId]);
    sMoneyNow = GetMoney(&gSaveBlock1Ptr->money);
    sLedger = false;
    sWon = outcome == B_OUTCOME_WON;
    if (!PrizeBattle() || !(outcome == B_OUTCOME_WON || outcome == B_OUTCOME_LOST))
        return;
    if (outcome == B_OUTCOME_WON)
        sPrize = Battle_NativeUiPrizeMoney();
    else if (sSnapValid && sSnapMoney > sMoneyNow)
        sPrize = sSnapMoney - sMoneyNow;
    else
        return;                    /* a loss without money paid (no whiteout): the engine's messages alone */
    sLedger = true;
    sPlayerOtId = READ_OTID_FROM_SAVE;
    sPartyCount = 0;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        const struct Snap *s;

        UiMon_FromPokemon(&sParty[i], mon);
        if (sParty[i].species == SPECIES_NONE)
            continue;
        sPartyCount = i + 1;
        sExpNow[i] = GetMonData(mon, MON_DATA_EXP);
        s = sSnapValid ? SnapOf(sParty[i].personality, GetMonData(mon, MON_DATA_OT_ID)) : NULL;
        sGain[i] = s != NULL && sExpNow[i] >= s->exp ? (s32)(sExpNow[i] - s->exp) : -1;
        sLevelsUp[i] = s != NULL && sParty[i].level > s->level ? sParty[i].level - s->level : 0;
        sTraded[i] = GetMonData(mon, MON_DATA_OT_ID) != sPlayerOtId;
    }
    CaptureFoe();
}

bool UiResults_LevelUpActive(void)
{
    return sBoxActive;
}

bool UiResults_LevelUpWaiting(void)
{
    return sBoxActive && sBox.waiting;
}

bool UiResults_LedgerShown(void)
{
    return sLedger;
}

/* ---------------------------------------------------------------- pieces */

static struct UiRamp Gold(void)
{
    return UiRampOf(UI_P_CMD_BAG, 0.45, 0.25, 0.6);
}

static struct UiRamp Steel(void)
{
    return UiRampOf(UiMix(UI_P_CMD_RUN, UI_P_TEXT_MUTED, 0.55), 0.42, 0.25, 0.6);
}

static UiRGBA Gain(void)
{
    return UiMix(UI_P_CMD_POKEMON, UI_P_INK, 0.25);
}

static UiRGBA GainSh(void)
{
    return UiMix(UI_P_CMD_POKEMON, UI_P_PAPER, 0.75);
}

/* the swallowtail ribbon header */
static void Ribbon(int x, int y, int w, const char *label, const struct UiRamp *r)
{
    const int pts[12] = {0, 0, w - 1, 0, w - 7, 8, w - 1, 16, 0, 16, 6, 8};

    UiG_Paint(UiM_Poly(w, 17, pts, 6), x, y, r->base, &(struct UiPaint){r->ink, r->hi, r->lo, 0, 0, NULL, UI_NONE});
    UiG_HLine(x + 10, y + 3, w - 20, UiMix(r->base, r->hi, 0.5));
    UiCaps_Draw(x + (w >> 1), y + 5, label, UI_P_TEXT_W, r->ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
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

static void Ball(int x, int y, u32 kind)
{
    struct BallCtx c = {kind};
    char key[24];

    snprintf(key, sizeof(key), "btlball:%u", (unsigned)kind);
    UiG_Blit(UiG_Cached(key, 8, 8, BuildBall, &c), x, y);
}

/* the defeated (or winning) trainer's plate, where the foe's databox was */
static void TrainerPlate(int x, int y)
{
    int w = 176, h = 40;

    UiG_Paint(UiM_Rounded4(w, h, 0, 4, 0, 4), x, y, UI_HEX(0xF4F4E6),
              &(struct UiPaint){UI_P_DATABOX_INK, UI_HEX(0xFFFFFF), UI_HEX(0xCFCFBE), 0, 2, NULL, UI_NONE});
    UiText_Draw(UI_FONT_SMALL, x + 8, y + 2, sTrainerClass, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_LEFT, 0);
    UiG_Text(x + 8, y + 15, sTrainerName);
    if (sFoePipsOn)
        for (int i = 0; i < PARTY_SIZE; i++)
            Ball(x + w - 62 + i * 9, y + 5, sFoePips[i]);
}

static void Money(u8 *dst, int size, u32 amount)
{
    UiItems_Money(dst, size, amount);
}

static void Ledger(int x, int y, int t)
{
    struct UiRamp gold = Gold(), steel = Steel();
    UiRGBA gain = Gain(), gainSh = GainSh();
    u32 before = sSnapValid ? sSnapMoney : 0, after;
    int py = y + 26, ry;
    u8 buf[32];

    UiG_Paint(UiM_Rounded4(LW, LH - 8, 3, 3, 3, 3), x, y + 8, UI_P_CREAM,
              &(struct UiPaint){UI_P_DATABOX_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 2, NULL, UI_NONE});
    Ribbon(x - 6, y, LW + 12, sWon ? "REWARDS" : "OUT OF POK" UI_CAPS_E_ACUTE "MON", sWon ? &gold : &steel);
    /* the prize money, the money before and after */
    UiCaps_Draw(x + 8, py + 5, sWon ? "PRIZE MONEY" : "PRIZE MONEY PAID", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    buf[0] = EOS;
    if (!sWon)
        UiText_AppendAscii(buf, sizeof(buf), "-");
    {
        u8 m[24];

        Money(m, sizeof(m), sPrize);
        UiText_Append(buf, sizeof(buf), m);
    }
    UiG_TextEx(x + LW - 8, py, buf, UI_FONT_NORMAL, sWon ? UI_P_TEXT : UI_P_TEXT_RED, sWon ? UI_P_TEXT_SH : UI_P_TEXT_RED_SH, UI_ALIGN_RIGHT);
    UiCaps_Draw(x + 8, py + 20, "MONEY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    /* won: what the money becomes (it is paid after the trainer's last words) */
    after = sWon ? (sSnapValid && sMoneyNow == sSnapMoney ? sSnapMoney + sPrize : sMoneyNow) : sMoneyNow;
    Money(buf, sizeof(buf), after);
    UiG_TextEx(x + LW - 8, py + 15, buf, UI_FONT_NORMAL, sWon ? gain : UI_P_TEXT_RED, sWon ? gainSh : UI_P_TEXT_RED_SH, UI_ALIGN_RIGHT);
    if (sSnapValid)
    {
        int bx = x + LW - 8 - UiText_Width(UI_FONT_NORMAL, buf, 0) - 2;
        int aw = UiText_Width(UI_FONT_NORMAL, sText_RightArrow, 0);

        UiG_TextEx(bx, py + 15, sText_RightArrow, UI_FONT_NORMAL, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_RIGHT);
        Money(buf, sizeof(buf), before);
        UiG_TextEx(bx - aw - 2, py + 15, buf, UI_FONT_NORMAL, UI_P_TEXT_MUTED, UI_NONE, UI_ALIGN_RIGHT);
    }
    UiG_HLine(x + 6, py + 31, LW - 12, UI_P_CREAM_LO);
    UiG_HLine(x + 6, py + 32, LW - 12, UI_P_CREAM_HI);
    /* the party */
    ry = py + 36;
    for (int i = 0; i < sPartyCount; i++)
    {
        const struct UiMon *m = &sParty[i];
        static const struct UiFilter sGray = {0.65f, 0, 0, 0, 0};
        u8 lv[16];
        int nx, lvw, rh = 32;
        bool fainted = !m->isEgg && m->hp == 0;

        if (m->species == SPECIES_NONE)
            continue;
        if (i < sPartyCount - 1)
            for (int dx = x + 10; dx < x + LW - 10; dx += 3)
                UiG_Put(dx, ry + rh - 1, UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.3));
        UiG_Save();
        UiG_Clip(x + 4, ry, 34, rh - 1);
        UiGfx_MonIcon(m->species, m->personality, m->isEgg, fainted ? 0 : ((t + i * 7) / 20) % 2, x + 4, ry - 4, fainted ? &sGray : NULL);
        UiG_Restore();
        nx = UiG_Text(x + 38, ry, m->nickname);
        if (sTraded[i] && !m->isEgg)
            UiCaps_Draw(nx + 4, ry + 5, "TRADED", UiMix(gold.lo, UI_P_INK, 0.2), UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        if (m->isEgg)
        {
            ry += rh;
            continue;
        }
        UiMon_LevelText(lv, sizeof(lv), m->level);
        lvw = UiText_Width(UI_FONT_NORMAL, lv, 0);
        UiG_TextEx(x + LW - 8, ry, lv, UI_FONT_NORMAL, sLevelsUp[i] ? gain : UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        if (fainted)
        {
            UiRGBA c;
            const char *st = UiMon_StatusLabel(m, &c);

            UiW_StatusBadge(x + 38, ry + 17, st != NULL ? st : "FNT", st != NULL ? c : UI_P_STATUS_FNT, 0);
            UiCaps_Draw(x + LW - 8, ry + 19, "NO EXP.", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
        }
        else
        {
            UiW_ExpBar(x + 38, ry + 20, BARW + 2, m->expFrac, false);
            if (sGain[i] >= 0)
            {
                char g[24];

                snprintf(g, sizeof(g), "+%u", (unsigned)sGain[i]);
                if (sGain[i] >= 1000)
                    snprintf(g, sizeof(g), "+%u,%03u", (unsigned)(sGain[i] / 1000), (unsigned)(sGain[i] % 1000));
                UiCaps_Draw(x + LW - 8, ry + 18, g, sTraded[i] ? UiMix(gold.lo, UI_P_INK, 0.15) : UI_P_TEXT, UI_NONE,
                            UI_CAPS_PLAIN, UI_ALIGN_RIGHT, UI_NONE);
            }
        }
        if (sLevelsUp[i])
        {
            struct UiRamp lr = UiRampOf(UI_P_CMD_POKEMON, 0.4, 0.25, 0.6);
            int ux = x + LW - 36 - lvw - 2;

            UiG_Paint(UiM_Capsule(28, 9, true, true), ux, ry + 4, lr.base, &(struct UiPaint){lr.ink, lr.hi, UI_NONE, 0, 0, NULL, UI_NONE});
            UiCaps_Draw(ux + 14, ry + 5, "UP", UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        }
        ry += rh;
    }
}

/* the Gen 3/4 level-up window: the gains, then the totals */
static void LevelBox(int x, int y)
{
    static const u8 order[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};
    const u8 *const labels[NUM_STATS] = {gText_MaxHP, gText_Attack, gText_Defense, gText_SpAtk, gText_SpDef, gText_Speed};
    struct UiRamp gold = Gold();
    int w = LW - 12, h = 129;
    u8 lv[16];

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_PAPER,
              &(struct UiPaint){UI_P_DATABOX_INK, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 2, NULL, UI_NONE});
    UiG_Paint(UiM_Rounded4(w - 6, 18, 2, 2, 0, 0), x + 3, y + 3, gold.base, &(struct UiPaint){UI_NONE, gold.hi, UI_NONE, 0, 0, NULL, UI_NONE});
    UiText_Draw(UI_FONT_NORMAL, x + 10, y + 3, sBoxMon.nickname, UI_P_TEXT_W, gold.ink, UI_ALIGN_LEFT, 0);
    UiMon_LevelText(lv, sizeof(lv), sBox.level);
    UiText_Draw(UI_FONT_NORMAL, x + w - 10, y + 3, lv, UI_P_TEXT_W, gold.ink, UI_ALIGN_RIGHT, 0);
    for (int i = 0; i < NUM_STATS; i++)
    {
        int yy = y + 25 + i * 16;
        u32 s = order[i];
        u8 v[16];

        UiG_Text(x + 10, yy - 1, labels[i]);
        v[0] = EOS;
        if (sBox.page == 1)
        {
            int d = (int)sBox.after[s] - (int)sBox.before[s];

            UiText_AppendAscii(v, sizeof(v), d >= 0 ? "+" : "-");
            UiText_AppendNumber(v, sizeof(v), d >= 0 ? d : -d, 1, false);
            UiG_TextEx(x + w - 12, yy - 1, v, UI_FONT_NORMAL, Gain(), GainSh(), UI_ALIGN_RIGHT);
        }
        else
        {
            UiText_AppendNumber(v, sizeof(v), sBox.after[s], 1, false);
            UiG_TextEx(x + w - 12, yy - 1, v, UI_FONT_NORMAL, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_RIGHT);
        }
    }
}

void UiResults_Draw(int L, int R, int T, int t)
{
    int lx = R - (UI_LOGICAL_WIDTH - 436), ly = 22;

    (void)L;
    if (sLedger)
    {
        TrainerPlate(L, T + 18);
        Ledger(lx, ly, t);
    }
    if (sBoxActive && sBox.shown)
        LevelBox(lx + 6, ly + 62);
}
#endif // ENABLE_NATIVE_UI
