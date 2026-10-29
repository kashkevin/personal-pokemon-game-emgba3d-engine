// Native PC build: Update 30 self-tests (breeding, Egg shiny rates, the Day Care Egg queue, hatching steps, PC Eggs).
// Project-owned. Everything runs through the game's own Day Care code before the game starts, then exits.
//   --selftest-breeding
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "battle.h"
#include "daycare.h"
#include "egg_hatch.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "string_util.h"
#include "strings.h"
#include "constants/abilities.h"
#include "constants/daycare.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/species.h"
#include "platform/mp_breeding.h"

static int sLegChecks, sLegFailures;

static void ExpectLeg(const char *what, bool32 ok)
{
    sLegChecks++;
    if (!ok)
        sLegFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    SetBagItemsPointers();
    ClearBag();
    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    FlagClear(FLAG_PENDING_DAYCARE_EGG);
}

static void Deposit(u32 slot, enum Species species, u32 gender, u32 otId, bool32 shiny)
{
    struct Pokemon mon;
    u32 personality = GetMonPersonality(species, gender, NATURE_RANDOM, RANDOM_UNOWN_LETTER);

    CreateMonWithIVs(&mon, species, 30, personality, OTID_STRUCT_PRESET(otId), 31);
    SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
    gSaveBlock1Ptr->daycare.mons[slot].mon = mon.box;
    gSaveBlock1Ptr->daycare.mons[slot].steps = 0;
    gSaveBlock1Ptr->daycare.offspringPersonality = 0;
}

static void ClearDaycare(void)
{
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    MPDaycare_QueueClear();
    FlagClear(FLAG_PENDING_DAYCARE_EGG);
}

static u32 Score(enum Species a, u32 ga, u32 ota, enum Species b, u32 gb, u32 otb)
{
    ClearDaycare();
    Deposit(0, a, ga, ota, FALSE);
    Deposit(1, b, gb, otb, FALSE);
    return GetDaycareCompatibilityScore(&gSaveBlock1Ptr->daycare);
}

// one 64-step check from a fresh deposit: how many of `windows` checks made an Egg; every Egg is collected
struct EggRun { u32 windows, eggs, shiny, level1, allEggs; };

static void RunChecks(u32 windows, struct EggRun *out)
{
    memset(out, 0, sizeof(*out));
    out->windows = windows;
    out->level1 = out->allEggs = 1;
    for (u32 i = 0; i < windows; i++)
    {
        struct Pokemon egg;

        gSaveBlock1Ptr->daycare.mons[0].steps = 63;
        gSaveBlock1Ptr->daycare.mons[1].steps = 63;
        IncrementDaycareSteps();
        if (MPDaycare_QueueCount() == 0)
            continue;
        MPDaycare_QueuePop(&egg);
        FlagClear(FLAG_PENDING_DAYCARE_EGG);
        out->eggs++;
        if (GetMonData(&egg, MON_DATA_IS_SHINY))
            out->shiny++;
        if (GetMonData(&egg, MON_DATA_LEVEL) != 1)
            out->level1 = 0;
        if (!GetMonData(&egg, MON_DATA_IS_EGG))
            out->allEggs = 0;
    }
}

// A parent with chosen nature, held item, IVs, ability slot, ball and first move.
static void DepositEx(u32 slot, enum Species species, u32 gender, u32 otId, u32 nature, u32 item, const u8 *ivs, u32 abilityNum, u32 ball, u32 move)
{
    struct Pokemon mon;
    u32 personality = GetMonPersonality(species, gender, nature, RANDOM_UNOWN_LETTER);
    u32 shiny = FALSE;

    CreateMonWithIVs(&mon, species, 30, personality, OTID_STRUCT_PRESET(otId), 0);
    SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
    for (u32 i = 0; i < 6; i++)
        SetMonData(&mon, MON_DATA_HP_IV + i, &ivs[i]);
    SetMonData(&mon, MON_DATA_HELD_ITEM, &item);
    SetMonData(&mon, MON_DATA_ABILITY_NUM, &abilityNum);
    SetMonData(&mon, MON_DATA_POKEBALL, &ball);
    if (move != MOVE_NONE)
        SetMonData(&mon, MON_DATA_MOVE1, &move);
    gSaveBlock1Ptr->daycare.mons[slot].mon = mon.box;
    gSaveBlock1Ptr->daycare.mons[slot].steps = 0;
    gSaveBlock1Ptr->daycare.offspringPersonality = 0;
}

static bool32 NextEgg(struct Pokemon *egg)
{
    gSaveBlock1Ptr->daycare.mons[0].steps = 63;
    gSaveBlock1Ptr->daycare.mons[1].steps = 63;
    IncrementDaycareSteps();
    return MPDaycare_QueuePop(egg);
}

// PKMN_BREED_ONLY=9 (a section number) runs just that section: quicker while one of them is being worked on.
static bool32 SectionWanted(u32 number)
{
    const char *only = getenv("PKMN_BREED_ONLY");

    return only == NULL || (u32)atoi(only) == number;
}

static int StepsToHatch(u32 cycles, bool32 incubator)
{
    struct Pokemon egg;
    u32 isEgg = TRUE;

    ZeroPlayerPartyMons();
    CreateMon(&egg, SPECIES_PICHU, 1, 0, OTID_STRUCT_PLAYER_ID);
    SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
    SetMonData(&egg, MON_DATA_FRIENDSHIP, &cycles);
    gParties[B_TRAINER_PLAYER][0] = egg;
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    if (incubator)
    {
        struct Pokemon magmar;
        u32 personality = 0x1000;        // even: ability slot 0 (Flame Body)

        CreateMon(&magmar, SPECIES_MAGMAR, 20, personality, OTID_STRUCT_PLAYER_ID);
        gParties[B_TRAINER_PLAYER][1] = magmar;
        gPartiesCount[B_TRAINER_PLAYER] = 2;
    }
    gSaveBlock1Ptr->daycare.stepCounter = 0;
    for (int steps = 1; steps < 100000; steps++)
        if (ShouldEggHatch())
            return steps;
    return -1;
}

// As StepsToHatch, for an Egg in the PC (the party: nothing, or the Magmar with Flame Body).
static int BoxStepsToHatch(u32 cycles, bool32 incubator)
{
    struct Pokemon egg;
    u32 isEgg = TRUE;

    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    gPartiesCount[B_TRAINER_PLAYER] = 0;
    if (incubator)
    {
        struct Pokemon magmar;

        CreateMon(&magmar, SPECIES_MAGMAR, 20, 0x1000, OTID_STRUCT_PLAYER_ID);   // ability slot 0: Flame Body
        gParties[B_TRAINER_PLAYER][0] = magmar;
        gPartiesCount[B_TRAINER_PLAYER] = 1;
    }
    CreateMon(&egg, SPECIES_PICHU, 1, 0, OTID_STRUCT_PLAYER_ID);
    SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
    SetMonData(&egg, MON_DATA_FRIENDSHIP, &cycles);
    gPokemonStoragePtr->boxes[0][0] = egg.box;
    gSaveBlock1Ptr->daycare.stepCounter = 0;
    for (int steps = 1; steps < 100000; steps++)
    {
        ShouldEggHatch();
        if (!GetBoxMonData(&gPokemonStoragePtr->boxes[0][0], MON_DATA_IS_EGG))
            return steps;
    }
    return -1;
}

static u32 CountBoxMons(void)
{
    u32 count = 0;

    for (u32 box = 0; box < TOTAL_BOXES_COUNT; box++)
        for (u32 slot = 0; slot < IN_BOX_COUNT; slot++)
            count += GetBoxMonData(&gPokemonStoragePtr->boxes[box][slot], MON_DATA_SANITY_HAS_SPECIES) != 0;
    return count;
}

static bool32 NicknameIsSpeciesName(struct Pokemon *mon)
{
    u8 name[POKEMON_NAME_LENGTH + 1];

    GetMonData(mon, MON_DATA_NICKNAME, name);
    return StringCompare(name, GetSpeciesName(GetMonData(mon, MON_DATA_SPECIES))) == 0;
}

int NativeSelfTest_Breeding(void)
{
    Setup();
    printf("selftest-breeding: Update 30 breeding\n");

    // 1. compatibility (the original chances, incompatible pairs never)
    if (SectionWanted(1))
    {
        ExpectLeg("same species, different trainers: 70 %", Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_PIKACHU, MON_MALE, 2) == PARENTS_MAX_COMPATIBILITY);
        ExpectLeg("same species, same trainer: 50 %", Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_PIKACHU, MON_MALE, 1) == PARENTS_MED_COMPATIBILITY);
        ExpectLeg("different species, different trainers: 50 %", Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_RAICHU, MON_MALE, 2) == PARENTS_MED_COMPATIBILITY);
        ExpectLeg("different species, same trainer: 20 %", Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_RAICHU, MON_MALE, 1) == PARENTS_LOW_COMPATIBILITY);
        ExpectLeg("Ditto with a Pokemon of another trainer: 50 %, of the same trainer: 20 %",
                  Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_DITTO, MON_GENDERLESS, 2) == PARENTS_MED_COMPATIBILITY
                  && Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_DITTO, MON_GENDERLESS, 1) == PARENTS_LOW_COMPATIBILITY);
        ExpectLeg("two males, two females: never",
                  Score(SPECIES_PIKACHU, MON_MALE, 1, SPECIES_PIKACHU, MON_MALE, 2) == PARENTS_INCOMPATIBLE
                  && Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_PIKACHU, MON_FEMALE, 2) == PARENTS_INCOMPATIBLE);
        ExpectLeg("two Ditto: never", Score(SPECIES_DITTO, MON_GENDERLESS, 1, SPECIES_DITTO, MON_GENDERLESS, 2) == PARENTS_INCOMPATIBLE);
        ExpectLeg("a Legendary (undiscovered egg group) never, not even with Ditto",
                  Score(SPECIES_MEWTWO, MON_GENDERLESS, 1, SPECIES_DITTO, MON_GENDERLESS, 2) == PARENTS_INCOMPATIBLE
                  && Score(SPECIES_ARTICUNO, MON_GENDERLESS, 1, SPECIES_PIKACHU, MON_FEMALE, 2) == PARENTS_INCOMPATIBLE);
        ExpectLeg("a Mythical (undiscovered egg group) never", Score(SPECIES_MEW, MON_GENDERLESS, 1, SPECIES_DITTO, MON_GENDERLESS, 2) == PARENTS_INCOMPATIBLE);
        ExpectLeg("a genderless Pokemon with a non-Ditto: never", Score(SPECIES_MAGNEMITE, MON_GENDERLESS, 1, SPECIES_PIKACHU, MON_FEMALE, 2) == PARENTS_INCOMPATIBLE);
        ExpectLeg("different egg groups: never", Score(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_ONIX, MON_MALE, 2) == PARENTS_INCOMPATIBLE);
    }

    // 2. an Egg check every 64 steps and no other
    if (SectionWanted(2))
    {
        struct EggRun run;
        u32 early = 0;
        double expected, sigma, diff;

        Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
        SeedRng(0x3001);
        // from step 0 to step 63 nothing happens (the check is the 64th step): 4000 fresh deposits of 63 steps
        for (u32 i = 0; i < 4000; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 0;
            gSaveBlock1Ptr->daycare.mons[1].steps = 0;
            for (u32 s = 0; s < 63; s++)
                IncrementDaycareSteps();
            early += MPDaycare_QueueCount();
            MPDaycare_QueueClear();
            FlagClear(FLAG_PENDING_DAYCARE_EGG);
        }
        ExpectLeg("63 steps never make an Egg (the check comes on the 64th)", early == 0);
        // one check per 64 steps: 640 steps from a fresh deposit make at most 10 Eggs, never an unscheduled one
        {
            u32 total = 0, tooMany = 0;

            for (u32 i = 0; i < 2000; i++)
            {
                gSaveBlock1Ptr->daycare.mons[0].steps = 0;
                gSaveBlock1Ptr->daycare.mons[1].steps = 0;
                MPDaycare_QueueClear();
                FlagClear(FLAG_PENDING_DAYCARE_EGG);
                for (u32 s = 0; s < 640; s++)
                    IncrementDaycareSteps();
                total += MPDaycare_QueueCount();
                if (MPDaycare_QueueCount() > 5)
                    tooMany++;
            }
            // 10 checks at 70 % with a queue that holds five: the mean is 5 * (1 - P(fewer than 5 of 10)), checked as "most runs fill it"
            ExpectLeg("640 steps = ten checks: the queue never passes five", tooMany == 0 && total > 0);
        }
        RunChecks(100000, &run);
        expected = run.windows * 0.70;
        sigma = __builtin_sqrt(run.windows * 0.70 * 0.30);
        diff = (double)run.eggs - expected;
        printf("70 %% pair: %u Eggs in %u checks (expected %.0f +- %.0f)\n", run.eggs, run.windows, expected, sigma);
        ExpectLeg("(70 % pair) the Egg chance per 64-step check is 70 %", diff > -4.5 * sigma && diff < 4.5 * sigma);
        ExpectLeg("every Egg made is an Egg at level 1", run.level1 && run.allEggs);
        Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 1, FALSE);
        RunChecks(100000, &run);
        expected = run.windows * 0.50;
        sigma = __builtin_sqrt(run.windows * 0.50 * 0.50);
        diff = (double)run.eggs - expected;
        printf("50 %% pair: %u Eggs in %u checks (expected %.0f +- %.0f)\n", run.eggs, run.windows, expected, sigma);
        ExpectLeg("(50 % pair) the Egg chance per check is 50 %", diff > -4.5 * sigma && diff < 4.5 * sigma);
        Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_RAICHU, MON_MALE, 1, FALSE);
        RunChecks(100000, &run);
        expected = run.windows * 0.20;
        sigma = __builtin_sqrt(run.windows * 0.20 * 0.80);
        diff = (double)run.eggs - expected;
        printf("20 %% pair: %u Eggs in %u checks (expected %.0f +- %.0f)\n", run.eggs, run.windows, expected, sigma);
        ExpectLeg("(20 % pair) the Egg chance per check is 20 %", diff > -4.5 * sigma && diff < 4.5 * sigma);
        Deposit(0, SPECIES_PIKACHU, MON_MALE, 1, FALSE);
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
        RunChecks(20000, &run);
        ExpectLeg("an incompatible pair never makes an Egg (20,000 checks)", run.eggs == 0);
    }

    // 3. the queue: five Eggs, oldest first, nothing overwritten, nothing lost
    if (SectionWanted(3))
    {
        struct Pokemon egg, out;
        u32 species[7] = {SPECIES_PICHU, SPECIES_CHARMANDER, SPECIES_SQUIRTLE, SPECIES_BULBASAUR, SPECIES_MUDKIP, SPECIES_TREECKO, SPECIES_TORCHIC};
        bool32 order = TRUE, refused;
        u8 backup[sizeof(struct SaveBlock3)];
        u32 isEgg = TRUE;

        ClearDaycare();
        for (u32 i = 0; i < 5; i++)
        {
            CreateMon(&egg, species[i], 1, 0x1000 + i, OTID_STRUCT_PLAYER_ID);
            SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
            if (!MPDaycare_QueuePush(&egg))
                order = FALSE;
        }
        CreateMon(&egg, species[5], 1, 0x2000, OTID_STRUCT_PLAYER_ID);
        refused = !MPDaycare_QueuePush(&egg);
        ExpectLeg("five Eggs fit and a sixth is refused (never overwrites)", order && refused && MPDaycare_QueueCount() == 5);
        // a save and a reload: the whole SaveBlock3 goes through the sectors
        memcpy(backup, gSaveBlock3Ptr, sizeof(backup));
        memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
        ExpectLeg("(before the reload) a zeroed block holds no Eggs", MPDaycare_QueueCount() == 0);
        memcpy(gSaveBlock3Ptr, backup, sizeof(backup));
        ExpectLeg("after a save and reload all five Eggs are there", MPDaycare_QueueCount() == 5 && MPDaycare_QueueIsSane());
        order = TRUE;
        for (u32 i = 0; i < 5; i++)
        {
            order = order && MPDaycare_QueuePop(&out) && GetMonData(&out, MON_DATA_SPECIES) == species[i]
                 && GetMonData(&out, MON_DATA_PERSONALITY) == 0x1000 + i && GetMonData(&out, MON_DATA_IS_EGG);
        }
        ExpectLeg("they come out oldest first, each exactly once, with their own personality", order && MPDaycare_QueueCount() == 0 && !MPDaycare_QueuePop(&out));
        // turning one down drops only the oldest
        for (u32 i = 0; i < 3; i++)
        {
            CreateMon(&egg, species[i], 1, 0x3000 + i, OTID_STRUCT_PLAYER_ID);
            MPDaycare_QueuePush(&egg);
        }
        MPDaycare_QueueDropOldest();
        MPDaycare_QueuePop(&out);
        ExpectLeg("turning an Egg down drops only the oldest", GetMonData(&out, MON_DATA_PERSONALITY) == 0x3001 && MPDaycare_QueueCount() == 1);
        // corruption reads as empty, never as garbage Eggs
        CreateMon(&egg, species[0], 1, 0x4000, OTID_STRUCT_PLAYER_ID);
        MPDaycare_QueuePush(&egg);
        gSaveBlock3Ptr->eggQueue.eggs[0][3] ^= 0x10;
        ExpectLeg("a corrupted queue (CRC) reads as empty", MPDaycare_QueueCount() == 0 && !MPDaycare_QueueIsSane());
        MPDaycare_QueueClear();
        ExpectLeg("the zeros of a save from before this update read as empty", MPDaycare_QueueCount() == 0);
        // a full queue stops the Day Care making more: production waits
        ClearDaycare();
        Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
        SeedRng(0x3002);
        for (u32 i = 0; i < 400; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 63;
            gSaveBlock1Ptr->daycare.mons[1].steps = 63;
            IncrementDaycareSteps();
        }
        ExpectLeg("the Day Care stops at five waiting Eggs", MPDaycare_QueueCount() == 5 && FlagGet(FLAG_PENDING_DAYCARE_EGG));
        // one is collected: production continues
        GiveEggFromDaycare();
        ExpectLeg("collecting one Egg puts it in the party and leaves four", MPDaycare_QueueCount() == 4 && FlagGet(FLAG_PENDING_DAYCARE_EGG)
                  && GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG) == 1);
        for (u32 i = 0; i < 400; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 63;
            gSaveBlock1Ptr->daycare.mons[1].steps = 63;
            IncrementDaycareSteps();
        }
        ExpectLeg("the Day Care makes Eggs again once there is room", MPDaycare_QueueCount() == 5);
        for (u32 i = 0; i < 5; i++)
            GiveEggFromDaycare();
        ExpectLeg("the flag clears with the last Egg", MPDaycare_QueueCount() == 0 && !FlagGet(FLAG_PENDING_DAYCARE_EGG));
        // an old save: the flag set and no queue: the Egg is made from the parents when collected, as it always was
        ZeroPlayerPartyMons();
        FlagSet(FLAG_PENDING_DAYCARE_EGG);
        GiveEggFromDaycare();
        ExpectLeg("(a save from before the update) a pending flag without a queue still gives one Egg", GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG) == 1
                  && !FlagGet(FLAG_PENDING_DAYCARE_EGG));
    }

    // 4. hatching: 75 % fewer steps, Flame Body / Magma Armor halve once, level 1
    if (SectionWanted(4))
    {
        bool32 exact = TRUE, flame = TRUE;

        for (u32 cycles = 1; cycles <= 40; cycles += 3)
        {
            int plain = StepsToHatch(cycles, FALSE), halved = StepsToHatch(cycles, TRUE);
            int original = (cycles + 1) * 128;                   // the Gen 8 value: 128 steps per cycle
            int expectHalved = 32 * ((cycles + 1) / 2 + 1);      // two cycles per 32 steps while two remain, then the last one

            if (plain != original / 4)
                exact = FALSE;
            if (halved != expectHalved && halved != 32 * ((cycles + 1) / 2 + 1) + 0)
                flame = FALSE;
            if (cycles == 1 || cycles == 20 || cycles == 40)
                printf("egg of %u cycles: %d steps (was %d), with Flame Body %d\n", cycles, plain, original, halved);
        }
        ExpectLeg("an Egg of C cycles hatches in 32 x (C + 1) steps: exactly a quarter of the original 128 x (C + 1)", exact);
        ExpectLeg("with Flame Body in the party the cycles are halved (once)", flame);
        {
            int one = StepsToHatch(30, TRUE), two;
            struct Pokemon magma;

            CreateMon(&magma, SPECIES_MAGCARGO, 20, 0x1000, OTID_STRUCT_PLAYER_ID);
            two = StepsToHatch(30, TRUE);
            gParties[B_TRAINER_PLAYER][2] = magma;
            gPartiesCount[B_TRAINER_PLAYER] = 3;
            gSaveBlock1Ptr->daycare.stepCounter = 0;
            {
                struct Pokemon egg = gParties[B_TRAINER_PLAYER][0];
                u32 cycles = 30, steps = 0, isEgg = TRUE;

                SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
                SetMonData(&egg, MON_DATA_FRIENDSHIP, &cycles);
                gParties[B_TRAINER_PLAYER][0] = egg;
                for (steps = 1; steps < 100000 && !ShouldEggHatch(); steps++)
                    ;
                ExpectLeg("two Flame Body / Magma Armor Pokemon do not stack: same steps as one", (int)steps == one && one == two);
            }
        }
        {
            bool32 allLevel1 = TRUE;

            for (enum Species s = SPECIES_BULBASAUR; s <= SPECIES_CELEBI; s++)
            {
                struct Pokemon egg;
                u32 isEgg = TRUE, notEgg = FALSE;

                if (!IsSpeciesEnabled(s))
                    continue;
                CreateMonWithIVs(&egg, s, EGG_HATCH_LEVEL, 0x1234, OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
                SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
                SetMonData(&egg, MON_DATA_IS_EGG, &notEgg);
                CalculateMonStats(&egg);
                if (GetMonData(&egg, MON_DATA_LEVEL) != 1)
                    allLevel1 = FALSE;
            }
            ExpectLeg("every Egg hatches at level 1", allLevel1 && EGG_HATCH_LEVEL == 1);
        }
    }

    // 5. Eggs in the PC (the owner's request of 2026-10-27; they used to stay as they were): they spend the egg cycles of
    //    the player's steps as a party Egg does - the same steps, never fewer - all of them at once, and hatch in their
    //    own slot with everything they were made with
    if (SectionWanted(5))
    {
        static const u32 sCycles[] = {0, 1, 7, 20};
        u32 c, incubator;

        for (c = 0; c < ARRAY_COUNT(sCycles); c++)
        {
            for (incubator = 0; incubator < 2; incubator++)
            {
                int party = StepsToHatch(sCycles[c], incubator);
                int box = BoxStepsToHatch(sCycles[c], incubator);
                char what[160];

                snprintf(what, sizeof(what), "a PC Egg of %u cycles hatches on the step a party Egg does%s (%d, %d)",
                         (unsigned)sCycles[c], incubator ? ", Flame Body in the party" : "", box, party);
                ExpectLeg(what, box > 0 && box == party);
            }
        }

        {
            struct Pokemon egg, back;
            struct BoxPokemon *slot;
            u32 isEgg = TRUE, shiny = TRUE, cycles = 2, language = LANGUAGE_JAPANESE, move = MOVE_EXTRASENSORY;
            u32 personality, otId, species, ability, gender, nature, ivs[NUM_STATS], moves[MAX_MON_MOVES], i;
            bool32 same = TRUE, fullPp = TRUE;
            int steps;

            ResetPokemonStorageSystem();
            ZeroPlayerPartyMons();
            gPartiesCount[B_TRAINER_PLAYER] = 0;
            CreateMonWithIVs(&egg, SPECIES_TOGEPI, EGG_HATCH_LEVEL, 0x4B1D5EED, OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
            SetMonData(&egg, MON_DATA_IS_SHINY, &shiny);
            SetMonData(&egg, MON_DATA_MOVE4, &move);            // an Egg move
            SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
            SetMonData(&egg, MON_DATA_FRIENDSHIP, &cycles);
            SetMonData(&egg, MON_DATA_LANGUAGE, &language);     // an Egg's, as the Day Care makes it
            SetMonData(&egg, MON_DATA_NICKNAME, gText_EggNickname);
            personality = GetMonData(&egg, MON_DATA_PERSONALITY);
            otId = GetMonData(&egg, MON_DATA_OT_ID);
            species = GetMonData(&egg, MON_DATA_SPECIES);
            ability = GetMonData(&egg, MON_DATA_ABILITY_NUM);
            gender = GetMonGender(&egg);
            nature = GetNature(&egg);
            for (i = 0; i < NUM_STATS; i++)
                ivs[i] = GetMonData(&egg, MON_DATA_HP_IV + i);
            for (i = 0; i < MAX_MON_MOVES; i++)
                moves[i] = GetMonData(&egg, MON_DATA_MOVE1 + i);
            slot = &gPokemonStoragePtr->boxes[TOTAL_BOXES_COUNT - 1][IN_BOX_COUNT - 1];   // the last slot of the last box
            *slot = egg.box;
            gSaveBlock1Ptr->daycare.stepCounter = 0;
            for (steps = 1; steps < 10000 && GetBoxMonData(slot, MON_DATA_IS_EGG); steps++)
                ShouldEggHatch();
            BoxMonToMon(slot, &back);
            for (i = 0; i < NUM_STATS; i++)
                same &= GetMonData(&back, MON_DATA_HP_IV + i) == ivs[i];
            for (i = 0; i < MAX_MON_MOVES; i++)
            {
                same &= GetMonData(&back, MON_DATA_MOVE1 + i) == moves[i];
                if (moves[i] != MOVE_NONE)
                    fullPp &= GetMonData(&back, MON_DATA_PP1 + i)
                           == CalculatePPWithBonus(moves[i], GetMonData(&back, MON_DATA_PP_BONUSES), i);
            }
            ExpectLeg("the last box's last slot hatches after 3 egg cycles of steps (2 cycles, then the hatch)",
                      !GetMonData(&back, MON_DATA_IS_EGG) && steps - 1 == 3 * MP_EGG_CYCLE_STEPS);
            ExpectLeg("it keeps its personality, nature, shininess, ability, gender, IVs, moves, species and trainer",
                      same && GetMonData(&back, MON_DATA_PERSONALITY) == personality && GetNature(&back) == nature
                      && GetMonData(&back, MON_DATA_IS_SHINY) && GetMonData(&back, MON_DATA_ABILITY_NUM) == ability
                      && GetMonGender(&back) == gender && GetMonData(&back, MON_DATA_SPECIES) == species
                      && GetMonData(&back, MON_DATA_OT_ID) == otId);
            ExpectLeg("it is named as its species, in the game's language, friendship 120, met at level 0, PP full",
                      GetMonData(&back, MON_DATA_LANGUAGE) == GAME_LANGUAGE
                      && GetMonData(&back, MON_DATA_FRIENDSHIP) == 120 && GetMonData(&back, MON_DATA_MET_LEVEL) == 0
                      && fullPp && NicknameIsSpeciesName(&back));
            ExpectLeg("it stays in its own slot, nothing else in the boxes", CountBoxMons() == 1);
        }

        {
            // several at once, in different boxes; a Pokemon that is no Egg and a bad Egg are left alone
            struct Pokemon mon;
            u32 isEgg = TRUE, zero = 0, seventy = 70, bad = TRUE, hatched = 0, i;
            static const u8 sSlots[][2] = {{0, 0}, {5, 12}, {13, 29}};

            ResetPokemonStorageSystem();
            ZeroPlayerPartyMons();
            gPartiesCount[B_TRAINER_PLAYER] = 0;
            for (i = 0; i < ARRAY_COUNT(sSlots); i++)
            {
                CreateMon(&mon, SPECIES_PICHU + i, EGG_HATCH_LEVEL, 0x100 + i, OTID_STRUCT_PLAYER_ID);
                SetMonData(&mon, MON_DATA_IS_EGG, &isEgg);
                SetMonData(&mon, MON_DATA_FRIENDSHIP, &zero);
                gPokemonStoragePtr->boxes[sSlots[i][0]][sSlots[i][1]] = mon.box;
            }
            CreateMon(&mon, SPECIES_ZIGZAGOON, 10, 0x200, OTID_STRUCT_PLAYER_ID);
            SetMonData(&mon, MON_DATA_FRIENDSHIP, &seventy);
            gPokemonStoragePtr->boxes[2][3] = mon.box;
            CreateMon(&mon, SPECIES_TOGEPI, EGG_HATCH_LEVEL, 0x300, OTID_STRUCT_PLAYER_ID);
            SetMonData(&mon, MON_DATA_IS_EGG, &isEgg);
            SetMonData(&mon, MON_DATA_FRIENDSHIP, &zero);
            SetMonData(&mon, MON_DATA_SANITY_IS_BAD_EGG, &bad);
            gPokemonStoragePtr->boxes[2][4] = mon.box;
            gSaveBlock1Ptr->daycare.stepCounter = 0;
            for (i = 0; i < MP_EGG_CYCLE_STEPS; i++)
                ShouldEggHatch();
            for (i = 0; i < ARRAY_COUNT(sSlots); i++)
                hatched += !GetBoxMonData(&gPokemonStoragePtr->boxes[sSlots[i][0]][sSlots[i][1]], MON_DATA_IS_EGG)
                        && GetBoxMonData(&gPokemonStoragePtr->boxes[sSlots[i][0]][sSlots[i][1]], MON_DATA_SPECIES)
                           == SPECIES_PICHU + i;
            ExpectLeg("three Eggs in three boxes hatch at the same cycle's end", hatched == 3);
            ExpectLeg("a Pokemon that is no Egg keeps its friendship",
                      GetBoxMonData(&gPokemonStoragePtr->boxes[2][3], MON_DATA_FRIENDSHIP) == 70);
            ExpectLeg("a bad Egg is left alone",
                      GetBoxMonData(&gPokemonStoragePtr->boxes[2][4], MON_DATA_SANITY_IS_BAD_EGG));
            ExpectLeg("the PC holds the same five Pokemon", CountBoxMons() == 5);
            (void)bad;
        }
    }

    // 6. Egg shiny rates through the whole Day Care (parents, Charm, Egg shiny rolls)
    if (SectionWanted(6))
    {
        static const struct { const char *name; bool32 parentShiny, charm; double odds; } sCases[] =
        {
            {"neither parent shiny, no Charm", FALSE, FALSE, 1.0 / 4096},
            {"a shiny parent, no Charm", TRUE, FALSE, 0},
            {"neither parent shiny, with the Charm", FALSE, TRUE, 0},
            {"a shiny parent and the Charm", TRUE, TRUE, 0},
        };
        const double p = 16.0 / 65536.0;
        const u32 rolls[4] = {1, 3, 3, 6};
        struct EggRun run;

        ExpectLeg("the Egg shiny rolls are 1 / 3 / 3 / 6 (two shiny parents give no more than one)",
                  MPBreeding_EggShinyRolls(FALSE, FALSE) == 1 && MPBreeding_EggShinyRolls(TRUE, FALSE) == 3
                  && MPBreeding_EggShinyRolls(FALSE, TRUE) == 3 && MPBreeding_EggShinyRolls(TRUE, TRUE) == 6);
        for (u32 c = 0; c < ARRAY_COUNT(sCases); c++)
        {
            double odds = 1.0 - __builtin_pow(1.0 - p, rolls[c]), expected, sigma, diff;
            char what[200];
            u32 windows = c == 0 ? 1200000 : c == 3 ? 400000 : 650000;

            Setup();
            if (sCases[c].charm)
                AddBagItem(ITEM_SHINY_CHARM, 1);
            Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, sCases[c].parentShiny);
            Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
            SeedRng(0x3010 + c);
            RunChecks(windows, &run);
            expected = run.eggs * odds;
            sigma = __builtin_sqrt(expected * (1.0 - odds));
            diff = (double)run.shiny - expected;
            printf("%s: %u shiny of %u Eggs (expected %.0f +- %.0f), 1 in %.1f, documented 1 in %.1f\n", sCases[c].name, run.shiny, run.eggs, expected, sigma,
                   run.shiny ? (double)run.eggs / run.shiny : 0.0, 1.0 / odds);
            snprintf(what, sizeof(what), "Egg shiny rate, %s: 1 in %.1f", sCases[c].name, 1.0 / odds);
            ExpectLeg(what, diff > -4.5 * sigma && diff < 4.5 * sigma);
        }
        // two shiny parents are no better than one
        {
            double odds = 1.0 - __builtin_pow(1.0 - p, 3), expected, sigma, diff;

            Setup();
            Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, TRUE);
            Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, TRUE);
            SeedRng(0x3020);
            RunChecks(650000, &run);
            expected = run.eggs * odds;
            sigma = __builtin_sqrt(expected * (1.0 - odds));
            diff = (double)run.shiny - expected;
            ExpectLeg("two shiny parents give the same 1 in 1365.7 as one", diff > -4.5 * sigma && diff < 4.5 * sigma);
        }
        // the Charm belongs to the player whose Day Care it is: an empty bag gets no Charm bonus whoever else has one
        {
            struct Pokemon egg;
            u32 shiny = 0, n = 300000;
            double odds = 1.0 - __builtin_pow(1.0 - p, 1), expected = n * odds, sigma = __builtin_sqrt(expected), diff;

            Setup();
            ClearBag();
            SeedRng(0x3030);
            gMPEggShinyRolls = MPBreeding_EggShinyRolls(FALSE, CheckBagHasItem(ITEM_SHINY_CHARM, 1));
            for (u32 i = 0; i < n * 4; i++)
            {
                CreateMonWithIVs(&egg, SPECIES_PICHU, 1, Random32(), OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
                shiny += GetMonData(&egg, MON_DATA_IS_SHINY);
            }
            gMPEggShinyRolls = 0;
            expected = n * 4 * odds;
            sigma = __builtin_sqrt(expected);
            diff = (double)shiny - expected;
            ExpectLeg("an Egg of a player without the Charm gets no Charm bonus", diff > -4.5 * sigma && diff < 4.5 * sigma);
            (void)n;
        }
        // shiny status is a property of the Egg's data: it survives hatching, the PC and a save/load
        {
            struct Pokemon egg, back;
            u32 shiny = TRUE, isEgg = TRUE, notEgg = FALSE;
            u8 saved[sizeof(struct BoxPokemon)];

            CreateMonWithIVs(&egg, SPECIES_PICHU, 1, 0x7777, OTID_STRUCT_PLAYER_ID, USE_RANDOM_IVS);
            SetMonData(&egg, MON_DATA_IS_SHINY, &shiny);
            SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
            CopyMonToPC(&egg);
            memcpy(saved, &gPokemonStoragePtr->boxes[0][0], sizeof(saved));
            memset(&gPokemonStoragePtr->boxes[0][0], 0, sizeof(saved));
            memcpy(&gPokemonStoragePtr->boxes[0][0], saved, sizeof(saved));
            BoxMonToMon(&gPokemonStoragePtr->boxes[0][0], &back);
            ExpectLeg("a shiny Egg stays shiny through the PC and a save", GetMonData(&back, MON_DATA_IS_SHINY) == 1 && GetMonData(&back, MON_DATA_IS_EGG) == 1);
            SetMonData(&back, MON_DATA_IS_EGG, &notEgg);
            ExpectLeg("and when it hatches", GetMonData(&back, MON_DATA_IS_SHINY) == 1 && GetMonData(&back, MON_DATA_PERSONALITY) == 0x7777);
        }
    }

    // 7. the whole implemented roster: every species that can breed makes a valid level-1 Egg of its own line with a Ditto
    if (SectionWanted(7))
    {
        u32 swept = 0, noEgg = 0, bad = 0, exceptions = 0, ivBad = 0, moveBad = 0, shown = 0;

        Setup();
        SeedRng(0x3040);
        for (enum Species sp = 1; sp < NUM_SPECIES; sp++)
        {
            u32 gender, tries;
            bool32 got = FALSE;
            enum Species eggSpecies = SPECIES_NONE;
            struct Pokemon egg;

            if (!IsSpeciesEnabled(sp) || gSpeciesInfo[sp].eggGroups[0] == EGG_GROUP_NO_EGGS_DISCOVERED || gSpeciesInfo[sp].eggGroups[0] == EGG_GROUP_DITTO
             || gSpeciesInfo[sp].eggGroups[0] == EGG_GROUP_NONE)
            {
                noEgg++;
                continue;
            }
            gender = gSpeciesInfo[sp].genderRatio == MON_FEMALE ? MON_FEMALE : gSpeciesInfo[sp].genderRatio == MON_GENDERLESS ? MON_GENDERLESS
                   : gSpeciesInfo[sp].genderRatio == MON_MALE ? MON_MALE : MON_FEMALE;
            ClearDaycare();
            Deposit(0, sp, gender, 1, FALSE);
            Deposit(1, SPECIES_DITTO, MON_GENDERLESS, 2, FALSE);
            swept++;
            for (tries = 0; tries < 400 && !got; tries++)
            {
                gSaveBlock1Ptr->daycare.mons[0].steps = 63;
                gSaveBlock1Ptr->daycare.mons[1].steps = 63;
                IncrementDaycareSteps();
                if (MPDaycare_QueuePop(&egg))
                {
                    got = TRUE;
                    eggSpecies = GetMonData(&egg, MON_DATA_SPECIES);
                }
            }
            if (!got)
            {
                bad++;
                if (shown++ < 10)
                    printf("  no Egg from species %u after 400 checks\n", sp);
                continue;
            }
            if (!GetMonData(&egg, MON_DATA_IS_EGG) || GetMonData(&egg, MON_DATA_LEVEL) != 1 || !IsSpeciesEnabled(eggSpecies) || GetMonData(&egg, MON_DATA_MOVE1) == MOVE_NONE)
                moveBad++;
            for (u32 iv = MON_DATA_HP_IV; iv <= MON_DATA_SPDEF_IV; iv++)
                if (GetMonData(&egg, iv) > 31)
                    ivBad++;
            if (GetEggSpecies(sp) != eggSpecies && GET_BASE_SPECIES_ID(GetEggSpecies(sp)) != GET_BASE_SPECIES_ID(eggSpecies))
            {
                // the known cases: the sex-flipped Nidoran / Illumise lines, the baby species that need an incense item
                // (Wynaut, Azurill, Happiny, ... hatch as their parent's own species without it), and Manaphy's Phione
                static const u16 sKnown[] = {29, 30, 31, 32, 33, 34, 113, 122, 143, 183, 184, 185, 202, 226, 242, 313, 314, 315, 358, 407, 490, 866, 981};
                bool32 known = FALSE;

                for (u32 k = 0; k < ARRAY_COUNT(sKnown); k++)
                    known = known || sKnown[k] == sp;
                if (!known)
                {
                    exceptions++;
                    printf("  unexpected: species %u -> Egg species %u\n", sp, eggSpecies);
                }
            }
        }
        printf("roster sweep: %u breedable species with a Ditto, %u without Eggs (undiscovered group, Ditto, no group or disabled), %u produced no Egg, %u unusual baby species\n",
               swept, noEgg, bad, exceptions);
        ExpectLeg("every breedable species of the implemented roster makes an Egg with a Ditto", swept > 500 && bad == 0);
        ExpectLeg("every such Egg is a level-1 Egg of an enabled species with a first move", moveBad == 0);
        ExpectLeg("every inherited IV is in 0..31", ivBad == 0);
        ExpectLeg("the Egg's species is its parent's own line (only the sex-flipped Nidoran lines, the incense babies and Manaphy differ)", exceptions == 0);
    }

    // 8. inheritance: the baby species, three IVs from the parents, the mother's ability
    if (SectionWanted(8))
    {
        u32 pichu = 0, total = 20000, ivSum = 0, perfect = 0, abil0 = 0, eggs = 0, nidoM = 0, nidoF = 0, phione = 0;
        struct Pokemon egg;

        Setup();
        SeedRng(0x3050);
        Deposit(0, SPECIES_RAICHU, MON_FEMALE, 1, FALSE);     // a mother whose line starts at Pichu; IVs 31 from Deposit
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
        for (u32 i = 0; i < total; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 63;
            gSaveBlock1Ptr->daycare.mons[1].steps = 63;
            IncrementDaycareSteps();
            if (MPDaycare_QueuePop(&egg))
            {
                u32 n31 = 0;

                eggs++;
                pichu += GetMonData(&egg, MON_DATA_SPECIES) == SPECIES_PICHU;
                for (u32 iv = MON_DATA_HP_IV; iv <= MON_DATA_SPDEF_IV; iv++)
                    n31 += GetMonData(&egg, iv) == 31;
                ivSum += n31;
                perfect += n31 >= 3;
            }
        }
        printf("Raichu x Pikachu: %u Eggs, %u Pichu; perfect IVs per Egg %.2f (three inherited from 31-IV parents, plus chance)\n", eggs, pichu, eggs ? (double)ivSum / eggs : 0.0);
        ExpectLeg("the baby of a Raichu is a Pichu (the egg line starts at the first stage)", eggs > 0 && pichu == eggs);
        ExpectLeg("at least three IVs come from the parents (all-31 parents give at least three 31s in every Egg)", eggs > 0 && perfect == eggs);
        ExpectLeg("and the average stays near 3 + 3/32 (no hidden extra inheritance)", eggs > 0 && (double)ivSum / eggs > 3.0 && (double)ivSum / eggs < 3.25);
        // Nidoran: a male or female baby depending on the Egg's own gender; Manaphy always Phione
        ClearDaycare();
        Deposit(0, SPECIES_NIDORAN_F, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_NIDORAN_M, MON_MALE, 2, FALSE);
        for (u32 i = 0; i < 4000; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 63;
            gSaveBlock1Ptr->daycare.mons[1].steps = 63;
            IncrementDaycareSteps();
            if (MPDaycare_QueuePop(&egg))
            {
                enum Species sp = GetMonData(&egg, MON_DATA_SPECIES);

                nidoM += sp == SPECIES_NIDORAN_M;
                nidoF += sp == SPECIES_NIDORAN_F;
            }
        }
        ExpectLeg("Nidoran Eggs hatch as either sex (about half each)", nidoM > 0 && nidoF > 0 && nidoM > nidoF / 2 && nidoF > nidoM / 2);
        ClearDaycare();
        Deposit(0, SPECIES_MANAPHY, MON_GENDERLESS, 1, FALSE);
        Deposit(1, SPECIES_DITTO, MON_GENDERLESS, 2, FALSE);
        for (u32 i = 0; i < 2000; i++)
        {
            gSaveBlock1Ptr->daycare.mons[0].steps = 63;
            gSaveBlock1Ptr->daycare.mons[1].steps = 63;
            IncrementDaycareSteps();
            if (MPDaycare_QueuePop(&egg))
                phione += GetMonData(&egg, MON_DATA_SPECIES) == SPECIES_PHIONE;
        }
        ExpectLeg("Manaphy with a Ditto: the Egg is a Phione (Manaphy itself is also barred as a plain pair)", phione > 0);
        (void)abil0; (void)total;
    }


    // 9. the modern rules of the expansion's breeding (Gen 6 and later), on the real Day Care code
    if (SectionWanted(9))
    {
        static const u8 all31[6] = {31, 31, 31, 31, 31, 31};
        static const u8 hp7[6] = {7, 31, 31, 31, 31, 31};
        static const u8 zero[6] = {0, 0, 0, 0, 0, 0};
        struct Pokemon egg;
        u32 eggs, adamant, modest, knot5, plainBelow3, hpWeight, ha, haEggs, moveHeld, nest, differentSpeciesBall;
        const u16 *eggMoves = GetSpeciesEggMoves(SPECIES_BULBASAUR);

        Setup();
        SeedRng(0x4090);
        // Everstone: the holder passes its Nature on, always
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_EVERSTONE, all31, 0, BALL_POKE, MOVE_NONE);
        eggs = adamant = 0;
        for (u32 i = 0; i < 3000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                adamant += GetNature(&egg) == NATURE_ADAMANT;
            }
        ExpectLeg("an Everstone on the father: every Egg has his Nature", eggs > 500 && adamant == eggs);
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_EVERSTONE, all31, 0, BALL_POKE, MOVE_NONE);
        eggs = adamant = modest = 0;
        for (u32 i = 0; i < 6000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                adamant += GetNature(&egg) == NATURE_ADAMANT;
                modest += GetNature(&egg) == NATURE_MODEST;
            }
        ExpectLeg("an Everstone on both: either parent's Nature, about half each, nothing else", eggs > 1000 && adamant + modest == eggs
                  && adamant > eggs * 4 / 10 && adamant < eggs * 6 / 10);
        // Destiny Knot: five IVs from the parents instead of three
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_DESTINY_KNOT, all31, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        eggs = knot5 = 0;
        for (u32 i = 0; i < 3000; i++)
            if (NextEgg(&egg))
            {
                u32 n31 = 0;

                eggs++;
                for (u32 iv = MON_DATA_HP_IV; iv <= MON_DATA_SPDEF_IV; iv++)
                    n31 += GetMonData(&egg, iv) == 31;
                knot5 += n31 >= 5;
            }
        ExpectLeg("a Destiny Knot: at least five inherited IVs in every Egg", eggs > 500 && knot5 == eggs);
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, zero, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_NONE, zero, 0, BALL_POKE, MOVE_NONE);
        eggs = plainBelow3 = 0;
        for (u32 i = 0; i < 3000; i++)
            if (NextEgg(&egg))
            {
                u32 n0 = 0;

                eggs++;
                for (u32 iv = MON_DATA_HP_IV; iv <= MON_DATA_SPDEF_IV; iv++)
                    n0 += GetMonData(&egg, iv) == 0;
                plainBelow3 += n0 < 3;
            }
        ExpectLeg("without a Knot only three IVs are inherited (all-0 parents: at least three 0s, never more than the chance gives)", eggs > 500 && plainBelow3 == 0);
        // A Power item hands over its stat from that parent
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_POWER_WEIGHT, hp7, 0, BALL_POKE, MOVE_NONE);
        eggs = hpWeight = 0;
        for (u32 i = 0; i < 3000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                hpWeight += GetMonData(&egg, MON_DATA_HP_IV) == 7;
            }
        ExpectLeg("a Power Weight: the Egg always has its holder's HP IV", eggs > 500 && hpWeight == eggs);
        // The mother's Hidden Ability is passed on 60% of the time (regular abilities 80%), with a Ditto too
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 2, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_DITTO, MON_GENDERLESS, 2, NATURE_ADAMANT, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        eggs = ha = haEggs = 0;
        for (u32 i = 0; i < 30000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                ha += GetMonData(&egg, MON_DATA_ABILITY_NUM) == 2;
            }
        haEggs = eggs;
        printf("Bulbasaur mother with her Hidden Ability + Ditto: %u of %u Eggs inherit it (%.1f %%; the rule is 60 %%)\n", ha, haEggs, haEggs ? 100.0 * ha / haEggs : 0.0);
        ExpectLeg("a Hidden Ability mother passes it on about 60% of the time", haEggs > 3000 && ha * 100 > haEggs * 56 && ha * 100 < haEggs * 64);
        // Egg moves from a parent that knows one (the species' own list)
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_NONE, all31, 0, BALL_POKE, eggMoves != NULL ? eggMoves[0] : MOVE_NONE);
        eggs = moveHeld = 0;
        for (u32 i = 0; i < 2000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                for (u32 m = 0; m < MAX_MON_MOVES; m++)
                    if (eggMoves != NULL && GetMonData(&egg, MON_DATA_MOVE1 + m) == eggMoves[0])
                    {
                        moveHeld++;
                        break;
                    }
            }
        ExpectLeg("an Egg move known by a parent is inherited", eggMoves != NULL && eggMoves[0] != MOVE_UNAVAILABLE && eggs > 300 && moveHeld == eggs);
        // Update 32: moves both parents know that the baby learns by level (Gen 6 rule). Two level-30 Bulbasaur know
        // the same four level-up moves (Razor Leaf, Sweet Scent, Growth, Double-Edge), none of them the learnset's
        // first: every Egg knows all four (the upstream loop found only a learnset's first move).
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_NONE, all31, 0, BALL_POKE, MOVE_NONE);
        GiveBoxMonInitialMoveset(&gSaveBlock1Ptr->daycare.mons[0].mon);     // CreateMon leaves the moves empty
        GiveBoxMonInitialMoveset(&gSaveBlock1Ptr->daycare.mons[1].mon);
        {
            enum Move shared[MAX_MON_MOVES];
            u32 sharedEggs = 0, allShared = 0;
            for (u32 m = 0; m < MAX_MON_MOVES; m++)
                shared[m] = GetBoxMonData(&gSaveBlock1Ptr->daycare.mons[0].mon, MON_DATA_MOVE1 + m);
            for (u32 i = 0; i < 500; i++)
                if (NextEgg(&egg))
                {
                    u32 known = 0;
                    sharedEggs++;
                    for (u32 m = 0; m < MAX_MON_MOVES; m++)
                        for (u32 k = 0; k < MAX_MON_MOVES; k++)
                            known += shared[m] != MOVE_NONE && GetMonData(&egg, MON_DATA_MOVE1 + k) == shared[m];
                    allShared += known == MAX_MON_MOVES;
                }
            printf("two level-30 Bulbasaur (moves %u %u %u %u): %u of %u Eggs know all four\n",
                   shared[0], shared[1], shared[2], shared[3], allShared, sharedEggs);
            ExpectLeg("level-up moves both parents know are inherited, not only the learnset's first",
                      sharedEggs > 50 && shared[0] != MOVE_TACKLE && shared[3] != MOVE_NONE && allShared == sharedEggs);
        }
        // The mother's Ball (Gen 6 rule); another species' father adds nothing
        ClearDaycare();
        DepositEx(0, SPECIES_BULBASAUR, MON_FEMALE, 1, NATURE_MODEST, ITEM_NONE, all31, 0, BALL_NEST, MOVE_NONE);
        DepositEx(1, SPECIES_BULBASAUR, MON_MALE, 2, NATURE_ADAMANT, ITEM_NONE, all31, 0, BALL_ULTRA, MOVE_NONE);
        eggs = nest = 0;
        for (u32 i = 0; i < 2000; i++)
            if (NextEgg(&egg))
            {
                eggs++;
                nest += GetMonData(&egg, MON_DATA_POKEBALL) == BALL_NEST;
            }
        differentSpeciesBall = nest;
        ExpectLeg("the Egg is in the mother's Poke Ball", eggs > 300 && differentSpeciesBall == eggs);
    }

    // 10. the Day Care's Eggs handed over without the Day Care man (the owner's request of 2026-10-29): to the party,
    // else to the PC, oldest first; with both full one waits with him
    if (SectionWanted(10))
    {
        static const u16 eggSpecies[3] = {SPECIES_PICHU, SPECIES_AZURILL, SPECIES_WYNAUT};
        struct Pokemon egg, mon, made[3];
        u32 toParty = 0, toPc = 0, box = 99, isEgg = TRUE, cycles = 7, same = TRUE;
        struct BoxPokemon *boxed;

        Setup();
        ClearDaycare();
        for (u32 i = 0; i < 4; i++)
        {
            CreateMon(&mon, SPECIES_ZIGZAGOON, 5, 0x100 + i, OTID_STRUCT_PLAYER_ID);
            gParties[B_TRAINER_PLAYER][i] = mon;
        }
        CalculatePlayerPartyCount();
        for (u32 i = 0; i < 3; i++)
        {
            CreateMon(&made[i], eggSpecies[i], 1, 0x5000 + i, OTID_STRUCT_PLAYER_ID);
            SetMonData(&made[i], MON_DATA_IS_EGG, &isEgg);
            SetMonData(&made[i], MON_DATA_FRIENDSHIP, &cycles);
            MPDaycare_QueuePush(&made[i]);
        }
        FlagSet(FLAG_PENDING_DAYCARE_EGG);
        MPDaycare_DeliverWaitingEggs(&toParty, &toPc, &box);
        boxed = GetBoxedMonPtr(0, 0);
        ExpectLeg("three Eggs waiting, two party slots free: two join the party oldest first, the third goes to the PC",
                  toParty == 2 && toPc == 1 && box == 0 && gPartiesCount[B_TRAINER_PLAYER] == PARTY_SIZE
                  && GetMonData(&gParties[B_TRAINER_PLAYER][4], MON_DATA_PERSONALITY) == 0x5000
                  && GetMonData(&gParties[B_TRAINER_PLAYER][5], MON_DATA_PERSONALITY) == 0x5001
                  && GetBoxMonData(boxed, MON_DATA_PERSONALITY) == 0x5002 && GetBoxMonData(boxed, MON_DATA_IS_EGG));
        ExpectLeg("then none waits and the Day Care man has none to offer",
                  MPDaycare_QueueCount() == 0 && !FlagGet(FLAG_PENDING_DAYCARE_EGG) && GetDaycareState() != DAYCARE_EGG_WAITING);
        // nothing of an Egg changes on the way: species, personality, trainer, cycles, shininess, moves
        for (u32 i = 0; i < 3; i++)
        {
            struct Pokemon got;

            if (i < 2)
                got = gParties[B_TRAINER_PLAYER][4 + i];
            else
                BoxMonToMon(boxed, &got);
            for (u32 field = 0; field < 7; field++)
            {
                static const u32 fields[7] = {MON_DATA_SPECIES, MON_DATA_PERSONALITY, MON_DATA_OT_ID, MON_DATA_FRIENDSHIP,
                                              MON_DATA_IS_SHINY, MON_DATA_MOVE1, MON_DATA_IS_EGG};
                same = same && GetMonData(&got, fields[field]) == GetMonData(&made[i], fields[field]);
            }
        }
        ExpectLeg("each arrives as it was made (species, personality, trainer, cycles, shininess, moves)", same);
        // the party and the PC full: it waits, and the Day Care man still offers it
        for (u32 b = 0; b < TOTAL_BOXES_COUNT; b++)
            for (u32 slot = 0; slot < IN_BOX_COUNT; slot++)
                if (!GetBoxMonDataAt(b, slot, MON_DATA_SANITY_HAS_SPECIES))
                    SetBoxMonAt(b, slot, &mon.box);
        CreateMon(&egg, SPECIES_PICHU, 1, 0x6000, OTID_STRUCT_PLAYER_ID);
        SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
        MPDaycare_QueuePush(&egg);
        FlagSet(FLAG_PENDING_DAYCARE_EGG);
        toParty = toPc = 0;
        MPDaycare_DeliverWaitingEggs(&toParty, &toPc, &box);
        ExpectLeg("with the party and the PC full the Egg waits; the Day Care man offers it as before",
                  toParty == 0 && toPc == 0 && MPDaycare_QueueCount() == 1 && FlagGet(FLAG_PENDING_DAYCARE_EGG)
                  && GetDaycareState() == DAYCARE_EGG_WAITING);
        // a PC slot freed: it goes there
        ZeroBoxMonAt(5, 7);
        MPDaycare_DeliverWaitingEggs(&toParty, &toPc, &box);
        ExpectLeg("a PC slot freed takes it", toPc == 1 && box == 5 && GetBoxMonDataAt(5, 7, MON_DATA_PERSONALITY) == 0x6000
                  && GetBoxMonDataAt(5, 7, MON_DATA_IS_EGG) && MPDaycare_QueueCount() == 0 && !FlagGet(FLAG_PENDING_DAYCARE_EGG));
        // a save from before Update 30: the flag set and no queue: the Egg is made at the next step and handed over
        Setup();
        ClearDaycare();
        Deposit(0, SPECIES_PIKACHU, MON_FEMALE, 1, FALSE);
        Deposit(1, SPECIES_PIKACHU, MON_MALE, 2, FALSE);
        CreateMon(&mon, SPECIES_ZIGZAGOON, 5, 0x100, OTID_STRUCT_PLAYER_ID);
        gParties[B_TRAINER_PLAYER][0] = mon;
        CalculatePlayerPartyCount();
        FlagSet(FLAG_PENDING_DAYCARE_EGG);
        IncrementDaycareSteps();       // the first step since the deposit: no Egg check of its own
        ExpectLeg("(a save from before Update 30) its pending Egg is made at the next step", MPDaycare_QueueCount() == 1
                  && FlagGet(FLAG_PENDING_DAYCARE_EGG));
        toParty = toPc = 0;
        MPDaycare_DeliverWaitingEggs(&toParty, &toPc, &box);
        ExpectLeg("... and handed over to the party", toParty == 1 && GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_IS_EGG)
                  && GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_SPECIES) == SPECIES_PICHU && !FlagGet(FLAG_PENDING_DAYCARE_EGG));
    }

    printf("selftest-breeding: %d checks, %d failed\n", sLegChecks, sLegFailures);
    return sLegFailures ? 1 : 0;
}
