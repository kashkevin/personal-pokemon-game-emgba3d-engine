// Update 32: the soft reset (include/multiplayer_soft_reset.h); since MS-61 the reset key's, no menu row. Native PC
// build only: the GBA ROM build leaves this file out (Makefile), so the release ROM stays byte-identical.
// Project-owned.
#ifdef PORTABLE
#include <stdio.h>
#endif
#include "global.h"
#ifdef PORTABLE
#include "multiplayer_soft_reset.h"
#include "multiplayer_menu.h"
#include "multiplayer_presence.h"
#include "platform/mp_bridge.h"
#include "main.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "save.h"
#include "script.h"

extern const u8 EventScript_SoftResetConfirm[];

// A press waits at most this long for the field to take it: a step in progress reaches its tile's centre (where
// START is read too) within 16 frames. Anything that holds the field first - a menu, a script, a warp, a battle -
// drops the press at once, so a reset question never turns up later, after the player has moved on to something else.
#define KEY_WAIT_FRAMES 30

static u32 sKeyAge; // frames since the press, from 1; 0: none waiting

// The game's own log (stderr, the launcher's runtime.log): what the player chose (tools/pc_port/soft_reset_check.py).
static void Log(const char *text)
{
    fprintf(stderr, "%s\n", text);
    fflush(stderr);
}

void MultiplayerSoftReset_KeyPressed(void)
{
    sKeyAge = 1;
    Log("soft reset: key");
}

void MultiplayerSoftReset_KeyFrame(void)
{
    if (sKeyAge == 0)
        return;
    // Runs before this frame's callbacks: the press is from an earlier frame and nobody took it then. The START menu
    // takes it on the frame it arrives; the field waits only for a step to end.
    if (gMain.callback2 != CB2_Overworld || ArePlayerFieldControlsLocked() || gSoftResetDisabled
     || ++sKeyAge > KEY_WAIT_FRAMES)
    {
        sKeyAge = 0;
        Log("soft reset: key dropped (only in the field or the START menu)");
    }
}

bool32 MultiplayerSoftReset_TakeKey(void)
{
    if (sKeyAge == 0)
        return FALSE;
    sKeyAge = 0;
    if (gSoftResetDisabled)
    {
        Log("soft reset: key dropped (reset disabled here)");
        return FALSE;
    }
    Log("soft reset: key taken");
    return TRUE;
}

bool32 MultiplayerSoftReset_Begin(void)
{
    if (!MpBridge_IsEnabled())
    {
        Log("soft reset: asked (no world)");
        ScriptContext_SetupScript(EventScript_SoftResetConfirm);
        return TRUE;
    }
    if (!MpBridge_MenuOpenPageEnabled())
    {
        MultiplayerPresence_Refuse("Soft reset needs this game's|own launcher version.");
        return FALSE;
    }
    if (!MultiplayerMenu_OpenPage(MP_MENU_PAGE_SOFT_RESET))
    {
        u32 session, incarnation;

        MultiplayerPresence_PlayersRefused(); // says why (not connected, link lost, not ready yet)
        fprintf(stderr, "soft reset: Players refused (connected %d field %d enrolled %d idle %d menu %d locked %d "
                "script %d fade %d step %d)\n", Multiplayer_IsConnected(), MultiplayerSave_FieldReady(),
                MultiplayerOwnership_IsEnrolled(), MultiplayerSave_GetIdleBinding(&session, &incarnation),
                MultiplayerMenu_IsActive(), ArePlayerFieldControlsLocked(), ScriptContext_IsEnabled(),
                gPaletteFade.active, gPlayerAvatar.tileTransitionState);
        fflush(stderr);
        return FALSE;
    }
    Log("soft reset: Players opened on its page");
    return TRUE;
}

void MultiplayerSoftReset_Do(void)
{
    if (MpBridge_IsEnabled() || gSoftResetDisabled)
        return; // a world's reset is the launcher's; a disabled reset stays disabled
    Log("soft reset: resetting");
    DoSoftReset();
}

// MS-61: the host save an in-game SAVE asks for in a world. It waits for the free field (the save dialog has just
// closed, so normally the next frame), at most HOST_SAVE_WAIT_FRAMES: a game that cannot reach its world by then says
// so, and the launcher's autosave and Save now are still there.
#define HOST_SAVE_WAIT_FRAMES (60 * 10)

static u32 sHostSaveAge; // frames since the in-game SAVE (or the party change), from 1; 0: none waiting
static bool8 sHostSaveAuto; // the one waiting is the party watch's: given up silently

void MultiplayerHostSave_GameSaved(void)
{
    if (!MpBridge_HostSavePageEnabled())
        return; // no world, or a launcher without the host save page: the game's own save is all there is
    sHostSaveAge = 1;
    sHostSaveAuto = FALSE;
    Log("host save: in-game SAVE written; asking the launcher");
}

bool32 MultiplayerHostSave_TryOpen(void)
{
    if (sHostSaveAge == 0)
        return FALSE;
    if (!MpBridge_HostSavePageEnabled())
    {
        sHostSaveAge = 0;
        return FALSE;
    }
    if (MultiplayerMenu_OpenPage(MP_MENU_PAGE_HOST_SAVE))
    {
        sHostSaveAge = 0;
        Log("host save: Players opened on its page");
        return TRUE;
    }
    if (++sHostSaveAge > HOST_SAVE_WAIT_FRAMES)
    {
        sHostSaveAge = 0;
        if (sHostSaveAuto)
        {
            Log("host save: the world could not be reached; the party watch asks again later");
            return FALSE;
        }
        MultiplayerPresence_Refuse("Saved on this PC only.|See the launcher window.");
        Log("host save: the world could not be reached; saved on this PC only");
    }
    return FALSE;
}

// The owner's request (2026-10-22): no save is needed before a battle (include/multiplayer_soft_reset.h). The party
// as of the game's last save is known from the save counter: the game's first free frame is the save it loaded (the
// host's), and any save since (the launcher's export, an in-game SAVE, a trade's import) moves the counter.
//
// The owner's report (2026-10-23): saving whenever the party changed saved every minute or two (friendship rises every
// 128 steps, poison, HP and experience after any battle) and showed the old window each time. Every battle, the Tower
// and the rival gate start from the Players menu, so the host save now comes there: opening PLAYERS with a party that
// differs from the last save's asks for the host save first (page 3, which draws nothing), and the menu opens once the
// save is made - or after THEN_WAIT_FRAMES without one, as before (Enter then says why). Only a change of the party's
// Pokemon themselves (one received, caught, released, traded away or evolved: rare) is saved at once, from the free
// field, so a trainer who is challenged has the host save with that Pokemon (a challenge names the saves; a trainer
// whose host save has no Pokemon cannot be challenged). A save that is not made is asked for again after a wait that
// doubles each time, up to five minutes.
#define THEN_WAIT_FRAMES (60 * 15)
#define AUTO_SAVE_GAP_FRAMES (60 * 10)
#define AUTO_SAVE_GAP_MAX (60 * 60 * 5)

static bool8 sPartyKnown, sThenPending;
static u16 sThenPage;
static u32 sPartySaveCounter, sPartyCrc, sPartyMons, sThenCounter, sThenAge, sAutoSaveWait, sAutoSaveGap;

// The party's Pokemon as a word: each one's personality and species, and their count (HP, experience, friendship and
// the like do not count).
static u32 PartyMons(void)
{
    u32 i, sum = gPartiesCount[B_TRAINER_PLAYER];

    for (i = 0; i < gPartiesCount[B_TRAINER_PLAYER] && i < PARTY_SIZE; i++)
    {
        sum = sum * 2654435761u + GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_PERSONALITY);
        sum = sum * 2654435761u + GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES);
    }
    return sum;
}

void MultiplayerHostSave_PartyWatch(void)
{
    if (!MpBridge_HostSavePageEnabled())
        return;
    if (!sPartyKnown || gSaveCounter != sPartySaveCounter)
    {
        sPartyKnown = TRUE;
        sPartySaveCounter = gSaveCounter;
        sPartyCrc = MultiplayerBattle_PartyCrc32();
        sPartyMons = PartyMons();
        sAutoSaveWait = 0;
        sAutoSaveGap = AUTO_SAVE_GAP_FRAMES;
        return;
    }
    if (sAutoSaveWait != 0)
    {
        sAutoSaveWait--;
        return;
    }
    if (sHostSaveAge != 0 || sThenPending || PartyMons() == sPartyMons)
        return;
    sHostSaveAge = 1;
    sHostSaveAuto = TRUE;
    sAutoSaveWait = sAutoSaveGap;
    sAutoSaveGap = sAutoSaveGap * 2 > AUTO_SAVE_GAP_MAX ? AUTO_SAVE_GAP_MAX : sAutoSaveGap * 2;
    Log("host save: the party's Pokemon changed since the last save; asking the launcher");
}

bool32 MultiplayerHostSave_BeforePlayers(u16 page)
{
    if (!MpBridge_HostSavePageEnabled() || !sPartyKnown || sThenPending || sHostSaveAge != 0
     || gSaveCounter != sPartySaveCounter || MultiplayerBattle_PartyCrc32() == sPartyCrc)
        return FALSE;
    sHostSaveAge = 1;
    sHostSaveAuto = TRUE;
    sThenPending = TRUE;
    sThenPage = page;
    sThenCounter = gSaveCounter;
    sThenAge = 0;
    Log("host save: the party changed since the last save; saving before PLAYERS");
    return TRUE;
}

bool32 MultiplayerHostSave_TryThen(void)
{
    bool32 saved;

    if (!sThenPending || sHostSaveAge != 0)
        return FALSE;               // the host save page has not opened yet
    saved = gSaveCounter != sThenCounter;
    if (!saved && ++sThenAge <= THEN_WAIT_FRAMES)
        return FALSE;
    sThenPending = FALSE;
    Log(saved ? "host save: saved; PLAYERS opens" : "host save: no save came; PLAYERS opens anyway");
    if (sThenPage ? MultiplayerMenu_OpenPage(sThenPage) : MultiplayerMenu_Open())
        return TRUE;
    MultiplayerPresence_PlayersRefused();
    return FALSE;
}
#endif // PORTABLE
