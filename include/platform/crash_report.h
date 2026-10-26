#ifndef GUARD_PLATFORM_CRASH_REPORT_H
#define GUARD_PLATFORM_CRASH_REPORT_H

/*
 * The native PC build's crash report (src/platform/crash_report.c): crash.log in the working directory (the
 * launcher's launch folder) when the game crashes, aborts or stops on a fatal error, with the game's state, a
 * stack trace and the breadcrumbs below. Project-owned.
 */
#include <stdint.h>

/* What a breadcrumb records (its two numbers in brackets). */
enum CrashCrumb
{
    CRUMB_CALLBACK2 = 1, /* the game's main callback changed (the new one's address, 0) */
    CRUMB_SCREEN,        /* the native screen in front changed (its name's address, 0) */
    CRUMB_LINK_COMMAND,  /* a battle controller command arrived over the link (battler, command id) */
    CRUMB_LINK_SEND,     /* a game sent a battle link block (size, its first byte) */
    CRUMB_LINK_DELIVER,  /* the host delivered battle link blocks to this game (count, acknowledged) */
    CRUMB_EFFECT,        /* a battle animation started in the effects core (type, id) */
    CRUMB_MEMORY,        /* the process (resident MB, free address space MB) */
    CRUMB_ERROR,         /* an assertion failed and the game went on (count so far, 0) */
    CRUMB_CHOICE,        /* the battle HUD: the player controller's choice changed (enum NativeBattleInput now, before) */
    CRUMB_MESSAGE,       /* the battle HUD: the message window (1 a new text printing, 2 waiting for a button, 3 done;
                          * whether the battle is a link battle) */
    CRUMB_KEY,           /* the battle HUD: A or B newly pressed (the keys, 0) */
    CRUMB_HEALTHBOX_STATUS, /* a healthbox drew its status icon (battler, the party Pokemon's status it drew) */
    CRUMB_HUD_BOX,       /* the battle HUD's box shows something else (battler, species << 8 | enum UiAilment) */
};

void CrashReport_Init(void);
/* Again after the window and OpenGL are up (a driver may have put its own handler in). */
void CrashReport_Reinstall(void);
/* At the start of a thread of the game's own (room on its stack for the report of a stack overflow). */
void CrashReport_ThreadStarted(void);
/* At each frame boundary (the game thread parked): breadcrumbs for the game's main callback and the native screen
 * when they change, and the process's memory every ten seconds. */
void CrashReport_FrameBoundary(void);
/* A breadcrumb: the last 256 are written into crash.log. Cheap; any thread. With PKMN_LINK_TRACE set, each also goes
 * to stderr with the time ("link-trace MS FRAME KIND A B"). */
void CrashReport_Crumb(uint8_t kind, uint32_t a, uint32_t b);
/* A failed assertion the game recovers from: one line to stderr (runtime.log) and errors.log. */
void CrashReport_Error(const char *message);
/* A fatal error: crash.log with the message, then the process ends (exit code 70) instead of hanging. */
_Noreturn void CrashReport_Fatal(const char *message);

#endif
