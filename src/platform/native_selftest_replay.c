// Native PC build: self-test of what the replaying game of a link battle shows (any game but the link master: it never
// runs the battle engine, it replays the master's commands to its controllers, and its gBattleMons stay empty).
// Project-owned. Runs the game's own code on battle state built here, then exits.
//   --selftest-link-replay
//  - the switch-in message ("Go! {B_BUFF1}!") is printed before the switch-in animation, while the replaying game's
//    party index of the place is still the last Pokemon's: a disguised Zorua must be named as its Illusion shows it,
//    as the master names it (battle_message.c, STRINGID_SWITCHINMON);
//  - a Transformed Pokemon's picture reloaded (a menu closed, a Substitute faded) must keep the copied colours and
//    Transform's tint, as the transform animation drew it (battle_gfx_sfx_util.c, IsTransformedLook).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_gfx_sfx_util.h"
#include "battle_message.h"
#include "battle_util.h"
#include "load_save.h"
#include "malloc.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "string_util.h"
#include "util.h"
#include "constants/abilities.h"
#include "constants/battle_anim.h"
#include "constants/battle_string_ids.h"
#include "constants/characters.h"
#include "constants/rgb.h"
#include "constants/species.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
}

static bool32 Contains(const u8 *text, const u8 *word)
{
    u32 n = StringLength(word);

    for (; *text != EOS; text++)
    {
        if (memcmp(text, word, n) == 0)
            return TRUE;
    }
    return FALSE;
}

static void Nickname(struct Pokemon *mon, u8 *dst)
{
    GetMonData(mon, MON_DATA_NICKNAME, dst);
    StringGet_Nickname(dst);
}

static void MakeMon(struct Pokemon *mon, enum Species species, u32 personality)
{
    CreateMon(mon, species, 30, personality, OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(mon); // its HP: a Pokemon able to fight
}

// A link battle of two players, seen from the game of the player on the left (battler 0), the master or not.
static void LinkBattle(bool32 master)
{
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER | (master ? BATTLE_TYPE_IS_MASTER : 0);
    gBattlersCount = 2;
    gBattlerPositions[0] = B_POSITION_PLAYER_LEFT;
    gBattlerPositions[1] = B_POSITION_OPPONENT_LEFT;
    gBattlerBattleController[0] = BATTLE_CONTROLLER_PLAYER;
    gBattlerBattleController[1] = BATTLE_CONTROLLER_LINK_OPPONENT;
    memset(gBattleMons, 0, sizeof(gBattleMons));
}

// The switch-in message as a game prints it when the master sends it (PRINTSTRING): the master's buffer names the
// incoming Pokemon by its party index; `placeIndex` is the game's party index of the place at that moment.
static void SwitchInMessage(u32 incoming, u32 placeIndex, u8 *out)
{
    struct BattleMsgData *msg = (struct BattleMsgData *)&gBattleResources->bufferA[0][4];

    memset(msg, 0, sizeof(*msg));
    msg->scrActive = 0;
    msg->hpScale = 0; // "Go! {B_BUFF1}!"
    PREPARE_MON_NICK_BUFFER(msg->textBuffs[0], 0, incoming);
    gBattlerPartyIndexes[0] = placeIndex;
    gDisplayedStringBattle[0] = EOS;
    BufferStringBattle(STRINGID_SWITCHINMON, 0);
    StringCopy(out, gDisplayedStringBattle);
    printf("     \"");
    for (u32 i = 0; out[i] != EOS && i < 60; i++) // the message's letters, as text
    {
        u8 c = out[i];

        putchar(c >= CHAR_A && c <= CHAR_Z ? 'A' + (c - CHAR_A) : c >= CHAR_a && c <= CHAR_z ? 'a' + (c - CHAR_a)
                : c == CHAR_EXCL_MARK ? '!' : c == CHAR_SPACE ? ' ' : '?');
    }
    printf("\"\n");
}

static void CheckSwitchInMessage(void)
{
    struct Pokemon *party = gParties[B_TRAINER_PLAYER];
    u8 zorua[POKEMON_NAME_BUFFER_SIZE + 4], snorlax[POKEMON_NAME_BUFFER_SIZE + 4], pikachu[POKEMON_NAME_BUFFER_SIZE + 4];
    u8 replay[256], onMaster[256];
    u16 noHp = 0;

    ZeroPlayerPartyMons();
    MakeMon(&party[0], SPECIES_VOLTORB, 0x100);
    MakeMon(&party[1], SPECIES_ZORUA, 0x101);
    MakeMon(&party[2], SPECIES_PIKACHU, 0x102);
    MakeMon(&party[3], SPECIES_SNORLAX, 0x103);
    SetMonData(&party[0], MON_DATA_HP, &noHp); // fainted: the Zorua comes in for it
    Nickname(&party[1], zorua);
    Nickname(&party[2], pikachu);
    Nickname(&party[3], snorlax);
    Expect("the Zorua has Illusion", GetMonAbility(&party[1]) == ABILITY_ILLUSION);

    // The master: getswitchedmondata set the place's party index and the switch-in data forgot the last Pokemon's
    // Illusion before the message.
    LinkBattle(TRUE);
    ClearIllusionMon(0);
    SwitchInMessage(1, 1, onMaster);
    Expect("the master names the incoming Zorua as its Illusion (the party's last Pokemon, a Snorlax)",
           Contains(onMaster, snorlax) && !Contains(onMaster, zorua));

    // The replaying game: the place still holds the fainted Voltorb (and its Illusion state) until the animation.
    LinkBattle(FALSE);
    SetIllusionMon(&party[0], 0);
    SwitchInMessage(1, 0, replay);
    Expect("the replaying game names the incoming Zorua as its Illusion too (not \"Go! Zorua!\")",
           Contains(replay, snorlax) && !Contains(replay, zorua));
    Expect("... in the master's words", StringCompare(replay, onMaster) == 0);
    Expect("... and the Illusion name was for that message alone", gBattleScripting.illusionNickHack == 0);

    SwitchInMessage(2, 0, replay);
    Expect("the replaying game names an incoming Pokemon without Illusion by its own name",
           Contains(replay, pikachu) && !Contains(replay, snorlax));

    // Zorua the last Pokemon able to fight: no disguise, on either game.
    SetMonData(&party[2], MON_DATA_HP, &noHp);
    SetMonData(&party[3], MON_DATA_HP, &noHp);
    SwitchInMessage(1, 0, replay);
    LinkBattle(TRUE);
    ClearIllusionMon(0);
    SwitchInMessage(1, 1, onMaster);
    Expect("a Zorua that is the last Pokemon able to fight comes in as itself, on both games",
           Contains(replay, zorua) && StringCompare(replay, onMaster) == 0);
    ClearIllusionMon(0);
}

// The 16 colours of a battler's sprite palette as last loaded.
static void SpritePalette(u32 slot, u16 *out)
{
    memcpy(out, &gPlttBufferUnfaded[OBJ_PLTT_ID(slot)], PLTT_SIZE_4BPP);
}

// The palette a sprite of species/shininess/personality gets, with Transform's tint or without.
static void ExpectedPalette(enum Species species, bool32 shiny, u32 personality, bool32 tint, u16 *out)
{
    const u32 slot = 7;

    LoadPalette(GetMonSpritePalFromSpeciesAndPersonality(species, shiny, personality), OBJ_PLTT_ID(slot),
                PLTT_SIZE_4BPP);
    if (tint)
    {
        BlendPalette(OBJ_PLTT_ID(slot), 16, 6, RGB_WHITE);
        CpuCopy32(&gPlttBufferFaded[OBJ_PLTT_ID(slot)], &gPlttBufferUnfaded[OBJ_PLTT_ID(slot)], PLTT_SIZEOF(16));
    }
    SpritePalette(slot, out);
}

static void CheckTransformedReload(void)
{
    const u32 dittoPersonality = 0x2468ACE1, snorlaxPersonality = 0x13572468;
    struct Pokemon *ditto = &gParties[B_TRAINER_PLAYER][0];
    struct Pokemon *snorlax = &gParties[B_TRAINER_OPPONENT_A][0];
    bool32 shiny = TRUE, notShiny = FALSE;
    u16 drawn[16], reloaded[16], want[16];

    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    MakeMon(ditto, SPECIES_DITTO, dittoPersonality);
    SetMonData(ditto, MON_DATA_IS_SHINY, &shiny);
    MakeMon(snorlax, SPECIES_SNORLAX, snorlaxPersonality);
    SetMonData(snorlax, MON_DATA_IS_SHINY, &notShiny);
    Expect("a shiny Ditto against a Snorlax that is not shiny",
           GetMonData(ditto, MON_DATA_IS_SHINY) && !GetMonData(snorlax, MON_DATA_IS_SHINY));
    ResetSpriteData();
    AllocateBattleSpritesData();
    AllocateMonSpritesGfx();
    gBattlerSpriteIds[0] = 0;
    gBattlerSpriteIds[1] = 1;
    gBattlerPartyIndexes[0] = 0;
    gBattlerPartyIndexes[1] = 0;

    // The replaying game: the move animation of Transform (Imposter's too) brings the copied personality and
    // shininess (BtlController_HandleMoveAnimation) and AnimTask_TransformMon changes the picture.
    LinkBattle(FALSE);
    gTransformedPersonalities[0] = snorlaxPersonality;
    gTransformedShininess[0] = FALSE;
    HandleSpeciesGfxDataChange(0, 1, SPECIES_GFX_CHANGE_TRANSFORM);
    SpritePalette(0, drawn);
    ExpectedPalette(SPECIES_SNORLAX, FALSE, snorlaxPersonality, TRUE, want);
    Expect("the replaying game's transform animation draws the Snorlax's colours, tinted",
           memcmp(drawn, want, sizeof(want)) == 0);
    // ... and the picture reloaded as the battle screen comes back from a menu (reshow_battle_screen.c)
    BattleLoadMonSpriteGfx(GetBattlerMon(0), 0);
    SpritePalette(0, reloaded);
    Expect("reloaded on the replaying game, it keeps the Snorlax's colours and the tint (not a shiny Snorlax)",
           memcmp(reloaded, drawn, sizeof(drawn)) == 0);

    // The master is unchanged: gBattleMons says it.
    LinkBattle(TRUE);
    gBattleMons[0].volatiles.transformed = TRUE;
    BattleLoadMonSpriteGfx(GetBattlerMon(0), 0);
    SpritePalette(0, reloaded);
    Expect("reloaded on the master (transformed in gBattleMons), the same picture",
           memcmp(reloaded, drawn, sizeof(drawn)) == 0);

    // A form change is no Transform: the Pokemon's own colours, untinted, on the replaying game.
    LinkBattle(FALSE);
    HandleSpeciesGfxDataChange(0, 0, SPECIES_GFX_CHANGE_FORM_CHANGE);
    BattleLoadMonSpriteGfx(GetBattlerMon(0), 0);
    SpritePalette(0, reloaded);
    ExpectedPalette(SPECIES_DITTO, TRUE, dittoPersonality, FALSE, want);
    Expect("after a form change, reloaded on the replaying game: its own colours, untinted",
           memcmp(reloaded, want, sizeof(want)) == 0);

    // Sent out again (the switch-in clears the transform): its own colours.
    HandleSpeciesGfxDataChange(0, 1, SPECIES_GFX_CHANGE_TRANSFORM);
    ClearTemporarySpeciesSpriteData(0, FALSE, FALSE);
    BattleLoadMonSpriteGfx(GetBattlerMon(0), 0);
    SpritePalette(0, reloaded);
    LoadPalette(GetMonFrontSpritePal(ditto), OBJ_PLTT_ID(7), PLTT_SIZE_4BPP);
    SpritePalette(7, want);
    Expect("its transform cleared by a switch-in, reloaded on the replaying game: its own colours, untinted",
           memcmp(reloaded, want, sizeof(want)) == 0);

    FreeMonSpritesGfx();
    FreeBattleSpritesData();
}

int NativeSelfTest_LinkReplay(void)
{
    InitHeap(gHeap, HEAP_SIZE); // the message box's line breaking and the battle's sprite data allocate from it
    SetSaveBlocksPointers(0);
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gBattleStruct = calloc(1, sizeof(*gBattleStruct));
    gBattleResources = calloc(1, sizeof(*gBattleResources));
    if (gBattleStruct == NULL || gBattleResources == NULL)
        return 2;
    gMain.inBattle = TRUE;

    printf("the switch-in message of a disguised Zorua\n");
    CheckSwitchInMessage();
    printf("a Transformed Pokemon's picture reloaded\n");
    CheckTransformedReload();

    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;
    free(gBattleResources);
    gBattleResources = NULL;
    free(gBattleStruct);
    gBattleStruct = NULL;
    printf("selftest-link-replay: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
