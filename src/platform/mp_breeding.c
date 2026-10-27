/* The Day Care egg queue and the egg shiny rolls: see include/platform/mp_breeding.h. Project-owned. */
#include "global.h"
#ifdef PORTABLE
#include <stdio.h>
#include <string.h>
#include "event_data.h"
#include "main.h"
#include "multiplayer_world.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/game_stat.h"
#include "platform/mp_breeding.h"

_Static_assert(sizeof(struct BoxPokemon) == sizeof(((struct MPDaycareEggQueue *)0)->eggs[0]), "Egg queue slot holds one BoxPokemon");

u32 MPBreeding_EggShinyRolls(bool32 parentShiny, bool32 charmOwned)
{
    if (parentShiny && charmOwned)
        return 6;
    if (parentShiny || charmOwned)
        return 3;
    return 1;
}

static u32 Crc32(const u8 *data, u32 size)
{
    u32 crc = 0xFFFFFFFF;

    while (size--)
    {
        u32 i;

        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static u32 QueueCrc(const struct MPDaycareEggQueue *queue)
{
    return Crc32((const u8 *)queue, offsetof(struct MPDaycareEggQueue, crc32));
}

bool32 MPDaycare_QueueIsSane(void)
{
    const struct MPDaycareEggQueue *queue = &gSaveBlock3Ptr->eggQueue;

    return queue->magic == MP_DAYCARE_EGG_QUEUE_MAGIC && queue->version == 1 && queue->count <= MP_DAYCARE_EGG_QUEUE_SIZE
        && queue->reserved == 0 && queue->crc32 == QueueCrc(queue);
}

static void Seal(struct MPDaycareEggQueue *queue)
{
    queue->magic = MP_DAYCARE_EGG_QUEUE_MAGIC;
    queue->version = 1;
    queue->reserved = 0;
    queue->crc32 = QueueCrc(queue);
}

u32 MPDaycare_QueueCount(void)
{
    return MPDaycare_QueueIsSane() ? gSaveBlock3Ptr->eggQueue.count : 0;
}

void MPDaycare_QueueClear(void)
{
    memset(&gSaveBlock3Ptr->eggQueue, 0, sizeof(gSaveBlock3Ptr->eggQueue));
}

bool32 MPDaycare_QueuePush(const struct Pokemon *egg)
{
    struct MPDaycareEggQueue *queue = &gSaveBlock3Ptr->eggQueue;
    u32 count = MPDaycare_QueueCount();

    if (count >= MP_DAYCARE_EGG_QUEUE_SIZE)
        return FALSE;
    if (!MPDaycare_QueueIsSane())
        MPDaycare_QueueClear();
    memcpy(queue->eggs[count], &egg->box, sizeof(egg->box));
    queue->count = count + 1;
    Seal(queue);
    return TRUE;
}

bool32 MPDaycare_QueueDropOldest(void)
{
    struct MPDaycareEggQueue *queue = &gSaveBlock3Ptr->eggQueue;
    u32 count = MPDaycare_QueueCount(), i;

    if (count == 0)
        return FALSE;
    for (i = 1; i < count; i++)
        memcpy(queue->eggs[i - 1], queue->eggs[i], sizeof(queue->eggs[0]));
    memset(&queue->eggs[count - 1], 0, sizeof(queue->eggs[0]));
    queue->count = count - 1;
    Seal(queue);
    return TRUE;
}

bool32 MPDaycare_QueuePop(struct Pokemon *egg)
{
    struct MPDaycareEggQueue *queue = &gSaveBlock3Ptr->eggQueue;

    if (MPDaycare_QueueCount() == 0)
        return FALSE;
    BoxMonToMon((const struct BoxPokemon *)queue->eggs[0], egg);
    MPDaycare_QueueDropOldest();
    return TRUE;
}

// ---------------------------------------------------------------------------
// Eggs in the PC's boxes

static enum Species sHatched[2];   // the first two hatched since the last banner
static u32 sHatchedCount;          // how many hatched since the last banner
static char sLastNotice[80];       // the last banner shown (the test builds' breed log)

// The changes the party's hatch makes (egg_hatch.c AddHatchedMonToParty), on a boxed Egg: no longer an Egg, its
// species' name, the game's language, friendship 120, hatched here (met level 0), PP restored, in the Pokedex. Its
// personality (nature, shininess, ability, gender), IVs, moves, species and ownership serial are not touched. The
// original trainer is set from the save as the party's hatch does only when it is already this player (the Eggs this
// player's Day Care made are): the host refuses a save whose Pokemon changed its original trainer.
static void HatchBoxEgg(struct BoxPokemon *mon)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    enum NationalDexOrder dex = SpeciesToNationalPokedexNum(species);
    u8 name[POKEMON_NAME_LENGTH + 1];
    u32 notEgg = FALSE, friendship = 120, metLevel = 0, otId = READ_OTID_FROM_SAVE;
    enum Language language = GAME_LANGUAGE;
    metloc_u8_t metLocation = GetCurrentRegionMapSectionId();

    SetBoxMonData(mon, MON_DATA_IS_EGG, &notEgg);
    StringCopy(name, GetSpeciesName(species));
    SetBoxMonData(mon, MON_DATA_NICKNAME, name);
    GetSetPokedexFlag(dex, FLAG_SET_SEEN);
    GetSetPokedexFlag(dex, FLAG_SET_CAUGHT);
    SetBoxMonData(mon, MON_DATA_LANGUAGE, &language);
    SetBoxMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    SetBoxMonData(mon, MON_DATA_MET_LEVEL, &metLevel);
    SetBoxMonData(mon, MON_DATA_MET_LOCATION, &metLocation);
    SetBoxMonData(mon, MON_DATA_MET_GAME, &gGameVersion);
    if (GetBoxMonData(mon, MON_DATA_OT_ID) == otId)
    {
        SetBoxMonData(mon, MON_DATA_OT_NAME, gSaveBlock2Ptr->playerName);
        SetBoxMonData(mon, MON_DATA_OT_GENDER, &gSaveBlock2Ptr->playerGender);
    }
    BoxMonRestorePP(mon);
    IncrementGameStat(GAME_STAT_HATCHED_EGGS);
    if (sHatchedCount < ARRAY_COUNT(sHatched))
        sHatched[sHatchedCount] = species;
    sHatchedCount++;
}

void MPBreeding_AdvanceBoxEggs(u32 toSub)
{
    u32 box, slot;

    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
    {
        for (slot = 0; slot < IN_BOX_COUNT; slot++)
        {
            struct BoxPokemon *mon = GetBoxedMonPtr(box, slot);
            u32 cycles;

            // the header alone (no decryption) for the empty slots and the Pokemon that are not Eggs
            if (!GetBoxMonData(mon, MON_DATA_SANITY_HAS_SPECIES) || !GetBoxMonData(mon, MON_DATA_SANITY_IS_EGG)
             || GetBoxMonData(mon, MON_DATA_SANITY_IS_BAD_EGG) || !GetBoxMonData(mon, MON_DATA_IS_EGG))
                continue;
            cycles = GetBoxMonData(mon, MON_DATA_FRIENDSHIP);
            if (cycles != 0)
            {
                cycles -= cycles >= toSub ? toSub : 1;   // as a party Egg (TryToHatchEgg)
                SetBoxMonData(mon, MON_DATA_FRIENDSHIP, &cycles);
            }
            else
            {
                HatchBoxEgg(mon);
            }
        }
    }
}

// A species' name for the banner (ASCII): letters, digits and the few marks species names use.
static void SpeciesAscii(char *out, u32 size, enum Species species)
{
    const u8 *name = GetSpeciesName(species);
    u32 n = 0;

    for (; *name != EOS && n + 1 < size; name++)
    {
        u8 c = *name;
        char a = '?';

        if (c >= CHAR_A && c <= CHAR_Z) a = (char)('A' + (c - CHAR_A));
        else if (c >= CHAR_a && c <= CHAR_z) a = (char)('a' + (c - CHAR_a));
        else if (c >= CHAR_0 && c <= CHAR_9) a = (char)('0' + (c - CHAR_0));
        else if (c == CHAR_SPACE) a = ' ';
        else if (c == CHAR_PERIOD) a = '.';
        else if (c == CHAR_HYPHEN) a = '-';
        else if (c == CHAR_SGL_QUOTE_RIGHT) a = '\'';
        else if (c == CHAR_COLON) a = ':';
        else if (c == CHAR_e_ACUTE) a = 'e';
        else if (c == CHAR_MALE) a = 'M';
        else if (c == CHAR_FEMALE) a = 'F';
        out[n++] = a;
    }
    out[n] = '\0';
}

// Two lines of at most 32 characters: "PC EGG HATCHED!|Togepi hatched in your PC.", "2 PC EGGS HATCHED!|Togepi, Pichu",
// "5 PC EGGS HATCHED!|Togepi, Pichu +3". Shown as this game's own banners are, once the field is calm.
void MPBreeding_BoxEggNoticeTick(void)
{
    char first[16], second[16], text[80];

    if (sHatchedCount == 0)
        return;
    SpeciesAscii(first, sizeof(first), sHatched[0]);
    if (sHatchedCount == 1)
    {
        snprintf(text, sizeof(text), "PC EGG HATCHED!|%s hatched in your PC.", first);
    }
    else
    {
        SpeciesAscii(second, sizeof(second), sHatched[1]);
        if (sHatchedCount == 2)
            snprintf(text, sizeof(text), "%u PC EGGS HATCHED!|%s, %s", (unsigned)sHatchedCount, first, second);
        else
            snprintf(text, sizeof(text), "%u PC EGGS HATCHED!|%s, %s +%u", (unsigned)sHatchedCount, first, second,
                     (unsigned)(sHatchedCount - 2));
    }
    if (MultiplayerWorld_ShowLocalNotice(text))
    {
        sHatchedCount = 0;
        snprintf(sLastNotice, sizeof(sLastNotice), "%s", text);
    }
}

const char *MPBreeding_LastBoxEggNotice(void)
{
    return sLastNotice;
}
#endif
