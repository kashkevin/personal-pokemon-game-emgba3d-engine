#include "global.h"
#include "event_data.h"
#include "field_move.h"
#include "fldeff.h"
#include "fldeff_misc.h"
#include "party_menu.h"
#include "strings.h"
#include "constants/field_move.h"
#include "constants/moves.h"
#include "constants/party_menu.h"

static bool32 IsAlwaysFalse(enum FieldMove fieldMove)
{
    return FALSE;
}

static bool32 IsAlwaysTrue(enum FieldMove fieldMove)
{
    return TRUE;
}

static bool32 HasBadgeForFieldMove(enum FieldMove fieldMove)
{
    return FlagGet(gFieldMoveInfo[fieldMove].arg + FLAG_BADGE01_GET);
}

const struct FieldMoveUnlock gFieldMoveUnlocks[FIELD_MOVE_UNLOCK_COUNT] =
{
    [CANT_UNLOCK] =
    {
        .isUnlockedFunc = IsAlwaysFalse,
        .lockedMessage = gText_EmptyString2,
    },
    [ALWAYS_UNLOCKED] =
    {
        .isUnlockedFunc = IsAlwaysTrue,
        .lockedMessage = gText_EmptyString2,
    },
    [BADGE_UNLOCK] =
    {
        .isUnlockedFunc = HasBadgeForFieldMove,
        .lockedMessage = gText_CantUseUntilNewBadge,
    },
};

#define FLAG_TO_BADGE(flag) flag - FLAG_BADGE01_GET

const struct FieldMoveInfo gFieldMoveInfo[FIELD_MOVES_COUNT] =
{
    [FIELD_MOVE_CUT] =
    {
        .fieldMoveFunc = SetUpFieldMove_Cut,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_CUT,
        .partyMsgID = PARTY_MSG_NOTHING_TO_CUT,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE02_GET) : FLAG_TO_BADGE(FLAG_BADGE01_GET),
    },

    [FIELD_MOVE_FLASH] =
    {
        .fieldMoveFunc = SetUpFieldMove_Flash,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_FLASH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE01_GET) : FLAG_TO_BADGE(FLAG_BADGE02_GET),
    },

    [FIELD_MOVE_ROCK_SMASH] =
    {
        .fieldMoveFunc = SetUpFieldMove_RockSmash,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_ROCK_SMASH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE06_GET) : FLAG_TO_BADGE(FLAG_BADGE03_GET),
    },

    [FIELD_MOVE_STRENGTH] =
    {
        .fieldMoveFunc = SetUpFieldMove_Strength,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_STRENGTH,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE04_GET),
    },

    [FIELD_MOVE_SURF] =
    {
        .fieldMoveFunc = SetUpFieldMove_Surf,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_SURF,
        .partyMsgID = PARTY_MSG_CANT_SURF_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE05_GET),
    },

    [FIELD_MOVE_FLY] =
    {
        .fieldMoveFunc = SetUpFieldMove_Fly,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_FLY,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE03_GET) : FLAG_TO_BADGE(FLAG_BADGE06_GET),
    },

    [FIELD_MOVE_DIVE] =
    {
        .fieldMoveFunc = SetUpFieldMove_Dive,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_DIVE,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = FLAG_TO_BADGE(FLAG_BADGE07_GET),
    },

    [FIELD_MOVE_WATERFALL] =
    {
        .fieldMoveFunc = SetUpFieldMove_Waterfall,
        .unlockType = BADGE_UNLOCK,
        .moveID = MOVE_WATERFALL,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .arg = IS_FRLG ? FLAG_TO_BADGE(FLAG_BADGE07_GET) : FLAG_TO_BADGE(FLAG_BADGE08_GET),
    },

    [FIELD_MOVE_TELEPORT] =
    {
        .fieldMoveFunc = SetUpFieldMove_Teleport,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_TELEPORT,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_DIG] =
    {
        .fieldMoveFunc = SetUpFieldMove_Dig,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_DIG,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_SECRET_POWER] =
    {
        .fieldMoveFunc = SetUpFieldMove_SecretPower,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SECRET_POWER,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },

    [FIELD_MOVE_MILK_DRINK] =
    {
        .fieldMoveFunc = SetUpFieldMove_SoftBoiled,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_MILK_DRINK,
        .partyMsgID = PARTY_MSG_NOT_ENOUGH_HP,
    },

    [FIELD_MOVE_SOFT_BOILED] =
    {
        .fieldMoveFunc = SetUpFieldMove_SoftBoiled,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SOFT_BOILED,
        .partyMsgID = PARTY_MSG_NOT_ENOUGH_HP,
    },

    [FIELD_MOVE_SWEET_SCENT] =
    {
        .fieldMoveFunc = SetUpFieldMove_SweetScent,
        .unlockType = ALWAYS_UNLOCKED,
        .moveID = MOVE_SWEET_SCENT,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
    },
    [FIELD_MOVE_ROCK_CLIMB] =
    {
        .fieldMoveFunc = SetUpFieldMove_RockClimb,
#if OW_ROCK_CLIMB_FIELD_MOVE
        .unlockType = ALWAYS_UNLOCKED,
#else
        .unlockType = CANT_UNLOCK,
#endif
        .moveID = MOVE_ROCK_CLIMB,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .hideIfLocked = TRUE,
    },
    [FIELD_MOVE_DEFOG] =
    {
        .fieldMoveFunc = SetUpFieldMove_Defog,
#if OW_DEFOG_FIELD_MOVE
        .unlockType = ALWAYS_UNLOCKED,
#else
        .unlockType = CANT_UNLOCK,
#endif
        .moveID = MOVE_DEFOG,
        .partyMsgID = PARTY_MSG_CANT_USE_HERE,
        .hideIfLocked = TRUE,
    },
};

#ifdef PORTABLE
// HMs from the PC boxes (PC only; the owner's request of 2026-10-24): an HM can be used outside battle when any of the
// player's Pokemon knows it, in the party or in a PC box (never an Egg); the badge each HM needs is unchanged. A party
// Pokemon that knows the move is the user; otherwise the first boxed one that does (boxes and slots in order). It stays
// in its box: what the field move shows of it (the cut-in's picture, its name in the messages) is read from a copy taken
// when it was chosen, carried through the scripts and field effects as the party index FIELD_MOVE_BOX_USER, which every
// read of the field move's user turns into that copy (FieldMove_UserBoxMon) and nothing reads as a party slot.
#include "pokemon.h"
#include "pokemon_storage_system.h"

static struct BoxPokemon sFieldMoveBoxUser;
static bool8 sPartyMenuUserBoxed;

// The one search of the boxes: every box and slot, empty slots and Eggs skipped, the move in any of the four slots.
bool32 FieldMove_FindBoxMonWithMove(enum Move move, u32 *boxId, u32 *position)
{
    u32 box, slot;

    if (move == MOVE_NONE || gPokemonStoragePtr == NULL)
        return FALSE;
    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
    {
        for (slot = 0; slot < IN_BOX_COUNT; slot++)
        {
            struct BoxPokemon *boxMon = &gPokemonStoragePtr->boxes[box][slot];

            if (!GetBoxMonData(boxMon, MON_DATA_SANITY_HAS_SPECIES) || GetBoxMonData(boxMon, MON_DATA_SANITY_IS_BAD_EGG)
             || GetBoxMonData(boxMon, MON_DATA_SANITY_IS_EGG) || GetBoxMonData(boxMon, MON_DATA_IS_EGG))
                continue;
            if (BoxMonKnowsMove(boxMon, move))
            {
                if (boxId != NULL)
                    *boxId = box;
                if (position != NULL)
                    *position = slot;
                return TRUE;
            }
        }
    }
    return FALSE;
}

// Whether a party Pokemon (not an Egg) knows the move, as checkfieldmove looks.
bool32 FieldMove_PartyMonKnowsMove(enum Move move)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            break;
        if (!GetMonData(mon, MON_DATA_IS_EGG) && MonKnowsMove(mon, move))
            return TRUE;
    }
    return FALSE;
}

// An HM known in the boxes: the copy of the Pokemon that uses it (FieldMove_UserBoxMon(FIELD_MOVE_BOX_USER)) taken now.
bool32 FieldMove_TakeBoxUser(enum Move move)
{
    u32 box, slot;

    if (!IsMoveHM(move) || !FieldMove_FindBoxMonWithMove(move, &box, &slot))
        return FALSE;
    sFieldMoveBoxUser = gPokemonStoragePtr->boxes[box][slot];
    return TRUE;
}

// The field move's user by its party index: the party Pokemon in that slot, or the boxed one's copy for
// FIELD_MOVE_BOX_USER.
struct BoxPokemon *FieldMove_UserBoxMon(u32 slot)
{
    if (slot == FIELD_MOVE_BOX_USER)
        return &sFieldMoveBoxUser;
    return &gParties[B_TRAINER_PLAYER][slot].box;
}

// The party menu: Fly and Flash (used only from there) offered from the boxes on a Pokemon that does not know them; set
// as each field move is chosen (CursorCb_FieldMove), read where the move starts.
void FieldMove_SetPartyMenuUserBoxed(bool32 boxed)
{
    sPartyMenuUserBoxed = boxed;
}

// The party index of the Pokemon using the field move chosen in the party menu (slot: the one chosen there).
u32 FieldMove_PartyMenuUser(u32 slot)
{
    return sPartyMenuUserBoxed ? FIELD_MOVE_BOX_USER : slot;
}
#endif
