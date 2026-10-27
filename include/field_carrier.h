#ifndef GUARD_FIELD_CARRIER_H
#define GUARD_FIELD_CARRIER_H

// The Pokemon carrying the player, drawn as itself (src/field_carrier.c): on the water (Surf, and up a Waterfall), under
// it (Dive) and in the air (Fly's take-off and landing), in place of the generic surf blob, underwater silhouette and
// bird. Native PC build only.
#ifdef PORTABLE

struct Sprite;
struct SpritePalette;
struct ObjectEvent;
struct ObjectEventGraphicsInfo;

// The local player's look under the water with a carrier (their surfing pose as the silhouette), and the palettes made
// at run time for it and the carrier: above the character selection's ids and below the followers' tags.
#define FIELD_CARRIER_GFX_UNDERWATER_RIDER       0x0F00
#define FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER   0x3CF0
#define FIELD_CARRIER_PAL_TAG_UNDERWATER_CARRIER 0x3CF1

// The Pokemon that used the move, by its party index (FIELD_MOVE_BOX_USER for one using it from the PC boxes):
// FldEff_UseSurf and FldEff_UseDive (the water's carrier), FldEff_UseFly (the flyer).
void FieldCarrier_SetWater(u32 partyIndex);
void FieldCarrier_SetFlyer(u32 partyIndex);

// The player's surf sprite: the water's carrier made from its overworld picture (FldEff_SurfBlob), or MAX_SPRITES when
// there is none to show (the generic blob is made then). x, y: the original blob's place (its centre); the carrier is
// put where it will carry the rider. underwater: drawn as a silhouette, as the original's.
u8 FieldCarrier_CreateSurfSprite(s16 x, s16 y, u8 subpriority, bool32 underwater);
bool32 FieldCarrier_IsSurfSprite(const struct Sprite *sprite);
// UpdateSurfBlobFieldEffect, after the original's work on a carrier: its swimming animation, its place under the rider
// (the seat; followsRider: the original placed it at the player this frame) and its drawing order. FALSE for the
// generic blob.
bool32 FieldCarrier_UpdateSurfSprite(struct ObjectEvent *player, struct Sprite *playerSprite, struct Sprite *sprite,
                                     bool32 followsRider);
// The player's look under the water while a carrier is shown (FIELD_CARRIER_GFX_UNDERWATER_RIDER): their surfing pose
// as a silhouette, the original's underwater picture having the generic blob drawn in. A save keeps the original's
// underwater look instead (load_save.c), and the avatar's state and gender are read from it as from that look.
bool32 FieldCarrier_IsUnderwaterAvatarGraphicsId(u16 graphicsId);
const struct ObjectEventGraphicsInfo *FieldCarrier_ResolveGraphics(u16 graphicsId);

// The fly bird: the flyer (FIELD_CARRIER_PLAYER) or the Pokemon an NPC flies away on (FIELD_CARRIER_NPC), made from its
// overworld picture, or MAX_SPRITES (the generic bird is made then).
#define FIELD_CARRIER_PLAYER 0
#define FIELD_CARRIER_NPC    1
u8 FieldCarrier_CreateFlySprite(u32 who, s16 x, s16 y, u8 subpriority);
// The bird's callbacks, after they set its place on its path (x2, y2): the player's flyer drawn so that the rider, kept
// on the original's path (8 px over the bird's centre), sits on its back where the riding seats put a rider, and drawn
// behind the rider while it carries them (rider: NULL when it carries nobody). Nothing for the generic bird.
void FieldCarrier_ShiftFlySprite(struct Sprite *bird, const struct Sprite *rider);

// A palette as the original's underwater silhouette draws: each colour by its brightness on its blues (transparency kept)
void FieldCarrier_Silhouette(const u16 *source, u16 *out);
// Sprite palettes made at run time (the silhouettes), for LoadObjectEventPalette and the other palette lookups.
bool32 FieldCarrier_GetSpritePalette(u16 tag, struct SpritePalette *out);
// Each frame: frees the palette of a carrier sprite the game has destroyed.
void FieldCarrier_Tick(void);

// For the self-test and the multiplayer record: the water's carrier now (species, shiny, female), FALSE for none.
bool32 FieldCarrier_GetWater(u16 *species, bool8 *shiny, bool8 *female);
// Where the rider sits on a carrier facing that way (multiplayer_ride.c's seat; facing the camera, low enough that the
// rider's head and face show over it): px the carrier is drawn below the rider, px the rider sits right of its middle
void FieldCarrier_GetSeat(u16 carrierGraphicsId, u16 riderGraphicsId, u32 facing, u8 *lift, s8 *shift);
// The carrier sprites live now (MAX_SPRITES for none): the test driver's log (battle_env_test.c PKMN_TEST_CARRIER_LOG)
void FieldCarrier_GetSpriteIds(u8 *water, u8 *fly);
// The overworld picture a carrier of that species is drawn with (a form without one of its own: its base form's), FALSE
// when there is none (the generic blob or bird is drawn then)
bool32 FieldCarrier_GetGraphicsId(u16 species, bool8 shiny, bool8 female, u16 *graphicsId);

#endif // PORTABLE
#endif // GUARD_FIELD_CARRIER_H
