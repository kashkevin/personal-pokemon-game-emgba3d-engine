// Native PC build: self-test of move switching as in Legends Z-A (MOVES in the party menu and the summary screen's
// relearner; src/move_relearner.c, the end of the file). Project-owned. Runs the game's own code on save state built
// here, then exits:
//   --selftest-move-switch
// What a Pokemon can switch in (level-up moves to its level with its pre-evolutions', egg moves only if it hatched and
// from its own line, the TMs and HMs the player has, the moves of the one-time tutors used), TMs never used up, an HM's
// move switched out only when the HM can switch it back in, the warnings before a move goes (can't be switched back in,
// PP Ups lost), and the places with no move switching (Battle Pike, Battle Pyramid, Battle Factory, Slateport Battle
// Tent).
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "move.h"
#include "move_relearner.h"
#include "party_menu.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/region_map_sections.h"

bool32 PartyMenuTest_OffersMoves(u8 slot);
bool32 SummaryTest_MoveSwitching(void *mons, u8 monIndex, u8 maxMonIndex, u8 slot, u16 newMove);

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static const char *Ascii(const u8 *text)
{
    static char out[256];
    u32 n = 0;

    for (; text != NULL && *text != EOS && n < sizeof(out) - 1; text++)
    {
        u8 c = *text;

        if (c >= CHAR_A && c <= CHAR_Z)
            out[n++] = 'A' + (c - CHAR_A);
        else if (c >= CHAR_a && c <= CHAR_z)
            out[n++] = 'a' + (c - CHAR_a);
        else if (c >= CHAR_0 && c <= CHAR_9)
            out[n++] = '0' + (c - CHAR_0);
        else if (c == CHAR_EXTRA_SYMBOL || c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN)
            text++; // a control code and its argument
        else
            out[n++] = c == CHAR_SGL_QUOTE_RIGHT ? '\'' : c == CHAR_QUESTION_MARK ? '?' : c == CHAR_PERIOD ? '.' : ' ';
    }
    out[n] = 0;
    return out;
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    SetBagItemsPointers();
    ClearBag();
    ZeroPlayerPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gMapHeader.mapLayoutId = LAYOUT_LITTLEROOT_TOWN;
    gMapHeader.regionMapSectionId = MAPSEC_LITTLEROOT_TOWN;
    gRelearnMode = RELEARN_MODE_NONE;
    gSpecialVar_0x8004 = 0;
}

// The party's first Pokemon: `species` at `level` with its level-up moveset, met at that level (hatched: met at 0)
static struct BoxPokemon *Lead(u32 species, u32 level, bool32 hatched)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u32 metLevel = hatched ? 0 : level;

    CreateMon(mon, species, level, 0x1234, OTID_STRUCT_PLAYER_ID);
    GiveMonInitialMoveset(mon);
    SetMonData(mon, MON_DATA_MET_LEVEL, &metLevel);
    CalculatePlayerPartyCount();
    return &mon->box;
}

static bool32 SpeciesLearnsByLevel(u32 species, enum Move move, u32 level)
{
    const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);

    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= level; i++)
    {
        if (learnset[i].move == move)
            return TRUE;
    }
    return FALSE;
}

static void TestTmsReusable(void)
{
    Expect("TMs are never used up on PC: TM03 Water Pulse is important (I_REUSABLE_TMS)",
           GetItemImportance(ITEM_TM_WATER_PULSE) != 0);
    Expect("... TM39 Rock Tomb (Roxanne's, given once) too", GetItemImportance(ITEM_TM_ROCK_TOMB) != 0);
    Expect("HMs stay key items", GetItemImportance(ITEM_HM_SURF) != 0);
}

static void TestLevelUp(void)
{
    struct BoxPokemon *mon;
    const struct LevelUpMove *learnset;
    enum Move above = MOVE_NONE, azurillOnly = MOVE_NONE;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("Mudkip Lv 20: Tackle (Lv 1) can be switched back in", MoveRelearner_CanSwitchBackIn(mon, MOVE_TACKLE));
    learnset = GetSpeciesLevelUpLearnset(SPECIES_MUDKIP);
    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && above == MOVE_NONE; i++)
    {
        if (learnset[i].level > 20 && !SpeciesLearnsByLevel(SPECIES_MUDKIP, learnset[i].move, 20)
         && GetTMHMItemIdFromMoveId(learnset[i].move) == ITEM_NONE)
            above = learnset[i].move;
    }
    Expect("Mudkip Lv 20: a move it learns only after Lv 20 is not offered",
           above != MOVE_NONE && !MoveRelearner_CanSwitchBackIn(mon, above));

    // A pre-evolution's level-up move (Azurill's, not Marill's or Azumarill's) up to the Pokemon's level
    learnset = GetSpeciesLevelUpLearnset(SPECIES_AZURILL);
    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= 30 && azurillOnly == MOVE_NONE; i++)
    {
        enum Move move = learnset[i].move;

        if (!SpeciesLearnsByLevel(SPECIES_AZUMARILL, move, 30) && !SpeciesLearnsByLevel(SPECIES_MARILL, move, 30)
         && GetTMHMItemIdFromMoveId(move) == ITEM_NONE)
            azurillOnly = move;
    }
    mon = Lead(SPECIES_AZUMARILL, 30, FALSE);
    if (azurillOnly != MOVE_NONE)
        Expect("Azumarill Lv 30: a move only Azurill learns by level (by Lv 30) is offered (its pre-evolution's)",
               MoveRelearner_CanSwitchBackIn(mon, azurillOnly) && HasMoveToRelearn(mon, MOVE_RELEARNER_LEVEL_UP_MOVES));
    else
        printf("skip Azurill has no level-up move of its own by Lv 30 in this data\n");
    gRelearnMode = RELEARN_MODE_NONE;
}

// ------------------------------------------------------------------------------- the level-up list's caps (the fix)

// The relearner's own list (MoveRelearner_GetListMoves: what its screen shows) has `move`
static bool32 ListHas(struct BoxPokemon *mon, enum MoveRelearnerStates list, enum Move move)
{
    u16 moves[MAX_RELEARNER_MOVES];
    u32 count = MoveRelearner_GetListMoves(mon, list, moves);

    for (u32 i = 0; i < count; i++)
    {
        if (moves[i] == move)
            return TRUE;
    }
    return FALSE;
}

// The Pokemon knows Splash alone (no move of the lists checked): what the lists offer is all there is to see
static void KnowOnlySplash(struct BoxPokemon *mon)
{
    u16 move = MOVE_SPLASH;

    SetBoxMonData(mon, MON_DATA_MOVE1, &move);
    move = MOVE_NONE;
    for (u32 i = 1; i < MAX_MON_MOVES; i++)
        SetBoxMonData(mon, MON_DATA_MOVE1 + i, &move);
}

// Offered by level up: on the list and switchable back in (the warnings' rule) alike
static bool32 LevelUpOffers(struct BoxPokemon *mon, enum Move move)
{
    return ListHas(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, move) && MoveRelearner_CanSwitchBackIn(mon, move);
}

static bool32 LevelUpRefuses(struct BoxPokemon *mon, enum Move move)
{
    return !ListHas(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, move) && !MoveRelearner_CanSwitchBackIn(mon, move);
}

static u32 LearnLevel(u32 species, enum Move move)
{
    const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);

    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END; i++)
    {
        if (learnset[i].move == move)
            return learnset[i].level;
    }
    return 0xFF;
}

static u32 EvolutionLevel(u32 species, u32 into)
{
    const struct Evolution *evos = GetSpeciesEvolutions(species);

    for (u32 i = 0; evos != NULL && evos[i].method != EVOLUTIONS_END; i++)
    {
        if (evos[i].targetSpecies == into && evos[i].method == EVO_LEVEL)
            return evos[i].param;
    }
    return 0;
}

// A Pokemon's level-up list may hold only moves one of its forms learns by its level (never above it)
static bool32 ListWithinLevel(struct BoxPokemon *mon)
{
    u16 moves[MAX_RELEARNER_MOVES], chain[4];
    u32 count = MoveRelearner_GetListMoves(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, moves);
    u32 level = GetLevelFromBoxMonExp(mon), lastLevel = 0, forms = 0;

    for (u32 stage = GetBoxMonData(mon, MON_DATA_SPECIES); stage != SPECIES_NONE && forms < 4; stage = GetSpeciesPreEvolution(stage))
        chain[forms++] = stage;
    for (u32 i = 0; i < count; i++)
    {
        struct MoveSwitchSource source = {0};
        bool32 found = FALSE;

        for (u32 f = 0; f < forms && !found; f++)
            found = LearnLevel(chain[f], moves[i]) <= level;
        if (!found || !MoveRelearner_GetSource(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, moves[i], &source)
         || source.kind != MOVE_SOURCE_LEVEL || source.number > level || source.number < lastLevel)
        {
            printf("     %s:", Ascii(GetSpeciesName(GetBoxMonData(mon, MON_DATA_SPECIES))));
            printf(" %s on the list (source level %u, the Pokemon's level %u)\n", Ascii(GetMoveName(moves[i])),
                   (unsigned)source.number, (unsigned)level);
            return FALSE;
        }
        lastLevel = source.number; // and in level order
    }
    return TRUE;
}

static void TestLevelUpCaps(void)
{
    struct BoxPokemon *mon;
    struct MoveSwitchSource source;
    u8 text[16];

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    // the data the checks stand on (this game's level-up learnsets: P_LVL_UP_LEARNSETS, GEN_LATEST is GEN_6 here)
    Expect("(data) Charmander evolves at Lv 16, Charmeleon at Lv 36",
           EvolutionLevel(SPECIES_CHARMANDER, SPECIES_CHARMELEON) == 16 && EvolutionLevel(SPECIES_CHARMELEON, SPECIES_CHARIZARD) == 36);
    Expect("(data) Scary Face: Charmander Lv 19, Charmeleon Lv 21; Dragon Rage: Charmander Lv 16, Charmeleon Lv 17",
           LearnLevel(SPECIES_CHARMANDER, MOVE_SCARY_FACE) == 19 && LearnLevel(SPECIES_CHARMELEON, MOVE_SCARY_FACE) == 21
           && LearnLevel(SPECIES_CHARMANDER, MOVE_DRAGON_RAGE) == 16 && LearnLevel(SPECIES_CHARMELEON, MOVE_DRAGON_RAGE) == 17);

    // Charmeleon Lv 20 (the owner's case): Charmander's moves count only up to Lv 16, where it evolved
    mon = Lead(SPECIES_CHARMELEON, 20, FALSE);
    KnowOnlySplash(mon);
    Expect("Charmeleon Lv 20: no Scary Face (Charmander's at Lv 19, after it evolved at 16; Charmeleon's own at Lv 21)",
           LevelUpRefuses(mon, MOVE_SCARY_FACE));
    Expect("Charmeleon Lv 20: Dragon Rage, Smokescreen, Ember offered", LevelUpOffers(mon, MOVE_DRAGON_RAGE)
           && LevelUpOffers(mon, MOVE_SMOKESCREEN) && LevelUpOffers(mon, MOVE_EMBER));
    MoveRelearner_GetSource(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, MOVE_DRAGON_RAGE, &source);
    MoveRelearner_SourceText(&source, text, FALSE);
    Expect("... Dragon Rage's tag: \"Lv 17\", Charmeleon's own (its form first)", source.kind == MOVE_SOURCE_LEVEL
           && source.number == 17 && source.species == SPECIES_CHARMELEON && strcmp(Ascii(text), "Lv 17") == 0);
    Expect("Charmeleon Lv 20: every move on its list is one it could have learned by Lv 20, in level order", ListWithinLevel(mon));

    // a level just below and at a learn level
    mon = Lead(SPECIES_CHARMELEON, 21, FALSE);
    KnowOnlySplash(mon);
    Expect("Charmeleon Lv 21: Scary Face (its own Lv 21)", LevelUpOffers(mon, MOVE_SCARY_FACE));
    mon = Lead(SPECIES_CHARMELEON, 16, FALSE);
    KnowOnlySplash(mon);
    Expect("Charmeleon Lv 16: Dragon Rage, Charmander's at Lv 16 - the level it evolved at counts",
           LevelUpOffers(mon, MOVE_DRAGON_RAGE) && MoveRelearner_GetSource(mon, MOVE_RELEARNER_LEVEL_UP_MOVES, MOVE_DRAGON_RAGE, &source)
           && source.species == SPECIES_CHARMANDER && source.number == 16);
    mon = Lead(SPECIES_CHARMANDER, 15, FALSE);
    KnowOnlySplash(mon);
    Expect("Charmander Lv 15: no Dragon Rage yet (Lv 16)", LevelUpRefuses(mon, MOVE_DRAGON_RAGE));
    Expect("(data) Mudkip learns Mud Sport at Lv 20", LearnLevel(SPECIES_MUDKIP, MOVE_MUD_SPORT) == 20);
    mon = Lead(SPECIES_MUDKIP, 19, FALSE);
    KnowOnlySplash(mon);
    Expect("Mudkip Lv 19: no Mud Sport by level up yet", LevelUpRefuses(mon, MOVE_MUD_SPORT));
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    KnowOnlySplash(mon);
    Expect("Mudkip Lv 20: Mud Sport", LevelUpOffers(mon, MOVE_MUD_SPORT));

    // two evolutions by level: each form up to its own evolution, never above the next form's cap
    Expect("(data) Flamethrower: Charmander Lv 37, Charmeleon Lv 43, Charizard Lv 47; Slash: 34, 39, 41",
           LearnLevel(SPECIES_CHARMANDER, MOVE_FLAMETHROWER) == 37 && LearnLevel(SPECIES_CHARMELEON, MOVE_FLAMETHROWER) == 43
           && LearnLevel(SPECIES_CHARIZARD, MOVE_FLAMETHROWER) == 47 && LearnLevel(SPECIES_CHARMANDER, MOVE_SLASH) == 34
           && LearnLevel(SPECIES_CHARMELEON, MOVE_SLASH) == 39 && LearnLevel(SPECIES_CHARIZARD, MOVE_SLASH) == 41);
    mon = Lead(SPECIES_CHARIZARD, 40, FALSE);
    KnowOnlySplash(mon);
    Expect("Charizard Lv 40: no Flamethrower (Charmander's Lv 37 > 16, Charmeleon's Lv 43, Charizard's own Lv 47)",
           LevelUpRefuses(mon, MOVE_FLAMETHROWER));
    Expect("Charizard Lv 40: no Slash (Charmeleon's Lv 39 is after it evolved at 36; its own Lv 41)",
           LevelUpRefuses(mon, MOVE_SLASH));
    Expect("Charizard Lv 40: Flame Burst (Charmeleon's and its own Lv 32), Wing Attack (its own Lv 36)",
           LevelUpOffers(mon, MOVE_FLAME_BURST) && LevelUpOffers(mon, MOVE_WING_ATTACK));
    Expect("Charizard Lv 40: its list within its level, in level order", ListWithinLevel(mon));
    mon = Lead(SPECIES_CHARIZARD, 41, FALSE);
    KnowOnlySplash(mon);
    Expect("Charizard Lv 41: Slash (its own Lv 41)", LevelUpOffers(mon, MOVE_SLASH));

    // Shedinja comes when Nincada evolves at Lv 20 (EVO_SPLIT_FROM_EVO)
    Expect("(data) Nincada learns Metal Claw at Lv 21 and Bide at Lv 29, Shedinja neither, Ninjask no Metal Claw; Lv 20",
           LearnLevel(SPECIES_NINCADA, MOVE_METAL_CLAW) == 21 && LearnLevel(SPECIES_NINCADA, MOVE_BIDE) == 29
           && LearnLevel(SPECIES_SHEDINJA, MOVE_METAL_CLAW) == 0xFF && LearnLevel(SPECIES_SHEDINJA, MOVE_BIDE) == 0xFF
           && LearnLevel(SPECIES_NINJASK, MOVE_METAL_CLAW) == 0xFF && EvolutionLevel(SPECIES_NINCADA, SPECIES_NINJASK) == 20);
    mon = Lead(SPECIES_SHEDINJA, 30, FALSE);
    KnowOnlySplash(mon);
    Expect("Shedinja Lv 30: no Metal Claw, no Bide (Nincada's, after Lv 20)",
           LevelUpRefuses(mon, MOVE_METAL_CLAW) && LevelUpRefuses(mon, MOVE_BIDE));
    Expect("Shedinja Lv 30: Mud-Slap (Nincada's Lv 17)", LevelUpOffers(mon, MOVE_MUD_SLAP));
    mon = Lead(SPECIES_NINJASK, 30, FALSE);
    KnowOnlySplash(mon);
    Expect("Ninjask Lv 30: no Metal Claw either", LevelUpRefuses(mon, MOVE_METAL_CLAW));

    // a move learned on evolving (level 0): "Evo"
    source.kind = MOVE_SOURCE_LEVEL;
    source.number = 0;
    MoveRelearner_SourceText(&source, text, FALSE);
    Expect("a move learned on evolving is tagged \"Evo\"", strcmp(Ascii(text), "Evo") == 0);

    // an evolution with no level (a stone, friendship): the earlier form may have stayed so up to the Pokemon's level
    {
        u32 found = 0;

        for (u32 species = 1; species < NUM_SPECIES && !found; species++)
        {
            const struct Evolution *evos;

            if (!IsSpeciesEnabled(species))
                continue;
            evos = GetSpeciesEvolutions(species);
            for (u32 e = 0; evos != NULL && evos[e].method != EVOLUTIONS_END && !found; e++)
            {
                const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);
                u32 target = evos[e].targetSpecies, below = MOVE_NONE, above = MOVE_NONE;

                if (evos[e].method != EVO_ITEM || !IsSpeciesEnabled(target) || GetSpeciesPreEvolution(target) != species
                 || GetSpeciesPreEvolution(species) != SPECIES_NONE)
                    continue;
                for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END; i++)
                {
                    if (LearnLevel(target, learnset[i].move) <= 60 || GetTMHMItemIdFromMoveId(learnset[i].move) != ITEM_NONE)
                        continue;
                    if (learnset[i].level >= 2 && learnset[i].level <= 40 && below == MOVE_NONE)
                        below = learnset[i].move;
                    else if (learnset[i].level > 40 && above == MOVE_NONE)
                        above = learnset[i].move;
                }
                if (below == MOVE_NONE)
                    continue;
                found = target;
                mon = Lead(target, 40, FALSE);
                KnowOnlySplash(mon);
                printf("     a stone evolution: %s", Ascii(GetSpeciesName(target)));
                printf(" from %s at Lv 40:", Ascii(GetSpeciesName(species)));
                printf(" %s (its earlier form's Lv %u)\n", Ascii(GetMoveName(below)), (unsigned)LearnLevel(species, below));
                Expect("a stone evolution's Pokemon at Lv 40: its earlier form's move by Lv 40 offered (no level of its own)",
                       LevelUpOffers(mon, below));
                if (above != MOVE_NONE)
                    Expect("... and one its earlier form learns after Lv 40 not", LevelUpRefuses(mon, above));
                Expect("... its list within its level", ListWithinLevel(mon));
            }
        }
        if (!found)
            printf("skip no stone evolution with a move of its earlier form's own in this data\n");
    }

    // every Pokemon's list within its level (a sample across the species at three levels)
    {
        u32 bad = 0, seen = 0;

        for (u32 species = 1; species < NUM_SPECIES; species += 3)
        {
            static const u8 sLevels[] = {5, 30, 70};

            if (!IsSpeciesEnabled(species) || GetSpeciesPreEvolution(species) == SPECIES_NONE)
                continue;
            for (u32 l = 0; l < ARRAY_COUNT(sLevels); l++)
            {
                mon = Lead(species, sLevels[l], FALSE);
                seen++;
                if (!ListWithinLevel(mon) && ++bad > 5)
                    break;
            }
            if (bad > 5)
                break;
        }
        printf("     %u evolved Pokemon and levels checked\n", (unsigned)seen);
        Expect("no evolved Pokemon is offered a level-up move above its own level (a third of the species, Lv 5/30/70)",
               bad == 0 && seen > 100);
    }
    gRelearnMode = RELEARN_MODE_NONE;
}

// The other lists as the relearner builds them (what its screen shows), each source's tag
static void TestListSources(void)
{
    struct BoxPokemon *mon;
    struct MoveSwitchSource source;
    u8 text[16];

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_TREECKO, 5, FALSE);
    Expect("caught Treecko: its egg move list is empty (no Leaf Storm)",
           MoveRelearner_GetListMoves(mon, MOVE_RELEARNER_EGG_MOVES, (u16[MAX_RELEARNER_MOVES]){0}) == 0
           && !ListHas(mon, MOVE_RELEARNER_EGG_MOVES, MOVE_LEAF_STORM));
    mon = Lead(SPECIES_TREECKO, 5, TRUE);
    Expect("hatched Treecko: Leaf Storm on its egg move list, tagged \"Egg\"",
           ListHas(mon, MOVE_RELEARNER_EGG_MOVES, MOVE_LEAF_STORM)
           && MoveRelearner_GetSource(mon, MOVE_RELEARNER_EGG_MOVES, MOVE_LEAF_STORM, &source)
           && source.kind == MOVE_SOURCE_EGG && source.species == SPECIES_TREECKO
           && MoveRelearner_SourceText(&source, text, FALSE) != NULL && strcmp(Ascii(text), "Egg") == 0);
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("no tutor used: Double-Edge not on the tutor list", !ListHas(mon, MOVE_RELEARNER_TUTOR_MOVES, MOVE_DOUBLE_EDGE));
    FlagSet(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE);
    Expect("the Sootopolis tutor used: Double-Edge on the tutor list, tagged \"Tutor\"",
           ListHas(mon, MOVE_RELEARNER_TUTOR_MOVES, MOVE_DOUBLE_EDGE)
           && MoveRelearner_GetSource(mon, MOVE_RELEARNER_TUTOR_MOVES, MOVE_DOUBLE_EDGE, &source)
           && source.kind == MOVE_SOURCE_TUTOR && MoveRelearner_SourceText(&source, text, FALSE) != NULL && strcmp(Ascii(text), "Tutor") == 0);
    {
        u8 place[32];

        MoveRelearner_TutorPlace(source.number, place);
        Expect("... the details name its town: Sootopolis City", strcmp(Ascii(place), "SOOTOPOLIS CITY") == 0);
        printf("     %s\n", Ascii(place));
    }
    Expect("no TM03: Water Pulse not on the TM list", !ListHas(mon, MOVE_RELEARNER_TM_MOVES, MOVE_WATER_PULSE));
    AddBagItem(ITEM_TM_WATER_PULSE, 1);
    AddBagItem(ITEM_HM_SURF, 1);
    Expect("TM03 in the Bag: Water Pulse on the TM list, tagged \"TM03\"",
           ListHas(mon, MOVE_RELEARNER_TM_MOVES, MOVE_WATER_PULSE)
           && MoveRelearner_GetSource(mon, MOVE_RELEARNER_TM_MOVES, MOVE_WATER_PULSE, &source)
           && source.kind == MOVE_SOURCE_TM && source.number == 3
           && MoveRelearner_SourceText(&source, text, FALSE) != NULL && strcmp(Ascii(text), "TM03") == 0);
    Expect("HM03 in the Bag: Surf on the TM list, tagged \"HM03\"",
           ListHas(mon, MOVE_RELEARNER_TM_MOVES, MOVE_SURF)
           && MoveRelearner_GetSource(mon, MOVE_RELEARNER_TM_MOVES, MOVE_SURF, &source)
           && source.kind == MOVE_SOURCE_HM && source.number == 3
           && MoveRelearner_SourceText(&source, text, FALSE) != NULL && strcmp(Ascii(text), "HM03") == 0);
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestEggMoves(void)
{
    struct BoxPokemon *mon;

    Setup();
    Expect("(data) Leaf Storm is a Treecko egg move", SpeciesHasEggMove(SPECIES_TREECKO, MOVE_LEAF_STORM));
    mon = Lead(SPECIES_TREECKO, 5, FALSE);
    Expect("a caught Lv 5 Treecko (met at Lv 5): no egg moves at all", !HasMoveToRelearn(mon, MOVE_RELEARNER_EGG_MOVES)
           && !CanBoxMonRelearnMoves(mon, MOVE_RELEARNER_EGG_MOVES));
    Expect("... Leaf Storm is not offered", !MoveRelearner_CanSwitchBackIn(mon, MOVE_LEAF_STORM));
    mon = Lead(SPECIES_TREECKO, 5, TRUE);
    Expect("a Treecko that hatched from an Egg (met at Lv 0): its egg moves (Leaf Storm)",
           CanBoxMonRelearnMoves(mon, MOVE_RELEARNER_EGG_MOVES) && MoveRelearner_CanSwitchBackIn(mon, MOVE_LEAF_STORM));

    // The incense babies: an Egg made without Sea Incense / Full Incense hatches as Marill / Snorlax
    Expect("(data) Belly Drum is Marill's egg move, not Azurill's",
           SpeciesHasEggMove(SPECIES_MARILL, MOVE_BELLY_DRUM) && !SpeciesHasEggMove(SPECIES_AZURILL, MOVE_BELLY_DRUM));
    mon = Lead(SPECIES_AZUMARILL, 30, TRUE);
    Expect("a hatched Azumarill: Marill's egg moves (Belly Drum, Aqua Jet, Superpower)",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM) && MoveRelearner_CanSwitchBackIn(mon, MOVE_AQUA_JET)
           && MoveRelearner_CanSwitchBackIn(mon, MOVE_SUPERPOWER));
    Expect("... and Azurill's (Encore: it may have hatched as Azurill)", MoveRelearner_CanSwitchBackIn(mon, MOVE_ENCORE));
    mon = Lead(SPECIES_MARILL, 15, TRUE);
    Expect("a hatched Marill: Belly Drum", MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM));
    mon = Lead(SPECIES_AZUMARILL, 30, FALSE);
    Expect("a caught Azumarill: no egg moves (Belly Drum not offered)",
           !MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM) && !HasMoveToRelearn(mon, MOVE_RELEARNER_EGG_MOVES));
    Expect("(data) Fissure and Power-Up Punch are Snorlax's egg moves, not Munchlax's",
           SpeciesHasEggMove(SPECIES_SNORLAX, MOVE_FISSURE) && !SpeciesHasEggMove(SPECIES_MUNCHLAX, MOVE_FISSURE));
    mon = Lead(SPECIES_SNORLAX, 40, TRUE);
    Expect("a hatched Snorlax: its own egg moves (Fissure, Power-Up Punch)",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_FISSURE) && MoveRelearner_CanSwitchBackIn(mon, MOVE_POWER_UP_PUNCH));
    mon = Lead(SPECIES_SWAMPERT, 40, TRUE);
    Expect("a hatched Swampert: Mudkip's egg moves (Mirror Coat)", SpeciesHasEggMove(SPECIES_MUDKIP, MOVE_MIRROR_COAT)
           && MoveRelearner_CanSwitchBackIn(mon, MOVE_MIRROR_COAT));
}

static void TestTutors(void)
{
    struct BoxPokemon *mon;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("(data) Mudkip can learn Double-Edge and Snore from tutors",
           CanLearnTeachableMove(SPECIES_MUDKIP, MOVE_DOUBLE_EDGE) && CanLearnTeachableMove(SPECIES_MUDKIP, MOVE_SNORE));
    Expect("no tutor used: no tutor moves (the old list offered all 30 from the start)",
           !HasMoveToRelearn(mon, MOVE_RELEARNER_TUTOR_MOVES) && !MoveRelearner_CanSwitchBackIn(mon, MOVE_DOUBLE_EDGE));
    FlagSet(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE);
    Expect("the Sootopolis tutor used (its flag): Double-Edge offered",
           HasMoveToRelearn(mon, MOVE_RELEARNER_TUTOR_MOVES) && MoveRelearner_CanSwitchBackIn(mon, MOVE_DOUBLE_EDGE));
    for (u32 flag = FLAG_MOVE_TUTOR_TAUGHT_SWAGGER; flag <= FLAG_MOVE_TUTOR_TAUGHT_EXPLOSION; flag++)
        FlagSet(flag);
    Expect("every one-time tutor used: a Battle Frontier tutor's move (Snore, for Battle Points) still not offered",
           !MoveRelearner_CanSwitchBackIn(mon, MOVE_SNORE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestTms(void)
{
    struct BoxPokemon *mon;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("no TM03: Water Pulse not offered", !MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE));
    AddBagItem(ITEM_TM_WATER_PULSE, 1);
    Expect("TM03 in the Bag: Water Pulse offered", MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE)
           && HasMoveToRelearn(mon, MOVE_RELEARNER_TM_MOVES));
    RemoveBagItem(ITEM_TM_WATER_PULSE, 1);
    AddPCItem(ITEM_TM_WATER_PULSE, 1);
    Expect("TM03 in the PC's item storage (an older save): Water Pulse offered",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestHms(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    struct BoxPokemon *mon;

    Setup();
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    SetMonMoveSlot(lead, MOVE_SURF, 0);
    AddBagItem(ITEM_HM_SURF, 1);
    Expect("no move switching (a move learned by level up): Surf stays (P_CAN_FORGET_HIDDEN_MOVE)",
           !MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    Expect("MOVES, HM03 in the Bag: Surf can be switched out (the summary lets it be chosen)",
           MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_PSS_PAGE_BATTLE_MOVES;
    Expect("the summary's relearner, HM03 in the Bag: the same", MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF));
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    RemoveBagItem(ITEM_HM_SURF, 1);
    Expect("MOVES with no HM03 (a traded-in Surf): Surf stays, it could not come back",
           !MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestWarnings(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    struct BoxPokemon *mon;
    const u8 *warning;
    u8 bonus;

    Setup();
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    SetMonMoveSlot(lead, MOVE_TACKLE, 0);
    SetMonMoveSlot(lead, MOVE_SNORE, 1);
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    Expect("Tackle (a level-up move), no PP Ups: switched out with no question", MoveRelearner_ForgetWarning(mon, 0) == NULL);
    warning = MoveRelearner_ForgetWarning(mon, 1);
    Expect("Snore (a Battle Frontier tutor's): \"can't be switched back in later\"",
           warning != NULL && strstr(Ascii(warning), "can't be") != NULL && strstr(Ascii(warning), "PP Ups") == NULL);
    printf("     %s\n", Ascii(warning));
    bonus = 1 << 0; // one PP Up on the first slot
    SetMonData(lead, MON_DATA_PP_BONUSES, &bonus);
    warning = MoveRelearner_ForgetWarning(mon, 0);
    Expect("Tackle with a PP Up: \"PP Ups will be lost\"",
           warning != NULL && strstr(Ascii(warning), "PP Ups") != NULL && strstr(Ascii(warning), "can't be") == NULL);
    printf("     %s\n", Ascii(warning));
    bonus = 3 << 2; // PP Max on the second slot
    SetMonData(lead, MON_DATA_PP_BONUSES, &bonus);
    warning = MoveRelearner_ForgetWarning(mon, 1);
    Expect("Snore with PP Max: both", warning != NULL && strstr(Ascii(warning), "PP Ups") != NULL
           && strstr(Ascii(warning), "can't be") != NULL);
    printf("     %s\n", Ascii(warning));
    Expect("... a PP bonus on another slot is not this one's", MoveRelearner_ForgetWarning(mon, 0) == NULL);
    gRelearnMode = RELEARN_MODE_NONE;
    Expect("not move switching (a move learned by level up, the Move Reminder): no question, as on the GBA",
           MoveRelearner_ForgetWarning(mon, 1) == NULL);
    gRelearnMode = RELEARN_MODE_SCRIPT;
    Expect("... the Move Reminder's relearner (RELEARN_MODE_SCRIPT) neither", MoveRelearner_ForgetWarning(mon, 1) == NULL);
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestPlaces(void)
{
    static const struct
    {
        const char *name;
        u16 layout;
        bool8 offered;
    } sPlaces[] =
    {
        {"Littleroot Town", LAYOUT_LITTLEROOT_TOWN, TRUE},
        {"the Battle Pike's rooms", LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL, FALSE},
        {"the Battle Pyramid's floors", LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR, FALSE},
        {"the Battle Pyramid's top", LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_TOP, FALSE},
        {"the Battle Factory's battle room", LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_BATTLE_ROOM, FALSE},
    };
    char what[160];

    Setup();
    Lead(SPECIES_MUDKIP, 20, FALSE);
    for (u32 i = 0; i < ARRAY_COUNT(sPlaces); i++)
    {
        gMapHeader.mapLayoutId = sPlaces[i].layout;
        snprintf(what, sizeof(what), "%s: MOVES in the party menu %s", sPlaces[i].name, sPlaces[i].offered ? "offered" : "hidden");
        Expect(what, PartyMenuTest_OffersMoves(0) == sPlaces[i].offered);
        snprintf(what, sizeof(what), "%s: the summary's relearner (START on a move page) %s", sPlaces[i].name,
                 sPlaces[i].offered ? "offered" : "hidden");
        Expect(what, SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_NONE) == sPlaces[i].offered);
    }
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_TENT_BATTLE_ROOM;
    gMapHeader.regionMapSectionId = MAPSEC_SLATEPORT_CITY;
    Expect("the Slateport Battle Tent: MOVES hidden", !PartyMenuTest_OffersMoves(0));
    Expect("the Slateport Battle Tent: the summary's relearner hidden",
           !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_NONE));
    gMapHeader.regionMapSectionId = MAPSEC_VERDANTURF_TOWN;
    Expect("the Verdanturf Battle Tent (the player's own Pokemon, healed between battles): MOVES offered",
           PartyMenuTest_OffersMoves(0));
}

int NativeSelfTest_MoveSwitch(void)
{
    printf("selftest-move-switch: move switching as in Legends Z-A (MOVES, the summary's relearner)\n");
    TestTmsReusable();
    TestLevelUp();
    TestLevelUpCaps();
    TestListSources();
    TestEggMoves();
    TestTutors();
    TestTms();
    TestHms();
    TestWarnings();
    TestPlaces();
    printf("selftest-move-switch: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
