// Development tool for the native PC build (Linux i386 only).
//
// On the GBA, reading through a NULL pointer returns BIOS bytes and writing is
// ignored, so the game (and pokeemerald-expansion) contains NULL dereferences
// that are harmless there but crash natively. With PKMN_NULL_TRAP=1 the first
// 64 KiB of the address space is reserved; every access there is logged with
// the faulting instruction address, allowed to complete against a zero-filled
// page, and the page is re-protected after one single-stepped instruction.
// The log (null_trap.log) lists each distinct site once with a hit count, so a
// play session finds every such site without crashing. Symbolize with
//   addr2line -f -e pokeemerald-mp <address>
// Requires vm.mmap_min_addr=0. Never enabled in release builds.
#if defined(NATIVE_LINUX) && defined(__i386__)
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <ucontext.h>

#define TRAP_SIZE 0x10000
#define MAX_SITES 512
#define EFLAGS_TF 0x100

static struct { unsigned eip; unsigned addr; unsigned count; } sSites[MAX_SITES];
static int sSiteCount;
static int sEnabled;
static FILE *sLog;

static void Protect(int prot)
{
    extern volatile uintptr_t gNullTrapBase;
    mprotect((void *)gNullTrapBase, TRAP_SIZE, prot);
}

static void RecordSite(unsigned eip, unsigned addr)
{
    int i;
    for (i = 0; i < sSiteCount; i++)
    {
        if (sSites[i].eip == eip)
        {
            sSites[i].count++;
            return;
        }
    }
    if (sSiteCount < MAX_SITES)
    {
        sSites[sSiteCount].eip = eip;
        sSites[sSiteCount].addr = addr;
        sSites[sSiteCount].count = 1;
        sSiteCount++;
        if (sLog != NULL)
        {
            fprintf(sLog, "NULL access at eip=0x%08x addr=0x%04x\n", eip, addr);
            fflush(sLog);
        }
    }
}

static void OnSegv(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
    unsigned addr = (unsigned)(uintptr_t)info->si_addr;

    if (!sEnabled || addr >= TRAP_SIZE)
    {
        signal(SIGSEGV, SIG_DFL); // a real crash: let it happen
        return;
    }
    RecordSite(uc->uc_mcontext.gregs[REG_EIP], addr);
    Protect(PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= EFLAGS_TF; // re-protect after this instruction
}

volatile uintptr_t gNullTrapBase; // 0, hidden from the optimiser
#define sTrapBase gNullTrapBase

static void OnTrap(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;

    memset((void *)sTrapBase, 0, TRAP_SIZE); // writes through NULL are discarded
    Protect(PROT_NONE);
    uc->uc_mcontext.gregs[REG_EFL] &= ~EFLAGS_TF;
}

static void PrintSummary(void)
{
    int i;
    if (sLog == NULL)
        return;
    fprintf(sLog, "--- %d distinct NULL access site(s)\n", sSiteCount);
    for (i = 0; i < sSiteCount; i++)
        fprintf(sLog, "0x%08x x%u (addr 0x%04x)\n", sSites[i].eip, sSites[i].count, sSites[i].addr);
    fclose(sLog);
    sLog = NULL;
}

void NullTrap_Init(void)
{
    struct sigaction sa;
    const char *env = getenv("PKMN_NULL_TRAP");

    if (env == NULL || env[0] != '1')
        return;
    if (mmap((void *)0, TRAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != (void *)0)
    {
        fprintf(stderr, "null trap: cannot map page 0 (set vm.mmap_min_addr=0)\n");
        return;
    }
    sLog = fopen("null_trap.log", "w");
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = OnSegv;
    sigaction(SIGSEGV, &sa, NULL);
    sa.sa_sigaction = OnTrap;
    sigaction(SIGTRAP, &sa, NULL);
    atexit(PrintSummary);
    sEnabled = 1;
    fprintf(stderr, "null trap: enabled, logging to null_trap.log\n");
}
#else
void NullTrap_Init(void)
{
}
#endif
