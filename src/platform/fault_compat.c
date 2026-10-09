// Native PC build: GBA-compatible integer division faults.
//
// The GBA has no divide instruction. GCC calls libgcc helpers that return a
// value when dividing by zero instead of trapping:
//   signed   x/0, x%0 -> INT_MAX if x > 0, INT_MIN if x < 0, 0 if x == 0
//   unsigned x/0, x%0 -> 0xFFFFFFFF if x != 0, else 0
//   INT_MIN / -1      -> INT_MIN, remainder 0
// Some game code divides by zero in rare cases and relies on this. On x86 the
// div/idiv instruction raises #DE instead, so this module catches the fault,
// decodes the faulting div/idiv, writes the GBA result to the destination
// registers and resumes after the instruction. Each distinct site is logged once
// so it can be fixed in the source. Any other fault is left alone.
#if defined(NATIVE_LINUX) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // REG_EAX and friends in <ucontext.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__i386__) && (defined(_WIN32) || defined(NATIVE_LINUX))

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <ucontext.h>
#endif

struct Regs
{
    uint32_t *gpr[8]; // eax, ecx, edx, ebx, esp, ebp, esi, edi
    uint32_t *eip;
};

#define MAX_LOGGED_SITES 64
static uint32_t sLoggedSites[MAX_LOGGED_SITES];
static int sLoggedCount;

static void LogSite(uint32_t eip, const char *what)
{
    int i;
    for (i = 0; i < sLoggedCount; i++)
        if (sLoggedSites[i] == eip)
            return;
    if (sLoggedCount < MAX_LOGGED_SITES)
        sLoggedSites[sLoggedCount++] = eip;
    fprintf(stderr, "[compat] %s at 0x%08x (GBA result substituted)\n", what, (unsigned)eip);
    fflush(stderr);
}

// Decodes the ModRM operand of a div/idiv. Returns the instruction length, or 0
// if the instruction is not one this module handles.
static int DecodeDivision(const uint8_t *code, const struct Regs *regs, int *isSigned, int *width, uint32_t *divisor)
{
    int len = 0;
    int opsize16 = 0;
    uint8_t opcode, modrm, mod, reg, rm;
    uint32_t address = 0;

    for (;;)
    {
        uint8_t b = code[len];
        if (b == 0x66)
            opsize16 = 1;
        else if (b != 0x26 && b != 0x2E && b != 0x36 && b != 0x3E && b != 0x64 && b != 0x65)
            break;
        if (++len > 4)
            return 0;
    }
    opcode = code[len++];
    if (opcode != 0xF6 && opcode != 0xF7)
        return 0;
    modrm = code[len++];
    mod = modrm >> 6;
    reg = (modrm >> 3) & 7;
    rm = modrm & 7;
    if (reg != 6 && reg != 7)
        return 0;
    *isSigned = (reg == 7);
    *width = (opcode == 0xF6) ? 8 : (opsize16 ? 16 : 32);

    if (mod == 3)
    {
        uint32_t value = *regs->gpr[rm];
        if (*width == 8)
            value = (rm < 4) ? (value & 0xFF) : ((*regs->gpr[rm - 4] >> 8) & 0xFF);
        *divisor = value;
        return len;
    }
    if (rm == 4)
    {
        uint8_t sib = code[len++];
        uint8_t scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
        if (index != 4)
            address += *regs->gpr[index] << scale;
        if (base == 5 && mod == 0)
        {
            address += *(const uint32_t *)&code[len];
            len += 4;
        }
        else
        {
            address += *regs->gpr[base];
        }
    }
    else if (rm == 5 && mod == 0)
    {
        address = *(const uint32_t *)&code[len];
        len += 4;
    }
    else
    {
        address = *regs->gpr[rm];
    }
    if (mod == 1)
    {
        address += (int8_t)code[len];
        len += 1;
    }
    else if (mod == 2)
    {
        address += *(const uint32_t *)&code[len];
        len += 4;
    }
    if (*width == 8)
        *divisor = *(const uint8_t *)(uintptr_t)address;
    else if (*width == 16)
        *divisor = *(const uint16_t *)(uintptr_t)address;
    else
        *divisor = *(const uint32_t *)(uintptr_t)address;
    return len;
}

static int32_t SignedZeroResult(int64_t dividend)
{
    return dividend > 0 ? INT32_MAX : (dividend < 0 ? INT32_MIN : 0);
}

// Returns 1 if the fault was a division this module handled.
static int HandleDivisionFault(struct Regs *regs)
{
    const uint8_t *code = (const uint8_t *)(uintptr_t)*regs->eip;
    int isSigned, width;
    uint32_t divisor;
    int len = DecodeDivision(code, regs, &isSigned, &width, &divisor);
    uint32_t *eax = regs->gpr[0], *edx = regs->gpr[2];

    if (len == 0)
        return 0;

    if (width == 32)
    {
        if (isSigned)
        {
            int64_t dividend = (int64_t)(((uint64_t)*edx << 32) | *eax);
            if (divisor == 0)
            {
                *eax = *edx = (uint32_t)SignedZeroResult(dividend);
                LogSite(*regs->eip, "signed division by zero");
            }
            else // INT_MIN / -1 overflow
            {
                *eax = (uint32_t)INT32_MIN;
                *edx = 0;
                LogSite(*regs->eip, "signed division overflow");
            }
        }
        else
        {
            uint64_t dividend = ((uint64_t)*edx << 32) | *eax;
            *eax = *edx = dividend != 0 ? 0xFFFFFFFFu : 0;
            LogSite(*regs->eip, "unsigned division by zero");
        }
    }
    else if (width == 16)
    {
        uint32_t dividend = ((*edx & 0xFFFF) << 16) | (*eax & 0xFFFF);
        uint16_t result = isSigned ? (uint16_t)(((int32_t)dividend > 0) ? 0x7FFF : ((int32_t)dividend < 0 ? 0x8000 : 0))
                                   : (dividend != 0 ? 0xFFFF : 0);
        *eax = (*eax & 0xFFFF0000) | result;
        *edx = (*edx & 0xFFFF0000) | result;
        LogSite(*regs->eip, "16-bit division by zero");
    }
    else
    {
        uint16_t dividend = *eax & 0xFFFF;
        uint8_t result = isSigned ? (uint8_t)(((int16_t)dividend > 0) ? 0x7F : ((int16_t)dividend < 0 ? 0x80 : 0))
                                  : (dividend != 0 ? 0xFF : 0);
        *eax = (*eax & 0xFFFF0000) | ((uint32_t)result << 8) | result;
        LogSite(*regs->eip, "8-bit division by zero");
    }
    *regs->eip += len;
    return 1;
}

#ifdef _WIN32
static LONG CALLBACK DivisionHandler(PEXCEPTION_POINTERS info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    CONTEXT *c = info->ContextRecord;
    struct Regs regs = {{(uint32_t *)&c->Eax, (uint32_t *)&c->Ecx, (uint32_t *)&c->Edx, (uint32_t *)&c->Ebx,
                         (uint32_t *)&c->Esp, (uint32_t *)&c->Ebp, (uint32_t *)&c->Esi, (uint32_t *)&c->Edi},
                        (uint32_t *)&c->Eip};

    if (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    return HandleDivisionFault(&regs) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

void FaultCompat_Init(void)
{
    AddVectoredExceptionHandler(1, DivisionHandler);
}
#else
static void DivisionHandler(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
    greg_t *g = uc->uc_mcontext.gregs;
    struct Regs regs = {{(uint32_t *)&g[REG_EAX], (uint32_t *)&g[REG_ECX], (uint32_t *)&g[REG_EDX], (uint32_t *)&g[REG_EBX],
                         (uint32_t *)&g[REG_UESP], (uint32_t *)&g[REG_EBP], (uint32_t *)&g[REG_ESI], (uint32_t *)&g[REG_EDI]},
                        (uint32_t *)&g[REG_EIP]};

    if (!HandleDivisionFault(&regs))
        signal(SIGFPE, SIG_DFL); // not a division we understand: fault normally
}

void FaultCompat_Init(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = DivisionHandler;
    sigaction(SIGFPE, &sa, NULL);
}
#endif

#else
void FaultCompat_Init(void)
{
}
#endif
