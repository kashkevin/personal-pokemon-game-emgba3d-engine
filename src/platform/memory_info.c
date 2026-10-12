/*
 * Process and system memory (see platform/memory_info.h). Project-owned
 * (performance work, 2026-10-11).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#ifdef _WIN32
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2 /* GetProcessMemoryInfo is K32GetProcessMemoryInfo in kernel32 */
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/sysinfo.h>
#endif

#include "platform/memory_info.h"

#define MB (1024.0 * 1024.0)

#ifdef _WIN32
static bool ExeIsLargeAddressAware(void)
{
    const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;

    if (base == NULL || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE
        && (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}
#else
/* A /proc/self/status line's value in kB, as bytes (0 when absent). */
static uint64_t StatusValue(const char *text, const char *name)
{
    const char *line = strstr(text, name);
    unsigned long long kb = 0;

    if (line == NULL || sscanf(line + strlen(name), " %llu", &kb) != 1)
        return 0;
    return (uint64_t)kb * 1024u;
}
#endif

bool MemoryInfo_Get(struct MemoryInfo *info)
{
    bool ok = true;

    memset(info, 0, sizeof(*info));
    info->pointerBits = (int)(sizeof(void *) * 8);
#ifdef _WIN32
    {
        MEMORYSTATUSEX status;
        PROCESS_MEMORY_COUNTERS_EX counters;

        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status))
        {
            info->physicalTotal = status.ullTotalPhys;
            info->physicalAvailable = status.ullAvailPhys;
            info->addressSpaceTotal = status.ullTotalVirtual;
            info->addressSpaceAvailable = status.ullAvailVirtual;
        }
        else
            ok = false;
        memset(&counters, 0, sizeof(counters));
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&counters, sizeof(counters)))
        {
            info->processResident = counters.WorkingSetSize;
            info->processPeakResident = counters.PeakWorkingSetSize;
            info->processPrivate = counters.PrivateUsage;
        }
        else
            ok = false;
        info->largeAddressAware = ExeIsLargeAddressAware();
    }
#else
    {
        FILE *file = fopen("/proc/self/status", "r");
        char text[4096];
        size_t length = 0;
        struct sysinfo system;

        if (file != NULL)
        {
            length = fread(text, 1, sizeof(text) - 1, file);
            fclose(file);
        }
        text[length] = '\0';
        if (length > 0)
        {
            uint64_t size = StatusValue(text, "VmSize:");

            info->processResident = StatusValue(text, "VmRSS:");
            info->processPeakResident = StatusValue(text, "VmHWM:");
            info->processPrivate = StatusValue(text, "RssAnon:");
            /* A 32-bit program on a 64-bit kernel may use 4 GB of address space. */
            info->addressSpaceTotal = sizeof(void *) == 4 ? (uint64_t)4 << 30 : (uint64_t)128 << 40;
            info->addressSpaceAvailable = info->addressSpaceTotal > size ? info->addressSpaceTotal - size : 0;
        }
        else
            ok = false;
        if (sysinfo(&system) == 0)
        {
            info->physicalTotal = (uint64_t)system.totalram * system.mem_unit;
            info->physicalAvailable = ((uint64_t)system.freeram + system.bufferram) * system.mem_unit;
        }
        else
            ok = false;
        info->largeAddressAware = true; /* not a Windows notion: the kernel gives a 32-bit program 4 GB */
    }
#endif
    return ok;
}

void MemoryInfo_Format(const struct MemoryInfo *info, char *buffer, size_t size)
{
    SDL_snprintf(buffer, size,
                 "game process %.0f MB in use (peak %.0f MB, private %.0f MB); %d-bit program, "
                 "address space %.0f MB of which %.0f MB free%s; PC RAM %.0f MB, %.0f MB available",
                 info->processResident / MB, info->processPeakResident / MB, info->processPrivate / MB,
                 info->pointerBits, info->addressSpaceTotal / MB, info->addressSpaceAvailable / MB,
#ifdef _WIN32
                 info->pointerBits == 32 ? (info->largeAddressAware ? " (large address aware)"
                                                                    : " (not large address aware)") : "",
#else
                 "",
#endif
                 info->physicalTotal / MB, info->physicalAvailable / MB);
}

uint64_t MemoryInfo_HighAddressTest(void)
{
#ifdef _WIN32
    const char *value = getenv("PKMN_HIGH_ADDRESS_TEST");
    uint64_t reserved = 0;
    SIZE_T chunk = (SIZE_T)64 << 20;

    if (value == NULL || value[0] != '1')
        return 0;
    /* Without the flag nothing lies above 2 GB: the test would starve the run. */
    if (sizeof(void *) == 4 && !ExeIsLargeAddressAware())
        return 0;
    /* Bottom-up: each reservation takes the lowest free range that fits. One
     * that lands at 2 GB or above means the low half is full at that size. */
    while (chunk >= ((SIZE_T)64 << 10))
    {
        void *range = VirtualAlloc(NULL, chunk, MEM_RESERVE, PAGE_NOACCESS);

        if (range == NULL)
        {
            chunk /= 2;
            continue;
        }
        if ((uintptr_t)range >= 0x80000000u)
        {
            VirtualFree(range, 0, MEM_RELEASE);
            chunk /= 2;
            continue;
        }
        reserved += chunk;
    }
    return reserved;
#else
    return 0;
#endif
}
