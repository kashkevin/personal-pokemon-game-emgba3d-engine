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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#endif

#include "global.h"
#include "main.h"

#define MAX_FRAMES 48

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
    fprintf(log, "pokeemerald-mp crash report, %s", ctime(&now));
    return log;
}

static void CloseLog(FILE *log)
{
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

void CrashReport_Init(void)
{
    HMODULE module = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)module + dos->e_lfanew);

    sTextStart = (uintptr_t)module + nt->OptionalHeader.BaseOfCode;
    sTextEnd = sTextStart + nt->OptionalHeader.SizeOfCode;
    SetUnhandledExceptionFilter(CrashFilter);
}

#else

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
    // Thread stacks are at most a few MiB; bound the walk to 8 MiB above esp
    // (saturating: the main thread's stack sits just below 4 GiB).
    uintptr_t limit = esp + (8u << 20) < esp ? UINTPTR_MAX - 8 : esp + (8u << 20);
    fprintf(log, "esp=%08lx ebp=%08lx\n", (unsigned long)esp, (unsigned long)ebp);
    if (WalkFrames(log, ebp, esp, limit) < 2)
        ScanStack(log, esp, esp + 16384 < esp ? UINTPTR_MAX - 4 : esp + 16384);
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

#endif
