#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/metatile_behaviors.h"
#include "diorama/sprite_frame.h"
#include "diorama/terrain_mesh.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)
#define CHECK_CLOSE(actual, expected) CheckClose((actual), (expected), #actual, __LINE__)

static struct DioramaSceneSnapshot sSnapshot;
static struct DioramaResolvedCell sResolvedCells[DIORAMA_MAX_VISIBLE_CELLS];
static uint32_t sPixels[DIORAMA_SPRITE_MAX_PIXELS];

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_sprite_frame.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

static void CheckClose(float actual, float expected, const char *expression, int line)
{
    if (fabsf(actual - expected) > 0.0001f)
    {
        fprintf(stderr, "test_sprite_frame.c:%d: %s was %.6f, expected %.6f\n",
                line, expression, actual, expected);
        exit(EXIT_FAILURE);
    }
}

static void FillTile(struct DioramaObjectSnapshot *object, uint16_t tile, uint8_t color)
{
    memset(&object->frameGraphics[tile * DIORAMA_TILE_BYTES],
           color | (color << 4), DIORAMA_TILE_BYTES);
}

static void InitSnapshot(void)
{
    int x;
    int y;

    memset(&sSnapshot, 0, sizeof(sSnapshot));
    sSnapshot.sequence = 7;
    sSnapshot.mapGeneration = 3;
    sSnapshot.objPaletteGeneration = 13;
    sSnapshot.visibleCellCount = DIORAMA_MAX_VISIBLE_CELLS;
    for (y = 0; y < DIORAMA_GRID_HEIGHT; y++)
    {
        for (x = 0; x < DIORAMA_GRID_WIDTH; x++)
        {
            int index = y * DIORAMA_GRID_WIDTH + x;
            sSnapshot.cells[index].mapX = x;
            sSnapshot.cells[index].mapY = y;
        }
    }
    sSnapshot.fadedPalette[256 + 1] = 0x001F;
    sSnapshot.fadedPalette[256 + 2] = 0x03E0;
    sSnapshot.fadedPalette[256 + 3] = 0x7C00;
    sSnapshot.fadedPalette[256 + 4] = 0x7FFF;
}

static struct DioramaObjectSnapshot MakeObject(void)
{
    struct DioramaObjectSnapshot object;

    memset(&object, 0, sizeof(object));
    object.active = 1;
    object.localId = 1;
    object.tileNum = 10;
    object.oamShape = 0;
    object.oamSize = 1;
    object.frameSize = 16 * 16 / 2;
    object.frameHash = 17;
    return object;
}

static void TestDimensions(void)
{
    static const uint8_t expectedWidths[3][4] = {
        {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}
    };
    static const uint8_t expectedHeights[3][4] = {
        {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}
    };
    uint8_t shape;
    uint8_t size;

    for (shape = 0; shape < 3; shape++)
    {
        for (size = 0; size < 4; size++)
        {
            uint8_t width;
            uint8_t height;

            CHECK(DioramaSprite_GetDimensions(shape, size, &width, &height));
            CHECK(width == expectedWidths[shape][size]);
            CHECK(height == expectedHeights[shape][size]);
        }
    }
    CHECK(!DioramaSprite_GetDimensions(3, 0, &shape, &size));
}

static void TestDecodeAndFlips(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    uint8_t width;
    uint8_t height;

    FillTile(&object, 0, 1);
    FillTile(&object, 1, 2);
    FillTile(&object, 2, 3);
    FillTile(&object, 3, 4);
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(width == 16 && height == 16);
    CHECK(sPixels[0] == 0xFFFF0000u);
    CHECK(sPixels[8] == 0xFF00FF00u);
    CHECK(sPixels[8 * width] == 0xFF0000FFu);
    CHECK(sPixels[8 * width + 8] == 0xFFFFFFFFu);

    object.hFlip = 1;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0xFF00FF00u);
    CHECK(sPixels[15] == 0xFFFF0000u);
    object.hFlip = 0;
    object.vFlip = 1;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0xFF0000FFu);

    FillTile(&object, 0, 0);
    object.vFlip = 0;
    CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels,
                                    DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
    CHECK(sPixels[0] == 0);
}

static void TestSupportAndFrameKeys(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpriteFrameKey left;
    struct DioramaSpriteFrameKey right;

    CHECK(DioramaSprite_IsSupported(&object));
    DioramaSprite_MakeFrameKey(&sSnapshot, &object, &left);
    DioramaSprite_MakeFrameKey(&sSnapshot, &object, &right);
    CHECK(DioramaSprite_FrameKeyEqual(&left, &right));
    right.paletteHash++;
    CHECK(!DioramaSprite_FrameKeyEqual(&left, &right));
    object.bpp = 1;
    CHECK(!DioramaSprite_IsSupported(&object));
    object.bpp = 0;
    object.affineMode = 1;
    CHECK(!DioramaSprite_IsSupported(&object)); /* no matrix: flattened */
    object.affineMatrix[0] = 0x200; /* half width, as a Poke Ball animation */
    object.affineMatrix[3] = 0x100;
    CHECK(DioramaSprite_IsSupported(&object));
    object.affineMode = 3; /* double size */
    CHECK(DioramaSprite_IsSupported(&object));
    object.affineMode = 2; /* erase */
    CHECK(!DioramaSprite_IsSupported(&object));
    object.affineMatrix[0] = 0;
    object.affineMatrix[3] = 0;
    object.affineMode = 0;
    object.flags = DIORAMA_OBJECT_SUBSPRITES;
    CHECK(!DioramaSprite_IsSupported(&object));
}

static void TestPoseAndInterpolation(void)
{
    struct DioramaSceneSnapshot previousSnapshot = sSnapshot;
    struct DioramaObjectSnapshot previous = MakeObject();
    struct DioramaObjectSnapshot current = MakeObject();
    struct DioramaSpritePose pose;
    struct DioramaSpritePose interpolated;

    sSnapshot.cells[1 * DIORAMA_GRID_WIDTH + 2].behavior = MB_JUMP_EAST;
    sSnapshot.sequence = previousSnapshot.sequence + 1;
    current.previousMapX = 1;
    current.previousMapY = 1;
    current.currentMapX = 2;
    current.currentMapY = 1;
    current.mapPixelOffsetX = -8;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &current, &pose));
    CHECK_CLOSE(pose.x, 1.5f);
    CHECK_CLOSE(pose.z, -1.0f);
    CHECK_CLOSE(pose.y, 0.0f);

    previous.previousMapX = 1;
    previous.previousMapY = 1;
    previous.currentMapX = 1;
    previous.currentMapY = 1;
    current.mapPixelOffsetX = -15;
    CHECK(DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
    interpolated = DioramaSprite_InterpolatePose((struct DioramaSpritePose){1, 2, 3, 4},
                                                  (struct DioramaSpritePose){3, 4, 5, 6}, 0.25f);
    CHECK_CLOSE(interpolated.x, 1.5f);
    CHECK_CLOSE(interpolated.y, 2.5f);
    CHECK_CLOSE(interpolated.z, 3.5f);
    CHECK_CLOSE(interpolated.groundY, 4.5f);

    sSnapshot.mapGeneration++;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
    sSnapshot.mapGeneration--;
    current.currentMapX = 4;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous, &sSnapshot, &current));
}

static void TestJumpLift(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpritePose pose;

    object.previousMapX = object.currentMapX = 2;
    object.previousMapY = object.currentMapY = 2;
    object.spriteY2 = -8;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
    CHECK_CLOSE(pose.groundY, 0.0f);
    CHECK_CLOSE(pose.y, 0.5f);
}

static void TestResolvedPlateauPose(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpritePose pose;
    int index = 2 * DIORAMA_GRID_WIDTH + 2;

    memset(sResolvedCells, 0, sizeof(sResolvedCells));
    object.previousMapX = object.currentMapX = 2;
    object.previousMapY = object.currentMapY = 2;
    sResolvedCells[index].shape = DIORAMA_SHAPE_FLAT;
    sResolvedCells[index].groundHeight = 2.0f;
    sResolvedCells[index].topHeight = 2.0f;
    CHECK(DioramaSprite_BuildResolvedPose(&sSnapshot, sResolvedCells,
                                           sSnapshot.visibleCellCount,
                                           &object, &pose));
    CHECK_CLOSE(pose.groundY, 2.0f);
    CHECK_CLOSE(pose.y, 2.0f);
}

static void TestCardinalMovementAnchors(void)
{
    struct DioramaObjectSnapshot object = MakeObject();
    struct DioramaSpritePose pose;

    object.previousMapX = 2;
    object.previousMapY = 2;
    object.currentMapX = 2;
    object.currentMapY = 2;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);
    CHECK_CLOSE(pose.z, -2.0f);

    object.currentMapX = 3;
    object.mapPixelOffsetX = -16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);
    object.currentMapX = 1;
    object.mapPixelOffsetX = 16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.x, 2.0f);

    object.currentMapX = 2;
    object.mapPixelOffsetX = 0;
    object.currentMapY = 3;
    object.mapPixelOffsetY = -16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
    object.currentMapY = 1;
    object.mapPixelOffsetY = 16;
    CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &pose));
    CHECK_CLOSE(pose.z, -2.0f);
}

static void TestInterpolationDiscontinuities(void)
{
    struct DioramaSceneSnapshot previousSnapshot = sSnapshot;
    struct DioramaSceneSnapshot currentSnapshot = sSnapshot;
    struct DioramaObjectSnapshot previous = MakeObject();
    struct DioramaObjectSnapshot current = MakeObject();

    previous.previousMapX = previous.currentMapX = 1;
    previous.previousMapY = previous.currentMapY = 1;
    current.previousMapX = current.currentMapX = 2;
    current.previousMapY = current.currentMapY = 1;
    currentSnapshot.sequence = previousSnapshot.sequence + 1;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    current.previousMapX = 1;
    current.mapPixelOffsetX = 0;
    current.currentMapX = 2;
    currentSnapshot.sequence = previousSnapshot.sequence + 1;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    current.mapPixelOffsetX = -15;
    CHECK(DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                        &currentSnapshot, &current));
    /* A drawn frame that caught up game frames still moves between them
     * (DIORAMA_INTERPOLATION_MAX_GAP); past that, and backwards, it does not. */
    currentSnapshot.sequence = previousSnapshot.sequence + 2;
    CHECK(DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                        &currentSnapshot, &current));
    currentSnapshot.sequence = previousSnapshot.sequence + DIORAMA_INTERPOLATION_MAX_GAP + 1;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    currentSnapshot.sequence = previousSnapshot.sequence;
    CHECK(!DioramaSprite_CanInterpolate(&previousSnapshot, &previous,
                                         &currentSnapshot, &current));
    CHECK(DioramaSprite_GapAlpha(1, 0.25f) == 0.25f);
    CHECK(DioramaSprite_GapAlpha(2, 0.0f) == 0.5f);
    CHECK(DioramaSprite_GapAlpha(2, 1.0f) == 1.0f);
    CHECK(DioramaSprite_GapAlpha(4, 0.5f) == 0.875f);
    CHECK(DioramaSprite_GapAlpha(DIORAMA_INTERPOLATION_MAX_GAP + 1, 0.25f) == 0.25f);
    CHECK(DioramaSprite_GapAlpha(3, 2.0f) == 1.0f);
}

static void TestDepthOrder(void)
{
    struct DioramaSpritePose farPose = {0, 0, 5, 0};
    struct DioramaSpritePose nearPose = {0, 0, 2, 0};

    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) < 0);
    CHECK(DioramaSprite_CompareDepth(&nearPose, 2, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) < 0);
    CHECK(DioramaSprite_CompareDepth(&nearPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.70758444f) > 0);
    farPose.y = 2.0f;
    farPose.z = 1.0f;
    nearPose.y = 0.0f;
    nearPose.z = 0.0f;
    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 0.34906585f) < 0);
    CHECK(DioramaSprite_CompareDepth(&farPose, 1, 10, 1,
                                     &nearPose, 1, 10, 2, 0, 1.22173048f) > 0);
}

static void TestAttachedScreenOffset(void)
{
    struct DioramaSpritePose pose = {2.0f, 3.0f, 4.0f, 1.0f};
    const float pitch = 0.70758444f;

    pose = DioramaSprite_ApplyScreenOffset(pose, 8, 16, pitch);
    CHECK_CLOSE(pose.x, 2.5f);
    CHECK_CLOSE(pose.y, 3.0f - cosf(pitch));
    CHECK_CLOSE(pose.z, 4.0f - sinf(pitch));
    CHECK_CLOSE(pose.groundY, 1.0f);
}

/* A composite vehicle (the moving truck, the S.S. Tidal, the submarine's shadow, the S.S. Anne): its parts assembled
 * by the snapshot into one picture of its own size, laid out in tiles; drawn as one card, its centre and bottom row
 * where the picture is (KNOWN_ISSUES P7: such objects were refused and the 3D view fell back to 2D). */
static void TestCompositeVehicles(void)
{
    static const struct { uint8_t w, h; int8_t shiftX, feet; } sizes[] = {
        {48, 48, 0, 0},    /* the moving truck */
        {96, 40, 0, 0},    /* the S.S. Tidal */
        {88, 32, 0, 0},    /* the submarine's shadow */
        {128, 64, 32, 16}, /* the S.S. Anne (FRLG): its parts from x -32 to 96 and y -16 to 48 about the sprite */
    };
    struct DioramaObjectSnapshot object;
    struct DioramaSpriteFrameKey truckKey;
    struct DioramaSpriteFrameKey shipKey;
    struct DioramaSpritePose plain;
    struct DioramaSpritePose shifted;
    uint8_t width;
    uint8_t height;
    size_t i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        object = MakeObject();
        object.oamShape = 0;
        object.oamSize = 0; /* the sprite's own OAM: 8x8, as the S.S. Tidal's */
        object.flags = DIORAMA_OBJECT_SUBSPRITES;
        CHECK(!DioramaSprite_IsSupported(&object)); /* not assembled: still refused */
        object.flags = DIORAMA_OBJECT_COMPOSITE;
        object.compositeWidth = sizes[i].w;
        object.compositeHeight = sizes[i].h;
        object.compositeShiftX = sizes[i].shiftX;
        object.compositeFeetShift = sizes[i].feet;
        object.frameSize = (uint16_t)(sizes[i].w * sizes[i].h / 2);
        memset(object.frameGraphics, 0, sizeof(object.frameGraphics));
        FillTile(&object, 0, 1);                                   /* top-left tile */
        FillTile(&object, (uint16_t)(sizes[i].w / 8 * sizes[i].h / 8 - 1), 4); /* bottom-right tile */
        CHECK(DioramaSprite_IsSupported(&object));
        CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels, DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
        CHECK(width == sizes[i].w && height == sizes[i].h);
        CHECK(sPixels[0] == 0xFFFF0000u);
        CHECK(sPixels[(size_t)width * height - 1] == 0xFFFFFFFFu);
        CHECK(sPixels[width - 1] == 0);
        object.hFlip = 1; /* the whole picture mirrored, as the GBA mirrors the parts' places and the parts */
        CHECK(DioramaSprite_DecodeFrame(&sSnapshot, &object, sPixels, DIORAMA_SPRITE_MAX_PIXELS, &width, &height));
        CHECK(sPixels[width - 1] == 0xFFFF0000u);
        object.hFlip = 0;
        /* a frame size that is not the picture's is refused */
        object.frameSize = (uint16_t)(sizes[i].w * sizes[i].h / 2 - 32);
        CHECK(!DioramaSprite_IsSupported(&object));
        object.frameSize = (uint16_t)(sizes[i].w * sizes[i].h / 2);
        if (i == 0)
            DioramaSprite_MakeFrameKey(&sSnapshot, &object, &truckKey);
        if (i == 1)
            DioramaSprite_MakeFrameKey(&sSnapshot, &object, &shipKey);
        /* the card stands where the picture is */
        object.currentMapX = object.previousMapX = 3;
        object.currentMapY = object.previousMapY = 2;
        CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &shifted));
        object.flags = 0;
        object.oamSize = 1;
        object.frameSize = 16 * 16 / 2;
        CHECK(DioramaSprite_BuildPose(&sSnapshot, &object, &plain));
        CHECK_CLOSE(shifted.x - plain.x, sizes[i].shiftX / 16.0f);
        CHECK_CLOSE(shifted.z - plain.z, -sizes[i].feet / 16.0f);
    }
    /* two composites with the same tiles but different sizes are different textures */
    truckKey.frameHash = shipKey.frameHash;
    CHECK(!DioramaSprite_FrameKeyEqual(&truckKey, &shipKey));
    /* larger than a frame can hold: refused */
    object = MakeObject();
    object.flags = DIORAMA_OBJECT_COMPOSITE;
    object.compositeWidth = 128;
    object.compositeHeight = 128;
    object.frameSize = 8192 / 2 * 2;
    CHECK(!DioramaSprite_IsSupported(&object));
}

int main(void)
{
    InitSnapshot();
    TestDimensions();
    TestDecodeAndFlips();
    TestSupportAndFrameKeys();
    TestPoseAndInterpolation();
    TestJumpLift();
    TestResolvedPlateauPose();
    TestCardinalMovementAnchors();
    TestInterpolationDiscontinuities();
    TestDepthOrder();
    TestAttachedScreenOffset();
    TestCompositeVehicles();
    puts("sprite frame tests passed");
    return EXIT_SUCCESS;
}
