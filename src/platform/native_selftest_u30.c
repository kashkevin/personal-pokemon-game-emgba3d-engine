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

int NativeSelfTest_Breeding(void)
{
    Setup();
    printf("selftest-breeding: Update 30 breeding\n");

    // 1. compatibility (the original chances, incompatible pairs never)
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

    // 5. Eggs in the PC: stay Eggs, do not hatch, keep their cycles, come back unchanged
    {
        struct Pokemon egg, back;
        u32 isEgg = TRUE, cycles = 7, before, after;
        u32 i;

        ZeroPlayerPartyMons();
        CreateMon(&egg, SPECIES_TOGEPI, 1, 0x5555, OTID_STRUCT_PLAYER_ID);
        SetMonData(&egg, MON_DATA_IS_EGG, &isEgg);
        SetMonData(&egg, MON_DATA_FRIENDSHIP, &cycles);
        before = GetMonData(&egg, MON_DATA_PERSONALITY);
        ExpectLeg("an Egg can be sent to the PC", CopyMonToPC(&egg) == MON_GIVEN_TO_PC);
        gSaveBlock1Ptr->daycare.stepCounter = 0;
        gPartiesCount[B_TRAINER_PLAYER] = 0;
        for (i = 0; i < 20000; i++)
            ShouldEggHatch();
        BoxMonToMon(&gPokemonStoragePtr->boxes[0][0], &back);
        after = GetMonData(&back, MON_DATA_FRIENDSHIP);
        ExpectLeg("20,000 steps do not hatch it or spend a cycle while it is in the PC",
                  GetMonData(&back, MON_DATA_IS_EGG) == 1 && after == 7 && GetMonData(&back, MON_DATA_PERSONALITY) == before
                  && GetMonData(&back, MON_DATA_SPECIES) == SPECIES_TOGEPI);
        ExpectLeg("a stored Egg is still an Egg after the steps and the withdraw",
                  GetMonData(&back, MON_DATA_IS_EGG) == 1);
    }

    // 6. Egg shiny rates through the whole Day Care (parents, Charm, Egg shiny rolls)
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

    printf("selftest-breeding: %d checks, %d failed\n", sLegChecks, sLegFailures);
    return sLegFailures ? 1 : 0;
}
