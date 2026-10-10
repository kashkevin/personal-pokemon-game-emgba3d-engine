#ifndef GUARD_FIELD_DOOR_H
#define GUARD_FIELD_DOOR_H

void FieldSetDoorOpened(u32 x, u32 y);
void FieldSetDoorClosed(u32 x, u32 y);
s8 FieldAnimateDoorClose(u32 x, u32 y);
s8 FieldAnimateDoorOpen(u32 x, u32 y);
bool8 FieldIsDoorAnimationRunning(void);
u32 GetDoorSoundEffect(u32 x, u32 y);

#ifdef ENABLE_DIORAMA
// The door standing open on the current map, for the Diorama 3D view: its
// leftmost cell (map grid coordinates), width in cells and how far open (1-3).
bool8 DioramaDoor_Get(s16 *x, s16 *y, u8 *width, u8 *level);
#endif

#endif //GUARD_FIELD_DOOR_H
