#ifndef GUARD_CHARACTER_CUSTOMIZATION_H
#define GUARD_CHARACTER_CUSTOMIZATION_H

// Character selection: the approved roster of the character-customization
// library (32 presets, kit 1's builds A and B and its three class looks) as
// playable trainers. Native PC build only: the GBA ROM build compiles none of
// it (src/character_customization.c is empty there).
//
// * The choice is part of the multiplayer look (MP_APPEARANCE_BYTES): byte 0 = 1
//   and the character id in the reserved nibbles of bytes 4 and 6, so the host
//   stores and relays it unchanged and older games still read a valid look
//   (they show the default trainer). It is kept in the save as well.
// * The local player keeps the engine's own avatar graphics ids (every script,
//   state check and save stays as it was); GetObjectEventGraphicsInfo returns
//   the character's graphics for them. Other players' avatars use explicit ids
//   (CC_GFX_ID) that carry their own character.
// * Frames are the library's own pixels placed by its anchors (corrected ground
//   line; surfing by the seat), played by the engine's own animation tables.
//   Data: src/data/character_customization/ (tools/character_customization).

#ifdef PORTABLE
#include "constants/character_customization.h"

#define CC_CHARACTER_NONE 0  // the default trainer (Brendan / May, recolourable)

enum CharCustomGroup
{
    CC_GROUP_NORMAL,
    CC_GROUP_MACH,
    CC_GROUP_ACRO,
    CC_GROUP_SURF,
    CC_GROUP_FIELD,
    CC_GROUP_FISH,
    CC_GROUP_WATERING,
    CC_GROUP_DECORATING,
    CC_GROUP_COUNT
};

enum CharCustomKind
{
    CC_KIND_PRESET,
    CC_KIND_KIT1,
    CC_KIND_LOOK,
};

struct CharCustomGfx
{
    const struct SpriteFrameImage *images;
    const union AnimCmd *const *anims;
    u16 frameSize;
    u8 width;
    u8 height;
    u8 tracks;
};

struct CharCustomCharacter
{
    const u8 *name;
    const u8 *variant;
    const char *libraryId;      // the library's preset / kit / look id
    u16 registryNum;            // the library registry number (0 for kit 1)
    u8 gender;
    u8 kind;
    bool8 vanillaAvatar;        // the engine's own Brendan / May graphics
    u16 frontPic;               // TRAINER_PIC_*: Trainer Card, opponent view
    u16 backPic;                // TRAINER_PIC_*: battle intro and throw
    const u32 *cardPic;         // a front bigger than 64 x 64, shown whole on the Trainer Card
    const u16 *cardPal;
    u8 cardWidth;
    u8 cardHeight;
    const u16 *palette;
    struct CharCustomGfx gfx[CC_GROUP_COUNT];
};

// Avatar graphics ids that name a character (other players' avatars):
// CC_GFX_ID(character, gender, state), state = PLAYER_AVATAR_STATE_* or
// CC_GFX_STATE_DECORATING. Character 0 is the engine's own trainer of that gender.
#define CC_GFX_STATES            10
#define CC_GFX_STATE_DECORATING  9
#define CC_GFX_BASE              0x0C00
#define CC_GFX_ID(character, gender, state) (CC_GFX_BASE + ((character) * 2 + (gender)) * CC_GFX_STATES + (state))
#define CC_GFX_END               CC_GFX_ID(CC_CHARACTER_COUNT, 0, 0)
#define CC_PAL_TAG_BASE          0x3D00 // one sprite palette per character, shared by every avatar using it

struct CharCustomEditorState
{
    bool8 active;
    u8 cursor;          // CC_EDITOR_ROW_*
    u8 character;       // previewed character (0 = default trainer)
    u8 count;           // characters in the roster, the default trainer included
    u8 colours[4];      // skin, hair, outfit, hat/bag of the default trainer
    bool8 coloursEnabled;
};

enum
{
    CC_EDITOR_ROW_CHARACTER,
    CC_EDITOR_ROW_SKIN,
    CC_EDITOR_ROW_HAIR,
    CC_EDITOR_ROW_OUTFIT,
    CC_EDITOR_ROW_HAT,
    CC_EDITOR_ROW_SAVE,
    CC_EDITOR_ROW_CANCEL,
    CC_EDITOR_ROW_COUNT
};

// Roster
u32 CharCustom_Count(void);
const struct CharCustomCharacter *CharCustom_Get(u32 character);   // NULL for 0 / unknown ids
bool32 CharCustom_IsValid(u32 character);

// Looks (8 bytes, MP_APPEARANCE_BYTES)
u32 CharCustom_FromLook(const u8 *look);
void CharCustom_SetInLook(u8 *look, u32 character);
bool32 CharCustom_ReadSavedLook(u8 *look, bool8 *pending);
void CharCustom_WriteSavedLook(const u8 *look, bool8 pending);

// The local player's character: the one being previewed while the style
// editor is open, else the saved one.
u32 CharCustom_LocalCharacter(void);
void CharCustom_RefreshLocalAvatar(void);

// Engine hooks (event_object_movement.c, multiplayer.c)
const struct ObjectEventGraphicsInfo *GetVanillaObjectEventGraphicsInfo(u16 graphicsId);
const struct ObjectEventGraphicsInfo *CharCustom_ResolveGraphics(u16 *graphicsId);
bool32 CharCustom_GetSpritePalette(u16 tag, struct SpritePalette *out);
const struct StepAnimTable *CharCustom_GetStepAnimTable(const union AnimCmd *const *anims);
u16 CharCustom_RemoteGraphicsId(const u8 *look, u16 vanillaGraphicsId);

// Battles and the Trainer Card (data.h macros, trainer_card.c, battle_controllers.c)
bool32 CharCustom_DrawTrainerCardPic(u8 windowId, u8 paletteSlot, u8 x, u8 y);
void CharCustom_SetupTrainerBackSprite(u8 spriteId, u16 trainerPicId);

// The style editor (multiplayer_appearance.c) and the native UI's screen 9
void CharCustom_GetEditorState(struct CharCustomEditorState *out);
bool32 CharCustom_EditorSelect(u32 character); // as LEFT / RIGHT on CHARACTER would; FALSE when the editor is closed
const u8 *CharCustom_GetName(u32 character);
const u8 *CharCustom_GetVariant(u32 character);

// A character's walking and running field sprite for previews (the dressing
// room's mirror, player lists): frames of width x height in 4bpp OAM tile
// order, picked by its own animation table (ANIM_STD_FACE_* / ANIM_STD_GO_* /
// ANIM_RUN_*: frame index and hFlip). The palette is NULL for the default
// trainer (0), whose colours come from the look (MultiplayerAppearance_BuildPalette).
struct CharCustomFieldSprite
{
    const struct SpriteFrameImage *images;
    const union AnimCmd *const *anims;
    const u16 *palette;
    u8 width;
    u8 height;
};
void CharCustom_GetFieldSprite(u32 character, u32 gender, struct CharCustomFieldSprite *out);

#endif // PORTABLE

#endif // GUARD_CHARACTER_CUSTOMIZATION_H
