// Native PC build: replacement for src/siirtc.c (the Seiko S-3511 real-time
// clock on the cartridge GPIO port). Date and time come from the host clock;
// the game's own clock handling (src/rtc.c, including the multiplayer world
// clock) sits on top of this unchanged.
#include <stdlib.h> // before global.h, whose abs() macro stdlib.h's declaration would meet
#include "global.h"
#include "siirtc.h"

#include <time.h>

static bool8 sLocked;

static u8 sStatus = SIIRTCINFO_24HOUR;

static u8 ToBcd(u32 value)
{
    return ((value / 10) << 4) | (value % 10);
}

static int sStartHour = -1;

// The launcher's character setup (--setup, sdl2.c): a new world's day starts at 12:00 noon (the owner, 2026-10-17; the
// host's world clock starts there too, app balance.py), so the New Game played to make the character does as well. Only
// this run's clock: the introduction sets the save's clock to the time it shows (offset 0 from the clock it read), so the
// character's own clock offline is the PC's as always.
void SiiRtcPc_StartAtHour(int hour)
{
    sStartHour = hour >= 0 && hour <= 23 ? hour : -1;
}

// PKMN_CLOCK_HOUR=H (0-23; development and the benchmark's night scene): the
// clock reads so that the game's own time is H:00 when it is first read - the
// save's clock setting accounted for - and runs on from there as usual.
// Unset (the player's game): the host clock as it is (character setup: SiiRtcPc_StartAtHour).
static int ClockHourOverride(void)
{
    static int sHour = -2;

    if (sHour == -2)
    {
        const char *value = getenv("PKMN_CLOCK_HOUR");

        sHour = value != NULL && value[0] >= '0' && value[0] <= '9' ? atoi(value) : -1;
        if (sHour > 23)
            sHour = -1;
        if (sHour < 0)
            sHour = sStartHour;
    }
    return sHour;
}

static void OverrideHour(struct tm *local, time_t now)
{
    static time_t sFirst;
    long minuteOfDay;

    if (sFirst == 0)
        sFirst = now;
    minuteOfDay = ClockHourOverride() * 60L + (long)(now - sFirst) / 60;
    // The game's time is the clock's less the save's offset (rtc.c). The clock is
    // first read at boot (RtcInit), before the save blocks exist: no offset then.
    if (gSaveBlock2Ptr != NULL)
        minuteOfDay += gSaveBlock2Ptr->localTimeOffset.hours * 60L + gSaveBlock2Ptr->localTimeOffset.minutes;
    minuteOfDay = ((minuteOfDay % 1440) + 1440) % 1440;
    local->tm_hour = (int)(minuteOfDay / 60);
    local->tm_min = (int)(minuteOfDay % 60);
}

static void ReadHostClock(struct SiiRtcInfo *rtc, bool8 withDate)
{
    time_t now = time(NULL);
    struct tm local;

#ifdef _WIN32
    local = *localtime(&now); // only the game thread reads the clock
#else
    localtime_r(&now, &local);
#endif
    if (ClockHourOverride() >= 0)
        OverrideHour(&local, now);
    if (withDate)
    {
        // The cartridge clock stores a two-digit year (2000-2099).
        rtc->year = ToBcd((u32)(local.tm_year + 1900 - 2000) % 100);
        rtc->month = ToBcd(local.tm_mon + 1);
        rtc->day = ToBcd(local.tm_mday);
        rtc->dayOfWeek = ToBcd(local.tm_wday);
    }
    rtc->hour = ToBcd(local.tm_hour);
    rtc->minute = ToBcd(local.tm_min);
    rtc->second = ToBcd(local.tm_sec > 59 ? 59 : local.tm_sec);
}

void SiiRtcUnprotect(void)
{
    sLocked = FALSE;
}

void SiiRtcProtect(void)
{
    sLocked = TRUE;
}

u8 SiiRtcProbe(void)
{
    // A healthy clock in 24-hour mode: no reset needed (errorCode 0), present (1).
    return 1;
}

bool8 SiiRtcReset(void)
{
    if (sLocked == TRUE)
        return FALSE;
    sStatus = SIIRTCINFO_24HOUR;
    return TRUE;
}

bool8 SiiRtcGetStatus(struct SiiRtcInfo *rtc)
{
    if (sLocked == TRUE)
        return FALSE;
    rtc->status = sStatus;
    return TRUE;
}

bool8 SiiRtcSetStatus(struct SiiRtcInfo *rtc)
{
    if (sLocked == TRUE)
        return FALSE;
    sStatus = SIIRTCINFO_24HOUR | (rtc->status & (SIIRTCINFO_INTFE | SIIRTCINFO_INTME | SIIRTCINFO_INTAE));
    return TRUE;
}

bool8 SiiRtcGetDateTime(struct SiiRtcInfo *rtc)
{
    if (sLocked == TRUE)
        return FALSE;
    ReadHostClock(rtc, TRUE);
    return TRUE;
}

bool8 SiiRtcGetTime(struct SiiRtcInfo *rtc)
{
    if (sLocked == TRUE)
        return FALSE;
    ReadHostClock(rtc, FALSE);
    return TRUE;
}
