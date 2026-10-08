#ifndef GUARD_MULTIPLAYER_RESET_ADMISSION_H
#define GUARD_MULTIPLAYER_RESET_ADMISSION_H
#define MP_RESET_MAGIC 0x31524D45u
#define MP_RESET_ABI 1
struct MultiplayerResetRequest {
    u32 sequence, session, incarnation, encounter, observedCount;
    u8 reserved[12];
};
struct MultiplayerResetReply {
    u32 sequence, session, incarnation, encounter, attempt, status;
    u8 reserved[8];
};
struct MultiplayerResetMailbox {
    u32 magic;
    u16 abi, byteSize;
    u32 frame, localGeneration;
    struct MultiplayerResetRequest request;
    u32 inboundGeneration;
    struct MultiplayerResetReply reply;
    u8 reserved[44];
};
extern volatile struct MultiplayerResetMailbox gMultiplayerResetMailbox;
extern const u32 gMultiplayerResetAbiVersion;
void MultiplayerResetAdmission_Init(void);
void MultiplayerResetAdmission_Tick(void);
void MultiplayerResetAdmission_Begin(u32 encounter);
bool32 MultiplayerResetAdmission_IsWaiting(void);
#endif
