#ifndef GUARD_MULTIPLAYER_RIDE_H
#define GUARD_MULTIPLAYER_RIDE_H

// Ground riding (owner amendment "RIDEABLE POKEMON"): a cosmetic/comfort
// movement mode. The lead Pokemon (the follower) carries the player; it never
// grants Surf, Fly or any other traversal ability. Every mount moves at the
// same standard pace (the running pace, without holding B). The state is
// volatile EWRAM only: a reloaded save always starts on foot.

#define MP_RIDE_LOCAL_ID_MOUNT 248 // between the World Event object (247) and generated wild Pokemon (249-252)

void MultiplayerRide_Init(void);
void MultiplayerRide_Tick(bool32 fieldReady);
bool32 MultiplayerRide_IsRiding(void);
bool32 MultiplayerRide_CanMount(void);
bool32 MultiplayerRide_IsSpeciesRideable(u16 species);
bool32 MultiplayerRide_IsMountObject(const struct ObjectEvent *objectEvent);

// Field input (R button) and Start menu entries.
bool32 MultiplayerRide_TryToggleFromField(void);
void MultiplayerRide_StartFromMenu(void);

// Hooks in field_player_avatar.c / event_object_movement.c.
void MultiplayerRide_MirrorPlayerMovement(u8 movementActionId);
void MultiplayerRide_BeginObjectUpdate(struct ObjectEvent *objectEvent, struct Sprite *sprite);
void MultiplayerRide_EndObjectUpdate(struct ObjectEvent *objectEvent, struct Sprite *sprite);
u8 MultiplayerRide_GetLiftForMount(u16 mountGraphicsId);

// Script specials (follower interaction).
void MultiplayerRide_CanRideFollower(void);
void MultiplayerRide_MountFromScript(void);

#endif // GUARD_MULTIPLAYER_RIDE_H
