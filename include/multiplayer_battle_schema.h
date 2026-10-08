#ifndef GUARD_MULTIPLAYER_BATTLE_SCHEMA_H
#define GUARD_MULTIPLAYER_BATTLE_SCHEMA_H

// Numeric native command / raw payload length. Wire length is raw-raw%4+12.
// Variable SETMONDATA (2) is handled by the audited field table below.
#define MULTIPLAYER_MASTER_CONTROLLER_SCHEMA(X) \
    X(0, 4) X(4, 4) X(5, 4) X(6, 2) X(7, 4) X(8, 4) X(9, 4) X(10, 4) \
    X(14, 24) X(15, 80) X(16, 80) X(17, 4) X(19, 44) X(21, 9) \
    X(23, 4) X(25, 5) X(26, 6) X(36, 4) X(37, 4) X(38, 4) X(39, 4) \
    X(40, 4) X(41, 2) X(42, 4) X(43, 52) X(44, 4) X(45, 4) \
    X(46, 4) X(47, 12) X(48, 4) X(49, 2) X(50, 6)
#define MULTIPLAYER_SET_FIELD_SCHEMA(X) \
    X(1, 2) X(2, 2) X(3, 16) X(8, 5) X(9, 1) X(10, 1) X(11, 1) X(12, 1) \
    X(40, 4) X(42, 2)

u16 MultiplayerBattle_ControllerRawSize(u8 command, u8 request);
void MultiplayerBattle_ResetControllerValidation(void);
void MultiplayerBattle_NoteController(const u8 *data, u8 source);

#endif
