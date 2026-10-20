#ifndef GUARD_MULTIPLAYER_SAVE_H
#define GUARD_MULTIPLAYER_SAVE_H

#define MULTIPLAYER_SAVE_MAGIC 0x31534D45
#define MULTIPLAYER_SAVE_SIZE 512
#define MULTIPLAYER_SAVE_FLASH_SIZE 131072
#define MULTIPLAYER_SAVE_CHUNK 128

enum MultiplayerSaveOpcode {
    MP_SAVE_BIND = 1, MP_SAVE_BEGIN_EXPORT, MP_SAVE_READ, MP_SAVE_END,
    MP_SAVE_BEGIN_IMPORT, MP_SAVE_WRITE, MP_SAVE_COMMIT, MP_SAVE_CANCEL, MP_SAVE_QUERY,
    MP_SAVE_QUERY_OWNERSHIP, MP_SAVE_GRANT_OWNERSHIP,
    MP_SAVE_BEGIN_TRADE_EXPORT, MP_SAVE_KEEP_TRADE, MP_SAVE_ABORT_TRADE,
    MP_SAVE_RELEASE_TRADE, MP_SAVE_REBIND_TRADE, MP_SAVE_QUERY_TRADE,
    MP_SAVE_READ_WINDOW,
    MP_SAVE_BEGIN_EMERGENCY_EXPORT, MP_SAVE_PRESERVE_EMERGENCY, MP_SAVE_QUERY_EMERGENCY, MP_SAVE_PROMOTE_EMERGENCY_EXPORT,
    MP_SAVE_BEGIN_EFFECT_RECORD, MP_SAVE_CHOOSE_EFFECT_MOVE,
    MP_SAVE_QUERY_EFFECT_RECORD, MP_SAVE_DISCARD_EFFECT_RECORD,
    MP_SAVE_BEGIN_EFFECT_SAVE, MP_SAVE_READ_EFFECT_WINDOW,
    MP_SAVE_QUERY_EFFECT_CHOICE,
#ifdef PORTABLE
    MP_SAVE_WRITE_WINDOW,  // PC builds: one staged import sector, from gMultiplayerSaveImportWindow
#endif
};
enum MultiplayerSaveState {
    MP_SAVE_UNBOUND, MP_SAVE_BOUND, MP_SAVE_EXPORTING, MP_SAVE_IMPORTING,
    MP_SAVE_FINALIZING, MP_SAVE_RELOADING, MP_SAVE_COMMITTED, MP_SAVE_ERROR,
    MP_SAVE_TRADE_HELD,
    MP_SAVE_EMERGENCY_UNSAVED, MP_SAVE_EMERGENCY_PRESERVED,
};
enum MultiplayerSaveStatus {
    MP_SAVE_OK, MP_SAVE_BUSY, MP_SAVE_UNSAFE, MP_SAVE_MALFORMED, MP_SAVE_SESSION,
    MP_SAVE_ORDER, MP_SAVE_CRC, MP_SAVE_FLASH, MP_SAVE_SPECIAL, MP_SAVE_OWNERSHIP,
    MP_SAVE_VALIDATION, MP_SAVE_CANCELED, MP_SAVE_TIMEOUT, MP_SAVE_INTERNAL, MP_SAVE_BATTLE_ADMISSION,
};
struct MultiplayerSaveControl {
    u32 incarnation;
    u32 sessionEpoch;
    u8 requestId[16];
    u32 commandSeq;
    u32 offset;
    u32 totalSize;
    u32 revision;
    u8 opcode;
    u8 status;
    u16 length;
    u32 chunkCrc32;
    u8 data[192];
};
struct MultiplayerSaveMailbox {
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 frame;
    u32 state;
    u32 reserved[2];
    u32 commandGeneration;
    struct MultiplayerSaveControl command;
    u32 responseGeneration;
    struct MultiplayerSaveControl response;
};
extern volatile struct MultiplayerSaveMailbox gMultiplayerSaveMailbox;
extern const u32 gMultiplayerTradeAbiVersion;
extern const u32 gMultiplayerTradeEffectPrepareAbiVersion;
extern const u32 gMultiplayerTradeEffectSaveAbiVersion;
extern const u32 gMultiplayerTradeEffectChoiceAbiVersion;
extern const u32 gMultiplayerSaveWindowAbiVersion;
#ifdef PORTABLE
// PC builds only (the GBA ROM stays byte-identical): a trade's received save arrives one 4 KiB sector at a time.
#define MULTIPLAYER_SAVE_IMPORT_WINDOW 4096
extern u8 gMultiplayerSaveImportWindow[MULTIPLAYER_SAVE_IMPORT_WINDOW];
extern const u32 gMultiplayerSaveImportWindowAbiVersion;
// Update 32: TRUE while the game's save is the canonical image it imported last (a trade's post-image) and the game has
// not saved since: that image names its own save counter, one lower than the one the game wrote it under.
bool32 MultiplayerSave_IsImportedCounter(u32 counter);
#endif
extern const u32 gMultiplayerEmergencySaveAbiVersion;
void MultiplayerSave_Init(void);
void MultiplayerSave_Tick(void);
bool32 MultiplayerSave_IsHeld(void);
bool32 MultiplayerSave_SkipCallbacks(void);
bool32 MultiplayerSave_PauseClocks(void);
bool32 MultiplayerSave_FieldReady(void);
bool32 MultiplayerSave_FieldReadyForBattleMenu(u32 session, u32 incarnation);
bool32 MultiplayerSave_CheckBinding(u32 session, u32 incarnation);
bool32 MultiplayerSave_HasBinding(void);
bool32 MultiplayerSave_GetIdleBinding(u32 *session, u32 *incarnation);
bool32 MultiplayerSave_GetTradeMenuBinding(u32 *session, u32 *incarnation);

#if TESTING
u32 MultiplayerSave_TestCrc32(u32 crc, const u8 *data, u32 size);
#endif

#endif
