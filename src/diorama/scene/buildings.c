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

#include "diorama/buildings.h"
#include "diorama/rules.h"
#include "diorama/scene_snapshot.h"

extern const unsigned int gDioramaBuildingsTableSize;
extern const unsigned char gDioramaBuildingsTable[];
extern const unsigned int gDioramaBuildingsPageCount;
extern const unsigned int gDioramaBuildingsPageOffsets[];
extern const unsigned char gDioramaBuildingsPageSlots[];

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

bool DioramaBuildings_CellAt(uint16_t layout, int x, int y,
                             uint16_t *ground, uint8_t *quarters, float *top)
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
        if (ground != NULL)
            *ground = p->ground;
        if (quarters != NULL)
            *quarters = sQuarters[k];
        if (top != NULL)
            *top = sHeights[k] / 16.0f;
        return true;
    }
    return false;
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

void DioramaBuildings_FlattenResolvedCells(const struct DioramaSceneSnapshot *snapshot,
                                           struct DioramaResolvedCell *resolved,
                                           unsigned count)
{
    unsigned i;
    int face;

    if (!sReady)
        return;
    DioramaBuildings_Validate(snapshot);
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
                r->materials[face].metatileId = ground;
                r->materials[face].layer = DIORAMA_MATERIAL_FULL;
            }
            r->materials[face].rotation = 0;
            r->materials[face].flags = 0;
        }
    }
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
#endif // ENABLE_DIORAMA
