/*
 * Animated battle sprites: see include/platform/battle_sprites.h. Project-owned.
 *
 * Three steps, all reading what the game itself did:
 *  1. Game thread, inside the battle code (hooks under ENABLE_BATTLE_SPRITES): what each
 *     battler's picture shows - species, personality, shininess, its 16 colours and the
 *     picture's opaque box - when the game loads it; the Substitute doll; a move's copy of
 *     the picture on BG1/BG2; a fainting foe's sinking line.
 *  2. Main thread, as the VBlank handler loads OAM (LoadOam): that state latched with the
 *     battler sprites' OAM tiles, so it matches the OAM, palette and VRAM the next picture is
 *     drawn from (sdl2.c draws the frame before the VBlank handler runs).
 *  3. Main thread, at the start of each picture (DrawFrame): per OAM entry and per BG, the
 *     library frame to draw instead (gBattleSpriteObjViews, gBattleSpriteBgCopies), its colours
 *     faded as the entry's palette is faded.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "main.h"
#include "pokemon.h"
#include "sprite.h"
#include "constants/species.h"
#include "platform/battle_sprites.h"
#include "platform/battle_sprite_library.h"
#include "platform/png_write.h"
#ifdef ENABLE_DIORAMA
#include "diorama/battle_scene.h"
#endif

/* The generated SPECIES_* -> library target table (the runtime export's bsl_species_map.h). */
#include "bsl_species_map.h"

#define BS_BATTLERS 4
#define BS_VARIANTS 4       /* palettes one battler is drawn in within a frame (its own, a copy's...) */
#define BS_STRIPS 8         /* decoded facings kept */
#define BS_NO_SINK INT32_MAX
#define GBA_FRAME_NUM 10000000u  /* ms per frame = 1e7 / 597275 (59.7275 Hz) */
#define GBA_FRAME_DEN 597275u

/* What a battler's picture shows, as the game loaded it. */
struct Look
{
    bool valid;            /* a Pokemon's picture (NoteMon) */
    bool substitute;       /* the Substitute doll instead */
    bool shiny, female, back;
    uint16_t species;      /* as drawn: after Transform, Unown's letter resolved */
    uint32_t personality;
    uint16_t palette[16];  /* the picture's own colours, before any tint */
    int8_t gx0, gy0, gx1, gy1; /* frame 0's opaque box in the 64x64 picture (x1, y1 exclusive) */
    uint32_t serial;       /* new for every look */
};

struct MonBg
{
    int8_t battler;        /* -1: none */
    uint16_t firstTile;
    uint8_t paletteId;
};

/* Written by the game thread's hooks. */
static struct
{
    struct Look looks[BS_BATTLERS];
    struct MonBg monBg[4];
    int32_t sink[BS_BATTLERS];
    uint32_t serial;
} sPending;

/* Latched with OAM (LoadOam). */
static struct
{
    bool battleScreen;
    struct Look looks[BS_BATTLERS];
    int16_t tileNum[BS_BATTLERS]; /* the battler sprite's OAM tiles; -1: no usable sprite */
    int16_t restTop[BS_BATTLERS]; /* its 64x64 box's top-left on the screen at rest (no x2/y2) */
    int16_t restLeft[BS_BATTLERS];
    struct MonBg monBg[4];
    int32_t sink[BS_BATTLERS];
} sLatched;

struct Strip
{
    int facing;            /* -1: free */
    uint8_t *pixels;
    uint32_t lastUse;
    int16_t bx0, by0, bx1, by1; /* opaque box over all frames */
};

/* A battler's look resolved against the pack. */
struct Active
{
    uint32_t serial;       /* of the look resolved */
    bool resolved, usable;
    int facing;
    const BslFacingInfo *info;
    struct Strip *strip;
    int16_t left, top;     /* frame position in the GBA box */
    uint32_t phase;
    const char *why;       /* when not usable */
};

static bool sInit, sOff;
static BslPack *sPack;
static struct Strip sStrips[BS_STRIPS];
static struct Active sActive[BS_BATTLERS];
static uint32_t sFrames, sBattleFrames, sStripClock;
static bool sLog;
static FILE *sTrace;
/* Costs (PKMN_BATTLE_SPRITES_LOG): the whole software picture of a battle frame, the frame setup
 * here, and decoding facings, logged every 600 battle frames. */
static struct
{
    uint64_t begin, pictureSum, pictureMax, setupSum, decodeSum, decodeMax;
    uint32_t frames, decodes;
} sCost;
static char sDumpStem[512];
static uint32_t sDumpFrom, sDumpEvery = 1, sDumpCount;
static int sDumpAfterLook;          /* dump this many frames after any battler's look changed */
static uint32_t sLookChangedAt, sLookSerials[BS_BATTLERS];

/* This frame's views: per battler, one per palette it is drawn in. */
static struct BattleSpriteView sViews[BS_BATTLERS][BS_VARIANTS];
static const uint16_t *sViewSource[BS_BATTLERS][BS_VARIANTS];
static uint16_t sViewColors[BS_BATTLERS][BS_VARIANTS][256];
static float sViewFade[BS_BATTLERS][BS_VARIANTS];
static uint8_t sViewCount[BS_BATTLERS];
static struct BattleSpriteView sBase[BS_BATTLERS];
static uint16_t sBaseFrame[BS_BATTLERS];
static bool sBaseUsable[BS_BATTLERS];
static uint32_t sPaletteRefusals[BS_BATTLERS];

const struct BattleSpriteView *gBattleSpriteObjViews[128];
struct BattleSpriteBgCopy gBattleSpriteBgCopies[4];
struct BattleSpriteOverflow gBattleSpriteOverflow[BATTLE_SPRITE_OVERFLOW_MAX];
unsigned gBattleSpriteOverflowCount;
uint32_t gBattleSpriteOverflowSerial;

static int EnvInt(const char *name, int fallback)
{
    const char *value = SDL_getenv(name);
    return value != NULL && value[0] != '\0' ? atoi(value) : fallback;
}

/* ── Game thread ───────────────────────────────────────────────────────── */

/* The opaque box of a 64x64 4bpp picture (1D tile order, 8 tiles a row). */
static void PicBox(const uint8_t *pic, struct Look *look)
{
    int x, y, x0 = 64, y0 = 64, x1 = 0, y1 = 0;

    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
        {
            uint8_t byte = pic[((y >> 3) * 8 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];

            if (((x & 1) ? byte >> 4 : byte & 15) == 0)
                continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x + 1 > x1) x1 = x + 1;
            if (y + 1 > y1) y1 = y + 1;
        }
    if (x1 <= x0)
        x0 = y0 = x1 = y1 = 0;
    look->gx0 = (int8_t)x0;
    look->gy0 = (int8_t)y0;
    look->gx1 = (int8_t)x1;
    look->gy1 = (int8_t)y1;
}

void BattleSprites_NoteMon(uint32_t battler, uint32_t species, uint32_t personality, bool shiny,
                           const uint16_t *palette, const void *pic)
{
    struct Look *look;

    if (battler >= BS_BATTLERS || palette == NULL || pic == NULL)
        return;
    look = &sPending.looks[battler];
    species = SanitizeSpeciesId(species);
    if (species == SPECIES_UNOWN)
        species = GetUnownSpeciesId(personality);
    look->valid = true;
    look->substitute = false;
    look->species = (uint16_t)species;
    look->personality = personality;
    look->shiny = shiny;
    look->female = GetGenderFromSpeciesAndPersonality(species, personality) == MON_FEMALE;
    look->back = GetBattlerSide(battler) == B_SIDE_PLAYER;
    memcpy(look->palette, palette, sizeof(look->palette));
    PicBox(pic, look);
    look->serial = ++sPending.serial;
    sPending.sink[battler] = BS_NO_SINK;
}

void BattleSprites_NoteSubstitute(uint32_t battler)
{
    if (battler >= BS_BATTLERS)
        return;
    sPending.looks[battler].substitute = true;
    sPending.looks[battler].serial = ++sPending.serial;
}

void BattleSprites_NoteMonBg(uint32_t battler, uint32_t bgId, uint32_t tilesOffset, uint32_t paletteId)
{
    if (bgId >= 4 || battler >= BS_BATTLERS)
        return;
    sPending.monBg[bgId].battler = (int8_t)battler;
    sPending.monBg[bgId].firstTile = (uint16_t)tilesOffset;
    sPending.monBg[bgId].paletteId = (uint8_t)paletteId;
}

void BattleSprites_NoteMonBgCleared(uint32_t bgId)
{
    if (bgId < 4)
        sPending.monBg[bgId].battler = -1;
}

void BattleSprites_NoteSink(uint32_t battler, int32_t lineY)
{
    if (battler < BS_BATTLERS)
        sPending.sink[battler] = lineY;
}

/* ── Latch (LoadOam) ───────────────────────────────────────────────────── */

static void ClearPending(void)
{
    unsigned i;

    for (i = 0; i < BS_BATTLERS; i++)
    {
        sPending.looks[i].valid = false;
        sPending.looks[i].substitute = false;
        sPending.sink[i] = BS_NO_SINK;
    }
    for (i = 0; i < 4; i++)
        sPending.monBg[i].battler = -1;
}

void BattleSprites_LatchOam(void)
{
    unsigned b;

    if (!gMain.inBattle)
        ClearPending();
    sLatched.battleScreen = gMain.inBattle && gMain.callback2 == BattleMainCB2;
    memcpy(sLatched.looks, sPending.looks, sizeof(sLatched.looks));
    memcpy(sLatched.monBg, sPending.monBg, sizeof(sLatched.monBg));
    memcpy(sLatched.sink, sPending.sink, sizeof(sLatched.sink));
    for (b = 0; b < BS_BATTLERS; b++)
    {
        const struct Sprite *sprite;
        unsigned id;

        sLatched.tileNum[b] = -1;
        if (!sLatched.battleScreen || b >= gBattlersCount || !sLatched.looks[b].valid)
            continue;
        id = gBattlerSpriteIds[b];
        if (id >= MAX_SPRITES)
            continue;
        sprite = &gSprites[id];
        /* A Pokemon's battle sprite: made from the multiuse template with the battler's own
         * palette slot (battle_controllers.c); a trainer's has a tag palette (slot 4 up). */
        if (!sprite->inUse || sprite->template != &gMultiuseSpriteTemplate
         || sprite->oam.paletteNum != b || sprite->oam.shape != SPRITE_SHAPE(64x64)
         || sprite->oam.size != SPRITE_SIZE(64x64))
            continue;
        sLatched.tileNum[b] = (int16_t)sprite->oam.tileNum;
        sLatched.restTop[b] = (int16_t)(sprite->y + sprite->centerToCornerVecY);
        sLatched.restLeft[b] = (int16_t)(sprite->x + sprite->centerToCornerVecX);
    }
}

/* ── The pack ──────────────────────────────────────────────────────────── */

static void Init(void)
{
    char err[256] = "";
    const char *path = SDL_getenv("PKMN_BATTLE_SPRITES_PACK");
    const char *value;
    char *base = NULL, alt[1024];
    int bound;

    sInit = true;
    ClearPending();
    memset(&sLatched, 0, sizeof(sLatched));
    for (unsigned i = 0; i < 4; i++)
        sLatched.monBg[i].battler = -1;
    for (unsigned i = 0; i < BS_STRIPS; i++)
        sStrips[i].facing = -1;
    sLog = EnvInt("PKMN_BATTLE_SPRITES_LOG", 0) != 0;
    value = SDL_getenv("PKMN_BATTLE_SPRITES_TRACE");
    if (value != NULL && value[0] != '\0' && (sTrace = fopen(value, "w")) != NULL)
        setvbuf(sTrace, NULL, _IOLBF, 1 << 16); /* whole lines: a test may end the process at any time */
    value = SDL_getenv("PKMN_BATTLE_SPRITES_DUMP");
    if (value != NULL && value[0] != '\0')
    {
        SDL_strlcpy(sDumpStem, value, sizeof(sDumpStem));
        sDumpFrom = (uint32_t)EnvInt("PKMN_BATTLE_SPRITES_DUMP_FROM", 1);
        sDumpEvery = (uint32_t)EnvInt("PKMN_BATTLE_SPRITES_DUMP_EVERY", 1);
        sDumpCount = (uint32_t)EnvInt("PKMN_BATTLE_SPRITES_DUMP_COUNT", 1);
        sDumpAfterLook = EnvInt("PKMN_BATTLE_SPRITES_DUMP_AFTER_LOOK", 0);
        if (sDumpEvery == 0)
            sDumpEvery = 1;
    }
    /* Off: the GBA sprites are drawn; the pack is still read when a test traces what it would
     * have drawn (the reference run of a comparison). */
    sOff = EnvInt("PKMN_BATTLE_SPRITES", 1) == 0;
    if (sOff)
        SDL_Log("Battle sprites: off (PKMN_BATTLE_SPRITES=0): the GBA sprites are drawn");
    if (sOff && sTrace == NULL)
        return;
    if (path == NULL || path[0] == '\0')
    {
        path = "battle-sprites.ebsp";
        sPack = bsl_open_file(path, err, sizeof(err));
        if (sPack == NULL && (base = SDL_GetBasePath()) != NULL)
        {
            SDL_snprintf(alt, sizeof(alt), "%sbattle-sprites.ebsp", base);
            SDL_free(base);
            path = alt;
            sPack = bsl_open_file(path, err, sizeof(err));
        }
    }
    else
        sPack = bsl_open_file(path, err, sizeof(err));
    if (sPack == NULL)
    {
        SDL_Log("Battle sprites: no pack (%s): the GBA sprites are drawn", err);
        return;
    }
    bound = bsl_bind_species(sPack, gBslSpeciesNames, sizeof(gBslSpeciesNames) / sizeof(gBslSpeciesNames[0]),
                             NUM_SPECIES);
    SDL_Log("Battle sprites: %s, %u bytes: %u targets, %u facings; %d of the game's species have a library target",
            path, (unsigned)bsl_pack_size(sPack), bsl_target_count(sPack), bsl_facing_count(sPack), bound);
}

/* A facing's strip, decoded once and kept while it is in use (the least recently used goes). */
static struct Strip *GetStrip(int facing)
{
    const BslFacingInfo *info = bsl_facing(sPack, facing);
    struct Strip *strip = NULL;
    uint8_t *pixels = NULL;
    size_t size = 0, stride;
    unsigned i, x, y;
    int x0, y0, x1, y1;

    for (i = 0; i < BS_STRIPS; i++)
        if (sStrips[i].facing == facing)
        {
            sStrips[i].lastUse = ++sStripClock;
            return &sStrips[i];
        }
    {
        uint64_t t0 = SDL_GetPerformanceCounter(), dt;
        int failed = info == NULL || bsl_decode_indices(sPack, facing, &pixels, &size) != 0;

        dt = SDL_GetPerformanceCounter() - t0;
        sCost.decodeSum += dt;
        if (dt > sCost.decodeMax)
            sCost.decodeMax = dt;
        sCost.decodes++;
        if (failed)
            return NULL;
    }
    for (i = 0; i < BS_STRIPS; i++)
    {
        bool busy = false;

        for (unsigned b = 0; b < BS_BATTLERS; b++)
            if (sActive[b].usable && sActive[b].strip == &sStrips[i])
                busy = true;
        if (busy)
            continue;
        if (strip == NULL || sStrips[i].facing < 0 || sStrips[i].lastUse < strip->lastUse)
            strip = &sStrips[i];
        if (sStrips[i].facing < 0)
            break;
    }
    if (strip == NULL)
    {
        bsl_free(pixels);
        return NULL;
    }
    if (strip->pixels != NULL)
        bsl_free(strip->pixels);
    strip->facing = facing;
    strip->pixels = pixels;
    strip->lastUse = ++sStripClock;
    stride = (size_t)info->frame_width * info->unique_frames;
    x0 = info->frame_width, y0 = info->frame_height, x1 = 0, y1 = 0;
    for (y = 0; y < info->frame_height; y++)
        for (x = 0; x < stride; x++)
            if (pixels[y * stride + x] != 0)
            {
                int fx = (int)(x % info->frame_width);

                if (fx < x0) x0 = fx;
                if (fx + 1 > x1) x1 = fx + 1;
                if ((int)y < y0) y0 = (int)y;
                if ((int)y + 1 > y1) y1 = (int)y + 1;
            }
    strip->bx0 = (int16_t)x0;
    strip->by0 = (int16_t)y0;
    strip->bx1 = (int16_t)x1;
    strip->by1 = (int16_t)y1;
    return strip;
}

static const char *SpeciesName(uint16_t species)
{
    int target = sPack != NULL ? bsl_target_for_species(sPack, species) : -1;

    return target >= 0 ? bsl_target_id(sPack, target) : "?";
}

/* The look against the pack: which facing draws it, or why the GBA sprite stays. */
static void Resolve(unsigned b)
{
    const struct Look *look = &sLatched.looks[b];
    struct Active *active = &sActive[b];
    int target;

    if (active->resolved && active->serial == look->serial)
        return;
    active->resolved = true;
    active->serial = look->serial;
    active->usable = false;
    active->strip = NULL;
    active->info = NULL;
    active->facing = -1;
    if (!look->valid)
        active->why = "no Pokemon picture";
    else if (look->substitute)
        active->why = "the Substitute doll";
    else if (look->gx1 <= look->gx0)
        active->why = "an empty GBA picture";
    else if ((target = bsl_target_for_species(sPack, look->species)) < 0)
        active->why = "species not in the pack";
    else if ((active->facing = bsl_select(sPack, target, look->back, look->female)) < 0)
        active->why = "facing not in the pack (permission-gated or missing art)";
    else if ((active->info = bsl_facing(sPack, active->facing)) == NULL)
        active->why = "pack error";
    else if (look->shiny && !(active->info->flags & BSL_FLAG_SHINY))
        active->why = "no library shiny for this facing";
    else if ((active->strip = GetStrip(active->facing)) == NULL)
        active->why = "pack read error";
    else
    {
        const struct Strip *strip = active->strip;
        /* The frame placed in the GBA box so that the two pictures stand on the same line
         * (the row under their lowest opaque pixels: the library's feet anchor) and share a
         * horizontal centre (their opaque boxes'), halves rounded down. */
        int twice = look->gx0 + look->gx1 - strip->bx0 - strip->bx1;

        active->usable = true;
        active->why = NULL;
        active->left = (int16_t)(twice >= 0 ? twice / 2 : -((1 - twice) / 2));
        active->top = (int16_t)(look->gy1 - active->info->anchor_y);
        active->phase = bsl_phase_ms(active->info, look->personality ^ (b * 0x9E3779B9u));
    }
    if (sLog && look->valid)
    {
        if (active->usable)
            SDL_Log("Battle sprites: battler %u %s %s%s%s -> pack facing %d (%s, %ux%u, %u frames, %u ms loop), "
                    "at %d,%d in the box", b, SpeciesName(look->species), look->back ? "back" : "front",
                    look->shiny ? " shiny" : "", look->female ? " female" : "", active->facing,
                    active->info->source, active->info->frame_width, active->info->frame_height,
                    active->info->unique_frames, active->info->loop_ms, active->left, active->top);
        else
            SDL_Log("Battle sprites: battler %u species %u (%s) %s%s%s -> GBA sprite: %s", b, look->species,
                    SpeciesName(look->species), look->back ? "back" : "front", look->shiny ? " shiny" : "",
                    look->female ? " female" : "", active->why);
    }
}

/* ── Colours ───────────────────────────────────────────────────────────── */

static float Channel(uint16_t color, unsigned shift)
{
    return (float)((color >> shift) & 31);
}

/*
 * The fade that turns the picture's own colours into the ones shown, shown = keep * own +
 * (1 - keep) * target, by least squares over colours 1-15 (battle_capture.c's FadeFit): a palette
 * fade, a move's blend towards a colour, Transform's pale tint. False when the colours are changed
 * some other way (an inversion, a greyscale, another picture's palette): the GBA sprite is drawn.
 */
static bool FitFade(const uint16_t *shown, const uint16_t *own, float *amount, float rgb[3])
{
    float n = 15.0f, cov = 0.0f, var = 0.0f, keep;
    float su[3] = {0}, sf[3] = {0}, suu[3] = {0}, suf[3] = {0};
    unsigned i, c;

    for (i = 1; i < 16; i++)
        for (c = 0; c < 3; c++)
        {
            float u = Channel(own[i], c * 5), f = Channel(shown[i], c * 5);

            su[c] += u;
            sf[c] += f;
            suu[c] += u * u;
            suf[c] += u * f;
        }
    for (c = 0; c < 3; c++)
    {
        cov += n * suf[c] - su[c] * sf[c];
        var += n * suu[c] - su[c] * su[c];
    }
    keep = var > 1.0f ? cov / var : 0.0f;
    if (keep < 0.0f) keep = 0.0f;
    if (keep > 1.0f) keep = 1.0f;
    *amount = 1.0f - keep;
    for (c = 0; c < 3; c++)
    {
        float offset = (sf[c] - keep * su[c]) / n;
        float value = *amount > 0.01f ? offset / *amount : 0.0f;

        if (value < 0.0f) value = 0.0f;
        if (value > 31.0f) value = 31.0f;
        rgb[c] = value;
    }
    /* Every colour explained to within the game's own rounding. */
    for (i = 1; i < 16; i++)
        for (c = 0; c < 3; c++)
        {
            float u = Channel(own[i], c * 5), f = Channel(shown[i], c * 5);
            float predicted = u + (rgb[c] - u) * *amount;

            if (predicted - f > 1.6f || f - predicted > 1.6f)
                return false;
        }
    return true;
}

/* The battler's view in the colours of `shown` (a palette slot of 16), NULL when they are not a
 * fade of its own. */
static const struct BattleSpriteView *ViewFor(unsigned b, const uint16_t *shown)
{
    const struct BattleSpriteView *base = &sBase[b];
    const BslFacingInfo *info = sActive[b].info;
    const uint32_t *palette;
    unsigned k, i, n;
    float amount = 0.0f, rgb[3] = {0};
    struct BattleSpriteView *view;
    uint16_t *colors;
    bool same;

    for (k = 0; k < sViewCount[b]; k++)
        if (sViewSource[b][k] == shown)
            return sViewFade[b][k] < 0.0f ? NULL : &sViews[b][k];
    if (sViewCount[b] >= BS_VARIANTS)
        return NULL;
    k = sViewCount[b]++;
    sViewSource[b][k] = shown;
    same = memcmp(shown + 1, sLatched.looks[b].palette + 1, 15 * sizeof(uint16_t)) == 0;
    if (!same && !FitFade(shown, sLatched.looks[b].palette, &amount, rgb))
    {
        sViewFade[b][k] = -1.0f;
        sPaletteRefusals[b]++;
        return NULL;
    }
    if (amount < 1.0f / 32.0f)
        amount = 0.0f;
    sViewFade[b][k] = amount;
    view = &sViews[b][k];
    *view = *base;
    colors = sViewColors[b][k];
    view->colors = colors;
    palette = sLatched.looks[b].shiny ? info->palette_shiny : info->palette_normal;
    n = info->colours < 256 ? info->colours : 256;
    for (i = 0; i < n; i++)
    {
        const uint8_t *rgba = (const uint8_t *)&palette[i];
        float c5[3];
        uint16_t out = 0;

        for (unsigned c = 0; c < 3; c++)
        {
            int v;

            c5[c] = (float)(rgba[c] * 31 + 127) / 255.0f;
            c5[c] = (float)(int)(c5[c]);
            c5[c] += (rgb[c] - c5[c]) * amount;
            v = (int)(c5[c] + 0.5f);
            if (v < 0) v = 0;
            if (v > 31) v = 31;
            out |= (uint16_t)(v << (c * 5));
        }
        colors[i] = out;
    }
    for (; i < 256; i++)
        colors[i] = 0;
    return view;
}

/* ── The frame ─────────────────────────────────────────────────────────── */

static void Trace(void)
{
    unsigned b, i;
    bool first = true;

    fprintf(sTrace, "{\"frame\": %u, \"battle_frame\": %u, \"now_ms\": %u, \"battlers\": [", sFrames, sBattleFrames,
            (uint32_t)((uint64_t)sFrames * GBA_FRAME_NUM / GBA_FRAME_DEN));
    for (b = 0; b < BS_BATTLERS; b++)
    {
        const struct Look *look = &sLatched.looks[b];

        if (!look->valid)
            continue;
        fprintf(sTrace, "%s{\"battler\": %u, \"species\": %u, \"id\": \"%s\", \"back\": %d, \"shiny\": %d, "
                        "\"female\": %d, \"tiles\": %d, \"gba_box\": [%d, %d, %d, %d]",
                first ? "" : ", ", b, look->species, SpeciesName(look->species), look->back, look->shiny,
                look->female, sLatched.tileNum[b], look->gx0, look->gy0, look->gx1, look->gy1);
        if (sActive[b].usable && !sBaseUsable[b])
            fprintf(sTrace, ", \"facing\": %d, \"source\": \"%s\", \"no_sprite\": 1", sActive[b].facing,
                    sActive[b].info->source);
        else if (sActive[b].usable)
            fprintf(sTrace, ", \"facing\": %d, \"source\": \"%s\", \"frame_index\": %u, \"phase\": %u, "
                            "\"left\": %d, \"top\": %d, \"size\": [%d, %d], \"clip\": %d, \"palette_refusals\": %u",
                    sActive[b].facing, sActive[b].info->source, sBaseFrame[b], sActive[b].phase, sBase[b].left,
                    sBase[b].top, sBase[b].width, sBase[b].height, sBase[b].clipBottom, sPaletteRefusals[b]);
        else
            fprintf(sTrace, ", \"gba\": \"%s\"", sActive[b].why != NULL ? sActive[b].why : "");
        fprintf(sTrace, "}");
        first = false;
    }
    fprintf(sTrace, "], \"obj\": [");
    first = true;
    for (i = 0; i < 128; i++)
    {
        const struct OamData *oam = &((const struct OamData *)OAM)[i];
        const struct OamData *m = &((const struct OamData *)OAM)[oam->matrixNum * 4];
        const struct BattleSpriteView *view = gBattleSpriteObjViews[i];

        for (b = 0; b < BS_BATTLERS; b++)
            if (sLatched.tileNum[b] >= 0 && oam->tileNum == (uint16_t)sLatched.tileNum[b] && oam->shape == 0
             && oam->size == 3 && ((oam->affineMode & 1) || !(oam->affineMode & 2)))
                break;
        if (b == BS_BATTLERS)
            continue;
        fprintf(sTrace, "%s{\"oam\": %u, \"battler\": %u, \"x\": %u, \"y\": %u, \"affine\": %u, \"priority\": %u, "
                        "\"palette\": %u, \"mode\": %u, \"mosaic\": %u, \"flip\": %u, \"pack\": %d",
                first ? "" : ", ", i, b, oam->x, oam->y, oam->affineMode, oam->priority, oam->paletteNum,
                oam->objMode, oam->mosaic, (oam->affineMode & 1) ? 0u : (unsigned)(oam->matrixNum >> 3), view != NULL);
        if (oam->affineMode & 1)
            fprintf(sTrace, ", \"matrix\": [%d, %d, %d, %d]", (int16_t)m[0].affineParam, (int16_t)m[1].affineParam,
                    (int16_t)m[2].affineParam, (int16_t)m[3].affineParam);
        if (view != NULL)
            fprintf(sTrace, ", \"fade\": %.3f", sViewFade[b][view - sViews[b]]);
        fprintf(sTrace, "}");
        first = false;
    }
    fprintf(sTrace, "], \"bg\": [");
    first = true;
    for (i = 1; i < 3; i++)
        if (gBattleSpriteBgCopies[i].view != NULL)
        {
            fprintf(sTrace, "%s{\"bg\": %u, \"battler\": %d}", first ? "" : ", ", i, sLatched.monBg[i].battler);
            first = false;
        }
    fprintf(sTrace, "]}\n");
}

/* The battler a view belongs to. */
static unsigned ViewBattler(const struct BattleSpriteView *view)
{
    unsigned b;

    for (b = 0; b < BS_BATTLERS; b++)
        if (view >= sViews[b] && view < sViews[b] + BS_VARIANTS)
            return b;
    return BS_BATTLERS;
}

/* The screen box a battler's frames cover at rest (x1, y1 exclusive); false when it lies inside
 * the picture (nothing to draw around it). */
static bool RestBox(unsigned b, int box[4])
{
    const struct Strip *strip = sActive[b].strip;

    box[0] = sLatched.restLeft[b] + sBase[b].left + strip->bx0;
    box[1] = sLatched.restTop[b] + sBase[b].top + strip->by0;
    box[2] = sLatched.restLeft[b] + sBase[b].left + strip->bx1;
    box[3] = sLatched.restTop[b] + sBase[b].top + strip->by1;
    return box[1] < 0 || box[0] < 0 || box[2] > DISPLAY_WIDTH;
}

static void AddOverflow(const struct BattleSpriteView *view, int x, int y, const struct OamData *oam,
                        const int box[4])
{
    struct BattleSpriteOverflow *o;
    unsigned blendMode = (REG_BLDCNT >> 6) & 3;

    if (gBattleSpriteOverflowCount >= BATTLE_SPRITE_OVERFLOW_MAX)
        return;
    o = &gBattleSpriteOverflow[gBattleSpriteOverflowCount++];
    memset(o, 0, sizeof(*o));
    o->view = view;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->restLeft = (int16_t)box[0];
    o->restTop = (int16_t)box[1];
    o->restRight = (int16_t)box[2];
    o->restBottom = (int16_t)box[3];
    o->alpha = 16;
    if (oam != NULL)
    {
        o->affine = oam->affineMode & 1;
        if (o->affine)
        {
            const struct OamData *m = &((const struct OamData *)OAM)[oam->matrixNum * 4];
            int half = (oam->affineMode & 2) ? 64 : 32;

            o->pa = (int16_t)m[0].affineParam;
            o->pb = (int16_t)m[1].affineParam;
            o->pc = (int16_t)m[2].affineParam;
            o->pd = (int16_t)m[3].affineParam;
            o->x = (int16_t)(x + half);
            o->y = (int16_t)(y + half);
        }
        else
        {
            o->flipX = (oam->matrixNum >> 3) & 1;
            o->flipY = (oam->matrixNum >> 4) & 1;
        }
        if (oam->objMode == 1)
        {
            unsigned eva = REG_BLDALPHA & 0x1F;

            o->alpha = (uint8_t)(eva > 16 ? 16 : eva);
        }
    }
    if ((REG_BLDCNT & BLDCNT_TGT1_OBJ) && (blendMode == 2 || blendMode == 3))
    {
        unsigned evy = REG_BLDY & 0x1F;

        o->brightness = (int8_t)((evy > 16 ? 16 : evy) * (blendMode == 2 ? 1 : -1));
    }
}

/* The library sprites whose frames reach outside the picture this frame, back to front (the
 * GBA's order: higher priority numbers first, then higher OAM indexes). */
static void Overflow(void)
{
    const struct OamData *oam = (const struct OamData *)OAM;
    int prio, i;

    for (prio = 3; prio >= 0; prio--)
    {
        for (i = 3; i >= 1; i--)
        {
            const struct BattleSpriteBgCopy *copy = &gBattleSpriteBgCopies[i];
            uint16_t control, hoffs, voffs;
            unsigned b;
            int box[4], x, y;

            if (i == 3 || copy->view == NULL)
                continue;
            control = *(uint16_t *)(REG_ADDR_BG0CNT + i * 2);
            if ((int)(control & 3) != prio)
                continue;
            b = ViewBattler(copy->view);
            if (b >= BS_BATTLERS || !RestBox(b, box))
                continue;
            /* the copy's block at the map's origin, on the screen through the layer's scroll */
            hoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + i * 4);
            voffs = *(uint16_t *)(REG_ADDR_BG0VOFS + i * 4);
            x = -(int)(hoffs & 0x1FF);
            y = -(int)(voffs & 0xFF);
            if (x < -256) x += 512;
            if (y < -128) y += 256;
            AddOverflow(copy->view, x, y, NULL, box);
        }
        for (i = 127; i >= 0; i--)
        {
            const struct BattleSpriteView *view = gBattleSpriteObjViews[i];
            unsigned b;
            int box[4], x, y;

            if (view == NULL || oam[i].priority != prio || oam[i].objMode == 2)
                continue;
            b = ViewBattler(view);
            if (b >= BS_BATTLERS || !RestBox(b, box))
                continue;
            x = oam[i].x;
            y = oam[i].y;
            if (x >= DISPLAY_WIDTH)
                x -= 512;
            if (y >= DISPLAY_HEIGHT)
                y -= 256;
            AddOverflow(view, x, y, &oam[i], box);
        }
    }
}

void BattleSprites_BeginFrame(void)
{
    const struct OamData *oam = (const struct OamData *)OAM;
    uint32_t nowMs;
    unsigned b, i;

    sCost.begin = SDL_GetPerformanceCounter();
    memset(gBattleSpriteObjViews, 0, sizeof(gBattleSpriteObjViews));
    memset(gBattleSpriteBgCopies, 0, sizeof(gBattleSpriteBgCopies));
    gBattleSpriteOverflowCount = 0;
    gBattleSpriteOverflowSerial++;
    if (!sInit)
        Init();
    sFrames++;
    if (sPack == NULL || !sLatched.battleScreen)
    {
        sBattleFrames = 0;
        return;
    }
    sBattleFrames++;
    for (b = 0; b < BS_BATTLERS; b++)
        if (sLatched.looks[b].valid && sLatched.looks[b].serial != sLookSerials[b])
        {
            sLookSerials[b] = sLatched.looks[b].serial;
            sLookChangedAt = sBattleFrames;
        }
    nowMs = (uint32_t)((uint64_t)sFrames * GBA_FRAME_NUM / GBA_FRAME_DEN);
    for (b = 0; b < BS_BATTLERS; b++)
    {
        const BslFacingInfo *info;
        uint32_t frame;

        sViewCount[b] = 0;
        sBaseUsable[b] = false;
        Resolve(b);
        if (!sActive[b].usable || sLatched.tileNum[b] < 0)
            continue;
        info = sActive[b].info;
        frame = bsl_frame_at(info, nowMs + sActive[b].phase);
        if (frame >= info->unique_frames)
            frame = 0;
        sBaseFrame[b] = (uint16_t)frame;
        sBase[b].pixels = sActive[b].strip->pixels + (size_t)frame * info->frame_width;
        sBase[b].stride = (uint32_t)info->frame_width * info->unique_frames;
        sBase[b].width = (int16_t)info->frame_width;
        sBase[b].height = (int16_t)info->frame_height;
        sBase[b].left = sActive[b].left;
        sBase[b].top = sActive[b].top;
        sBase[b].clipBottom = sLatched.sink[b] == BS_NO_SINK ? INT16_MAX
                            : (int16_t)(sLatched.sink[b] < -256 ? -256 : sLatched.sink[b] > 512 ? 512 : sLatched.sink[b]);
        sBase[b].colors = NULL;
        sBaseUsable[b] = true;
    }

    if (sOff)
    {
        for (b = 0; b < BS_BATTLERS; b++)
            sBaseUsable[b] = false;
        if (sTrace != NULL)
            Trace();
        return;
    }
    /* Every 64x64 entry on the battlers' tiles: the battler sprite, or a move's copy of it. */
    for (i = 0; i < 128; i++)
    {
        const struct OamData *e = &oam[i];

        if (e->shape != 0 || e->size != 3 || e->bpp)
            continue;
        if (!(e->affineMode & 1) && (e->affineMode & 2))
            continue;
        for (b = 0; b < BS_BATTLERS; b++)
            if (sBaseUsable[b] && e->tileNum == (uint16_t)sLatched.tileNum[b])
                break;
        if (b == BS_BATTLERS)
            continue;
        gBattleSpriteObjViews[i] = ViewFor(b, (const uint16_t *)(PLTT + 0x200) + e->paletteNum * 16);
    }

    /* A move's copy of a battler on BG1 or BG2 ("monbg"), while the layer still shows it. */
    for (i = 1; i < 3; i++)
    {
        const struct MonBg *copy = &sLatched.monBg[i];
        uint16_t control = *(uint16_t *)(REG_ADDR_BG0CNT + i * 2);
        const uint16_t *screen;

        if (copy->battler < 0 || !sBaseUsable[copy->battler])
            continue;
        if ((REG_DISPCNT & 7) != 0 || !(REG_DISPCNT & (0x100 << i)) || (control & BGCNT_256COLOR))
            continue;
        screen = (const uint16_t *)BG_SCREEN_ADDR((control >> 8) & 0x1F);
        if (screen[0] != (uint16_t)(copy->firstTile | (copy->paletteId << 12)))
            continue;
        gBattleSpriteBgCopies[i].view = ViewFor(copy->battler, (const uint16_t *)PLTT + copy->paletteId * 16);
        gBattleSpriteBgCopies[i].firstTile = copy->firstTile;
        gBattleSpriteBgCopies[i].paletteId = copy->paletteId;
    }
    Overflow();
    sCost.setupSum += SDL_GetPerformanceCounter() - sCost.begin;
    if (sTrace != NULL)
        Trace();
}

static void CostFrame(void)
{
    uint64_t picture = SDL_GetPerformanceCounter() - sCost.begin;
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();

    sCost.pictureSum += picture;
    if (picture > sCost.pictureMax)
        sCost.pictureMax = picture;
    if (++sCost.frames < 600)
        return;
    if (sLog)
        SDL_Log("Battle sprites: %s; the battle picture %.3f ms a frame (at most %.3f), of which the sprites' "
                "setup %.3f ms; %u facings decoded, %.2f ms each (at most %.2f)",
                sOff ? "off" : "on", sCost.pictureSum * ms / sCost.frames, sCost.pictureMax * ms,
                sCost.setupSum * ms / sCost.frames, sCost.decodes,
                sCost.decodes ? sCost.decodeSum * ms / sCost.decodes : 0.0, sCost.decodeMax * ms);
    memset(&sCost, 0, sizeof(sCost));
}

void BattleSprites_EndFrame(const uint16_t *pixels)
{
    static uint8_t rgba[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
    char path[600];
    uint32_t n;

    if (sBattleFrames > 0)
        CostFrame();

    if (sDumpStem[0] == '\0' || sBattleFrames == 0)
        return;
    if (sDumpAfterLook > 0)
    {
        if (sLookChangedAt == 0 || sBattleFrames != sLookChangedAt + (uint32_t)sDumpAfterLook)
            return;
    }
    else
    {
        if (sBattleFrames < sDumpFrom || sDumpCount == 0)
            return;
        n = sBattleFrames - sDumpFrom;
        if (n % sDumpEvery != 0 || n / sDumpEvery >= sDumpCount)
            return;
    }
    for (unsigned i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        uint16_t c = pixels[i];

        rgba[i * 4 + 0] = (uint8_t)((c & 31) * 255 / 31);
        rgba[i * 4 + 1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
        rgba[i * 4 + 2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
        rgba[i * 4 + 3] = 255;
#ifdef ENABLE_DIORAMA
        /* where the 3D world shows through the picture */
        if (gDioramaSceneLayers)
            rgba[i * 4 + 3] = (uint8_t)((16u - gDioramaSceneCover[i]) * 255u / 16u);
#endif
    }
    SDL_snprintf(path, sizeof(path), "%s-%04u.png", sDumpStem, sBattleFrames);
    PngWrite_RGBA(path, rgba, DISPLAY_WIDTH, DISPLAY_HEIGHT, false);
    if (sTrace != NULL)
        fflush(sTrace);
}
