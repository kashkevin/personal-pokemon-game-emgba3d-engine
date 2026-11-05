#ifndef GUARD_MULTIPLAYER_MENU_H
#define GUARD_MULTIPLAYER_MENU_H

#define MP_MENU_MAGIC 0x31554D45u
#define MP_MENU_ABI 2
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
#ifdef PORTABLE
// KNOWN_ISSUES MS-52 (bridge cap towerRecord, the launcher's tower_record=1; the manifest's marker is tower_legends.c's
// gMultiplayerTowerRecordAbiVersion): the host's own Tower record for this player, sent after a Tower page's screen (a
// style 1-8) in the same 'M' frame and kept at mailbox offset 276 with that screen (zero after any other screen). The
// native Tower screens draw from it while the save's copy waits for the host's TOWER_STATE word, which is applied only
// on the field.
#define MP_MENU_TOWER_RECORD_MAGIC 0x31485754u // "TWH1"
struct MultiplayerMenuTowerRecord {
    u32 magic;
    u8 progress;  // the floor to climb next, 1..101 (101: all cleared)
    u8 highest;   // the highest floor cleared, 0..100
    u16 claims;   // the claim bits as the host holds them (prizes 0-11, milestone Pokemon 12-15)
};
#endif
struct MultiplayerMenuMailbox {
    u32 magic;
    u16 abiVersion, byteSize;
    u32 frame, localGeneration;
    struct MultiplayerMenuRequest request;
    u32 inboundGeneration;
    struct MultiplayerMenuScreen screen;
#ifdef PORTABLE
    struct MultiplayerMenuTowerRecord towerRecord;
    u8 reserved[228];
#else
    u8 reserved[236];
#endif
};
extern const u32 gMultiplayerMenuAbiVersion;
extern volatile struct MultiplayerMenuMailbox gMultiplayerMenuMailbox;
#ifdef PORTABLE
// PC builds only (the GBA ROM stays byte-identical): a screen with flags 3
// ends the Enter battle latch when the companion refused that Enter before
// any ADMIT, so the window never waits for an admission that cannot come.
#define MP_MENU_RELEASE_ABI 1
#define MP_MENU_FLAG_RELEASE 3
extern const u32 gMultiplayerMenuReleaseAbiVersion;
// Update 32: an OPEN may name the page the companion shows first (bridge cap menuOpenPage, the launcher's
// menu_open_page=1): 1 the soft reset (START > PLAYER > RESET in a world).
#define MP_MENU_OPEN_PAGE_ABI 1
#define MP_MENU_PAGE_SOFT_RESET 1
extern const u32 gMultiplayerMenuOpenPageAbiVersion;
// MS-61: page 3, the host save after an in-game SAVE in a world (bridge cap hostSavePage, the launcher's
// host_save_page=1): the companion closes the window at once and the launcher saves on the host, as Save now does.
#define MP_MENU_HOST_SAVE_PAGE_ABI 1
#define MP_MENU_PAGE_HOST_SAVE 3
extern const u32 gMultiplayerHostSavePageAbiVersion;
// KNOWN_ISSUES MS-55: an ordinary screen with flags MP_MENU_FLAG_NEW_PAGE is the first of a page the window was not on
// (bridge cap menuNewPage, the launcher's menu_new_page=1): the cursor starts on its first row. A page's own refreshes
// and turning through one list's pages keep the row, as before.
#define MP_MENU_NEW_PAGE_ABI 1
#define MP_MENU_FLAG_NEW_PAGE 0x80
extern const u32 gMultiplayerMenuNewPageAbiVersion;
bool32 MultiplayerMenu_OpenPage(u16 page);
// KNOWN_ISSUES MS-52: the host's record kept with the Tower page shown (FALSE: none; the save's record stands).
bool32 MultiplayerMenu_TowerRecordShown(struct MultiplayerMenuTowerRecord *out);
// --selftest-tower: the window without the field or its text window (Begin), a screen read by the frame's ReadScreen.
bool32 MultiplayerMenu_SelfTestBegin(void);
void MultiplayerMenu_SelfTestReadScreen(void);
#endif

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
