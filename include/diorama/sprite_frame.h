#ifndef GUARD_DIORAMA_SPRITE_FRAME_H
#define GUARD_DIORAMA_SPRITE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaResolvedCell;

#define DIORAMA_SPRITE_MAX_DIMENSION 64
/* A frame's pixels: 64x64, or a composite vehicle's assembled picture (DIORAMA_OBJ_FRAME_MAX_BYTES at 4bpp). */
#define DIORAMA_SPRITE_MAX_PIXELS (DIORAMA_OBJ_FRAME_MAX_BYTES * 2)

struct DioramaSpriteFrameKey
{
    uint8_t shape;
    uint8_t size;
    uint8_t width;   /* the frame's own size: a composite's differs from any OAM shape */
    uint8_t height;
    uint8_t hFlip;
    uint8_t vFlip;
    uint64_t frameHash;
    uint64_t paletteHash;
};

struct DioramaSpritePose
{
    float x;
    float y;
    float z;
    float groundY;
};

bool DioramaSprite_GetDimensions(uint8_t shape, uint8_t size,
                                 uint8_t *width, uint8_t *height);
bool DioramaSprite_IsSupported(const struct DioramaObjectSnapshot *object);
void DioramaSprite_MakeFrameKey(const struct DioramaSceneSnapshot *snapshot,
                                const struct DioramaObjectSnapshot *object,
                                struct DioramaSpriteFrameKey *key);
bool DioramaSprite_FrameKeyEqual(const struct DioramaSpriteFrameKey *left,
                                 const struct DioramaSpriteFrameKey *right);
/* The picture's size: the OAM frame's, or a composite's assembled picture (DIORAMA_OBJECT_COMPOSITE). */
bool DioramaSprite_ObjectDimensions(const struct DioramaObjectSnapshot *object, uint8_t *width, uint8_t *height);
bool DioramaSprite_DecodeFrame(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaObjectSnapshot *object,
                               uint32_t *pixels, size_t pixelCapacity,
                               uint8_t *width, uint8_t *height);
bool DioramaSprite_BuildPose(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaObjectSnapshot *object,
                              struct DioramaSpritePose *pose);
bool DioramaSprite_BuildResolvedPose(const struct DioramaSceneSnapshot *snapshot,
                                     const struct DioramaResolvedCell *resolvedCells,
                                     uint16_t resolvedCount,
                                     const struct DioramaObjectSnapshot *object,
                                     struct DioramaSpritePose *pose);
/*
 * Up to this many game frames between two snapshots drawn one after the other
 * are still drawn moving between them: a drawn frame catches up to five game
 * frames after a slow one (sdl2.c), and the camera and the walkers snapped
 * over any gap of more than one, a jolt after every slow frame and whenever
 * the display's beat drifted against the game's (the owner's camera jitter
 * when running, 2026-10-26).
 */
#define DIORAMA_INTERPOLATION_MAX_GAP 6
/* How far between a drawn snapshot and the one drawn before it, gap game
 * frames apart, a frame frameAlpha of the way past the last game frame
 * stands: the game's states between them taken as on the line between. */
float DioramaSprite_GapAlpha(uint64_t gap, float frameAlpha);
bool DioramaSprite_CanInterpolate(const struct DioramaSceneSnapshot *previousSnapshot,
                                  const struct DioramaObjectSnapshot *previousObject,
                                   const struct DioramaSceneSnapshot *currentSnapshot,
                                   const struct DioramaObjectSnapshot *currentObject);
bool DioramaSprite_CanInterpolateResolved(
    const struct DioramaSceneSnapshot *previousSnapshot,
    const struct DioramaResolvedCell *previousResolvedCells,
    uint16_t previousResolvedCount,
    const struct DioramaObjectSnapshot *previousObject,
    const struct DioramaSceneSnapshot *currentSnapshot,
    const struct DioramaResolvedCell *currentResolvedCells,
    uint16_t currentResolvedCount,
    const struct DioramaObjectSnapshot *currentObject);
struct DioramaSpritePose DioramaSprite_InterpolatePose(struct DioramaSpritePose previous,
                                                         struct DioramaSpritePose current,
                                                         float alpha);
struct DioramaSpritePose DioramaSprite_ApplyScreenOffset(struct DioramaSpritePose pose,
                                                          float offsetX, float offsetY,
                                                          float cameraPitch);
int DioramaSprite_CompareDepth(const struct DioramaSpritePose *left, uint8_t leftPriority,
                               uint8_t leftSubpriority, uint8_t leftOamOrder,
                                const struct DioramaSpritePose *right, uint8_t rightPriority,
                                uint8_t rightSubpriority, uint8_t rightOamOrder,
                                float cameraZ, float cameraPitch);

#endif
