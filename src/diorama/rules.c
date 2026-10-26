#ifdef ENABLE_DIORAMA

#include <stddef.h>
#include <string.h>

#include "constants/map_types.h"
#include "constants/metatile_behaviors.h"
#include "diorama/rules.generated.h"
#include "diorama/rules.h"

static int FindCell(const struct DioramaSceneSnapshot *snapshot,
                    const struct DioramaCellSnapshot *origin, int dx, int dy);
static const struct DioramaGeneratedLayoutV2 *FindLayout(uint16_t layoutId);

static void DisableMeasuredVolume(struct DioramaResolvedCell *resolved)
{
    resolved->shape = DIORAMA_SHAPE_FLAT;
    resolved->profile = DIORAMA_ROOF_NONE;
    resolved->featureHeight = 0.0f;
    resolved->topHeight = resolved->groundHeight;
    resolved->structureBodyHeight = 0.0f;
    resolved->structureRoofHeight = 0.0f;
    resolved->structureRoofRows = 0;
    resolved->measuredBandCount = 0;
}

static const struct DioramaCellSnapshot *FindSourceCell(
    const struct DioramaSceneSnapshot *snapshot, const struct DioramaCellSnapshot *origin,
    uint16_t layoutId, int16_t sourceX, int16_t sourceY)
{
    uint16_t i;

    for (i = 0; i < snapshot->visibleCellCount && i < DIORAMA_MAX_VISIBLE_CELLS; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];

        if ((cell->flags & DIORAMA_CELL_SOURCE_VALID)
         && cell->sourceMapGroup == origin->sourceMapGroup
         && cell->sourceMapNum == origin->sourceMapNum
         && cell->sourceLayoutId == layoutId
         && cell->sourceMapX == sourceX && cell->sourceMapY == sourceY)
            return cell;
    }
    return NULL;
}

static bool PixelObjectMatchesSnapshot(const struct DioramaSceneSnapshot *snapshot,
                                       const struct DioramaCellSnapshot *origin,
                                       const struct DioramaGeneratedPixelObjectV2 *object)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(object->layoutId);
    const struct DioramaGeneratedPixelV2 *pixels;
    size_t count;
    size_t i;
    uint32_t previousOffset = UINT32_MAX;

    if (layout == NULL)
        return false;
    pixels = DioramaRules_GetPixelObjectPixels(object, &count);
    if (pixels == NULL)
        return false;
    for (i = 0; i < count; i++)
    {
        if (pixels[i].sourceCellOffset == previousOffset)
            continue;
        previousOffset = pixels[i].sourceCellOffset;
        const struct DioramaCellSnapshot *cell = FindSourceCell(
            snapshot, origin, object->layoutId,
            pixels[i].sourceCellOffset % layout->width,
            pixels[i].sourceCellOffset / layout->width);

        if (cell != NULL && cell->metatileId != pixels[i].expectedMetatile)
            return false;
    }
    return true;
}

static bool PixelObjectSupportMatchesSnapshot(
    const struct DioramaSceneSnapshot *snapshot, const struct DioramaResolvedCell *resolvedCells,
    uint16_t count, const struct DioramaCellSnapshot *origin,
    const struct DioramaGeneratedPixelObjectV2 *object)
{
    const struct DioramaGeneratedStructureV2 *support;
    const struct DioramaGeneratedLayoutV2 *layout;
    uint16_t i;
    uint32_t cellIndex;

    if (object->supportStructureId == 0)
        return true;
    for (i = 0; i < count; i++)
        if (resolvedCells[i].structureId == object->supportStructureId
         && snapshot->cells[i].sourceMapGroup == origin->sourceMapGroup
         && snapshot->cells[i].sourceMapNum == origin->sourceMapNum)
            return true;
    support = DioramaRules_GetStructure(object->supportStructureId);
    layout = support != NULL ? FindLayout(support->layoutId) : NULL;
    if (support == NULL || layout == NULL
     || support->cellOffset > gDioramaStructureCellV2Count
     || support->cellCount > gDioramaStructureCellV2Count - support->cellOffset)
        return false;
    for (cellIndex = 0; cellIndex < support->cellCount; cellIndex++)
    {
        uint32_t offset = gDioramaStructureCellsV2[support->cellOffset + cellIndex];

        if (FindSourceCell(snapshot, origin, support->layoutId,
                           offset % layout->width, offset / layout->width) != NULL)
            return false;
    }
    return true;
}

static const struct DioramaGeneratedLayoutV2 *FindLayout(uint16_t layoutId)
{
    if (layoutId == 0 || layoutId > gDioramaLayoutV2Count)
        return NULL;
    if (gDioramaLayoutsV2[layoutId - 1].id == layoutId)
        return &gDioramaLayoutsV2[layoutId - 1];
    return NULL;
}

static const struct DioramaGeneratedTerrainV2 *FindStaticTerrain(
    const struct DioramaGeneratedLayoutV2 *layout, uint32_t cellOffset,
    uint8_t mapGroup, uint8_t mapNum)
{
    uint32_t low = 0;
    uint32_t high;

    if (layout == NULL || layout->terrainRecordOffset > gDioramaTerrainV2Count
     || layout->terrainRecordCount > gDioramaTerrainV2Count - layout->terrainRecordOffset)
        return NULL;
    high = layout->terrainRecordCount;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        const struct DioramaGeneratedTerrainV2 *record =
            &gDioramaTerrainV2[layout->terrainRecordOffset + middle];

        if (record->cellOffset < cellOffset)
            low = middle + 1;
        else
            high = middle;
    }
    while (low < layout->terrainRecordCount)
    {
        const struct DioramaGeneratedTerrainV2 *record =
            &gDioramaTerrainV2[layout->terrainRecordOffset + low++];

        if (record->cellOffset != cellOffset)
            break;
        if (record->mapGroup == mapGroup && record->mapNumber == mapNum)
            return record;
        if (record->mapGroup == UINT8_MAX && record->mapNumber == UINT8_MAX)
            return record;
    }
    return NULL;
}

static const struct DioramaGeneratedStructureCellV2 *FindStaticStructure(
    const struct DioramaGeneratedLayoutV2 *layout, uint32_t cellOffset,
    uint16_t expectedMetatile, uint8_t mapGroup, uint8_t mapNum)
{
    size_t i;

    for (i = 0; i < gDioramaStructureOverrideV2Count; i++)
    {
        const struct DioramaGeneratedStructureOverrideV2 *override =
            &gDioramaStructureOverridesV2[i];

        if (override->layoutId == layout->id && override->cellOffset == cellOffset
         && override->expectedMetatile == expectedMetatile
         && override->mapGroup == mapGroup && override->mapNumber == mapNum)
            return &override->cell;
    }
    if (layout->structureCellOffset > gDioramaStructureOwnerV2Count
     || cellOffset >= gDioramaStructureOwnerV2Count - layout->structureCellOffset)
        return NULL;
    return &gDioramaStructureOwnersV2[layout->structureCellOffset + cellOffset];
}

static bool StructureIsDirty(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell,
                             const struct DioramaGeneratedStructureV2 *structure,
                             const struct DioramaGeneratedLayoutV2 *layout)
{
    uint8_t dirtyIndex;

    if (structure == NULL || cell->sourceMapGroup != snapshot->mapGroup
     || cell->sourceMapNum != snapshot->mapNum)
        return false;
    if (snapshot->dirtyOverflow)
        return true;
    if (structure->cellOffset > gDioramaStructureCellV2Count
     || structure->cellCount > gDioramaStructureCellV2Count - structure->cellOffset)
        return true;
    for (dirtyIndex = 0; dirtyIndex < snapshot->dirtyCellCount; dirtyIndex++)
    {
        int x = snapshot->dirtyCells[dirtyIndex].mapX - snapshot->mapCoordinateOffset;
        int y = snapshot->dirtyCells[dirtyIndex].mapY - snapshot->mapCoordinateOffset;
        uint32_t target;
        uint32_t low = 0;
        uint32_t high = structure->cellCount;

        if (x < 0 || y < 0 || x >= layout->width || y >= layout->height)
            continue;
        target = (uint32_t)y * layout->width + x;
        while (low < high)
        {
            uint32_t middle = low + (high - low) / 2;
            uint32_t value = gDioramaStructureCellsV2[structure->cellOffset + middle];

            if (value < target)
                low = middle + 1;
            else
                high = middle;
        }
        if (low < structure->cellCount
         && gDioramaStructureCellsV2[structure->cellOffset + low] == target)
            return true;
    }
    return false;
}

static void InitializeResolved(struct DioramaResolvedCell *resolved)
{
    size_t i;

    memset(resolved, 0, sizeof(*resolved));
    resolved->shape = DIORAMA_SHAPE_FLAT;
    resolved->archetype = DIORAMA_ARCHETYPE_GROUND;
    resolved->terrainClass = DIORAMA_TERRAIN_CLASS_GROUND;
    resolved->classifierClass = 1;
    resolved->artMode = DIORAMA_ART_FLAT;
    resolved->source = DIORAMA_RULE_SOURCE_FALLBACK;
    resolved->baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
    resolved->rulePriority = -32768;
    for (i = 0; i < DIORAMA_MATERIAL_FACE_COUNT; i++)
    {
        resolved->materials[i].metatileId = DIORAMA_MATERIAL_METATILE_SELF;
        resolved->materials[i].layer = i == DIORAMA_MATERIAL_FACE_PLANE
            ? DIORAMA_MATERIAL_FOREGROUND : DIORAMA_MATERIAL_FULL;
    }
}

static void ApplyStaticTerrain(const struct DioramaSceneSnapshot *snapshot,
                               const struct DioramaCellSnapshot *cell,
                               struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedLayoutV2 *layout;
    const struct DioramaGeneratedTerrainV2 *record;
    const struct DioramaGeneratedStructureCellV2 *structureCell;
    const struct DioramaGeneratedStructureV2 *structure;
    uint32_t cellOffset;

    if (!(cell->flags & DIORAMA_CELL_SOURCE_VALID) || cell->sourceMapX < 0
     || cell->sourceMapY < 0)
        return;
    layout = FindLayout(cell->sourceLayoutId);
    if (layout == NULL || cell->sourceMapX >= layout->width || cell->sourceMapY >= layout->height)
        return;
    cellOffset = (uint32_t)cell->sourceMapY * layout->width + cell->sourceMapX;
    record = FindStaticTerrain(layout, cellOffset, cell->sourceMapGroup, cell->sourceMapNum);
    if (record == NULL)
        return;
    if (record->expectedMetatile != cell->metatileId)
    {
        /* A cell a script redrew (Route 113's ash brushed off the grass the player
         * walks through, a barrier gone): the new picture is not the one this record
         * was made for, but the cell stays at its place's level - the terrace solver's
         * in a lowered region - instead of the flat ground's, which stood it a whole
         * level over its neighbours (a block at the player's feet, in the field and in
         * battles). Added by the battle environments' work. */
        if (record->measuredFlags & DIORAMA_MEASURED_TERRACE_TOPOLOGY)
        {
            resolved->groundHeight = record->groundQ16 / 16.0f;
            resolved->topHeight = resolved->groundHeight;
        }
        return;
    }
    if (record->measuredBandCount > DIORAMA_MAX_MEASURED_BANDS
     || record->measuredBandOffset > gDioramaMeasuredBandV2Count
     || record->measuredBandCount > gDioramaMeasuredBandV2Count - record->measuredBandOffset)
        return;
    for (uint8_t bandIndex = 0; bandIndex < record->measuredBandCount; bandIndex++)
    {
        const struct DioramaGeneratedMeasuredBandV2 *band =
            &gDioramaMeasuredBandsV2[record->measuredBandOffset + bandIndex];
        const struct DioramaCellSnapshot *sourceCell = FindSourceCell(
            snapshot, cell, layout->id, band->sourceCellOffset % layout->width,
            band->sourceCellOffset / layout->width);

        if (sourceCell != NULL && sourceCell->metatileId != band->expectedMetatile)
            return;
    }
    resolved->shape = record->shape;
    resolved->archetype = record->archetype;
    resolved->terrainClass = record->terrainClass;
    resolved->classifierClass = record->classId;
    resolved->classifierSource = record->sourceId;
    resolved->semanticPool = record->pool;
    resolved->artMode = record->artMode;
    resolved->authored = record->authored;
    resolved->source = record->sourceKind;
    resolved->classifierConfidence = record->confidence;
    resolved->evidenceFlags = record->evidenceFlags;
    resolved->ambiguityFlags = record->ambiguityFlags;
    resolved->evidenceDetailsId = record->evidenceDetailsId;
    resolved->ambiguityDetailsId = record->ambiguityDetailsId;
    resolved->groundMode = record->propGroundMode;
    resolved->baseMetatileId = record->propGroundMetatile;
    resolved->planeAxis = record->axis;
    if (record->measuredFlags & DIORAMA_MEASURED_TERRACE_TOPOLOGY)
    {
        resolved->groundHeight = record->groundQ16 / 16.0f;
        resolved->topHeight = record->topQ16 / 16.0f;
        resolved->featureHeight = resolved->topHeight - resolved->groundHeight;
    }
    else
    {
        resolved->featureHeight = record->heightQ16 > 0 ? record->heightQ16 / 16.0f : 0.0f;
        resolved->groundHeight = record->heightQ16 < 0 ? record->heightQ16 / 16.0f : 0.0f;
        if (record->shape == DIORAMA_SHAPE_LEDGE)
            resolved->groundHeight = resolved->featureHeight;
        resolved->topHeight = resolved->groundHeight + resolved->featureHeight;
        if (record->shape == DIORAMA_SHAPE_LEDGE)
            resolved->topHeight = resolved->groundHeight;
    }
    resolved->cliffEdgeMask = record->cliffEdgeMask;
    resolved->cliffBaseMask = record->cliffBaseMask;
    resolved->cliffTransitionMask = record->cliffTransitionMask;
    resolved->cliffCornerMask = record->cliffCornerMask;
    resolved->terraceProfile = record->terraceProfile;
    resolved->measuredAxis = record->measuredAxis;
    resolved->measuredExtentBands = record->measuredExtentBands;
    resolved->measuredPeriodBands = record->measuredPeriodBands;
    resolved->measuredRoofBands = record->measuredRoofBands;
    resolved->measuredRunLocal = record->measuredRunLocal;
    resolved->measuredRunLength = record->measuredRunLength;
    resolved->measuredFlags = record->measuredFlags;
    resolved->measuredBandCount = record->measuredBandCount;
    resolved->measuredConfidence = record->measuredConfidence;
    resolved->structureBodyHeight = record->measuredBodyQ16 / 16.0f;
    resolved->structureRoofHeight = record->measuredRoofQ16 / 16.0f;
    resolved->structureRoofRows = record->measuredRoofBands;
    if (record->measuredRoofQ16 != 0)
        resolved->profile = record->measuredAxis == DIORAMA_PLANE_AXIS_Z
                          ? DIORAMA_ROOF_GABLE_Z : DIORAMA_ROOF_GABLE_X;
    for (uint8_t bandIndex = 0; bandIndex < record->measuredBandCount; bandIndex++)
    {
        const struct DioramaGeneratedMeasuredBandV2 *band =
            &gDioramaMeasuredBandsV2[record->measuredBandOffset + bandIndex];

        resolved->measuredBands[bandIndex].metatileId = band->expectedMetatile;
        resolved->measuredBands[bandIndex].layer = band->layer;
        resolved->measuredBands[bandIndex].sourceHalf = band->sourceHalf;
    }
    structureCell = FindStaticStructure(layout, cellOffset, cell->metatileId,
                                        cell->sourceMapGroup, cell->sourceMapNum);
    if (structureCell == NULL)
    {
        if (record->measuredBandCount != 0)
            DisableMeasuredVolume(resolved);
        return;
    }
    resolved->voidKind = structureCell->voidKind;
    if (!StructureIsDirty(snapshot, cell,
                          DioramaRules_GetStructure(structureCell->claimOwner != 0
                              ? structureCell->claimOwner : structureCell->regionId), layout))
    {
        resolved->claimOwner = structureCell->claimOwner;
        resolved->regionId = structureCell->regionId;
        resolved->structureId = resolved->claimOwner != 0
                              ? resolved->claimOwner : resolved->regionId;
        resolved->doorFold = structureCell->doorFold;
        structure = DioramaRules_GetStructure(resolved->structureId);
        if (structure != NULL)
        {
            const struct DioramaGeneratedPixelObjectV2 *pixelObject;

            resolved->structureTemplateId = structure->kind == DIORAMA_OWNER_TEMPLATE
                                          ? structure->id : 0;
            resolved->structureX = structure->x;
            resolved->structureY = structure->y;
            resolved->structureWidth = structure->width;
            resolved->structureHeight = structure->height;
            resolved->structureLocalX = cell->sourceMapX - structure->x;
            resolved->structureLocalY = cell->sourceMapY - structure->y;
            resolved->structureOwnerKind = structure->kind;
            resolved->rulePriority = structure->priority;
            if (structure->kind == DIORAMA_OWNER_TEMPLATE
             && structure->width == 2
             && structure->height >= 1 && structure->height <= 3
             && strcmp(DioramaRules_ClassName(structure->classId), "grouped-hull") == 0)
            {
                uint8_t bodyRows = structure->height >= 2 ? 2 : 1;
                uint8_t bodyStart = structure->height - bodyRows;
                uint32_t topOffset = (uint32_t)structure->y * layout->width
                                   + structure->x + resolved->structureLocalX;
                const struct DioramaGeneratedTerrainV2 *topRecord = FindStaticTerrain(
                    layout, topOffset, cell->sourceMapGroup, cell->sourceMapNum);

                resolved->shape = DIORAMA_SHAPE_EXTRUDED;
                resolved->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
                resolved->featureHeight = bodyRows;
                resolved->topHeight = resolved->groundHeight + bodyRows;
                if (topRecord != NULL)
                {
                    resolved->materials[DIORAMA_MATERIAL_FACE_TOP].metatileId =
                        topRecord->expectedMetatile;
                    resolved->materials[DIORAMA_MATERIAL_FACE_TOP].layer =
                        DIORAMA_MATERIAL_FOREGROUND;
                }
                if (resolved->structureLocalY >= bodyStart)
                {
                    resolved->measuredAxis = DIORAMA_PLANE_AXIS_Z;
                    resolved->measuredBandCount = bodyRows * 2;
                    resolved->measuredExtentBands = bodyRows * 2;
                    resolved->measuredPeriodBands = bodyRows * 2;
                    for (uint8_t band = 0; band < resolved->measuredBandCount; band++)
                    {
                        uint16_t sourceY = structure->y + structure->height - 1 - band / 2;
                        uint32_t sourceOffset = (uint32_t)sourceY * layout->width
                                              + structure->x + resolved->structureLocalX;
                        const struct DioramaGeneratedTerrainV2 *sourceRecord = FindStaticTerrain(
                            layout, sourceOffset, cell->sourceMapGroup, cell->sourceMapNum);

                        if (sourceRecord == NULL)
                        {
                            resolved->measuredBandCount = 0;
                            break;
                        }
                        resolved->measuredBands[band].metatileId =
                            sourceRecord->expectedMetatile;
                        resolved->measuredBands[band].layer = DIORAMA_MATERIAL_FOREGROUND;
                        resolved->measuredBands[band].sourceHalf = (band & 1) ? 0 : 1;
                    }
                }
            }
            if (resolved->archetype == DIORAMA_ARCHETYPE_ROUND_HULL)
                for (uint8_t face = DIORAMA_MATERIAL_FACE_TOP;
                     face < DIORAMA_MATERIAL_FACE_COUNT; face++)
                    resolved->materials[face].layer = DIORAMA_MATERIAL_FOREGROUND;
            pixelObject = DioramaRules_FindPixelObject(layout->id,
                cell->sourceMapGroup, cell->sourceMapNum, structure->id);
            if (pixelObject != NULL)
            {
                resolved->pixelObjectId = pixelObject->id;
                resolved->groundMode = pixelObject->groundMode;
                resolved->baseMetatileId = pixelObject->groundMetatile;
            }
        }
    }
    else if (record->measuredBandCount != 0)
    {
        DisableMeasuredVolume(resolved);
    }
}

static const struct DioramaGeneratedMapV2 *FindMap(uint8_t mapGroup, uint8_t mapNum,
                                                    uint16_t layoutId)
{
    size_t i;

    for (i = 0; i < gDioramaMapV2Count; i++)
        if (gDioramaMapsV2[i].group == mapGroup
         && gDioramaMapsV2[i].number == mapNum
         && gDioramaMapsV2[i].layoutId == layoutId)
            return &gDioramaMapsV2[i];
    return NULL;
}

static const struct DioramaGeneratedMapV2 *FindMapIdentity(uint8_t mapGroup, uint8_t mapNum)
{
    size_t i;

    for (i = 0; i < gDioramaMapV2Count; i++)
        if (gDioramaMapsV2[i].group == mapGroup && gDioramaMapsV2[i].number == mapNum)
            return &gDioramaMapsV2[i];
    return NULL;
}

bool DioramaRules_ProfileOccupies(uint8_t profileId, uint8_t x, uint8_t y)
{
    size_t profileIndex;

    if (profileId == 0)
        return x < DIORAMA_BUILDING_PIXELS_PER_CELL && y < DIORAMA_BUILDING_PIXELS_PER_CELL;
    for (profileIndex = 0; profileIndex < gDioramaProfileV2Count; profileIndex++)
    {
        const struct DioramaGeneratedProfileV2 *profile = &gDioramaProfilesV2[profileIndex];
        uint16_t maskIndex;

        if (profile->id != profileId)
            continue;
        for (maskIndex = 0; maskIndex < profile->maskCount; maskIndex++)
        {
            const struct DioramaGeneratedMaskV2 *mask =
                &gDioramaMasksV2[profile->maskOffset + maskIndex];

            if (mask->kind != 1)
                continue;
            if (x >= mask->width || y >= mask->height || y >= mask->rowCount)
                return false;
            return (gDioramaMaskRowsV2[mask->rowOffset + y] & (UINT64_C(1) << x)) != 0;
        }
        return x < DIORAMA_BUILDING_PIXELS_PER_CELL && y < DIORAMA_BUILDING_PIXELS_PER_CELL;
    }
    return false;
}

bool DioramaRules_ProfileIsFullCell(uint8_t profileId)
{
    uint8_t y;

    for (y = 0; y < DIORAMA_BUILDING_PIXELS_PER_CELL; y++)
    {
        uint8_t x;
        for (x = 0; x < DIORAMA_BUILDING_PIXELS_PER_CELL; x++)
            if (!DioramaRules_ProfileOccupies(profileId, x, y))
                return false;
    }
    return true;
}

static uint8_t ResolveEffectiveElevation(const struct DioramaSceneSnapshot *snapshot,
                                         const struct DioramaCellSnapshot *cell)
{
    static const int8_t sCardinalOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    uint8_t counts[15] = {0};
    uint8_t best = 0;
    uint8_t bestCount = 0;
    int direction;

    if (cell->elevation > 0 && cell->elevation < 15)
        return cell->elevation;
    for (direction = 0; direction < 4; direction++)
    {
        int index = FindCell(snapshot, cell, sCardinalOffsets[direction][0],
                             sCardinalOffsets[direction][1]);
        uint8_t elevation;

        if (index < 0)
            continue;
        elevation = snapshot->cells[index].elevation;
        if (elevation == 0 || elevation == 15)
            continue;
        counts[elevation]++;
        if (counts[elevation] > bestCount)
        {
            best = elevation;
            bestCount = counts[elevation];
        }
    }
    if (bestCount != 0)
        for (direction = 1; direction < 15; direction++)
            if (direction != best && counts[direction] == bestCount)
                return 0;
    return best;
}

static void FinalizeSurfaces(const struct DioramaSceneSnapshot *snapshot,
                             const struct DioramaCellSnapshot *cell,
                             struct DioramaResolvedCell *resolved)
{
    float top;

    top = resolved->topHeight;

    if ((cell->elevation > 0 && cell->elevation < 15)
     || resolved->effectiveElevation == 0)
        resolved->effectiveElevation = ResolveEffectiveElevation(snapshot, cell);
    resolved->surfaceCount = resolved->shape == DIORAMA_SHAPE_HIDDEN ? 0 : 1;
    resolved->surfaces[0].topHeight = top;
    resolved->surfaces[0].bottomHeight = top - 1.0f / 16.0f;
    resolved->surfaces[0].gameplayElevation = resolved->effectiveElevation;
    if (resolved->shape == DIORAMA_SHAPE_BRIDGE)
    {
        resolved->surfaceCount = 2;
        resolved->surfaces[0].topHeight = resolved->groundHeight;
        resolved->surfaces[0].bottomHeight = resolved->groundHeight - 1.0f / 16.0f;
        resolved->surfaces[0].gameplayElevation = resolved->effectiveElevation;
        resolved->surfaces[1].topHeight = -0.125f;
        resolved->surfaces[1].bottomHeight = -0.1875f;
        resolved->surfaces[1].gameplayElevation = resolved->effectiveElevation == 1 ? 0 : 1;
    }
}

static int FindCell(const struct DioramaSceneSnapshot *snapshot,
                    const struct DioramaCellSnapshot *origin, int dx, int dy)
{
    int mapX = origin->mapX + dx;
    int mapY = origin->mapY + dy;
    int gridX = mapX - snapshot->gridOriginX;
    int gridY = mapY - snapshot->gridOriginY;
    int index;

    if (gridX < 0 || gridY < 0 || gridX >= DIORAMA_GRID_WIDTH || gridY >= DIORAMA_GRID_HEIGHT)
        return -1;
    index = gridY * DIORAMA_GRID_WIDTH + gridX;
    if (index >= snapshot->visibleCellCount || index >= DIORAMA_MAX_VISIBLE_CELLS
     || snapshot->cells[index].mapX != mapX || snapshot->cells[index].mapY != mapY)
        return -1;
    return index;
}

static void ClaimBorderTreeCell(const struct DioramaCellSnapshot *cell,
                                struct DioramaResolvedCell *resolved,
                                uint16_t structureId, uint8_t localX, uint8_t localY)
{
    resolved->shape = DIORAMA_SHAPE_EXTRUDED;
    resolved->archetype = DIORAMA_ARCHETYPE_GROUPED_HULL;
    resolved->artMode = DIORAMA_ART_UPRIGHT;
    resolved->source = DIORAMA_RULE_SOURCE_PATTERN;
    resolved->claimOwner = structureId;
    resolved->regionId = structureId;
    resolved->structureId = structureId;
    resolved->rulePriority = 415;
    resolved->structureX = cell->mapX - localX;
    resolved->structureY = cell->mapY - localY;
    resolved->structureWidth = 2;
    resolved->structureHeight = 2;
    resolved->structureLocalX = localX;
    resolved->structureLocalY = localY;
    resolved->structureOwnerKind = DIORAMA_OWNER_TEMPLATE;
    resolved->featureHeight = 2.0f;
    resolved->topHeight = resolved->groundHeight + 2.0f;
}

static void ClaimBorderTrees(const struct DioramaSceneSnapshot *snapshot,
                             struct DioramaResolvedCell *resolvedCells, uint16_t count)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(snapshot->mapLayoutId);

    if (layout == NULL || layout->primaryTilesetId == 0
     || layout->primaryTilesetId > gDioramaTilesetV2Count
     || strcmp(gDioramaTilesetsV2[layout->primaryTilesetId - 1].symbol,
               "gTileset_General") != 0)
        return;
    for (uint16_t index = 0; index < count; index++)
    {
        const struct DioramaCellSnapshot *northWest = &snapshot->cells[index];
        uint16_t structureId;
        int northEast;
        int southWest;
        int southEast;
        int members[4];

        if (!(northWest->flags & DIORAMA_CELL_BORDER) || northWest->metatileId != 468)
            continue;
        northEast = FindCell(snapshot, northWest, 1, 0);
        southWest = FindCell(snapshot, northWest, 0, 1);
        southEast = FindCell(snapshot, northWest, 1, 1);
        if (northEast < 0 || southWest < 0 || southEast < 0
         || !(snapshot->cells[northEast].flags & DIORAMA_CELL_BORDER)
         || !(snapshot->cells[southWest].flags & DIORAMA_CELL_BORDER)
         || !(snapshot->cells[southEast].flags & DIORAMA_CELL_BORDER)
         || snapshot->cells[northEast].sourceLayoutId != northWest->sourceLayoutId
         || snapshot->cells[southWest].sourceLayoutId != northWest->sourceLayoutId
         || snapshot->cells[southEast].sourceLayoutId != northWest->sourceLayoutId
         || snapshot->cells[northEast].metatileId != 469
         || snapshot->cells[southWest].metatileId != 476
         || snapshot->cells[southEast].metatileId != 477)
            continue;
        members[0] = index;
        members[1] = northEast;
        members[2] = southWest;
        members[3] = southEast;
        structureId = UINT16_C(0x8000)
                    | ((northWest->mapX & 0x7F) << 8)
                    | (northWest->mapY & 0xFF);
        for (uint8_t member = 0; member < 4; member++)
            ClaimBorderTreeCell(&snapshot->cells[members[member]],
                                &resolvedCells[members[member]], structureId,
                                member & 1, member >> 1);
    }
}

uint32_t DioramaRules_GetGeneration(void)
{
    return gDioramaRulesGeneration;
}

const char *DioramaRules_GetSha256(void)
{
    return gDioramaRulesSha256;
}

const char *DioramaRules_ClassName(uint16_t classId)
{
    static const char *const sNames[] = {
        NULL, "ground", "void", "water", "shallow-water", "waterfall", "current",
        "hot-spring", "grass", "flower", "animated-cutout", "ledge", "mound",
        "stairs", "stairs-n", "stairs-s", "stairs-e", "stairs-w", "stairs-down-n",
        "stairs-down-s", "stairs-down-e", "stairs-down-w", "cliff", "wall",
        "wall-volume", "roof", "top-slab", "bridge", "deck", "rail", "support",
        "tree", "forest-wall", "shrub", "hedge", "rock", "boulder", "building",
        "awning", "claim-only", "counter", "table", "desk", "bed", "bookcase",
        "billboard", "cutout", "console", "signpost", "post", "stump", "round-hull",
        "grouped-hull", "relief"
    };

    return classId < sizeof(sNames) / sizeof(sNames[0]) ? sNames[classId] : NULL;
}

const char *DioramaRules_ArtModeName(uint8_t artMode)
{
    static const char *const sNames[] = {
        "flat", "top", "upright", "grass", "flower", "stair", "cutout"
    };

    return artMode < sizeof(sNames) / sizeof(sNames[0]) ? sNames[artMode] : NULL;
}

const char *DioramaRules_ClassifierSourceName(uint16_t sourceId)
{
    return sourceId != 0 && sourceId <= gDioramaClassifierSourceV2Count
        && gDioramaClassifierSourcesV2[sourceId - 1].id == sourceId
        ? gDioramaClassifierSourcesV2[sourceId - 1].name : NULL;
}

const char *DioramaRules_EvidenceDetails(uint16_t detailsId)
{
    return detailsId != 0 && detailsId <= gDioramaEvidenceDetailV2Count
        && gDioramaEvidenceDetailsV2[detailsId - 1].id == detailsId
        ? gDioramaEvidenceDetailsV2[detailsId - 1].details : NULL;
}

const char *DioramaRules_AmbiguityDetails(uint16_t detailsId)
{
    return detailsId != 0 && detailsId <= gDioramaAmbiguityV2Count
        && gDioramaAmbiguitiesV2[detailsId - 1].id == detailsId
        ? gDioramaAmbiguitiesV2[detailsId - 1].details : NULL;
}

const struct DioramaGeneratedStructureV2 *DioramaRules_GetStructure(uint16_t id)
{
    return id != 0 && id <= gDioramaStructureV2Count
        && gDioramaStructuresV2[id - 1].id == id ? &gDioramaStructuresV2[id - 1] : NULL;
}

const struct DioramaGeneratedPixelObjectV2 *DioramaRules_GetPixelObject(uint16_t id)
{
    return id != 0 && id <= gDioramaPixelObjectV2Count
        && gDioramaPixelObjectsV2[id - 1].id == id ? &gDioramaPixelObjectsV2[id - 1] : NULL;
}

const struct DioramaGeneratedPixelObjectV2 *DioramaRules_FindPixelObject(
    uint16_t layoutId, uint8_t mapGroup, uint8_t mapNum, uint16_t structureId)
{
    const struct DioramaGeneratedPixelObjectV2 *fallback = NULL;
    size_t i;

    for (i = 0; i < gDioramaPixelObjectV2Count; i++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[i];

        if (object->layoutId != layoutId || object->structureId != structureId)
            continue;
        if (object->mapGroup == mapGroup && object->mapNumber == mapNum)
            return object;
        if (object->mapGroup == UINT8_MAX && object->mapNumber == UINT8_MAX)
            fallback = object;
    }
    return fallback;
}

const struct DioramaGeneratedPixelV2 *DioramaRules_GetPixelObjectPixels(
    const struct DioramaGeneratedPixelObjectV2 *object, size_t *count)
{
    if (count != NULL)
        *count = 0;
    if (object == NULL || object->pixelOffset > gDioramaPixelV2Count
     || object->pixelCount > gDioramaPixelV2Count - object->pixelOffset)
        return NULL;
    if (count != NULL)
        *count = object->pixelCount;
    return &gDioramaPixelsV2[object->pixelOffset];
}

const char *DioramaRules_StructureKindName(uint8_t kind)
{
    static const char *const sNames[] = {
        "none", "template", "authored", "prop", "volume", "region"
    };

    return kind < sizeof(sNames) / sizeof(sNames[0]) ? sNames[kind] : NULL;
}

const struct DioramaGeneratedBuildingTemplate *DioramaRules_GetBuildingTemplate(uint16_t id)
{
    (void)id;
    return NULL;
}

bool DioramaRules_IsMapSupported(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    return map != NULL && map->supported;
}

const char *DioramaRules_GetUnsupportedReason(uint8_t mapGroup, uint8_t mapNum,
                                              uint16_t layoutId)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    return map == NULL ? "Map is absent from the generated G3 catalog."
                       : map->unsupportedReason;
}

bool DioramaRules_GetMapProfile(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId,
                                struct DioramaMapProfile *profile)
{
    const struct DioramaGeneratedMapV2 *map = FindMap(mapGroup, mapNum, layoutId);

    if (map == NULL && FindLayout(layoutId) != NULL)
        map = FindMapIdentity(mapGroup, mapNum);
    if (map == NULL || profile == NULL || !map->supported)
        return false;
    profile->cameraProfile = map->cameraProfile;
    profile->cameraPitch = map->pitchRadians;
    profile->cameraFocalLength = map->focalLength;
    return true;
}

bool DioramaRules_StaticHeights(uint16_t layoutId, uint8_t mapGroup, uint8_t mapNum,
                                int16_t sourceX, int16_t sourceY, uint16_t metatileId,
                                float *groundHeight, float *topHeight, uint8_t *archetype)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(layoutId);
    const struct DioramaGeneratedTerrainV2 *record;
    float ground;
    float top;

    if (layout == NULL || sourceX < 0 || sourceY < 0
     || sourceX >= layout->width || sourceY >= layout->height)
        return false;
    record = FindStaticTerrain(layout, (uint32_t)sourceY * layout->width + sourceX, mapGroup, mapNum);
    if (record == NULL || record->expectedMetatile != metatileId)
        return false;
    /* ApplyStaticTerrain's heights */
    if (record->measuredFlags & DIORAMA_MEASURED_TERRACE_TOPOLOGY)
    {
        ground = record->groundQ16 / 16.0f;
        top = record->topQ16 / 16.0f;
    }
    else
    {
        float feature = record->heightQ16 > 0 ? record->heightQ16 / 16.0f : 0.0f;

        ground = record->heightQ16 < 0 ? record->heightQ16 / 16.0f : 0.0f;
        if (record->shape == DIORAMA_SHAPE_LEDGE)
            ground = feature;
        top = record->shape == DIORAMA_SHAPE_LEDGE ? ground : ground + feature;
    }
    if (record->measuredBodyQ16 + record->measuredRoofQ16 > 0
     && ground + (record->measuredBodyQ16 + record->measuredRoofQ16) / 16.0f > top)
        top = ground + (record->measuredBodyQ16 + record->measuredRoofQ16) / 16.0f;
    *groundHeight = ground;
    *topHeight = top;
    *archetype = record->archetype;
    return true;
}

/*
 * Past a cave's edge (the owner's report of 2026-10-23: "it looks like u can
 * walk through some places when u cant"): the map's border block lay as flat
 * ground all round a cave, and most caves draw it with their own rock (Granite
 * Cave with the sand it also tops its rock with). On an underground map a
 * border cell takes the record of a cell inside the map with the same metatile
 * standing as rock on the cave's lowest level (compile_rules.py: the cave's
 * pins and its rock masses), so the cave is closed in rock; a border no rock in
 * the map is drawn with (the black past the edge, Navel Rock's sky) stays flat.
 *
 * Past an outdoor map's edge the border block is mostly the sea (an ocean route
 * with no neighbour that way). With no record it lay as flat ground at the
 * land's level, a whole level over the map's own sea (buildings.c
 * LevelWaterBodies lowers only water), and stood along the edge as a wall of
 * water - the seams the 3D sweep found on Routes 105, 108, 109, 122, 125 and
 * 130-132 (2026-10-26). A border cell whose tile is the sea takes the record of
 * a cell inside the map with the same metatile standing as water (the highest
 * of them: LevelWaterBodies then lays it on the sea's level, as it does the
 * map's own), so the sea goes on past the edge at one level.
 */
static bool IsBorderSea(uint8_t behavior)
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

static bool IsOutdoorMap(uint8_t mapType)
{
    return mapType == MAP_TYPE_TOWN || mapType == MAP_TYPE_CITY || mapType == MAP_TYPE_ROUTE
        || mapType == MAP_TYPE_OCEAN_ROUTE;
}

static const struct DioramaGeneratedTerrainV2 *BorderRecord(
    const struct DioramaSceneSnapshot *snapshot, const struct DioramaCellSnapshot *cell)
{
    /* the last few answers (a border is one or two metatiles), as (layout << 16 | metatile) + 1 and
     * the record's index + 1: the render thread's grid and the game thread's sprites both ask, so an
     * entry is used only when its key reads the same before and after its record */
    static volatile uint32_t sKey[4];
    static volatile uint32_t sRecord[4];
    static volatile uint8_t sNext;
    const struct DioramaGeneratedTerrainV2 *found = NULL;
    const struct DioramaGeneratedLayoutV2 *layout;
    uint32_t key;
    uint32_t i;

    bool sea;

    if (!(cell->flags & DIORAMA_CELL_BORDER) || (cell->flags & DIORAMA_CELL_SOURCE_VALID))
        return NULL;
    sea = IsOutdoorMap(snapshot->mapType) && IsBorderSea(cell->behavior);
    if (snapshot->mapType != MAP_TYPE_UNDERGROUND && !sea)
        return NULL;
    key = ((uint32_t)cell->sourceLayoutId << 16 | cell->metatileId) + 1;
    for (i = 0; i < 4; i++)
    {
        uint32_t record;

        if (sKey[i] != key)
            continue;
        record = sRecord[i];
        __sync_synchronize();
        if (sKey[i] == key)
            return record != 0 ? &gDioramaTerrainV2[record - 1] : NULL;
    }
    layout = FindLayout(cell->sourceLayoutId);
    if (layout != NULL && layout->terrainRecordOffset <= gDioramaTerrainV2Count
     && layout->terrainRecordCount <= gDioramaTerrainV2Count - layout->terrainRecordOffset)
    {
        /* the sea: the ground and top most of the map's own cells of that metatile have (its open sea, not the water
         * lifted round a rock or a shoal's) */
        struct { int16_t ground, top; uint32_t count; const struct DioramaGeneratedTerrainV2 *record; } kinds[8];
        uint32_t kindCount = 0, k;

        for (i = 0; i < layout->terrainRecordCount && (found == NULL || sea); i++)
        {
            const struct DioramaGeneratedTerrainV2 *record =
                &gDioramaTerrainV2[layout->terrainRecordOffset + i];

            if (record->expectedMetatile != cell->metatileId || record->mapGroup != UINT8_MAX
             || record->measuredBandCount != 0)
                continue;
            if (!sea)
            {
                if (record->shape == DIORAMA_SHAPE_CLIFF && record->groundQ16 == 0)
                    found = record;
                continue;
            }
            if (record->shape != DIORAMA_SHAPE_WATER && record->terrainClass != DIORAMA_TERRAIN_CLASS_WATER)
                continue;
            for (k = 0; k < kindCount; k++)
                if (kinds[k].ground == record->groundQ16 && kinds[k].top == record->topQ16)
                    break;
            if (k == kindCount && kindCount < sizeof(kinds) / sizeof(kinds[0]))
            {
                kinds[kindCount].ground = record->groundQ16;
                kinds[kindCount].top = record->topQ16;
                kinds[kindCount].count = 0;
                kinds[kindCount].record = record;
                kindCount++;
            }
            if (k < kindCount)
                kinds[k].count++;
        }
        if (kindCount > 0)
        {
            uint32_t best = 0;

            for (k = 1; k < kindCount; k++)
                if (kinds[k].count > kinds[best].count)
                    best = k;
            found = kinds[best].record;
        }
    }
    i = sNext++ % 4;
    sKey[i] = 0;
    __sync_synchronize();
    sRecord[i] = found != NULL ? (uint32_t)(found - gDioramaTerrainV2) + 1 : 0;
    __sync_synchronize();
    sKey[i] = key;
    return found;
}

static void ApplyBorderRecord(const struct DioramaGeneratedTerrainV2 *record,
                              struct DioramaResolvedCell *resolved)
{
    resolved->shape = record->shape;
    resolved->archetype = record->archetype;
    resolved->terrainClass = record->terrainClass;
    resolved->classifierClass = record->classId;
    resolved->classifierSource = record->sourceId;
    resolved->semanticPool = record->pool;
    resolved->artMode = record->artMode;
    resolved->authored = record->authored;
    resolved->source = record->sourceKind;
    resolved->classifierConfidence = record->confidence;
    resolved->evidenceFlags = record->evidenceFlags;
    resolved->groundMode = record->propGroundMode;
    resolved->planeAxis = record->axis;
    resolved->groundHeight = record->groundQ16 / 16.0f;
    resolved->topHeight = record->topQ16 / 16.0f;
    resolved->featureHeight = resolved->topHeight - resolved->groundHeight;
    resolved->cliffEdgeMask = record->cliffEdgeMask;
    resolved->cliffBaseMask = record->cliffBaseMask;
    resolved->cliffTransitionMask = record->cliffTransitionMask;
    resolved->cliffCornerMask = record->cliffCornerMask;
    resolved->terraceProfile = record->terraceProfile;
    resolved->measuredFlags = record->measuredFlags;
}

bool DioramaRules_ResolveCell(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaCellSnapshot *cell,
                              struct DioramaResolvedCell *resolved)
{
    const struct DioramaGeneratedTerrainV2 *border;

    if (snapshot == NULL || cell == NULL || resolved == NULL)
        return false;
    InitializeResolved(resolved);
    ApplyStaticTerrain(snapshot, cell, resolved);
    border = BorderRecord(snapshot, cell);
    if (border != NULL)
        ApplyBorderRecord(border, resolved);
    if (cell->collision != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_COLLISION;
    if (cell->elevation != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_ELEVATION;
    if (cell->layerType != 0)
        resolved->evidenceFlags |= DIORAMA_EVIDENCE_LAYER;
    FinalizeSurfaces(snapshot, cell, resolved);
    return true;
}

void DioramaRules_ResolveGrid(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolvedCells)
{
    uint16_t count;
    uint16_t i;

    if (snapshot == NULL || resolvedCells == NULL)
        return;
    count = snapshot->visibleCellCount;
    if (count > DIORAMA_MAX_VISIBLE_CELLS)
        count = DIORAMA_MAX_VISIBLE_CELLS;
    for (i = 0; i < count; i++)
    {
        const struct DioramaCellSnapshot *cell = &snapshot->cells[i];
        const struct DioramaGeneratedTerrainV2 *border;

        InitializeResolved(&resolvedCells[i]);
        ApplyStaticTerrain(snapshot, cell, &resolvedCells[i]);
        border = BorderRecord(snapshot, cell);
        if (border != NULL)
            ApplyBorderRecord(border, &resolvedCells[i]);
        if (cell->collision != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_COLLISION;
        if (cell->elevation != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_ELEVATION;
        if (cell->layerType != 0)
            resolvedCells[i].evidenceFlags |= DIORAMA_EVIDENCE_LAYER;
    }
    ClaimBorderTrees(snapshot, resolvedCells, count);
    for (i = 0; i < count; i++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object =
            DioramaRules_GetPixelObject(resolvedCells[i].pixelObjectId);
        uint16_t previous;
        uint16_t other;

        if (object == NULL)
            continue;
        for (previous = 0; previous < i; previous++)
            if (resolvedCells[previous].pixelObjectId == object->id)
                break;
        if (previous < i)
            continue;
        if (PixelObjectMatchesSnapshot(snapshot, &snapshot->cells[i], object)
         && PixelObjectSupportMatchesSnapshot(snapshot, resolvedCells, count,
                                               &snapshot->cells[i], object))
            continue;
        for (other = 0; other < count; other++)
            if (resolvedCells[other].pixelObjectId == object->id)
            {
                resolvedCells[other].pixelObjectId = 0;
                resolvedCells[other].groundMode = 0;
                resolvedCells[other].baseMetatileId = DIORAMA_MATERIAL_METATILE_SELF;
            }
    }
    for (i = 0; i < count; i++)
        resolvedCells[i].effectiveElevation = snapshot->cells[i].elevation > 0
                                          && snapshot->cells[i].elevation < 15
                                          ? snapshot->cells[i].elevation : 0;
    {
        bool visited[DIORAMA_MAX_VISIBLE_CELLS] = {false};
        uint16_t queue[DIORAMA_MAX_VISIBLE_CELLS];
        static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

        for (i = 0; i < count; i++)
        {
            uint16_t componentCount = 0;
            uint16_t readIndex = 0;
            uint8_t candidate = 0;
            bool conflict = false;

            if (visited[i] || resolvedCells[i].effectiveElevation != 0)
                continue;
            visited[i] = true;
            queue[componentCount++] = i;
            while (readIndex < componentCount)
            {
                uint16_t current = queue[readIndex++];
                uint8_t direction;

                for (direction = 0; direction < 4; direction++)
                {
                    int neighbor = FindCell(snapshot, &snapshot->cells[current],
                                            offsets[direction][0], offsets[direction][1]);
                    uint8_t elevation;

                    if (neighbor < 0)
                        continue;
                    elevation = resolvedCells[neighbor].effectiveElevation;
                    if (elevation != 0)
                    {
                        if (candidate != 0 && candidate != elevation)
                            conflict = true;
                        else
                            candidate = elevation;
                    }
                    else if (!visited[neighbor])
                    {
                        visited[neighbor] = true;
                        queue[componentCount++] = neighbor;
                    }
                }
            }
            if (!conflict && candidate != 0)
                for (readIndex = 0; readIndex < componentCount; readIndex++)
                    resolvedCells[queue[readIndex]].effectiveElevation = candidate;
        }
    }
    for (i = 0; i < count; i++)
        FinalizeSurfaces(snapshot, &snapshot->cells[i], &resolvedCells[i]);
}

float DioramaRules_DefaultGroundHeight(uint8_t behavior)
{
    (void)behavior;
    return 0.0f;
}

float DioramaRules_ProfileHeight(uint8_t archetype, uint8_t behavior,
                                 float groundHeight, float featureHeight,
                                 uint8_t pixelX, uint8_t pixelY)
{
    uint8_t direction;
    uint8_t coordinate;
    uint8_t step;
    bool descending;

    if (archetype < DIORAMA_ARCHETYPE_STAIRS_N
     || archetype > DIORAMA_ARCHETYPE_STAIRS_DOWN_W)
        return groundHeight;
    descending = archetype >= DIORAMA_ARCHETYPE_STAIRS_DOWN_N;
    direction = descending ? archetype - DIORAMA_ARCHETYPE_STAIRS_DOWN_N
                           : archetype - DIORAMA_ARCHETYPE_STAIRS_N;
    coordinate = direction <= 1 ? pixelY : pixelX;
    if (direction == 0 || direction == 2)
        coordinate = DIORAMA_BUILDING_PIXELS_PER_CELL - 1 - coordinate;
    if (behavior == MB_MUDDY_SLOPE || behavior == MB_BUMPY_SLOPE)
        step = coordinate + 1;
    else
        step = (coordinate / 4 + 1) * 4;
    if (featureHeight == 0.0f)
        featureHeight = 1.0f;
    return groundHeight + (descending ? -1.0f : 1.0f)
         * featureHeight * step / DIORAMA_BUILDING_PIXELS_PER_CELL;
}

bool DioramaRules_CellOverWater(const struct DioramaCellSnapshot *cell, uint8_t mapType)
{
    int water = 0;
    int i;

    if (cell == NULL || mapType == MAP_TYPE_NONE || mapType == MAP_TYPE_INDOOR
     || mapType == MAP_TYPE_UNDERGROUND || mapType == MAP_TYPE_SECRET_BASE
     || mapType == MAP_TYPE_UNDERWATER)
        return false;
    for (i = 0; i < DIORAMA_METATILE_ENTRY_COUNT / 2; i++)
    {
        uint16_t tile = cell->tileEntries[i] & 0x3FF;

        if (tile >= 432 && tile < 462)
            water++;
    }
    return water >= 2;
}

int16_t DioramaRules_StaticGroundQ16(uint16_t layoutId, uint32_t cellOffset)
{
    const struct DioramaGeneratedLayoutV2 *layout = FindLayout(layoutId);
    uint32_t low = 0;
    uint32_t high;

    if (layout == NULL || layout->terrainRecordOffset > gDioramaTerrainV2Count
     || layout->terrainRecordCount > gDioramaTerrainV2Count - layout->terrainRecordOffset)
        return 0;
    high = layout->terrainRecordCount;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;

        if (gDioramaTerrainV2[layout->terrainRecordOffset + middle].cellOffset < cellOffset)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < layout->terrainRecordCount
     && gDioramaTerrainV2[layout->terrainRecordOffset + low].cellOffset == cellOffset)
        return gDioramaTerrainV2[layout->terrainRecordOffset + low].groundQ16;
    return 0;
}

bool DioramaRules_LayoutUsesSecondary(uint16_t layoutId, const char *symbol)
{
    size_t i;

    if (symbol == NULL)
        return false;
    for (i = 0; i < gDioramaLayoutV2Count; i++)
    {
        uint8_t tileset;
        size_t t;

        if (gDioramaLayoutsV2[i].id != layoutId)
            continue;
        tileset = gDioramaLayoutsV2[i].secondaryTilesetId;
        for (t = 0; t < gDioramaTilesetV2Count; t++)
            if (gDioramaTilesetsV2[t].id == tileset)
                return gDioramaTilesetsV2[t].symbol != NULL
                    && strcmp(gDioramaTilesetsV2[t].symbol, symbol) == 0;
        return false;
    }
    return false;
}

uint8_t DioramaRules_CellLava(const struct DioramaCellSnapshot *cell, uint16_t layoutId)
{
    /* per layout id: 0 not looked up, 1 no lava tileset, 2 gTileset_Lavaridge */
    static uint8_t sLavaLayout[2048];
    int i;

    /* the secondary tileset's metatiles (NUM_METATILES_IN_PRIMARY up) */
    if (cell == NULL || cell->metatileId < 512 || layoutId == 0
     || layoutId >= sizeof(sLavaLayout))
        return 0;
    if (sLavaLayout[layoutId] == 0)
        sLavaLayout[layoutId] = DioramaRules_LayoutUsesSecondary(layoutId, "gTileset_Lavaridge")
                              ? 2 : 1;
    if (sLavaLayout[layoutId] != 2)
        return 0;
    for (i = 0; i < DIORAMA_METATILE_ENTRY_COUNT / 2; i++)
    {
        uint16_t tile = cell->tileEntries[i] & 0x3FF;

        if (tile != 608 && (tile < 672 || tile > 675))
            return 0;
    }
    for (i = DIORAMA_METATILE_ENTRY_COUNT / 2; i < DIORAMA_METATILE_ENTRY_COUNT; i++)
        if ((cell->tileEntries[i] & 0x3FF) != 0)
            return 1;
    return 2;
}

float DioramaRules_ObjectGroundHeight(const struct DioramaResolvedCell *resolved,
                                       uint8_t cellElevation, uint8_t objectElevation,
                                       uint8_t behavior,
                                       uint8_t pixelX, uint8_t pixelY)
{
    uint8_t i;

    if (resolved == NULL)
        return 0.0f;
    if (resolved->surfaceCount > 1 && objectElevation > 0 && objectElevation < 15)
        for (i = 0; i < resolved->surfaceCount; i++)
            if (resolved->surfaces[i].gameplayElevation == objectElevation)
                return resolved->surfaces[i].topHeight;
    (void)cellElevation;
    return DioramaRules_ProfileHeight(resolved->archetype, behavior,
                                       resolved->groundHeight, resolved->featureHeight,
                                       pixelX, pixelY);
}

#endif
