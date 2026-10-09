// Native PC build: soft reset support.
//
// On the GBA, a soft reset (A+B+START+SELECT, or the game calling SoftReset)
// clears work RAM and restarts the cartridge from its entry point, which
// re-initialises every global variable. Only the cartridge (save flash and the
// real-time clock) keeps its state.
//
// Natively the game's globals are ordinary process memory. Makefile_pc moves
// the .data and .bss of every game object into dedicated sections (game_data /
// game_bss on Linux, .data$game / .bss$game on Windows); platform objects that
// must survive a reset (window, audio device, save image, multiplayer bridge)
// keep the normal sections. GameMemory_Snapshot() records the contents of both
// game sections just before the game thread starts; GameMemory_Reset() restores
// exactly that state. The .bss is copied too, not just zeroed: platform start-up
// code has already written some emulated hardware state (for example REG_VCOUNT
// and the key register) that the game relies on at boot.
// System headers first: global.h defines an abs() macro that breaks <stdlib.h>.
#include <stdlib.h>
#include <string.h>

#include "global.h"

#ifdef _WIN32
// PE/COFF: inside each output section the linker sorts "$"-suffixed input
// sections by name (.data$gama < .data$game < .data$gamz), so these markers
// bracket the game sections. GCC emits a variable placed in ".bss$..." as
// initialised data, which ld would put in a separate output section, so the bss
// markers are defined in assembly with the "b" (uninitialised) flag.
// tools/pc_port/check_game_sections.py verifies the linked layout.
__attribute__((section(".data$gama"))) char gGameDataBegin[4] = {1};
__attribute__((section(".data$gamz"))) char gGameDataEnd[4] = {1};
__asm__(".section .bss$gama,\"bw\"\n\t.balign 4\n\t.globl _gGameBssBegin\n_gGameBssBegin:\n\t.space 4\n"
        ".section .bss$gamz,\"bw\"\n\t.balign 4\n\t.globl _gGameBssEnd\n_gGameBssEnd:\n\t.space 4\n\t.text\n");
extern char gGameBssBegin[], gGameBssEnd[];
#define GAME_DATA_START (gGameDataBegin + sizeof(gGameDataBegin))
#define GAME_DATA_END   (gGameDataEnd)
#define GAME_BSS_START  (gGameBssBegin + 4)
#define GAME_BSS_END    (gGameBssEnd)
#else
// GNU ld defines __start_/__stop_ symbols for sections with C identifier names.
extern char __start_game_data[], __stop_game_data[];
extern char __start_game_bss[], __stop_game_bss[];
#define GAME_DATA_START __start_game_data
#define GAME_DATA_END   __stop_game_data
#define GAME_BSS_START  __start_game_bss
#define GAME_BSS_END    __stop_game_bss
#endif

static char *sInitialGameData;
static char *sInitialGameBss;
static size_t sGameDataSize;
static size_t sGameBssSize;

size_t GameMemory_DataSize(void)
{
    return (size_t)(GAME_DATA_END - GAME_DATA_START);
}

size_t GameMemory_BssSize(void)
{
    return (size_t)(GAME_BSS_END - GAME_BSS_START);
}

void GameMemory_Snapshot(void)
{
    sGameDataSize = GameMemory_DataSize();
    sGameBssSize = GameMemory_BssSize();
    free(sInitialGameData);
    free(sInitialGameBss);
    sInitialGameData = malloc(sGameDataSize ? sGameDataSize : 1);
    sInitialGameBss = malloc(sGameBssSize ? sGameBssSize : 1);
    if (sInitialGameData == NULL || sInitialGameBss == NULL)
    {
        free(sInitialGameData);
        free(sInitialGameBss);
        sInitialGameData = sInitialGameBss = NULL;
        return;
    }
    memcpy(sInitialGameData, GAME_DATA_START, sGameDataSize);
    memcpy(sInitialGameBss, GAME_BSS_START, sGameBssSize);
}

bool32 GameMemory_Reset(void)
{
    if (sInitialGameData == NULL || sInitialGameBss == NULL)
        return FALSE;
    memcpy(GAME_DATA_START, sInitialGameData, sGameDataSize);
    memcpy(GAME_BSS_START, sInitialGameBss, sGameBssSize);
    return TRUE;
}
