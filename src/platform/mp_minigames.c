// Native PC build: KNOWN_ISSUES MS-141c, the wireless minigames with the world's players. Project-owned.
//
// Berry Crush (a Pokemon Center's Direct Corner) and Pokemon Jump and Dodrio Berry Picking (Mossdeep City's minigame
// building) need GBAs linked by the Wireless Adapter. With computer partners (MS-141) the group's link runs in memory;
// with the world's players it runs over the world server: the leader's game is the group's parent, each member's a
// child (src/link_rfu_2.c NetLink), and the bridge carries the link's frames and the lobby's requests to and from the
// companion (src/platform/mp_bridge.c, frames 'u' and 'm'), which sends them to the world server and back.
//
// The lobby - lead a group, join one, the players in it, start - is a page of the Players menu, which the companion
// draws (MP_MENU_PAGE_MINIGAME_*). The script opens it (OpenWorldMinigameLobby) and waits; this game says who it is
// (HELLO: its name, gender, trainer id, as a GBA's link player is shown to the others). When the group starts, the
// companion sends STARTED - every player's link player and this game's number in the group - then closes the menu;
// the script walks into the link room and the game starts as with computer partners (StartWirelessMinigameTask): the
// link's standby, now a real wait for every player, then the game. When the game closes the link, this game tells the
// companion it has left (LEAVE). If the group closes under a game being played (a player's game or connection is gone,
// or the leader's frames stop for 10 s), the game shows its own communication error and returns to the last save
// (NetLink_LinkLost), as on the GBA when the adapter loses its group; its LEAVE then says the link was lost, and the
// others' games are told the same.
#include "global.h"
#include <stdio.h>
#include <string.h>
#include "event_data.h"
#include "link.h"
#include "link_rfu.h"
#include "main.h"
#include "multiplayer.h"
#include "event_object_movement.h"
#include "multiplayer_battle.h"
#include "palette.h"
#include "overworld.h"
#include "multiplayer_menu.h"
#include "multiplayer_presence.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "script.h"
#include "string_util.h"
#include "task.h"
#include "union_room.h"
#include "constants/union_room.h"
#include "constants/characters.h"
#include "platform/mp_bridge.h"
#include "platform/mp_minigames.h"

#define REQUEST_QUEUE 4
// The Players menu opens on an idle save: after the save the attendant asks for, a friend's launcher first hands it to the
// host (a round trip), and the field may need a few frames to be calm; at most 10 s, then the menu's own refusal banner.
#define LOBBY_OPEN_FRAMES (60 * 10)

static u8 sRequests[REQUEST_QUEUE][2 + 1 + MP_MINIGAME_PLAYER_BYTES];
static u8 sRequestLength[REQUEST_QUEUE];
static u8 sRequestHead, sRequestCount;
static bool8 sStarted;     // STARTED came for the lobby now open
static bool8 sInGroup;     // a world group's link is up (or its game still runs)
static u8 sStartedActivity;
static u8 sStartedPlayers;
static u8 sStartedId;
static u8 sStartedLink[MAX_RFU_PLAYERS][MP_MINIGAME_PLAYER_BYTES];

static void QueueRequest(const u8 *data, u8 length)
{
    u8 slot;

    if (sRequestCount >= REQUEST_QUEUE)
        return;
    slot = (sRequestHead + sRequestCount) % REQUEST_QUEUE;
    memcpy(sRequests[slot], data, length);
    sRequestLength[slot] = length;
    sRequestCount++;
}

// Why this game leaves (MP_MINIGAME_LEAVE_*): its game closed the link at its end, as every player's game does (the
// others are not told); or its link was lost, or it left the lobby while its group may be starting - the others' games,
// which would wait for it, are told the group has ended.
static void QueueLeave(u8 reason)
{
    u8 leave[3] = {MP_MINIGAME_KIND_LOBBY, MP_MINIGAME_OP_LEAVE};

    leave[2] = reason;
    QueueRequest(leave, sizeof(leave));
    fprintf(stderr, "minigame: this game leaves its lobby or group (reason %u)\n", reason);
}

// This game's link player as the others' games show it: its name (the game's characters), gender, language, version
// and trainer id.
static void PackLocalPlayer(u8 *out)
{
    u32 trainerId = READ_OTID_FROM_SAVE;
    u16 version = gGameVersion + 0x4000;

    memset(out, 0, MP_MINIGAME_PLAYER_BYTES);
    StringCopyN(out, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1);
    out[8] = gSaveBlock2Ptr->playerGender;
    out[9] = GAME_LANGUAGE;
    out[10] = version;
    out[11] = version >> 8;
    out[12] = trainerId;
    out[13] = trainerId >> 8;
    out[14] = trainerId >> 16;
    out[15] = trainerId >> 24;
}

static void UnpackLinkPlayer(struct LinkPlayer *player, const u8 *in, u8 id)
{
    memset(player, 0, sizeof(*player));
    memcpy(player->name, in, PLAYER_NAME_LENGTH);
    player->name[PLAYER_NAME_LENGTH] = EOS;
    player->gender = in[8] & 1;
    player->language = in[9];
    player->version = in[10] | (in[11] << 8);
    player->trainerId = in[12] | (in[13] << 8) | (in[14] << 16) | ((u32)in[15] << 24);
    player->id = id;
}

static u8 PageActivity(u16 page)
{
    switch (page)
    {
    case MP_MENU_PAGE_MINIGAME_POKEMON_JUMP:
        return ACTIVITY_POKEMON_JUMP;
    case MP_MENU_PAGE_MINIGAME_DODRIO:
        return ACTIVITY_BERRY_PICK;
    default:
        return ACTIVITY_BERRY_CRUSH;
    }
}

// The bridge: the next frame for the companion. The link's frames first - once its game has closed the link, its last
// frames (the leader's closing frame, which the members' games wait for) still go out before LEAVE - then the lobby's
// requests, and LEAVE once a group's link has ended.
bool8 MpMinigames_Take(uint8_t *data, size_t *length)
{
    u8 frame[NETLINK_FRAME_BYTES];
    u8 size;

    if (NetLink_TakeOutbound(frame, &size))
    {
        data[0] = MP_MINIGAME_KIND_LINK;
        memcpy(data + 1, frame, size);
        *length = 1 + size;
        return TRUE;
    }
    if (sInGroup && !NetLink_IsActive())
    {
        sInGroup = FALSE;
        QueueLeave(NetLink_WasLost() ? MP_MINIGAME_LEAVE_LOST : MP_MINIGAME_LEAVE_ENDED);
    }
    if (sRequestCount == 0)
        return FALSE;
    memcpy(data, sRequests[sRequestHead], sRequestLength[sRequestHead]);
    *length = sRequestLength[sRequestHead];
    sRequestHead = (sRequestHead + 1) % REQUEST_QUEUE;
    sRequestCount--;
    return TRUE;
}

// The bridge: a frame from the companion.
void MpMinigames_Deliver(const uint8_t *data, size_t length)
{
    u8 i;

    if (length >= 2 && data[0] == MP_MINIGAME_KIND_LINK)
    {
        NetLink_PutInbound(data[1], data + 2, length - 2);
        return;
    }
    if (length < 2 || data[0] != MP_MINIGAME_KIND_LOBBY)
        return;
    switch (data[1])
    {
    case MP_MINIGAME_OP_STARTED:
        // [kind, op, activity, players, own id, players x link player]
        if (length < 5 || data[3] < 2 || data[3] > MAX_RFU_PLAYERS || data[4] >= data[3]
         || length != 5 + (size_t)data[3] * MP_MINIGAME_PLAYER_BYTES || NetLink_IsActive())
            return;
        sStartedActivity = data[2];
        sStartedPlayers = data[3];
        sStartedId = data[4];
        for (i = 0; i < sStartedPlayers; i++)
            memcpy(sStartedLink[i], data + 5 + i * MP_MINIGAME_PLAYER_BYTES, MP_MINIGAME_PLAYER_BYTES);
        sStarted = TRUE;
        fprintf(stderr, "minigame: STARTED, activity %u, %u players, this game player %u\n", sStartedActivity,
                sStartedPlayers, sStartedId);
        break;
    case MP_MINIGAME_OP_CLOSED:
        // During a group's game: the link is lost. Before it (the lobby): the companion's menu says so.
        fprintf(stderr, "minigame: CLOSED (reason %u), linked %d\n", length > 2 ? data[2] : 0, NetLink_IsActive());
        if (NetLink_IsActive())
        {
            sInGroup = FALSE;
            NetLink_LinkLost();
        }
        sStarted = FALSE;
        break;
    }
}

bool8 MpMinigames_InGroup(void)
{
    return sInGroup;
}

// The group starts: every player's link player from the companion, this game the leader (0) or a member, then the
// same walk into the room, standby and start as with computer partners.
static void StartGroup(void)
{
    u8 i;

    if (sStartedId == 0)
        NetLink_StartLeader(sStartedPlayers);
    else
        NetLink_StartMember(sStartedPlayers, sStartedId);
    for (i = 0; i < sStartedPlayers; i++)
        UnpackLinkPlayer(&gLinkPlayers[i], sStartedLink[i], i);
    sInGroup = TRUE;
    StartWirelessMinigameTask(sStartedActivity);
}

// Why the Players menu does not open yet (logged while the lobby waits for it): the field test's conditions.
static void LogLobbyWait(s16 frames)
{
    u32 session, incarnation;
    u32 id = gPlayerAvatar.objectEventId;

    fprintf(stderr, "minigame lobby: PLAYERS not open after %d frames: connected %d held %d menu %d battle %d cb1 %d "
            "cb2 %d fade %d locked %d script %d tile %d running %d link %d player %d enrolled %d idle save %d\n",
            frames, Multiplayer_IsConnected(), MultiplayerSave_IsHeld(), MultiplayerMenu_IsActive(),
            MultiplayerBattle_IsActive(), gMain.callback1 == CB1_Overworld, gMain.callback2 == CB2_Overworld,
            gPaletteFade.active, ArePlayerFieldControlsLocked(), ScriptContext_IsEnabled(),
            gPlayerAvatar.tileTransitionState, gPlayerAvatar.runningState, IsNativeLinkSessionActive(),
            id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active, MultiplayerOwnership_IsEnrolled(),
            MultiplayerSave_GetIdleBinding(&session, &incarnation));
}

// data[0] the state, data[1] the page, data[2] frames waited to open the menu
static void Task_WorldMinigameLobby(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    switch (data[0])
    {
    case 0:
        // The attendant's script waits for this task (waitstate), the field's controls still locked by it: the Players
        // menu opens only on a free field, so they are freed for the moment it opens (it locks them itself), and locked
        // again if it cannot open yet - the player stays at the counter either way. The player is held standing there,
        // whatever the avatar's last step said: a press toward the counter before A leaves it "moving" (a bump), and
        // the menu, which waits for a player standing still, never opened (found in the launcher proof).
        gPlayerAvatar.runningState = NOT_MOVING;
        UnlockPlayerFieldControls();
        if (MultiplayerMenu_OpenPage(data[1]))
        {
            data[0] = 1;
        }
        else if (++data[2] > LOBBY_OPEN_FRAMES)
        {
            LogLobbyWait(data[2]);
            LockPlayerFieldControls();
            MultiplayerPresence_PlayersRefused();
            QueueLeave(MP_MINIGAME_LEAVE_LOBBY);
            gSpecialVar_Result = FALSE;
            DestroyTask(taskId);
            ScriptContext_Enable();
        }
        else
        {
            if (data[2] % 60 == 0)
                LogLobbyWait(data[2]);
            LockPlayerFieldControls();
        }
        break;
    case 1:
        if (MultiplayerMenu_IsActive())
            break;
        LockPlayerFieldControls(); // the script goes on from here (into the room, or its goodbye)
        if (sStarted && sStartedActivity == PageActivity(data[1]))
        {
            sStarted = FALSE;
            StartGroup();
            gSpecialVar_Result = TRUE;
        }
        else
        {
            sStarted = FALSE;
            QueueLeave(MP_MINIGAME_LEAVE_LOBBY);
            gSpecialVar_Result = FALSE;
        }
        DestroyTask(taskId);
        ScriptContext_Enable();
        break;
    }
}

// A world in which this game can play the minigames with the world's players: connected, its companion takes the
// minigame frames and opens the Players menu on a page, and the menu can open (a save of this world, bound to it: the
// launcher's own world always is). Otherwise the attendant offers computer partners only, as before.
void IsWorldMinigameAvailable(void)
{
    gSpecialVar_Result = MpBridge_IsEnabled() && Multiplayer_IsConnected() && MpBridge_MenuOpenPageEnabled()
                      && MultiplayerOwnership_IsEnrolled() && MultiplayerSave_HasBinding() && !NetLink_IsActive();
}

// VAR_0x8005: the game (0 Berry Crush, 1 Pokemon Jump, 2 Dodrio Berry Picking). The script has released the player and
// waits (waitstate); VAR_RESULT afterwards: TRUE when the group started (the script walks into the room), FALSE when
// the player left the lobby.
void OpenWorldMinigameLobby(void)
{
    u8 hello[3 + MP_MINIGAME_PLAYER_BYTES];
    u8 taskId;
    u16 page = gSpecialVar_0x8005 == 1 ? MP_MENU_PAGE_MINIGAME_POKEMON_JUMP
             : gSpecialVar_0x8005 == 2 ? MP_MENU_PAGE_MINIGAME_DODRIO
                                       : MP_MENU_PAGE_MINIGAME_BERRY_CRUSH;

    sStarted = FALSE;
    hello[0] = MP_MINIGAME_KIND_LOBBY;
    hello[1] = MP_MINIGAME_OP_HELLO;
    hello[2] = PageActivity(page);
    PackLocalPlayer(hello + 3);
    QueueRequest(hello, sizeof(hello));
    taskId = CreateTask(Task_WorldMinigameLobby, 80);
    gTasks[taskId].data[1] = page;
}
