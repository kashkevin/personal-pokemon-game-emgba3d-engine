#ifdef ENABLE_DIORAMA
/*
 * Reader for the building table compiled into the program by
 * tools/diorama_buildings/export_gl.py (see include/diorama/buildings.h).
 *
 * Ported from ZallaxDev/pokeemerald-3Ds-dualscreen 3ds_port/src/voxel/
 * voxel_building.c (commit 4a699e3, MIT; notice in src/diorama/README.md): the
 * VXB7 layout and its checks are that file's. Changed here: the table is read
 * from memory instead of the console's RomFS; page texels are palette slots in
 * plain rows (export_gl.py) instead of RGBA5551 in the PICA200's order; the
 * relief lift and baked lighting of the 3DS renderer are not ported (the GL
 * renderer lights with a shadow map, diorama/gl_shadow.h).
 *
 * Layout (little endian):
 *   "VXB7", u16 pages, models, pageModels, placements, heightBytes, masks,
 *   u32 vertices, u16 variants, u16 0
 *   pages       x 8:  u16 w, h; u32 (offset of the 3DS texels, unused here)
 *   models      x 16: u8 w, h; u16 ground; u32 firstVertex, vertexCount, heights
 *   pageModels  x 8:  u16 model, page; i16 ox, oy (pixels)
 *   placements  x 16: u16 layout, pageModel, x, y, ground, extraCount;
 *                     u32 extraFirst (sorted by layout)
 *   heightBytes       one byte per model cell, pixels; 255 = not the model's
 *   padding to 2, u16 footprint per model cell, masks x 32, u8 quarters per
 *   height byte, padding to 2, variants x 6, padding to 4,
 *   vertices x 24: float x, y, z, u, v, shade
 */
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "fieldmap.h"
#include "diorama/buildings.h"
#include "diorama/props.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"
#include "diorama/tree_sprites.h"
#include "constants/layouts.h"
#include "constants/map_types.h"
#include "constants/metatile_behaviors.h"
#include "tilesets.h"

extern const unsigned int gDioramaBuildingsTableSize;
extern const unsigned char gDioramaBuildingsTable[];
extern const unsigned int gDioramaBuildingsPageCount;
extern const unsigned int gDioramaBuildingsPageOffsets[];
extern const unsigned char gDioramaBuildingsPageSlots[];
/* page, row, first column, length << 8 | brightness (export_gl.py) */
extern const unsigned int gDioramaBuildingsWindowRunCount;
extern const unsigned short gDioramaBuildingsWindowRuns[][4];

struct BuildingPage
{
    uint16_t w, h;
};

struct BuildingModel
{
    uint8_t w, h;
    uint16_t ground;
    uint32_t firstVertex, vertexCount, heights;
};

struct BuildingPageModel
{
    uint16_t model, page;
    int16_t ox, oy;
};

struct BuildingPlacement
{
    uint16_t layout, pageModel, x, y, ground, extraCount;
    uint32_t extraFirst;
};

static bool sReady;
static struct BuildingPage *sPages;
static unsigned sPageCount;
static struct BuildingModel *sModels;
static unsigned sModelCount;
static struct BuildingPageModel *sPageModels;
static unsigned sPageModelCount;
static struct BuildingPlacement *sPlacements;
static unsigned sPlacementCount;
static const uint8_t *sHeights;
static const uint8_t *sQuarters;
static unsigned sHeightBytes;
static struct DioramaBuildingVertex *sVertices;
static unsigned sVertexCount;
/* The placements last looked up: a chunk build asks about every cell it
 * touches, nearly always of the layout it asked about last. */
static int sLastLayout = -1;
static unsigned sLastFirst, sLastCount;
/* Per placement: 1 while a cell it owns is not the drawing it was made from
 * (DIORAMA_CELL_CHANGED: a door opened, a light went out, a floor was generated);
 * such a placement does not stand (DioramaBuildings_Validate). */
static uint8_t *sChanged;
static uint32_t sChangedHash;

/* An upright face looking south: its span in x and its z, in tiles of the
 * layout (DioramaBuildings_FrontAt). */
struct FrontFace
{
    float lo, hi, z;
};

/*
 * A walker's card asks for the faces of the cells under it every frame, and a
 * model is read triangle by triangle - thousands of them for a Pokemon Center
 * - so a cell's faces are found once and kept. The models never change; a
 * cell is known by its layout and the height asked about. A cell with more
 * faces than an entry holds is read again each time.
 */
#define FRONT_SETS 64u
#define FRONT_FACES 16u

struct FrontCell
{
    bool valid;
    uint16_t layout;
    int x, y;
    float height;
    unsigned count;
    struct FrontFace face[FRONT_FACES];
};

static struct FrontCell sFrontCells[FRONT_SETS];

static unsigned U16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void Release(void)
{
    free(sPages);
    free(sModels);
    free(sPageModels);
    free(sPlacements);
    free(sVertices);
    free(sChanged);
    sChanged = NULL;
    sChangedHash = 0;
    sPages = NULL;
    sModels = NULL;
    sPageModels = NULL;
    sPlacements = NULL;
    sVertices = NULL;
    sHeights = sQuarters = NULL;
    sPageCount = sModelCount = sPageModelCount = sPlacementCount = 0;
    sHeightBytes = sVertexCount = 0;
    sLastLayout = -1;
    memset(sFrontCells, 0, sizeof(sFrontCells));
    sReady = false;
}

bool DioramaBuildings_Init(void)
{
    const uint8_t *table = gDioramaBuildingsTable;
    size_t size = gDioramaBuildingsTableSize;
    size_t at;
    unsigned maskCount;
    unsigned variantCount;
    unsigned i;

    Release();
    if (size < 24 || memcmp(table, "VXB7", 4) != 0)
        goto bad;
    sPageCount = U16(table + 4);
    sModelCount = U16(table + 6);
    sPageModelCount = U16(table + 8);
    sPlacementCount = U16(table + 10);
    sHeightBytes = U16(table + 12);
    maskCount = U16(table + 14);
    sVertexCount = U32(table + 16);
    variantCount = U16(table + 20);
    if (sPageCount != gDioramaBuildingsPageCount)
        goto bad;
    sPages = calloc(sPageCount + 1, sizeof(*sPages));
    sModels = calloc(sModelCount + 1, sizeof(*sModels));
    sPageModels = calloc(sPageModelCount + 1, sizeof(*sPageModels));
    sPlacements = calloc(sPlacementCount + 1, sizeof(*sPlacements));
    sChanged = calloc(sPlacementCount + 1, 1);
    sVertices = calloc(sVertexCount + 1, sizeof(*sVertices));
    if (!sPages || !sModels || !sPageModels || !sPlacements || !sVertices)
        goto bad;

    at = 24;
    if (at + 8u * sPageCount + 16u * sModelCount + 8u * sPageModelCount
        + 16u * sPlacementCount > size)
        goto bad;
    for (i = 0; i < sPageCount; i++, at += 8)
    {
        sPages[i].w = (uint16_t)U16(table + at);
        sPages[i].h = (uint16_t)U16(table + at + 2);
    }
    for (i = 0; i < sModelCount; i++, at += 16)
    {
        struct BuildingModel *m = &sModels[i];

        m->w = table[at];
        m->h = table[at + 1];
        m->ground = (uint16_t)U16(table + at + 2);
        m->firstVertex = U32(table + at + 4);
        m->vertexCount = U32(table + at + 8);
        m->heights = U32(table + at + 12);
        if (m->firstVertex + m->vertexCount > sVertexCount
         || m->heights + (unsigned)m->w * m->h > sHeightBytes)
            goto bad;
    }
    for (i = 0; i < sPageModelCount; i++, at += 8)
    {
        struct BuildingPageModel *pm = &sPageModels[i];

        pm->model = (uint16_t)U16(table + at);
        pm->page = (uint16_t)U16(table + at + 2);
        pm->ox = (int16_t)U16(table + at + 4);
        pm->oy = (int16_t)U16(table + at + 6);
        if (pm->model >= sModelCount || pm->page >= sPageCount)
            goto bad;
    }
    for (i = 0; i < sPlacementCount; i++, at += 16)
    {
        struct BuildingPlacement *p = &sPlacements[i];

        p->layout = (uint16_t)U16(table + at);
        p->pageModel = (uint16_t)U16(table + at + 2);
        p->x = (uint16_t)U16(table + at + 4);
        p->y = (uint16_t)U16(table + at + 6);
        p->ground = (uint16_t)U16(table + at + 8);
        p->extraCount = (uint16_t)U16(table + at + 10);
        p->extraFirst = U32(table + at + 12);
        if (p->pageModel >= sPageModelCount || p->extraFirst + p->extraCount > sVertexCount
         || (i > 0 && p->layout < sPlacements[i - 1].layout))
            goto bad;
    }
    /* heights, padding, footprints, masks */
    sHeights = table + at;
    at += sHeightBytes + (sHeightBytes & 1);
    at += 2u * sHeightBytes + 32u * maskCount;
    /* quarters, padding, variants, padding to 4 */
    sQuarters = table + at;
    at += sHeightBytes + (sHeightBytes & 1);
    at += 6u * variantCount;
    at = (at + 3u) & ~(size_t)3u;
    if (at + 24u * sVertexCount != size)
        goto bad;
    /* Six little-endian floats a vertex, the layout of DioramaBuildingVertex
     * on every target this builds for. */
    memcpy(sVertices, table + at, 24u * sVertexCount);
    for (i = 0; i < sPageCount; i++)
    {
        unsigned end = i + 1 < sPageCount ? gDioramaBuildingsPageOffsets[i + 1] : UINT32_MAX;

        if (gDioramaBuildingsPageOffsets[i] + (uint32_t)sPages[i].w * sPages[i].h > end)
            goto bad;
    }
    sReady = true;
    SDL_Log("Diorama: %u modelled buildings on %u pages, %u placements, %u vertices",
            sModelCount, sPageCount, sPlacementCount, sVertexCount);
    return true;

bad:
    SDL_Log("Diorama: building table is malformed; houses stay extruded");
    Release();
    return false;
}

bool DioramaBuildings_IsAvailable(void)
{
    return sReady;
}

unsigned DioramaBuildings_LayoutPlacements(uint16_t layout, unsigned *first)
{
    unsigned lo = 0;
    unsigned hi = sPlacementCount;
    unsigned n = 0;

    *first = 0;
    if (!sReady)
        return 0;
    if ((int)layout != sLastLayout)
    {
        while (lo < hi)
        {
            unsigned mid = (lo + hi) / 2;

            if (sPlacements[mid].layout < layout)
                lo = mid + 1;
            else
                hi = mid;
        }
        while (lo + n < sPlacementCount && sPlacements[lo + n].layout == layout)
            n++;
        sLastLayout = layout;
        sLastFirst = lo;
        sLastCount = n;
    }
    *first = sLastFirst;
    return sLastCount;
}

bool DioramaBuildings_GetPlacement(unsigned index, struct DioramaBuildingPlacementInfo *info)
{
    const struct BuildingPlacement *p;
    const struct BuildingPageModel *pm;
    const struct BuildingModel *m;

    if (!sReady || index >= sPlacementCount)
        return false;
    p = &sPlacements[index];
    pm = &sPageModels[p->pageModel];
    m = &sModels[pm->model];
    info->layout = p->layout;
    info->x = p->x;
    info->y = p->y;
    info->width = m->w;
    info->height = m->h;
    info->page = pm->page;
    info->pageX = pm->ox;
    info->pageY = pm->oy;
    info->firstVertex = m->firstVertex;
    info->vertexCount = m->vertexCount;
    info->firstGroundVertex = p->extraFirst;
    info->groundVertexCount = p->extraCount;
    return true;
}

const struct DioramaBuildingVertex *DioramaBuildings_Vertices(void)
{
    return sVertices;
}

/* The placement whose model stands in layout cell (x, y): its index in sPlacements, and the cell's in sHeights; -1
 * when none does. */
static int PlacementAt(uint16_t layout, int x, int y, unsigned *heightIndex)
{
    unsigned first;
    unsigned count = DioramaBuildings_LayoutPlacements(layout, &first);
    unsigned i;

    for (i = 0; i < count; i++)
    {
        const struct BuildingPlacement *p = &sPlacements[first + i];
        const struct BuildingModel *m = &sModels[sPageModels[p->pageModel].model];
        int cx = x - p->x;
        int cy = y - p->y;
        unsigned k;

        if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h || (sChanged != NULL && sChanged[first + i]))
            continue;
        /* A hedge's rectangle holds the house it runs round. */
        k = m->heights + (unsigned)cy * m->w + (unsigned)cx;
        if (sHeights[k] == 0xFF)
            continue;
        if (heightIndex != NULL)
            *heightIndex = k;
        return (int)(first + i);
    }
    return -1;
}

bool DioramaBuildings_CellAt(uint16_t layout, int x, int y,
                             uint16_t *ground, uint8_t *quarters, float *top)
{
    unsigned k = 0;
    int placement = PlacementAt(layout, x, y, &k);

    if (placement < 0)
        return false;
    if (ground != NULL)
        *ground = sPlacements[placement].ground;
    if (quarters != NULL)
        *quarters = sQuarters[k];
    if (top != NULL)
        *top = sHeights[k] / 16.0f;
    return true;
}

/*
 * The upright faces looking south in layout cell (x, y) that cross `height`:
 * each one's span in x and its z. Every model that reaches the cell, not the
 * first one whose rectangle holds it: a Pokemon Center's PC stands in a cell
 * of the counter's rectangle, and the counter has no face there. Returns how
 * many there are, writing at most `max`.
 */
static unsigned CollectFronts(uint16_t layout, int x, int y, float height,
                              struct FrontFace *out, unsigned max)
{
    unsigned first;
    unsigned count = DioramaBuildings_LayoutPlacements(layout, &first);
    unsigned found = 0;
    unsigned i;

    for (i = 0; i < count; i++)
    {
        const struct BuildingPlacement *p = &sPlacements[first + i];
        const struct BuildingModel *m = &sModels[sPageModels[p->pageModel].model];
        const struct DioramaBuildingVertex *v = &sVertices[m->firstVertex];
        int lx = x - p->x;
        int ly = y - p->y;
        float north = (float)ly;
        uint32_t k;

        if (lx < 0 || ly < 0 || lx >= m->w || ly >= m->h || (sChanged != NULL && sChanged[first + i]))
            continue;
        for (k = 0; k + 2 < m->vertexCount; k += 3)
        {
            const struct DioramaBuildingVertex *a = &v[k];
            const struct DioramaBuildingVertex *b = &v[k + 1];
            const struct DioramaBuildingVertex *c = &v[k + 2];
            float lo = a->x < b->x ? a->x : b->x;
            float hi = a->x > b->x ? a->x : b->x;
            float y0 = a->y < b->y ? a->y : b->y;
            float y1 = a->y > b->y ? a->y : b->y;
            struct FrontFace face;
            unsigned j;

            if (c->x < lo) lo = c->x;
            if (c->x > hi) hi = c->x;
            if (c->y < y0) y0 = c->y;
            if (c->y > y1) y1 = c->y;
            /* upright, in the cell, at the height */
            if (a->z != b->z || a->z != c->z || a->z < north - 0.01f || a->z > north + 1.01f
             || y0 > height || y1 < height)
                continue;
            /* and looking south: a counter's back is not in front of the
             * nurse behind it (a face winds outwards) */
            if ((b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x) <= 0.0f)
                continue;
            face.lo = lo + p->x;
            face.hi = hi + p->x;
            face.z = p->y + a->z;
            /* A face at the same depth that meets one already found is the
             * same front: one span. */
            for (j = 0; j < found && j < max; j++)
                if (out[j].z == face.z && face.lo <= out[j].hi && face.hi >= out[j].lo)
                {
                    if (face.lo < out[j].lo) out[j].lo = face.lo;
                    if (face.hi > out[j].hi) out[j].hi = face.hi;
                    break;
                }
            if (j < found && j < max)
                continue;
            if (found < max)
                out[found] = face;
            found++;
        }
    }
    return found;
}

static const struct FrontCell *FrontCellAt(uint16_t layout, int x, int y, float height)
{
    uint32_t hash = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u
                  ^ (uint32_t)layout * 83492791u;
    struct FrontCell *cell = &sFrontCells[(hash ^ (hash >> 16)) & (FRONT_SETS - 1u)];

    if (!cell->valid || cell->layout != layout || cell->x != x || cell->y != y
     || cell->height != height)
    {
        cell->valid = true;
        cell->layout = layout;
        cell->x = x;
        cell->y = y;
        cell->height = height;
        cell->count = CollectFronts(layout, x, y, height, cell->face, FRONT_FACES);
    }
    return cell;
}

bool DioramaBuildings_FrontAt(uint16_t layout, int x, int y, float x0, float x1,
                              float height, float *z)
{
    struct FrontFace many[64];
    const struct FrontCell *cell;
    const struct FrontFace *face;
    unsigned count;
    unsigned i;
    bool found = false;

    if (!sReady)
        return false;
    cell = FrontCellAt(layout, x, y, height);
    face = cell->face;
    count = cell->count;
    if (count > FRONT_FACES)
    {
        count = CollectFronts(layout, x, y, height, many, 64);
        if (count > 64)
            count = 64;
        face = many;
    }
    for (i = 0; i < count; i++)
    {
        /* across the span */
        if (face[i].lo >= x1 || face[i].hi <= x0)
            continue;
        if (!found || face[i].z > *z)
            *z = face[i].z;
        found = true;
    }
    return found;
}

/*
 * People the map puts on a model's cells, by hand (KNOWN_ISSUES MS-226; the 3D eye review of the rooms, 2026-11-10).
 * The Battle Dome battle room's stands are a box a tile and a half high over rows 1-3, its top their seats: the
 * audience the map puts in rows 1 and 2 stood on the floor inside it, only hair and hats over its top. The Battle
 * Tents' battle room (Fallarbor's and Slateport's) has a low wall at the back wall's foot, a box over row 2: the
 * attendant Fallarbor's map puts there, at (3, 2), stood inside it. The Aqua Hideout B2F's desk is a box over row 6:
 * the grunt at (13, 5) behind it showed only the cap. The 2D game draws the audience in the stands and the other two
 * over the drawing in front of them. Every other cell keeps the rules of ModelPush (gl_object_renderer.c).
 */
enum
{
    PERSON_ON_TOP = 1,
    PERSON_OVER_FRONT,
};

static const struct
{
    uint16_t layout;
    uint8_t x0, y0, x1, y1;
    uint8_t how;
} sPeopleOnModels[] =
{
    {LAYOUT_BATTLE_FRONTIER_BATTLE_DOME_BATTLE_ROOM, 0, 1, 19, 2, PERSON_ON_TOP},
    {LAYOUT_BATTLE_TENT_BATTLE_ROOM,                 1, 2,  9, 2, PERSON_OVER_FRONT},
    {LAYOUT_AQUA_HIDEOUT_B2F,                       11, 5, 13, 5, PERSON_OVER_FRONT},
};

static uint8_t PersonOnModel(uint16_t layout, int x, int y)
{
    unsigned i;

    for (i = 0; i < ARRAY_COUNT(sPeopleOnModels); i++)
        if (sPeopleOnModels[i].layout == layout
         && x >= sPeopleOnModels[i].x0 && x <= sPeopleOnModels[i].x1
         && y >= sPeopleOnModels[i].y0 && y <= sPeopleOnModels[i].y1)
            return sPeopleOnModels[i].how;
    return 0;
}

bool DioramaBuildings_PersonOnTop(uint16_t layout, int x, int y, float *top)
{
    float height = 0.0f;

    if (!sReady || PersonOnModel(layout, x, y) != PERSON_ON_TOP
     || !DioramaBuildings_CellAt(layout, x, y, NULL, NULL, &height) || height <= 0.0f)
        return false;
    if (top != NULL)
        *top = height;
    return true;
}

bool DioramaBuildings_PersonOverFront(uint16_t layout, int x, int y)
{
    return sReady && PersonOnModel(layout, x, y) == PERSON_OVER_FRONT
        && DioramaBuildings_CellAt(layout, x, y, NULL, NULL, NULL);
}

uint32_t DioramaBuildings_Validate(const struct DioramaSceneSnapshot *snapshot)
{
    uint32_t hash = 2166136261u;
    unsigned i;

    if (!sReady || sChanged == NULL || snapshot == NULL)
        return 0;
    memset(sChanged, 0, sPlacementCount);
    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        unsigned first;
        unsigned count;
        unsigned j;

        if ((cell->flags & (DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CHANGED))
            != (DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_CHANGED))
            continue;
        count = DioramaBuildings_LayoutPlacements(cell->sourceLayoutId, &first);
        for (j = 0; j < count; j++)
        {
            const struct BuildingPlacement *p = &sPlacements[first + j];
            const struct BuildingModel *m = &sModels[sPageModels[p->pageModel].model];
            int cx = cell->sourceMapX - p->x;
            int cy = cell->sourceMapY - p->y;

            if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h
             || sHeights[m->heights + (unsigned)cy * m->w + (unsigned)cx] == 0xFF)
                continue;
            if (!sChanged[first + j])
                hash = (hash ^ (first + j)) * 16777619u;
            sChanged[first + j] = 1;
        }
    }
    if (hash != sChangedHash)
    {
        /* the fronts walkers are pushed in front of change with what stands */
        memset(sFrontCells, 0, sizeof(sFrontCells));
        sChangedHash = hash;
    }
    return hash;
}

bool DioramaBuildings_PlacementStands(unsigned index)
{
    return sReady && index < sPlacementCount && (sChanged == NULL || !sChanged[index]);
}

static bool IsWaterCell(const struct DioramaSceneSnapshot *snapshot,
                        const struct DioramaResolvedCell *resolved, unsigned count, int i)
{
    const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

    /* past the map's edge the border block's water (an ocean route without a
     * neighbour that way) lies with the sea it borders */
    return i >= 0 && (unsigned)i < count
        && (cell->flags & (DIORAMA_CELL_SOURCE_VALID | DIORAMA_CELL_BORDER))
        && cell->behavior != MB_WATERFALL
        && (resolved[i].shape == DIORAMA_SHAPE_WATER
         || resolved[i].terrainClass == DIORAMA_TERRAIN_CLASS_WATER);
}

/* The layout whose tilesets drew a cell: a connected map's own, otherwise the
 * map's (props.c CellLayout). */
static uint16_t CellLayoutId(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell)
{
    if ((cell->flags & DIORAMA_CELL_CONNECTED) && cell->sourceLayoutId != 0)
        return cell->sourceLayoutId;
    return snapshot->mapLayoutId;
}

/*
 * The sea at one level (KNOWN_ISSUES E1). The terrace solver gives the open sea
 * the level it finds for it - a whole level below the land where it meets a
 * terraced islet (Routes 127 and 132-134) - and water it finds penned in, or
 * that touches no terrace, the water's nominal level. Between them were steps
 * with no faces, and the sky showed through them, in the field and in
 * battles; with the terrain's risers (terrain_mesh.c ColumnBottom) they would
 * be walls of water.
 *
 * So each body of water lies at one level, worked out once per layout from the
 * whole layout (never from the cells in view, which would move the sea as the
 * player walked): its water cells, 4-connected (water joins water of its own
 * elevation, and shallow water the water it runs into: the shoals round an
 * islet, a harbour's, walked through at the land's elevation but the sea's own
 * surface), waterfalls apart, lie at the lowest level the solver gave any of
 * them. Outdoors the sea (ocean water, currents, seaweed, deep water) lies a
 * level below the land at the most, whatever the solver found: a town's sea or
 * a route's open sea away from any terrace met the routes' lowered sea at the
 * maps' seams. Nothing is raised. The cells of a model or a rock standing in
 * water take the level of the water round them, so it stands in it (its lift
 * is its cell's ground).
 */
extern const struct MapLayout *const gMapLayouts[];

#define WATER_LEVEL_NONE INT8_MIN
/* the layouts the cells come from at once - the map, its neighbours and the
 * maps past them (scene_snapshot.c TryCopySecondMapCell) - with room over */
#define WATER_LEVEL_CACHE 16

struct WaterLevels
{
    uint16_t layoutId;
    bool outdoor;
    int width;
    int height;
    uint32_t used;
    int8_t *level; /* per layout cell: its body's level in tiles, or WATER_LEVEL_NONE */
    uint8_t *sea;  /* per layout cell: 1 when its body is the open sea outdoors (laid on the sea's level, raised if need be) */
    int8_t *surface; /* per layout cell: its body's deepest record's surface in sixteenths of a tile, or WATER_LEVEL_NONE */
    int8_t edgeSeaLevel; /* the level of most of the sea along the map's edge, or WATER_LEVEL_NONE */
};

static struct WaterLevels sWaterLevels[WATER_LEVEL_CACHE];
static uint32_t sWaterLevelClock;

static bool IsWaterBehavior(uint8_t behavior)
{
    switch (behavior)
    {
    case MB_POND_WATER:
    case MB_INTERIOR_DEEP_WATER:
    case MB_DEEP_WATER:
    case MB_SOOTOPOLIS_DEEP_WATER:
    case MB_OCEAN_WATER:
    case MB_SHALLOW_WATER:
    case MB_NO_SURFACING:
    case MB_SEAWEED:
    case MB_SEAWEED_NO_SURFACING:
    case MB_EASTWARD_CURRENT:
    case MB_WESTWARD_CURRENT:
    case MB_NORTHWARD_CURRENT:
    case MB_SOUTHWARD_CURRENT:
    case MB_HOT_SPRINGS:
        return true;
    default:
        return false;
    }
}

static bool IsSeaBehavior(uint8_t behavior)
{
    switch (behavior)
    {
    case MB_OCEAN_WATER:
    case MB_DEEP_WATER:
    case MB_NO_SURFACING:
    case MB_SEAWEED:
    case MB_SEAWEED_NO_SURFACING:
    case MB_EASTWARD_CURRENT:
    case MB_WESTWARD_CURRENT:
    case MB_NORTHWARD_CURRENT:
    case MB_SOUTHWARD_CURRENT:
        return true;
    default:
        return false;
    }
}

static bool IsOutdoorMapType(uint8_t mapType)
{
    return mapType != MAP_TYPE_NONE && mapType != MAP_TYPE_INDOOR && mapType != MAP_TYPE_UNDERGROUND
        && mapType != MAP_TYPE_SECRET_BASE && mapType != MAP_TYPE_UNDERWATER;
}

static uint8_t LayoutBehavior(const struct MapLayout *layout, int offset, uint8_t *elevation)
{
    uint16_t block = layout->map[offset];
    uint16_t metatile = block & MAPGRID_METATILE_ID_MASK;
    uint16_t primaryCount = layout->isFrlg ? NUM_METATILES_IN_PRIMARY_FRLG : NUM_METATILES_IN_PRIMARY;
    const struct Tileset *tileset = metatile < primaryCount ? layout->primaryTileset
                                                            : layout->secondaryTileset;

    *elevation = UNPACK_ELEVATION(block);
    if (tileset == NULL || tileset->metatileAttributes == NULL)
        return 0;
    return UNPACK_BEHAVIOR(tileset->metatileAttributes[metatile < primaryCount ? metatile
                                                                              : metatile - primaryCount]);
}

/* The bodies of water of one layout and their levels (see above), made once. */
static const struct WaterLevels *WaterLevelsFor(uint16_t layoutId, bool outdoor)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    struct WaterLevels *entry = NULL;
    const struct MapLayout *layout;
    uint8_t *behavior;
    uint8_t *elevation;
    int32_t *queue;
    int count;
    int i;

    for (i = 0; i < WATER_LEVEL_CACHE; i++)
        if (sWaterLevels[i].level != NULL && sWaterLevels[i].layoutId == layoutId
         && sWaterLevels[i].outdoor == outdoor)
        {
            sWaterLevels[i].used = ++sWaterLevelClock;
            return &sWaterLevels[i];
        }
    if (layoutId == 0 || !DioramaProps_KnownLayout(layoutId))
        return NULL;
    layout = gMapLayouts[layoutId - 1];
    if (layout == NULL || layout->map == NULL || layout->width <= 0 || layout->height <= 0)
        return NULL;
    for (i = 0; i < WATER_LEVEL_CACHE; i++)
        if (entry == NULL || sWaterLevels[i].used < entry->used)
            entry = &sWaterLevels[i];
    free(entry->level);
    free(entry->sea);
    free(entry->surface);
    memset(entry, 0, sizeof(*entry));
    count = layout->width * layout->height;
    entry->level = malloc(count);
    entry->sea = calloc(count, 1);
    entry->surface = malloc(count);
    behavior = malloc(count);
    elevation = malloc(count);
    queue = malloc(count * sizeof(*queue));
    if (entry->level == NULL || entry->sea == NULL || entry->surface == NULL || behavior == NULL || elevation == NULL
     || queue == NULL)
    {
        free(entry->level);
        free(entry->sea);
        free(entry->surface);
        entry->level = NULL;
        entry->sea = NULL;
        entry->surface = NULL;
        free(behavior);
        free(elevation);
        free(queue);
        return NULL;
    }
    entry->layoutId = layoutId;
    entry->outdoor = outdoor;
    entry->width = layout->width;
    entry->height = layout->height;
    entry->used = ++sWaterLevelClock;
    for (i = 0; i < count; i++)
        behavior[i] = LayoutBehavior(layout, i, &elevation[i]);
    memset(entry->level, 0, count);
    for (i = 0; i < count; i++)
    {
        entry->level[i] = WATER_LEVEL_NONE;
        entry->surface[i] = WATER_LEVEL_NONE;
    }
    for (i = 0; i < count; i++)
    {
        int head = 0, tail = 0, b;
        int16_t lowest = 0;
        /* the deepest surface the body's records give it, while each is of the water's own depth (KNOWN_ISSUES
         * MS-231) */
        int16_t surface = 0;
        bool surfaced = true;
        bool sea = false;

        if (entry->level[i] != WATER_LEVEL_NONE || !IsWaterBehavior(behavior[i]))
            continue;
        /* mark the body (0 for now), then give it its level */
        entry->level[i] = 0;
        queue[tail++] = i;
        while (head < tail)
        {
            int cell = queue[head++];
            int x = cell % layout->width, y = cell / layout->width;
            int16_t ground = DioramaRules_StaticGroundQ16(layoutId, (uint32_t)cell);
            int16_t own;

            if (ground < lowest)
                lowest = ground;
            if (DioramaRules_StaticWaterSurfaceQ16(layoutId, (uint32_t)cell, &own))
            {
                if (own < surface)
                    surface = own;
            }
            else
                surfaced = false;
            sea |= IsSeaBehavior(behavior[cell]);
            for (int d = 0; d < 4; d++)
            {
                int nx = x + step[d][0], ny = y + step[d][1], n;

                if (nx < 0 || ny < 0 || nx >= layout->width || ny >= layout->height)
                    continue;
                n = ny * layout->width + nx;
                /* Its banks too, the land one walks on beside it (step 16, the world geometry overhaul): the
                 * water lies no higher than the lowest of them. Water the terrace solver left on the level it
                 * gives ground of no level of its own (0), beside banks a level lower, stood over them as a
                 * box of water (the Safari Zone's north-west pond at the foot of its rock: the owner's "large
                 * featureless blue rectangles"). */
                if (!IsWaterBehavior(behavior[n]) && behavior[n] != MB_WATERFALL
                 && !(layout->map[n] & MAPGRID_COLLISION_MASK))
                {
                    int16_t bank = DioramaRules_StaticGroundQ16(layoutId, (uint32_t)n);

                    if (bank < lowest)
                        lowest = bank;
                }
                if (entry->level[n] != WATER_LEVEL_NONE || !IsWaterBehavior(behavior[n]))
                    continue;
                if (elevation[n] != elevation[cell] && behavior[n] != MB_SHALLOW_WATER
                 && behavior[cell] != MB_SHALLOW_WATER)
                    continue;
                entry->level[n] = 0;
                queue[tail++] = n;
            }
        }
        /* Whole tiles, as the solver's levels are; water over a whole tile (the
         * water's own nominal depth, -2/16) lies a tile down. A level a few
         * sixteenths under a whole tile is that tile: the rules lift the sea's
         * water round its rocks onto the sea's level with that water's own depth
         * (graphics-wip-20261012-1, compile_rules.py _lift_record: -16 - 1), and
         * rounded a tile further down it took the whole sea with it, a level
         * under the rest of the sea outside that body, and stood the rest as
         * walls of water (Route 132). */
        if (lowest < 0)
        {
            int tiles = (-lowest + 11) / 16;

            lowest = (int16_t)-(tiles > 0 ? tiles : 1);
        }
        else
            lowest = 0;
        /* the open sea outdoors at one level, the sea's: a body the solver laid
         * deeper (Mossdeep's east sea under its tall cliffs, -3; Ever Grande's
         * below its waterfall, -2) met the rest of the sea, the border block's
         * and the next map's as a wall of water; LevelWaterBodies raises it and
         * what stands in it, and the shore under it (2026-10-26) */
        if (outdoor && sea)
            lowest = -1;
        if (lowest < INT8_MIN + 1)
            lowest = INT8_MIN + 1;
        for (b = 0; b < tail; b++)
        {
            entry->level[queue[b]] = (int8_t)lowest;
            entry->sea[queue[b]] = outdoor && sea;
            if (surfaced && surface > INT8_MIN)
                entry->surface[queue[b]] = (int8_t)surface;
        }
    }
    /* the sea along the map's edge: the level most of its cells there lie at (StaticWaterLevel: past the edge of a
     * map that is not outdoors the sea lies at it, all of it, also beside the land that reaches the edge) */
    {
        uint16_t counts[256] = {0};
        int best = -1, l;

        entry->edgeSeaLevel = WATER_LEVEL_NONE;
        for (i = 0; i < count; i++)
        {
            int x = i % layout->width, y = i / layout->width;

            if ((x != 0 && y != 0 && x != layout->width - 1 && y != layout->height - 1)
             || entry->level[i] == WATER_LEVEL_NONE || !IsSeaBehavior(behavior[i]))
                continue;
            if (counts[entry->level[i] + 128] < UINT16_MAX)
                counts[entry->level[i] + 128]++;
        }
        for (l = 0; l < 256; l++)
            if (counts[l] > 0 && (best < 0 || counts[l] > counts[best]))
                best = l;
        if (best >= 0)
            entry->edgeSeaLevel = (int8_t)(best - 128);
    }
    free(behavior);
    free(elevation);
    free(queue);
    return entry;
}

/* A water cell's level: its body's in its own layout (WaterLevelsFor); past the
 * map's edge the border block's sea lies at the sea's. *sea: the open sea
 * outdoors (at its level whatever the solver found). false: none to keep. */
static bool StaticWaterLevel(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell, float *level, bool *sea)
{
    bool outdoor = IsOutdoorMapType(snapshot->mapType);
    const struct WaterLevels *levels;
    int offset;

    if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID))
    {
        if (!(cell->flags & DIORAMA_CELL_BORDER) || !IsSeaBehavior(cell->behavior))
            return false;
        if (outdoor)
        {
            *level = -1.0f;
            *sea = true;
            return true;
        }
        /* the sea round an island the game types as indoor (Faraway Island's entrance, Birth Island, Navel Rock) or
         * underground (the Abandoned Ship's deck): at the level of the map's own sea along its edge, as that is laid
         * (lowered only). Faraway Island's own sea lies a level down; the sea past its edge lay at the water's nominal
         * level, a step up all round the map, its side a line along the edge (KNOWN_ISSUES MS-229). */
        if (cell->sourceLayoutId != snapshot->mapLayoutId)
            return false;
        levels = WaterLevelsFor(snapshot->mapLayoutId, false);
        if (levels == NULL || levels->edgeSeaLevel == WATER_LEVEL_NONE)
            return false;
        *level = (float)levels->edgeSeaLevel;
        *sea = false;
        return true;
    }
    levels = WaterLevelsFor(CellLayoutId(snapshot, cell), outdoor);
    if (levels == NULL || cell->sourceMapX < 0 || cell->sourceMapY < 0
     || cell->sourceMapX >= levels->width || cell->sourceMapY >= levels->height)
        return false;
    offset = cell->sourceMapY * levels->width + cell->sourceMapX;
    if (levels->level[offset] == WATER_LEVEL_NONE)
        return false;
    *level = (float)levels->level[offset];
    *sea = levels->sea[offset] != 0;
    /* a body that is not the open sea at one surface, its deepest record's (lowered only): its shallow water's records
     * lie a sixteenth over its deep water's, a step whose side showed as a line through a cave's pool (the Marine Cave's
     * and the Seafloor Cavern's entrances, the Shoal Cave's rooms at high tide) - KNOWN_ISSUES MS-231 */
    if (!*sea && levels->surface[offset] != WATER_LEVEL_NONE && levels->surface[offset] / 16.0f < *level)
        *level = levels->surface[offset] / 16.0f;
    return true;
}

/* A cell's ground (and all of it with it) to `level`, up or down; true when it went up. */
static bool MoveCellTo(struct DioramaResolvedCell *r, float level)
{
    float rise = level - r->groundHeight;

    if (rise > -0.001f && rise < 0.001f)
        return false;
    r->groundHeight += rise;
    r->topHeight += rise;
    for (unsigned k = 0; k < r->surfaceCount && k < DIORAMA_MAX_VISUAL_SURFACES; k++)
    {
        r->surfaces[k].topHeight += rise;
        r->surfaces[k].bottomHeight += rise;
    }
    return rise > 0.0f;
}

static void LowerCellTo(struct DioramaResolvedCell *r, float level)
{
    float drop = r->groundHeight - level;

    if (drop <= 0.001f)
        return;
    r->groundHeight -= drop;
    r->topHeight -= drop;
    for (unsigned k = 0; k < r->surfaceCount && k < DIORAMA_MAX_VISUAL_SURFACES; k++)
    {
        r->surfaces[k].topHeight -= drop;
        r->surfaces[k].bottomHeight -= drop;
    }
}

/* A mountain's rock standing in the sea alone (compile_rules.py
 * _reefs_at_sea_level: its ground the sea's nominal level, REEF_GROUND): each
 * piece of it on the level of the water round it, all its cells together, so
 * it stays one mass (gl_terrain_renderer.c ShapeRockMasses) instead of
 * standing on a plinth of its own edge's drawing over the sea lowered past it
 * (Route 106's reefs, the owner's report of 2026-10-24). */
#define REEF_GROUND (-2.0f / 16.0f)

static bool IsReefCell(const struct DioramaResolvedCell *r)
{
    return r->shape == DIORAMA_SHAPE_CLIFF && (r->measuredFlags & DIORAMA_MEASURED_MOUNTAIN_ART)
        && r->groundHeight > REEF_GROUND - 0.001f && r->groundHeight < REEF_GROUND + 0.001f;
}

static void LevelReefs(struct DioramaResolvedCell *resolved, unsigned count, const uint8_t *inWater)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint8_t seen[DIORAMA_MAX_VISIBLE_CELLS];
    static uint16_t piece[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;

    memset(seen, 0, count < DIORAMA_MAX_VISIBLE_CELLS ? count : DIORAMA_MAX_VISIBLE_CELLS);
    for (i = 0; i < count && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        unsigned size = 0, read = 0;
        float level = 1.0e9f;

        if (seen[i] || !IsReefCell(&resolved[i]))
            continue;
        seen[i] = 1;
        piece[size++] = (uint16_t)i;
        while (read < size)
        {
            int at = piece[read++];
            int gx = at % DIORAMA_GRID_WIDTH, gy = at / DIORAMA_GRID_WIDTH;

            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count)
                    continue;
                if (inWater[n] == 1 && resolved[n].groundHeight < level)
                    level = resolved[n].groundHeight;
                if (!seen[n] && IsReefCell(&resolved[n]))
                {
                    seen[n] = 1;
                    piece[size++] = (uint16_t)n;
                }
            }
        }
        if (level < 1.0e8f)
            for (read = 0; read < size; read++)
                LowerCellTo(&resolved[piece[read]], level);
    }
}

/* The ground under a raised sea (a cliff's foot the solver laid deeper, flat land next to the water and lower than
 * it) comes up to the sea, as far as it goes on below it: else a pit beside the water, walled by the sea's sides. */
static void FillUnderSea(struct DioramaResolvedCell *resolved, unsigned count, const uint8_t *inWater, uint8_t *raised)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned head = 0, tail = 0, i;

    for (i = 0; i < count; i++)
        if (raised[i])
            queue[tail++] = (uint16_t)i;
    while (head < tail)
    {
        int at = queue[head++];
        int gx = at % DIORAMA_GRID_WIDTH, gy = at / DIORAMA_GRID_WIDTH;
        float level = resolved[at].groundHeight;

        for (int d = 0; d < 4; d++)
        {
            int nx = gx + step[d][0], ny = gy + step[d][1], n;
            struct DioramaResolvedCell *r;

            if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                continue;
            n = ny * DIORAMA_GRID_WIDTH + nx;
            if ((unsigned)n >= count || raised[n] || inWater[n] != 0)
                continue;
            r = &resolved[n];
            if (r->shape != DIORAMA_SHAPE_FLAT || r->topHeight - r->groundHeight > 0.05f
             || r->groundHeight > level - 0.001f)
                continue;
            MoveCellTo(r, level);
            raised[n] = 1;
            queue[tail++] = (uint16_t)n;
        }
    }
}

/* Water the map's elevations part from the open sea it touches (a body of its own in WaterLevelsFor, at its own level:
 * Route 132's line of deep water by Pacifidlog stood a step over the sea round it) lies at the sea's level with it, when
 * it is within a tile of it; further apart the two are kept. */
static void JoinSea(struct DioramaResolvedCell *resolved, unsigned count, const uint8_t *inWater, uint8_t *raised,
                    uint8_t *inSea)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned head = 0, tail = 0, i;

    for (i = 0; i < count; i++)
        if (inWater[i] == 1 && inSea[i])
            queue[tail++] = (uint16_t)i;
    while (head < tail)
    {
        int at = queue[head++];
        int gx = at % DIORAMA_GRID_WIDTH, gy = at / DIORAMA_GRID_WIDTH;
        float level = resolved[at].groundHeight;

        for (int d = 0; d < 4; d++)
        {
            int nx = gx + step[d][0], ny = gy + step[d][1], n;
            float apart;

            if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                continue;
            n = ny * DIORAMA_GRID_WIDTH + nx;
            if ((unsigned)n >= count || inWater[n] != 1 || inSea[n])
                continue;
            apart = resolved[n].groundHeight - level;
            if (apart > 1.001f || apart < -1.001f)
                continue;
            raised[n] |= MoveCellTo(&resolved[n], level);
            inSea[n] = 1;
            queue[tail++] = (uint16_t)n;
        }
    }
}

static void LevelWaterBodies(const struct DioramaSceneSnapshot *snapshot,
                             struct DioramaResolvedCell *resolved, unsigned count,
                             const uint8_t *inWater)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    /* raised: the open sea raised onto the sea's level here, or what stands in it or lay under it; sea: the open sea
     * laid on its level here, or a cell standing in it */
    static uint8_t raised[DIORAMA_MAX_VISIBLE_CELLS];
    static uint8_t inSea[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;
    int pass;

    memset(raised, 0, count < DIORAMA_MAX_VISIBLE_CELLS ? count : DIORAMA_MAX_VISIBLE_CELLS);
    memset(inSea, 0, count < DIORAMA_MAX_VISIBLE_CELLS ? count : DIORAMA_MAX_VISIBLE_CELLS);
    for (i = 0; i < count; i++)
    {
        float level;
        bool sea = false;

        if (inWater[i] == 1 && StaticWaterLevel(snapshot, &snapshot->cells[i], &level, &sea))
        {
            if (sea)
            {
                raised[i] = MoveCellTo(&resolved[i], level);
                inSea[i] = 1;
            }
            else
                LowerCellTo(&resolved[i], level);
        }
    }
    JoinSea(resolved, count, inWater, raised, inSea);
    /* a model's or a rock's cells in the water: the level of the water round
     * them, passed on through the model's own cells */
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < count; i++)
        {
            int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
            float level = 1.0e9f, waterLevel = 1.0e9f;
            bool sea = false;

            if (inWater[i] != 2)
                continue;
            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count || inWater[n] == 0)
                    continue;
                if (resolved[n].groundHeight < level)
                    level = resolved[n].groundHeight;
                if (inWater[n] == 1 && resolved[n].groundHeight < waterLevel)
                    waterLevel = resolved[n].groundHeight;
                sea |= inSea[n] != 0;
            }
            /* in the open sea it stands on the water round it, up or down (a rock the solver laid with a deeper
             * sea came up with it, else it stood in a pit of water walls); its own cells pass the level on */
            if (sea && level < 1.0e8f)
            {
                raised[i] |= MoveCellTo(&resolved[i], waterLevel < 1.0e8f ? waterLevel : level);
                inSea[i] = 1;
            }
            else if (level < 1.0e8f)
                LowerCellTo(&resolved[i], level);
        }
    FillUnderSea(resolved, count, inWater, raised);
    LevelReefs(resolved, count, inWater);
}

/*
 * A cave's water lies CAVE_POOL_DEPTH below its floor: the GBA draws a cave's
 * pools, channels and tidal shallows at the floor's level, and nothing lowers
 * them (the terrace solver reads General's terraces, which caves have none
 * of), so the pool a battle was fought on lay flat with the sand round it
 * (the Seafloor Cavern's entrance, the Shoal Cave). Every body of water in the
 * cave, what stands in it and the waterfalls between them sink alike after
 * the bodies are levelled; the banks, the rock round it stand. Underground
 * maps only. Added by the battle environments' work.
 */
#define CAVE_POOL_DEPTH (6.0f / 16.0f)

static void SinkCavePools(const struct DioramaSceneSnapshot *snapshot,
                          struct DioramaResolvedCell *resolved, unsigned count,
                          const uint8_t *inWater)
{
    unsigned i;

    if (snapshot->mapType != MAP_TYPE_UNDERGROUND)
        return;
    for (i = 0; i < count; i++)
    {
        struct DioramaResolvedCell *r = &resolved[i];

        /* the water, what stands in it, and a waterfall between two pools */
        if (inWater[i] == 0
         && !(snapshot->cells[i].behavior == MB_WATERFALL
           && (r->shape == DIORAMA_SHAPE_WATER || r->terrainClass == DIORAMA_TERRAIN_CLASS_WATER)))
            continue;
        r->groundHeight -= CAVE_POOL_DEPTH;
        r->topHeight -= CAVE_POOL_DEPTH;
        for (unsigned k = 0; k < r->surfaceCount && k < DIORAMA_MAX_VISUAL_SURFACES; k++)
        {
            r->surfaces[k].topHeight -= CAVE_POOL_DEPTH;
            r->surfaces[k].bottomHeight -= CAVE_POOL_DEPTH;
        }
    }
}

/*
 * The water under a bridge (its lower surface, rules.c FinalizeSurfaces: the
 * water's nominal level) lies at the level of the water beside it. The passes
 * above move the water and left the bridge's where it was, with a step between
 * them that nothing drew: Meteor Falls' bridge kept its water CAVE_POOL_DEPTH
 * over the sunken river round it, the sky showing through under its deck (step
 * 16, the world geometry overhaul). The water beside it, the lowest there;
 * else, with nothing but water and bridge beside it (the middle of a bridge
 * three wide), a bridge's cell levelled beside it; never up to the deck. A
 * bridge's cell over the bank keeps its own.
 */
static void LevelUnderBridges(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolved, unsigned count,
                              const uint8_t *inWater)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint8_t levelled[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;
    int pass;

    memset(levelled, 0, count < DIORAMA_MAX_VISIBLE_CELLS ? count : DIORAMA_MAX_VISIBLE_CELLS);
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < count; i++)
        {
            struct DioramaResolvedCell *r = &resolved[i];
            int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
            float level = 1.0e9f, bridged = 1.0e9f;
            bool land = false;

            if (levelled[i] || r->shape != DIORAMA_SHAPE_BRIDGE || r->surfaceCount < 2
             || !(snapshot->cells[i].flags & DIORAMA_CELL_SOURCE_VALID))
                continue;
            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count || !(snapshot->cells[n].flags & DIORAMA_CELL_SOURCE_VALID))
                    continue;
                if (inWater[n] == 1)
                {
                    if (resolved[n].topHeight < level)
                        level = resolved[n].topHeight;
                }
                else if (resolved[n].shape != DIORAMA_SHAPE_BRIDGE)
                    land = true;
                else if (levelled[n] && resolved[n].surfaces[1].topHeight < bridged)
                    bridged = resolved[n].surfaces[1].topHeight;
            }
            if (level > 1.0e8f && !land)
                level = bridged;
            if (level > 1.0e8f)
                continue;
            if (level > r->groundHeight - 2.0f / 16.0f)
                level = r->groundHeight - 2.0f / 16.0f;
            r->surfaces[1].bottomHeight = level - (r->surfaces[1].topHeight - r->surfaces[1].bottomHeight);
            r->surfaces[1].topHeight = level;
            levelled[i] = 1;
        }
}

/*
 * A deck over walkable ground (Victory Road 1F's bridges over its lower floor, raised with the floors they join by
 * the rules' cave_levels): the ground under it, its lower surface, at the level of the ground it crosses, with that
 * ground's elevation. rules.c FinalizeSurfaces gives a bridge's lower surface the water's elevation and level, and the
 * deck an elevation from the cells round it - the floor's, where the floor is all that touches it - so the player
 * walking under the deck at the floor's elevation stood on the deck (found 2026-11-03). The walkable ground beside it
 * (not water, not a bridge) DECK_OVER_GROUND or more under its deck, the lowest such, gives the lower surface its level
 * and elevation; with only bridges beside it, a bridge cell so set beside it. The deck then never answers to that
 * elevation: the walkway at its level beside it gives it its own, else none (an object of any other elevation on it
 * stands on the deck, ObjectGroundHeight's fallback). A deck less than that over the ground (Shoal Cave's at low tide,
 * half a level) keeps its own: a person under it would stand through it.
 */
#define DECK_OVER_GROUND 0.75f
static void GroundUnderBridges(const struct DioramaSceneSnapshot *snapshot,
                               struct DioramaResolvedCell *resolved, unsigned count,
                               const uint8_t *inWater)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint8_t grounded[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;
    int pass;

    memset(grounded, 0, count < DIORAMA_MAX_VISIBLE_CELLS ? count : DIORAMA_MAX_VISIBLE_CELLS);
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < count; i++)
        {
            struct DioramaResolvedCell *r = &resolved[i];
            int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
            float under = 1.0e9f;
            uint8_t underElevation = 0, deckElevation = 0;

            if (grounded[i] || inWater[i] || r->shape != DIORAMA_SHAPE_BRIDGE || r->surfaceCount < 2
             || !(snapshot->cells[i].flags & DIORAMA_CELL_SOURCE_VALID))
                continue;
            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;
                const struct DioramaCellSnapshot *c;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count || !(snapshot->cells[n].flags & DIORAMA_CELL_SOURCE_VALID) || inWater[n])
                    continue;
                c = &snapshot->cells[n];
                if (resolved[n].shape == DIORAMA_SHAPE_BRIDGE)
                {
                    if (grounded[n] && resolved[n].surfaces[1].topHeight < under)
                    {
                        under = resolved[n].surfaces[1].topHeight;
                        underElevation = resolved[n].surfaces[1].gameplayElevation;
                    }
                    continue;
                }
                if (c->collision != 0 || c->elevation == 0 || c->elevation == 15)
                    continue;
                if (resolved[n].groundHeight <= r->groundHeight - DECK_OVER_GROUND)
                {
                    if (resolved[n].groundHeight < under)
                    {
                        under = resolved[n].groundHeight;
                        underElevation = c->elevation;
                    }
                }
                else if (resolved[n].groundHeight > r->groundHeight - 1.0f / 16.0f
                      && resolved[n].groundHeight < r->groundHeight + 1.0f / 16.0f)
                    deckElevation = c->elevation;
            }
            if (under > 1.0e8f)
                continue;
            r->surfaces[1].bottomHeight = under - (r->surfaces[1].topHeight - r->surfaces[1].bottomHeight);
            r->surfaces[1].topHeight = under;
            r->surfaces[1].gameplayElevation = underElevation;
            if (r->surfaces[0].gameplayElevation == underElevation)
                r->surfaces[0].gameplayElevation = deckElevation != underElevation ? deckElevation : 0;
            grounded[i] = 1;
        }
}


/*
 * Mt Chimney's crater: its lava lake, which the GBA draws flat at the summit's
 * level, lies LAVA_DEPTH down (and the Magma Hideout's lava pools with it), the
 * rock drawn over its edge rimming it (that rim's side shows the rock,
 * gl_terrain_renderer.c). DioramaRules_CellLava says which cells are lava.
 * Added by the battle environments' work.
 */
#define LAVA_DEPTH 1.0f

static void SinkLava(const struct DioramaSceneSnapshot *snapshot,
                     struct DioramaResolvedCell *resolved, unsigned count)
{
    unsigned i;

    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        struct DioramaResolvedCell *r = &resolved[i];

        if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID) || r->shape != DIORAMA_SHAPE_FLAT
         || DioramaRules_CellLava(cell, CellLayoutId(snapshot, cell)) != 2)
            continue;
        r->groundHeight -= LAVA_DEPTH;
        r->topHeight -= LAVA_DEPTH;
        for (unsigned k = 0; k < r->surfaceCount && k < DIORAMA_MAX_VISUAL_SURFACES; k++)
        {
            r->surfaces[k].topHeight -= LAVA_DEPTH;
            r->surfaces[k].bottomHeight -= LAVA_DEPTH;
        }
    }
}

/*
 * Route 111's desert: its deep sand (MB_DEEP_SAND, the rippled patches the GBA
 * draws with a darker rim) stands as dunes, each patch rising in steps of
 * DUNE_STEP from its rim to its middle, DUNE_STEPS of them at the most, instead
 * of a floor flat to the horizon (a battle's low camera saw nothing raised
 * there). A cell's step is its ring in the patch: its distance (8-way) to the
 * nearest cell that is no deep sand. Past the scene's cells the patch is taken
 * to go on, so a cell's height can change only within DUNE_STEPS of the
 * scene's edge, far out of sight. How it looks only: the sand's behaviour is
 * untouched, and the player, a trainer or a battler stands on the dune (its
 * cells' ground). Added by the battle environments' work.
 */
#define DUNE_STEPS 3
#define DUNE_STEP (3.0f / 16.0f)

static void RaiseDunes(const struct DioramaSceneSnapshot *snapshot,
                       struct DioramaResolvedCell *resolved, unsigned count)
{
    static uint8_t ring[DIORAMA_MAX_VISIBLE_CELLS];
    bool any = false;
    unsigned i;
    int pass;

    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        ring[i] = (cell->flags & DIORAMA_CELL_SOURCE_VALID)
               && cell->behavior == MB_DEEP_SAND
               && resolved[i].shape == DIORAMA_SHAPE_FLAT ? DUNE_STEPS : 0;
        any |= ring[i] != 0;
    }
    if (!any)
        return;
    for (pass = 0; pass < DUNE_STEPS; pass++)
        for (i = 0; i < count; i++)
        {
            int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
            int dx, dy;

            if (ring[i] <= 1)
                continue;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                {
                    int nx = gx + dx, ny = gy + dy;
                    unsigned n;

                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0
                     || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                        continue;
                    n = (unsigned)(ny * DIORAMA_GRID_WIDTH + nx);
                    if (n >= count || !(snapshot->cells[n].flags & DIORAMA_CELL_SOURCE_VALID))
                        continue; /* past the map's edge: the patch goes on */
                    if (ring[n] + 1 < ring[i])
                        ring[i] = ring[n] + 1;
                }
        }
    for (i = 0; i < count; i++)
    {
        struct DioramaResolvedCell *r = &resolved[i];
        float raise = ring[i] * DUNE_STEP;

        if (ring[i] == 0)
            continue;
        r->groundHeight += raise;
        r->topHeight += raise;
        for (unsigned k = 0; k < r->surfaceCount && k < DIORAMA_MAX_VISUAL_SURFACES; k++)
        {
            r->surfaces[k].topHeight += raise;
            r->surfaces[k].bottomHeight += raise;
        }
    }
}

/*
 * A rock standing in the shallows or the sea, drawn on the water: a cell that is
 * no water, is not walked on, is flat ground under its detail object (a rock's
 * mound, props.c) and whose lower layer is the sea's tiles
 * (DioramaRules_CellOverWater), with water beside it and no ground walked on
 * (a cliff or another rock may be). It stands in the water like a model's cells
 * (inWater 2), at the water's level: at the land's level it stood a block out of
 * the sea lowered past it (Lilycove's harbour). A sandbar, one cell of sand with
 * water all round, lies at the water's level the same way. Added by the battle
 * environments' work.
 */
static void MarkRocksInWater(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaResolvedCell *resolved, unsigned count,
                             uint8_t *inWater)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint8_t rock[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;

    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        rock[i] = inWater[i] == 0 && cell->collision != 0
               && (cell->flags & DIORAMA_CELL_SOURCE_VALID)
               && resolved[i].shape == DIORAMA_SHAPE_FLAT
               && DioramaRules_CellOverWater(cell, snapshot->mapType);
    }
    /* a sandbar: one cell of sand with water on all four sides (Lilycove's harbour, Route
     * 125), flush with the water as the GBA draws it, not a block of the land's height */
    for (i = 0; i < count; i++)
    {
        int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
        int waterSides = 0;

        if (inWater[i] != 0 || snapshot->cells[i].behavior != MB_SAND
         || !(snapshot->cells[i].flags & DIORAMA_CELL_SOURCE_VALID)
         || resolved[i].shape != DIORAMA_SHAPE_FLAT)
            continue;
        for (int d = 0; d < 4; d++)
        {
            int nx = gx + step[d][0], ny = gy + step[d][1], n;

            if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                continue;
            n = ny * DIORAMA_GRID_WIDTH + nx;
            if ((unsigned)n < count && inWater[n] == 1)
                waterSides++;
        }
        if (waterSides == 4)
            rock[i] = 1;
    }
    for (i = 0; i < count; i++)
    {
        int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
        int waterSides = 0, otherSides = 0;

        if (!rock[i])
            continue;
        for (int d = 0; d < 4; d++)
        {
            int nx = gx + step[d][0], ny = gy + step[d][1], n;

            if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                continue;
            n = ny * DIORAMA_GRID_WIDTH + nx;
            if ((unsigned)n >= count || !(snapshot->cells[n].flags & DIORAMA_CELL_SOURCE_VALID))
                continue; /* past the map's edge: says nothing */
            if (inWater[n] != 0)
                waterSides++;
            else if (!rock[n] && snapshot->cells[n].collision == 0)
                otherSides++; /* ground walked on: a deck's barrel, not a rock in the sea */
        }
        if (waterSides > 0 && otherSides == 0)
            inWater[i] = 2;
    }
}

/*
 * A model in the water as one piece (KNOWN_ISSUES MS-230). A cell of a model drawn over its own cells (a rock, a sea
 * boulder) stands in water when every neighbour not its own is water, and LevelWaterBodies lays such a cell on the
 * water round it, a level down with the open sea. A boulder at the foot of the land kept its cells beside the land at
 * the water's nominal level, the rest a level down: half sunk, its raised half's sides a box of water (Faraway Island's
 * strip, rocks along the sea routes' shores). A piece with a cell in water and more of its neighbours water than not
 * is in it whole, the cells of it the rules lay at the water's surface (under the land: a sea boulder's pieces); those
 * at the land's level stay (a rock on Route 132's sandbar, drawn over the sand, sank them as pits in the sand); nothing
 * else changes.
 */
static void JoinModelsInWater(uint8_t *inWater, const int32_t *placementOf, const uint8_t (*sidesOf)[3],
                              unsigned count)
{
    static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static uint8_t visited[DIORAMA_MAX_VISIBLE_CELLS];
    static uint16_t piece[DIORAMA_MAX_VISIBLE_CELLS];
    unsigned i;

    memset(visited, 0, count);
    for (i = 0; i < count; i++)
    {
        unsigned head = 0, tail = 0, water = 0, other = 0, k;
        bool anyIn = false;

        if (visited[i] || placementOf[i] < 0)
            continue;
        visited[i] = 1;
        piece[tail++] = (uint16_t)i;
        while (head < tail)
        {
            int c = piece[head++];
            int gx = c % DIORAMA_GRID_WIDTH, gy = c / DIORAMA_GRID_WIDTH;

            water += sidesOf[c][0];
            other += sidesOf[c][1];
            anyIn |= inWater[c] == 2;
            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count || visited[n] || placementOf[n] != placementOf[i])
                    continue;
                visited[n] = 1;
                piece[tail++] = (uint16_t)n;
            }
        }
        if (!anyIn || water <= other)
            continue;
        for (k = 0; k < tail; k++)
            if (sidesOf[piece[k]][2])
                inWater[piece[k]] = 2;
    }
}

void DioramaBuildings_FlattenResolvedCells(const struct DioramaSceneSnapshot *snapshot,
                                           struct DioramaResolvedCell *resolved,
                                           unsigned count)
{
    /* 1: a water cell; 2: a cell of a model standing in water (LevelWaterBodies) */
    static uint8_t inWater[DIORAMA_MAX_VISIBLE_CELLS];
    /* a model's cell drawn over its own (a rock): its placement; its neighbours not its own, water and other; and
     * whether the rules lay it under the land, at the water's surface (JoinModelsInWater) */
    static int32_t placementOf[DIORAMA_MAX_VISIBLE_CELLS];
    static uint8_t sidesOf[DIORAMA_MAX_VISIBLE_CELLS][3];
    /* A placement's ground is the commonest walkable metatile round it in its layout (gen_voxel_buildings.py
     * find_placements). Where the 3D view stands General's trees it draws a tree's crown top as the ground under
     * it (scene_snapshot.c ApplyTreeSprites, DioramaTreeSprites_GroundMetatile), so no cell in view keeps that
     * metatile and the atlas never draws its slot: a model's cells laid with it drew nothing, and the sky showed
     * where the model does not close (a Fortree tree house's sides under its canopy; KNOWN_ISSUES MS-240). Such a
     * ground is drawn as the view draws those cells. */
    bool treeGrounds = DioramaTreeSprites_Enabled() && snapshot->primaryTileset == &gTileset_General
                    && snapshot->mapType != MAP_TYPE_INDOOR && snapshot->mapType != MAP_TYPE_SECRET_BASE;
    unsigned i;
    int face;

    if (!sReady)
        return;
    DioramaBuildings_Validate(snapshot);
    if (count > DIORAMA_MAX_VISIBLE_CELLS)
        count = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < count; i++)
    {
        inWater[i] = IsWaterCell(snapshot, resolved, count, (int)i) ? 1 : 0;
        placementOf[i] = -1;
    }
    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        struct DioramaResolvedCell *r = &resolved[i];
        uint16_t ground;
        uint8_t quarters;
        float groundHeight;

        if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID)
         || !DioramaBuildings_CellAt(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY,
                                     &ground, &quarters, NULL))
            continue;
        /* found by its tiles over its cells' own drawing (a rock in the sea): standing in
         * water if every neighbour that is not its own is water */
        if (ground == DIORAMA_BUILDING_OWN_GROUND && quarters != 0)
        {
            static const int step[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            int gx = (int)i % DIORAMA_GRID_WIDTH, gy = (int)i / DIORAMA_GRID_WIDTH;
            int waterSides = 0, otherSides = 0;

            for (int d = 0; d < 4; d++)
            {
                int nx = gx + step[d][0], ny = gy + step[d][1], n;
                const struct DioramaCellSnapshot *next;

                if (nx < 0 || ny < 0 || nx >= DIORAMA_GRID_WIDTH || ny >= DIORAMA_GRID_HEIGHT)
                    continue;
                n = ny * DIORAMA_GRID_WIDTH + nx;
                if ((unsigned)n >= count)
                    continue;
                next = &snapshot->cells[n];
                if (!(next->flags & DIORAMA_CELL_SOURCE_VALID))
                    continue; /* past the map's edge: says nothing */
                if (DioramaBuildings_CellAt(next->sourceLayoutId, next->sourceMapX, next->sourceMapY,
                                            NULL, NULL, NULL))
                    continue; /* the model's own (or another model's) cell */
                if (inWater[n] == 1)
                    waterSides++;
                else
                    otherSides++;
            }
            if (waterSides > 0 && otherSides == 0)
                inWater[i] = 2;
            placementOf[i] = PlacementAt(cell->sourceLayoutId, cell->sourceMapX, cell->sourceMapY, NULL);
            sidesOf[i][0] = (uint8_t)waterSides;
            sidesOf[i][1] = (uint8_t)otherSides;
            sidesOf[i][2] = r->groundHeight < -0.01f;
        }
        groundHeight = r->groundHeight;
        r->shape = DIORAMA_SHAPE_FLAT;
        r->profile = DIORAMA_ROOF_NONE;
        r->archetype = DIORAMA_ARCHETYPE_GROUND;
        r->semanticProfile = 0;
        r->terrainClass = DIORAMA_TERRAIN_CLASS_GROUND;
        r->groundMode = 0;
        r->baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
        r->pixelObjectId = 0;
        r->structureId = 0;
        r->structureTemplateId = 0;
        r->structureX = 0;
        r->structureY = 0;
        r->structureWidth = 0;
        r->structureHeight = 0;
        r->structureRoofRows = 0;
        r->structureLocalX = 0;
        r->structureLocalY = 0;
        r->structureOwnerKind = 0;
        r->doorFold = 0;
        r->voidKind = 0;
        r->structureSouthFacadeRows = 0;
        r->measuredExtentBands = 0;
        r->measuredPeriodBands = 0;
        r->measuredRoofBands = 0;
        r->measuredRunLocal = 0;
        r->measuredRunLength = 0;
        r->measuredFlags = 0;
        r->measuredBandCount = 0;
        r->cliffEdgeMask = 0;
        r->cliffBaseMask = 0;
        r->cliffTransitionMask = 0;
        r->cliffCornerMask = 0;
        r->terraceProfile = DIORAMA_TERRACE_NONE;
        r->topHeight = groundHeight;
        r->featureHeight = 0.0f;
        r->structureBodyHeight = 0.0f;
        r->structureRoofHeight = 0.0f;
        r->surfaceCount = 1;
        r->surfaces[0].bottomHeight = groundHeight - 1.0f;
        r->surfaces[0].topHeight = groundHeight;
        for (face = 0; face < DIORAMA_MATERIAL_FACE_COUNT; face++)
        {
            if (ground == DIORAMA_BUILDING_OWN_GROUND)
            {
                /* the cell's own drawing; without its upper layer when the
                 * model stands for part of it (a rock in the water) */
                r->materials[face].metatileId = DIORAMA_MATERIAL_METATILE_SELF;
                r->materials[face].layer = quarters != 0 ? DIORAMA_MATERIAL_BASE
                                                         : DIORAMA_MATERIAL_FULL;
            }
            else
            {
                r->materials[face].metatileId = treeGrounds && ground < NUM_METATILES_IN_PRIMARY
                                              ? DioramaTreeSprites_GroundMetatile(false, ground) : ground;
                r->materials[face].layer = DIORAMA_MATERIAL_FULL;
            }
            r->materials[face].rotation = 0;
            r->materials[face].flags = 0;
        }
    }
    JoinModelsInWater(inWater, placementOf, sidesOf, count);
    MarkRocksInWater(snapshot, resolved, count, inWater);
    LevelWaterBodies(snapshot, resolved, count, inWater);
    SinkCavePools(snapshot, resolved, count, inWater);
    LevelUnderBridges(snapshot, resolved, count, inWater);
    GroundUnderBridges(snapshot, resolved, count, inWater);
    RaiseDunes(snapshot, resolved, count);
    SinkLava(snapshot, resolved, count);
}

unsigned DioramaBuildings_PageCount(void)
{
    return sReady ? sPageCount : 0;
}

bool DioramaBuildings_Page(unsigned page, unsigned *width, unsigned *height,
                           const uint8_t **slots)
{
    if (!sReady || page >= sPageCount)
        return false;
    *width = sPages[page].w;
    *height = sPages[page].h;
    *slots = gDioramaBuildingsPageSlots + gDioramaBuildingsPageOffsets[page];
    return true;
}

bool DioramaBuildings_PageWindows(unsigned page, uint8_t *glow)
{
    unsigned w;
    unsigned h;
    unsigned i;
    bool any = false;

    if (!sReady || page >= sPageCount || glow == NULL)
        return false;
    w = sPages[page].w;
    h = sPages[page].h;
    for (i = 0; i < gDioramaBuildingsWindowRunCount; i++)
    {
        const unsigned short *run = gDioramaBuildingsWindowRuns[i];
        unsigned length = run[3] >> 8;
        unsigned level = run[3] & 0xFF;

        /* a run off its page is a stale table's: left out */
        if (run[0] != page || run[1] >= h || run[2] >= w || length == 0 || run[2] + length > w)
            continue;
        memset(glow + (size_t)run[1] * w + run[2], (int)level, length);
        any = true;
    }
    return any;
}
#endif // ENABLE_DIORAMA
