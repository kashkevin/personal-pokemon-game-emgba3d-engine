/* A Pokemon received in a world trade evolves as after a link trade: see include/platform/mp_trade_evolution.h
 * (KNOWN_ISSUES MS-151). Project-owned. */
#include "global.h"
#ifdef PORTABLE
#include <stdio.h>
#include <string.h>
#include "evolution_scene.h"
#include "field_weather.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "task.h"
#include "constants/items.h"
#include "platform/mp_trade_evolution.h"

static struct Pokemon sBefore[PARTY_SIZE];   // the party before the trade's save was imported
static bool8 sNoted;
static struct Pokemon sSent;                 // the Pokemon given away: the partner Karrablast and Shelmet look for
static u8 sSlot = PARTY_SIZE;                // the received Pokemon's slot while its evolution waits (PARTY_SIZE: none)
static u32 sPersonality, sOtId;              // ... and who it is: the slot is looked at again before it evolves
static u32 sTarget;                          // ... and what it evolves into
static char sLastNote[96];

static bool32 SameMon(struct Pokemon *a, struct Pokemon *b)
{
    return GetMonData(a, MON_DATA_SPECIES) == GetMonData(b, MON_DATA_SPECIES)
        && GetMonData(a, MON_DATA_PERSONALITY) == GetMonData(b, MON_DATA_PERSONALITY)
        && GetMonData(a, MON_DATA_OT_ID) == GetMonData(b, MON_DATA_OT_ID);
}

static void Note(const char *text)
{
    snprintf(sLastNote, sizeof(sLastNote), "%s", text);
    fprintf(stderr, "trade evolution: %s\n", text);
}

void MPTradeEvolution_NoteBeforeImport(void)
{
    memcpy(sBefore, gParties[B_TRAINER_PLAYER], sizeof(sBefore));
    sNoted = TRUE;
    sSlot = PARTY_SIZE;
}

void MPTradeEvolution_AfterTradeLoaded(void)
{
    u32 i, slot = PARTY_SIZE, changed = 0;
    struct Pokemon *received;
    enum Species target;
    char text[96];

    if (!sNoted)
        return;
    sNoted = FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (!SameMon(&gParties[B_TRAINER_PLAYER][i], &sBefore[i]))
        {
            slot = i;
            changed++;
        }
    }
    // The exact exchange: one slot holds another Pokemon, a Pokemon there before and after
    if (changed != 1 || GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_SPECIES) == SPECIES_NONE
     || GetMonData(&sBefore[slot], MON_DATA_SPECIES) == SPECIES_NONE)
    {
        snprintf(text, sizeof(text), "no single Pokemon received (%u slots changed)", (unsigned)changed);
        Note(text);
        return;
    }
    received = &gParties[B_TRAINER_PLAYER][slot];
    sSent = sBefore[slot];
    target = GetEvolutionTargetSpecies(received, EVO_MODE_TRADE, ITEM_NONE, &sSent, NULL, CHECK_EVO);
    snprintf(text, sizeof(text), "slot %u received species %u for %u: %s %u", (unsigned)slot,
             (unsigned)GetMonData(received, MON_DATA_SPECIES), (unsigned)GetMonData(&sSent, MON_DATA_SPECIES),
             target != SPECIES_NONE ? "evolves into" : "does not evolve", (unsigned)target);
    Note(text);
    if (target == SPECIES_NONE)
        return;
    sSlot = slot;
    sTarget = target;
    sPersonality = GetMonData(received, MON_DATA_PERSONALITY);
    sOtId = GetMonData(received, MON_DATA_OT_ID);
}

// The field controls stay locked from the fade to the field's return (no script releases them here)
static void CB2_ReturnToFieldAfterTradeEvolution(void)
{
    UnlockPlayerFieldControls();
    SetMainCallback2(CB2_ReturnToField);
}

#define tSlot data[0]
#define tTarget data[1]

static void Task_TradeEvolution(u8 taskId)
{
    struct Pokemon *mon;
    enum Species target;
    u32 slot;

    if (gPaletteFade.active)
        return;
    slot = gTasks[taskId].tSlot;
    target = gTasks[taskId].tTarget;
    mon = &gParties[B_TRAINER_PLAYER][slot];
    DestroyTask(taskId);
    CleanupOverworldWindowsAndTilemaps();
    // DO_EVO uses up the held item the evolution needed (a Metal Coat, a Dragon Scale, ...), as a link trade does
    GetEvolutionTargetSpecies(mon, EVO_MODE_TRADE, ITEM_NONE, &sSent, NULL, DO_EVO);
    gCB2_AfterEvolution = CB2_ReturnToFieldAfterTradeEvolution;
    EvolutionScene(mon, target, FALSE, slot);
}

void MPTradeEvolution_Tick(bool32 fieldReady)
{
    struct Pokemon *mon;
    enum Species target;
    u8 taskId;
    char text[96];

    if (sSlot >= PARTY_SIZE || !fieldReady)
        return;
    mon = &gParties[B_TRAINER_PLAYER][sSlot];
    target = SPECIES_NONE;
    if (GetMonData(mon, MON_DATA_PERSONALITY) == sPersonality && GetMonData(mon, MON_DATA_OT_ID) == sOtId)
        target = GetEvolutionTargetSpecies(mon, EVO_MODE_TRADE, ITEM_NONE, &sSent, NULL, CHECK_EVO);
    if (target == SPECIES_NONE || (taskId = CreateTask(Task_TradeEvolution, 10)) == TASK_NONE)
    {
        snprintf(text, sizeof(text), "slot %u: no longer evolves", (unsigned)sSlot);
        Note(text);
        sSlot = PARTY_SIZE;
        return;
    }
    snprintf(text, sizeof(text), "slot %u: the evolution into %u starts", (unsigned)sSlot, (unsigned)target);
    Note(text);
    gTasks[taskId].tSlot = sSlot;
    gTasks[taskId].tTarget = target;
    sSlot = PARTY_SIZE;
    LockPlayerFieldControls();
    FadeScreen(FADE_TO_BLACK, 0);
}

#undef tSlot
#undef tTarget

const char *MPTradeEvolution_LastNote(void)
{
    return sLastNote;
}

u32 MPTradeEvolution_WaitingSlot(u32 *target)
{
    *target = sSlot < PARTY_SIZE ? sTarget : SPECIES_NONE;
    return sSlot;
}
#endif
