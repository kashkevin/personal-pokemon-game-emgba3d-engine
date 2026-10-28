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
