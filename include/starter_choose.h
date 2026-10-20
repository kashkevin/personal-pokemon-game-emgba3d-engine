#ifndef GUARD_STARTER_CHOOSE_H
#define GUARD_STARTER_CHOOSE_H

extern const u16 gBirchBagGrass_Pal[];
extern const u32 gBirchBagTilemap[];
extern const u32 gBirchGrassTilemap[];
extern const u32 gBirchBagGrass_Gfx[];
extern const u32 gPokeballSelection_Gfx[];

u16 GetStarterPokemon(u16 chosenStarterId);
u16 GetPlayerStarterPokemon(void);
void SavePlayerStarterChoice(u16 choice);
void CB2_ChooseStarter(void);
#ifdef PORTABLE
void StarterChoose_OpenFromLab(u8 selection); // Update 32: the next CB2_ChooseStarter starts there; B backs out
#endif

#endif // GUARD_STARTER_CHOOSE_H
