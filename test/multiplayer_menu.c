#include "global.h"
#include "multiplayer_menu.h"
#include "multiplayer_save.h"
#include "multiplayer_battle.h"
#include "main.h"
#include "test/test.h"
#include "constants/characters.h"

static struct MultiplayerMenuScreen Screen(u32 ack, u32 revision)
{
    struct MultiplayerMenuScreen screen = {0};
    screen.ackRequest = ack; screen.screen = revision; screen.count = 2;
    memcpy(screen.title, "Players", 8); memcpy(screen.status, "Choose an action", 17);
    screen.rows[0].action = 1; memcpy(screen.rows[0].label, "Team", 5);
    screen.rows[1].action = 2; memcpy(screen.rows[1].label, "Challenge", 10);
    return screen;
}

static void Deliver(const struct MultiplayerMenuScreen *screen, u32 generation)
{
    gMultiplayerMenuMailbox.inboundGeneration = generation | 1;
    gMultiplayerMenuMailbox.screen = *screen;
    gMultiplayerMenuMailbox.inboundGeneration = generation;
    MultiplayerMenu_TestReadScreen();
}

TEST("Multiplayer menu: strict screen parser rejects malformed actions, native text controls and hidden unused bytes")
{
    struct MultiplayerMenuScreen screen = Screen(1, 2), valid = screen;
    EXPECT(MultiplayerMenu_ValidateScreen(&screen));
    screen.backAction = 7; EXPECT(MultiplayerMenu_ValidateScreen(&screen));
    screen.backAction = 3; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.rows[1].action = 1; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.rows[1].action = 0; EXPECT(MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.rows[2].label[0] = 'X'; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.title[23] = 'X'; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    memset(screen.status, 'A', sizeof(screen.status)); EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.rows[0].label[1] = 0xFC; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.rows[0].label[1] = 31; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.reserved[3] = 1; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.count = 7; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.flags = 2; EXPECT(MultiplayerMenu_ValidateScreen(&screen));
    screen.flags = 3; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.ackRequest = 0; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen = valid;
    screen.screen = 0; EXPECT(!MultiplayerMenu_ValidateScreen(&screen));
}

TEST("Multiplayer menu: only complete current responses enable choices and old screen actions cannot be replayed")
{
    struct MultiplayerMenuScreen screen = Screen(1, 10);
    MultiplayerMenu_Init(); EXPECT(MultiplayerMenu_TestBegin());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 1);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_OPEN);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.screen, 0);
    MultiplayerMenu_TestInput(A_BUTTON); EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 1);
    Deliver(&screen, 1); EXPECT_EQ(MultiplayerMenu_TestScreen(), 0);
    screen.ackRequest = 2; Deliver(&screen, 2); EXPECT_EQ(MultiplayerMenu_TestScreen(), 0);
    screen.ackRequest = 1; Deliver(&screen, 4); EXPECT_EQ(MultiplayerMenu_TestScreen(), 10);
    MultiplayerMenu_TestInput(A_BUTTON);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_CHOOSE);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.action, 1);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.screen, 10);
    MultiplayerMenu_TestInput(A_BUTTON); EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    screen.screen = 11; Deliver(&screen, 6); EXPECT_EQ(MultiplayerMenu_TestScreen(), 10);
    screen.ackRequest = 2; screen.screen = 10; Deliver(&screen, 8);
    MultiplayerMenu_TestInput(A_BUTTON); EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    screen.screen = 11; Deliver(&screen, 10); EXPECT_EQ(MultiplayerMenu_TestScreen(), 11);
    MultiplayerMenu_TestInput(DPAD_DOWN); MultiplayerMenu_TestInput(A_BUTTON);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 3);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.action, 2);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.screen, 11);
    EXPECT_EQ(gMultiplayerMenuMailbox.localGeneration & 1, 0);
    MultiplayerMenu_TestClose(); MultiplayerMenu_Init();
}

TEST("Multiplayer menu: disabled rows cannot consent and B distinguishes subpage back from local close")
{
    struct MultiplayerMenuScreen screen = Screen(1, 1);
    MultiplayerMenu_Init(); EXPECT(MultiplayerMenu_TestBegin());
    screen.rows[0].action = 0; screen.backAction = 7;
    Deliver(&screen, 2); MultiplayerMenu_TestInput(A_BUTTON);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 1);
    MultiplayerMenu_TestInput(B_BUTTON);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.action, 7);
    EXPECT(MultiplayerMenu_IsActive());
    // B remains available when a server response is missing; it never emits a
    // second choice or invents authoritative cancellation of the first one.
    MultiplayerMenu_TestInput(B_BUTTON);
    EXPECT(!MultiplayerMenu_IsActive());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 3);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_CLOSE);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.action, 0);
    MultiplayerMenu_Init();
}

TEST("Multiplayer menu: host close acknowledges after local release and reopen retains monotonic requests")
{
    struct MultiplayerMenuScreen screen = Screen(1, 42);
    MultiplayerMenu_Init(); EXPECT(MultiplayerMenu_TestBegin());
    Deliver(&screen, 2); screen.flags = 1; Deliver(&screen, 4);
    EXPECT(!MultiplayerMenu_IsActive()); EXPECT_EQ(MultiplayerMenu_TestScreen(), 0);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_CLOSE);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.screen, 42);
    EXPECT(MultiplayerMenu_TestBegin());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_OPEN);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 3);
    screen.flags = 0; Deliver(&screen, 6); EXPECT_EQ(MultiplayerMenu_TestScreen(), 0);
    screen.ackRequest = 3; screen.screen = 1; Deliver(&screen, 8);
    EXPECT_EQ(MultiplayerMenu_TestScreen(), 1);
    MultiplayerMenu_TestInput(B_BUTTON);
    EXPECT(!MultiplayerMenu_IsActive()); EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 4);
    MultiplayerMenu_Init();
}

TEST("Multiplayer menu: disconnected tick releases the menu with a newer close request and consumes closing input")
{
    u16 oldNew = gMain.newKeys, oldRaw = gMain.newKeysRaw, oldRepeated = gMain.newAndRepeatedKeys;
    MultiplayerMenu_Init(); EXPECT(MultiplayerMenu_TestBegin());
    // Fixture has no production connection/save binding, so the same runtime
    // guard used on bridge loss must close before accepting any screen/input.
    gMain.newKeys = gMain.newKeysRaw = gMain.newAndRepeatedKeys = A_BUTTON;
    MultiplayerMenu_Tick();
    EXPECT(!MultiplayerMenu_IsActive());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.kind, MP_MENU_CLOSE);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    EXPECT_EQ(gMain.newKeys, 0); EXPECT_EQ(gMain.newKeysRaw, 0); EXPECT_EQ(gMain.newAndRepeatedKeys, 0);
    EXPECT(!MultiplayerMenu_Open());
    gMain.newKeys = oldNew; gMain.newKeysRaw = oldRaw; gMain.newAndRepeatedKeys = oldRepeated;
    MultiplayerMenu_Init();
}

TEST("Multiplayer menu: printable ASCII translation cannot produce a native string control byte")
{
    for (u32 c = 32; c <= 126; c++) EXPECT(MultiplayerMenu_TestGlyph(c) < CHAR_DYNAMIC);
    EXPECT_EQ(MultiplayerMenu_TestGlyph('A'), CHAR_A);
    EXPECT_EQ(MultiplayerMenu_TestGlyph('z'), CHAR_z);
    EXPECT_EQ(MultiplayerMenu_TestGlyph('7'), CHAR_7);
    EXPECT_EQ(MultiplayerMenu_TestGlyph(':'), CHAR_COLON);
    EXPECT_EQ(MultiplayerMenu_TestGlyph('~'), CHAR_QUESTION_MARK);
}

TEST("Multiplayer menu: unsolicited trade reopen cannot own an idle or disconnected core")
{
    u32 session = 123, incarnation = 456;
    struct MultiplayerMenuScreen screen = Screen(1, 1);
    MultiplayerMenu_Init();
    EXPECT(!MultiplayerSave_GetTradeMenuBinding(&session, &incarnation));
    EXPECT_EQ(session, 123); EXPECT_EQ(incarnation, 456);
    screen.flags = 2; Deliver(&screen, 2);
    EXPECT(!MultiplayerMenu_IsActive());
    EXPECT(MultiplayerMenu_TestBegin());
    screen.ackRequest = 1; Deliver(&screen, 4);
    EXPECT_EQ(MultiplayerMenu_TestScreen(), 0);
    MultiplayerMenu_TestClose();
    screen.ackRequest = 2; Deliver(&screen, 6);
    EXPECT(!MultiplayerMenu_IsActive());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    MultiplayerMenu_Init();
}

TEST("Multiplayer menu: explicit resume Enter selection latches before IPC and ignores B, disconnect and ordinary CLOSE")
{
    struct MultiplayerMenuScreen screen = Screen(1, 10);
    bool8 resetDisabled = gSoftResetDisabled;
    MultiplayerMenu_Init(); EXPECT(MultiplayerMenu_TestBegin());
    screen.reserved[0] = 2;
    EXPECT(MultiplayerMenu_ValidateScreen(&screen));
    screen.rows[1].action = 0; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen.rows[1].action = 2;
    screen.reserved[0] = 7; EXPECT(!MultiplayerMenu_ValidateScreen(&screen)); screen.reserved[0] = 2;
    Deliver(&screen, 2);
    EXPECT(!MultiplayerMenu_IsBattleAdmissionHeld());
    MultiplayerMenu_TestInput(DPAD_DOWN); MultiplayerMenu_TestInput(A_BUTTON);
    EXPECT_EQ(gMultiplayerMenuMailbox.request.action, 2);
    EXPECT(MultiplayerMenu_IsBattleAdmissionHeld()); EXPECT(MultiplayerBattle_PauseClocks());
    EXPECT(MultiplayerBattle_SkipCallbacks()); EXPECT(gSoftResetDisabled);
    MultiplayerMenu_TestInput(B_BUTTON); MultiplayerMenu_Tick();
    EXPECT(MultiplayerMenu_IsActive()); EXPECT(MultiplayerMenu_IsBattleAdmissionHeld());
    EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    screen.ackRequest = 2; screen.flags = 1; screen.reserved[0] = 0;
    Deliver(&screen, 4); MultiplayerMenu_TestClose();
    EXPECT(MultiplayerMenu_IsActive()); EXPECT_EQ(gMultiplayerMenuMailbox.request.sequence, 2);
    EXPECT(!MultiplayerMenu_CanAdmitBattle(7, 11)); // Bare test Begin owns no production field/binding.
    MultiplayerMenu_Init(); EXPECT(!MultiplayerMenu_IsBattleAdmissionHeld());
    EXPECT_EQ(gSoftResetDisabled, resetDisabled);
}
