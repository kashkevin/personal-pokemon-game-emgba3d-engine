#include "global.h"
#include "test/battle.h"
#include "palette.h"

// Every Mega Evolution in the game, one PARAMETRIZE each, found at run time in the form change tables.
// A Mega Stone's holder is a species whose form change table lists FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM with that stone,
// leaving out the Mega forms and the other battle-only forms that share their base's table (Gigantamax Venusaur shares
// Venusaur's). So Floette's holder is Floette-Eternal, Zygarde's is Zygarde-Complete, and each Meowstic gender has its own.
// Mega Rayquaza (Dragon Ascent, no stone) is tested on its own below.
//
// The test Pokemon are level 100 with 0 IVs and EVs and a neutral nature (OpenPokemon) and set no fixed stats, so after
// Mega Evolving their stats must be exactly what the Mega form's base stats give them (CalculateMonStats).

static bool32 IsMegaEvolutionBase(enum Species species)
{
    const struct SpeciesInfo *info = &gSpeciesInfo[species];
    return IsSpeciesEnabled(species)
        && !info->isMegaEvolution && !info->isPrimalReversion && !info->isUltraBurst
        && !info->isGigantamax && !info->isTeraForm && !info->isTotem;
}

static bool32 IsMegaEvolutionMethod(enum FormChanges method)
{
    return method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM || method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE;
}

// A stat other than HP of this Pokemon as `species`, by the formula CalculateMonStats uses (B_FRIENDSHIP_BOOST is off).
static u32 ExpectedStat(struct Pokemon *mon, enum Species species, enum Stat stat)
{
    u32 level = GetMonData(mon, MON_DATA_LEVEL);
    u32 iv = GetMonData(mon, MON_DATA_HP_IV + stat);
    u32 ev = GetMonData(mon, MON_DATA_HP_EV + stat);
    u32 value = (((2 * GetSpeciesBaseStat(species, stat) + iv + ev / 4) * level) / 100) + 5;
    return ModifyStatByNature(GetMonData(mon, MON_DATA_HIDDEN_NATURE), value, stat);
}

// Counts a value that differs from what Mega Evolving into _mega should give, and prints it, so that a sweep over every
// Mega names every difference instead of stopping at the first one.
#define CHECK_MEGA(_count, _mega, _what, _actual, _expected)                                                      \
    do                                                                                                             \
    {                                                                                                              \
        u32 actual_ = (_actual), expected_ = (_expected);                                                          \
        if (actual_ != expected_)                                                                                  \
        {                                                                                                          \
            (_count)++;                                                                                            \
            Test_MgbaPrintf("%s:%d: %S (species %d): %s is %d, expected %d", gTestRunnerState.test->filename,        \
                            __LINE__, GetSpeciesName(_mega), _mega, _what, actual_, expected_);                      \
        }                                                                                                          \
    } while (0)

// The battler after Mega Evolving into `mega`: the Mega form's species, typing and Ability, and the stats the Mega form's
// base stats give this Pokemon (`mon` is its party data from GIVEN). Its level and max HP must not change.
static u32 CountMegaFormMismatches(const struct BattlePokemon *battler, struct Pokemon *mon, enum Species mega, enum Type type0, enum Type type1)
{
    u32 count = 0;
    CHECK_MEGA(count, mega, "species", battler->species, mega);
    CHECK_MEGA(count, mega, "type 1", battler->types[0], type0);
    CHECK_MEGA(count, mega, "type 2", battler->types[1], type1);
    CHECK_MEGA(count, mega, "type 3", battler->types[2], TYPE_MYSTERY);
    CHECK_MEGA(count, mega, "Ability", battler->ability, GetSpeciesAbility(mega, 0));
    CHECK_MEGA(count, mega, "Attack", battler->attack, ExpectedStat(mon, mega, STAT_ATK));
    CHECK_MEGA(count, mega, "Defense", battler->defense, ExpectedStat(mon, mega, STAT_DEF));
    CHECK_MEGA(count, mega, "Sp. Atk", battler->spAttack, ExpectedStat(mon, mega, STAT_SPATK));
    CHECK_MEGA(count, mega, "Sp. Def", battler->spDefense, ExpectedStat(mon, mega, STAT_SPDEF));
    CHECK_MEGA(count, mega, "Speed", battler->speed, ExpectedStat(mon, mega, STAT_SPEED));
    CHECK_MEGA(count, mega, "level", battler->level, GetMonData(mon, MON_DATA_LEVEL));
    CHECK_MEGA(count, mega, "max HP", battler->maxHP, GetMonData(mon, MON_DATA_MAX_HP));
    return count;
}

// One PARAMETRIZE per Mega Stone used by a holder whose National Pokedex number is in [_firstDexNum, _lastDexNum].
#define PARAMETRIZE_MEGA_STONES(_firstDexNum, _lastDexNum)                                                         \
    for (enum Species species_ = SPECIES_NONE + 1; species_ < NUM_SPECIES; species_++)                            \
    {                                                                                                             \
        /* GetSpeciesFormChanges asserts on a disabled species: test that first */                               \
        const struct FormChange *formChanges_ = IsMegaEvolutionBase(species_) ? GetSpeciesFormChanges(species_) : NULL; \
        u32 dexNum_ = gSpeciesInfo[species_].natDexNum;                                                           \
        if (formChanges_ == NULL || dexNum_ < (_firstDexNum) || dexNum_ > (_lastDexNum))                          \
            continue;                                                                                             \
        for (u32 j_ = 0; formChanges_[j_].method != FORM_CHANGE_TERMINATOR; j_++)                                 \
        {                                                                                                         \
            if (formChanges_[j_].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM)                                \
                PARAMETRIZE { holder = species_; stone = formChanges_[j_].param1; mega = formChanges_[j_].targetSpecies; } \
        }                                                                                                         \
    }

// The holder, holding its stone, Mega Evolves on the first turn and uses Celebrate.
static void MegaEvolveHolder(struct BattlePokemon *player, enum Species holder, enum Item stone, enum Species mega, u32 *mismatches)
{
    enum Ability megaAbility = GetSpeciesAbility(mega, 0);
    // Protean (Mega Greninja) gives its user the type of the move it uses: after Celebrate it is Normal type.
    bool32 retypedByMove = (megaAbility == ABILITY_PROTEAN || megaAbility == ABILITY_LIBERO);

    GIVEN {
        // Comatose can't be Traced, so Mega Alakazam and Mega Meowstic keep Trace.
        ASSUME(gAbilitiesInfo[GetSpeciesAbility(SPECIES_KOMALA, 0)].cantBeTraced);
        PLAYER(holder) { Item(stone); }
        OPPONENT(SPECIES_KOMALA);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
        if (retypedByMove)
            ABILITY_POPUP(player, megaAbility);
    } THEN {
        enum Type type0 = retypedByMove ? GetMoveType(MOVE_CELEBRATE) : GetSpeciesType(mega, 0);
        enum Type type1 = retypedByMove ? GetMoveType(MOVE_CELEBRATE) : GetSpeciesType(mega, 1);
        struct Pokemon *partyMon = &gParties[B_TRAINER_PLAYER][0];

        *mismatches = CountMegaFormMismatches(player, &PLAYER_PARTY[0], mega, type0, type1);
        // The battle is over: the party Pokemon is the holder again, with the holder's stats, and keeps its stone.
        CHECK_MEGA(*mismatches, mega, "species after the battle", GetMonData(partyMon, MON_DATA_SPECIES), holder);
        CHECK_MEGA(*mismatches, mega, "held item after the battle", GetMonData(partyMon, MON_DATA_HELD_ITEM), stone);
        CHECK_MEGA(*mismatches, mega, "Attack after the battle", GetMonData(partyMon, MON_DATA_ATK), ExpectedStat(&PLAYER_PARTY[0], holder, STAT_ATK));
    }
}

// Split by generation so that the four run in parallel and a failure only holds up its own group.
SINGLE_BATTLE_TEST("Mega Evolution: every Gen 1-2 Mega Stone holder becomes its Mega form, with that form's typing, Ability and stats", u32 mismatches)
{
    enum Species holder = SPECIES_NONE, mega = SPECIES_NONE;
    enum Item stone = ITEM_NONE;
    PARAMETRIZE_MEGA_STONES(NATIONAL_DEX_BULBASAUR, NATIONAL_DEX_CELEBI);
    MegaEvolveHolder(player, holder, stone, mega, &results[i].mismatches);
    FINALLY {
        u32 total = 0;
        for (u32 j = 0; j < gBattleTestRunnerState->parameters; j++)
            total += results[j].mismatches;
        EXPECT_EQ(total, 0);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: every Gen 3 Mega Stone holder becomes its Mega form, with that form's typing, Ability and stats", u32 mismatches)
{
    enum Species holder = SPECIES_NONE, mega = SPECIES_NONE;
    enum Item stone = ITEM_NONE;
    PARAMETRIZE_MEGA_STONES(NATIONAL_DEX_CELEBI + 1, NATIONAL_DEX_DEOXYS);
    MegaEvolveHolder(player, holder, stone, mega, &results[i].mismatches);
    FINALLY {
        u32 total = 0;
        for (u32 j = 0; j < gBattleTestRunnerState->parameters; j++)
            total += results[j].mismatches;
        EXPECT_EQ(total, 0);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: every Gen 4-5 Mega Stone holder becomes its Mega form, with that form's typing, Ability and stats", u32 mismatches)
{
    enum Species holder = SPECIES_NONE, mega = SPECIES_NONE;
    enum Item stone = ITEM_NONE;
    PARAMETRIZE_MEGA_STONES(NATIONAL_DEX_DEOXYS + 1, NATIONAL_DEX_GENESECT);
    MegaEvolveHolder(player, holder, stone, mega, &results[i].mismatches);
    FINALLY {
        u32 total = 0;
        for (u32 j = 0; j < gBattleTestRunnerState->parameters; j++)
            total += results[j].mismatches;
        EXPECT_EQ(total, 0);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: every Gen 6-9 Mega Stone holder becomes its Mega form, with that form's typing, Ability and stats", u32 mismatches)
{
    enum Species holder = SPECIES_NONE, mega = SPECIES_NONE;
    enum Item stone = ITEM_NONE;
    PARAMETRIZE_MEGA_STONES(NATIONAL_DEX_GENESECT + 1, NATIONAL_DEX_COUNT);
    MegaEvolveHolder(player, holder, stone, mega, &results[i].mismatches);
    FINALLY {
        u32 total = 0;
        for (u32 j = 0; j < gBattleTestRunnerState->parameters; j++)
            total += results[j].mismatches;
        EXPECT_EQ(total, 0);
    }
}

SINGLE_BATTLE_TEST("Mega Evolution: Rayquaza that knows Dragon Ascent becomes Mega Rayquaza, with that form's typing, Ability and stats")
{
    GIVEN {
        PLAYER(SPECIES_RAYQUAZA) { Moves(MOVE_DRAGON_ASCENT, MOVE_CELEBRATE); }
        OPPONENT(SPECIES_KOMALA);
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        MESSAGE("1's fervent wish has reached Rayquaza!");
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
    } THEN {
        EXPECT_EQ(CountMegaFormMismatches(player, &PLAYER_PARTY[0], SPECIES_RAYQUAZA_MEGA,
                                          GetSpeciesType(SPECIES_RAYQUAZA_MEGA, 0), GetSpeciesType(SPECIES_RAYQUAZA_MEGA, 1)), 0);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_RAYQUAZA);
    }
}

// The sweeps above start from the holders; this one starts from the Mega forms, so a Mega form no table leads to is caught.
TEST("Mega Evolution: every Mega form can be reached from its base form, by Mega Stone or by Dragon Ascent")
{
    enum Species mega = SPECIES_NONE;
    bool32 reached = FALSE;

    for (enum Species species = SPECIES_NONE + 1; species < NUM_SPECIES; species++)
    {
        if (IsSpeciesEnabled(species) && gSpeciesInfo[species].isMegaEvolution)
            PARAMETRIZE_LABEL("%S (species %d)", GetSpeciesName(species), species) { mega = species; }
    }
    ASSUME(mega != SPECIES_NONE);

    for (enum Species species = SPECIES_NONE + 1; species < NUM_SPECIES && !reached; species++)
    {
        const struct FormChange *formChanges = IsMegaEvolutionBase(species) ? GetSpeciesFormChanges(species) : NULL;
        if (formChanges == NULL)
            continue;
        for (u32 j = 0; formChanges[j].method != FORM_CHANGE_TERMINATOR; j++)
        {
            if (IsMegaEvolutionMethod(formChanges[j].method) && formChanges[j].targetSpecies == mega)
                reached = TRUE;
        }
    }
    EXPECT(reached);
}

// The Mega Evolution animation draws the Mega form with GetMonSpritePalFromSpeciesAndPersonality(Mega form, the Pokemon's
// shininess, its personality) (HandleSpeciesGfxDataChange, via AnimTask_HideSwapSprite).
TEST("Mega Evolution: every Mega form has a shiny palette of its own")
{
    enum Species holder = SPECIES_NONE, mega = SPECIES_NONE;

    for (enum Species species = SPECIES_NONE + 1; species < NUM_SPECIES; species++)
    {
        const struct FormChange *formChanges = IsMegaEvolutionBase(species) ? GetSpeciesFormChanges(species) : NULL;
        if (formChanges == NULL)
            continue;
        for (u32 j = 0; formChanges[j].method != FORM_CHANGE_TERMINATOR; j++)
        {
            if (IsMegaEvolutionMethod(formChanges[j].method))
                PARAMETRIZE_LABEL("%S (species %d)", GetSpeciesName(formChanges[j].targetSpecies), formChanges[j].targetSpecies) { holder = species; mega = formChanges[j].targetSpecies; }
        }
    }
    ASSUME(mega != SPECIES_NONE);

    const u16 *normalPalette = GetMonSpritePalFromSpeciesAndPersonality(mega, FALSE, 0);
    const u16 *shinyPalette = GetMonSpritePalFromSpeciesAndPersonality(mega, TRUE, 0);
    EXPECT(gSpeciesInfo[mega].palette != NULL);
    EXPECT(gSpeciesInfo[mega].shinyPalette != NULL);
    EXPECT(shinyPalette == gSpeciesInfo[mega].shinyPalette); // not SPECIES_NONE's fallback palette
    EXPECT(shinyPalette != normalPalette);
    EXPECT(memcmp(shinyPalette, normalPalette, PLTT_SIZE_4BPP) != 0);
    EXPECT(shinyPalette != GetMonSpritePalFromSpeciesAndPersonality(holder, TRUE, 0)); // not the holder's shiny colors
}

SINGLE_BATTLE_TEST("Mega Evolution: a shiny Pokemon Mega Evolves into its Mega form's shiny colors")
{
    bool32 shiny = FALSE;
    PARAMETRIZE { shiny = FALSE; }
    PARAMETRIZE { shiny = TRUE; }
    GIVEN {
        FORCE_MOVE_ANIM(TRUE); // The Mega Evolution animation is what swaps the sprite and loads the Mega form's palette.
        PLAYER(SPECIES_CHARIZARD) { Item(ITEM_CHARIZARDITE_X); Shiny(shiny); }
        OPPONENT(SPECIES_WOBBUFFET) { Shiny(FALSE); } // Shiny() carries over to the Pokemon declared after it.
    } WHEN {
        TURN { MOVE(player, MOVE_CELEBRATE, gimmick: GIMMICK_MEGA); }
    } SCENE {
        ANIMATION(ANIM_TYPE_GENERAL, B_ANIM_MEGA_EVOLUTION, player);
    } THEN {
        const u16 *megaPalette = shiny ? gSpeciesInfo[SPECIES_CHARIZARD_MEGA_X].shinyPalette : gSpeciesInfo[SPECIES_CHARIZARD_MEGA_X].palette;
        FORCE_MOVE_ANIM(FALSE);
        EXPECT_EQ(player->species, SPECIES_CHARIZARD_MEGA_X);
        EXPECT_EQ((u32)player->isShiny, (u32)shiny);
        EXPECT(GetMonSpritePalFromSpeciesAndPersonality(player->species, player->isShiny, player->personality) == megaPalette);
        // The colors the battler's sprite is drawn with (palette slot = battler id).
        EXPECT_EQ(memcmp(&gPlttBufferUnfaded[OBJ_PLTT_ID(B_BATTLER_0)], megaPalette, PLTT_SIZE_4BPP), 0);
        // Still shiny once it is its base form again after the battle.
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_SHINY), (u32)shiny);
    }
}
