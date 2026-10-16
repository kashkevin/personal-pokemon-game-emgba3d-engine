/*
 * Keys as the game sees them, one step per game frame: see include/platform/key_queue.h. Project-owned.
 */
#include "platform/key_queue.h"

void KeyQueue_Reset(struct KeyQueue *queue)
{
    queue->head = 0;
    queue->count = 0;
    queue->delivered = 0;
}

uint8_t KeyQueue_Pending(const struct KeyQueue *queue)
{
    return queue->count;
}

static unsigned Slot(const struct KeyQueue *queue, unsigned offset)
{
    return (queue->head + offset) % KEY_QUEUE_MAX;
}

void KeyQueue_Set(struct KeyQueue *queue, uint16_t level)
{
    uint16_t before = queue->count > 0 ? queue->level[Slot(queue, queue->count - 1u)] : queue->delivered;
    unsigned tail;

    if (level == before)
        return;
    if (queue->count > 0)
    {
        tail = Slot(queue, queue->count - 1u);
        // Merge into the newest pending frame unless that would lose a press (a key it brings down that this level has
        // already released) or a release a later press needs (a key it takes up that this level presses again).
        if ((queue->added[tail] & (uint16_t)~level) == 0 && (queue->dropped[tail] & level) == 0)
        {
            queue->added[tail] = (uint16_t)((queue->added[tail] | (level & ~before)) & level);
            queue->dropped[tail] = (uint16_t)(queue->dropped[tail] | (before & ~level));
            queue->level[tail] = level;
            return;
        }
    }
    if (queue->count == KEY_QUEUE_MAX)
    {
        // A backlog this long: the newest frame takes the change (a press may be merged away, never a hang)
        tail = Slot(queue, KEY_QUEUE_MAX - 1u);
        queue->level[tail] = level;
        queue->added[tail] = (uint16_t)(queue->added[tail] & level);
        return;
    }
    tail = Slot(queue, queue->count);
    queue->level[tail] = level;
    queue->added[tail] = (uint16_t)(level & ~before);
    queue->dropped[tail] = (uint16_t)(before & ~level);
    queue->count++;
}

uint16_t KeyQueue_Take(struct KeyQueue *queue)
{
    if (queue->count > 0)
    {
        queue->delivered = queue->level[queue->head];
        queue->head = (uint8_t)((queue->head + 1u) % KEY_QUEUE_MAX);
        queue->count--;
    }
    return queue->delivered;
}
