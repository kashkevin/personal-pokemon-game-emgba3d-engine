#ifndef GUARD_PLATFORM_MEMORY_INFO_H
#define GUARD_PLATFORM_MEMORY_INFO_H

/*
 * The game process's memory and the PC's (performance work 2026-10-11,
 * project-owned): what the memory diagnostics, the benchmark and the memory
 * budget read. System RAM only - the graphics card's memory is the renderer's
 * to count (diorama/gl_profile.h).
 *
 * The native game is a 32-bit program (the game data holds 32-bit pointers,
 * docs/performance/64-bit-and-memory.md in the app repository). On 64-bit
 * Windows the exe is large-address-aware, so it may address 4 GB instead of 2.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct MemoryInfo
{
    uint64_t processResident;      /* working set (Windows), resident set (Linux) */
    uint64_t processPeakResident;
    uint64_t processPrivate;       /* private bytes (Windows), anonymous memory (Linux) */
    uint64_t addressSpaceTotal;    /* the user address space the process may use */
    uint64_t addressSpaceAvailable;
    uint64_t physicalTotal;        /* the PC's RAM */
    uint64_t physicalAvailable;
    bool largeAddressAware;        /* Windows: the exe's flag (a 32-bit exe without it gets 2 GB) */
    int pointerBits;
};

/* False when the system would not say (the fields it could fill are filled). */
bool MemoryInfo_Get(struct MemoryInfo *info);
/* One line: the process, its address space and the PC's RAM. */
void MemoryInfo_Format(const struct MemoryInfo *info, char *buffer, size_t size);

/*
 * PKMN_HIGH_ADDRESS_TEST=1 (Windows, development): at startup, reserve (not
 * commit) the free address space below 2 GB, so everything the run allocates
 * afterwards - SDL, the graphics driver, the renderer, thread stacks - lies
 * above it. A large-address-aware build must still run correctly: this is how
 * it was tested that nothing treats an address over 2 GB as a negative number.
 * Returns the bytes reserved (0 when off or not on Windows).
 */
uint64_t MemoryInfo_HighAddressTest(void);

#endif
