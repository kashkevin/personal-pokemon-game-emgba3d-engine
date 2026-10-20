// Why the local player is off the field, for the other players' Players lists (include/multiplayer_presence.h).
// The owner's report of 2026-10-19: a player who entered a battle seemed to vanish; the session now says "In battle".
// Native PC build only: the GBA ROM build leaves this file out (Makefile), so the release ROM stays byte-identical.
// Read-only: nothing here changes what the game does. Project-owned.
#include "global.h"
#ifdef PORTABLE
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "multiplayer_menu.h"
#include "multiplayer_ownership.h"
#include "multiplayer_presence.h"
#include "multiplayer_save.h"
#include "multiplayer_world.h"
#include "platform/mp_bridge.h"
#include "fldeff.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "start_menu.h"
#include "task.h"

const u32 gMultiplayerPresenceDetailAbiVersion = MULTIPLAYER_PRESENCE_DETAIL_ABI;

#define REFUSAL_BANNER_FRAMES (60 * 5) // the banner waits this long for a calm field, then is dropped

static EWRAM_DATA const char *sRefusal = NULL;
static EWRAM_DATA u16 sRefusalFrames = 0;

// The callbacks that load a map or bring the field back: a warp (doors, stairs, elevators, Fly, Teleport, Dig, an
// Escape Rope), a whiteout, the return from a battle or a full-screen menu.
static bool32 MapLoading(MainCallback screen)
{
    return screen == CB2_LoadMap || screen == CB2_DoChangeMap || screen == CB2_WhiteOut
        || screen == CB2_ReturnToField || screen == CB2_ReturnToFieldContinueScript
        || screen == CB2_ReturnToFieldContinueScriptPlayMapMusic || screen == CB2_ReturnToFieldFadeFromBlack
        || screen == CB2_ReturnToFieldFromMultiplayer || screen == CB2_ReturnToFieldCableClub
        || screen == CB2_ReturnToFieldContestHall;
}

u8 MultiplayerPresence_Detail(void)
{
    MainCallback screen = gMain.callback2;

    if (gMain.inBattle || MultiplayerBattle_IsActive())
        return MULTIPLAYER_DETAIL_BATTLE;
    if (MapLoading(screen))
        return MULTIPLAYER_DETAIL_TRANSITION;
    // The START menu and PLAYERS draw over the field; the bag, the party, the PC and the other full-screen menus
    // replace its callback.
    if (MultiplayerMenu_IsActive() || FuncIsActiveTask(Task_ShowStartMenu) || screen == CB2_ReturnToFieldWithOpenMenu
     || (screen != NULL && screen != CB2_Overworld))
        return MULTIPLAYER_DETAIL_MENU;
    // A fade on the field: the screen goes dark before a warp and comes back after it.
    if (gPaletteFade.active)
        return MULTIPLAYER_DETAIL_TRANSITION;
    // Update 32: a dialogue, a cutscene, a save on the field. The player stands there as in a menu, and the other
    // players keep seeing them (they used to vanish for every sign read).
    return MULTIPLAYER_DETAIL_MENU;
}

// START > PLAYER > PLAYERS could not open (MultiplayerMenu_Open refused): say why in the field's banner. It used to
// close without a word, so a game that had lost its link looked like a world nobody else was in.
void MultiplayerPresence_PlayersRefused(void)
{
    if (MpBridge_LinkState() == MP_LINK_LOST)
        sRefusal = "Link to the world was lost.|See the launcher window.";
    else if (!MpBridge_IsEnabled())
        sRefusal = "PLAYERS needs a multiplayer|world from the launcher.";
    else if (!Multiplayer_IsConnected())
        sRefusal = "Not connected to the world.|See the launcher window.";
    else if (!MultiplayerOwnership_IsEnrolled())
        sRefusal = "This character is not|in a world yet.";
    else
        sRefusal = "PLAYERS is not ready yet.|Try again in a moment.";
    sRefusalFrames = 0;
}

void MultiplayerPresence_Tick(void)
{
    if (sRefusal == NULL)
        return;
    if (MultiplayerWorld_ShowLocalNotice(sRefusal) || ++sRefusalFrames >= REFUSAL_BANNER_FRAMES)
        sRefusal = NULL;
}
#endif // PORTABLE
