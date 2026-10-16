#ifndef GUARD_PLATFORM_KEY_QUEUE_H
#define GUARD_PLATFORM_KEY_QUEUE_H

// Keys as the game sees them, one step per game frame (Update 28, the dialogue bug).
//
// The window loop reads the keyboard between game frames and hands the game the keys held at the frame boundary. A tap that
// begins and ends between two boundaries (a frame takes long when the 3D view draws slowly; Windows delivers the down and up
// events in one batch) left no trace: the press was lost, and the player had to press A again. The queue keeps every change
// the events made, in order, and the game frame takes the next one: a tap is a frame with the key down followed by a frame
// with it up. Changes that lose nothing are merged (two keys pressed in one batch arrive in one frame), so ordinary play sees
// exactly what it saw before; a press is never merged away, and a key released and pressed again in one batch is delivered
// up for a frame between, so the game sees a new press.
#include <stdint.h>

#define KEY_QUEUE_MAX 16

struct KeyQueue
{
    uint16_t level[KEY_QUEUE_MAX]; // the pending frames' key sets, oldest first (a ring from head)
    uint16_t added[KEY_QUEUE_MAX]; // the keys each went down in (against the frame before it)
    uint16_t dropped[KEY_QUEUE_MAX]; // the keys each went up in
    uint8_t head, count;
    uint16_t delivered; // what the game saw last frame
};

void KeyQueue_Reset(struct KeyQueue *queue);                      // nothing held, nothing pending
void KeyQueue_Set(struct KeyQueue *queue, uint16_t level);       // the keys held now (after an event)
uint16_t KeyQueue_Take(struct KeyQueue *queue);                   // the keys for this game frame
uint8_t KeyQueue_Pending(const struct KeyQueue *queue);

#endif // GUARD_PLATFORM_KEY_QUEUE_H
