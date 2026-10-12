/*
 * battle_sprite_library.h - loader for the animated Gen 1-6 battle sprite pack ("EBSP", version 1).
 *
 * The pack is produced by the sprite library's runtime export
 * (sprite-library/pipeline/spritelib/runtime_export.py; format described there). It holds only sets that pass
 * the permission gate (official art, or granted sources). Every facing without a pack entry keeps the game's
 * existing GBA sprite: bsl_select() returns -1 for it.
 *
 * Purely visual: nothing here reads or changes battle state, and nothing uses the game's random number
 * generator, so battle mechanics and link/multiplayer synchronisation are unaffected. Pixel data is palette
 * indices (index 0 transparent) with a normal and a shiny palette per facing.
 *
 * C99, no global state. Inflate comes from stb_image.h (public domain / MIT), or from zlib when compiled with
 * BSL_USE_ZLIB.
 */
#ifndef BATTLE_SPRITE_LIBRARY_H
#define BATTLE_SPRITE_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSL_FORMAT_VERSION 1

enum BslFlags {
    BSL_FLAG_ANIMATED = 1,  /* more than one frame */
    BSL_FLAG_SHINY = 2,     /* palette_shiny is a real shiny palette (otherwise it equals palette_normal) */
    BSL_FLAG_OFFICIAL = 4,  /* official Black/White art */
    BSL_FLAG_EDITED = 8,    /* official art with a detail drawn on by the project (owner-approved) */
    BSL_FLAG_STILL = 16     /* taken from a still set (one frame) */
};

/* Maps a game species id (SPECIES_* value) to a library target id; generated as bsl_species_map.h. */
struct BslSpeciesName {
    uint16_t species;
    const char *id;
};

typedef struct BslPack BslPack;

typedef struct BslFacingInfo {
    uint16_t frame_width, frame_height; /* every frame of a facing has the same canvas */
    uint16_t unique_frames, steps;
    int16_t anchor_x, anchor_y;         /* feet point inside the frame, in pixels: place it on the ground point */
    uint32_t loop_ms;                   /* sum of durations_ms */
    const uint16_t *sequence;           /* steps entries: unique frame shown at each step */
    const uint16_t *durations_ms;       /* steps entries */
    const uint32_t *palette_normal;     /* colours entries; bytes in memory: R, G, B, A (A = 0 for index 0) */
    const uint32_t *palette_shiny;      /* equals palette_normal when the facing has no usable shiny */
    uint16_t colours, flags;
    const char *source;                 /* library set path, for logs */
} BslFacingInfo;

/* ---- opening ---------------------------------------------------------------------------------------- */

/* Reads and validates a pack. On failure returns NULL and writes a message to err (if given). */
BslPack *bsl_open_file(const char *path, char *err, size_t err_size);
/* Same from memory. copy != 0 copies the data; otherwise it must outlive the pack. */
BslPack *bsl_open_memory(const void *data, size_t size, int copy, char *err, size_t err_size);
void bsl_close(BslPack *pack);

const char *bsl_info(const BslPack *pack); /* provenance text written by the export */
uint32_t bsl_target_count(const BslPack *pack);
uint32_t bsl_facing_count(const BslPack *pack);
size_t bsl_pack_size(const BslPack *pack);

/* ---- lookup ----------------------------------------------------------------------------------------- */

int bsl_find_target(const BslPack *pack, const char *id); /* library target id, e.g. "venusaurmega"; -1 if absent */
const char *bsl_target_id(const BslPack *pack, int target);
const char *bsl_target_constant(const BslPack *pack, int target); /* "SPECIES_..." */

/* Builds the species -> target table (species ids below num_species). Returns how many names were bound;
 * names whose target is not in the pack are skipped. */
int bsl_bind_species(BslPack *pack, const struct BslSpeciesName *names, size_t count, uint32_t num_species);
int bsl_target_for_species(const BslPack *pack, uint32_t species); /* -1 if unknown */

/* Facing index for a target: back != 0 for the player's side, female != 0 for a female Pokemon (falls back to the
 * default facing when there is no female entry). -1: no pack entry, keep the game's GBA sprite. */
int bsl_select(const BslPack *pack, int target, int back, int female);
/* Convenience: bsl_target_for_species() + bsl_select(). */
int bsl_select_species(const BslPack *pack, uint32_t species, int back, int female);
const BslFacingInfo *bsl_facing(const BslPack *pack, int facing_index);

/* ---- timing ----------------------------------------------------------------------------------------- */

/* Unique frame to show when the facing's clock reads t_ms (the animation loops). */
uint32_t bsl_frame_at(const BslFacingInfo *facing, uint32_t t_ms);
/* Step index for t_ms (0 .. steps-1). */
uint32_t bsl_step_at(const BslFacingInfo *facing, uint32_t t_ms);
/* A deterministic start phase in [0, loop_ms) from any seed (e.g. personality ^ battler slot), so two identical
 * Pokemon do not move in lockstep. Uses no random number generator. */
uint32_t bsl_phase_ms(const BslFacingInfo *facing, uint32_t seed);

/* ---- pixels ----------------------------------------------------------------------------------------- */

/* Decompresses the facing's strip: unique_frames frames side by side, frame_width*unique_frames x frame_height
 * palette indices, row-major. Checks the CRC. Returns 0 on success; free *out with bsl_free(). */
int bsl_decode_indices(const BslPack *pack, int facing_index, uint8_t **out, size_t *out_size);
void bsl_free(void *p);

/* Grid atlas of the frames within max_size x max_size (power-of-two sizes are not required). Returns 0 when the
 * frames fit, -1 otherwise. */
int bsl_atlas_layout(const BslFacingInfo *facing, uint32_t max_size, uint32_t *cols, uint32_t *rows,
                     uint32_t *atlas_w, uint32_t *atlas_h);
/* Top-left pixel of a unique frame in an atlas with `cols` columns. */
void bsl_atlas_frame_xy(const BslFacingInfo *facing, uint32_t cols, uint32_t frame, uint32_t *x, uint32_t *y);
/* Builds the atlas from a decoded strip, either as RGBA8 (4 bytes per pixel, palette applied, shiny != 0 for the
 * shiny palette) or as palette indices (1 byte per pixel, for a palette-lookup shader). Unused cells are
 * transparent. Returns 0 on success; free *out with bsl_free(). */
int bsl_build_atlas_rgba(const BslFacingInfo *facing, const uint8_t *strip, int shiny, uint32_t max_size,
                         uint8_t **out, uint32_t *atlas_w, uint32_t *atlas_h, uint32_t *cols);
int bsl_build_atlas_indices(const BslFacingInfo *facing, const uint8_t *strip, uint32_t max_size,
                            uint8_t **out, uint32_t *atlas_w, uint32_t *atlas_h, uint32_t *cols);
/* Tight bounding box of a frame's opaque pixels (x1, y1 exclusive), e.g. for move effects that aim at the body
 * rather than the canvas. Returns 0, or -1 when the frame is empty. */
int bsl_frame_bounds(const BslFacingInfo *facing, const uint8_t *strip, uint32_t frame, uint16_t *x0, uint16_t *y0,
                     uint16_t *x1, uint16_t *y1);
/* One frame as RGBA8 into dst (frame_width x frame_height, dst_stride bytes per row). */
void bsl_expand_frame_rgba(const BslFacingInfo *facing, const uint8_t *strip, uint32_t frame, int shiny,
                           uint8_t *dst, size_t dst_stride);

#ifdef __cplusplus
}
#endif

#endif /* BATTLE_SPRITE_LIBRARY_H */
