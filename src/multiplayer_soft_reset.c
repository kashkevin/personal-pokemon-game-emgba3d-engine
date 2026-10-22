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
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "overworld.h"
#include "palette.h"
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

static u32 sHostSaveAge; // frames since the in-game SAVE, from 1; 0: none waiting

void MultiplayerHostSave_GameSaved(void)
{
    if (!MpBridge_HostSavePageEnabled())
        return; // no world, or a launcher without the host save page: the game's own save is all there is
    sHostSaveAge = 1;
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
        MultiplayerPresence_Refuse("Saved on this PC only.|See the launcher window.");
        Log("host save: the world could not be reached; saved on this PC only");
    }
    return FALSE;
}
#endif // PORTABLE
