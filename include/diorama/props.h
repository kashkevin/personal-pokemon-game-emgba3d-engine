#ifndef GUARD_DIORAMA_PROPS_H
#define GUARD_DIORAMA_PROPS_H

/*
 * World detail objects (graphics overhaul, phase 2, project-owned): the small
 * things the map art paints flat - signs, mailboxes, lamps, fences, rocks,
 * flowers, tall grass - stood up in 3D from their own metatile art, so they keep
 * the game's pixel art and gain real height, depth, shadows and light.
 *
 * A catalogue per tileset (props.c) names the metatiles that hold one and how
 * it stands: its form, which of the art's rows stand up, how thick it is,
 * where in the cell, what the cell's ground shows instead of the flat picture.
 * DioramaProps_Build turns a snapshot's cells into instances; the renderer
 * (gl_props.c) draws them, and the terrain (gl_terrain_renderer.c) draws those
 * cells' ground through DioramaProps_GroundMaterial. Collision, behaviours and
 * everything the game does with the cells are untouched: this is how they look.
 */
#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

struct DioramaResolvedCell;

enum DioramaPropForm
{
    /* The art standing up, `depth` pixels thick: each opaque pixel a little
     * block (signs, mailboxes, lamps, fences). */
    DIORAMA_PROP_SLAB,
    /* Two slabs crossed at right angles: solid from every side (rocks,
     * bushes). */
    DIORAMA_PROP_CROSS,
    /* The art's top and bottom halves as two thin upright cards, one behind
     * the other, swaying (tall grass, flowers). */
    DIORAMA_PROP_CARDS,
    /* The art on top of a block `height16` sixteenths tall, as seen from
     * above, each opaque pixel a column down to the ground (hedges,
     * planters). */
    DIORAMA_PROP_BLOCK,
    /* The art as seen from above, each opaque pixel a column, the taller the
     * further it is from the art's edge, `height16` sixteenths at the most: a
     * rounded heap (rocks, boulders). */
    DIORAMA_PROP_MOUND,
};

enum DioramaPropGround
{
    DIORAMA_PROP_GROUND_KEEP,      /* the cell is drawn as before under it (tall grass) */
    DIORAMA_PROP_GROUND_LOWER,     /* the cell's lower layer: the object was its upper layer */
    DIORAMA_PROP_GROUND_NEIGHBOUR, /* a neighbouring cell's ground: the art fills the cell */
};

enum DioramaPropKind
{
    DIORAMA_PROP_KIND_NONE,
    DIORAMA_PROP_KIND_GRASS,
    DIORAMA_PROP_KIND_LONG_GRASS,
    DIORAMA_PROP_KIND_FLOWERS,
    DIORAMA_PROP_KIND_SIGN,
    DIORAMA_PROP_KIND_LAMP,
    DIORAMA_PROP_KIND_FENCE,
    DIORAMA_PROP_KIND_ROCK,
    DIORAMA_PROP_KIND_BUSH,
    DIORAMA_PROP_KIND_COUNT
};

enum DioramaPropFlags
{
    DIORAMA_PROP_SWAY = 1 << 0,   /* moves in the wind (and when walked through) */
    DIORAMA_PROP_LIGHT = 1 << 1,  /* gives light at night (a lamp's head) */
    DIORAMA_PROP_JOINED = 1 << 2, /* a mound going on past the cell's edges (a rock of 2 x 2
                                   * cells): its rim is only where its art ends inside */
};

#define DIORAMA_PROP_OWN_ART 0

/* A catalogue entry: how one metatile's object stands. */
struct DioramaProp
{
    uint16_t metatileId;
    uint8_t kind;          /* DioramaPropKind */
    uint8_t form;          /* DioramaPropForm */
    uint8_t layer;         /* the art's layer: 0 lower, 1 upper */
    uint8_t ground;        /* DioramaPropGround */
    uint8_t artTop;        /* the art's rows artTop to artBottom - 1 stand up ... */
    uint8_t artBottom;     /* ... the last on the ground (0: 16, all of them) */
    uint8_t keyIndex;      /* a colour index left out (0: none): the grass between blades */
    uint8_t shadowIndex;   /* the painted shadow's colour index (0: none), left out ... */
    uint8_t shadowFromRow; /* ... from this row down: the real shadow replaces it */
    uint8_t depth;         /* pixels thick (a slab) */
    uint8_t facing;        /* 0: faces south; 1: faces east (a fence running north-south) */
    int8_t offsetX;        /* pixels east of the cell's centre */
    int8_t offsetZ;        /* pixels south of it */
    uint8_t standOn;       /* stands on the cell this many south, this many tiles up (a lamp's head) */
    uint8_t height16;      /* the art's 16 rows stand this many sixteenths of a tile tall (0: 16) */
    uint8_t flags;         /* DioramaPropFlags */
    uint16_t artMetatile;  /* another metatile's art (DIORAMA_PROP_OWN_ART: its own) */
};

/* The props a tileset's metatiles hold (props.c). */
struct DioramaPropTileset
{
    const char *symbol; /* gTileset_General */
    const struct DioramaProp *props;
    uint16_t count;
};

/* One object to draw. Positions in the scene's terms: x east, y up, z = -map y. */
struct DioramaPropInstance
{
    float x, y, z;          /* the middle of the art's bottom edge, on the ground */
    float yaw;              /* 0: faces south; pi/2: faces east */
    float width;            /* tiles across (1: the art's 16 pixels) */
    float height;           /* tiles the art's 16 rows stand (a block: tall) */
    float depth;            /* tiles thick (a block: north to south) */
    float sway;             /* how far its top moves in the wind, tiles */
    float phase;            /* where in the wind's cycle it is */
    uint16_t cellIndex;     /* the snapshot cell it stands for */
    uint16_t artSlot;       /* the atlas slot of the art (metatile_atlas.h) */
    uint8_t artTop;         /* rows shown */
    uint8_t artBottom;
    uint8_t kind;
    uint8_t flags;
    const struct DioramaProp *prop;
};

/* The 3D view's detail objects can be switched off (PKMN_DIORAMA_PROPS=0, the
 * quality settings): their cells are drawn flat again. */
bool DioramaProps_Enabled(void);
void DioramaProps_SetEnabled(bool enabled);

/* The catalogue (props.c), and an entry's tileset by its symbol. */
const struct DioramaPropTileset *DioramaProps_Tilesets(unsigned *count);
/* The prop of a snapshot cell (its layout's tilesets: primary below metatile
 * 512, secondary from it); NULL for none, for a cell the rules already stand
 * up as a pixel object, and when props are off. */
const struct DioramaProp *DioramaProps_ForCell(const struct DioramaSceneSnapshot *snapshot,
                                               const struct DioramaCellSnapshot *cell);
/* The prop of a layout's metatile, NULL for none. */
const struct DioramaProp *DioramaProps_Find(uint16_t layoutId, uint16_t metatileId);
/* Whether the rules know a layout id (one of the maps the 3D view shows). */
bool DioramaProps_KnownLayout(uint16_t layoutId);

/*
 * What the terrain draws on a prop cell's ground: false when the cell is drawn
 * as before (no prop, or GROUND_KEEP); otherwise the metatile and layer
 * (DIORAMA_MATERIAL_BASE: the lower layer; DIORAMA_MATERIAL_FULL: a
 * neighbour's whole metatile) for its top face.
 */
bool DioramaProps_GroundMaterial(const struct DioramaSceneSnapshot *snapshot, int cellIndex,
                                 uint16_t *metatileId, uint8_t *layer);

/*
 * The instances of a snapshot's props (resolvedCells for the ground's height,
 * as the rules resolved it). Returns how many; at most `capacity`, *complete
 * false when more did not fit.
 */
unsigned DioramaProps_Build(const struct DioramaSceneSnapshot *snapshot,
                            const struct DioramaResolvedCell *resolvedCells,
                            struct DioramaPropInstance *instances, unsigned capacity,
                            bool *complete);

/*
 * A prop's art, 16 x 16 ARGB (0 where left out: transparent, its key colour,
 * its painted shadow, rows outside artTop..artBottom), from a cell's art
 * (tiles, faded palette) and tile entries (its own, or artMetatile's).
 */
void DioramaProps_ComposeArt(const struct DioramaProp *prop, const uint8_t *tileGraphics,
                             const uint16_t *palette, const uint16_t *tileEntries,
                             uint32_t pixels[16 * 16]);

/* Where a prop borrows another metatile's art (artMetatile): its tile entries
 * for a layout, set by the renderer (the game's tilesets); false when unknown,
 * and the prop then uses its own art. */
typedef bool (*DioramaPropMetatileEntries)(uint16_t layoutId, uint16_t metatileId,
                                           uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT]);
void DioramaProps_SetMetatileSource(DioramaPropMetatileEntries source);
bool DioramaProps_ArtEntries(const struct DioramaCellSnapshot *cell, const struct DioramaProp *prop,
                             uint16_t entries[DIORAMA_METATILE_ENTRY_COUNT]);

const char *DioramaProps_KindName(uint8_t kind);
/* A prop's last row standing plus one (artBottom, 0 meaning 16). */
uint8_t DioramaProps_ArtBottom(const struct DioramaProp *prop);

#endif
