// Update 32: the soft reset of START > PLAYER > RESET (include/multiplayer_soft_reset.h). Native PC build only: the
// GBA ROM build leaves this file out (Makefile), so the release ROM stays byte-identical. Project-owned.
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
#include "script.h"

extern const u8 EventScript_SoftResetConfirm[];

// The game's own log (stderr, the launcher's runtime.log): what the player chose (tools/pc_port/soft_reset_check.py).
static void Log(const char *text)
{
    fprintf(stderr, "%s\n", text);
    fflush(stderr);
}

void MultiplayerSoftReset_Begin(void)
{
    if (!MpBridge_IsEnabled())
    {
        Log("soft reset: asked (no world)");
        ScriptContext_SetupScript(EventScript_SoftResetConfirm);
        return;
    }
    if (!MpBridge_MenuOpenPageEnabled())
    {
        MultiplayerPresence_Refuse("Soft reset needs this game's|own launcher version.");
        return;
    }
    if (!MultiplayerMenu_OpenPage(MP_MENU_PAGE_SOFT_RESET))
        MultiplayerPresence_PlayersRefused(); // says why (not connected, link lost, not ready yet)
    else
        Log("soft reset: Players opened on its page");
}

void MultiplayerSoftReset_Do(void)
{
    if (MpBridge_IsEnabled() || gSoftResetDisabled)
        return; // a world's reset is the launcher's; a disabled reset stays disabled
    Log("soft reset: resetting");
    DoSoftReset();
}
#endif // PORTABLE
