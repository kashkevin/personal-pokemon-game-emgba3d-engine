// Native PC build: Update 29 self-tests (wild encounters, Legendary/Mythical rolls, shiny rolls, coins) and the species
// data fingerprint. Project-owned. Everything here runs through the game's own code before the game starts and exits.
//
//   --dump-species-data FILE   one line per species: "id name fnv64", a hash of every field the update must not change
//                              (base stats, types, catch rate, experience, EVs, abilities, gender ratio, egg data,
//                              friendship, growth rate, flags, level-up / teachable / egg move lists, evolutions with
//                              their conditions). tools/pc_port/species_data_check.py compares it with the committed
//                              baseline: any difference fails the check.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "pokemon.h"
#include "constants/species.h"
#include "constants/moves.h"

static u64 Fnv(u64 hash, u32 value, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++)
    {
        hash ^= (value >> (8 * i)) & 0xFF;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

static u64 SpeciesHash(enum Species species)
{
    const struct SpeciesInfo *info = &gSpeciesInfo[species];
    u64 h = 0xCBF29CE484222325ULL;
    const struct LevelUpMove *levelUp = GetSpeciesLevelUpLearnset(species);
    const u16 *list;
    const struct Evolution *evolutions = GetSpeciesEvolutions(species);

    h = Fnv(h, info->baseHP, 1); h = Fnv(h, info->baseAttack, 1); h = Fnv(h, info->baseDefense, 1);
    h = Fnv(h, info->baseSpeed, 1); h = Fnv(h, info->baseSpAttack, 1); h = Fnv(h, info->baseSpDefense, 1);
    h = Fnv(h, info->types[0], 1); h = Fnv(h, info->types[1], 1);
    h = Fnv(h, info->catchRate, 1); h = Fnv(h, info->expYield, 2);
    h = Fnv(h, info->evYield_HP | info->evYield_Attack << 2 | info->evYield_Defense << 4 | info->evYield_Speed << 6
              | info->evYield_SpAttack << 8 | info->evYield_SpDefense << 10, 2);
    h = Fnv(h, info->itemCommon, 2); h = Fnv(h, info->itemRare, 2);
    h = Fnv(h, info->genderRatio, 1); h = Fnv(h, info->eggCycles, 1); h = Fnv(h, info->friendship, 1);
    h = Fnv(h, info->growthRate, 1); h = Fnv(h, info->eggGroups[0], 1); h = Fnv(h, info->eggGroups[1], 1);
    for (u32 i = 0; i < NUM_ABILITY_SLOTS; i++)
        h = Fnv(h, info->abilities[i], 2);
    h = Fnv(h, info->safariZoneFleeRate, 1);
    h = Fnv(h, info->natDexNum, 2);
    h = Fnv(h, info->isRestrictedLegendary | info->isSubLegendary << 1 | info->isMythical << 2 | info->isUltraBeast << 3
              | info->isParadox << 4 | info->isMegaEvolution << 6 | info->isPrimalReversion << 7 | info->cannotBeTraded << 15
              | info->perfectIVCount << 16, 4);
    if (levelUp != NULL)
        for (u32 i = 0; i < 400 && levelUp[i].move != LEVEL_UP_MOVE_END; i++)
            h = Fnv(Fnv(h, levelUp[i].move, 2), levelUp[i].level, 2);
    h = Fnv(h, 0xFFFF, 2);
    list = GetSpeciesTeachableLearnset(species);
    if (list != NULL)
        for (u32 i = 0; i < 1000 && list[i] != MOVE_UNAVAILABLE; i++)
            h = Fnv(h, list[i], 2);
    h = Fnv(h, 0xFFFF, 2);
    list = GetSpeciesEggMoves(species);
    if (list != NULL)
        for (u32 i = 0; i < 100 && list[i] != MOVE_UNAVAILABLE; i++)
            h = Fnv(h, list[i], 2);
    h = Fnv(h, 0xFFFF, 2);
    if (evolutions != NULL)
        for (u32 i = 0; i < 20 && evolutions[i].method != EVOLUTIONS_END; i++)
        {
            h = Fnv(h, evolutions[i].method, 2); h = Fnv(h, evolutions[i].param, 2); h = Fnv(h, evolutions[i].targetSpecies, 2);
            if (evolutions[i].params != NULL)
                for (u32 j = 0; j < 12 && evolutions[i].params[j].condition != CONDITIONS_END; j++)
                {
                    h = Fnv(h, evolutions[i].params[j].condition, 2); h = Fnv(h, evolutions[i].params[j].arg1, 2);
                    h = Fnv(h, evolutions[i].params[j].arg2, 2); h = Fnv(h, evolutions[i].params[j].arg3, 2);
                }
        }
    return h;
}

int NativeSelfTest_DumpSpeciesData(const char *outPath)
{
    FILE *out = fopen(outPath, "w");

    if (out == NULL)
    {
        fprintf(stderr, "dump-species-data: cannot open %s\n", outPath);
        return 2;
    }
    for (u32 species = 1; species < NUM_SPECIES; species++)
        if (IsSpeciesEnabled(species))
            fprintf(out, "%u %016llx\n", species, (unsigned long long)SpeciesHash(species)), fflush(out);
    fclose(out);
    printf("dump-species-data: written\n");
    return 0;
}
