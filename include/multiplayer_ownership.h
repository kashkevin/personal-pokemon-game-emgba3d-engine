#ifndef GUARD_MULTIPLAYER_OWNERSHIP_H
#define GUARD_MULTIPLAYER_OWNERSHIP_H

#define MULTIPLAYER_OWNERSHIP_MAGIC 0x314F4D45
#define MULTIPLAYER_OWNERSHIP_SIZE 600
#define MULTIPLAYER_OWNERSHIP_SERIAL_LIMIT 0x100000
#define MULTIPLAYER_OWNERSHIP_SLOTS 432
#define MP_RETIRE_RELEASE 1
#define MP_RETIRE_NPC_EXCHANGE 2

extern const u32 gMultiplayerOwnershipAbi;
void MultiplayerOwnership_InvalidateGrant(void);
void MultiplayerOwnership_Loaded(void);
bool32 MultiplayerOwnership_IsEnrolled(void);
bool32 MultiplayerOwnership_CanSaveParty(void);
void MultiplayerOwnership_RefreshChecksum(void);
bool32 MultiplayerOwnership_ValidateSave(const u8 *data, bool32 allowLegacy);
bool32 MultiplayerOwnership_IsLegacy(const u8 *data);
u8 MultiplayerOwnership_Grant(const u8 *data, u32 lease, u32 incarnation);
u8 MultiplayerOwnership_Query(u8 *data);
u8 MultiplayerOwnership_PrepareExport(void);
bool32 MultiplayerOwnership_Retire(const struct BoxPokemon *mon, u32 reason);
void MultiplayerOwnership_Acknowledge(void);
bool32 MultiplayerOwnership_ImportMatches(const u8 *data);

#endif
