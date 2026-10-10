/*
 * The overworld camera (include/diorama/camera.h): its framing against the
 * scene shaders' projection, the modes and their limits, the follow (frame
 * rate independent), warps, map crossings and photo mode. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "diorama/camera.h"

#define DEG (3.14159265358979f / 180.0f)

static int sChecks;

#define CHECK(condition) Check((condition), #condition, __LINE__)
#define NEAR(a, b, tolerance) Near((a), (b), (tolerance), #a, #b, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    sChecks++;
    if (!condition)
    {
        fprintf(stderr, "test_camera.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void Near(double a, double b, double tolerance, const char *left, const char *right, int line)
{
    sChecks++;
    if (!(fabs(a - b) <= tolerance))
    {
        fprintf(stderr, "test_camera.c:%d: %s == %s failed (%.6f vs %.6f)\n", line, left, right, a, b);
        exit(EXIT_FAILURE);
    }
}

/* The scene shaders' projection (gl_terrain_renderer.c and the rest) of a
 * point, relative to the view's target, with the view as the rig. */
static void Project(const struct DioramaCameraView *view, float relativeZ, float y,
                    float *depth, float *vertical)
{
    float pitchSin = sinf(view->pitch);
    float pitchCos = cosf(view->pitch);

    *depth = (view->height - y) * pitchSin + (relativeZ + view->distance) * pitchCos;
    *vertical = -(view->height - y) * pitchCos + (relativeZ + view->distance) * pitchSin;
}

static struct DioramaCameraInput Input(float x, float z, float seconds)
{
    struct DioramaCameraInput input = {0};

    input.targetX = x;
    input.targetZ = z;
    input.seconds = seconds;
    input.place = DIORAMA_CAMERA_OUTDOORS;
    return input;
}

/* A fresh camera in a mode with no zoom or tilt, its first view snapped to (x, z). */
static struct DioramaCameraView Start(enum DioramaCameraMode mode, float x, float z)
{
    struct DioramaCameraInput input = Input(x, z, 0.0f);
    struct DioramaCameraView view;

    DioramaCamera_SetMode(DIORAMA_CAMERA_ADVENTURE);
    DioramaCamera_Reset();
    DioramaCamera_SetMode(mode);
    DioramaCamera_SetSteps(mode, 0, 0);
    input.snap = true;
    DioramaCamera_Update(&input, &view);
    return view;
}

static void FramingMatchesTheShaders(void)
{
    static const float sPitches[] = {28.0f, 42.0f, 52.0f, 70.0f};
    static const float sLenses[] = {20.0f, 33.0f};
    static const float sExtents[] = {7.0f, 11.5f, 24.0f};
    static const float sGrounds[] = {0.0f, 2.5f};

    for (unsigned p = 0; p < 4; p++)
    for (unsigned l = 0; l < 2; l++)
    for (unsigned e = 0; e < 3; e++)
    for (unsigned g = 0; g < 2; g++)
    {
        struct DioramaCameraView view;
        float depth;
        float vertical;
        float northDepth;
        float northVertical;

        DioramaCamera_Frame(sPitches[p] * DEG, sLenses[l], sExtents[e], sGrounds[g], &view);
        NEAR(view.focalLength, 80.0f / tanf(sLenses[l] * 0.5f * DEG), 1e-3);
        Project(&view, 0.0f, sGrounds[g], &depth, &vertical);
        /* The picture is extent tiles tall at the target: 160 / focal x depth. */
        NEAR(160.0f / view.focalLength * depth, sExtents[e], 1e-3);
        /* The target is where the overworld has always put the player. */
        NEAR(vertical, DIORAMA_CAMERA_TARGET_VERTICAL, 1e-4);
        CHECK(view.height > sGrounds[g]);
        CHECK(view.distance > 0.0f);
        /* North (+z) is further away and higher up the picture. */
        Project(&view, 1.0f, sGrounds[g], &northDepth, &northVertical);
        CHECK(northDepth > depth);
        CHECK(northVertical / northDepth > vertical / depth);
    }
}

static void ModeFramings(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 5.0f, -7.0f);
    struct DioramaCameraInput input;

    CHECK(DioramaCamera_GetMode() == DIORAMA_CAMERA_ADVENTURE);
    NEAR(view.x, 5.0f, 1e-6);
    NEAR(view.z, -7.0f, 1e-6);
    NEAR(view.extent, 11.5f, 1e-5);
    NEAR(view.pitch, 42.0f * DEG, 1e-5);
    NEAR(view.focalLength, 80.0f / tanf(16.5f * DEG), 1e-3);
    /* About the 3DS project's eye: some 13 tiles up and 14 back. */
    CHECK(view.height > 12.0f && view.height < 15.0f);
    CHECK(view.distance > 12.0f && view.distance < 16.0f);

    input = Input(5.0f, -7.0f, 1.0f / 60.0f);
    input.place = DIORAMA_CAMERA_INDOORS;
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 9.0f, 1e-5);
    input.place = DIORAMA_CAMERA_CAVE;
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 10.5f, 1e-5);

    view = Start(DIORAMA_CAMERA_DIORAMA, 0.0f, 0.0f);
    CHECK(DioramaCamera_GetMode() == DIORAMA_CAMERA_DIORAMA);
    CHECK(DioramaCamera_GetPlayMode() == DIORAMA_CAMERA_DIORAMA);
    NEAR(view.extent, 24.0f, 1e-5);
    NEAR(view.pitch, 52.0f * DEG, 1e-5);
    NEAR(view.focalLength, 80.0f / tanf(10.0f * DEG), 1e-3);
}

static void ZoomAndTiltLimits(void)
{
    struct DioramaCameraInput input = Input(0.0f, 0.0f, 1.0f / 60.0f);
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    int zoom;
    int tilt;

    DioramaCamera_Zoom(100);
    DioramaCamera_Tilt(100);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_ADVENTURE, &zoom, &tilt);
    CHECK(zoom == 4 && tilt == 5);
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 7.5f, 1e-5);
    NEAR(view.pitch, 62.0f * DEG, 1e-5);

    DioramaCamera_Zoom(-100);
    DioramaCamera_Tilt(-100);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_ADVENTURE, &zoom, &tilt);
    CHECK(zoom == -5 && tilt == -3);
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 16.5f, 1e-5);
    NEAR(view.pitch, 30.0f * DEG, 1e-5);

    /* Indoors a zoom may go a little closer than outdoors' limit. */
    DioramaCamera_SetSteps(DIORAMA_CAMERA_ADVENTURE, 4, 0);
    input.place = DIORAMA_CAMERA_INDOORS;
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 5.25f, 1e-5);

    view = Start(DIORAMA_CAMERA_DIORAMA, 0.0f, 0.0f);
    DioramaCamera_Zoom(1);
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 13.0f, 1e-5); /* indoors: 15 less a 2 tile step */
    input.place = DIORAMA_CAMERA_OUTDOORS;
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 22.0f, 1e-5);
}

static void StepsAreKeptPerMode(void)
{
    int zoom;
    int tilt;

    Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    DioramaCamera_SetSteps(DIORAMA_CAMERA_ADVENTURE, 2, -1);
    DioramaCamera_SetSteps(DIORAMA_CAMERA_DIORAMA, -3, 1);
    DioramaCamera_SetMode(DIORAMA_CAMERA_DIORAMA);
    DioramaCamera_SetMode(DIORAMA_CAMERA_ADVENTURE);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_ADVENTURE, &zoom, &tilt);
    CHECK(zoom == 2 && tilt == -1);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_DIORAMA, &zoom, &tilt);
    CHECK(zoom == -3 && tilt == 1);
    /* Hand-edited values are brought inside the limits. */
    DioramaCamera_SetSteps(DIORAMA_CAMERA_ADVENTURE, 99, -99);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_ADVENTURE, &zoom, &tilt);
    CHECK(zoom == 4 && tilt == -3);
    /* Photo mode's steps are its own, never kept. */
    DioramaCamera_SetSteps(DIORAMA_CAMERA_PHOTO, 3, 3);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_PHOTO, &zoom, &tilt);
    CHECK(zoom == 0 && tilt == 0);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_MODE_COUNT, &zoom, &tilt);
    CHECK(zoom == 4 && tilt == -3);
}

/* The view's x after the target steps 2 tiles east and stands still for
 * `seconds`, at `fps` frames a second. */
static float FollowAt(float fps, float seconds)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    struct DioramaCameraInput input = Input(2.0f, 0.0f, 1.0f / fps);
    int frames = (int)(seconds * fps + 0.5f);

    for (int i = 0; i < frames; i++)
        DioramaCamera_Update(&input, &view);
    return view.x;
}

static void FollowIsSmoothAtAnyFrameRate(void)
{
    float at30 = FollowAt(30.0f, 0.5f);
    float at144 = FollowAt(144.0f, 0.5f);
    float oneFrame = FollowAt(60.0f, 1.0f / 60.0f);

    NEAR(at30, at144, 1e-3);
    NEAR(at30, 2.0f * (1.0f - expf(-0.5f / 0.09f)), 1e-3);
    CHECK(oneFrame > 0.1f && oneFrame < 0.6f); /* eased, not jumped */
    /* A very long frame (a hitch) is taken as a tenth of a second. */
    NEAR(FollowAt(1.0f, 1.0f), 2.0f * (1.0f - expf(-0.1f / 0.09f)), 1e-4);
}

static void LooksAheadWhileWalking(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    struct DioramaCameraInput input = Input(0.0f, 0.0f, 1.0f / 60.0f);
    int frame;

    /* Walking east at 4 tiles a second for a second. */
    input.facingX = 1.0f;
    for (frame = 1; frame <= 60; frame++)
    {
        input.targetX = frame * 4.0f / 60.0f;
        DioramaCamera_Update(&input, &view);
    }
    CHECK(view.x > input.targetX + 0.05f);
    CHECK(view.x < input.targetX + 0.6f);
    NEAR(view.z, 0.0f, 1e-6);
    /* Standing still: the look-ahead goes and the view settles on the player. */
    for (frame = 0; frame < 120; frame++)
        DioramaCamera_Update(&input, &view);
    NEAR(view.x, input.targetX, 0.01);

    /* Diorama mode has none. */
    view = Start(DIORAMA_CAMERA_DIORAMA, 0.0f, 0.0f);
    input.targetX = 0.0f;
    for (frame = 1; frame <= 60; frame++)
    {
        input.targetX = frame * 4.0f / 60.0f;
        DioramaCamera_Update(&input, &view);
    }
    CHECK(view.x < input.targetX);
}

static void WarpsSnap(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    struct DioramaCameraInput input = Input(10.0f, 0.0f, 1.0f / 60.0f);

    DioramaCamera_Update(&input, &view); /* more than 4 tiles: a warp or a cut */
    NEAR(view.x, 10.0f, 1e-6);
    input = Input(12.0f, 1.0f, 1.0f / 60.0f);
    input.snap = true;
    DioramaCamera_Update(&input, &view);
    NEAR(view.x, 12.0f, 1e-6);
    NEAR(view.z, 1.0f, 1e-6);
}

static void MapCrossingsAreSeamless(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    struct DioramaCameraInput input = Input(1.0f, 0.0f, 1.0f / 60.0f);
    float before;
    float unshifted;

    DioramaCamera_Update(&input, &view);
    before = view.x;
    CHECK(before > 0.05f && before < 0.95f);
    /* The same step again without a crossing... */
    DioramaCamera_Update(&input, &view);
    unshifted = view.x;
    /* ...and with one: the new map's coordinates are 20 tiles west. */
    view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    input = Input(1.0f, 0.0f, 1.0f / 60.0f);
    DioramaCamera_Update(&input, &view);
    input = Input(-19.0f, 0.0f, 1.0f / 60.0f);
    input.shift = true;
    input.shiftX = -20.0f;
    DioramaCamera_Update(&input, &view);
    NEAR(view.x, unshifted - 20.0f, 1e-4);
}

static void TheEyeRidesTheGround(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    struct DioramaCameraInput input = Input(0.0f, 0.0f, 1.0f / 60.0f);
    float rise = view.height - view.groundY;

    input.groundY = 1.0f;
    for (int frame = 0; frame < 15; frame++)
        DioramaCamera_Update(&input, &view);
    NEAR(view.groundY, 1.0f - expf(-1.0f), 1e-3);
    NEAR(view.height - view.groundY, rise, 1e-4);
    for (int frame = 0; frame < 180; frame++)
        DioramaCamera_Update(&input, &view);
    NEAR(view.groundY, 1.0f, 1e-3);
}

static void PhotoMode(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_DIORAMA, 4.0f, -4.0f);
    struct DioramaCameraView shown = view;
    struct DioramaCameraInput input = Input(4.0f, -4.0f, 1.0f / 60.0f);
    int zoom;
    int tilt;

    DioramaCamera_SetMode(DIORAMA_CAMERA_PHOTO);
    CHECK(DioramaCamera_GetMode() == DIORAMA_CAMERA_PHOTO);
    CHECK(DioramaCamera_GetPlayMode() == DIORAMA_CAMERA_DIORAMA);
    DioramaCamera_Update(&input, &view);
    /* It begins from the view shown. */
    NEAR(view.x, shown.x, 1e-5);
    NEAR(view.z, shown.z, 1e-5);
    NEAR(view.extent, shown.extent, 1e-5);
    NEAR(view.pitch, shown.pitch, 1e-5);
    NEAR(view.focalLength, shown.focalLength, 1e-3);

    /* Its own steps: 1.5 tiles a zoom step, 4 to 40 tiles, 15 to 80 degrees. */
    DioramaCamera_Zoom(2);
    DioramaCamera_Update(&input, &view);
    NEAR(view.extent, 21.0f, 1e-5);
    DioramaCamera_Zoom(-100);
    DioramaCamera_Tilt(100);
    DioramaCamera_Update(&input, &view);
    CHECK(view.extent <= 40.0f && view.extent > 38.0f);
    NEAR(view.pitch, 80.0f * DEG, 1e-5);
    DioramaCamera_GetSteps(DIORAMA_CAMERA_DIORAMA, &zoom, &tilt);
    CHECK(zoom == 0 && tilt == 0); /* the play mode's are untouched */

    /* Panning: whole tiles, at most 16 from where it began, even far from
     * the player (no snapping back to them). */
    DioramaCamera_PhotoPan(20, -3);
    for (int frame = 0; frame < 120; frame++)
        DioramaCamera_Update(&input, &view);
    NEAR(view.x, 4.0f + 16.0f, 1e-3);
    NEAR(view.z, -4.0f - 3.0f, 1e-3);
    DioramaCamera_PhotoPan(-1, 0);
    DioramaCamera_Update(&input, &view);
    CHECK(view.x < 20.0f && view.x > 19.0f); /* eased */

    /* Leaving returns to the play mode's own view of the player. */
    DioramaCamera_SetMode(DIORAMA_CAMERA_DIORAMA);
    CHECK(DioramaCamera_GetMode() == DIORAMA_CAMERA_DIORAMA);
    input.snap = true;
    DioramaCamera_Update(&input, &view);
    NEAR(view.x, 4.0f, 1e-5);
    NEAR(view.extent, 24.0f, 1e-5);

    /* A panning call outside photo mode does nothing; a reset leaves photo mode. */
    DioramaCamera_PhotoPan(5, 5);
    DioramaCamera_Update(&input, &view);
    NEAR(view.x, 4.0f, 1e-5);
    DioramaCamera_SetMode(DIORAMA_CAMERA_PHOTO);
    DioramaCamera_Reset();
    CHECK(DioramaCamera_GetMode() == DIORAMA_CAMERA_DIORAMA);
}

static void FixedEyeLens(void)
{
    struct DioramaCameraView view = Start(DIORAMA_CAMERA_ADVENTURE, 0.0f, 0.0f);
    float focal = DioramaCamera_FixedEyeFocalLength(&view);
    struct DioramaCameraView fixedEye = view;
    float depth;
    float vertical;

    /* terrain_mesh.h's overworld eye: 16 tiles over y = 0, the target on the ground there. */
    fixedEye.groundY = 0.0f;
    fixedEye.height = 16.0f;
    fixedEye.distance = (16.0f * cosf(view.pitch) + DIORAMA_CAMERA_TARGET_VERTICAL) / sinf(view.pitch);
    Project(&fixedEye, 0.0f, 0.0f, &depth, &vertical);
    NEAR(vertical, DIORAMA_CAMERA_TARGET_VERTICAL, 1e-4);
    NEAR(160.0f / focal * depth, view.extent, 1e-3);
}

int main(void)
{
    FramingMatchesTheShaders();
    ModeFramings();
    ZoomAndTiltLimits();
    StepsAreKeptPerMode();
    FollowIsSmoothAtAnyFrameRate();
    LooksAheadWhileWalking();
    WarpsSnap();
    MapCrossingsAreSeamless();
    TheEyeRidesTheGround();
    PhotoMode();
    FixedEyeLens();
    printf("camera tests passed (%d checks)\n", sChecks);
    return EXIT_SUCCESS;
}
