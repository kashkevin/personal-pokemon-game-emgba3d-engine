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
// keep the normal sections. GameMemory_Snapshot() records the initial contents
// before the game thread starts; GameMemory_Reset() restores them.
#include "global.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// PE/COFF orders "$"-suffixed sections alphabetically within .data / .bss.
__attribute__((section(".data$gama"))) static char sGameDataBegin[1] = {1};
__attribute__((section(".data$gamz"))) static char sGameDataEnd[1] = {1};
__attribute__((section(".bss$gama"))) static char sGameBssBegin[1];
__attribute__((section(".bss$gamz"))) static char sGameBssEnd[1];
#define GAME_DATA_START ((char *)sGameDataBegin + sizeof(sGameDataBegin))
#define GAME_DATA_END   ((char *)sGameDataEnd)
#define GAME_BSS_START  ((char *)sGameBssBegin + sizeof(sGameBssBegin))
#define GAME_BSS_END    ((char *)sGameBssEnd)
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
static size_t sGameDataSize;

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
    free(sInitialGameData);
    sInitialGameData = malloc(sGameDataSize ? sGameDataSize : 1);
    if (sInitialGameData != NULL)
        memcpy(sInitialGameData, GAME_DATA_START, sGameDataSize);
}

bool32 GameMemory_Reset(void)
{
    if (sInitialGameData == NULL)
        return FALSE;
    memcpy(GAME_DATA_START, sInitialGameData, sGameDataSize);
    memset(GAME_BSS_START, 0, GameMemory_BssSize());
    return TRUE;
}
