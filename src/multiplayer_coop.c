#include "global.h"
#include "multiplayer_coop.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "multiplayer_world.h"
#include "battle.h"
#include "battle_setup.h"
#include "data.h"
#include "event_data.h"
#include "event_scripts.h"
#include "main.h"
#include "money.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "constants/battle.h"
#include "constants/maps.h"
#include "constants/opponents.h"
#ifdef PORTABLE
#include "battle_controllers.h"
#include "evolution_scene.h"
#include "field_weather.h"
#include "move.h"
#include "pokemon.h"
#include "pokemon_summary_screen.h"
#include "constants/characters.h"
#include "constants/pokemon.h"
static void ResetCoopExp(void);
static void TrackCoopBattle(void);
static void CaptureCoopExp(u32 outcome);
#endif
#line 22 // gba-line

// Co-op Gym battles (owner brief "GYMS AND TWIN GYM LEADERS").
//
// A solo challenger keeps the ordinary Gym battle. When the player's
// host-formed teammate stands in the same Gym, the Leader offers a team
// challenge instead: the game asks the host (world action COOP_GYM) to issue a
// native co-op challenge, which both trainers accept and enter from the
// Players menu like any native battle. Both trainers then fight together
// against the Leader and the Leader's partner, each choosing for their own
// Pokemon while the master core runs both AI trainers.
//
// Native battles restore every party exactly afterwards (the host's safety
// invariant for all networked battles), so the co-op battle itself grants no
// experience. Each trainer's own game grants its own rewards when it returns
// from a victory: the Gym's original victory script (badge, TM and story
// flags) for a first win, and the Leader's prize money for every winner.

struct CoopGym
{
    u16 leader;
    u16 twin;
    u16 map;
};

static const struct CoopGym sCoopGyms[MP_COOP_GYM_COUNT + 1] =
{
    [MP_COOP_GYM_RUSTBORO]   = { TRAINER_ROXANNE_1,       TRAINER_MP_TWIN_ROXANNE,       MAP_RUSTBORO_CITY_GYM },
    [MP_COOP_GYM_DEWFORD]    = { TRAINER_BRAWLY_1,        TRAINER_MP_TWIN_BRAWLY,        MAP_DEWFORD_TOWN_GYM },
    [MP_COOP_GYM_MAUVILLE]   = { TRAINER_WATTSON_1,       TRAINER_MP_TWIN_WATTSON,       MAP_MAUVILLE_CITY_GYM },
    [MP_COOP_GYM_LAVARIDGE]  = { TRAINER_FLANNERY_1,      TRAINER_MP_TWIN_FLANNERY,      MAP_LAVARIDGE_TOWN_GYM_1F },
    [MP_COOP_GYM_PETALBURG]  = { TRAINER_NORMAN_1,        TRAINER_MP_TWIN_NORMAN,        MAP_PETALBURG_CITY_GYM },
    [MP_COOP_GYM_FORTREE]    = { TRAINER_WINONA_1,        TRAINER_MP_TWIN_WINONA,        MAP_FORTREE_CITY_GYM },
    [MP_COOP_GYM_MOSSDEEP]   = { TRAINER_TATE_AND_LIZA_1, TRAINER_MP_TWIN_TATE_AND_LIZA, MAP_MOSSDEEP_CITY_GYM },
    [MP_COOP_GYM_SOOTOPOLIS] = { TRAINER_JUAN_1,          TRAINER_MP_TWIN_JUAN,          MAP_SOOTOPOLIS_CITY_GYM_1F },
};

#define REQUEST_TIMEOUT_FRAMES 300

static EWRAM_DATA u8 sPendingGym = 0;
static EWRAM_DATA u8 sPendingOutcome = 0;

bool32 MultiplayerCoop_IsValidGym(u32 gym)
{
    return gym >= MP_COOP_GYM_RUSTBORO && gym <= MP_COOP_GYM_COUNT;
}

u16 MultiplayerCoop_GetLeader(u32 gym)
{
    return MultiplayerCoop_IsValidGym(gym) ? sCoopGyms[gym].leader : TRAINER_NONE;
}

u16 MultiplayerCoop_GetTwin(u32 gym)
{
    return MultiplayerCoop_IsValidGym(gym) ? sCoopGyms[gym].twin : TRAINER_NONE;
}

void MultiplayerCoop_Init(void)
{
    sPendingGym = 0;
    sPendingOutcome = 0;
#ifdef PORTABLE
    ResetCoopExp();
#endif
#line 82 // gba-line
}

static bool32 InGym(u32 gym)
{
    u16 map = sCoopGyms[gym].map;
    return gSaveBlock1Ptr != NULL
        && gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(map)
        && gSaveBlock1Ptr->location.mapNum == MAP_NUM(map);
}

// VAR_0x8004 = Gym. VAR_RESULT: MP_COOP_OFFER_READY when this player's
// teammate is connected and in the same Gym, otherwise MP_COOP_OFFER_NONE.
void MultiplayerCoop_CanOffer(void)
{
    u32 gym = gSpecialVar_0x8004, teammate;

    gSpecialVar_Result = MP_COOP_OFFER_NONE;
    if (!MultiplayerCoop_IsValidGym(gym) || !Multiplayer_IsConnected() || !InGym(gym)
     || MultiplayerBattle_IsActive() || HasTrainerBeenFought(sCoopGyms[gym].leader))
        return;
    teammate = MultiplayerWorld_GetTeammateToken();
    if (teammate != 0 && Multiplayer_IsPeerOnCurrentMap(teammate))
        gSpecialVar_Result = MP_COOP_OFFER_READY;
}

#define tTimer  data[0]
#define tPosted data[1]

// The host acknowledges the request once it has issued the challenge (or
// refused it with a banner). The script resumes either way.
static void Task_WaitCoopRequest(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (!tPosted)
    {
        // One frame later than the special: its waitstate must run first.
        gSpecialVar_Result = FALSE;
        DestroyTask(taskId);
        ScriptContext_Enable();
        return;
    }
    if (!MultiplayerWorld_IsActionPending() || ++tTimer > REQUEST_TIMEOUT_FRAMES)
    {
        gSpecialVar_Result = !MultiplayerWorld_IsActionPending();
        DestroyTask(taskId);
        ScriptContext_Enable();
    }
}

// VAR_0x8004 = Gym. Must be followed by waitstate. VAR_RESULT = TRUE when the
// host received the request.
void MultiplayerCoop_RequestBattle(void)
{
    u8 taskId = CreateTask(Task_WaitCoopRequest, 80);

    if (taskId == TASK_NONE)
    {
        // No free task: post nothing and let the script continue.
        gSpecialVar_Result = FALSE;
        ScriptContext_Enable();
        return;
    }
    gTasks[taskId].tTimer = 0;
    gTasks[taskId].tPosted = MultiplayerCoop_IsValidGym(gSpecialVar_0x8004)
        && MultiplayerWorld_PostCoopRequest(gSpecialVar_0x8004);
}

#undef tTimer
#undef tPosted

static u32 LeaderPrizeMoney(u32 gym)
{
    u16 leader = sCoopGyms[gym].leader;
    const struct TrainerMon *party = GetTrainerPartyFromId(leader);
    u32 size = GetTrainerPartySizeFromId(leader), rate, money;

    if (party == NULL || size == 0)
        return 0;
    rate = gTrainerClasses[GetTrainerClassFromId(leader)].money;
    if (rate == 0)
        rate = 5;
    // The ordinary solo formula for this Leader (double for Tate & Liza).
    money = 4 * party[size - 1].lvl * rate;
    if (GetTrainerStructFromId(leader)->battleType == TRAINER_BATTLE_TYPE_DOUBLES)
        money *= 2;
    return money;
}

// VAR_0x8004 = Gym. Adds the Leader's prize money; STR_VAR_1 = amount.
void MultiplayerCoop_GivePrizeMoney(void)
{
    u32 money = MultiplayerCoop_IsValidGym(gSpecialVar_0x8004) ? LeaderPrizeMoney(gSpecialVar_0x8004) : 0;
    AddMoney(&gSaveBlock1Ptr->money, money);
    ConvertIntToDecimalStringN(gStringVar1, money, STR_CONV_MODE_LEFT_ALIGN, 6);
}

// VAR_0x8004 = Gym. STR_VAR_2 = the Leader's partner's name.
void MultiplayerCoop_BufferPartnerName(void)
{
    u32 gym = gSpecialVar_0x8004;
    StringCopy(gStringVar2, MultiplayerCoop_IsValidGym(gym) ? GetTrainerNameFromId(sCoopGyms[gym].twin) : gText_EmptyString2);
}

void MultiplayerCoop_OnNativeResult(u32 gym, u32 outcome)
{
#ifdef PORTABLE
    // Co-op experience: kept before the party is restored (below).
    CaptureCoopExp(outcome);
#endif
#line 188 // gba-line
    if (!MultiplayerCoop_IsValidGym(gym))
        return;
    sPendingGym = gym;
    sPendingOutcome = outcome;
}

// After the native battle has fully released this game back to a calm field,
// run this player's own reward (or consolation) script once.
void MultiplayerCoop_Tick(void)
{
#ifdef PORTABLE
    TrackCoopBattle();
#endif
#line 198 // gba-line
    if (sPendingGym == 0)
        return;
    if (MultiplayerBattle_IsActive() || MultiplayerBattle_PauseClocks())
        return;
    if (gMain.callback2 != CB2_Overworld || ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked()
     || gPaletteFade.active)
        return;
    gSpecialVar_0x8004 = sPendingGym;
    if (sPendingOutcome == B_OUTCOME_WON)
        ScriptContext_SetupScript(EventScript_MultiplayerCoopVictory);
    else
        ScriptContext_SetupScript(EventScript_MultiplayerCoopDefeat);
    sPendingGym = 0;
    sPendingOutcome = 0;
}

#ifdef PORTABLE
// ---------------------------------------------------------------------------
// Co-op experience (owner decision 2026-10-13: co-op battles award experience
// to every participating player; battles between players never do).
//
// Only the master game runs the battle script, link battles give no
// experience in battle (BattleTypeAllowsExp), and every native battle
// restores the party exactly (the host's canonical-party check). So each
// player's own game, for its own party only:
//   1. while the co-op battle runs (TrackCoopBattle, every frame), notes which
//      of its Pokemon were on the field with each opposing Pokemon, and when an
//      opposing Pokemon falls, which of its Pokemon could still battle;
//   2. when the battle ends (CaptureCoopExp, before the restore), keeps that
//      list of defeated Pokemon;
//   3. once the battle has released the party and the field is calm, awards
//      it (EventScript_MultiplayerCoopExp, run by the victory and defeat
//      scripts): for every defeated Pokemon, once, the full experience to each
//      of its Pokemon that faced it and half to every other one that could
//      still battle (the permanent Gen 9 Exp. Share), none to fainted Pokemon
//      or Eggs; the battle's own formula (MultiplayerCoop_ExpReward), EVs,
//      friendship, level-ups, moves learned on level-up (with the usual "forget
//      a move?" choice) and evolutions.
// Nothing is sent to the host or the teammate: the next checkpoint carries the
// new experience like a wild battle's. The list is consumed once; a reset
// before the award loses it and never doubles it.

#define COOP_MAX_KOS (2 * PARTY_SIZE)

enum
{
    COOP_EXP_STAGE_GAINED,
    COOP_EXP_STAGE_TEAM,
    COOP_EXP_STAGE_LEVEL,
    COOP_EXP_STAGE_MOVES,
    COOP_EXP_STAGE_EVOLVE,
    COOP_EXP_STAGE_DONE,
};

struct CoopKo
{
    u16 species;
    u8 level;
    u8 faced; // own party slots that were on the field with it
    u8 alive; // own party slots that could still battle when it fell
};

static EWRAM_DATA struct
{
    bool8 tracking;
    bool8 pending;
    u8 count;
    u8 faced[2][PARTY_SIZE];
    bool8 seen[2][PARTY_SIZE];
    bool8 fallen[2][PARTY_SIZE];
    struct CoopKo kos[COOP_MAX_KOS];
} sCoopExp = {0};

static EWRAM_DATA struct
{
    u8 stage;
    u8 slot;
    u8 level;
    bool8 firstMove;
    u8 participants; // gained experience by facing a defeated Pokemon
    u8 sharers;      // gained only the Exp. Share's half
    u8 leveled;
    u8 tried;        // offered an evolution
    u8 startLevel[PARTY_SIZE];
    u32 gained[PARTY_SIZE];
    u16 move;
    u16 evolveSpecies;
    bool8 canStopEvo;
    u8 evolveMode;
    u8 evolveArg;
} sCoopAward = {0};

u32 MultiplayerCoop_ExpReward(struct Pokemon *mon, enum Species faintedSpecies, u32 faintedLevel, bool32 faced);

static const u8 sText_CoopExpGained[] = _("{STR_VAR_1} gained {STR_VAR_2} Exp. Points!");
static const u8 sText_CoopExpTeam[] = _("The rest of your team gained Exp.\nPoints thanks to the Exp. Share!");
static const u8 sText_CoopExpLevel[] = _("{STR_VAR_1} grew to Lv. {STR_VAR_2}!");
static const u8 sText_CoopExpLearned[] = _("{STR_VAR_1} learned {STR_VAR_2}!");
static const u8 sText_CoopExpForgot[] = _("1, {PAUSE 15}2, and{PAUSE 15}… {PAUSE 15}… {PAUSE 15}… {PAUSE 15}Poof!\p{STR_VAR_1} forgot {STR_VAR_3}.\pAnd…\p{STR_VAR_1} learned {STR_VAR_2}!");
static const u8 sText_CoopExpNotLearned[] = _("{STR_VAR_1} did not learn {STR_VAR_2}.");
static const u8 sText_CoopExpHm[] = _("HM moves can't be forgotten now.");

static void ResetCoopExp(void)
{
    memset(&sCoopExp, 0, sizeof(sCoopExp));
    memset(&sCoopAward, 0, sizeof(sCoopAward));
}

// A Pokemon (not an Egg, not an empty slot), standing or fainted.
static bool32 IsCoopMon(struct Pokemon *mon)
{
    enum Species species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    return species != SPECIES_NONE && species != SPECIES_EGG && !GetMonData(mon, MON_DATA_IS_EGG);
}

static struct Pokemon *OpposingMon(u32 side, u32 slot)
{
    return &gParties[side ? B_TRAINER_OPPONENT_B : B_TRAINER_OPPONENT_A][slot];
}

static u32 OwnAbleMask(void)
{
    u32 i, mask = 0;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (IsValidForBattle(&gParties[B_TRAINER_PLAYER][i]))
            mask |= 1u << i;
    }
    return mask;
}

static void RecordKo(u32 side, u32 slot, u32 able)
{
    struct Pokemon *mon = OpposingMon(side, slot);

    sCoopExp.fallen[side][slot] = TRUE;
    if (sCoopExp.count >= COOP_MAX_KOS)
        return;
    sCoopExp.kos[sCoopExp.count].species = GetMonData(mon, MON_DATA_SPECIES);
    sCoopExp.kos[sCoopExp.count].level = GetMonData(mon, MON_DATA_LEVEL);
    sCoopExp.kos[sCoopExp.count].faced = sCoopExp.faced[side][slot];
    sCoopExp.kos[sCoopExp.count].alive = able;
    sCoopExp.count++;
}

static void TrackCoopFrame(void)
{
    u32 battler, own = MAX_BATTLERS_COUNT, ownSlot = PARTY_SIZE, side, slot;
    bool32 ownOnField;

    if (!sCoopExp.tracking)
    {
        // A new co-op battle. An award still waiting keeps its list.
        memset(sCoopExp.faced, 0, sizeof(sCoopExp.faced));
        memset(sCoopExp.seen, 0, sizeof(sCoopExp.seen));
        memset(sCoopExp.fallen, 0, sizeof(sCoopExp.fallen));
        sCoopExp.tracking = TRUE;
    }
    for (battler = 0; battler < gBattlersCount; battler++)
    {
        if (gBattlerBattleController[battler] == BATTLE_CONTROLLER_PLAYER)
            own = battler;
    }
    if (own < MAX_BATTLERS_COUNT)
        ownSlot = gBattlerPartyIndexes[own];
    ownOnField = own < MAX_BATTLERS_COUNT && ownSlot < PARTY_SIZE && !(gAbsentBattlerFlags & (1u << own))
              && GetMonData(&gParties[B_TRAINER_PLAYER][ownSlot], MON_DATA_HP) != 0;
    for (battler = 0; battler < gBattlersCount; battler++)
    {
        if (GetBattlerSide(battler) != B_SIDE_OPPONENT || (gAbsentBattlerFlags & (1u << battler)))
            continue;
        side = (battler & BIT_FLANK) ? 1 : 0;
        slot = gBattlerPartyIndexes[battler];
        if (slot < PARTY_SIZE && ownOnField && GetMonData(OpposingMon(side, slot), MON_DATA_HP) != 0)
            sCoopExp.faced[side][slot] |= 1u << ownSlot;
    }
    // An opposing Pokemon this battle showed standing that has now fallen.
    for (side = 0; side < 2; side++)
    {
        for (slot = 0; slot < PARTY_SIZE; slot++)
        {
            struct Pokemon *mon = OpposingMon(side, slot);

            if (sCoopExp.fallen[side][slot] || !IsCoopMon(mon))
                continue;
            if (GetMonData(mon, MON_DATA_HP) != 0)
                sCoopExp.seen[side][slot] = TRUE;
            else if (sCoopExp.seen[side][slot])
                RecordKo(side, slot, OwnAbleMask());
        }
    }
}

static EWRAM_DATA bool8 sTestAwardArmed = FALSE;
static EWRAM_DATA u16 sTestAwardCalmFrames = 0;

// --test-coop-exp-award (a test of the award's screens in the real game, no
// battle): once the field has been calm for two seconds, this game behaves as
// after a lost co-op battle in which its lead knocked out two Lv. 100 Blissey
// (the others standing): the defeat script runs the award, then its message.
static void TestAwardTick(void)
{
    if (gMain.callback2 != CB2_Overworld || ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked()
     || gPaletteFade.active || MultiplayerBattle_IsActive())
    {
        sTestAwardCalmFrames = 0;
        return;
    }
    if (++sTestAwardCalmFrames < 120)
        return;
    sTestAwardArmed = FALSE;
    sCoopExp.count = 0;
    for (sCoopExp.count = 0; sCoopExp.count < 2; sCoopExp.count++)
    {
        sCoopExp.kos[sCoopExp.count].species = SPECIES_BLISSEY;
        sCoopExp.kos[sCoopExp.count].level = 100;
        sCoopExp.kos[sCoopExp.count].faced = 1u << 0;
        sCoopExp.kos[sCoopExp.count].alive = OwnAbleMask();
    }
    sCoopExp.pending = TRUE;
    sPendingGym = MP_COOP_GYM_RUSTBORO;
    sPendingOutcome = B_OUTCOME_LOST;
}

void MultiplayerCoop_TestArmAward(void)
{
    sTestAwardArmed = TRUE;
}

static void TrackCoopBattle(void)
{
    if (sTestAwardArmed)
        TestAwardTick();
    if (!MultiplayerBattle_IsCoop() || !gMain.inBattle || gBattlersCount != MAX_BATTLERS_COUNT)
        return;
    TrackCoopFrame();
}

static void CaptureCoopExpNow(u32 outcome)
{
    u32 side, slot;

    TrackCoopFrame(); // the last frame
    if (outcome == B_OUTCOME_WON)
    {
        // Every opposing Pokemon was defeated. One this game did not see fall
        // counts now, for the Pokemon still standing at the end.
        for (side = 0; side < 2; side++)
        {
            for (slot = 0; slot < PARTY_SIZE; slot++)
            {
                if (sCoopExp.seen[side][slot] && !sCoopExp.fallen[side][slot])
                    RecordKo(side, slot, OwnAbleMask());
            }
        }
    }
    sCoopExp.tracking = FALSE;
    sCoopExp.pending = sCoopExp.count != 0;
}

static void CaptureCoopExp(u32 outcome)
{
    if (MultiplayerBattle_IsCoop())
        CaptureCoopExpNow(outcome);
}

static void ApplyCoopExp(void)
{
    u32 i, k;

    memset(&sCoopAward, 0, sizeof(sCoopAward));
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        enum Species species = GetMonData(mon, MON_DATA_SPECIES);

        if (!IsCoopMon(mon))
            continue;
        sCoopAward.startLevel[i] = GetMonData(mon, MON_DATA_LEVEL);
        for (k = 0; k < sCoopExp.count; k++)
        {
            const struct CoopKo *ko = &sCoopExp.kos[k];
            bool32 faced = (ko->faced >> i) & 1;
            u32 level = GetMonData(mon, MON_DATA_LEVEL), reward, exp, maxExp;

            if (!((ko->alive >> i) & 1))
                continue; // fainted (or an Egg) when it fell: nothing
            if (level >= MAX_LEVEL)
            {
                if (B_MAX_LEVEL_EV_GAINS >= GEN_5)
                    MonGainEVs(mon, ko->species);
                continue;
            }
            reward = MultiplayerCoop_ExpReward(mon, ko->species, ko->level, faced);
            MonGainEVs(mon, ko->species);
            if (reward == 0)
                continue;
            sCoopAward.gained[i] += reward;
            if (faced)
                sCoopAward.participants |= 1u << i;
            else
                sCoopAward.sharers |= 1u << i;
            exp = GetMonData(mon, MON_DATA_EXP) + reward;
            maxExp = gExperienceTables[gSpeciesInfo[species].growthRate][MAX_LEVEL];
            if (exp > maxExp)
                exp = maxExp;
            SetMonData(mon, MON_DATA_EXP, &exp);
            CalculateMonStats(mon);
            for (; level < GetMonData(mon, MON_DATA_LEVEL); level++)
                AdjustFriendship(mon, FRIENDSHIP_EVENT_GROW_LEVEL);
        }
        if (GetMonData(mon, MON_DATA_LEVEL) > sCoopAward.startLevel[i])
            sCoopAward.leveled |= 1u << i;
    }
    sCoopAward.sharers &= ~sCoopAward.participants;
    sCoopExp.count = 0;
}

// Special. VAR_RESULT = TRUE when this game has experience to award (once).
void MultiplayerCoop_ExpBegin(void)
{
    gSpecialVar_Result = FALSE;
    if (!sCoopExp.pending)
        return;
    sCoopExp.pending = FALSE; // consumed here, once
    ApplyCoopExp();
    sCoopAward.stage = COOP_EXP_STAGE_GAINED;
    sCoopAward.slot = 0;
    gSpecialVar_Result = (sCoopAward.participants | sCoopAward.sharers | sCoopAward.leveled) != 0;
}

static void BufferMonName(u32 slot)
{
    GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_NICKNAME, gStringVar1);
    StringGet_Nickname(gStringVar1);
}

static u32 CoopExpMessage(const u8 *text)
{
    StringExpandPlaceholders(gStringVar4, text);
    return MP_COOP_EXP_MESSAGE;
}

static u32 NextCoopExpStep(void)
{
    for (;;)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][sCoopAward.slot < PARTY_SIZE ? sCoopAward.slot : 0];

        switch (sCoopAward.stage)
        {
        case COOP_EXP_STAGE_GAINED:
            // "X gained N Exp. Points!" for each Pokemon that faced a defeated one.
            for (; sCoopAward.slot < PARTY_SIZE; sCoopAward.slot++)
            {
                if (sCoopAward.participants & (1u << sCoopAward.slot))
                {
                    BufferMonName(sCoopAward.slot);
                    ConvertIntToDecimalStringN(gStringVar2, sCoopAward.gained[sCoopAward.slot], STR_CONV_MODE_LEFT_ALIGN, 7);
                    sCoopAward.slot++;
                    return CoopExpMessage(sText_CoopExpGained);
                }
            }
            sCoopAward.stage = COOP_EXP_STAGE_TEAM;
            break;
        case COOP_EXP_STAGE_TEAM:
            sCoopAward.stage = COOP_EXP_STAGE_LEVEL;
            sCoopAward.slot = 0;
            if (sCoopAward.sharers)
                return CoopExpMessage(sText_CoopExpTeam);
            break;
        case COOP_EXP_STAGE_LEVEL:
            for (; sCoopAward.slot < PARTY_SIZE; sCoopAward.slot++)
            {
                if (sCoopAward.leveled & (1u << sCoopAward.slot))
                    break;
            }
            if (sCoopAward.slot >= PARTY_SIZE)
            {
                sCoopAward.stage = COOP_EXP_STAGE_EVOLVE;
                sCoopAward.slot = 0;
                break;
            }
            mon = &gParties[B_TRAINER_PLAYER][sCoopAward.slot];
            BufferMonName(sCoopAward.slot);
            ConvertIntToDecimalStringN(gStringVar2, GetMonData(mon, MON_DATA_LEVEL), STR_CONV_MODE_LEFT_ALIGN, 3);
            sCoopAward.stage = COOP_EXP_STAGE_MOVES;
            sCoopAward.level = sCoopAward.startLevel[sCoopAward.slot] + 1;
            sCoopAward.firstMove = TRUE;
            return CoopExpMessage(sText_CoopExpLevel);
        case COOP_EXP_STAGE_MOVES:
            // The moves of every level it grew through, as the battle teaches them.
            while (sCoopAward.level <= GetMonData(mon, MON_DATA_LEVEL))
            {
                enum Move move = MonTryLearningNewMoveAtLevel(mon, sCoopAward.firstMove, sCoopAward.level);

                sCoopAward.firstMove = FALSE;
                if (move == MOVE_NONE)
                {
                    sCoopAward.level++;
                    sCoopAward.firstMove = TRUE;
                    continue;
                }
                if (move == MON_ALREADY_KNOWS_MOVE)
                    continue;
                BufferMonName(sCoopAward.slot);
                if (move == MON_HAS_MAX_MOVES)
                {
                    sCoopAward.move = gMoveToLearn;
                    StringCopy(gStringVar2, GetMoveName(gMoveToLearn));
                    return MP_COOP_EXP_MOVE_FULL;
                }
                StringCopy(gStringVar2, GetMoveName(move));
                return CoopExpMessage(sText_CoopExpLearned);
            }
            sCoopAward.stage = COOP_EXP_STAGE_LEVEL;
            sCoopAward.slot++;
            break;
        case COOP_EXP_STAGE_EVOLVE:
            // As TryEvolvePokemon after a battle, for the Pokemon that grew.
            for (; sCoopAward.slot < PARTY_SIZE; sCoopAward.slot++)
            {
                u32 slot = sCoopAward.slot;
                bool32 canStopEvo = TRUE;
                enum Species species;

                if (!(sCoopAward.leveled & (1u << slot)) || (sCoopAward.tried & (1u << slot)))
                    continue;
                sCoopAward.tried |= 1u << slot;
                mon = &gParties[B_TRAINER_PLAYER][slot];
                species = GetEvolutionTargetSpecies(mon, EVO_MODE_BATTLE_ONLY, sCoopAward.leveled & ~(1u << slot), NULL, &canStopEvo, CHECK_EVO);
                if (species != SPECIES_NONE)
                {
                    sCoopAward.evolveSpecies = species;
                    sCoopAward.canStopEvo = canStopEvo;
                    sCoopAward.evolveArg = sCoopAward.leveled & ~(1u << slot);
                    sCoopAward.slot++;
                    gSpecialVar_0x8005 = slot;
                    return MP_COOP_EXP_EVOLVE;
                }
            }
            sCoopAward.stage = COOP_EXP_STAGE_DONE;
            break;
        default:
            return MP_COOP_EXP_DONE;
        }
    }
}

// Special. VAR_RESULT = MP_COOP_EXP_*; a message is in gStringVar4.
void MultiplayerCoop_ExpStep(void)
{
    gSpecialVar_Result = NextCoopExpStep();
}

static void Task_CoopExpChooseForget(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        ShowSelectMovePokemonSummaryScreen(gParties[B_TRAINER_PLAYER], sCoopAward.slot, CB2_ReturnToFieldContinueScript, sCoopAward.move);
        DestroyTask(taskId);
    }
}

// Special (waitstate): the summary screen's "forget which move?" for the move
// waiting for a free slot.
void MultiplayerCoop_ExpChooseForget(void)
{
    LockPlayerFieldControls();
    FadeScreen(FADE_TO_BLACK, 0);
    CreateTask(Task_CoopExpChooseForget, 10);
}

static bool32 ReplaceCoopMove(u32 moveSlot)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][sCoopAward.slot];
    enum Move old;

    if (moveSlot >= MAX_MON_MOVES)
        return FALSE;
    old = GetMonData(mon, MON_DATA_MOVE1 + moveSlot);
    if (CannotForgetMove(old))
    {
        StringExpandPlaceholders(gStringVar4, sText_CoopExpHm);
        return FALSE;
    }
    BufferMonName(sCoopAward.slot);
    StringCopy(gStringVar2, GetMoveName(sCoopAward.move));
    StringCopy(gStringVar3, GetMoveName(old));
    RemoveMonPPBonus(mon, moveSlot);
    SetMonMoveSlot(mon, sCoopAward.move, moveSlot);
    StringExpandPlaceholders(gStringVar4, sText_CoopExpForgot);
    return TRUE;
}

// Special. VAR_RESULT = TRUE when a move was forgotten for the new one
// (gStringVar4 says so); FALSE when none was chosen.
void MultiplayerCoop_ExpForgetResult(void)
{
    gStringVar4[0] = EOS;
    gSpecialVar_Result = ReplaceCoopMove(GetMoveSlotToReplace());
}

// Special: gStringVar4 = "X did not learn M."
void MultiplayerCoop_ExpSkipMove(void)
{
    BufferMonName(sCoopAward.slot);
    StringCopy(gStringVar2, GetMoveName(sCoopAward.move));
    StringExpandPlaceholders(gStringVar4, sText_CoopExpNotLearned);
}

static void Task_CoopExpEvolve(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        u32 slot = gSpecialVar_0x8005;
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][slot];
        bool32 canStopEvo = sCoopAward.canStopEvo;

        DestroyTask(taskId);
        CleanupOverworldWindowsAndTilemaps();
        GetEvolutionTargetSpecies(mon, EVO_MODE_BATTLE_ONLY, sCoopAward.evolveArg, NULL, &canStopEvo, DO_EVO);
        gCB2_AfterEvolution = CB2_ReturnToFieldContinueScript;
        EvolutionScene(mon, sCoopAward.evolveSpecies, sCoopAward.canStopEvo, slot);
    }
}

// Special (waitstate): the evolution chosen by MultiplayerCoop_ExpStep
// (VAR_0x8005 = party slot); the scene teaches its own moves.
void MultiplayerCoop_ExpEvolve(void)
{
    LockPlayerFieldControls();
    FadeScreen(FADE_TO_BLACK, 0);
    CreateTask(Task_CoopExpEvolve, 10);
}

// --selftest-coop-exp (src/platform/native_selftest.c) drives these.
void MultiplayerCoop_TestTrackFrame(void)
{
    TrackCoopFrame();
}

void MultiplayerCoop_TestCapture(u32 outcome)
{
    CaptureCoopExpNow(outcome);
}

bool32 MultiplayerCoop_TestForget(u32 moveSlot)
{
    return ReplaceCoopMove(moveSlot);
}

u32 MultiplayerCoop_TestKoCount(void)
{
    return sCoopExp.pending ? sCoopExp.count : 0;
}

void MultiplayerCoop_TestReset(void)
{
    ResetCoopExp();
}
#endif // PORTABLE
