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
    if (!MultiplayerCoop_IsValidGym(gym))
        return;
    sPendingGym = gym;
    sPendingOutcome = outcome;
}

// After the native battle has fully released this game back to a calm field,
// run this player's own reward (or consolation) script once.
void MultiplayerCoop_Tick(void)
{
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
