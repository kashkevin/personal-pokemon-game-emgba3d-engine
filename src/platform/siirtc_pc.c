// Native PC build: replacement for src/siirtc.c (the Seiko S-3511 real-time
// clock on the cartridge GPIO port). Date and time come from the host clock;
// the game's own clock handling (src/rtc.c, including the multiplayer world
// clock) sits on top of this unchanged.
#include "global.h"
#include "siirtc.h"

#include <time.h>

static bool8 sLocked;
static u8 sStatus = SIIRTCINFO_24HOUR;

static u8 ToBcd(u32 value)
{
    return ((value / 10) << 4) | (value % 10);
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
