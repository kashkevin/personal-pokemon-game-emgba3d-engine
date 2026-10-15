#include "global.h"
#ifdef PORTABLE
#include "multiplayer_ladder.h"
#include "multiplayer_battle.h"
#include "battle.h"
#include "data.h"
#include "event_data.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "constants/battle.h"
#include "constants/opponents.h"

extern const u8 EventScript_MultiplayerLadderResult[];

static EWRAM_DATA u32 sPendingCode = 0;
static EWRAM_DATA u8 sPendingWon = 0;

#define TRAINER_MASK 0x3FF

u32 MultiplayerLadder_Encode(u16 trainerA, u16 trainerB, u32 level, bool32 awardExp)
{
    return MP_LADDER_FLAG | (awardExp ? MP_LADDER_EXP : 0) | ((u32)(trainerA & TRAINER_MASK) << 20)
         | ((u32)(trainerB & TRAINER_MASK) << 10) | (level & 0x7F);
}

bool32 MultiplayerLadder_IsLadder(u32 code) { return (code & MP_LADDER_FLAG) != 0; }
u16 MultiplayerLadder_TrainerA(u32 code) { return (code >> 20) & TRAINER_MASK; }
u16 MultiplayerLadder_TrainerB(u32 code) { return (code >> 10) & TRAINER_MASK; }
u32 MultiplayerLadder_Level(u32 code) { return code & 0x7F; }
bool32 MultiplayerLadder_ExpAllowed(u32 code) { return (code & MP_LADDER_EXP) != 0; }

static bool32 UsableTrainer(u16 id)
{
    const struct Trainer *trainer;

    if (id == 0 || id >= TRAINERS_COUNT_EMERALD || id == TRAINER_SECRET_BASE || id == TRAINER_LINK_OPPONENT)
        return FALSE;
    trainer = GetTrainerStructFromId(id);
    return trainer->partySize != 0 || trainer->overrideTrainer != 0;
}

bool32 MultiplayerLadder_IsValid(u32 code)
{
    u32 level = MultiplayerLadder_Level(code);

    return MultiplayerLadder_IsLadder(code) && (code & 0x380) == 0
        && level >= MP_LADDER_MIN_LEVEL && level <= MP_LADDER_MAX_LEVEL // 0: the trainers' own levels
        && MultiplayerLadder_TrainerA(code) != MultiplayerLadder_TrainerB(code)
        && UsableTrainer(MultiplayerLadder_TrainerA(code)) && UsableTrainer(MultiplayerLadder_TrainerB(code));
}

void MultiplayerLadder_ScaleOpponents(u32 code)
{
    u32 level = MultiplayerLadder_Level(code), side, slot;
    bool32 rescale = level != 0; // level 0: the trainers keep their own levels (the Pokemon League)

    for (side = B_TRAINER_OPPONENT_A; side <= B_TRAINER_OPPONENT_B; side++)
    {
        for (slot = 0; slot < PARTY_SIZE; slot++)
        {
            struct Pokemon *mon = &gParties[side][slot];
            enum Species species = GetMonData(mon, MON_DATA_SPECIES);
            u32 hp;

            if (slot >= MP_LADDER_TEAM_SIZE)
            {
                // Two opponents bring three Pokemon each, like the Frontier's multi battles, whatever the trainer had.
                ZeroMonData(mon);
                continue;
            }
            if (species == SPECIES_NONE || !rescale)
                continue;
            SetMonData(mon, MON_DATA_EXP, &gExperienceTables[gSpeciesInfo[species].growthRate][level]);
            CalculateMonStats(mon);
            hp = GetMonData(mon, MON_DATA_MAX_HP);
            SetMonData(mon, MON_DATA_HP, &hp);
        }
        gPartiesCount[side] = CalculatePartyCount(side);
    }
}

void MultiplayerLadder_OnResult(u32 code, u32 outcome)
{
    sPendingCode = code;
    sPendingWon = outcome == B_OUTCOME_WON;
}

// Once the battle has released the party and the field is calm: show the result (and award the experience when the host
// allowed it). Money and progress are the host's and arrive on their own.
void MultiplayerLadder_Tick(void)
{
    if (sPendingCode == 0)
        return;
    if (MultiplayerBattle_IsActive() || MultiplayerBattle_PauseClocks())
        return;
    if (gMain.callback2 != CB2_Overworld || ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked()
     || gPaletteFade.active)
        return;
    gSpecialVar_0x8004 = sPendingWon;
    gSpecialVar_0x8005 = MultiplayerLadder_ExpAllowed(sPendingCode);
    sPendingCode = 0;
    ScriptContext_SetupScript(EventScript_MultiplayerLadderResult);
}
#endif // PORTABLE
