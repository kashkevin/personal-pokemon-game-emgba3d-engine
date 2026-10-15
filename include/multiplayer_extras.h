#ifndef GUARD_MULTIPLAYER_EXTRAS_H
#define GUARD_MULTIPLAYER_EXTRAS_H

// Shared-world extras (EmeraldTogether expansion Phase 6, ABI EMX1 v1):
// multiplayer Secret Bases.
//
// While connected, the game publishes the player's own secret base (the
// record in the save, without the registry flags only this game keeps) and
// applies the bases the host relays from the other players. The host, not
// this game, decides who sees whose base (permissions) and withdraws a base by
// sending its owner's name with no location. Applying a base is the original's
// record-mix rules (secret_base.c, MultiplayerSecretBase_Receive): it lands in
// a free or unregistered slot and the game's own entrance, interior, decoration
// and owner-battle code do the rest.
//
// Native PC builds only (the GBA ROM has no such mailbox and its bytes stay
// pinned). The mailbox sits next to the EMV1 spawn mailbox and is moved by the
// native bridge (mp_bridge.c frames 'e', 'x' and 'y'); the app repository's
// extras_mailbox.py is its byte-for-byte counterpart.

#ifdef PORTABLE

#define MP_EXTRAS_MAGIC         0x31584D45u // EMX1
#define MP_EXTRAS_ABI_VERSION   1
#define MP_EXTRAS_MAILBOX_BYTES 384
#define MP_EXTRAS_BASE_BYTES    160 // sizeof(struct SecretBase)

#define MP_EXTRAS_FLAG_ENABLED  (1 << 0) // connected to a world that relays bases

struct MultiplayerExtrasLocal
{
    u32 baseSeq;     // changes whenever the player's own base changes; 0: nothing published yet
    u8 hasBase;
    u8 reserved[3];
    u8 base[MP_EXTRAS_BASE_BYTES];
};

struct MultiplayerExtrasMailbox
{
    // Game-owned: odd localGeneration while writing.
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 localGeneration;
    struct MultiplayerExtrasLocal local;
    // Bridge-owned from here on: odd inboundGeneration while writing.
    u32 inboundGeneration;
    u32 flags;
    u32 baseAck;     // the last baseSeq the host has received
    u32 recordSeq;   // changes for every base the host hands over; 0: none yet
    u8 sourceSlot;   // roster slot of the base's owner (information only)
    u8 reserved[3];
    u8 base[MP_EXTRAS_BASE_BYTES]; // all zero but a name: withdraw that trainer's base
    u8 pad[24];
};

extern volatile struct MultiplayerExtrasMailbox gMultiplayerExtrasMailbox;
extern const u32 gMultiplayerExtrasAbiVersion;

void MultiplayerExtras_Init(void);
void MultiplayerExtras_Tick(bool32 fieldReady, bool32 connected);

// secret_base.c
bool32 MultiplayerSecretBase_GetOwn(u8 *out);
bool32 MultiplayerSecretBase_Receive(const u8 *record);

#endif // PORTABLE
#endif // GUARD_MULTIPLAYER_EXTRAS_H
