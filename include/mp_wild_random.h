#ifndef GUARD_MP_WILD_RANDOM_H
#define GUARD_MP_WILD_RANDOM_H

#include "wild_encounter.h"

// Randomized, rarity-weighted Gen 1-6 wild encounters (project spec,
// "Wild encounters and shinies"). The map's own encounter table still chooses
// the encounter level (progression); this module only replaces the species.
// Legendary/Mythical/Ultra Beast/Paradox families are never generated.

enum MPWildTier
{
    MP_WILD_TIER_COMMON,
    MP_WILD_TIER_UNCOMMON,
    MP_WILD_TIER_RARE,
    MP_WILD_TIER_VERY_RARE,
    MP_WILD_TIER_COUNT,
};

#define MP_WILD_HABITAT_LAND (1 << 0)
#define MP_WILD_HABITAT_SURF (1 << 1)
#define MP_WILD_HABITAT_FISH (1 << 2)
#define MP_WILD_HABITAT_ROCK (1 << 3)

#define MP_WILD_TIME_ANY        0
#define MP_WILD_TIME_NOCTURNAL  1
#define MP_WILD_TIME_DIURNAL    2

struct MPWildMember
{
    u16 species;
    u8 minLevel;   // first map level at which this stage may appear
    u8 depth;      // 0 = first stage
};

struct MPWildFamily
{
    u16 firstMember;
    u8 memberCount;
    u8 tier;
    u8 habitat;
    u8 time;
    u8 type1;
    u8 type2;
};

// Returns the randomized species for a wild encounter of the given area and
// level. Returns vanillaSpecies unchanged where randomization does not apply
// (Battle Frontier facilities, empty pools).
u16 MPWild_RandomizeSpecies(u16 vanillaSpecies, u8 level, enum WildPokemonArea area);

// Rarity tier of a species' family (for UI such as a "rare spawn" cue), or
// MP_WILD_TIER_COUNT if the species is not part of the randomized pool.
u8 MPWild_GetSpeciesTier(u16 species);

#if defined(PLATFORM_SDL2)
// PC builds: the next rolls avoid the families of these species (the visible spawner's no-repeat rule,
// wild_encounter_ow.c); count 0 ends it.
void MPWild_SetAvoidSpecies(const u16 *species, u32 count);
#endif

#endif // GUARD_MP_WILD_RANDOM_H
