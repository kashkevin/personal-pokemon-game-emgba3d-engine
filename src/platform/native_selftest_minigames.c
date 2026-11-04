// Native PC build: KNOWN_ISSUES MS-141, the wireless minigames' computer partners (src/link_rfu_2.c). Project-owned.
// The PC has no Wireless Adapter: the game leads a group whose other members are computer partners, and the link's
// exchange of each frame runs in memory. Checked here through the link's own calls, a frame at a time as the main
// loop runs them (HandleLinkConnection, then the tasks): the group's size and the local player's id, the leader's
// command returned to it as player 0's, a partner's packet in its slot, standbys, blocks both ways (one piece and
// several, a partner's second block held until the leader has reset its first, no packet while a block is sent), the
// status icon not made, the closing of the link, and CloseLink ending a group its game did not close.
// Nothing is saved: run before the game starts; then it exits.
//   --selftest-minigame-partners
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "link.h"
#include "link_rfu.h"
#include "main.h"
#include "sprite.h"
#include "task.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

enum {
    PARTNERS_IDLE,
    PARTNERS_PACKET,     // partner 2 sends a packet
    PARTNERS_BLOCK,      // each partner sends its block once
    PARTNERS_PACKET_ALL, // every partner tries a packet (refused while its block is sent)
};

static u8 sPartnerMode;
static bool8 sBlockSent[MAX_RFU_PLAYERS];
static bool8 sPacketTaken[MAX_RFU_PLAYERS];
static u8 sPartnerBlock[MAX_RFU_PLAYERS][64];
static u16 sPartnerBlockSize[MAX_RFU_PLAYERS];
static const u16 sPacket[RFU_PACKET_SIZE] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666};

static void TestPartner(u8 id)
{
    u16 packet[RFU_PACKET_SIZE];

    switch (sPartnerMode)
    {
    case PARTNERS_PACKET:
        if (id == 2)
        {
            memcpy(packet, sPacket, sizeof(packet));
            packet[0] = 0x2002;
            LocalLink_PartnerSendPacket(id, packet);
        }
        break;
    case PARTNERS_BLOCK:
        if (!sBlockSent[id] && sPartnerBlockSize[id] != 0)
            sBlockSent[id] = LocalLink_PartnerSendBlock(id, sPartnerBlock[id], sPartnerBlockSize[id]);
        break;
    case PARTNERS_PACKET_ALL:
        sPacketTaken[id] = LocalLink_PartnerSendPacket(id, sPacket);
        break;
    }
}

static void RunFrame(void)
{
    HandleLinkConnection();
    RunTasks();
}

// Frames until `done` (at most `limit`); limit + 1 when it never was.
static u32 RunUntil(bool32 (*done)(void), u32 limit)
{
    u32 frames;

    for (frames = 0; frames < limit && !done(); frames++)
        RunFrame();
    return done() ? frames : limit + 1;
}

static u8 sWantedBlocks;

static bool32 BlocksArrived(void)
{
    return GetBlockReceivedStatus() == sWantedBlocks;
}

static bool32 LinkTaskFinished(void)
{
    return IsLinkTaskFinished();
}

static bool32 LinkClosed(void)
{
    return !gReceivedRemoteLinkPlayers;
}

static void ClearPartnerBlocks(void)
{
    memset(sBlockSent, 0, sizeof(sBlockSent));
    memset(sPartnerBlockSize, 0, sizeof(sPartnerBlockSize));
    memset(sPartnerBlock, 0, sizeof(sPartnerBlock));
}

int NativeSelfTest_MinigamePartners(void)
{
    u16 leaderBlock[1] = {7};
    u8 firstBlock[60];
    u32 frames, i;
    u16 readyNum;
    bool32 ok;

    printf("selftest-minigame-partners: the wireless minigames' computer partners (src/link_rfu_2.c)\n");
    ResetTasks();

    // The group: the local game leads 4 players, the other 3 computer partners.
    LocalLink_Start(4);
    LocalLink_SetPartnerFunc(TestPartner);
    Expect("a group of computer partners is active", LocalLink_IsActive());
    Expect("its size is 4 players", GetLinkPlayerCount() == 4);
    Expect("the local game is player 0, the leader", GetMultiplayerId() == 0);
    Expect("the other players count as received (what the minigames check first)", gReceivedRemoteLinkPlayers == TRUE);
    Expect("the link is the wireless one (what Berry Crush checks)", gWirelessCommType == 1);
    Expect("no link task runs", IsLinkTaskFinished());

    // The leader's packet comes back to it as player 0's on the next frame, as the adapter returns it.
    Rfu_SendPacket((void *)sPacket);
    Expect("the leader's packet waits in its send buffer", gSendCmd[0] == RFUCMD_SEND_PACKET);
    RunFrame();
    Expect("the next frame it is player 0's command",
           gRecvCmds[0][0] == RFUCMD_SEND_PACKET && memcmp(&gRecvCmds[0][1], sPacket, sizeof(sPacket)) == 0);
    Expect("and the send buffer is free again", gSendCmd[0] == 0);
    RunFrame();
    Expect("for that frame only", gRecvCmds[0][0] == 0);

    // A partner's packet arrives in its own slot.
    sPartnerMode = PARTNERS_PACKET;
    RunFrame();
    sPartnerMode = PARTNERS_IDLE;
    Expect("partner 2's packet is player 2's command",
           gRecvCmds[2][0] == RFUCMD_SEND_PACKET && gRecvCmds[2][1] == 0x2002 && gRecvCmds[2][2] == sPacket[1]);
    Expect("players 1 and 3 sent nothing", gRecvCmds[1][0] == 0 && gRecvCmds[3][0] == 0);
    Expect("and no fifth player", gRecvCmds[4][0] == 0);

    // A standby: the partners answer as a member's link layer does, the leader's own answer comes back to it.
    readyNum = gRfu.allReadyNum;
    SetLinkStandbyCallback();
    Expect("a standby is running", !IsLinkTaskFinished());
    frames = RunUntil(LinkTaskFinished, 60);
    printf("     standby: %u frames\n", frames);
    Expect("it ends within 4 frames", frames <= 4);
    Expect("and is counted once", gRfu.allReadyNum == readyNum + 1);
    SetLinkStandbyCallback();
    frames = RunUntil(LinkTaskFinished, 60);
    Expect("a second standby ends too", frames <= 4 && gRfu.allReadyNum == readyNum + 2);

    // Blocks: the leader's own and one from each partner (a 2-byte one, as Berry Crush's).
    ClearPartnerBlocks();
    for (i = 1; i < 4; i++)
    {
        sPartnerBlock[i][0] = 100 + i;
        sPartnerBlockSize[i] = 2;
    }
    sPartnerMode = PARTNERS_BLOCK;
    Expect("the leader's block starts", SendBlock(0, leaderBlock, sizeof(leaderBlock)));
    sWantedBlocks = 0x0F;
    frames = RunUntil(BlocksArrived, 60);
    printf("     4 blocks: %u frames\n", frames);
    Expect("all 4 players' blocks arrive within 12 frames", frames <= 12);
    Expect("player 0's is the leader's own", gBlockRecvBuffer[0][0] == 7);
    Expect("players 1 to 3 are the partners'",
           gBlockRecvBuffer[1][0] == 101 && gBlockRecvBuffer[2][0] == 102 && gBlockRecvBuffer[3][0] == 103);
    Expect("the leader's sending has ended", IsLinkTaskFinished());
    ResetBlockReceivedFlags();
    Expect("reset: none received", GetBlockReceivedStatus() == 0);

    // A block of several pieces (60 bytes, as Dodrio Berry Picking's results table).
    ClearPartnerBlocks();
    for (i = 0; i < 60; i++)
        sPartnerBlock[3][i] = i * 3 + 1;
    sPartnerBlockSize[3] = 60;
    sWantedBlocks = 1 << 3;
    frames = RunUntil(BlocksArrived, 60);
    printf("     60-byte block: %u frames\n", frames);
    Expect("partner 3's 60-byte block arrives (5 pieces)", frames <= 12);
    Expect("whole and in order", memcmp(gBlockRecvBuffer[3], sPartnerBlock[3], 60) == 0);

    // A partner's next block waits until the leader has reset the last one it received from that partner.
    memcpy(firstBlock, sPartnerBlock[3], sizeof(firstBlock));
    sBlockSent[3] = FALSE;
    memset(sPartnerBlock[3], 0, sizeof(sPartnerBlock[3]));
    sPartnerBlock[3][0] = 0xAB;
    sPartnerBlockSize[3] = 2;
    for (i = 0; i < 20; i++)
        RunFrame();
    Expect("a second block from partner 3 is held while the first is not reset",
           sBlockSent[3] && LocalLink_PartnerIsSendingBlock(3) && memcmp(gBlockRecvBuffer[3], firstBlock, 60) == 0);
    ResetBlockReceivedFlags();
    frames = RunUntil(BlocksArrived, 60);
    Expect("after the reset it arrives", frames <= 12 && gBlockRecvBuffer[3][0] == 0xAB);
    ResetBlockReceivedFlags();

    // While a partner sends a block its command is taken: a packet that frame is refused, as Rfu_SendPacket does
    // nothing while gSendCmd is in use.
    ClearPartnerBlocks();
    sPartnerBlockSize[1] = 24;
    sPartnerMode = PARTNERS_BLOCK;
    RunFrame(); // its block starts
    sPartnerMode = PARTNERS_PACKET_ALL;
    RunFrame();
    Expect("a packet is refused while the partner sends a block", !sPacketTaken[1] && LocalLink_PartnerIsSendingBlock(1));
    Expect("the other partners' packets go", sPacketTaken[2] && sPacketTaken[3] && gRecvCmds[2][0] == RFUCMD_SEND_PACKET);
    sPartnerMode = PARTNERS_IDLE;
    sWantedBlocks = 1 << 1;
    frames = RunUntil(BlocksArrived, 60);
    Expect("and its block arrives after", frames <= 12);
    ResetBlockReceivedFlags();

    // No status icon: computer partners have no signal to show, and none is destroyed.
    gWirelessStatusIndicatorSpriteId = 3;
    CreateWirelessStatusIndicatorSprite(0, 0);
    Expect("no wireless status icon is made", gWirelessStatusIndicatorSpriteId == SPRITE_NONE);
    DestroyWirelessStatusIndicatorSprite();
    Expect("destroying none does nothing", gWirelessStatusIndicatorSpriteId == SPRITE_NONE);

    // Closing the link, as the minigames close it: the partners answer, the group ends, the link is the cable's again.
    SetCloseLinkCallback();
    frames = RunUntil(LinkClosed, 120);
    printf("     close: %u frames\n", frames);
    Expect("the link closes within 10 frames", frames <= 10);
    Expect("the group has ended", !LocalLink_IsActive());
    Expect("the link is no longer the wireless one", gWirelessCommType == 0);
    Expect("no link callback left", gRfu.callback == NULL);

    // CloseLink (the rooms' exit script) ends a group its game did not close.
    LocalLink_Start(2);
    ok = LocalLink_IsActive() && GetLinkPlayerCount() == 2;
    CloseLink();
    Expect("CloseLink ends a group of 2 left open", ok && !LocalLink_IsActive() && gWirelessCommType == 0
                                                     && !gReceivedRemoteLinkPlayers);

    // A size out of range is a full group of 5.
    LocalLink_Start(9);
    Expect("a group of 9 asked is 5 players", GetLinkPlayerCount() == MAX_RFU_PLAYERS);
    LocalLink_End();
    Expect("ended", !LocalLink_IsActive() && gWirelessCommType == 0);

    printf("selftest-minigame-partners: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// KNOWN_ISSUES MS-141c: the same group with the world's players (src/link_rfu_2.c NetLink). One game is either the
// leader or a member, so each side is checked against peers played here: as the leader, members whose commands are put
// in as the network would deliver them; as a member, a leader whose frames are put in, echoing the member's own
// commands back in its slot as the leader's frames do. What goes out to the network is read as the bridge reads it.
//   --selftest-netlink

static u8 sNetOut[NETLINK_FRAME_BYTES];
static u8 sNetOutLength;

static bool32 TakeNetOut(void)
{
    return NetLink_TakeOutbound(sNetOut, &sNetOutLength);
}

static u16 NetOutWord(u8 player, u8 word)
{
    u16 index = (player * (CMD_LENGTH - 1) + word) * 2;

    return sNetOut[index] | (sNetOut[index + 1] << 8);
}

static void DrainNetOut(void)
{
    while (TakeNetOut())
        ;
}

static void PutMemberCommand(u8 member, const u16 *words)
{
    u8 bytes[NETLINK_CMD_BYTES];
    u8 i;

    for (i = 0; i < CMD_LENGTH - 1; i++)
    {
        bytes[i * 2] = words[i];
        bytes[i * 2 + 1] = words[i] >> 8;
    }
    NetLink_PutInbound(member, bytes, sizeof(bytes));
}

// A leader's frame for a member test: every player's command (rows of CMD_LENGTH - 1 words; NULL: an empty frame).
static void PutLeaderFrame(const u16 (*cmds)[CMD_LENGTH - 1])
{
    u8 bytes[NETLINK_FRAME_BYTES];
    u8 i;
    u8 j;

    if (cmds == NULL)
    {
        NetLink_PutInbound(0, bytes, 0);
        return;
    }
    for (i = 0; i < MAX_RFU_PLAYERS; i++)
        for (j = 0; j < CMD_LENGTH - 1; j++)
        {
            bytes[(i * (CMD_LENGTH - 1) + j) * 2] = cmds[i][j];
            bytes[(i * (CMD_LENGTH - 1) + j) * 2 + 1] = cmds[i][j] >> 8;
        }
    NetLink_PutInbound(0, bytes, sizeof(bytes));
}

// The member under test is player sEchoId: a leader frame echoing the member's last command (what the leader handled).
static u8 sEchoId;
static u16 sEcho[CMD_LENGTH - 1];
static u16 sLeaderOwn[CMD_LENGTH - 1];

static void RunMemberFrameWithEcho(void)
{
    u16 frame[MAX_RFU_PLAYERS][CMD_LENGTH - 1];
    u8 i;

    memset(frame, 0, sizeof(frame));
    memcpy(frame[0], sLeaderOwn, sizeof(sLeaderOwn));
    memcpy(frame[sEchoId], sEcho, sizeof(sEcho));
    PutLeaderFrame((const u16 (*)[CMD_LENGTH - 1])frame);
    RunFrame();
    memset(sEcho, 0, sizeof(sEcho));
    if (TakeNetOut() && sNetOutLength == NETLINK_CMD_BYTES)
        for (i = 0; i < CMD_LENGTH - 1; i++)
            sEcho[i] = sNetOut[i * 2] | (sNetOut[i * 2 + 1] << 8);
    DrainNetOut();
}

int NativeSelfTest_NetLink(void)
{
    static const u16 memberPacket[CMD_LENGTH - 1] = {RFUCMD_SEND_PACKET, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};
    static const u16 memberPacket2[CMD_LENGTH - 1] = {RFUCMD_SEND_PACKET, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6};
    u16 command[CMD_LENGTH - 1];
    u16 frame[MAX_RFU_PLAYERS][CMD_LENGTH - 1];
    u8 block[24];
    u32 frames, i, sent;
    u16 readyNum;
    bool32 ok;

    printf("selftest-netlink: the wireless minigames with the world's players (src/link_rfu_2.c NetLink)\n");
    ResetTasks();

    // ---- The leader of a world group of 3.
    NetLink_StartLeader(3);
    DrainNetOut();
    Expect("leader: a world group is active", NetLink_IsActive() && NetLink_IsLeader() && LocalLink_IsActive());
    Expect("leader: 3 players, the local game player 0", GetLinkPlayerCount() == 3 && GetMultiplayerId() == 0);
    Expect("leader: the wireless link, the others received", gWirelessCommType == 1 && gReceivedRemoteLinkPlayers);

    // Its own packet comes back as player 0's, and the frame it handled goes out at the next frame.
    Rfu_SendPacket((void *)sPacket);
    RunFrame();
    Expect("leader: its packet is player 0's command",
           gRecvCmds[0][0] == RFUCMD_SEND_PACKET && memcmp(&gRecvCmds[0][1], sPacket, sizeof(sPacket)) == 0);
    Expect("leader: nothing goes out before the frame is over", !TakeNetOut());
    RunFrame();
    ok = TakeNetOut() && sNetOutLength == NETLINK_FRAME_BYTES && NetOutWord(0, 0) == RFUCMD_SEND_PACKET
      && NetOutWord(0, 1) == sPacket[0] && NetOutWord(1, 0) == 0;
    Expect("leader: then the frame goes out, its packet in player 0's slot", ok);
    RunFrame();
    Expect("leader: an empty frame goes out too (as nothing)", TakeNetOut() && sNetOutLength == 0);
    DrainNetOut();

    // Members' commands go into their slots, one a frame each, in order.
    PutMemberCommand(1, memberPacket);
    PutMemberCommand(1, memberPacket2);
    PutMemberCommand(2, memberPacket2);
    RunFrame();
    Expect("leader: member 1's first command in slot 1, member 2's in slot 2",
           gRecvCmds[1][0] == RFUCMD_SEND_PACKET && gRecvCmds[1][1] == 0xA1 && gRecvCmds[2][1] == 0xB1);
    RunFrame();
    Expect("leader: member 1's second the next frame, member 2 nothing",
           gRecvCmds[1][1] == 0xB1 && gRecvCmds[2][0] == 0);
    // (the queue: the frame handled before these commands, then the one with them)
    ok = TakeNetOut() && sNetOutLength == 0 && TakeNetOut() && sNetOutLength == NETLINK_FRAME_BYTES
      && NetOutWord(1, 1) == 0xA1 && NetOutWord(2, 1) == 0xB1;
    Expect("leader: the members get the frame as the leader handled it", ok);
    Expect("leader: a command from a player outside the group is refused",
           !NetLink_PutInbound(3, (const u8 *)memberPacket, NETLINK_CMD_BYTES)
           && !NetLink_PutInbound(0, (const u8 *)memberPacket, NETLINK_CMD_BYTES));
    DrainNetOut();
    RunFrame();
    DrainNetOut();

    // A standby: the members answer as their link layer does; the leader's answer goes out to them.
    readyNum = gRfu.allReadyNum;
    SetLinkStandbyCallback();
    memset(command, 0, sizeof(command));
    command[0] = RFUCMD_READY_EXIT_STANDBY;
    command[1] = readyNum;
    PutMemberCommand(1, command);
    PutMemberCommand(2, command);
    frames = RunUntil(LinkTaskFinished, 60);
    printf("     leader standby: %u frames\n", frames);
    Expect("leader: the standby ends once both members are ready", frames <= 4 && gRfu.allReadyNum == readyNum + 1);
    RunFrame();
    for (ok = FALSE; TakeNetOut();)
        ok |= sNetOutLength && (NetOutWord(0, 0) & RFUCMD_MASK) == RFUCMD_READY_EXIT_STANDBY;
    Expect("leader: its own READY_EXIT_STANDBY went out to the members", ok);

    // A member's block (24 bytes, 2 pieces): its start, then its pieces.
    for (i = 0; i < sizeof(block); i++)
        block[i] = 0x40 + i;
    memset(command, 0, sizeof(command));
    command[0] = RFUCMD_SEND_BLOCK_INIT;
    command[1] = 2;
    command[2] = 0x81;
    PutMemberCommand(1, command);
    for (i = 0; i < 2; i++)
    {
        u8 j;

        memset(command, 0, sizeof(command));
        command[0] = RFUCMD_SEND_BLOCK | i;
        for (j = 0; j < 6; j++)
            command[j + 1] = block[i * 12 + j * 2] | (block[i * 12 + j * 2 + 1] << 8);
        PutMemberCommand(1, command);
    }
    sWantedBlocks = 1 << 1;
    frames = RunUntil(BlocksArrived, 30);
    Expect("leader: member 1's block arrives, whole", frames <= 6 && memcmp(gBlockRecvBuffer[1], block, 24) == 0);
    ResetBlockReceivedFlags();
    DrainNetOut();

    // The leader's block goes out in its slot: its start (three times, as a parent sends it), then its pieces.
    Expect("leader: its block starts", SendBlock(0, block, sizeof(block)));
    for (i = 0, sent = 0; i < 12; i++)
    {
        RunFrame();
        while (TakeNetOut())
            if (sNetOutLength && (NetOutWord(0, 0) & RFUCMD_MASK) == RFUCMD_SEND_BLOCK && NetOutWord(0, 1) != 0)
                sent |= 1 << (NetOutWord(0, 0) & 0xFF);
    }
    Expect("leader: both pieces of its block went out", sent == 3);
    ResetBlockReceivedFlags();

    // A member's commands queue at most half a second: the oldest packet makes room, a handshake stays.
    for (i = 0; i < 40; i++)
        PutMemberCommand(2, memberPacket);
    memset(command, 0, sizeof(command));
    command[0] = RFUCMD_READY_CLOSE_LINK;
    PutMemberCommand(2, command);
    for (i = 0, ok = FALSE; i < 40; i++)
    {
        RunFrame();
        ok |= (gRecvCmds[2][0] & RFUCMD_MASK) == RFUCMD_READY_CLOSE_LINK;
    }
    Expect("leader: a full queue drops its oldest packets, never the handshake after them",
           ok && NetLink_DroppedCount() > 0);
    DrainNetOut();
    gRfu.readyCloseLink[2] = FALSE;

    // Closing the link: the members answer, the group ends, and the leader's last frame still goes out.
    SetCloseLinkCallback();
    memset(command, 0, sizeof(command));
    command[0] = RFUCMD_READY_CLOSE_LINK;
    PutMemberCommand(1, command);
    PutMemberCommand(2, command);
    frames = RunUntil(LinkClosed, 120);
    printf("     leader close: %u frames\n", frames);
    Expect("leader: the link closes", frames <= 12 && !NetLink_IsActive() && gWirelessCommType == 0);
    for (ok = FALSE; TakeNetOut();)
        ok |= sNetOutLength && (NetOutWord(0, 0) & RFUCMD_MASK) == RFUCMD_READY_CLOSE_LINK;
    Expect("leader: its READY_CLOSE_LINK went out to the members", ok);
    Expect("leader: a link its game closed is not a lost one (its LEAVE says so)", !NetLink_WasLost());

    // ---- A member of a world group of 3: player 2.
    NetLink_StartMember(3, 2);
    DrainNetOut();
    Expect("member: a world group is active, not its leader", NetLink_IsActive() && !NetLink_IsLeader());
    Expect("member: 3 players, the local game player 2", GetLinkPlayerCount() == 3 && GetMultiplayerId() == 2);

    // Without the leader's frame nothing is handled and nothing goes out; its packet waits.
    Rfu_SendPacket((void *)sPacket);
    RunFrame();
    Expect("member: without a leader's frame nothing goes out", !TakeNetOut() && gSendCmd[0] == RFUCMD_SEND_PACKET);
    Expect("member: and nothing is received", gRecvCmds[0][0] == 0 && gRecvCmds[2][0] == 0);
    PutLeaderFrame(NULL);
    RunFrame();
    ok = TakeNetOut() && sNetOutLength == NETLINK_CMD_BYTES && (sNetOut[0] | (sNetOut[1] << 8)) == RFUCMD_SEND_PACKET
      && (sNetOut[2] | (sNetOut[3] << 8)) == sPacket[0];
    Expect("member: a leader's frame (empty) is answered with its packet", ok && gSendCmd[0] == 0);
    PutLeaderFrame(NULL);
    RunFrame();
    Expect("member: with nothing to send it sends nothing", !TakeNetOut());

    // The leader's frame is every player's command.
    memset(frame, 0, sizeof(frame));
    memcpy(frame[0], memberPacket, sizeof(memberPacket));
    memcpy(frame[1], memberPacket2, sizeof(memberPacket2));
    PutLeaderFrame((const u16 (*)[CMD_LENGTH - 1])frame);
    RunFrame();
    Expect("member: the leader's and member 1's packets are players 0's and 1's",
           gRecvCmds[0][1] == 0xA1 && gRecvCmds[1][1] == 0xB1 && gRecvCmds[2][0] == 0);
    RunFrame();
    Expect("member: for that frame only", gRecvCmds[0][0] == 0);
    Expect("member: a frame from a member, or of the wrong size, is refused",
           !NetLink_PutInbound(1, (const u8 *)frame, NETLINK_FRAME_BYTES)
           && !NetLink_PutInbound(0, (const u8 *)frame, NETLINK_CMD_BYTES));

    // Behind by 10 frames (the leader's packet numbering them): two a frame until 6 or fewer wait, then one.
    for (i = 1; i <= 10; i++)
    {
        memset(frame, 0, sizeof(frame));
        frame[0][0] = RFUCMD_SEND_PACKET;
        frame[0][1] = i;
        PutLeaderFrame((const u16 (*)[CMD_LENGTH - 1])frame);
    }
    RunFrame();
    ok = gRecvCmds[0][1] == 2;
    RunFrame();
    ok = ok && gRecvCmds[0][1] == 4;
    RunFrame();
    ok = ok && gRecvCmds[0][1] == 5;
    for (i = 0; i < 5; i++)
        RunFrame();
    Expect("member: 10 frames behind it handles two a frame, then one at 6 behind", ok && gRecvCmds[0][1] == 10);
    RunFrame();
    Expect("member: and is level with the leader again", gRecvCmds[0][0] == 0);
    DrainNetOut();

    // A standby: it answers when it handles the leader's frames; it ends when the leader's frame says all are ready.
    readyNum = gRfu.allReadyNum;
    sEchoId = 2;
    memset(sEcho, 0, sizeof(sEcho));
    memset(sLeaderOwn, 0, sizeof(sLeaderOwn));
    SetLinkStandbyCallback();
    RunMemberFrameWithEcho();
    Expect("member: it answers the standby", (sEcho[0] & RFUCMD_MASK) == RFUCMD_READY_EXIT_STANDBY);
    memset(frame, 0, sizeof(frame));
    for (i = 0; i < 3; i++)
    {
        frame[i][0] = RFUCMD_READY_EXIT_STANDBY;
        frame[i][1] = readyNum;
    }
    PutLeaderFrame((const u16 (*)[CMD_LENGTH - 1])frame);
    frames = RunUntil(LinkTaskFinished, 30);
    Expect("member: the standby ends when everyone's ready", frames <= 4 && gRfu.allReadyNum == readyNum + 1);
    DrainNetOut();

    // Its block: the start until it sees it echoed in its slot, then the pieces, then the last until echoed.
    for (i = 0; i < sizeof(block); i++)
        block[i] = 0x60 + i;
    Expect("member: its block starts", SendBlock(0, block, sizeof(block)));
    memset(sEcho, 0, sizeof(sEcho));
    for (i = 0, sent = 0; i < 30 && !IsLinkTaskFinished(); i++)
    {
        RunMemberFrameWithEcho();
        if ((sEcho[0] & RFUCMD_MASK) == RFUCMD_SEND_BLOCK)
            sent |= 1 << (sEcho[0] & 0xFF);
    }
    printf("     member block: %u frames\n", i);
    Expect("member: both pieces went out and its sending ended", sent == 3 && IsLinkTaskFinished());
    ResetBlockReceivedFlags();

    // The group closed under it: the game's communication error, the group gone.
    NetLink_LinkLost();
    Expect("member: a lost group ends the link with the game's communication error",
           !NetLink_IsActive() && NetLink_WasLost() && gMain.callback2 == CB2_LinkError);
    SetMainCallback2(NULL);
    gWirelessCommType = 0;

    // A new group starts clean; then its leader's frames stop: 10 s (600 frames) without one and the group is lost, as
    // the adapter loses a parent that stops answering.
    NetLink_StartMember(2, 1);
    Expect("member: a new group keeps nothing of the last one's", NetLink_IsActive() && !NetLink_WasLost() && !TakeNetOut());
    PutLeaderFrame(NULL);
    RunFrame();
    for (i = 0; i < 599 && NetLink_IsActive(); i++)
        RunFrame();
    ok = NetLink_IsActive();
    RunFrame();
    Expect("member: 10 s without a leader's frame loses the group (the communication error)",
           ok && !NetLink_IsActive() && NetLink_WasLost() && gMain.callback2 == CB2_LinkError);
    SetMainCallback2(NULL);
    gWirelessCommType = 0;

    // Closing the link as a member: its READY_CLOSE_LINK goes out; the leader's last frame says every player is ready;
    // the member's link then ends as the leader's does (a GBA child leaves that to its adapter, which the PC lacks:
    // the member waited for frames that never came and showed the communication error - the launcher proof's run 11).
    NetLink_StartMember(2, 1);
    sEchoId = 1;
    memset(sEcho, 0, sizeof(sEcho));
    memset(sLeaderOwn, 0, sizeof(sLeaderOwn));
    SetCloseLinkCallback();
    for (i = 0, ok = FALSE; i < 10 && !ok; i++)
    {
        RunMemberFrameWithEcho();
        ok = (sEcho[0] & RFUCMD_MASK) == RFUCMD_READY_CLOSE_LINK;
    }
    Expect("member: its READY_CLOSE_LINK goes out", ok);
    memset(frame, 0, sizeof(frame));
    frame[0][0] = frame[1][0] = RFUCMD_READY_CLOSE_LINK;
    PutLeaderFrame((const u16 (*)[CMD_LENGTH - 1])frame);
    frames = RunUntil(LinkClosed, 60);
    printf("     member close: %u frames\n", frames);
    Expect("member: every player ready, its link ends (not lost, no communication error)",
           frames <= 10 && !NetLink_IsActive() && !NetLink_WasLost() && gWirelessCommType == 0
           && gMain.callback2 != CB2_LinkError);

    printf("selftest-netlink: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
