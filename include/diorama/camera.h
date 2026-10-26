#ifndef GUARD_DIORAMA_CAMERA_H
#define GUARD_DIORAMA_CAMERA_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The overworld's camera (graphics overhaul, phase 1: the app's
 * docs/graphics-overhaul.md). Project-owned; pure (no GL), checked by
 * tests/diorama/test_camera.c.
 *
 * Adventure (the default): the 3DS project's framing - about 11.5 tiles from
 * the top of the picture to the bottom at the player outdoors, a little closer
 * indoors and in caves - looking 42 degrees down through a moderate 33 degree
 * lens, the eye following the ground's height, a smoothed follow with a little
 * look-ahead in the walking direction.
 * Diorama (F7): higher (52 degrees) and wider (about 24 tiles) through a long
 * 20 degree lens: the miniature look, for admiring a place.
 * Photo (F9): starts from the view shown; the camera moves freely (zoom,
 * tilt, pan) while the game stands still.
 *
 * Views are in the scene shaders' terms: the target on the ground (x east,
 * z = -map y), the pitch, the focal length in GBA pixels (80 / tan of half
 * the vertical view) and the eye (terrain_mesh.h DioramaCameraRig: its height
 * over y = 0, how far south of the target it stands).
 */
enum DioramaCameraMode
{
    DIORAMA_CAMERA_ADVENTURE,
    DIORAMA_CAMERA_DIORAMA,
    DIORAMA_CAMERA_PHOTO,
    DIORAMA_CAMERA_MODE_COUNT
};

enum DioramaCameraPlace
{
    DIORAMA_CAMERA_OUTDOORS,
    DIORAMA_CAMERA_INDOORS,
    DIORAMA_CAMERA_CAVE
};

struct DioramaCameraView
{
    float x;
    float z;
    float groundY;     /* the ground under the target */
    float pitch;       /* radians down from the horizontal */
    float focalLength; /* GBA pixels */
    float height;      /* the eye over y = 0 */
    float distance;    /* how far south of the target the eye stands */
    float extent;      /* tiles from the top of the picture to the bottom at the target */
};

/* What a frame tells the camera. */
struct DioramaCameraInput
{
    float targetX;      /* the game camera's focus, scene coordinates */
    float targetZ;
    float groundY;      /* the ground under the player */
    float facingX;      /* the player's facing, a unit step: east +x, north +z */
    float facingZ;
    float seconds;      /* real time since the last frame */
    bool snap;          /* a warp or the first frame: no easing */
    bool shift;         /* a map crossing: the coordinates moved by shiftX/Z */
    float shiftX;
    float shiftZ;
    enum DioramaCameraPlace place;
};

/* The vertical view of each mode, the lens the extent is seen through. */
#define DIORAMA_CAMERA_TARGET_VERTICAL (-0.458944f) /* terrain_mesh.h's */

void DioramaCamera_Reset(void);
void DioramaCamera_SetMode(enum DioramaCameraMode mode);
enum DioramaCameraMode DioramaCamera_GetMode(void);
/* The mode photo mode returns to. */
enum DioramaCameraMode DioramaCamera_GetPlayMode(void);
/* Zoom (+ in) and tilt (+ steeper) steps of the current mode, within its
 * limits; kept per mode (pokeemerald.cfg). */
void DioramaCamera_Zoom(int steps);
void DioramaCamera_Tilt(int steps);
void DioramaCamera_GetSteps(enum DioramaCameraMode mode, int *zoomSteps, int *tiltSteps);
void DioramaCamera_SetSteps(enum DioramaCameraMode mode, int zoomSteps, int tiltSteps);
/* Photo mode: moves the target by whole tiles (east +x, north +z), within
 * 16 tiles of where photo mode began. */
void DioramaCamera_PhotoPan(int dx, int dz);
/* Once a frame: the view to draw. */
void DioramaCamera_Update(const struct DioramaCameraInput *input, struct DioramaCameraView *view);
/* The eye for a framing: pitch, lens (degrees of vertical view), extent
 * (tiles at the target) and the ground under the target. */
void DioramaCamera_Frame(float pitch, float fovDegrees, float extent, float groundY,
                         struct DioramaCameraView *view);
/* The focal length that frames a view the same at the target with the
 * overworld's old fixed eye (16 tiles over the ground): for a caller that
 * still draws with that eye (a 3D battle's glide from the field's view). */
float DioramaCamera_FixedEyeFocalLength(const struct DioramaCameraView *view);

/*
 * How far a view sees of level ground `drop` tiles under the target's, in the
 * scene shaders' projection with the rig's screenOffsetY and aspectCorrection
 * (terrain_mesh.h): tiles north of the target at the top of the picture
 * (ahead), south of it at the bottom (behind), and east or west of it at the
 * top corners (side). A view that sees over the horizon reaches
 * DIORAMA_CAMERA_REACH_HORIZON. For the scene's cells (scene_snapshot.h
 * DIORAMA_GRID_FORWARD) and the log of what a player's camera sees.
 */
#define DIORAMA_CAMERA_REACH_HORIZON 1.0e6f
struct DioramaCameraReach
{
    float ahead;
    float behind;
    float side;
};
void DioramaCamera_GroundReach(const struct DioramaCameraView *view, float screenOffsetY, float aspectCorrection,
                               float drop, struct DioramaCameraReach *reach);

#endif
