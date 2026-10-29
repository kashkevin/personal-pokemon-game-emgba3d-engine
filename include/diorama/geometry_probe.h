#ifndef GUARD_DIORAMA_GEOMETRY_PROBE_H
#define GUARD_DIORAMA_GEOMETRY_PROBE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The geometry probe (step 16, the world geometry overhaul: the app's docs/3d-world-geometry-20261029.md), a test
 * tool of the 3D test build (DIORAMA=1 BFX_TEST=1) only. PKMN_TEST_GEO_TOUR=<file> tours places in one process, one
 * per line "<key> <group> <num> <x> <y>" ('#' comments): at noon (PKMN_TEST_GEO_HOUR), the player is put there; once
 * the field is free and the 3D view has built every chunk round the player, each cell of the scene is written as the
 * terrain mesh is built from it (PKMN_TEST_GEO_OUT/cells/<key>.tsv), then a picture is taken from each camera of
 * PKMN_TEST_GEO_CAMERAS (default "a,d,s15,s75": the Adventure camera, the Diorama camera, a 34-tile view at 15 and 75
 * degrees) to <out>/shots/<key>-<camera>.png, against a magenta sky (PKMN_TEST_GEO_SKY=0 keeps the game's), so a
 * hole in the scene is a colour the game never draws. Each place's outcome is a line of <out>/results.tsv. In memory
 * only: nothing is saved. tools/qa/geometry_check.py makes the tour and reads what it writes.
 */

struct DioramaSceneSnapshot;
struct DioramaResolvedCell;
struct DioramaCameraView;

/* Game thread, each field frame (battle_env_test.c DioramaBattleEnvTest_FieldTick): true while the tour drives the
 * field (the warps, the waiting). */
bool DioramaGeoProbe_FieldTick(void);
/* Game thread, as a warp lands (overworld.c through DioramaBattleEnvTest_TakeWarpCoords): the place's coordinates,
 * which a warp's own s8 ones cannot hold past 127. */
bool DioramaGeoProbe_TakeWarpCoords(int16_t *x, int16_t *y);

/* Render thread, each frame the 3D field was synced (gl_compositor.c): the waiting, the cells, the cameras. */
void DioramaGeoProbe_RenderFrame(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaResolvedCell *resolved, bool ready3D);
/* Render thread (gl_compositor.c UpdateCamera): the camera of the picture being taken, if any, framed on the
 * target given. */
bool DioramaGeoProbe_CameraView(float targetX, float targetZ, float groundY, int place,
                                struct DioramaCameraView *view);
/* Render thread (gl_compositor.c SkyColour): the sky the pictures are taken against. */
bool DioramaGeoProbe_SkyColour(float rgb[3]);
/* Render thread (platform/sdl2.c, the frame presented): the stem of the picture due now, or NULL; then written. */
const char *DioramaGeoProbe_CaptureStem(void);
void DioramaGeoProbe_CaptureWritten(void);

#endif // GUARD_DIORAMA_GEOMETRY_PROBE_H
