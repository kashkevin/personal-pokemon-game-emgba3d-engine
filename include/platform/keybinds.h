#ifndef GUARD_PLATFORM_KEYBINDS_H
#define GUARD_PLATFORM_KEYBINDS_H

/*
 * The keyboard bindings of the native PC game (added 2026-10-15; before this the keys were fixed
 * #defines in src/platform/sdl2.c). Pure logic without a window: the table of keys, the lines of
 * game-settings.cfg ("key.a=Z"), conflicts and reserved keys. tools/pc_port/tests/test_keybinds.c
 * checks it; sdl2.c applies it to the key events; the launcher (game_controls.py) edits the same
 * lines while no game runs; the 3D build's F10 settings menu has a Controls page.
 *
 * Keys are SDL key codes (SDLK_*), written by SDL's own key names so the file can be read and edited
 * by hand. A binding is written to the file only once it differs from the default, so a file that
 * never held one follows the defaults (and "restore defaults" is simply no lines).
 *
 * What stays fixed on purpose, so the settings can always be reached and fixed again:
 *   - F10 opens the settings menu, Esc leaves a menu or a key capture: reserved, never bindable.
 *     So are F1-F12 (the 3D build's shortcuts). The settings menu's own navigation (arrows, Enter,
 *     Esc) and typing screens do not follow the bindings.
 *   - The gamepad (XInput on Windows, the Android controller) is not rebindable here and keeps
 *     working whatever the keyboard is set to.
 */
#include <stdbool.h>
#include <stdio.h>

enum KeybindAction
{
    KEYBIND_A,
    KEYBIND_B,
    KEYBIND_START,
    KEYBIND_SELECT,
    KEYBIND_L,
    KEYBIND_R,
    KEYBIND_UP,
    KEYBIND_DOWN,
    KEYBIND_LEFT,
    KEYBIND_RIGHT,
    KEYBIND_COUNT,
    KEYBIND_GAME_BUTTONS = KEYBIND_COUNT, /* every action is a GBA button (the 2x "fast forward" key was removed in Update 30) */
};

/* SDL's names for keys; sdl2.c passes SDL_GetKeyName / SDL_GetKeyFromName, tests pass their own. */
struct KeybindNames
{
    const char *(*codeToName)(int code);
    int (*nameToCode)(const char *name); /* 0 for an unknown name */
};

struct Keybinds
{
    int key[KEYBIND_COUNT];
    bool custom[KEYBIND_COUNT]; /* differs from the default and is written to the file */
};

enum KeybindResult
{
    KEYBIND_ASSIGNED,   /* done */
    KEYBIND_UNCHANGED,  /* the action already had this key */
    KEYBIND_SWAPPED,    /* done; the action that had the key took this action's old key */
    KEYBIND_CONFLICT,   /* not done: another action has the key (assign again with swap) */
    KEYBIND_RESERVED,   /* not done: F1-F12 and Esc are never game buttons */
    KEYBIND_INVALID,    /* not done: no such key or action */
};

void Keybinds_Defaults(struct Keybinds *kb);
int Keybinds_DefaultKey(int action);
/* "A", "Start"... : what the settings screens call the action. */
const char *Keybinds_ActionLabel(int action);
/* "key.a" ... the line's name in game-settings.cfg. */
const char *Keybinds_ConfigName(int action);

/* Reads one line of the settings file; true when it was a key line (valid or not). A line naming an
 * unknown key, a reserved key, or an action that does not exist changes nothing. */
bool Keybinds_ParseLine(struct Keybinds *kb, const char *line, const struct KeybindNames *names);
/* After the whole file: a hand-edited file may bind one key twice. The later action of each pair
 * goes back to its default; if that is taken too, everything goes back to the defaults. The result
 * is always conflict-free. */
void Keybinds_Finish(struct Keybinds *kb);
/* Only the bindings that differ from the defaults. */
void Keybinds_Write(const struct Keybinds *kb, FILE *file, const struct KeybindNames *names);

bool Keybinds_IsReserved(int key);
/* The action using this key, or -1. */
int Keybinds_ActionForKey(const struct Keybinds *kb, int key);
/* Text for a key the game already uses for something else (a screen-reader hotkey, a camera key,
 * Tab's details view): allowed but worth a warning; NULL when the key is free. */
const char *Keybinds_SharedWarning(int key);

/* Binds a key. With a conflict and swap false nothing changes and *other names the action that has
 * the key; with swap true the two exchange keys. */
enum KeybindResult Keybinds_Assign(struct Keybinds *kb, int action, int key, bool swap, int *other);
void Keybinds_ResetAction(struct Keybinds *kb, int action);
void Keybinds_Reset(struct Keybinds *kb);

/* What a prompt shows for the key: "Z", "ENTER", "\\", "LEFT SHIFT"... (upper case, short). */
void Keybinds_KeyLabel(int key, const struct KeybindNames *names, char *out, int size);

#endif
