/*
 * Random Legendary and Mythical wild encounters: see include/platform/mp_legend.h. Project-owned.
 */
#include "global.h"
#include "platform/mp_legend.h"
#include "pokemon.h"
#include "random.h"
#include "rtc.h"
#include "safari_zone.h"
#include "wild_encounter.h"
#include "field_weather.h"
#include "constants/layouts.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"
#include "constants/species.h"
#include "constants/weather.h"

#include "mp_legend_data.h"

static u32 TagsFor(const struct MPLegendMapsecTags *table, u32 count, u32 mapsec)
{
    for (u32 i = 0; i < count; i++)
        if (table[i].mapsec == mapsec)
            return table[i].tags;
    return 0;
}

u32 MPLegend_AreaHabitats(enum WildPokemonArea area, u32 mapsec, u32 mapType, u32 weather)
{
    bool32 water = (area == WILD_AREA_WATER || area == WILD_AREA_FISHING);
    bool32 underground = (mapType == MAP_TYPE_UNDERGROUND);
    u32 tags = water ? MP_LEGEND_WATER_DEFAULT : underground ? MP_LEGEND_CAVE_DEFAULT : MP_LEGEND_LAND_DEFAULT;

    if (water)
    {
        tags |= TagsFor(sMPLegendWaterTags, ARRAY_COUNT(sMPLegendWaterTags), mapsec);
        if (underground)
            tags |= MP_LEGEND_CAVE_DEFAULT;
    }
    else
        tags |= TagsFor(sMPLegendLandTags, ARRAY_COUNT(sMPLegendLandTags), mapsec);
    if (!underground)
        for (u32 i = 0; i < ARRAY_COUNT(sMPLegendWeatherTags); i++)
            if (sMPLegendWeatherTags[i].weather == weather)
                tags |= sMPLegendWeatherTags[i].tag;
    return tags;
}

u32 MPLegend_OneIn(bool32 safari)
{
    return safari ? MP_LEGEND_ONE_IN_SAFARI : MP_LEGEND_ONE_IN_REGULAR;
}

static int sForce;

void MPLegend_TestForce(int mode)
{
    sForce = mode;
}

bool32 MPLegend_RollHits(u32 random, bool32 safari)
{
    // random is uniform in [0, 2^32): the 1/N chance is random < 2^32 / N, as integer arithmetic without bias from a modulo
    return ((u64)random * MPLegend_OneIn(safari)) >> 32 == 0;
}

static bool32 Eligible(const struct MPLegendSpecies *entry, u32 habitats, bool32 night, bool32 safari)
{
    if (safari)
        return TRUE;   // inside the Safari Zone every species of the one shared pool is eligible, whatever the habitat or hour
    if ((entry->flags & MP_LEGEND_NIGHT_ONLY) && !night)
        return FALSE;
    return (entry->habitats & habitats) != 0;
}

u32 MPLegend_PoolSize(u32 habitats, bool32 night, bool32 safari)
{
    u32 count = 0;

    for (u32 i = 0; i < MP_LEGEND_SPECIES_COUNT; i++)
        if (Eligible(&sMPLegendSpecies[i], habitats, night, safari))
            count++;
    return count;
}

u16 MPLegend_PoolSpecies(u32 habitats, bool32 night, bool32 safari, u32 index)
{
    for (u32 i = 0; i < MP_LEGEND_SPECIES_COUNT; i++)
        if (Eligible(&sMPLegendSpecies[i], habitats, night, safari))
        {
            if (index == 0)
                return sMPLegendSpecies[i].species;
            index--;
        }
    return SPECIES_NONE;
}

u16 MPLegend_Pick(u32 habitats, bool32 night, bool32 safari, u32 random)
{
    u32 size = MPLegend_PoolSize(habitats, night, safari);

    if (size == 0)
        return SPECIES_NONE;
    return MPLegend_PoolSpecies(habitats, night, safari, (u32)(((u64)random * size) >> 32));
}

u32 MPLegend_SpeciesCount(void)
{
    return MP_LEGEND_SPECIES_COUNT;
}

const struct MPLegendSpecies *MPLegend_SpeciesAt(u32 index)
{
    return index < MP_LEGEND_SPECIES_COUNT ? &sMPLegendSpecies[index] : NULL;
}

// ---- Game glue -------------------------------------------------------------------------------------------------------

static struct
{
    bool8 valid;
    u8 area;
    u16 species;
    u32 personality;
} sPending;

// The roll's Legendary/Mythical in the wild battle now (MPLegend_TakeBattleLegend)
static struct
{
    bool8 valid;
    bool8 shiny;
    bool8 female;
    u16 species;
} sBattleLegend;

bool32 MPLegend_IsLegendSpecies(u16 species)
{
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return FALSE;
    return gSpeciesInfo[species].isRestrictedLegendary || gSpeciesInfo[species].isSubLegendary
        || gSpeciesInfo[species].isMythical;
}

bool32 MPLegend_TakeBattleLegend(u16 *species, bool8 *shiny, bool8 *female)
{
    if (!sBattleLegend.valid)
        return FALSE;
    sBattleLegend.valid = FALSE;
    *species = sBattleLegend.species;
    *shiny = sBattleLegend.shiny;
    *female = sBattleLegend.female;
    return TRUE;
}

static bool32 IsFrontierMap(void)
{
    return gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS
        || gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR;
}

void MPLegend_ForgetPending(void)
{
    sPending.valid = FALSE;
}

void MPLegend_NoteWildMon(enum WildPokemonArea area)
{
    const struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];

    sPending.valid = FALSE;
    sBattleLegend.valid = FALSE;
    if (IsFrontierMap())
        return;     // the Battle Pike and Pyramid keep their own Pokemon
    sPending.species = GetMonData(mon, MON_DATA_SPECIES);
    sPending.personality = GetMonData(mon, MON_DATA_PERSONALITY);
    sPending.area = area;
    sPending.valid = sPending.species != SPECIES_NONE;
}

void MPLegend_ApplyToPendingWildBattle(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][0];
    bool32 safari = GetSafariZoneFlag();
    bool32 night;
    u32 habitats, level, shiny;
    u16 species;
    enum TimeOfDay timeOfDay;

    if (!sPending.valid)
        return;
    sPending.valid = FALSE;
    // Only the Pokemon the encounter code just made: a stale note (an overworld spawn that never became a battle) or
    // a different Pokemon in the opponent slot (DexNav, a scripted battle) never rolls.
    if (GetMonData(mon, MON_DATA_SPECIES) != sPending.species || GetMonData(mon, MON_DATA_PERSONALITY) != sPending.personality)
        return;
    if (sForce > 0 ? FALSE : (sForce < 0 || !MPLegend_RollHits(Random32(), safari)))
        return;
    timeOfDay = GetTimeOfDay();
    night = (timeOfDay == TIME_EVENING || timeOfDay == TIME_NIGHT);
    habitats = safari ? MP_LEGEND_ALL_TAGS
                      : MPLegend_AreaHabitats(sPending.area, gMapHeader.regionMapSectionId, gMapHeader.mapType,
                                              GetCurrentWeather());
    species = MPLegend_Pick(habitats, night, safari, Random32());
    if (species == SPECIES_NONE)
        return;     // an area no Legendary or Mythical fits: the encounter stays ordinary
    level = GetMonData(mon, MON_DATA_LEVEL);
    shiny = GetMonData(mon, MON_DATA_IS_SHINY);
    CreateWildMon(species, level);          // the level is the one the map's table chose; a level-N Pokemon of the species
    SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY, &shiny);  // the shiny roll of the encounter is kept
    sBattleLegend.valid = TRUE;
    sBattleLegend.species = species;
    sBattleLegend.shiny = shiny ? TRUE : FALSE;
    sBattleLegend.female = GetMonGender(&gParties[B_TRAINER_OPPONENT_A][0]) == MON_FEMALE;
}
