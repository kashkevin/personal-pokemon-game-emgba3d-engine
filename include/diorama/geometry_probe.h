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
 *
 * A place a warp cannot show (KNOWN_ISSUES MS-98: a room its facility's challenge sets up first) is entered through
 * its facility instead: "<key> <group> <num> <x> <y> enter <g>.<n>:<x>,<y>:<U|D|L|R> <steps>" puts the player at the
 * start (in front of the attendant, facing it), with the party PKMN_TEST_GEO_PARTY ("species:level,...") gives, and
 * goes in as a player would: A through every line, question (its first choice) and battle, on the frontier's party
 * screen the party's first Pokemon (as many as the facility asks, then CONFIRM), and on the field of each map of
 * <steps> ("<g>.<n>=<action>,...") that map's action: A (talk), U, D, L or R (walk), or pike (the Battle Pike's room of
 * three paths: into the door its hint points to, or another one when the hinted room is the wild Pokemon's). The cells
 * and pictures are taken on the first frame the player stands on the place's map with the field free. The challenge's
 * own save (its attendant saves first) is written to the test save copy the run works on. A place never reached in
 * time has a result line "not-reached" with the reason, and no cells or pictures.
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
/* Render thread, the game thread parked (battle_env_test.c DioramaBattleEnvTest_FilterKeys), each game frame: whether a
 * place is being entered through its facility, and then the keys a player would press for it. */
bool DioramaGeoProbe_Entering(void);
uint16_t DioramaGeoProbe_Keys(void);

/* Render thread, each frame the 3D field was synced (gl_compositor.c): the waiting, the cells, the cameras. */
void DioramaGeoProbe_RenderFrame(const struct DioramaSceneSnapshot *snapshot,
                                 const struct DioramaResolvedCell *resolved, bool ready3D);
/* Render thread (gl_compositor.c UpdateCamera): the camera of the picture being taken, if any, framed on the
 * target given. */
bool DioramaGeoProbe_CameraView(float targetX, float targetZ, float groundY, int place,
                                struct DioramaCameraView *view);
/* Render thread (gl_compositor.c SkyColour): the sky the pictures are taken against. */
bool DioramaGeoProbe_SkyColour(float rgb[3]);
/* Render thread (gl_terrain_renderer.c, a chunk's cells): true on a tour with the magenta sky in a room or a cave
 * (map type mapType), whose black void - the GBA's black cells, drawn as nothing, the room's dark showing there - is
 * then drawn as the black floor the GBA shows, so what shows the sky there is a hole in the scene, not the void. */
bool DioramaGeoProbe_VoidFloor(uint8_t mapType);
/* Render thread (platform/sdl2.c, the frame presented): the stem of the picture due now, or NULL; then written. */
const char *DioramaGeoProbe_CaptureStem(void);
void DioramaGeoProbe_CaptureWritten(void);

#endif // GUARD_DIORAMA_GEOMETRY_PROBE_H
