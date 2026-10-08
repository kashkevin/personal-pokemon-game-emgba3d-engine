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
};
enum MultiplayerSaveState {
    MP_SAVE_UNBOUND, MP_SAVE_BOUND, MP_SAVE_EXPORTING, MP_SAVE_IMPORTING,
    MP_SAVE_FINALIZING, MP_SAVE_RELOADING, MP_SAVE_COMMITTED, MP_SAVE_ERROR,
};
enum MultiplayerSaveStatus {
    MP_SAVE_OK, MP_SAVE_BUSY, MP_SAVE_UNSAFE, MP_SAVE_MALFORMED, MP_SAVE_SESSION,
    MP_SAVE_ORDER, MP_SAVE_CRC, MP_SAVE_FLASH, MP_SAVE_SPECIAL, MP_SAVE_OWNERSHIP,
    MP_SAVE_VALIDATION, MP_SAVE_CANCELED, MP_SAVE_TIMEOUT, MP_SAVE_INTERNAL,
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
void MultiplayerSave_Init(void);
void MultiplayerSave_Tick(void);
bool32 MultiplayerSave_IsHeld(void);
bool32 MultiplayerSave_SkipCallbacks(void);
bool32 MultiplayerSave_PauseClocks(void);

#endif
