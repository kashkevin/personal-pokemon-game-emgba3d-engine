#include "global.h"
#include "berry.h"
#include "clock.h"
#include "dewford_trend.h"
#include "event_data.h"
#include "field_specials.h"
#include "field_weather.h"
#include "lottery_corner.h"
#include "main.h"
#include "mass_outbreak.h"
#include "overworld.h"
#include "pokerus.h"
#include "random.h"
#include "rtc.h"
#include "time_events.h"
#include "tv.h"
#include "wallclock.h"
#include "constants/form_change_types.h"
#include "apricorn_tree.h"
#if defined(PLATFORM_SDL2)
#include "multiplayer_world.h"
#endif
#line 20 // gba-line

static void UpdatePerDay(struct Time *localTime);
static void UpdatePerMinute(struct Time *localTime);
#if defined(PLATFORM_SDL2)
static void RebaseWorldClockDays(const struct Time *realTime);
#endif
#line 23 // gba-line

void InitTimeBasedEvents(void)
{
    FlagSet(FLAG_SYS_CLOCK_SET);
    RtcCalcLocalTime();
#if defined(PLATFORM_SDL2)
    {
        struct Time realTime;
        RtcCalcRealTime(&realTime);
        gSaveBlock2Ptr->lastBerryTreeUpdate = realTime;
        VarSet(VAR_DAYS, realTime.days);
    }
#else
#line 28 // gba-line
    gSaveBlock2Ptr->lastBerryTreeUpdate = gLocalTime;
    VarSet(VAR_DAYS, gLocalTime.days);
#endif
#line 30 // gba-line
}

void DoTimeBasedEvents(void)
{
    if (FlagGet(FLAG_SYS_CLOCK_SET) && !InPokemonCenter())
    {
#if defined(PLATFORM_SDL2)
        // Visual time (gLocalTime) may run 12x while connected; the daily reset
        // and berry growth follow real time.
        struct Time realTime;
        RtcCalcLocalTime();
        RtcCalcRealTime(&realTime);
        RebaseWorldClockDays(&realTime);
        UpdatePerDay(&realTime);
        UpdatePerMinute(&realTime);
#else
#line 36 // gba-line
        RtcCalcLocalTime();
        UpdatePerDay(&gLocalTime);
        UpdatePerMinute(&gLocalTime);
#endif
#line 39 // gba-line
    }
}

void UpdateDailySeed(void)
{
    gSaveBlock1Ptr->dailySeed = Random32();
}

void DoDailyEvents(u32 daysSince)
{
    ClearDailyFlags();
    UpdateDailySeed();
    UpdateMassOutbreakDaysLeft(daysSince);
    UpdateDewfordTrendPerDay(daysSince);
    UpdateTVShowsPerDay(daysSince);
    UpdateWeatherPerDay(daysSince);
    UpdatePartyPokerusTime(daysSince);
    UpdateBirchState(daysSince);
    UpdateFrontierManiac(daysSince);
    UpdateFrontierGambler(daysSince);
    SetShoalItemFlag(daysSince);
    if (!OW_USE_DAILY_SEED_FOR_VANILLA_VARIABLES)
    {
        UpdateMirageRnd(daysSince);
        SetRandomLotteryNumber(daysSince);
    }
    UpdateDaysPassedSinceFormChange(daysSince);
    DailyResetApricornTrees();
}

static void UpdatePerDay(struct Time *localTime)
{
    u16 *days = GetVarPointer(VAR_DAYS);
    u16 daysSince;

    if (*days != localTime->days && *days <= localTime->days)
    {
        daysSince = localTime->days - *days;
        DoDailyEvents(daysSince);
        *days = localTime->days;
    }
}

static void UpdatePerMinute(struct Time *localTime)
{
    struct Time difference;
    int minutes;

    CalcTimeDifference(&difference, &gSaveBlock2Ptr->lastBerryTreeUpdate, localTime);
    minutes = 24 * 60 * difference.days + 60 * difference.hours + difference.minutes;
    if (minutes != 0)
    {
        if (minutes >= 0)
        {
            BerryTreeTimeUpdate(minutes);
            gSaveBlock2Ptr->lastBerryTreeUpdate = *localTime;
        }
    }
}

void FormChangeTimeUpdate()
{
    s32 i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        TryFormChange(&gParties[B_TRAINER_PLAYER][i], FORM_CHANGE_TIME_OF_DAY, B_TRAINER_PLAYER);
    }
}

static void ReturnFromStartWallClock(void)
{
    InitTimeBasedEvents();
    SetMainCallback2(CB2_ReturnToFieldContinueScriptPlayMapMusic);
}

void StartWallClock(void)
{
    SetMainCallback2(CB2_StartWallClock);
    gMain.savedCallback = ReturnFromStartWallClock;
}

#if defined(PLATFORM_SDL2)
// Saves played while connected before Phase 11 stored the 12x world clock's
// days (MP_WORLD_DAY_ORIGIN and later) in VAR_DAYS and the berry clock. Real
// time (days since the game's clock was set) is far behind them, so the daily
// reset and berry growth would wait for years (KNOWN_ISSUES G11). Such values
// come back to real time once; the days in between are not replayed.
static void RebaseWorldClockDays(const struct Time *realTime)
{
    u16 *days = GetVarPointer(VAR_DAYS);
    struct Time *berries = &gSaveBlock2Ptr->lastBerryTreeUpdate;

    if (*days >= MP_WORLD_DAY_ORIGIN && *days > realTime->days)
        *days = realTime->days;
    if (berries->days >= MP_WORLD_DAY_ORIGIN && berries->days > realTime->days)
        *berries = *realTime;
}
#endif
