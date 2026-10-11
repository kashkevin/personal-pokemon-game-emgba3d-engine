/*
 * Environmental object inventory (graphics overhaul, phase 2, project-owned).
 *
 * Counts, over every Emerald (Hoenn) map the rules cover, the cells of each
 * object class the rule compiler found (tools/diorama_rules: flowers, tall
 * grass, signposts, posts, rails, rocks, shrubs, stumps, trees...), how the
 * renderer draws them today (art mode, shape, pixel object or not), and the
 * metatiles behind them per tileset, so the object library knows what it has
 * to cover. Built against the generated rule tables (see README.md); prints a
 * Markdown report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diorama/rules.generated.h"
#include "diorama/rules.h"

#define MAX_CLASSES 64
#define MAX_TILESETS 128
#define MAX_METATILES 1024

struct ClassCount
{
    unsigned cells;
    unsigned maps;
    unsigned artModes[8];
    unsigned shapes[16];
    unsigned inPixelObjects;
    /* cells per (tileset of the metatile, metatile) */
    unsigned *metatiles; /* MAX_TILESETS * MAX_METATILES */
};

static struct ClassCount sClasses[MAX_CLASSES];

static const struct DioramaGeneratedMapV2 *FindMapForLayoutRecord(uint16_t layoutId, uint8_t group,
                                                                  uint8_t number)
{
    size_t i;

    for (i = 0; i < gDioramaMapV2Count; i++)
        if (gDioramaMapsV2[i].layoutId == layoutId
         && (group == UINT8_MAX || (gDioramaMapsV2[i].group == group && gDioramaMapsV2[i].number == number)))
            return &gDioramaMapsV2[i];
    return NULL;
}

static const char *TilesetName(uint8_t id)
{
    if (id == 0 || id > gDioramaTilesetV2Count)
        return "?";
    return gDioramaTilesetsV2[id - 1].symbol;
}

int main(void)
{
    size_t layoutIndex;
    size_t objectIndex;
    unsigned classId;
    static unsigned char seenMap[MAX_CLASSES][1024];

    for (classId = 0; classId < MAX_CLASSES; classId++)
        sClasses[classId].metatiles = calloc(MAX_TILESETS * MAX_METATILES, sizeof(unsigned));
    for (layoutIndex = 0; layoutIndex < gDioramaLayoutV2Count; layoutIndex++)
    {
        const struct DioramaGeneratedLayoutV2 *layout = &gDioramaLayoutsV2[layoutIndex];
        uint32_t r;

        for (r = 0; r < layout->terrainRecordCount; r++)
        {
            const struct DioramaGeneratedTerrainV2 *record = &gDioramaTerrainV2[layout->terrainRecordOffset + r];
            const struct DioramaGeneratedMapV2 *map = FindMapForLayoutRecord(layout->id, record->mapGroup,
                                                                             record->mapNumber);
            struct ClassCount *count;
            uint8_t tileset;

            if (record->classId >= MAX_CLASSES || map == NULL || !map->supported)
                continue;
            count = &sClasses[record->classId];
            count->cells++;
            if (record->artMode < 8)
                count->artModes[record->artMode]++;
            if (record->shape < 16)
                count->shapes[record->shape]++;
            tileset = record->expectedMetatile < 512 ? layout->primaryTilesetId : layout->secondaryTilesetId;
            if (tileset < MAX_TILESETS && record->expectedMetatile < MAX_METATILES)
                count->metatiles[tileset * MAX_METATILES + record->expectedMetatile]++;
            if (!seenMap[record->classId][(map - gDioramaMapsV2) % 1024])
            {
                seenMap[record->classId][(map - gDioramaMapsV2) % 1024] = 1;
                count->maps++;
            }
        }
    }
    for (objectIndex = 0; objectIndex < gDioramaPixelObjectV2Count; objectIndex++)
    {
        const struct DioramaGeneratedPixelObjectV2 *object = &gDioramaPixelObjectsV2[objectIndex];

        if (object->classId < MAX_CLASSES)
            sClasses[object->classId].inPixelObjects++;
    }

    printf("# Environmental object inventory (generated)\n\n");
    printf("Rules SHA-256 `%s`; %zu layouts, %zu maps, %zu terrain records, %zu pixel objects.\n\n",
           gDioramaRulesSha256, gDioramaLayoutV2Count, gDioramaMapV2Count, gDioramaTerrainV2Count,
           gDioramaPixelObjectV2Count);
    printf("| Class | Cells | Maps | Pixel objects | Art modes (flat/top/upright/grass/flower/stair/cutout) | Top metatiles (tileset:id x cells) |\n");
    printf("| --- | ---: | ---: | ---: | --- | --- |\n");
    for (classId = 1; classId < MAX_CLASSES; classId++)
    {
        const struct ClassCount *count = &sClasses[classId];
        const char *name = DioramaRules_ClassName((uint16_t)classId);
        unsigned best[6] = {0};
        unsigned bestKey[6] = {0};
        unsigned key;
        int k;

        if (name == NULL || count->cells == 0)
            continue;
        for (key = 0; key < MAX_TILESETS * MAX_METATILES; key++)
        {
            unsigned value = count->metatiles[key];

            for (k = 0; k < 6; k++)
                if (value > best[k])
                {
                    memmove(&best[k + 1], &best[k], (5 - k) * sizeof(best[0]));
                    memmove(&bestKey[k + 1], &bestKey[k], (5 - k) * sizeof(bestKey[0]));
                    best[k] = value;
                    bestKey[k] = key;
                    break;
                }
        }
        printf("| %s | %u | %u | %u | %u/%u/%u/%u/%u/%u/%u | ", name, count->cells, count->maps,
               count->inPixelObjects, count->artModes[0], count->artModes[1], count->artModes[2],
               count->artModes[3], count->artModes[4], count->artModes[5], count->artModes[6]);
        for (k = 0; k < 6 && best[k] != 0; k++)
        {
            const char *tileset = TilesetName((uint8_t)(bestKey[k] / MAX_METATILES));

            if (strncmp(tileset, "gTileset_", 9) == 0)
                tileset += 9;
            printf("%s%s:%03X x%u", k ? ", " : "", tileset, bestKey[k] % MAX_METATILES, best[k]);
        }
        printf(" |\n");
    }
    {
        /* Structures: the rules' multi-cell objects (trees in groups, signs,
         * rocks, buildings...), by class. */
        static unsigned structures[MAX_CLASSES];
        static unsigned structureCells[MAX_CLASSES];
        static unsigned sizes[MAX_CLASSES][4][4];
        size_t i;

        for (i = 0; i < gDioramaStructureV2Count; i++)
        {
            const struct DioramaGeneratedStructureV2 *structure = &gDioramaStructuresV2[i];

            if (structure->classId >= MAX_CLASSES)
                continue;
            structures[structure->classId]++;
            structureCells[structure->classId] += structure->cellCount;
            sizes[structure->classId][structure->width < 4 ? structure->width : 3]
                 [structure->height < 4 ? structure->height : 3]++;
        }
        printf("\n## Structures (multi-cell objects)\n\n");
        printf("| Class | Structures | Cells | 1x1 | 1x2 | 2x1 | 2x2 | larger |\n");
        printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
        for (classId = 1; classId < MAX_CLASSES; classId++)
        {
            const char *name = DioramaRules_ClassName((uint16_t)classId);
            unsigned larger = structures[classId] - sizes[classId][1][1] - sizes[classId][1][2]
                            - sizes[classId][2][1] - sizes[classId][2][2];

            if (name == NULL || structures[classId] == 0)
                continue;
            printf("| %s | %u | %u | %u | %u | %u | %u | %u |\n", name, structures[classId],
                   structureCells[classId], sizes[classId][1][1], sizes[classId][1][2],
                   sizes[classId][2][1], sizes[classId][2][2], larger);
        }
    }
    return 0;
}
