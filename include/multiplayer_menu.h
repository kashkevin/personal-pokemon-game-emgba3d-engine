#ifndef GUARD_MULTIPLAYER_MENU_H
#define GUARD_MULTIPLAYER_MENU_H

#define MP_MENU_MAGIC 0x31554D45u
#define MP_MENU_ABI 1
#define MP_MENU_OPEN 1
#define MP_MENU_CHOOSE 2
#define MP_MENU_CLOSE 3

struct MultiplayerMenuRequest {
    u32 sequence, screen;
    u16 action;
    u8 kind, reserved[5];
};
struct MultiplayerMenuRow {
    u16 action;
    u8 label[26]; // NUL-padded printable ASCII, never native control codes.
};
struct MultiplayerMenuScreen {
    u32 ackRequest, screen;
    u8 flags, count;
    u16 backAction;
    u8 title[24], status[32];
    struct MultiplayerMenuRow rows[6];
    u8 reserved[4];
};
struct MultiplayerMenuMailbox {
    u32 magic;
    u16 abiVersion, byteSize;
    u32 frame, localGeneration;
    struct MultiplayerMenuRequest request;
    u32 inboundGeneration;
    struct MultiplayerMenuScreen screen;
    u8 reserved[236];
};
extern const u32 gMultiplayerMenuAbiVersion;
extern volatile struct MultiplayerMenuMailbox gMultiplayerMenuMailbox;

void MultiplayerMenu_Init(void);
void MultiplayerMenu_Tick(void);
bool32 MultiplayerMenu_IsActive(void);
bool32 MultiplayerMenu_CanAdmitBattle(u32 session, u32 incarnation);
bool32 MultiplayerMenu_IsBattleAdmissionHeld(void);
bool32 MultiplayerMenu_AdmissionResetWasDisabled(void);
void MultiplayerMenu_TransferBattleAdmission(u32 session, u32 incarnation);
bool32 MultiplayerMenu_Open(void); // Caller has closed the ordinary Start menu.
bool32 MultiplayerMenu_ValidateScreen(const struct MultiplayerMenuScreen *screen);

#if TESTING
bool32 MultiplayerMenu_TestBegin(void);
void MultiplayerMenu_TestInput(u16 keys);
void MultiplayerMenu_TestReadScreen(void);
void MultiplayerMenu_TestClose(void);
u32 MultiplayerMenu_TestScreen(void);
u8 MultiplayerMenu_TestGlyph(u8 ascii);
#endif
#endif
