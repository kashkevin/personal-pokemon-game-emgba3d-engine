#include "global.h"
#include <stddef.h>
#include "multiplayer_menu.h"
#include "multiplayer.h"
#include "multiplayer_save.h"
#include "multiplayer_battle.h"
#include "multiplayer_ownership.h"
#include "main.h"
#include "menu.h"
#include "bg.h"
#include "window.h"
#include "text.h"
#include "text_window.h"
#include "script.h"
#include "event_object_lock.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "sound.h"
#include "constants/songs.h"

_Static_assert(sizeof(struct MultiplayerMenuRequest) == 16, "Menu request ABI");
_Static_assert(sizeof(struct MultiplayerMenuScreen) == 240, "Menu screen ABI");
_Static_assert(sizeof(struct MultiplayerMenuMailbox) == 512, "Menu mailbox ABI");
_Static_assert(offsetof(struct MultiplayerMenuMailbox, request) == 16, "Menu request offset");
_Static_assert(offsetof(struct MultiplayerMenuMailbox, inboundGeneration) == 32, "Menu inbound generation offset");
_Static_assert(offsetof(struct MultiplayerMenuMailbox, screen) == 36, "Menu screen offset");
_Static_assert(offsetof(struct MultiplayerMenuScreen, rows) == 68, "Menu row offset");

const u32 gMultiplayerMenuAbiVersion = MP_MENU_ABI;
#ifdef PORTABLE
#include "tower_legends.h"
const u32 gMultiplayerMenuReleaseAbiVersion = MP_MENU_RELEASE_ABI;
// Battle Tower: Legends (tower_legends.h): flags 4 start a solo floor, 5 open Hyper Training.
#define MENU_FLAGS_MAX MP_MENU_FLAG_HYPER_TRAINING
#else
#define MENU_FLAGS_MAX 2
#endif
#line 30 // gba-line
static EWRAM_DATA bool8 sBattleAdmissionLatched = FALSE;
static EWRAM_DATA bool8 sAdmissionResetDisabled = FALSE;
EWRAM_DATA volatile struct MultiplayerMenuMailbox gMultiplayerMenuMailbox = {0};
static EWRAM_DATA struct MultiplayerMenuScreen sMultiplayerMenuScreen;
static EWRAM_DATA u32 sSequence, sSeenGeneration, sSession, sIncarnation;
static EWRAM_DATA u8 sWindow, sMultiplayerMenuCursor;
static EWRAM_DATA bool8 sMultiplayerMenuActive, sMultiplayerMenuWaiting, sTradeMenu;
static const struct WindowTemplate sWindowTemplate = {
    .bg = 0, .tilemapLeft = 1, .tilemapTop = 1, .width = 28, .height = 18,
    .paletteNum = 15, .baseBlock = 8,
};
static const u8 sText_Title[] = _("Players");
static const u8 sText_Waiting[] = _("Waiting for server...");
static const u8 sText_Back[] = _("B: Back");
static const u8 sText_Close[] = _("B: Close");
static const u8 sText_Cursor[] = _(">");

static bool32 Zero(const u8 *bytes, u32 size)
{
    while (size--) if (*bytes++) return FALSE;
    return TRUE;
}

static bool32 ValidText(const u8 *text, u32 size)
{
    for (u32 i = 0; i < size; i++)
    {
        if (text[i] == 0) return Zero(text + i, size - i);
        if (text[i] < 32 || text[i] > 126) return FALSE;
    }
    return FALSE;
}

#ifdef PORTABLE
// Battle Tower: Legends: a Tower screen (flags 4 or 5) has no rows; flags 4 carry the solo command in the rows' bytes.
static bool32 ValidTowerScreen(const struct MultiplayerMenuScreen *screen)
{
    struct TowerLegendsCommand command;
    const u8 *rows = (const u8 *)screen->rows;

    if (screen->count != 0 || screen->reserved[0] != 0 || screen->reserved[1] != 0)
        return FALSE;
    if (screen->flags == MP_MENU_FLAG_HYPER_TRAINING)
        return Zero(rows, sizeof(screen->rows));
    memcpy(&command, rows, sizeof(command));
    return TowerLegends_ValidCommand(&command) && Zero(rows + sizeof(command), sizeof(screen->rows) - sizeof(command));
}
#endif

#line 63 // gba-line
bool32 MultiplayerMenu_ValidateScreen(const struct MultiplayerMenuScreen *screen)
{
    u32 admissionAction = screen->reserved[0] | screen->reserved[1] << 8;
#ifdef PORTABLE
    // Bytes 238-239: the native UI's Tower screen (style, argument); zero otherwise.
    if (!TowerLegends_ValidStyle(screen->reserved[2], screen->reserved[3]))
        return FALSE;
    if (screen->flags == MP_MENU_FLAG_TOWER_SOLO || screen->flags == MP_MENU_FLAG_HYPER_TRAINING)
        return screen->ackRequest && screen->screen && (screen->backAction == 0 || screen->backAction == 7)
            && ValidText(screen->title, sizeof(screen->title)) && ValidText(screen->status, sizeof(screen->status))
            && ValidTowerScreen(screen);
#endif
#line 66 // gba-line
    if (!screen->ackRequest || !screen->screen || screen->flags > MENU_FLAGS_MAX || screen->count > 6
     || (screen->backAction != 0 && screen->backAction != 7)
     || !ValidText(screen->title, sizeof(screen->title)) || !ValidText(screen->status, sizeof(screen->status))
#ifndef PORTABLE // PC: the style bytes were checked above
#line 69 // gba-line
     || !Zero(screen->reserved + 2, 2)
#endif
#line 70 // gba-line
     || (admissionAction && (*(const volatile u32 *)&gMultiplayerBattleResumeAbiVersion != MULTIPLAYER_BATTLE_RESUME_ABI
          || screen->flags || admissionAction > screen->count
          || screen->rows[admissionAction - 1].action != admissionAction))) return FALSE;
    for (u32 row = 0; row < 6; row++)
    {
        if (row >= screen->count)
        {
            if (!Zero((const void *)&screen->rows[row], sizeof(screen->rows[row]))) return FALSE;
        }
        else if ((screen->rows[row].action != 0 && screen->rows[row].action != row + 1)
              || !ValidText(screen->rows[row].label, sizeof(screen->rows[row].label))) return FALSE;
    }
    return TRUE;
}

static u8 Glyph(u8 c)
{
    if (c >= 'A' && c <= 'Z') return CHAR_A + c - 'A';
    if (c >= 'a' && c <= 'z') return CHAR_a + c - 'a';
    if (c >= '0' && c <= '9') return CHAR_0 + c - '0';
    switch (c)
    {
    case ' ': return CHAR_SPACE; case '!': return CHAR_EXCL_MARK;
    case '?': return CHAR_QUESTION_MARK; case '.': return CHAR_PERIOD;
    case '-': return CHAR_HYPHEN; case ',': return CHAR_COMMA;
    case '/': return CHAR_SLASH; case ':': return CHAR_COLON;
    case ';': return CHAR_SEMICOLON; case '+': return CHAR_PLUS;
    case '&': return CHAR_AMPERSAND; case '=': return CHAR_EQUALS;
    case '%': return CHAR_PERCENT; case '(': return CHAR_LEFT_PAREN;
    case ')': return CHAR_RIGHT_PAREN; case '<': return CHAR_LESS_THAN;
    case '>': return CHAR_GREATER_THAN; case '\'': return CHAR_SGL_QUOTE_RIGHT;
    case '"': return CHAR_DBL_QUOTE_RIGHT; case '*': return CHAR_MULT_SIGN;
    case '$': return CHAR_CURRENCY;
    default: return CHAR_QUESTION_MARK;
    }
}

static void PrintAscii(const u8 *ascii, u32 capacity, u32 x, u32 y, bool32 disabled)
{
    u8 text[34], colors[3] = {TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
    u32 length = 0;
    while (length + 1 < sizeof(text) && length < capacity && ascii[length])
    {
        text[length] = Glyph(ascii[length]); length++;
    }
    text[length] = EOS;
    if (disabled) colors[1] = TEXT_COLOR_LIGHT_GRAY;
    // The small font fits all bounded row labels. Long status text is split
    // by measured glyph width without accepting native control characters.
    if (y == 16 && GetStringWidth(FONT_SMALL, text, 0) > 208)
    {
        u32 split = length;
        while (split && GetStringWidth(FONT_SMALL, text, 0) > 208) text[--split] = EOS;
        AddTextPrinterParameterized4(sWindow, FONT_SMALL, x, y, 0, 0, colors, TEXT_SKIP_DRAW, text);
        for (u32 i = split; i < length; i++) text[i - split] = Glyph(ascii[i]);
        text[length - split] = EOS; y += 12;
    }
    AddTextPrinterParameterized4(sWindow, FONT_SMALL, x, y, 0, 0, colors, TEXT_SKIP_DRAW, text);
}

static void Render(void)
{
    if (sWindow == WINDOW_NONE) return;
    // Drawing the standard frame clears its pixel buffer. Do it before text.
    DrawStdWindowFrame(sWindow, FALSE);
    if (!sMultiplayerMenuScreen.screen)
        AddTextPrinterParameterized(sWindow, FONT_NORMAL, sText_Title, 8, 0, TEXT_SKIP_DRAW, NULL);
    else
        PrintAscii(sMultiplayerMenuScreen.title, sizeof(sMultiplayerMenuScreen.title), 8, 0, FALSE);
    if (sMultiplayerMenuWaiting)
        AddTextPrinterParameterized(sWindow, FONT_SMALL, sText_Waiting, 8, 16, TEXT_SKIP_DRAW, NULL);
    else
        PrintAscii(sMultiplayerMenuScreen.status, sizeof(sMultiplayerMenuScreen.status), 8, 16, FALSE);
    for (u32 row = 0; row < sMultiplayerMenuScreen.count; row++)
    {
        PrintAscii(sMultiplayerMenuScreen.rows[row].label, sizeof(sMultiplayerMenuScreen.rows[row].label), 16, 44 + row * 14, !sMultiplayerMenuScreen.rows[row].action);
        if (!sMultiplayerMenuWaiting && row == sMultiplayerMenuCursor)
            AddTextPrinterParameterized(sWindow, FONT_SMALL, sText_Cursor, 4, 44 + row * 14, TEXT_SKIP_DRAW, NULL);
    }
    AddTextPrinterParameterized(sWindow, FONT_SMALL, sMultiplayerMenuScreen.backAction ? sText_Back : sText_Close, 8, 130, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(sWindow, COPYWIN_FULL);
}

static void Publish(u8 kind, u16 action, u32 screen)
{
    struct MultiplayerMenuRequest request = {0};
    u32 generation = (gMultiplayerMenuMailbox.localGeneration + 1) | 1;
    request.sequence = ++sSequence; request.kind = kind; request.action = action; request.screen = screen;
    gMultiplayerMenuMailbox.localGeneration = generation;
    gMultiplayerMenuMailbox.request = request;
    gMultiplayerMenuMailbox.localGeneration = generation + 1;
}

static void Close(void)
{
    u32 screen = sMultiplayerMenuScreen.screen;
    if (!sMultiplayerMenuActive || (sBattleAdmissionLatched && !MultiplayerBattle_IsActive() && !MultiplayerBattle_IsAdmissionCleanupHeld())) return;
    sBattleAdmissionLatched = FALSE;
    if (sWindow != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sWindow, TRUE);
        RemoveWindow(sWindow); sWindow = WINDOW_NONE;
        ScriptUnfreezeObjectEvents(); UnlockPlayerFieldControls();
    }
    sMultiplayerMenuActive = sMultiplayerMenuWaiting = sTradeMenu = FALSE;
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen));
    // Publish only after window/field ownership has been released. Reserve the
    // final sequence for CLOSE; a ROM incarnation never wraps its requests.
    Publish(MP_MENU_CLOSE, 0, screen);
}

#ifdef PORTABLE
static u16 sOpenPage; // Update 32: the page the next OPEN names (MultiplayerMenu_OpenPage)
#endif

#line 181 // gba-line
static bool32 Begin(void)
{
    if (sMultiplayerMenuActive || sSequence >= 0xFFFFFFFEu) return FALSE;
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen)); sMultiplayerMenuCursor = 0;
    sSeenGeneration = gMultiplayerMenuMailbox.inboundGeneration;
    sMultiplayerMenuActive = sMultiplayerMenuWaiting = TRUE;
#ifdef PORTABLE
    Publish(MP_MENU_OPEN, sOpenPage, 0);
    sOpenPage = 0;
#else
#line 187 // gba-line
    Publish(MP_MENU_OPEN, 0, 0);
#endif
#line 188 // gba-line
    return TRUE;
}

void MultiplayerMenu_Init(void)
{
    if (sBattleAdmissionLatched) gSoftResetDisabled = sAdmissionResetDisabled;
    sBattleAdmissionLatched = FALSE;
    memset((void *)&gMultiplayerMenuMailbox, 0, sizeof(gMultiplayerMenuMailbox));
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen));
    sSequence = sSeenGeneration = sSession = sIncarnation = 0;
    sWindow = WINDOW_NONE; sMultiplayerMenuCursor = 0; sMultiplayerMenuActive = sMultiplayerMenuWaiting = sTradeMenu = FALSE;
    gMultiplayerMenuMailbox.abiVersion = *(const volatile u32 *)&gMultiplayerMenuAbiVersion;
    gMultiplayerMenuMailbox.byteSize = sizeof(gMultiplayerMenuMailbox);
    gMultiplayerMenuMailbox.magic = MP_MENU_MAGIC;
}

bool32 MultiplayerMenu_IsActive(void) { return sMultiplayerMenuActive; }
bool32 MultiplayerMenu_IsBattleAdmissionHeld(void) { return sBattleAdmissionLatched; }
bool32 MultiplayerMenu_AdmissionResetWasDisabled(void) { return sAdmissionResetDisabled; }

bool32 MultiplayerMenu_CanAdmitBattle(u32 session, u32 incarnation)
{
    // A normal production window owns the original safe field. The awaiting
    // CHOOSE response may transfer it; diagnostic Begin alone owns no field.
    return sMultiplayerMenuActive && sMultiplayerMenuWaiting && sBattleAdmissionLatched && sWindow != WINDOW_NONE
        && sSession == session && sIncarnation == incarnation;
}

void MultiplayerMenu_TransferBattleAdmission(u32 session, u32 incarnation)
{
    // The caller acquired the battle callback/clock hold first. Removing the
    // menu lock and publishing CLOSE cannot expose an unheld field frame.
    if ((MultiplayerBattle_IsActive() || MultiplayerBattle_IsAdmissionCleanupHeld()) && MultiplayerMenu_CanAdmitBattle(session, incarnation))
        Close();
}


bool32 MultiplayerMenu_Open(void)
{
    if (sMultiplayerMenuActive || sSequence >= 0xFFFFFFFEu || !Multiplayer_IsConnected()
     || !MultiplayerSave_FieldReady() || !MultiplayerOwnership_IsEnrolled()
     || !MultiplayerSave_GetIdleBinding(&sSession, &sIncarnation)) return FALSE;
    sWindow = AddWindow(&sWindowTemplate);
    if (sWindow == WINDOW_NONE) return FALSE;
    FreezeObjectEvents(); PlayerFreeze(); StopPlayerAvatar(); LockPlayerFieldControls();
    Begin(); Multiplayer_LeaveField(); Render();
    return TRUE;
}

#ifdef PORTABLE
const u32 gMultiplayerMenuOpenPageAbiVersion = MP_MENU_OPEN_PAGE_ABI;
const u32 gMultiplayerHostSavePageAbiVersion = MP_MENU_HOST_SAVE_PAGE_ABI; // MS-61: page 3, the in-game SAVE's host save

// Update 32: the Players menu opened on a page of its own (MP_MENU_PAGE_*): the companion shows that page first.
bool32 MultiplayerMenu_OpenPage(u16 page)
{
    bool32 opened;

    sOpenPage = page;
    opened = MultiplayerMenu_Open();
    sOpenPage = 0;
    return opened;
}
#endif

#ifdef PORTABLE
// The companion refused this Enter before sending any ADMIT (its own checks
// failed; nothing durable names the battle): the one screen a latched window
// takes. It ends the latch only while no battle or admission-cleanup hold
// exists, so the field this window owns was never transferred; the window then
// shows why, with its rows usable again (Cancel challenge, B: Back).
static bool32 ReleaseAdmission(const struct MultiplayerMenuScreen *screen)
{
    if (screen->flags != MP_MENU_FLAG_RELEASE || !MultiplayerMenu_ValidateScreen(screen)
     || screen->ackRequest != sSequence || screen->screen < sMultiplayerMenuScreen.screen
     || !sMultiplayerMenuActive || !sMultiplayerMenuWaiting || sWindow == WINDOW_NONE
     || MultiplayerBattle_IsActive() || MultiplayerBattle_IsAdmissionCleanupHeld())
        return FALSE;
    gSoftResetDisabled = sAdmissionResetDisabled;
    sBattleAdmissionLatched = FALSE;
    sMultiplayerMenuScreen = *screen;
    sMultiplayerMenuScreen.flags = 0;
    sMultiplayerMenuWaiting = FALSE;
    if (sMultiplayerMenuCursor >= sMultiplayerMenuScreen.count) sMultiplayerMenuCursor = 0;
    Render();
    return TRUE;
}
#endif

#line 237 // gba-line
static void ReadScreen(void)
{
    struct MultiplayerMenuScreen screen;
    u32 generation = gMultiplayerMenuMailbox.inboundGeneration;
#ifdef PORTABLE
    if ((generation & 1) || generation == sSeenGeneration) return;
#else
#line 241 // gba-line
    if (sBattleAdmissionLatched || (generation & 1) || generation == sSeenGeneration) return;
#endif
#line 242 // gba-line
    screen = gMultiplayerMenuMailbox.screen;
    if (generation != gMultiplayerMenuMailbox.inboundGeneration) return;
#ifdef PORTABLE
    // Latched: every other screen stays unread, as before; a release is read once.
    if (sBattleAdmissionLatched)
    {
        if (ReleaseAdmission(&screen)) sSeenGeneration = generation;
        return;
    }
#endif
#line 244 // gba-line
    sSeenGeneration = generation;
    if (!MultiplayerMenu_ValidateScreen(&screen) || screen.ackRequest != sSequence
     || screen.screen < sMultiplayerMenuScreen.screen) return;
    if (!sMultiplayerMenuActive)
    {
        // Reopen only following this exact native CLOSE, while the verified
        // trade exporter still owns the frozen party. Other field/save/battle
        // states cannot be made into a menu by an unsolicited screen packet.
        if (screen.flags != 2 || sSequence >= 0xFFFFFFFEu || gMultiplayerMenuMailbox.request.kind != MP_MENU_CLOSE
         || !Multiplayer_IsConnected() || MultiplayerBattle_IsActive()
         || !MultiplayerSave_GetTradeMenuBinding(&sSession, &sIncarnation)) return;
        sWindow = AddWindow(&sWindowTemplate);
        if (sWindow == WINDOW_NONE) return;
        FreezeObjectEvents(); PlayerFreeze(); StopPlayerAvatar(); LockPlayerFieldControls();
        if (!Begin()) { RemoveWindow(sWindow); sWindow = WINDOW_NONE; return; }
        sTradeMenu = TRUE;
        Render();
        return;
    }
    if (screen.flags == 2) return;
#ifdef PORTABLE
    if (screen.flags == MP_MENU_FLAG_RELEASE) return;   // a release for a latch that has already ended
    // Battle Tower: Legends: the host's answer to this window's choice closes it; once the field is calm the game starts
    // the solo floor (flags 4) or opens Hyper Training (flags 5).
    if (screen.flags == MP_MENU_FLAG_TOWER_SOLO || screen.flags == MP_MENU_FLAG_HYPER_TRAINING)
    {
        struct TowerLegendsCommand command;

        if (!sMultiplayerMenuWaiting || gMultiplayerMenuMailbox.request.kind != MP_MENU_CHOOSE)
            return;
        memcpy(&command, (const u8 *)screen.rows, sizeof(command));
        Close();
        if (screen.flags == MP_MENU_FLAG_TOWER_SOLO)
            TowerLegends_QueueSolo(&command);
        else
            TowerLegends_QueueHyperTraining();
        return;
    }
#endif
#line 264 // gba-line
    // A host-requested close may retain the last screen revision after an RPC
    // error. Ordinary duplicate revisions never re-enable old selections.
    if (screen.flags) { Close(); return; }
    if (screen.screen == sMultiplayerMenuScreen.screen) return;
    sMultiplayerMenuScreen = screen; sMultiplayerMenuWaiting = FALSE;
    if (sMultiplayerMenuCursor >= sMultiplayerMenuScreen.count) sMultiplayerMenuCursor = 0;
    Render();
}

static void Input(u16 keys)
{
    if (!sMultiplayerMenuActive || sBattleAdmissionLatched) return;
    if (keys & B_BUTTON)
    {
        if (!sMultiplayerMenuWaiting && sMultiplayerMenuScreen.backAction && sSequence < 0xFFFFFFFEu)
        { Publish(MP_MENU_CHOOSE, 7, sMultiplayerMenuScreen.screen); sMultiplayerMenuWaiting = TRUE; Render(); }
        else if (!sTradeMenu) Close();
    }
    else if (!sMultiplayerMenuWaiting)
    {
        if (keys & A_BUTTON)
        {
            if (sMultiplayerMenuCursor < sMultiplayerMenuScreen.count && sMultiplayerMenuScreen.rows[sMultiplayerMenuCursor].action)
            {
                if (sSequence >= 0xFFFFFFFEu) { Close(); return; }
                if (sMultiplayerMenuScreen.rows[sMultiplayerMenuCursor].action
                 == (sMultiplayerMenuScreen.reserved[0] | sMultiplayerMenuScreen.reserved[1] << 8))
                {
                    // Refuse BEFORE consuming Enter or publishing CHOOSE. A
                    // full tombstone set must not create an uncancellable hold.
                    // B/Close remains usable so the player can save normally.
                    if (!MultiplayerBattle_CanEnterAdmission())
                    {
                        memset(sMultiplayerMenuScreen.status, 0, sizeof(sMultiplayerMenuScreen.status));
                        memcpy(sMultiplayerMenuScreen.status, "Entry limit: save and rejoin", 27);
                        Render(); return;
                    }
                    sBattleAdmissionLatched = TRUE;
                    sAdmissionResetDisabled = gSoftResetDisabled; gSoftResetDisabled = TRUE;
                }
                Publish(MP_MENU_CHOOSE, sMultiplayerMenuScreen.rows[sMultiplayerMenuCursor].action, sMultiplayerMenuScreen.screen); sMultiplayerMenuWaiting = TRUE; Render();
            }
        }
        else if (sMultiplayerMenuScreen.count && (keys & (DPAD_UP | DPAD_DOWN)))
        {
            sMultiplayerMenuCursor = (sMultiplayerMenuCursor + ((keys & DPAD_UP) ? sMultiplayerMenuScreen.count - 1 : 1)) % sMultiplayerMenuScreen.count;
            PlaySE(SE_SELECT); Render();
        }
    }
}

void MultiplayerMenu_Tick(void)
{
    u32 session, incarnation;
    gMultiplayerMenuMailbox.frame++;
    if (!sMultiplayerMenuActive) { ReadScreen(); return; }
    if (!sBattleAdmissionLatched && (!Multiplayer_IsConnected() || MultiplayerBattle_IsActive()
     || (sTradeMenu ? !MultiplayerSave_GetTradeMenuBinding(&session, &incarnation)
                   : (MultiplayerSave_IsHeld() || !MultiplayerSave_GetIdleBinding(&session, &incarnation)))
     || session != sSession || incarnation != sIncarnation))
        Close();
    else
    {
        ReadScreen(); Input(gMain.newKeys);
    }
    // The closing B press must not also reach the resumed field callback.
    gMain.newKeys = gMain.newKeysRaw = gMain.newAndRepeatedKeys = 0;
    DoScheduledBgTilemapCopiesToVram();
}

#if TESTING
bool32 MultiplayerMenu_TestBegin(void) { return Begin(); }
void MultiplayerMenu_TestInput(u16 keys) { Input(keys); }
void MultiplayerMenu_TestReadScreen(void) { ReadScreen(); }
void MultiplayerMenu_TestClose(void) { Close(); }
u32 MultiplayerMenu_TestScreen(void) { return sMultiplayerMenuScreen.screen; }
u8 MultiplayerMenu_TestGlyph(u8 ascii) { return Glyph(ascii); }
#endif

#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI (src/native_ui/screens/ui_players.c) draws this menu in
// place of its window and follows this state; the input, the requests to the
// host and every rule stay here. Text is the host's bounded printable ASCII.
bool32 MultiplayerMenu_NativeUiState(struct NativeMpMenu *out)
{
    u32 admission;

    memset(out, 0, sizeof(*out));
    if (!sMultiplayerMenuActive || sWindow == WINDOW_NONE)
        return FALSE;
    out->waiting = sMultiplayerMenuWaiting;
    out->tradeMenu = sTradeMenu;
    out->admissionHeld = sBattleAdmissionLatched;
    out->cursor = sMultiplayerMenuCursor;
    out->revision = sMultiplayerMenuScreen.screen;
    out->count = sMultiplayerMenuScreen.count <= 6 ? sMultiplayerMenuScreen.count : 6;
    out->back = sMultiplayerMenuScreen.backAction != 0;
    memcpy(out->title, sMultiplayerMenuScreen.title, sizeof(sMultiplayerMenuScreen.title));
    memcpy(out->status, sMultiplayerMenuScreen.status, sizeof(sMultiplayerMenuScreen.status));
    admission = sMultiplayerMenuScreen.reserved[0] | sMultiplayerMenuScreen.reserved[1] << 8;
    out->towerStyle = sMultiplayerMenuScreen.reserved[2];
    out->towerArg = sMultiplayerMenuScreen.reserved[3];
    for (u32 row = 0; row < out->count; row++)
    {
        memcpy(out->rows[row], sMultiplayerMenuScreen.rows[row].label, sizeof(sMultiplayerMenuScreen.rows[row].label));
        out->enabled[row] = sMultiplayerMenuScreen.rows[row].action != 0;
        out->admission[row] = admission != 0 && sMultiplayerMenuScreen.rows[row].action == admission;
    }
    return TRUE;
}
#endif // ENABLE_NATIVE_UI
