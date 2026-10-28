#ifndef GUARD_POKEMON_JUMP_H
#define GUARD_POKEMON_JUMP_H

#include "main.h"

void StartPokemonJump(u16 partyId, MainCallback exitCallback);
bool32 IsSpeciesAllowedInPokemonJump(enum Species species);
void IsPokemonJumpSpeciesInParty(void);
void ResetPokemonJumpRecords(void);
void ShowPokemonJumpRecords(void);
#ifdef PORTABLE
void PokemonJump_StartComputerPartners(void); // KNOWN_ISSUES MS-141 (PC game only)
#endif
#line 11 // gba-line

#endif // GUARD_POKEMON_JUMP_H
