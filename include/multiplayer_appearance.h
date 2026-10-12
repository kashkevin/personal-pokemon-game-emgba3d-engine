#ifndef GUARD_MULTIPLAYER_APPEARANCE_H
#define GUARD_MULTIPLAYER_APPEARANCE_H

// Customized trainer looks (owner amendment "CHARACTER CUSTOMIZATION").
//
// Architecture: palette customization of the Emerald player sprites. Every
// player animation (walk, run, bikes, surf, fishing, field moves, watering)
// shares one 16-colour palette per base character, so recolouring its skin,
// hair, outfit and hat/bag colour slots works in every gameplay state with no
// extra sprite sheets, tiles or per-frame cost. A look is eight bytes:
//   [0] flags (1 = customized)  [1] skin 0-5  [2] hair 0-11
//   [3] outfit 0-15  [4] bottom (reserved)  [5] hat & bag 0-15
//   [6] accent (reserved)  [7] zero
// Value 0 of every part keeps Brendan's/May's original colours. The host
// stores each player's look in their profile and every client recolours the
// remote trainers locally from these bytes.

#define MP_APPEARANCE_BYTES 8

void MultiplayerAppearance_Init(void);
void MultiplayerAppearance_Tick(bool32 connected);
const u8 *MultiplayerAppearance_GetPublished(void);
void MultiplayerAppearance_OpenEditor(void);
bool32 MultiplayerAppearance_IsEditorActive(void);
void MultiplayerAppearance_BuildPalette(const u16 *base, bool32 isMay, const u8 *look, u16 *out);

#ifdef PORTABLE
// The look the local player shows: the one being edited while the style editor
// is open, else the saved one (src/character_customization.c keeps it in the save).
const u8 *MultiplayerAppearance_GetLocalLook(void);
#endif

#endif // GUARD_MULTIPLAYER_APPEARANCE_H
