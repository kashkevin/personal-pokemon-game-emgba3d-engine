#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_battle_schema.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_message.h"
#include "battle_anim.h"
#include "pokemon.h"
#include "string_util.h"
#include "constants/songs.h"
#include "constants/battle_anim.h"
#include "constants/characters.h"
#ifdef PORTABLE
#include "platform/battle_stat_stages.h"
#endif
#line 13 // gba-line

_Static_assert(sizeof(struct BattlePokemon) == 140, "Native battle mon schema");
_Static_assert(sizeof(struct BattleMsgData) == 76, "Native message schema");
_Static_assert(sizeof(struct ChooseMoveStruct) == 40, "Native move choice schema");
_Static_assert(sizeof(struct LinkBattleAnim) == 8, "Native animation schema");
_Static_assert(sizeof(struct MovePPInfo) == 16, "Native PP schema");
_Static_assert(sizeof(struct HpAndStatus) == 8, "Native party summary schema");
_Static_assert(REQUEST_STATUS_BATTLE == 40 && REQUEST_HP_BATTLE == 42, "Native setter IDs");
_Static_assert(CONTROLLER_ENDLINKBATTLE == 50 && CONTROLLER_DATATRANSFER == 28, "Native command IDs");

static EWRAM_DATA u8 sPending[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA u8 sDone[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA bool8 sReplied[MAX_BATTLERS_COUNT] = {0};

// Battlers on the field and the cores exchanging controller blocks differ in
// co-op Gym battles (four battlers, two cores; the master owns both AI foes).
static u32 BattlerCount(void) { return MultiplayerBattle_GetBattlerCount(); }
static u32 CoreCount(void) { return MultiplayerBattle_GetPlayerCount(); }
static u32 Owner(u32 battler) { return MultiplayerBattle_GetBattlerOwner(battler); }

static u16 Half(const u8 *p) { return p[0] | p[1] << 8; }
static u32 Word(const u8 *p) { return Half(p) | (u32)Half(p + 2) << 16; }
static bool32 Zero(const u8 *p, u32 n)
{
    while (n--) if (*p++) return FALSE;
    return TRUE;
}
static bool32 StringValid(const u8 *p, u32 size)
{
    while (size--)
    {
        if (*p == EOS) return TRUE;
        // Names/text cannot introduce control sequences or nested placeholders.
        if (*p >= 0xFC) return FALSE;
        p++;
    }
    return FALSE;
}

static bool32 TextBufferValid(const u8 *p)
{
    u32 i = 1, output = 0, n, value;
    if (p[0] != B_BUFF_PLACEHOLDER_BEGIN)
        return StringValid(p, TEXT_BUFF_ARRAY_COUNT);
    while (i < TEXT_BUFF_ARRAY_COUNT)
    {
        u8 token = p[i];
        if (token == B_BUFF_EOS) return output < sizeof(gStringVar1);
        n = 0;
        switch (token)
        {
        case B_BUFF_STRING:
            n = 3;
            if (i + n >= TEXT_BUFF_ARRAY_COUNT) return FALSE;
            value = Half(p + i + 1);
            if (value < STRINGID_TABLE_START || value >= STRINGID_COUNT || !gBattleStringsTable[value]) return FALSE;
            // ROM strings are trusted but concatenation still has a finite output.
            output += StringLength(gBattleStringsTable[value]);
            break;
        case B_BUFF_NUMBER:
            if (i + 3 >= TEXT_BUFF_ARRAY_COUNT || (p[i + 1] != 1 && p[i + 1] != 2 && p[i + 1] != 4)
             || !p[i + 2] || p[i + 2] > 10) return FALSE;
            n = 3 + p[i + 1]; output += p[i + 2]; break;
        case B_BUFF_MOVE: case B_BUFF_SPECIES: case B_BUFF_ABILITY: case B_BUFF_ITEM:
            n = 3;
            if (i + n >= TEXT_BUFF_ARRAY_COUNT) return FALSE;
            value = Half(p + i + 1);
            if ((token == B_BUFF_MOVE && value >= MOVES_COUNT_ALL)
             || (token == B_BUFF_SPECIES && value >= NUM_SPECIES)
             || (token == B_BUFF_ABILITY && value >= ABILITIES_COUNT)
             || (token == B_BUFF_ITEM && value >= ITEMS_COUNT)) return FALSE;
            output += 64; break;
        case B_BUFF_MON_NICK_WITH_PREFIX: case B_BUFF_MON_NICK_WITH_PREFIX_LOWER: case B_BUFF_MON_NICK:
            n = 3;
            if (i + n >= TEXT_BUFF_ARRAY_COUNT || p[i + 1] >= BattlerCount() || p[i + 2] >= PARTY_SIZE) return FALSE;
            output += 32; break;
        case B_BUFF_TYPE: case B_BUFF_STAT: case B_BUFF_NEGATIVE_FLAVOR:
            n = 2;
            if (i + n >= TEXT_BUFF_ARRAY_COUNT || (token == B_BUFF_TYPE && p[i + 1] >= NUMBER_OF_MON_TYPES)
             || (token == B_BUFF_STAT && p[i + 1] >= NUM_BATTLE_STATS)
             || (token == B_BUFF_NEGATIVE_FLAVOR && p[i + 1] >= 5)) return FALSE;
            output += 32; break;
        default: return FALSE;
        }
        if (i + n >= TEXT_BUFF_ARRAY_COUNT || output >= sizeof(gStringVar1)) return FALSE;
        i += n;
    }
    return FALSE;
}

static bool32 MessageValid(const u8 *p)
{
    struct BattleMsgData msg;
    u32 i;
    memcpy(&msg, p + 4, sizeof(msg));
    if (Half(p + 2) >= STRINGID_COUNT || Half(p + 2) == STRINGID_TRAINERSLIDE
     || (Half(p + 2) >= STRINGID_TABLE_START && !gBattleStringsTable[Half(p + 2)])
     || msg.currentMove >= MOVES_COUNT_ALL || msg.originallyUsedMove >= MOVES_COUNT_ALL
     || msg.lastItem >= ITEMS_COUNT || msg.lastAbility >= ABILITIES_COUNT
     || msg.scrActive >= BattlerCount() || msg.bakScriptPartyIdx >= PARTY_SIZE
     || msg.itemEffectBattler >= BattlerCount() || msg.moveType >= NUMBER_OF_MON_TYPES) return FALSE;
    for (i = 0; i < MAX_BATTLERS_COUNT; i++) if (msg.abilities[i] >= ABILITIES_COUNT) return FALSE;
    for (i = 0; i < 3; i++) if (!TextBufferValid(msg.textBuffs[i])) return FALSE;
    return TRUE;
}

static bool32 MoveChoiceValid(const u8 *p)
{
    struct ChooseMoveStruct move;
    u32 i;
    memcpy(&move, p + 4, sizeof(move));
#if MP_REMOTE_GIMMICK_ENABLED
    // Byte 3: 0, or the battler's usable Mega Evolution (MP_REMOTE_GIMMICK_MARK | GIMMICK_MEGA, battle_gimmick.c).
    if (p[1] != MultiplayerBattle_IsNativeDouble() || p[2] > 1 || (p[3] && p[3] != (MP_REMOTE_GIMMICK_MARK | GIMMICK_MEGA))
     || move.species >= NUM_SPECIES || move.zmove.healReplacement > PARTY_SIZE) return FALSE;
#else
#line 124 // gba-line
    if (p[1] != MultiplayerBattle_IsNativeDouble() || p[2] > 1 || p[3] || move.species >= NUM_SPECIES || move.zmove.healReplacement > PARTY_SIZE) return FALSE;
#endif
#line 125 // gba-line
    for (i = 0; i < MAX_MON_MOVES; i++)
        if (move.moves[i] >= MOVES_COUNT_ALL || move.currentPP[i] > move.maxPP[i]
         || move.zmove.baseMoves[i] >= MOVES_COUNT_ALL || (move.zmove.possibleZMoves[i] & ~15)) return FALSE;
    for (i = 0; i < 3; i++) if (move.monTypes[i] >= NUMBER_OF_MON_TYPES) return FALSE;
    return TRUE;
}

static bool32 MonReplyValid(const u8 *p)
{
    struct BattlePokemon mon;
    u32 i;
    memcpy(&mon, p + 4, sizeof(mon));
    if (p[1] != CONTROLLER_DATATRANSFER || Half(p + 2) != sizeof(mon)
     || !mon.species || mon.species >= NUM_SPECIES || mon.item >= ITEMS_COUNT
     || mon.ability >= ABILITIES_COUNT || !mon.level || mon.level > MAX_LEVEL || mon.hp > mon.maxHP
     || !StringValid(mon.nickname, sizeof(mon.nickname)) || !StringValid(mon.otName, sizeof(mon.otName))) return FALSE;
    for (i = 0; i < MAX_MON_MOVES; i++) if (mon.moves[i] >= MOVES_COUNT_ALL) return FALSE;
    for (i = 0; i < 3; i++) if (mon.types[i] >= NUMBER_OF_MON_TYPES) return FALSE;
    for (i = 0; i < NUM_BATTLE_STATS; i++) if (mon.statStages[i] < 0 || mon.statStages[i] > 12) return FALSE;
    return TRUE;
}

static bool32 AnimValid(const u8 *p)
{
    struct LinkBattleAnim anim;
    memcpy(&anim, p, sizeof(anim));
    return anim.rolloutTimer <= 5 && anim.furyCutterCounter <= 5 && anim.stockpileCounter <= 3
        && !(p[6] & 0xC0) && !p[7];
}

u16 MultiplayerBattle_ControllerRawSize(u8 command, u8 request)
{
    switch (command)
    {
#define SIZE(id, length) case id: return length;
    MULTIPLAYER_MASTER_CONTROLLER_SCHEMA(SIZE)
#undef SIZE
    case CONTROLLER_SETMONDATA:
        switch (request)
        {
#define FIELD(id, length) case id: return 3 + length;
        MULTIPLAYER_SET_FIELD_SCHEMA(FIELD)
#undef FIELD
        default: return 0;
        }
    case CONTROLLER_DATATRANSFER: return 144;
    case CONTROLLER_TWORETURNVALUES: return 6;
    case CONTROLLER_CHOSENMONRETURNVALUE: return 5;
#ifdef PORTABLE
    // the link master's stat stages for every battler (platform/battle_stat_stages.c; the app's route knows it too)
    case CONTROLLER_PC_STAT_STAGES: return PC_STAT_STAGES_RAW_SIZE;
#endif
#line 173 // gba-line
    default: return 0;
    }
}

static bool32 MasterDataValid(const u8 *p)
{
    u32 i, value;
    switch (p[0])
    {
    case CONTROLLER_GETMONDATA:
        return p[1] == REQUEST_ALL_BATTLE && !(p[2] & ~63) && (!p[2] || !(p[2] & (p[2] - 1))) && !p[3];
    case CONTROLLER_SETMONDATA:
        if (p[2] & ~63) return FALSE;
        if (p[1] == REQUEST_SPECIES_BATTLE) return Half(p + 3) < NUM_SPECIES;
        if (p[1] == REQUEST_HELDITEM_BATTLE) return Half(p + 3) < ITEMS_COUNT;
        if (p[1] == REQUEST_MOVES_PP_BATTLE)
        {
            for (i = 0; i < MAX_MON_MOVES; i++) if (Half(p + 3 + i * 2) >= MOVES_COUNT_ALL) return FALSE;
            return Zero(p + 16, 3); // MovePPInfo's trailing padding.
        }
        return TRUE;
    case CONTROLLER_SWITCHINANIM: return p[1] < PARTY_SIZE && p[2] <= 1 && p[3] <= 1;
    case CONTROLLER_RETURNMONTOBALL: return p[1] <= 1;
    case CONTROLLER_MOVEANIMATION: return Half(p + 1) < MOVES_COUNT_ALL && !p[14] && !p[15] && AnimValid(p + 16);
    case CONTROLLER_PRINTSTRING: case CONTROLLER_PRINTSTRINGPLAYERONLY: return MessageValid(p);
    case CONTROLLER_CHOOSEACTION:
#if defined(PORTABLE) || TESTING
        // KNOWN_ISSUES MS-57: the master sends every battler's action menu with battler 0's action of the turn
        // (battle_main.c: gChosenActionByBattler[0], as the original link battles do), and once battler 0 is out of
        // Pokemon while the battle goes on (the host's in a free-for-all, a 2v2 or a co-op battle) that is
        // B_ACTION_NOTHING_FAINTED. Refused, the master's own check stopped the battle for everyone.
        return (p[1] <= B_ACTION_RUN || p[1] == B_ACTION_NOTHING_FAINTED || p[1] == B_ACTION_NONE)
            && (p[1] == B_ACTION_USE_ITEM ? Half(p + 2) < ITEMS_COUNT : !Half(p + 2));
#else
#line 199 // gba-line
        return (p[1] <= B_ACTION_RUN || p[1] == B_ACTION_NONE)
            && (p[1] == B_ACTION_USE_ITEM ? Half(p + 2) < ITEMS_COUNT : !Half(p + 2));
#endif
#line 201 // gba-line
    case CONTROLLER_CHOOSEMOVE: return MoveChoiceValid(p);
    case CONTROLLER_CHOOSEPOKEMON:
        return p[1] <= 4 && p[2] <= PARTY_SIZE && (p[3] | p[7] << 8) < ABILITIES_COUNT
            && p[8] < BattlerCount() && (p[4] & 15) < PARTY_SIZE && (p[4] >> 4) < PARTY_SIZE
            && (p[5] & 15) < PARTY_SIZE && (p[5] >> 4) < PARTY_SIZE
            && (p[6] & 15) < PARTY_SIZE && (p[6] >> 4) < PARTY_SIZE;
    case CONTROLLER_STATUSICONUPDATE: return !(Word(p + 1) & ~(STATUS1_ANY | STATUS1_TOXIC_COUNTER));
    case CONTROLLER_STATUSANIMATION: return p[1] <= 1;
    case CONTROLLER_PLAYSE: case CONTROLLER_PLAYFANFAREORBGM:
        return Half(p + 1) <= LAST_PHONEME_SONG && (p[0] == CONTROLLER_PLAYSE ? !p[3] : p[3] <= 1);
    case CONTROLLER_INTROSLIDE: return p[1] < BATTLE_ENVIRONMENT_COUNT;
    case CONTROLLER_DRAWPARTYSTATUSSUMMARY:
        if (p[1] > 1 || p[2] > 1) return FALSE;
        for (i = 0; i < PARTY_SIZE; i++) if (Word(p + 4 + i * 8 + 4) & ~(STATUS1_ANY | STATUS1_TOXIC_COUNTER)) return FALSE;
        return TRUE;
    case CONTROLLER_SPRITEINVISIBILITY: return p[1] <= 1;
    case CONTROLLER_BATTLEANIMATION: return p[1] < NUM_B_ANIMS_GENERAL && AnimValid(p + 4);
    case CONTROLLER_LINKSTANDBYMSG: return p[1] <= LINK_STANDBY_MSG_ONLY && !p[2] && !p[3];
    case CONTROLLER_RESETACTIONMOVESELECTION: return p[1] <= RESET_MOVE_SELECTION;
    case CONTROLLER_ENDLINKBATTLE:
        value = p[1];
#if MP_FFA_ENABLED
        // Free-for-all: byte 2 names the winner's battler (0xFF: a draw); every core works out its own result from it.
        if (MultiplayerBattle_IsFreeForAll())
            return (value == B_OUTCOME_WON || value == B_OUTCOME_LOST || value == B_OUTCOME_DREW)
                && (p[2] < CoreCount() || p[2] == 0xFF) && !p[3] && !p[4] && !p[5]; // the winner is a player's battler
#endif
        return (value == B_OUTCOME_WON || value == B_OUTCOME_LOST || value == B_OUTCOME_DREW)
#line 222 // gba-line
            && !p[2] && !p[3] && !p[4] && !p[5];
#ifdef PORTABLE
    case CONTROLLER_PC_STAT_STAGES: return StatStages_CommandValid(p, BattlerCount());
#endif
#line 223 // gba-line
    default: return TRUE;
    }
}

bool32 MultiplayerBattle_ValidateController(const u8 *data, u16 size, u8 source)
{
    const u8 *p = data + 8;
    u16 raw, padded;
    u8 battler, command;
    u32 count = BattlerCount();
    if (size < 12 || size > MULTIPLAYER_BATTLE_BLOCK_SIZE || source >= CoreCount()) return FALSE;
    battler = data[1]; command = p[0]; padded = Half(data + 4);
    if (battler >= count || data[2] >= count || data[3] >= count || data[7] >= count || (data[6] & ~((1 << count) - 1))
     || padded + 8 != size) return FALSE;
    if (data[0] == B_COMM_CONTROLLER_IS_DONE)
        return padded == 8 && p[0] == source && Zero(p + 1, 7)
            && sPending[battler] != 0xFF && !(sDone[battler] & (1 << source))
            && (source != Owner(battler) || (sPending[battler] != CONTROLLER_GETMONDATA
                && sPending[battler] != CONTROLLER_CHOOSEACTION && sPending[battler] != CONTROLLER_CHOOSEMOVE
                && sPending[battler] != CONTROLLER_CHOOSEPOKEMON) || sReplied[battler]);
    raw = MultiplayerBattle_ControllerRawSize(command, p[1]);
    if (!raw || padded != raw - raw % 4 + 4 || !Zero(p + raw, padded - raw)) return FALSE;
    if (data[0] == B_COMM_TO_CONTROLLER)
        return source == 0 && command != CONTROLLER_DATATRANSFER && command != CONTROLLER_TWORETURNVALUES
            && command != CONTROLLER_CHOSENMONRETURNVALUE && sPending[battler] == 0xFF && MasterDataValid(p);
    if (data[0] != B_COMM_TO_ENGINE || source != Owner(battler) || sPending[battler] == 0xFF || sReplied[battler]) return FALSE;
    if (command == CONTROLLER_DATATRANSFER)
        return sPending[battler] == CONTROLLER_GETMONDATA && MonReplyValid(p);
    if (command == CONTROLLER_CHOSENMONRETURNVALUE)
        return sPending[battler] == CONTROLLER_CHOOSEPOKEMON && p[1] <= PARTY_SIZE
            && (p[2] & 15) < PARTY_SIZE && (p[2] >> 4) < PARTY_SIZE
            && (p[3] & 15) < PARTY_SIZE && (p[3] >> 4) < PARTY_SIZE
            && (p[4] & 15) < PARTY_SIZE && (p[4] >> 4) < PARTY_SIZE;
    if (command == CONTROLLER_TWORETURNVALUES)
    {
        u32 choice = Word(p + 2);
        if (sPending[battler] == CONTROLLER_CHOOSEACTION)
#ifdef PORTABLE
            // Co-op Gym: the master's AI foes (battlers 1 and 3) answer a move
            // action with their default Struggle target, an opposing player
            // battler, in the high byte (AI_TrySwitchOrUseItem:
            // GetOppositeBattler << 8; HandleTurnActionSelectionState reads
            // it as moveTarget). Trainers' own controllers send 0.
            return (p[1] <= B_ACTION_RUN || p[1] == B_ACTION_NOTHING_FAINTED || p[1] == B_ACTION_UNK_14)
                && (!choice || (MultiplayerBattle_IsCoop() && (battler & 1) && p[1] == B_ACTION_USE_MOVE
                    && !(choice & ~0xFF00) && (choice >> 8) < count && !((choice >> 8) & 1)));
#else
#line 260 // gba-line
            return (p[1] <= B_ACTION_RUN || p[1] == B_ACTION_NOTHING_FAINTED || p[1] == B_ACTION_UNK_14) && !choice;
#endif
#line 261 // gba-line
        if (sPending[battler] == CONTROLLER_CHOOSEMOVE)
            return p[1] == B_ACTION_EXEC_SCRIPT && (choice == 0xFFFF
                || (!(choice & ~(MultiplayerBattle_IsNativeDouble() ? 0x3FF : 0x1FF))
                    && (choice & 0x7F) < MAX_MON_MOVES && ((choice >> 8) & 3) < count));
    }
    return FALSE;
}

void MultiplayerBattle_NoteController(const u8 *data, u8 source)
{
    u8 battler = data[1];
    if (data[0] == B_COMM_TO_CONTROLLER)
    {
        sPending[battler] = data[8]; sDone[battler] = 0; sReplied[battler] = FALSE;
    }
    else if (data[0] == B_COMM_TO_ENGINE) sReplied[battler] = TRUE;
    else if (data[0] == B_COMM_CONTROLLER_IS_DONE)
    {
        sDone[battler] |= 1 << source;
        if (sDone[battler] == (1 << CoreCount()) - 1) sPending[battler] = 0xFF;
    }
}

void MultiplayerBattle_ResetControllerValidation(void)
{
    memset(sPending, 0xFF, sizeof(sPending)); memset(sDone, 0, sizeof(sDone)); memset(sReplied, 0, sizeof(sReplied));
}
