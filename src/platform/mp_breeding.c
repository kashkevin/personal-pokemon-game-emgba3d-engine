/* The Day Care egg queue and the egg shiny rolls: see include/platform/mp_breeding.h. Project-owned. */
#include "global.h"
#ifdef PORTABLE
#include <string.h>
#include "pokemon.h"
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
#endif
