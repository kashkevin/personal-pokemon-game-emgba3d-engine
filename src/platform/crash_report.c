// Native PC build: crash report.
//
// When the game crashes (an access violation, illegal instruction, abort...), this
// writes crash.log in the working directory before the process dies: the fault,
// the faulting instruction address, the game's current callbacks, and a stack
// trace. Every native build keeps frame pointers (Makefile_pc), so the trace
// follows the frame chain; it falls back to scanning the stack for code
// addresses. Symbolise the addresses with
//     addr2line -f -i -e pokeemerald-mp(.exe) ADDRESS...
// using the exact binary that crashed.
//
// Crashes here are usually GBA-tolerated behaviour (see KNOWN_ISSUES.md N9-N11):
// a report is the starting point for a source fix. It does not try to recover.
#ifndef _WIN32
#define _GNU_SOURCE // REG_EIP and friends in <ucontext.h>
#endif
// System headers first: global.h defines an abs() macro that breaks <stdlib.h>.
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#include <pthread.h>
#endif

#include "global.h"
#include "main.h"
#include "platform/crash_report.h"
#include "platform/memory_info.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_screen.h"
#endif

#define MAX_FRAMES 48
#define CRUMBS 256          /* a power of two */
#define MAX_ERRORS_LOGGED 32 /* errors.log lines a run writes at most */
#ifndef PKMN_ENGINE_COMMIT
#define PKMN_ENGINE_COMMIT "unknown"
#endif

/*
 * Breadcrumbs (MS-71: two crashes mid-battle with nothing to go on): what the game did last, written into
 * crash.log - the battle link's commands and blocks, the effects core's animations, screen changes and memory.
 */
struct Crumb
{
    uint32_t frame;
    uint32_t a;
    uint32_t b;
    uint8_t kind;
};
static struct Crumb sCrumbs[CRUMBS];
static volatile uint32_t sCrumbNext;
static int sErrorsLogged;

/* PKMN_LINK_TRACE set: every breadcrumb also goes to stderr with the time (the machine's monotonic clock, shared by
 * every process on it), for the multiplayer battle latency measurements (tools/pc_port/link_latency.py). */
static double MonotonicMs(void)
{
#ifdef _WIN32
    LARGE_INTEGER now, frequency;

    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1e6;
#endif
}

void CrashReport_Crumb(uint8_t kind, uint32_t a, uint32_t b)
{
    static int sTrace = -1;
    uint32_t i = __atomic_fetch_add(&sCrumbNext, 1, __ATOMIC_RELAXED) & (CRUMBS - 1);

    sCrumbs[i].kind = 0; /* a torn entry reads as empty */
    sCrumbs[i].frame = gMain.vblankCounter1;
    sCrumbs[i].a = a;
    sCrumbs[i].b = b;
    sCrumbs[i].kind = kind;
    if (sTrace < 0)
        sTrace = getenv("PKMN_LINK_TRACE") != NULL;
    if (sTrace)
        fprintf(stderr, "link-trace %.2f %u %u %u %u\n", MonotonicMs(), (unsigned)gMain.vblankCounter1, kind,
                (unsigned)a, (unsigned)b);
}

void CrashReport_FrameBoundary(void)
{
    static MainCallback sCallback2;
    static unsigned sFrames;
#ifdef ENABLE_NATIVE_UI
    static const char *sScreen;

    if (NativeUi_ScreenNow() != sScreen)
    {
        sScreen = NativeUi_ScreenNow();
        CrashReport_Crumb(CRUMB_SCREEN, (uint32_t)(uintptr_t)sScreen, 0);
    }
#endif
    if (gMain.callback2 != sCallback2)
    {
        sCallback2 = gMain.callback2;
        CrashReport_Crumb(CRUMB_CALLBACK2, (uint32_t)(uintptr_t)sCallback2, 0);
    }
    if (++sFrames % 600 == 0)
    {
        struct MemoryInfo info;

        if (MemoryInfo_Get(&info))
            CrashReport_Crumb(CRUMB_MEMORY, (uint32_t)(info.processResident >> 20), (uint32_t)(info.addressSpaceAvailable >> 20));
    }
}

static const char *CrumbName(uint8_t kind)
{
    static const char *const sNames[] = {
        [CRUMB_CALLBACK2] = "callback2", [CRUMB_SCREEN] = "screen", [CRUMB_LINK_COMMAND] = "link command",
        [CRUMB_LINK_SEND] = "link send", [CRUMB_LINK_DELIVER] = "link deliver", [CRUMB_EFFECT] = "animation",
        [CRUMB_MEMORY] = "memory MB", [CRUMB_ERROR] = "assert", [CRUMB_CHOICE] = "battle choice",
        [CRUMB_MESSAGE] = "battle text", [CRUMB_KEY] = "battle key", [CRUMB_HEALTHBOX_STATUS] = "box status",
        [CRUMB_HUD_BOX] = "HUD box", [CRUMB_NATIVE_FAIL] = "battle core stopped",
    };

    return kind < ARRAY_COUNT(sNames) && sNames[kind] != NULL ? sNames[kind] : "?";
}

static void WriteCrumbs(FILE *log, unsigned count)
{
    uint32_t next = sCrumbNext;

    if (count > CRUMBS)
        count = CRUMBS;
    fprintf(log, "last events (oldest first; frame, what, values):\n");
    for (uint32_t n = next > count ? next - count : 0; n < next; n++)
    {
        const struct Crumb *c = &sCrumbs[n & (CRUMBS - 1)];

        if (c->kind == 0)
            continue;
        if (c->kind == CRUMB_SCREEN)
            fprintf(log, "  %8u %-13s %s\n", (unsigned)c->frame, CrumbName(c->kind),
                    c->a != 0 ? (const char *)(uintptr_t)c->a : "(none)");
        else
            fprintf(log, "  %8u %-13s %lu %lu (0x%lx)\n", (unsigned)c->frame, CrumbName(c->kind), (unsigned long)c->a,
                    (unsigned long)c->b, (unsigned long)c->a);
    }
}

#ifndef _WIN32
// Program text bounds from the GNU linker.
extern char __executable_start[];
extern char etext[];
#endif

static uintptr_t sTextStart, sTextEnd;

static bool32 IsCodeAddress(uintptr_t address)
{
    return sTextStart != 0 && address >= sTextStart && address < sTextEnd;
}

static void WriteGameState(FILE *log)
{
    // Plain reads of game memory: the game thread is stopped in the fault (or the
    // main thread crashed between frames).
    fprintf(log, "gMain.callback1 = %p\n", (void *)(uintptr_t)gMain.callback1);
    fprintf(log, "gMain.callback2 = %p\n", (void *)(uintptr_t)gMain.callback2);
    fprintf(log, "gMain.vblankCallback = %p\n", (void *)(uintptr_t)gMain.vblankCallback);
    fprintf(log, "gMain.state = %u\n", (unsigned)gMain.state);
#ifdef ENABLE_NATIVE_UI
    {
        // The native screen in front when it crashed (a constant name; NULL: none).
        const char *screen = NativeUi_ScreenNow();

        fprintf(log, "native screen = %s\n", screen != NULL ? screen : "(none)");
    }
#endif
}

// Follows saved frame pointers (EBP chain) from the faulting frame.
static int WalkFrames(FILE *log, uintptr_t ebp, uintptr_t stackLow, uintptr_t stackHigh)
{
    int frames = 0;

    while (frames < MAX_FRAMES && ebp >= stackLow && ebp + 8 <= stackHigh && (ebp & 3) == 0)
    {
        uintptr_t next = ((uintptr_t *)ebp)[0];
        uintptr_t ret = ((uintptr_t *)ebp)[1];

        if (!IsCodeAddress(ret))
            break;
        fprintf(log, "  frame %2d: %p\n", frames, (void *)ret);
        frames++;
        if (next <= ebp)
            break;
        ebp = next;
    }
    return frames;
}

// Fallback: lists stack words that point into the program's code.
static void ScanStack(FILE *log, uintptr_t esp, uintptr_t stackHigh)
{
    int found = 0;

    fprintf(log, "stack scan (candidate return addresses, may include stale ones):\n");
    for (uintptr_t p = esp & ~(uintptr_t)3; p + 4 <= stackHigh && p < esp + 16384 && found < MAX_FRAMES; p += 4)
    {
        uintptr_t value = *(uintptr_t *)p;
        if (IsCodeAddress(value))
        {
            fprintf(log, "  [esp+%04x] %p\n", (unsigned)(p - esp), (void *)value);
            found++;
        }
    }
}

static FILE *OpenLog(void)
{
    FILE *log = fopen("crash.log", "w");
    time_t now = time(NULL);

    if (log == NULL)
        log = stderr;
    else
        setvbuf(log, NULL, _IONBF, 0); /* each line on disk at once: what was written survives a fault in here */
    fprintf(log, "pokeemerald-mp crash report, %s", ctime(&now));
    fprintf(log, "engine %s\n", PKMN_ENGINE_COMMIT);
#ifdef _WIN32
    {
        // Windows moves the exe (ASLR): addresses below are symbolised as (address - base + 0x400000)
        HMODULE module = GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)module + ((IMAGE_DOS_HEADER *)module)->e_lfanew);

        fprintf(log, "module base %p, link time stamp %08lx\n", (void *)module, (unsigned long)nt->FileHeader.TimeDateStamp);
    }
#endif
    return log;
}

static void CloseLog(FILE *log)
{
    WriteCrumbs(log, CRUMBS);
    fprintf(log, "Symbolise: addr2line -f -i -e <this binary> <addresses>\n");
    if (log != stderr)
    {
        fclose(log);
        fputs("The game crashed; details were written to crash.log\n", stderr);
    }
}

#ifdef _WIN32

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info)
{
    FILE *log = OpenLog();
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    CONTEXT *context = info->ContextRecord;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();

    fprintf(log, "exception 0x%08lx at %p", (unsigned long)record->ExceptionCode, record->ExceptionAddress);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        fprintf(log, " (%s address %p)", record->ExceptionInformation[0] ? "writing" : "reading",
                (void *)record->ExceptionInformation[1]);
    fprintf(log, "\nthread %lu\n", (unsigned long)GetCurrentThreadId());
    fprintf(log, "eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n",
            context->Eip, context->Esp, context->Ebp, context->Eax, context->Ebx, context->Ecx,
            context->Edx, context->Esi, context->Edi);
    WriteGameState(log);
    fprintf(log, "frames:\n  frame  -: %p (faulting instruction)\n", (void *)(uintptr_t)context->Eip);
    if (WalkFrames(log, context->Ebp, (uintptr_t)tib->StackLimit, (uintptr_t)tib->StackBase) < 2)
        ScanStack(log, context->Esp, (uintptr_t)tib->StackBase);
    CloseLog(log);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The report from inside the game: abort() (the C library, the effects core's QuickJS, the game heap's checks)
// or a fatal error. The stack is the caller's.
static void ReportHere(const char *what, const char *message)
{
    FILE *log = OpenLog();
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t ebp = (uintptr_t)__builtin_frame_address(0);

    fprintf(log, "%s\nthread %lu\n", what, (unsigned long)GetCurrentThreadId());
    if (message != NULL)
        fprintf(log, "message: %s\n", message);
    WriteGameState(log);
    fprintf(log, "frames:\n");
    if (WalkFrames(log, ebp, (uintptr_t)tib->StackLimit, (uintptr_t)tib->StackBase) < 2)
        ScanStack(log, ebp, (uintptr_t)tib->StackBase);
    CloseLog(log);
}

static void AbortSignal(int sig)
{
    (void)sig;
    ReportHere("abort() (SIGABRT): a check failed, or the C library or the effects core gave up", NULL);
    // abort() goes on to end the process (exit code 3)
}

void CrashReport_ThreadStarted(void)
{
    // Room for the filter to run when this thread's stack overflows (kernel32, Vista on).
    typedef BOOL(WINAPI *GuaranteeFn)(PULONG);
    GuaranteeFn guarantee = (GuaranteeFn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadStackGuarantee");
    ULONG size = 64 * 1024;

    if (guarantee != NULL)
        guarantee(&size);
}

void CrashReport_Reinstall(void)
{
    SetUnhandledExceptionFilter(CrashFilter);
    signal(SIGABRT, AbortSignal);
}

void CrashReport_Init(void)
{
    HMODULE module = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)module + dos->e_lfanew);

    sTextStart = (uintptr_t)module + nt->OptionalHeader.BaseOfCode;
    sTextEnd = sTextStart + nt->OptionalHeader.SizeOfCode;
    SetUnhandledExceptionFilter(CrashFilter);
    signal(SIGABRT, AbortSignal);
    CrashReport_ThreadStarted();
}

#else

// The top of the faulting thread's stack: the walk and the scan read no further (reading past it faulted again
// inside this handler, and the report was lost: the main thread's stack ends a few KiB above its frames).
static uintptr_t StackHigh(uintptr_t sp)
{
    pthread_attr_t attr;
    void *addr;
    size_t size;

    if (pthread_getattr_np(pthread_self(), &attr) == 0)
    {
        int got = pthread_attr_getstack(&attr, &addr, &size);

        pthread_attr_destroy(&attr);
        if (got == 0 && sp >= (uintptr_t)addr && sp < (uintptr_t)addr + size)
            return (uintptr_t)addr + size;
    }
    return (sp | 4095) + 1; // unknown: the end of the current page only
}

static void CrashSignal(int sig, siginfo_t *info, void *ucontextRaw)
{
    ucontext_t *uc = ucontextRaw;
    FILE *log = OpenLog();
    uintptr_t eip = uc->uc_mcontext.gregs[REG_EIP];
    uintptr_t esp = uc->uc_mcontext.gregs[REG_ESP];
    uintptr_t ebp = uc->uc_mcontext.gregs[REG_EBP];

    fprintf(log, "signal %d (%s) at %p, fault address %p\n", sig, strsignal(sig), (void *)eip, info->si_addr);
    WriteGameState(log);
    fprintf(log, "frames:\n  frame  -: %p (faulting instruction)\n", (void *)eip);
    uintptr_t limit = StackHigh(esp);

    fprintf(log, "esp=%08lx ebp=%08lx\n", (unsigned long)esp, (unsigned long)ebp);
    if (WalkFrames(log, ebp, esp, limit) < 2)
        ScanStack(log, esp, limit);
    CloseLog(log);

    // Re-raise with the default action so the process still dies (and dumps core).
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashReport_Init(void)
{
    struct sigaction action;
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

    sTextStart = (uintptr_t)__executable_start;
    sTextEnd = (uintptr_t)etext;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = CrashSignal;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (unsigned i = 0; i < ARRAY_COUNT(signals); i++)
    {
        // Leave handlers that other development aids installed first
        // (fault_compat's SIGFPE, the NULL trap's SIGSEGV).
        struct sigaction current;
        if (sigaction(signals[i], NULL, &current) == 0 && current.sa_handler == SIG_DFL)
            sigaction(signals[i], &action, NULL);
    }
}

static void ReportHere(const char *what, const char *message)
{
    FILE *log = OpenLog();
    uintptr_t ebp = (uintptr_t)__builtin_frame_address(0);
    uintptr_t limit = StackHigh(ebp);

    fprintf(log, "%s\n", what);
    if (message != NULL)
        fprintf(log, "message: %s\n", message);
    WriteGameState(log);
    fprintf(log, "frames:\n");
    WalkFrames(log, ebp, ebp, limit);
    CloseLog(log);
}

void CrashReport_ThreadStarted(void)
{
}

void CrashReport_Reinstall(void)
{
}

#endif

void CrashReport_Error(const char *message)
{
    fprintf(stderr, "[game ERROR] assertion failed, the game goes on: %s\n", message);
    CrashReport_Crumb(CRUMB_ERROR, (uint32_t)++sErrorsLogged, 0);
    if (sErrorsLogged <= MAX_ERRORS_LOGGED)
    {
        FILE *log = fopen("errors.log", "a");
        time_t now = time(NULL);

        if (log != NULL)
        {
            fprintf(log, "%.24s engine %s: %s\n", ctime(&now), PKMN_ENGINE_COMMIT, message);
            WriteCrumbs(log, 24);
            fclose(log);
        }
    }
}

_Noreturn void CrashReport_Fatal(const char *message)
{
    fprintf(stderr, "[game ERROR] fatal error: %s\n", message);
    ReportHere("fatal error (the game cannot go on)", message);
    fflush(NULL);
    _Exit(70);
}
