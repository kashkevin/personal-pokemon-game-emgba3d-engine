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
static EWRAM_DATA bool8 sBattleAdmissionLatched = FALSE;
static EWRAM_DATA bool8 sAdmissionResetDisabled = FALSE;
EWRAM_DATA volatile struct MultiplayerMenuMailbox gMultiplayerMenuMailbox = {0};
static EWRAM_DATA struct MultiplayerMenuScreen sMultiplayerMenuScreen;
static EWRAM_DATA u32 sSequence, sSeenGeneration, sSession, sIncarnation;
static EWRAM_DATA u8 sWindow, sMultiplayerMenuCursor;
static EWRAM_DATA bool8 sMultiplayerMenuActive, sMultiplayerMenuWaiting;
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

bool32 MultiplayerMenu_ValidateScreen(const struct MultiplayerMenuScreen *screen)
{
    u32 admissionAction = screen->reserved[0] | screen->reserved[1] << 8;
    if (!screen->ackRequest || !screen->screen || screen->flags > 1 || screen->count > 6
     || (screen->backAction != 0 && screen->backAction != 7)
     || !ValidText(screen->title, sizeof(screen->title)) || !ValidText(screen->status, sizeof(screen->status))
     || !Zero(screen->reserved + 2, 2)
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
    if (!sMultiplayerMenuActive || (sBattleAdmissionLatched && !MultiplayerBattle_IsActive())) return;
    sBattleAdmissionLatched = FALSE;
    if (sWindow != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sWindow, TRUE);
        RemoveWindow(sWindow); sWindow = WINDOW_NONE;
        ScriptUnfreezeObjectEvents(); UnlockPlayerFieldControls();
    }
    sMultiplayerMenuActive = sMultiplayerMenuWaiting = FALSE;
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen));
    // Publish only after window/field ownership has been released. Reserve the
    // final sequence for CLOSE; a ROM incarnation never wraps its requests.
    Publish(MP_MENU_CLOSE, 0, screen);
}

static bool32 Begin(void)
{
    if (sMultiplayerMenuActive || sSequence >= 0xFFFFFFFEu) return FALSE;
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen)); sMultiplayerMenuCursor = 0;
    sSeenGeneration = gMultiplayerMenuMailbox.inboundGeneration;
    sMultiplayerMenuActive = sMultiplayerMenuWaiting = TRUE;
    Publish(MP_MENU_OPEN, 0, 0);
    return TRUE;
}

void MultiplayerMenu_Init(void)
{
    if (sBattleAdmissionLatched) gSoftResetDisabled = sAdmissionResetDisabled;
    sBattleAdmissionLatched = FALSE;
    memset((void *)&gMultiplayerMenuMailbox, 0, sizeof(gMultiplayerMenuMailbox));
    memset(&sMultiplayerMenuScreen, 0, sizeof(sMultiplayerMenuScreen));
    sSequence = sSeenGeneration = sSession = sIncarnation = 0;
    sWindow = WINDOW_NONE; sMultiplayerMenuCursor = 0; sMultiplayerMenuActive = sMultiplayerMenuWaiting = FALSE;
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
    if (MultiplayerBattle_IsActive() && MultiplayerMenu_CanAdmitBattle(session, incarnation))
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

static void ReadScreen(void)
{
    struct MultiplayerMenuScreen screen;
    u32 generation = gMultiplayerMenuMailbox.inboundGeneration;
    if (sBattleAdmissionLatched || !sMultiplayerMenuActive || (generation & 1) || generation == sSeenGeneration) return;
    screen = gMultiplayerMenuMailbox.screen;
    if (generation != gMultiplayerMenuMailbox.inboundGeneration) return;
    sSeenGeneration = generation;
    if (!MultiplayerMenu_ValidateScreen(&screen) || screen.ackRequest != sSequence
     || screen.screen < sMultiplayerMenuScreen.screen) return;
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
        else Close();
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
    if (!sMultiplayerMenuActive) return;
    if (!sBattleAdmissionLatched && (!Multiplayer_IsConnected() || MultiplayerSave_IsHeld() || MultiplayerBattle_IsActive()
     || !MultiplayerSave_GetIdleBinding(&session, &incarnation)
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
