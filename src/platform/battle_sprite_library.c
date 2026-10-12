/*
 * battle_sprite_library.c - see battle_sprite_library.h. Pack format: runtime_export.py in the sprite library.
 */
#include "platform/battle_sprite_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BSL_USE_ZLIB
#include <zlib.h>
#else
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_IMAGE_STATIC
#define STBI_NO_GIF
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "platform/third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

#define HEADER_SIZE 64u
#define TARGET_SIZE 16u
#define FACING_SIZE 48u
#define BLOB_SIZE 16u

struct BslBlob {
    uint32_t offset, size, raw_size, crc;
};

struct BslPack {
    uint8_t *owned;          /* buffer to free, or NULL */
    const uint8_t *data;
    size_t size;
    uint32_t n_targets, n_facings, n_blobs;
    uint32_t off_targets, off_strings, strings_size;
    const char *info;
    BslFacingInfo *facings;
    uint32_t *facing_blob;   /* blob index per facing */
    struct BslBlob *blobs;
    int16_t *species;        /* species -> target, or NULL */
    uint32_t n_species;
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) {
        strncpy(err, msg, n - 1);
        err[n - 1] = '\0';
    }
}

static uint32_t crc32_bytes(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int ready = 0;
    uint32_t c;
    size_t i;
    if (!ready) {   /* idempotent: a race only recomputes the same table */
        uint32_t k;
        for (k = 0; k < 256; k++) {
            uint32_t v = k;
            int j;
            for (j = 0; j < 8; j++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[k] = v;
        }
        ready = 1;
    }
    c = 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* A NUL-terminated string at off inside the string table. */
static const char *str_at(const BslPack *pk, uint32_t off) {
    const char *s;
    if (off >= pk->strings_size)
        return NULL;
    s = (const char *)pk->data + pk->off_strings + off;
    if (!memchr(s, '\0', pk->strings_size - off))
        return NULL;
    return s;
}

static int host_is_little_endian(void) {
    const uint16_t v = 1;
    return *(const uint8_t *)&v == 1;
}

static BslPack *fail(BslPack *pk, char *err, size_t n, const char *msg) {
    set_err(err, n, msg);
    bsl_close(pk);
    return NULL;
}

BslPack *bsl_open_memory(const void *data, size_t size, int copy, char *err, size_t err_size) {
    BslPack *pk;
    const uint8_t *d;
    uint32_t off_facings, off_blobs, off_u16, u16_size, off_pal, pal_size, i;
    const uint16_t *u16;
    const uint32_t *pal;
    uint32_t n_u16, n_pal;

    if (!host_is_little_endian()) {
        set_err(err, err_size, "big-endian hosts are not supported");
        return NULL;
    }
    if (!data || size < HEADER_SIZE) {
        set_err(err, err_size, "pack too small");
        return NULL;
    }
    pk = (BslPack *)calloc(1, sizeof *pk);
    if (!pk) {
        set_err(err, err_size, "out of memory");
        return NULL;
    }
    if (copy || ((uintptr_t)data & 3u)) {
        pk->owned = (uint8_t *)malloc(size);
        if (!pk->owned)
            return fail(pk, err, err_size, "out of memory");
        memcpy(pk->owned, data, size);
        pk->data = pk->owned;
    } else {
        pk->data = (const uint8_t *)data;
    }
    pk->size = size;
    d = pk->data;
    if (memcmp(d, "EBSP", 4) != 0)
        return fail(pk, err, err_size, "not an EBSP pack");
    if (rd16(d + 4) != BSL_FORMAT_VERSION)
        return fail(pk, err, err_size, "unsupported pack version");
    if (rd16(d + 6) != HEADER_SIZE)
        return fail(pk, err, err_size, "unexpected header size");
    pk->n_targets = rd32(d + 8);
    pk->n_facings = rd32(d + 12);
    pk->n_blobs = rd32(d + 16);
    pk->off_targets = rd32(d + 20);
    off_facings = rd32(d + 24);
    off_blobs = rd32(d + 28);
    pk->off_strings = rd32(d + 32);
    pk->strings_size = rd32(d + 36);
    off_u16 = rd32(d + 40);
    u16_size = rd32(d + 44);
    off_pal = rd32(d + 48);
    pal_size = rd32(d + 52);

#define RANGE_OK(off, len) ((uint64_t)(off) + (uint64_t)(len) <= (uint64_t)size)
    if (!RANGE_OK(pk->off_targets, (uint64_t)pk->n_targets * TARGET_SIZE) ||
        !RANGE_OK(off_facings, (uint64_t)pk->n_facings * FACING_SIZE) ||
        !RANGE_OK(off_blobs, (uint64_t)pk->n_blobs * BLOB_SIZE) || !RANGE_OK(pk->off_strings, pk->strings_size) ||
        !RANGE_OK(off_u16, u16_size) || !RANGE_OK(off_pal, pal_size) || (off_u16 & 1u) || (off_pal & 3u) ||
        (u16_size & 1u) || (pal_size & 3u) || pk->n_targets > 32767 || pk->n_facings > 32767)
        return fail(pk, err, err_size, "pack tables out of range");
    pk->info = str_at(pk, rd32(d + 56));
    if (!pk->info)
        return fail(pk, err, err_size, "bad info string");
    u16 = (const uint16_t *)(d + off_u16);
    n_u16 = u16_size / 2;
    pal = (const uint32_t *)(d + off_pal);
    n_pal = pal_size / 4;

    pk->blobs = (struct BslBlob *)calloc(pk->n_blobs ? pk->n_blobs : 1, sizeof *pk->blobs);
    pk->facings = (BslFacingInfo *)calloc(pk->n_facings ? pk->n_facings : 1, sizeof *pk->facings);
    pk->facing_blob = (uint32_t *)calloc(pk->n_facings ? pk->n_facings : 1, sizeof *pk->facing_blob);
    if (!pk->blobs || !pk->facings || !pk->facing_blob)
        return fail(pk, err, err_size, "out of memory");
    for (i = 0; i < pk->n_blobs; i++) {
        const uint8_t *b = d + off_blobs + i * BLOB_SIZE;
        struct BslBlob *bl = &pk->blobs[i];
        bl->offset = rd32(b);
        bl->size = rd32(b + 4);
        bl->raw_size = rd32(b + 8);
        bl->crc = rd32(b + 12);
        if (!RANGE_OK(bl->offset, bl->size) || bl->size < 2 || bl->size > 0x7FFFFFFFu || bl->raw_size > 0x7FFFFFFFu)
            return fail(pk, err, err_size, "blob out of range");
    }
    for (i = 0; i < pk->n_facings; i++) {
        const uint8_t *f = d + off_facings + i * FACING_SIZE;
        BslFacingInfo *fi = &pk->facings[i];
        uint32_t seq_off, dur_off, pn, ps, blob, k, total = 0;
        fi->frame_width = rd16(f);
        fi->frame_height = rd16(f + 2);
        fi->unique_frames = rd16(f + 4);
        fi->steps = rd16(f + 6);
        fi->anchor_x = (int16_t)rd16(f + 8);
        fi->anchor_y = (int16_t)rd16(f + 10);
        fi->loop_ms = rd32(f + 12);
        seq_off = rd32(f + 16);
        dur_off = rd32(f + 20);
        pn = rd32(f + 24);
        ps = rd32(f + 28);
        fi->colours = rd16(f + 32);
        fi->flags = rd16(f + 34);
        blob = rd32(f + 36);
        fi->source = str_at(pk, rd32(f + 40));
        if (!fi->frame_width || !fi->frame_height || !fi->unique_frames || !fi->steps || !fi->source)
            return fail(pk, err, err_size, "bad facing record");
        if ((uint64_t)seq_off + fi->steps > n_u16 || (uint64_t)dur_off + fi->steps > n_u16)
            return fail(pk, err, err_size, "facing timing out of range");
        if (!fi->colours || fi->colours > 256 || (uint64_t)pn + fi->colours > n_pal || (uint64_t)ps + fi->colours > n_pal)
            return fail(pk, err, err_size, "facing palette out of range");
        if (blob >= pk->n_blobs ||
            (uint64_t)fi->frame_width * fi->unique_frames * fi->frame_height != pk->blobs[blob].raw_size)
            return fail(pk, err, err_size, "facing blob mismatch");
        fi->sequence = u16 + seq_off;
        fi->durations_ms = u16 + dur_off;
        fi->palette_normal = pal + pn;
        fi->palette_shiny = pal + ps;
        for (k = 0; k < fi->steps; k++) {
            if (fi->sequence[k] >= fi->unique_frames)
                return fail(pk, err, err_size, "sequence refers to a missing frame");
            total += fi->durations_ms[k];
        }
        if (total != fi->loop_ms || (fi->steps > 1 && total == 0))
            return fail(pk, err, err_size, "durations do not add up to loop_ms");
        pk->facing_blob[i] = blob;
    }
    for (i = 0; i < pk->n_targets; i++) {
        const uint8_t *t = d + pk->off_targets + i * TARGET_SIZE;
        int k;
        const char *id = str_at(pk, rd32(t));
        if (!id || !str_at(pk, rd32(t + 4)))
            return fail(pk, err, err_size, "bad target strings");
        if (i > 0 && strcmp(str_at(pk, rd32(t - TARGET_SIZE)), id) >= 0)
            return fail(pk, err, err_size, "targets not sorted");
        for (k = 0; k < 4; k++) {
            int16_t fx = (int16_t)rd16(t + 8 + 2 * k);
            if (fx < -1 || fx >= (int32_t)pk->n_facings)
                return fail(pk, err, err_size, "target facing index out of range");
        }
    }
#undef RANGE_OK
    return pk;
}

BslPack *bsl_open_file(const char *path, char *err, size_t err_size) {
    FILE *fp = fopen(path, "rb");
    long n;
    uint8_t *buf;
    BslPack *pk;
    if (!fp) {
        set_err(err, err_size, "cannot open pack file");
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        set_err(err, err_size, "cannot size pack file");
        return NULL;
    }
    buf = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (!buf) {
        fclose(fp);
        set_err(err, err_size, "out of memory");
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        free(buf);
        set_err(err, err_size, "cannot read pack file");
        return NULL;
    }
    fclose(fp);
    pk = bsl_open_memory(buf, (size_t)n, 0, err, err_size);
    if (!pk) {
        free(buf);
        return NULL;
    }
    if (pk->owned)
        free(buf);      /* bsl_open_memory made its own aligned copy */
    else
        pk->owned = buf; /* used in place: the pack owns the buffer now */
    return pk;
}

void bsl_close(BslPack *pk) {
    if (!pk)
        return;
    free(pk->owned);
    free(pk->facings);
    free(pk->facing_blob);
    free(pk->blobs);
    free(pk->species);
    free(pk);
}

const char *bsl_info(const BslPack *pk) { return pk ? pk->info : NULL; }
uint32_t bsl_target_count(const BslPack *pk) { return pk ? pk->n_targets : 0; }
uint32_t bsl_facing_count(const BslPack *pk) { return pk ? pk->n_facings : 0; }
size_t bsl_pack_size(const BslPack *pk) { return pk ? pk->size : 0; }

static const uint8_t *target_rec(const BslPack *pk, int target) {
    if (!pk || target < 0 || (uint32_t)target >= pk->n_targets)
        return NULL;
    return pk->data + pk->off_targets + (uint32_t)target * TARGET_SIZE;
}

const char *bsl_target_id(const BslPack *pk, int target) {
    const uint8_t *t = target_rec(pk, target);
    return t ? str_at(pk, rd32(t)) : NULL;
}

const char *bsl_target_constant(const BslPack *pk, int target) {
    const uint8_t *t = target_rec(pk, target);
    return t ? str_at(pk, rd32(t + 4)) : NULL;
}

int bsl_find_target(const BslPack *pk, const char *id) {
    int lo = 0, hi;
    if (!pk || !id)
        return -1;
    hi = (int)pk->n_targets - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(bsl_target_id(pk, mid), id);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

int bsl_bind_species(BslPack *pk, const struct BslSpeciesName *names, size_t count, uint32_t num_species) {
    size_t i;
    int bound = 0;
    if (!pk || !names || !num_species)
        return 0;
    free(pk->species);
    pk->species = (int16_t *)malloc(num_species * sizeof *pk->species);
    if (!pk->species) {
        pk->n_species = 0;
        return 0;
    }
    pk->n_species = num_species;
    for (i = 0; i < num_species; i++)
        pk->species[i] = -1;
    for (i = 0; i < count; i++) {
        int t;
        if (names[i].species >= num_species)
            continue;
        t = bsl_find_target(pk, names[i].id);
        if (t >= 0) {
            pk->species[names[i].species] = (int16_t)t;
            bound++;
        }
    }
    return bound;
}

int bsl_target_for_species(const BslPack *pk, uint32_t species) {
    if (!pk || !pk->species || species >= pk->n_species)
        return -1;
    return pk->species[species];
}

int bsl_select(const BslPack *pk, int target, int back, int female) {
    const uint8_t *t = target_rec(pk, target);
    int slot = back ? 1 : 0;
    if (!t)
        return -1;
    if (female) {
        int16_t f = (int16_t)rd16(t + 8 + 2 * (slot + 2));
        if (f >= 0)
            return f;
    }
    return (int16_t)rd16(t + 8 + 2 * slot);
}

int bsl_select_species(const BslPack *pk, uint32_t species, int back, int female) {
    return bsl_select(pk, bsl_target_for_species(pk, species), back, female);
}

const BslFacingInfo *bsl_facing(const BslPack *pk, int facing_index) {
    if (!pk || facing_index < 0 || (uint32_t)facing_index >= pk->n_facings)
        return NULL;
    return &pk->facings[facing_index];
}

uint32_t bsl_step_at(const BslFacingInfo *f, uint32_t t_ms) {
    uint32_t k, acc = 0;
    if (!f || f->steps <= 1 || !f->loop_ms)
        return 0;
    t_ms %= f->loop_ms;
    for (k = 0; k < f->steps; k++) {
        acc += f->durations_ms[k];
        if (t_ms < acc)
            return k;
    }
    return (uint32_t)f->steps - 1;
}

uint32_t bsl_frame_at(const BslFacingInfo *f, uint32_t t_ms) {
    return f ? f->sequence[bsl_step_at(f, t_ms)] : 0;
}

uint32_t bsl_phase_ms(const BslFacingInfo *f, uint32_t seed) {
    uint32_t h = seed;   /* murmur3 finaliser */
    if (!f || !f->loop_ms)
        return 0;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h % f->loop_ms;
}

void bsl_free(void *p) { free(p); }

int bsl_decode_indices(const BslPack *pk, int facing_index, uint8_t **out, size_t *out_size) {
    const struct BslBlob *bl;
    uint8_t *raw;
    if (out)
        *out = NULL;
    if (out_size)
        *out_size = 0;
    if (!pk || !out || facing_index < 0 || (uint32_t)facing_index >= pk->n_facings)
        return -1;
    bl = &pk->blobs[pk->facing_blob[facing_index]];
#ifdef BSL_USE_ZLIB
    {
        uLongf n = bl->raw_size;
        raw = (uint8_t *)malloc(bl->raw_size ? bl->raw_size : 1);
        if (!raw)
            return -1;
        if (uncompress(raw, &n, pk->data + bl->offset, bl->size) != Z_OK || n != bl->raw_size) {
            free(raw);
            return -1;
        }
    }
#else
    {
        int n = 0;
        char *p = stbi_zlib_decode_malloc_guesssize_headerflag((const char *)pk->data + bl->offset, (int)bl->size,
                                                               (int)bl->raw_size, &n, 1);
        if (!p)
            return -1;
        if ((uint32_t)n != bl->raw_size) {
            STBI_FREE(p);
            return -1;
        }
        /* hand out memory owned by malloc so bsl_free() works whatever STBI_MALLOC is */
        raw = (uint8_t *)malloc(bl->raw_size ? bl->raw_size : 1);
        if (!raw) {
            STBI_FREE(p);
            return -1;
        }
        memcpy(raw, p, bl->raw_size);
        STBI_FREE(p);
    }
#endif
    if (crc32_bytes(raw, bl->raw_size) != bl->crc) {
        free(raw);
        return -1;
    }
    *out = raw;
    if (out_size)
        *out_size = bl->raw_size;
    return 0;
}

int bsl_atlas_layout(const BslFacingInfo *f, uint32_t max_size, uint32_t *cols, uint32_t *rows, uint32_t *atlas_w,
                     uint32_t *atlas_h) {
    uint32_t c, r;
    if (!f || f->frame_width > max_size || f->frame_height > max_size)
        return -1;
    c = max_size / f->frame_width;
    if (c > f->unique_frames)
        c = f->unique_frames;
    r = (f->unique_frames + c - 1) / c;
    if ((uint64_t)r * f->frame_height > max_size)
        return -1;
    if (cols)
        *cols = c;
    if (rows)
        *rows = r;
    if (atlas_w)
        *atlas_w = c * f->frame_width;
    if (atlas_h)
        *atlas_h = r * f->frame_height;
    return 0;
}

void bsl_atlas_frame_xy(const BslFacingInfo *f, uint32_t cols, uint32_t frame, uint32_t *x, uint32_t *y) {
    if (!f || !cols) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    if (x)
        *x = (frame % cols) * f->frame_width;
    if (y)
        *y = (frame / cols) * f->frame_height;
}

void bsl_expand_frame_rgba(const BslFacingInfo *f, const uint8_t *strip, uint32_t frame, int shiny, uint8_t *dst,
                           size_t dst_stride) {
    const uint32_t *pal;
    uint32_t x, y, sw;
    if (!f || !strip || !dst || frame >= f->unique_frames)
        return;
    pal = shiny ? f->palette_shiny : f->palette_normal;
    sw = (uint32_t)f->frame_width * f->unique_frames;
    for (y = 0; y < f->frame_height; y++) {
        const uint8_t *src = strip + (size_t)y * sw + (size_t)frame * f->frame_width;
        uint8_t *row = dst + (size_t)y * dst_stride;
        for (x = 0; x < f->frame_width; x++) {
            uint8_t v = src[x];
            uint32_t c = v < f->colours ? pal[v] : 0u;  /* out-of-palette index -> transparent */
            if (v == 0)
                c = 0u;
            memcpy(row + 4 * x, &c, 4);
        }
    }
}

int bsl_frame_bounds(const BslFacingInfo *f, const uint8_t *strip, uint32_t frame, uint16_t *x0, uint16_t *y0,
                     uint16_t *x1, uint16_t *y1) {
    uint32_t x, y, sw, minx, miny, maxx = 0, maxy = 0;
    int any = 0;
    if (!f || !strip || frame >= f->unique_frames)
        return -1;
    sw = (uint32_t)f->frame_width * f->unique_frames;
    minx = f->frame_width;
    miny = f->frame_height;
    for (y = 0; y < f->frame_height; y++) {
        const uint8_t *row = strip + (size_t)y * sw + (size_t)frame * f->frame_width;
        for (x = 0; x < f->frame_width; x++) {
            if (row[x] && row[x] < f->colours) {
                any = 1;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (!any)
        return -1;
    if (x0) *x0 = (uint16_t)minx;
    if (y0) *y0 = (uint16_t)miny;
    if (x1) *x1 = (uint16_t)(maxx + 1);
    if (y1) *y1 = (uint16_t)(maxy + 1);
    return 0;
}

static int build_atlas(const BslFacingInfo *f, const uint8_t *strip, int shiny, int rgba, uint32_t max_size,
                       uint8_t **out, uint32_t *aw, uint32_t *ah, uint32_t *cols) {
    uint32_t c, r, w, h, k, bpp = rgba ? 4u : 1u;
    uint8_t *buf;
    if (out)
        *out = NULL;
    if (!f || !strip || !out || bsl_atlas_layout(f, max_size, &c, &r, &w, &h) != 0)
        return -1;
    buf = (uint8_t *)calloc((size_t)w * h, bpp);
    if (!buf)
        return -1;
    for (k = 0; k < f->unique_frames; k++) {
        uint32_t x0, y0;
        bsl_atlas_frame_xy(f, c, k, &x0, &y0);
        if (rgba) {
            bsl_expand_frame_rgba(f, strip, k, shiny, buf + ((size_t)y0 * w + x0) * 4, (size_t)w * 4);
        } else {
            uint32_t y, sw = (uint32_t)f->frame_width * f->unique_frames;
            for (y = 0; y < f->frame_height; y++)
                memcpy(buf + (size_t)(y0 + y) * w + x0, strip + (size_t)y * sw + (size_t)k * f->frame_width,
                       f->frame_width);
        }
    }
    *out = buf;
    if (aw)
        *aw = w;
    if (ah)
        *ah = h;
    if (cols)
        *cols = c;
    return 0;
}

int bsl_build_atlas_rgba(const BslFacingInfo *f, const uint8_t *strip, int shiny, uint32_t max_size, uint8_t **out,
                         uint32_t *atlas_w, uint32_t *atlas_h, uint32_t *cols) {
    return build_atlas(f, strip, shiny, 1, max_size, out, atlas_w, atlas_h, cols);
}

int bsl_build_atlas_indices(const BslFacingInfo *f, const uint8_t *strip, uint32_t max_size, uint8_t **out,
                            uint32_t *atlas_w, uint32_t *atlas_h, uint32_t *cols) {
    return build_atlas(f, strip, 0, 0, max_size, out, atlas_w, atlas_h, cols);
}
