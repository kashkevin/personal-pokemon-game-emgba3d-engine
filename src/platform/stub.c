// Native PC build: stand-ins for GBA hardware routines that have no PC
// equivalent (link-cable multiboot, GameCube link, interrupt vector code).
// Adapted from the pokeemerald PC port's stub.c.
#include "global.h"
#include "main.h"
#include "multiboot.h"
#include "libgcnmultiboot.h"

#include <stdio.h>

#define STUB_FUNC(func) func { puts("function \"" #func "\" is a stub"); }
#define STUB_FUNC_BLOCK(func, block) func { puts("function \"" #func "\" is a stub"); block }
#define STUB_FUNC_QUIET(func) func {}

// Interrupt dispatch is done by the platform main loop (src/platform/sdl2.c).
STUB_FUNC_QUIET(void IntrMain(void))

// Link-cable multiboot (src/multiboot.c is GBA-only).
STUB_FUNC(void MultiBootInit(struct MultiBootParam *mp))
STUB_FUNC_BLOCK(int MultiBootMain(struct MultiBootParam *mp), return 0;)
STUB_FUNC(void MultiBootStartProbe(struct MultiBootParam *mp))
STUB_FUNC(void MultiBootStartMaster(struct MultiBootParam *mp, const u8 *srcp, int length, u8 palette_color, s8 palette_speed))
STUB_FUNC_BLOCK(int MultiBootCheckComplete(struct MultiBootParam *mp), return 0;)

// GameCube link (src/libgcnmultiboot.s is GBA-only).
STUB_FUNC(void GameCubeMultiBoot_Hash(void))
STUB_FUNC_QUIET(void GameCubeMultiBoot_Main(struct GcmbStruct *gcmb))
STUB_FUNC(void GameCubeMultiBoot_ExecuteProgram(struct GcmbStruct *gcmb))
STUB_FUNC(void GameCubeMultiBoot_Init(struct GcmbStruct *gcmb))
STUB_FUNC(void GameCubeMultiBoot_HandleSerialInterrupt(struct GcmbStruct *gcmb))
STUB_FUNC_QUIET(void GameCubeMultiBoot_Quit(void))
