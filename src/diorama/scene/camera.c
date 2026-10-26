#ifdef ENABLE_DIORAMA
/*
 * The overworld's camera: see include/diorama/camera.h. Project-owned.
 *
 * The framing follows the 3DS project's voxel_camera.c (40 degrees, a 35
 * degree view, the eye about 13 tiles back on a large map: about 10.7 tiles
 * from top to bottom at the player) and its follow (an easing towards the
 * player each frame, the eye riding the ground's height), in real time
 * rather than per frame so it is the same at any frame rate.
 */
#include <math.h>
#include <string.h>

#include "diorama/camera.h"

#define DEG (3.14159265358979f / 180.0f)
#define PHOTO_PAN_LIMIT 16
#define SNAP_DISTANCE 4.0f     /* a jump this far (tiles) is a warp or a script's cut */
#define LEAD_SECONDS 0.35f     /* how fast the look-ahead comes and goes */
#define GROUND_SECONDS 0.25f   /* how fast the eye rides the ground */
#define MOVING_HOLD 0.30f      /* seconds a step keeps the look-ahead */

struct ModeSpec
{
    float pitch;        /* degrees */
    float fov;          /* degrees of vertical view */
    float extent[3];    /* tiles at the target: outdoors, indoors, caves */
    float zoomStep;     /* tiles a zoom step takes off the extent */
    float extentMin;
    float extentMax;
    float tiltStep;     /* degrees */
    float pitchMin;
    float pitchMax;
    float follow;       /* seconds: how closely the target is followed */
    float lead;         /* tiles of look-ahead while walking */
};

static const struct ModeSpec sSpecs[DIORAMA_CAMERA_MODE_COUNT] = {
    [DIORAMA_CAMERA_ADVENTURE] = {42.0f, 33.0f, {11.5f, 9.0f, 10.5f}, 1.0f, 7.0f, 17.0f,
                                  4.0f, 28.0f, 62.0f, 0.09f, 0.6f},
    [DIORAMA_CAMERA_DIORAMA] = {52.0f, 20.0f, {24.0f, 15.0f, 18.0f}, 2.0f, 14.0f, 34.0f,
                                4.0f, 38.0f, 70.0f, 0.20f, 0.0f},
    [DIORAMA_CAMERA_PHOTO] = {0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, 1.5f, 4.0f, 40.0f,
                              4.0f, 15.0f, 80.0f, 0.12f, 0.0f},
};

static enum DioramaCameraMode sMode = DIORAMA_CAMERA_ADVENTURE;
static enum DioramaCameraMode sPlayMode = DIORAMA_CAMERA_ADVENTURE;
static int sZoom[DIORAMA_CAMERA_MODE_COUNT];
static int sTilt[DIORAMA_CAMERA_MODE_COUNT];

static bool sHasView;
static float sX;
static float sZ;
static float sLeadX;
static float sLeadZ;
static float sGround;
static float sLastTargetX;
static float sLastTargetZ;
static float sMovingSeconds;
static struct DioramaCameraView sLastView;

/* Photo mode: where it began, how far it was panned, the view it began with. */
static float sPhotoX;
static float sPhotoZ;
static int sPhotoPanX;
static int sPhotoPanZ;
static float sPhotoExtent;
static float sPhotoPitch;
static float sPhotoFov;

static float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

static float Ease(float seconds, float timeConstant)
{
    if (timeConstant <= 0.0f)
        return 1.0f;
    return 1.0f - expf(-seconds / timeConstant);
}

void DioramaCamera_Reset(void)
{
    sHasView = false;
    sLeadX = sLeadZ = 0.0f;
    sMovingSeconds = 0.0f;
    if (sMode == DIORAMA_CAMERA_PHOTO)
        sMode = sPlayMode;
}

void DioramaCamera_SetMode(enum DioramaCameraMode mode)
{
    if (mode >= DIORAMA_CAMERA_MODE_COUNT || mode == sMode)
        return;
    if (mode == DIORAMA_CAMERA_PHOTO)
    {
        /* From the view shown: the same framing, now free. */
        sPhotoX = sHasView ? sLastView.x : 0.0f;
        sPhotoZ = sHasView ? sLastView.z : 0.0f;
        sPhotoPanX = sPhotoPanZ = 0;
        sPhotoExtent = sHasView ? sLastView.extent : sSpecs[sMode].extent[0];
        sPhotoPitch = sHasView ? sLastView.pitch / DEG : sSpecs[sMode].pitch;
        sPhotoFov = sSpecs[sMode].fov;
        sZoom[DIORAMA_CAMERA_PHOTO] = sTilt[DIORAMA_CAMERA_PHOTO] = 0;
        sMode = DIORAMA_CAMERA_PHOTO;
        return;
    }
    sMode = sPlayMode = mode;
}

enum DioramaCameraMode DioramaCamera_GetMode(void)
{
    return sMode;
}

enum DioramaCameraMode DioramaCamera_GetPlayMode(void)
{
    return sPlayMode;
}

/* The zoom steps that keep the extent inside the mode's limits, from its base. */
static int ClampZoom(enum DioramaCameraMode mode, int steps, float base)
{
    const struct ModeSpec *spec = &sSpecs[mode];
    int most = (int)floorf((base - spec->extentMin) / spec->zoomStep + 0.001f);
    int least = -(int)floorf((spec->extentMax - base) / spec->zoomStep + 0.001f);

    return steps > most ? most : (steps < least ? least : steps);
}

static int ClampTilt(enum DioramaCameraMode mode, int steps, float base)
{
    const struct ModeSpec *spec = &sSpecs[mode];
    int most = (int)floorf((spec->pitchMax - base) / spec->tiltStep + 0.001f);
    int least = -(int)floorf((base - spec->pitchMin) / spec->tiltStep + 0.001f);

    return steps > most ? most : (steps < least ? least : steps);
}

static float BaseExtent(enum DioramaCameraMode mode)
{
    /* Outdoors is the reference for the limits; a room's base is inside them too. */
    return mode == DIORAMA_CAMERA_PHOTO ? sPhotoExtent : sSpecs[mode].extent[0];
}

static float BasePitch(enum DioramaCameraMode mode)
{
    return mode == DIORAMA_CAMERA_PHOTO ? sPhotoPitch : sSpecs[mode].pitch;
}

void DioramaCamera_Zoom(int steps)
{
    sZoom[sMode] = ClampZoom(sMode, sZoom[sMode] + steps, BaseExtent(sMode));
}

void DioramaCamera_Tilt(int steps)
{
    sTilt[sMode] = ClampTilt(sMode, sTilt[sMode] + steps, BasePitch(sMode));
}

void DioramaCamera_GetSteps(enum DioramaCameraMode mode, int *zoomSteps, int *tiltSteps)
{
    if (mode >= DIORAMA_CAMERA_MODE_COUNT)
        mode = DIORAMA_CAMERA_ADVENTURE;
    *zoomSteps = sZoom[mode];
    *tiltSteps = sTilt[mode];
}

void DioramaCamera_SetSteps(enum DioramaCameraMode mode, int zoomSteps, int tiltSteps)
{
    if (mode >= DIORAMA_CAMERA_MODE_COUNT || mode == DIORAMA_CAMERA_PHOTO)
        return;
    sZoom[mode] = ClampZoom(mode, zoomSteps, BaseExtent(mode));
    sTilt[mode] = ClampTilt(mode, tiltSteps, BasePitch(mode));
}

void DioramaCamera_PhotoPan(int dx, int dz)
{
    if (sMode != DIORAMA_CAMERA_PHOTO)
        return;
    sPhotoPanX += dx;
    sPhotoPanZ += dz;
    if (sPhotoPanX > PHOTO_PAN_LIMIT) sPhotoPanX = PHOTO_PAN_LIMIT;
    if (sPhotoPanX < -PHOTO_PAN_LIMIT) sPhotoPanX = -PHOTO_PAN_LIMIT;
    if (sPhotoPanZ > PHOTO_PAN_LIMIT) sPhotoPanZ = PHOTO_PAN_LIMIT;
    if (sPhotoPanZ < -PHOTO_PAN_LIMIT) sPhotoPanZ = -PHOTO_PAN_LIMIT;
}

void DioramaCamera_Frame(float pitch, float fovDegrees, float extent, float groundY,
                         struct DioramaCameraView *view)
{
    float pitchSin = sinf(pitch);
    float pitchCos = cosf(pitch);
    float focal = 80.0f / tanf(fovDegrees * 0.5f * DEG);
    /* The picture's height at the target's depth is 160 / focal x depth. */
    float depth = extent * focal / 160.0f;
    /* The shaders' depth of the target, rel / sin + v cos / sin, solved for
     * the eye's height over the ground (v: the target's vertical offset). */
    float rel = depth * pitchSin - DIORAMA_CAMERA_TARGET_VERTICAL * pitchCos;

    view->groundY = groundY;
    view->pitch = pitch;
    view->focalLength = focal;
    view->height = groundY + rel;
    view->distance = (rel * pitchCos + DIORAMA_CAMERA_TARGET_VERTICAL) / pitchSin;
    view->extent = extent;
}

float DioramaCamera_FixedEyeFocalLength(const struct DioramaCameraView *view)
{
    float pitchSin = sinf(view->pitch);
    float pitchCos = cosf(view->pitch);
    float depth = 16.0f / pitchSin + DIORAMA_CAMERA_TARGET_VERTICAL * pitchCos / pitchSin;

    return 160.0f * depth / view->extent;
}

/* Where the picture's edge row (clipY / depth = edge) meets the ground: the
 * shaders' depth = H sin + (z + D) cos and vertical = -H cos + (z + D) sin with
 * vertical * 2 focal / 160 = (edge - screenOffsetY) * depth, solved for z. */
static bool EdgeOnGround(const struct DioramaCameraView *view, float screenOffsetY, float edge, float drop,
                         float *z, float *depth)
{
    float pitchSin = sinf(view->pitch);
    float pitchCos = cosf(view->pitch);
    float eye = view->height - view->groundY + drop; /* the eye over that ground */
    float slope = (edge - screenOffsetY) * 80.0f / view->focalLength;
    float lean = pitchSin - slope * pitchCos;
    float along;

    if (lean <= 1.0e-4f)
        return false;
    along = eye * (pitchCos + slope * pitchSin) / lean; /* z + D */
    *z = along - view->distance;
    *depth = eye * pitchSin + along * pitchCos;
    return true;
}

void DioramaCamera_GroundReach(const struct DioramaCameraView *view, float screenOffsetY, float aspectCorrection,
                               float drop, struct DioramaCameraReach *reach)
{
    float z;
    float depth;

    reach->ahead = reach->side = DIORAMA_CAMERA_REACH_HORIZON;
    reach->behind = 0.0f;
    if (EdgeOnGround(view, screenOffsetY, 1.0f, drop, &z, &depth))
    {
        reach->ahead = z;
        /* the top corners: clipX = x 2 focal / 240 aspect = +-depth */
        reach->side = depth * 120.0f / (view->focalLength * (aspectCorrection > 0.0f ? aspectCorrection : 1.0f));
    }
    if (EdgeOnGround(view, screenOffsetY, -1.0f, drop, &z, &depth))
        reach->behind = -z;
}

void DioramaCamera_Update(const struct DioramaCameraInput *input, struct DioramaCameraView *view)
{
    enum DioramaCameraMode mode = sMode;
    const struct ModeSpec *spec = &sSpecs[mode == DIORAMA_CAMERA_PHOTO ? sPlayMode : mode];
    float seconds = Clamp(input->seconds, 0.0f, 0.1f);
    float extent;
    float pitch;
    float fov;
    float dx;
    float dz;
    int place = input->place <= DIORAMA_CAMERA_CAVE ? (int)input->place : 0;

    if (input->shift && sHasView)
    {
        /* A map crossing: the same ground under new coordinates. */
        sX += input->shiftX;
        sZ += input->shiftZ;
        sLastTargetX += input->shiftX;
        sLastTargetZ += input->shiftZ;
        sPhotoX += input->shiftX;
        sPhotoZ += input->shiftZ;
    }
    dx = input->targetX - sX;
    dz = input->targetZ - sZ;
    /* Photo mode's camera may stand well away from the player: only a warp
     * or the first frame starts it afresh. */
    if (!sHasView || input->snap
     || (mode != DIORAMA_CAMERA_PHOTO && dx * dx + dz * dz > SNAP_DISTANCE * SNAP_DISTANCE))
    {
        sX = input->targetX;
        sZ = input->targetZ;
        sLeadX = sLeadZ = 0.0f;
        sGround = input->groundY;
        sMovingSeconds = 0.0f;
        sLastTargetX = input->targetX;
        sLastTargetZ = input->targetZ;
        sHasView = true;
        seconds = 0.0f;
    }

    if (mode == DIORAMA_CAMERA_PHOTO)
    {
        float k = seconds == 0.0f ? 1.0f : Ease(seconds, sSpecs[mode].follow);

        sX += (sPhotoX + (float)sPhotoPanX - sX) * k;
        sZ += (sPhotoZ + (float)sPhotoPanZ - sZ) * k;
        extent = Clamp(sPhotoExtent - sZoom[mode] * sSpecs[mode].zoomStep,
                       sSpecs[mode].extentMin, sSpecs[mode].extentMax);
        pitch = Clamp(sPhotoPitch + sTilt[mode] * sSpecs[mode].tiltStep,
                      sSpecs[mode].pitchMin, sSpecs[mode].pitchMax);
        fov = sPhotoFov;
    }
    else
    {
        float leadX = 0.0f;
        float leadZ = 0.0f;
        float k;

        /* A step under way keeps the look-ahead a moment, so it does not
         * flicker between the frames of a walk. */
        if (fabsf(input->targetX - sLastTargetX) + fabsf(input->targetZ - sLastTargetZ) > 0.0001f)
            sMovingSeconds = MOVING_HOLD;
        else if (sMovingSeconds > 0.0f)
            sMovingSeconds -= seconds;
        sLastTargetX = input->targetX;
        sLastTargetZ = input->targetZ;
        if (sMovingSeconds > 0.0f)
        {
            leadX = input->facingX * spec->lead;
            leadZ = input->facingZ * spec->lead;
        }
        k = Ease(seconds, LEAD_SECONDS);
        sLeadX += (leadX - sLeadX) * k;
        sLeadZ += (leadZ - sLeadZ) * k;
        k = seconds == 0.0f ? 1.0f : Ease(seconds, spec->follow);
        sX += (input->targetX + sLeadX - sX) * k;
        sZ += (input->targetZ + sLeadZ - sZ) * k;
        sGround += (input->groundY - sGround) * (seconds == 0.0f ? 1.0f : Ease(seconds, GROUND_SECONDS));
        extent = Clamp(spec->extent[place] - sZoom[mode] * spec->zoomStep,
                       spec->extentMin * (place == 0 ? 1.0f : 0.75f), spec->extentMax);
        pitch = Clamp(spec->pitch + sTilt[mode] * spec->tiltStep, spec->pitchMin, spec->pitchMax);
        fov = spec->fov;
    }
    DioramaCamera_Frame(pitch * DEG, fov, extent, sGround, view);
    view->x = sX;
    view->z = sZ;
    sLastView = *view;
}
#endif
