#ifdef ENABLE_DIORAMA
/*
 * World detail objects: the catalogue and the instances (see diorama/props.h).
 * Project-owned (graphics overhaul, phase 2).
 *
 * The catalogue was made by looking at each tileset's metatiles and where the
 * maps use them (tools/diorama_objects: metatile_sheet.py, map_sheet.py): a
 * metatile is listed only where it is that object on every map using it.
 */
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "constants/weather.h"
#include "diorama/metatile_atlas.h"
#include "diorama/props.h"
#include "diorama/rules.h"
#include "diorama/rules.generated.h"

#define SECONDARY_ID 0x200

/* An entry: metatile, kind, form, art layer, ground; then the details (left
 * out: all 16 rows, 16 sixteenths tall, its own art). */
#define PROP(id, kind_, form_, layer_, ground_) \
    .metatileId = (id), .kind = DIORAMA_PROP_KIND_##kind_, .form = DIORAMA_PROP_##form_, \
    .layer = (layer_), .ground = DIORAMA_PROP_GROUND_##ground_

/* Tall grass: the blades (the grass between them left out) as two cards, a
 * little taller than drawn, so they reach a standing character's knees. */
#define TALL_GRASS(id) { PROP(id, GRASS, CARDS, 0, KEEP), .keyIndex = 0xD, .depth = 1, \
                         .height16 = 20, .flags = DIORAMA_PROP_SWAY }

static const struct DioramaProp sGeneral[] = {
    /* The metal sign (towns, routes): drawn alone on its layer, filling the
     * cell, so the ground under it is its neighbours'. */
    { PROP(0x003, SIGN, SLAB, 0, NEIGHBOUR), .depth = 3 },
    /* Flowers over grass: the grass between them left out. */
    { PROP(0x004, FLOWERS, CARDS, 1, LOWER), .keyIndex = 0xD, .depth = 1, .flags = DIORAMA_PROP_SWAY },
    TALL_GRASS(0x00D),
    /* Long grass (Routes 119, 120...): dense, as tall as a character. */
    { PROP(0x015, LONG_GRASS, CARDS, 0, KEEP), .keyIndex = 0xD, .depth = 1, .height16 = 30,
      .flags = DIORAMA_PROP_SWAY },
    /* The wooden sign: its painted shadow at the foot of the post left out. */
    { PROP(0x01B, SIGN, SLAB, 1, LOWER), .shadowIndex = 5, .shadowFromRow = 13, .depth = 2 },
    /* A rock on sand (Dewford, the beaches): its shadow ring left out. */
    { PROP(0x0E2, ROCK, RELIEF, 1, LOWER), .shadowIndex = 5, .shadowFromRow = 12, .depth = 12 },
    /* The grey stone fence (Petalburg Woods' gates, routes): posts and a bar,
     * the shadow painted under it left out. */
    { PROP(0x148, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3 },
    { PROP(0x149, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3 },
    /* The grey stone fence running north-south (a line of posts in the
     * art): the east-west fence's posts and bar turned to face east. */
    { PROP(0x140, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3, .facing = 1, .offsetX = -4, .artMetatile = 0x149 },
    { PROP(0x141, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3, .facing = 1, .offsetX = 4, .artMetatile = 0x149 },
    { PROP(0x142, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3, .facing = 1, .offsetX = 4, .artMetatile = 0x149 },
    /* ... its corner: the east-west part, the north-south post standing in it. */
    { PROP(0x14A, FENCE, SLAB, 1, LOWER), .artBottom = 13, .shadowIndex = 7, .shadowFromRow = 12,
      .depth = 3 },
    /* A rock of 2 x 2 cells in the sea (Rustboro, Slateport, the routes). */
    { PROP(0x150, ROCK, MOUND, 1, LOWER), .height16 = 14, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x151, ROCK, MOUND, 1, LOWER), .height16 = 14, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x158, ROCK, MOUND, 1, LOWER), .height16 = 14, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x159, ROCK, MOUND, 1, LOWER), .height16 = 14, .flags = DIORAMA_PROP_JOINED },
    /* A gym's sign: its top a cell north of the board and stand (Rustboro's
     * own stand is 0x2D4), the shadow round the stand left out. */
    { PROP(0x130, SIGN, SLAB, 1, LOWER), .depth = 2, .standOn = 1 },
    { PROP(0x131, SIGN, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 11, .depth = 2 },
};

/* An iron fence running north-south (a line in the art): the east-west
 * fence's railing turned to face east, along the line. */
#define IRON_FENCE_NS(id, x) { PROP(id, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, \
                               .shadowFromRow = 14, .depth = 2, .facing = 1, .offsetX = (x), \
                               .artMetatile = 0x2A7 }
#define IRON_FENCE_EW(id) { PROP(id, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, \
                            .depth = 2 }

static const struct DioramaProp sRustboro[] = {
    /* The street lamps: the head over the post, a cell further north. */
    { PROP(0x212, LAMP, SLAB, 1, LOWER), .depth = 3, .standOn = 1, .flags = DIORAMA_PROP_LIGHT },
    { PROP(0x21A, LAMP, SLAB, 1, LOWER), .shadowIndex = 5, .shadowFromRow = 11, .depth = 3 },
    IRON_FENCE_EW(0x2A7),
    IRON_FENCE_EW(0x2B7),
    /* The fence's ends and corners (phase 4): a run's last post (2BE), the
     * post where it turns north-south (2BF, 352), a run with a bush at its
     * foot (33F) - the east-west railing with its posts, as drawn. */
    IRON_FENCE_EW(0x2BE),
    IRON_FENCE_EW(0x2BF),
    IRON_FENCE_NS(0x2C6, -5),
    /* The gym sign's stand (its top is General's 0x130). */
    { PROP(0x2D4, SIGN, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 11, .depth = 2 },
    IRON_FENCE_NS(0x2DE, -5),
    IRON_FENCE_NS(0x2DF, 4),
    IRON_FENCE_NS(0x2EC, 4),
    IRON_FENCE_EW(0x2FC),
    IRON_FENCE_NS(0x315, -5),
    IRON_FENCE_NS(0x318, 3),
    IRON_FENCE_NS(0x319, -4),
    /* A north-south run's corner, the east-west stubs drawn beside it. */
    IRON_FENCE_NS(0x31A, -4),
    IRON_FENCE_NS(0x31B, 4),
    IRON_FENCE_NS(0x31D, 4),
    IRON_FENCE_EW(0x33F),
    IRON_FENCE_EW(0x352),
};

/* Slateport: the market's baskets, pots and crates, the round bushes by the
 * contest hall and the museum, the bollards on the pier. */
#define BASKET(id) { PROP(id, DECOR, MOUND, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 9, \
                     .height16 = 8 }
/* The market's stalls (phase 4): the drawing's canopy row lies on a roof two
 * rows deep over the stall's floor (a row and a half south of its cells),
 * higher at the back; the row under it, the canopy's front, hangs from the
 * roof's edge high enough to show the stallholder under it; the posts stand
 * at the front corners, as tall as the front's top. The cells show their
 * floor (the lower layer). */
#define STALL_ROOF(id) { PROP(id, CANOPY, BLOCK, 1, LOWER), .lift16 = 44, .height16 = 1, .depth = 32, \
                         .offsetZ = 24, .rise16 = 10 }
#define STALL_FRONT(id) { PROP(id, CANOPY, SLAB, 1, LOWER), .depth = 1, .lift16 = 30, .height16 = 14, \
                          .offsetZ = 23 }
#define STALL_POST(id) { PROP(id, CANOPY, SLAB, 1, LOWER), .depth = 2, .height16 = 44, .offsetZ = 6 }
/* The orange awnings (the market's north stall, a house by the gate): both
 * rows of the drawing are its top, each lying over the cell south of it,
 * on two legs. */
#define AWNING_BACK(id) { PROP(id, CANOPY, BLOCK, 1, LOWER), .lift16 = 42, .height16 = 1, \
                          .offsetZ = 16, .rise16 = 4 }
#define AWNING_FRONT(id) { PROP(id, CANOPY, BLOCK, 1, LOWER), .lift16 = 38, .height16 = 1, \
                           .offsetZ = 16, .rise16 = 4 }
#define AWNING_LEG(id) { PROP(id, CANOPY, SLAB, 1, LOWER), .depth = 2, .height16 = 38, .offsetZ = 6 }
/* The rope along the beach's kerb: posts and a sagging rope (the art's lower
 * half). Running north-south, the rope turned to face east along its line,
 * the south end's post where the run ends. */
#define ROPE_EW(id) { PROP(id, FENCE, SLAB, 1, LOWER), .artTop = 8, .depth = 2 }
#define ROPE_NS(id, x, art) { PROP(id, FENCE, SLAB, 1, LOWER), .artTop = 8, .depth = 2, .facing = 1, \
                              .offsetX = (x), .artMetatile = (art) }
/* Route 109's beach (the Slateport tileset): parasols - the canopy's two rows
 * a dome lifted on the pole and centred over it (a row and a half south), the
 * shadow the sand painted under it left to the real one - and deck chairs,
 * low and walked over, the head's backrest raised. */
#define PARASOL_TOP(id) { PROP(id, CANOPY, MOUND, 1, LOWER), .lift16 = 26, .height16 = 8, .offsetZ = 24, \
                          .flags = DIORAMA_PROP_JOINED }
#define PARASOL_RIM(id) { PROP(id, CANOPY, MOUND, 1, NEIGHBOUR), .lift16 = 26, .height16 = 8, .offsetZ = 24, \
                          .flags = DIORAMA_PROP_JOINED }
#define PARASOL_POLE(id) { PROP(id, CANOPY, SLAB, 1, NEIGHBOUR), .artBottom = 8, .depth = 2, .height16 = 52 }
#define DECK_CHAIR(id, rise) { PROP(id, DECOR, BLOCK, 1, LOWER), .height16 = 3, .rise16 = (rise) }
/* An air mattress lying on the sand, three cells long, a little thicker than
 * drawn. */
#define AIR_MATTRESS(id) { PROP(id, DECOR, BLOCK, 1, LOWER), .height16 = 2 }
static const struct DioramaProp sSlateport[] = {
    STALL_ROOF(0x205),
    STALL_ROOF(0x207),
    BASKET(0x20C),
    STALL_ROOF(0x20D),
    STALL_ROOF(0x20E),
    STALL_ROOF(0x20F),
    { PROP(0x213, FLOWERS, CROSS, 1, LOWER), .depth = 6 },
    { PROP(0x214, FLOWERS, CROSS, 1, LOWER), .depth = 6 },
    AWNING_BACK(0x216),
    BASKET(0x21B),
    BASKET(0x21C),
    AWNING_FRONT(0x21E),
    BASKET(0x223),
    STALL_ROOF(0x225),
    STALL_ROOF(0x227),
    BASKET(0x22B),
    STALL_FRONT(0x22D),
    STALL_FRONT(0x22F),
    STALL_FRONT(0x235),
    STALL_FRONT(0x237),
    STALL_POST(0x23D),
    STALL_POST(0x23F),
    { PROP(0x243, BUSH, MOUND, 1, LOWER), .height16 = 11 },
    AWNING_LEG(0x263),
    AWNING_LEG(0x264),
    STALL_POST(0x26B),
    STALL_POST(0x26C),
    AWNING_BACK(0x270),
    AWNING_BACK(0x271),
    AWNING_BACK(0x272),
    ROPE_NS(0x275, 6, 0x2B8),
    ROPE_NS(0x276, -6, 0x2B8),
    AWNING_FRONT(0x278),
    AWNING_FRONT(0x279),
    AWNING_FRONT(0x27A),
    ROPE_EW(0x2B8),
    ROPE_EW(0x2B9),
    ROPE_EW(0x2BA),
    ROPE_NS(0x2BC, 6, 0x2B9),
    ROPE_NS(0x2C4, -6, 0x2B9),
    AIR_MATTRESS(0x2E0),
    DECK_CHAIR(0x2E1, 10),
    PARASOL_TOP(0x2E2),
    PARASOL_TOP(0x2E3),
    PARASOL_TOP(0x2E4),
    PARASOL_TOP(0x2E5),
    PARASOL_TOP(0x2E6),
    PARASOL_TOP(0x2E7),
    AIR_MATTRESS(0x2E8),
    DECK_CHAIR(0x2E9, 0),
    PARASOL_RIM(0x2EA),
    PARASOL_RIM(0x2EB),
    PARASOL_RIM(0x2EC),
    PARASOL_RIM(0x2ED),
    PARASOL_RIM(0x2EE),
    PARASOL_RIM(0x2EF),
    AIR_MATTRESS(0x2F0),
    DECK_CHAIR(0x2F1, 0),
    PARASOL_POLE(0x2F2),
    PARASOL_POLE(0x2F3),
    PARASOL_POLE(0x2F4),
    PARASOL_POLE(0x2F5),
    PARASOL_POLE(0x2F6),
    PARASOL_POLE(0x2F7),
    STALL_FRONT(0x2F8),
    STALL_FRONT(0x2F9),
    STALL_FRONT(0x2FA),
    { PROP(0x32D, DECOR, MOUND, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 11, .height16 = 9 },
    { PROP(0x32E, DECOR, BLOCK, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 4, .height16 = 9 },
    { PROP(0x32F, DECOR, BLOCK, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 4, .height16 = 9 },
    { PROP(0x33E, DECOR, BLOCK, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 4, .height16 = 9 },
    { PROP(0x33F, DECOR, BLOCK, 1, LOWER), .shadowIndex = 4, .shadowFromRow = 4, .height16 = 9 },
    { PROP(0x344, FLOWERS, CROSS, 1, LOWER), .depth = 6 },
};

/* Mauville (and Verdanturf, Routes 110, 111, 117, 118): rows of wooden posts.
 * A row running north-south is the east-west row turned to face east. */
#define WOOD_POSTS_EW(id) { PROP(id, FENCE, SLAB, 1, LOWER), .shadowIndex = 0xF, .shadowFromRow = 12, \
                            .depth = 4 }
#define WOOD_POSTS_NS(id, x) { PROP(id, FENCE, SLAB, 1, LOWER), .shadowIndex = 0xF, .shadowFromRow = 12, \
                               .depth = 4, .facing = 1, .offsetX = (x), .artMetatile = 0x291 }
static const struct DioramaProp sMauville[] = {
    WOOD_POSTS_EW(0x273),
    WOOD_POSTS_NS(0x288, -4),
    WOOD_POSTS_NS(0x28A, 4),
    WOOD_POSTS_EW(0x282),
    WOOD_POSTS_EW(0x290),
    WOOD_POSTS_EW(0x291),
    WOOD_POSTS_EW(0x292),
    WOOD_POSTS_NS(0x2B1, 4),
};

/* Petalburg City's hedges: the cells that block the way stand as blocks with
 * the hedge on top (the walkable row drawn north of them stays flat). */
#define HEDGE(id) { PROP(id, BUSH, BLOCK, 1, LOWER), .height16 = 12 }
static const struct DioramaProp sPetalburg[] = {
    HEDGE(0x23F),
    HEDGE(0x244),
    HEDGE(0x245),
    HEDGE(0x246),
    HEDGE(0x24C),
    HEDGE(0x24D),
    HEDGE(0x24E),
    HEDGE(0x254),
    HEDGE(0x255),
    HEDGE(0x256),
    HEDGE(0x264),
    HEDGE(0x265),
    HEDGE(0x266),
};

/* Lilycove: the blue railing along the harbour (Rustboro's iron fence's
 * kind: shadow colour 6 under it). */
static const struct DioramaProp sLilycove[] = {
    { PROP(0x250, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, .depth = 2 },
    { PROP(0x251, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, .depth = 2 },
    { PROP(0x252, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, .depth = 2 },
    { PROP(0x258, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, .depth = 2, .facing = 1,
      .offsetX = -5, .artMetatile = 0x251 },
    { PROP(0x25A, FENCE, SLAB, 1, LOWER), .shadowIndex = 6, .shadowFromRow = 14, .depth = 2, .facing = 1,
      .offsetX = 4, .artMetatile = 0x251 },
};

/* Lavaridge, Mt. Chimney, Jagged Pass and Route 112: ash-grey boulders and
 * rock piles that block the way (the pebbles one walks over stay flat). */
static const struct DioramaProp sLavaridge[] = {
    { PROP(0x2C5, ROCK, MOUND, 1, LOWER), .height16 = 11 },
    { PROP(0x2C6, ROCK, MOUND, 1, LOWER), .height16 = 11 },
    { PROP(0x2C7, ROCK, MOUND, 1, LOWER), .height16 = 11 },
    { PROP(0x2CE, ROCK, MOUND, 1, LOWER), .height16 = 12, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x2CF, ROCK, MOUND, 1, LOWER), .height16 = 12, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x30D, ROCK, RELIEF, 1, LOWER), .shadowIndex = 5, .shadowFromRow = 12, .depth = 12 },
    { PROP(0x315, ROCK, MOUND, 1, LOWER), .height16 = 11 },
};

/* Mossdeep and the seas round it: tall rocks standing in the water, each two
 * cells wide (a relief joined across the two). */
#define SEA_ROCK(id) { PROP(id, ROCK, RELIEF, 1, LOWER), .depth = 14, .flags = DIORAMA_PROP_JOINED }
static const struct DioramaProp sMossdeep[] = {
    SEA_ROCK(0x202),
    SEA_ROCK(0x203),
    SEA_ROCK(0x338),
    SEA_ROCK(0x33A),
    SEA_ROCK(0x33B),
    SEA_ROCK(0x33D),
};

/* Fallarbor and the ashen routes (113, 114): round boulders under the ash,
 * their painted shadow (colour 8) left out, and the big two-cell rocks. */
#define ASH_BOULDER(id) { PROP(id, ROCK, RELIEF, 1, LOWER), .shadowIndex = 8, .shadowFromRow = 11, \
                          .depth = 12 }
static const struct DioramaProp sFallarbor[] = {
    /* Route 113's ash-covered grass: its blades as tall grass's cards, the
     * mid grey between them (colour 11) left out. Walked through, the game
     * redraws the cell as plain grass and the cards go with the ash. */
    { PROP(0x20A, GRASS, CARDS, 0, KEEP), .keyIndex = 0xB, .depth = 1, .height16 = 20,
      .flags = DIORAMA_PROP_SWAY },
    ASH_BOULDER(0x219),
    ASH_BOULDER(0x21A),
    ASH_BOULDER(0x223),
    { PROP(0x254, ROCK, RELIEF, 1, LOWER), .depth = 14, .flags = DIORAMA_PROP_JOINED },
    { PROP(0x255, ROCK, RELIEF, 1, LOWER), .depth = 14, .flags = DIORAMA_PROP_JOINED },
    ASH_BOULDER(0x26F),
    ASH_BOULDER(0x2AF),
    ASH_BOULDER(0x2BF),
};

/* Caves: heaps of rocks on the floor. */
static const struct DioramaProp sCave[] = {
    { PROP(0x202, ROCK, MOUND, 1, LOWER), .height16 = 10 },
    { PROP(0x203, ROCK, MOUND, 1, LOWER), .height16 = 9 },
    { PROP(0x21F, ROCK, MOUND, 1, LOWER), .height16 = 10 },
    { PROP(0x2D5, ROCK, MOUND, 1, LOWER), .height16 = 10 },
};

#define TILESET(symbol_, props_) { .symbol = symbol_, .props = props_, \
                                   .count = sizeof(props_) / sizeof(props_[0]) }

static const struct DioramaPropTileset sTilesets[] = {
    TILESET("gTileset_General", sGeneral),
    TILESET("gTileset_Rustboro", sRustboro),
    TILESET("gTileset_Cave", sCave),
    TILESET("gTileset_Slateport", sSlateport),
    TILESET("gTileset_Mauville", sMauville),
    TILESET("gTileset_Lilycove", sLilycove),
    TILESET("gTileset_Lavaridge", sLavaridge),
    TILESET("gTileset_Mossdeep", sMossdeep),
    TILESET("gTileset_Fallarbor", sFallarbor),
    TILESET("gTileset_Petalburg", sPetalburg),
};

#define TILESET_COUNT (sizeof(sTilesets) / sizeof(sTilesets[0]))
#define MAX_RULES_TILESETS 256
#define MAX_LAYOUT_ID 1024

/* Set by the main thread (sdl2.c: pokeemerald.cfg, PKMN_DIORAMA_PROPS), read
 * by the renderer. */
static SDL_atomic_t sDisabled;
/* The rules' tileset ids to the catalogue's tilesets, each catalogue
 * tileset's metatiles to its entries (index + 1; 0 for none), and layout ids
 * to the rules' layouts (index + 1). Made once: the tables never change. */
static bool sIndexed;
static const struct DioramaPropTileset *sByRulesTileset[MAX_RULES_TILESETS];
static uint8_t sEntryIndex[TILESET_COUNT][DIORAMA_TILE_COUNT];
static uint16_t sRulesLayout[MAX_LAYOUT_ID];
static DioramaPropMetatileEntries sMetatileSource;
static DioramaPropCellTaken sCellTaken;

bool DioramaProps_Enabled(void)
{
    return SDL_AtomicGet(&sDisabled) == 0;
}

void DioramaProps_SetEnabled(bool enabled)
{
    SDL_AtomicSet(&sDisabled, enabled ? 0 : 1);
}

void DioramaProps_SetCellTaken(DioramaPropCellTaken taken)
{
    sCellTaken = taken;
}

const struct DioramaPropTileset *DioramaProps_Tilesets(unsigned *count)
{
    if (count != NULL)
        *count = TILESET_COUNT;
    return sTilesets;
}

const char *DioramaProps_KindName(uint8_t kind)
{
    static const char *const sNames[DIORAMA_PROP_KIND_COUNT] = {
        "none", "grass", "long grass", "flowers", "sign", "lamp", "fence", "rock", "bush", "decor", "canopy",
    };

    return kind < DIORAMA_PROP_KIND_COUNT ? sNames[kind] : "?";
}

static void Index(void)
{
    unsigned t;
    size_t r;

    if (sIndexed)
        return;
    memset(sByRulesTileset, 0, sizeof(sByRulesTileset));
    memset(sEntryIndex, 0, sizeof(sEntryIndex));
    memset(sRulesLayout, 0, sizeof(sRulesLayout));
    for (r = 0; r < gDioramaLayoutV2Count; r++)
        if (gDioramaLayoutsV2[r].id < MAX_LAYOUT_ID)
            sRulesLayout[gDioramaLayoutsV2[r].id] = (uint16_t)(r + 1);
    for (t = 0; t < TILESET_COUNT; t++)
    {
        unsigned i;

        for (i = 0; i < sTilesets[t].count && i < 255; i++)
        {
            uint16_t id = sTilesets[t].props[i].metatileId;

            if (id < DIORAMA_TILE_COUNT)
                sEntryIndex[t][id] = (uint8_t)(i + 1);
        }
        for (r = 0; r < gDioramaTilesetV2Count; r++)
            if (gDioramaTilesetsV2[r].symbol != NULL
             && strcmp(gDioramaTilesetsV2[r].symbol, sTilesets[t].symbol) == 0)
                sByRulesTileset[gDioramaTilesetsV2[r].id] = &sTilesets[t];
    }
    sIndexed = true;
}

static const struct DioramaGeneratedLayoutV2 *Layout(uint16_t layoutId)
{
    Index();
    if (layoutId == 0 || layoutId >= MAX_LAYOUT_ID || sRulesLayout[layoutId] == 0)
        return NULL;
    return &gDioramaLayoutsV2[sRulesLayout[layoutId] - 1];
}

bool DioramaProps_KnownLayout(uint16_t layoutId)
{
    return Layout(layoutId) != NULL;
}

const struct DioramaProp *DioramaProps_Find(uint16_t layoutId, uint16_t metatileId)
{
    const struct DioramaGeneratedLayoutV2 *layout = Layout(layoutId);
    const struct DioramaPropTileset *tileset;
    uint8_t rulesTileset;
    uint8_t entry;

    if (layout == NULL || metatileId >= DIORAMA_TILE_COUNT)
        return NULL;
    Index();
    rulesTileset = metatileId < SECONDARY_ID ? layout->primaryTilesetId : layout->secondaryTilesetId;
    tileset = sByRulesTileset[rulesTileset];
    if (tileset == NULL)
        return NULL;
    entry = sEntryIndex[tileset - sTilesets][metatileId];
    return entry != 0 ? &tileset->props[entry - 1] : NULL;
}

/* The layout whose tilesets a cell's metatile is: a connected map's own (its
 * cells may be on a page), otherwise the map's (its border too). */
static uint16_t CellLayout(const struct DioramaSceneSnapshot *snapshot,
                           const struct DioramaCellSnapshot *cell)
{
    if ((cell->flags & DIORAMA_CELL_CONNECTED) && cell->sourceLayoutId != 0)
        return cell->sourceLayoutId;
    return snapshot->mapLayoutId;
}

const struct DioramaProp *DioramaProps_ForCell(const struct DioramaSceneSnapshot *snapshot,
                                               const struct DioramaCellSnapshot *cell)
{
    if (!DioramaProps_Enabled() || snapshot == NULL || cell == NULL)
        return NULL;
    return DioramaProps_Find(CellLayout(snapshot, cell), cell->metatileId);
}

static const struct DioramaCellSnapshot *GridCell(const struct DioramaSceneSnapshot *snapshot,
                                                  int mapX, int mapY, int *index)
{
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int i;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return NULL;
    i = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (i >= snapshot->visibleCellCount || snapshot->cells[i].mapX != mapX
     || snapshot->cells[i].mapY != mapY)
        return NULL;
    if (index != NULL)
        *index = i;
    return &snapshot->cells[i];
}

bool DioramaProps_GroundMaterial(const struct DioramaSceneSnapshot *snapshot, int cellIndex,
                                 uint16_t *metatileId, uint8_t *layer)
{
    const struct DioramaCellSnapshot *cell;
    const struct DioramaProp *prop;

    if (snapshot == NULL || cellIndex < 0 || cellIndex >= snapshot->visibleCellCount)
        return false;
    cell = &snapshot->cells[cellIndex];
    prop = DioramaProps_ForCell(snapshot, cell);
    if (prop == NULL || prop->ground == DIORAMA_PROP_GROUND_KEEP)
        return false;
    if (prop->ground == DIORAMA_PROP_GROUND_LOWER)
    {
        *metatileId = cell->metatileId;
        *layer = DIORAMA_MATERIAL_BASE;
        return true;
    }
    {
        /* The ground most of its neighbours on the same tiles have (west and
         * east first: signs stand in rows of ground), none a prop. */
        static const int8_t sOffsets[4][2] = {{-1, 0}, {1, 0}, {0, 1}, {0, -1}};
        uint16_t candidates[4];
        unsigned votes[4];
        unsigned count = 0;
        unsigned best = 0;
        unsigned i;

        for (i = 0; i < 4; i++)
        {
            const struct DioramaCellSnapshot *other = GridCell(snapshot, cell->mapX + sOffsets[i][0],
                                                               cell->mapY + sOffsets[i][1], NULL);
            unsigned k;

            if (other == NULL || other->atlasPage != cell->atlasPage
             || other->treeSprite != 0 || DioramaProps_ForCell(snapshot, other) != NULL)
                continue;
            for (k = 0; k < count && candidates[k] != other->metatileId; k++)
                ;
            if (k == count)
            {
                candidates[count] = other->metatileId;
                votes[count++] = 0;
            }
            votes[k]++;
        }
        if (count == 0)
            return false;
        for (i = 1; i < count; i++)
            if (votes[i] > votes[best])
                best = i;
        *metatileId = candidates[best];
        *layer = DIORAMA_MATERIAL_FULL;
        return true;
    }
}

void DioramaProps_SetMetatileSource(DioramaPropMetatileEntries source)
{
    sMetatileSource = source;
}

bool DioramaProps_ArtEntries(const struct DioramaCellSnapshot *cell, const struct DioramaProp *prop,
                             uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT])
{
    if (prop->artMetatile != DIORAMA_PROP_OWN_ART && sMetatileSource != NULL
     && sMetatileSource(cell->sourceLayoutId, prop->artMetatile, entries))
        return true;
    memcpy(entries, cell->tileEntries, sizeof(cell->tileEntries));
    return prop->artMetatile == DIORAMA_PROP_OWN_ART;
}

void DioramaProps_ComposeArt(const struct DioramaProp *prop, const uint8_t *tileGraphics,
                             const uint16_t *palette, const uint16_t *tileEntries,
                             uint32_t pixels[16 * 16])
{
    unsigned quarter;

    memset(pixels, 0, 16 * 16 * sizeof(*pixels));
    if (prop->layer > 1)
        return;
    for (quarter = 0; quarter < 4; quarter++)
    {
        uint16_t entry = tileEntries[prop->layer * 4 + quarter];
        unsigned paletteId = entry >> 12;
        unsigned x0 = (quarter & 1) * 8;
        unsigned y0 = (quarter >> 1) * 8;
        unsigned x;
        unsigned y;

        for (y = 0; y < 8; y++)
        {
            unsigned row = y0 + y;

            if (row < prop->artTop || row >= DioramaProps_ArtBottom(prop))
                continue;
            for (x = 0; x < 8; x++)
            {
                uint8_t index = DioramaMetatile_DecodePixel(tileGraphics, entry, (uint8_t)x, (uint8_t)y);

                if (index == 0 || (prop->keyIndex != 0 && index == prop->keyIndex)
                 || (prop->shadowIndex != 0 && index == prop->shadowIndex && row >= prop->shadowFromRow))
                    continue;
                pixels[row * 16 + x0 + x] = DioramaMetatile_ConvertColor(palette[paletteId * 16 + index],
                                                                         false);
            }
        }
    }
}

static uint16_t ArtSlot(const struct DioramaCellSnapshot *cell, const struct DioramaProp *prop)
{
    return DioramaAtlas_Slot(cell->atlasPage, prop->artMetatile != DIORAMA_PROP_OWN_ART
                                              ? prop->artMetatile : cell->metatileId);
}

uint8_t DioramaProps_ArtBottom(const struct DioramaProp *prop)
{
    return prop->artBottom != 0 ? prop->artBottom : 16;
}

/* How hard the grass and flowers sway in a weather: a storm bends them, fog
 * leaves them still. */
static float Wind(uint8_t weather)
{
    switch (weather)
    {
    case WEATHER_RAIN: return 1.7f;
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR: return 2.4f;
    case WEATHER_SANDSTORM: return 2.6f;
    case WEATHER_SNOW: return 1.3f;
    case WEATHER_VOLCANIC_ASH: return 1.2f;
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
    case WEATHER_FOG: return 0.6f;
    default: return 1.0f;
    }
}

float DioramaProps_Wind(uint8_t weather)
{
    return Wind(weather);
}

static float Phase(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;

    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFF) / 65536.0f * 6.2831853f;
}

unsigned DioramaProps_Build(const struct DioramaSceneSnapshot *snapshot,
                            const struct DioramaResolvedCell *resolvedCells,
                            struct DioramaPropInstance *instances, unsigned capacity,
                            bool *complete)
{
    unsigned count = 0;
    int i;
    /* The cells someone stands in or is stepping out of: their grass rustles. */
    int16_t occupied[DIORAMA_MAX_OBJECTS * 2][2];
    unsigned occupiedCount = 0;

    *complete = true;
    if (snapshot == NULL || resolvedCells == NULL || !DioramaProps_Enabled())
        return 0;
    for (i = 0; i < snapshot->objectCount && i < DIORAMA_MAX_OBJECTS; i++)
    {
        const struct DioramaObjectSnapshot *object = &snapshot->objects[i];

        if (!object->active || (object->flags & DIORAMA_OBJECT_INVISIBLE))
            continue;
        occupied[occupiedCount][0] = object->currentMapX;
        occupied[occupiedCount][1] = object->currentMapY;
        occupiedCount++;
        if (object->previousMapX != object->currentMapX || object->previousMapY != object->currentMapY)
        {
            occupied[occupiedCount][0] = object->previousMapX;
            occupied[occupiedCount][1] = object->previousMapY;
            occupiedCount++;
        }
    }
    for (i = 0; i < snapshot->visibleCellCount; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        const struct DioramaProp *prop = DioramaProps_ForCell(snapshot, cell);
        const struct DioramaResolvedCell *ground = &resolvedCells[i];
        struct DioramaPropInstance base;
        unsigned parts;
        unsigned p;

        if (prop == NULL || resolvedCells[i].pixelObjectId != 0)
            continue;
        if (sCellTaken != NULL && (cell->flags & DIORAMA_CELL_SOURCE_VALID)
         && sCellTaken(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY))
            continue;
        if (prop->standOn != 0)
        {
            int baseIndex;

            const struct DioramaCellSnapshot *baseCell;
            const struct DioramaProp *baseProp;

            /* A part over another cell (a lamp's head): it stands on that
             * cell's ground, so without the rest of it there it is left out. */
            baseCell = GridCell(snapshot, cell->mapX, cell->mapY + prop->standOn, &baseIndex);
            baseProp = baseCell != NULL ? DioramaProps_ForCell(snapshot, baseCell) : NULL;
            if (baseProp == NULL || baseProp->kind != prop->kind || baseProp->standOn != 0)
                continue;
            ground = &resolvedCells[baseIndex];
        }
        memset(&base, 0, sizeof(base));
        base.x = (float)cell->mapX + prop->offsetX / 16.0f;
        base.z = -((float)cell->mapY + prop->standOn + prop->offsetZ / 16.0f);
        base.y = ground->topHeight + (float)prop->standOn + prop->lift16 / 16.0f;
        base.yaw = prop->facing ? 1.5707963f : 0.0f;
        base.width = 1.0f;
        base.height = (prop->height16 != 0 ? prop->height16 : 16) / 16.0f;
        if (prop->form == DIORAMA_PROP_BLOCK || prop->form == DIORAMA_PROP_MOUND)
        {
            base.depth = (prop->depth != 0 ? prop->depth : 16) / 16.0f;
            base.rise = prop->rise16 / 16.0f;
        }
        else
            base.depth = (prop->depth != 0 ? prop->depth : 1) / 16.0f;
        if (prop->form == DIORAMA_PROP_RELIEF)
            /* Its back on the cell's north edge, bulging south. */
            base.z += 0.5f - base.depth * 0.5f;
        base.sway = (prop->flags & DIORAMA_PROP_SWAY) ? 0.05f * Wind(snapshot->weather) : 0.0f;
        base.phase = Phase(cell->mapX, cell->mapY);
        if (prop->form == DIORAMA_PROP_CARDS)
        {
            unsigned o;

            for (o = 0; o < occupiedCount; o++)
                if (occupied[o][0] == cell->mapX && occupied[o][1] == cell->mapY)
                    base.rustle = 1.0f;
        }
        base.cellIndex = (uint16_t)i;
        base.artSlot = ArtSlot(cell, prop);
        base.artTop = prop->artTop;
        base.artBottom = DioramaProps_ArtBottom(prop);
        base.kind = prop->kind;
        base.flags = prop->flags;
        base.prop = prop;
        if (base.artSlot == DIORAMA_ATLAS_NO_SLOT)
            continue;
        parts = prop->form == DIORAMA_PROP_CROSS || prop->form == DIORAMA_PROP_CARDS ? 2 : 1;
        if (count + parts > capacity)
        {
            *complete = false;
            break;
        }
        for (p = 0; p < parts; p++)
        {
            struct DioramaPropInstance *instance = &instances[count++];

            *instance = base;
            if (prop->form == DIORAMA_PROP_CROSS)
                instance->yaw = p == 0 ? 0.0f : 1.5707963f;
            else if (prop->form == DIORAMA_PROP_CARDS)
            {
                /* The art's top half a quarter tile north, its bottom half a
                 * quarter south, each standing on the ground. */
                uint8_t bottom = DioramaProps_ArtBottom(prop);
                uint8_t middle = (uint8_t)((prop->artTop + bottom) / 2);

                instance->artTop = p == 0 ? prop->artTop : middle;
                instance->artBottom = p == 0 ? middle : bottom;
                instance->z = base.z + (p == 0 ? 0.25f : -0.25f);
                instance->phase = base.phase + (float)p * 1.3f;
            }
        }
    }
    return count;
}

#endif // ENABLE_DIORAMA
