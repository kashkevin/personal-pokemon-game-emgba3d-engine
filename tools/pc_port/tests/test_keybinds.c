// Keyboard bindings (include/platform/keybinds.h): defaults, the settings file's lines, conflicts,
// reserved keys, and that a damaged or hand-edited file can never leave the game without usable keys.
#include <SDL2/SDL_keycode.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "minitest.h"
#include "platform/keybinds.h"

// SDL's names for the keys these tests use (the game passes SDL_GetKeyName / SDL_GetKeyFromName).
static const struct { int code; const char *name; } sKeys[] = {
    {SDLK_z, "Z"}, {SDLK_x, "X"}, {SDLK_a, "A"}, {SDLK_s, "S"}, {SDLK_d, "D"}, {SDLK_w, "W"}, {SDLK_q, "Q"},
    {SDLK_e, "E"}, {SDLK_k, "K"}, {SDLK_j, "J"}, {SDLK_l, "L"}, {SDLK_RETURN, "Return"},
    {SDLK_BACKSLASH, "\\"}, {SDLK_SPACE, "Space"}, {SDLK_UP, "Up"}, {SDLK_DOWN, "Down"},
    {SDLK_LEFT, "Left"}, {SDLK_RIGHT, "Right"}, {SDLK_ESCAPE, "Escape"}, {SDLK_F10, "F10"},
    {SDLK_F1, "F1"}, {SDLK_F12, "F12"}, {SDLK_TAB, "Tab"}, {SDLK_EQUALS, "="}, {SDLK_LSHIFT, "Left Shift"},
    {SDLK_BACKSPACE, "Backspace"}, {SDLK_COMMA, ","}, {SDLK_KP_PLUS, "Keypad +"},
};

static const char *CodeToName(int code)
{
    for (unsigned i = 0; i < sizeof(sKeys) / sizeof(sKeys[0]); i++)
    {
        if (sKeys[i].code == code)
            return sKeys[i].name;
    }
    return "";
}

static int NameToCode(const char *name)
{
    for (unsigned i = 0; i < sizeof(sKeys) / sizeof(sKeys[0]); i++)
    {
        if (strcasecmp(sKeys[i].name, name) == 0)
            return sKeys[i].code;
    }
    return 0;
}

static const struct KeybindNames sNames = {CodeToName, NameToCode};

static struct Keybinds Parse(const char *const *lines, int count)
{
    struct Keybinds kb;

    Keybinds_Defaults(&kb);
    for (int i = 0; i < count; i++)
        Keybinds_ParseLine(&kb, lines[i], &sNames);
    Keybinds_Finish(&kb);
    return kb;
}

static bool ConflictFree(const struct Keybinds *kb)
{
    for (int i = 0; i < KEYBIND_COUNT; i++)
    {
        if (kb->key[i] == 0 || Keybinds_IsReserved(kb->key[i]))
            return false;
        for (int j = 0; j < i; j++)
        {
            if (kb->key[i] == kb->key[j])
                return false;
        }
    }
    return true;
}

MT_TEST(defaults_are_the_keys_before_rebinding)
{
    struct Keybinds kb;

    Keybinds_Defaults(&kb);
    MT_EQ(kb.key[KEYBIND_A], SDLK_z);
    MT_EQ(kb.key[KEYBIND_B], SDLK_x);
    MT_EQ(kb.key[KEYBIND_START], SDLK_RETURN);
    MT_EQ(kb.key[KEYBIND_SELECT], SDLK_BACKSLASH);
    MT_EQ(kb.key[KEYBIND_L], SDLK_a);
    MT_EQ(kb.key[KEYBIND_R], SDLK_s);
    MT_EQ(kb.key[KEYBIND_UP], SDLK_UP);
    MT_EQ(kb.key[KEYBIND_DOWN], SDLK_DOWN);
    MT_EQ(kb.key[KEYBIND_LEFT], SDLK_LEFT);
    MT_EQ(kb.key[KEYBIND_RIGHT], SDLK_RIGHT);
    MT_EQ(kb.key[KEYBIND_FAST_FORWARD], SDLK_SPACE);
    MT_CHECK(ConflictFree(&kb));
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_CHECK(!kb.custom[i] && kb.key[i] == Keybinds_DefaultKey(i));
    MT_EQ(Keybinds_ActionForKey(&kb, SDLK_z), KEYBIND_A);
    MT_EQ(Keybinds_ActionForKey(&kb, SDLK_q), -1);
    MT_EQ(Keybinds_ActionForKey(&kb, 0), -1);
}

MT_TEST(nothing_is_written_until_a_binding_changes)
{
    struct Keybinds kb;
    char buffer[256] = "";
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");

    Keybinds_Defaults(&kb);
    Keybinds_Write(&kb, file, &sNames);
    fclose(file);
    MT_EQ(strlen(buffer), 0);
}

MT_TEST(file_round_trip)
{
    static const char *const lines[] = {"key.a=Q\n", "key.b=E\r\n", "key.up=W\n", "key.fastforward=Left Shift\n"};
    struct Keybinds kb = Parse(lines, 4), again;
    char buffer[512] = "", *line;
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");

    MT_EQ(kb.key[KEYBIND_A], SDLK_q);
    MT_EQ(kb.key[KEYBIND_B], SDLK_e);
    MT_EQ(kb.key[KEYBIND_UP], SDLK_w);
    MT_EQ(kb.key[KEYBIND_FAST_FORWARD], SDLK_LSHIFT);
    MT_CHECK(kb.custom[KEYBIND_A] && kb.custom[KEYBIND_UP] && !kb.custom[KEYBIND_START]);
    MT_EQ(kb.key[KEYBIND_START], SDLK_RETURN); // untouched keys keep their default
    Keybinds_Write(&kb, file, &sNames);
    fclose(file);
    MT_CHECK(strstr(buffer, "key.a=Q\n") != NULL);
    MT_CHECK(strstr(buffer, "key.fastforward=Left Shift\n") != NULL);
    MT_CHECK(strstr(buffer, "key.start") == NULL); // only the changed ones
    Keybinds_Defaults(&again);
    for (line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
    {
        char withNewline[80];

        snprintf(withNewline, sizeof(withNewline), "%s\n", line);
        Keybinds_ParseLine(&again, withNewline, &sNames);
    }
    Keybinds_Finish(&again);
    MT_CHECK(memcmp(kb.key, again.key, sizeof(kb.key)) == 0);
}

MT_TEST(names_are_case_insensitive_and_the_equals_key_works)
{
    static const char *const lines[] = {"KEY.A=q\n", "key.b==\n", "key.l=left shift\n"};
    struct Keybinds kb = Parse(lines, 3);

    MT_EQ(kb.key[KEYBIND_A], SDLK_q);
    MT_EQ(kb.key[KEYBIND_B], SDLK_EQUALS);
    MT_EQ(kb.key[KEYBIND_L], SDLK_LSHIFT);
}

MT_TEST(bad_lines_are_ignored)
{
    static const char *const lines[] = {
        "key.a=Nonsense\n", "key.b=\n", "key.start\n", "key.select=Escape\n", "key.l=F10\n", "key.r=F1\n",
        "key.nothing=Q\n", "key.up=\n", "keyboard=Q\n", "key.a\n",
    };
    struct Keybinds kb = Parse(lines, 10), defaults;

    Keybinds_Defaults(&defaults);
    MT_CHECK(memcmp(kb.key, defaults.key, sizeof(kb.key)) == 0);
    MT_CHECK(Keybinds_ParseLine(&kb, "key.a=Nonsense\n", &sNames)); // a key line, even when unusable
    MT_CHECK(!Keybinds_ParseLine(&kb, "volume=5\n", &sNames));       // another setting: not ours
    MT_CHECK(!Keybinds_ParseLine(&kb, "keyboard=Q\n", &sNames));
}

MT_TEST(a_file_binding_one_key_twice_stays_playable)
{
    // A bound twice to Q, B also Q: the later action goes back to its default.
    static const char *const twice[] = {"key.a=Q\n", "key.b=Q\n"};
    struct Keybinds kb = Parse(twice, 2);

    MT_EQ(kb.key[KEYBIND_A], SDLK_q);
    MT_EQ(kb.key[KEYBIND_B], SDLK_x);
    MT_CHECK(ConflictFree(&kb));
    // L is bound to A's key X, and A's own default Z is free: L goes back to its default (A).
    static const char *const stolen[] = {"key.a=X\n", "key.b=Z\n", "key.l=X\n"};
    kb = Parse(stolen, 3);
    MT_CHECK(ConflictFree(&kb));
    MT_EQ(kb.key[KEYBIND_L], SDLK_a);
    // B is bound to L's default key: the later action (L) cannot go back to it, so everything does.
    static const char *const unresolvable[] = {"key.b=A\n", "key.a=Q\n"};
    kb = Parse(unresolvable, 2);
    MT_CHECK(ConflictFree(&kb));
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_EQ(kb.key[i], Keybinds_DefaultKey(i));
    // A file that swapped two defaults is a valid choice and is kept.
    static const char *const swapped[] = {"key.a=X\n", "key.b=Z\n"};
    kb = Parse(swapped, 2);
    MT_EQ(kb.key[KEYBIND_A], SDLK_x);
    MT_EQ(kb.key[KEYBIND_B], SDLK_z);
}

MT_TEST(any_damaged_file_yields_usable_keys)
{
    static const char *const names[] = {"Z", "X", "A", "S", "Q", "W", "E", "Return", "Space", "Up", "Left", "Tab", "Escape", "F10", "zzz", ""};
    static const char *const actions[] = {"a", "b", "start", "select", "l", "r", "up", "down", "left", "right", "fastforward"};
    unsigned seed = 12345;

    for (int round = 0; round < 2000; round++)
    {
        struct Keybinds kb;

        Keybinds_Defaults(&kb);
        for (int n = 0; n < 14; n++)
        {
            char line[80];

            seed = seed * 1103515245u + 12345u;
            snprintf(line, sizeof(line), "key.%s=%s\n", actions[(seed >> 8) % 11], names[(seed >> 16) % 16]);
            Keybinds_ParseLine(&kb, line, &sNames);
        }
        Keybinds_Finish(&kb);
        MT_CHECK(ConflictFree(&kb));
        for (int i = 0; i < KEYBIND_COUNT; i++)
            MT_CHECK(kb.custom[i] == (kb.key[i] != Keybinds_DefaultKey(i)));
    }
}

MT_TEST(assign_reports_conflicts_and_swaps_on_request)
{
    struct Keybinds kb;
    int other = 99;

    Keybinds_Defaults(&kb);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_q, false, &other), KEYBIND_ASSIGNED);
    MT_EQ(other, -1);
    MT_EQ(kb.key[KEYBIND_A], SDLK_q);
    MT_CHECK(kb.custom[KEYBIND_A]);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_q, false, &other), KEYBIND_UNCHANGED);
    // B asks for Q, which A has: nothing changes until the player accepts the swap.
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_B, SDLK_q, false, &other), KEYBIND_CONFLICT);
    MT_EQ(other, KEYBIND_A);
    MT_EQ(kb.key[KEYBIND_B], SDLK_x);
    MT_EQ(kb.key[KEYBIND_A], SDLK_q);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_B, SDLK_q, true, &other), KEYBIND_SWAPPED);
    MT_EQ(kb.key[KEYBIND_B], SDLK_q);
    MT_EQ(kb.key[KEYBIND_A], SDLK_x); // A took B's old key: no action is ever left without one
    MT_CHECK(ConflictFree(&kb));
    // Swapping back to the defaults clears the custom marks (nothing to write).
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_z, true, &other), KEYBIND_ASSIGNED);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_B, SDLK_x, true, &other), KEYBIND_ASSIGNED);
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_CHECK(!kb.custom[i]);
    MT_CHECK(ConflictFree(&kb));
}

MT_TEST(reserved_and_invalid_keys_are_refused)
{
    struct Keybinds kb;
    int other;

    Keybinds_Defaults(&kb);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_ESCAPE, true, &other), KEYBIND_RESERVED);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_F10, true, &other), KEYBIND_RESERVED);
    for (int key = SDLK_F1; key <= SDLK_F12; key++)
        MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, key, true, &other), KEYBIND_RESERVED);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, 0, true, &other), KEYBIND_INVALID);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_COUNT, SDLK_q, true, &other), KEYBIND_INVALID);
    MT_EQ(Keybinds_Assign(&kb, -1, SDLK_q, true, &other), KEYBIND_INVALID);
    MT_EQ(kb.key[KEYBIND_A], SDLK_z);
    // The keys that reach the settings can never be taken away from them.
    MT_CHECK(Keybinds_IsReserved(SDLK_F10) && Keybinds_IsReserved(SDLK_ESCAPE));
    MT_CHECK(!Keybinds_IsReserved(SDLK_z) && !Keybinds_IsReserved(SDLK_SPACE));
}

MT_TEST(every_action_can_be_rebound_and_the_settings_keys_still_work)
{
    // Move every action off its default (shifted along a ring of free keys): F10 and Esc are still
    // reserved, nothing is bound to them, and restoring puts the defaults back.
    static const int ring[] = {SDLK_q, SDLK_w, SDLK_e, SDLK_k, SDLK_j, SDLK_d, SDLK_l, SDLK_TAB, SDLK_COMMA, SDLK_EQUALS, SDLK_LSHIFT};
    struct Keybinds kb;
    int other;

    Keybinds_Defaults(&kb);
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_EQ(Keybinds_Assign(&kb, i, ring[i], false, &other), KEYBIND_ASSIGNED);
    MT_CHECK(ConflictFree(&kb));
    MT_EQ(Keybinds_ActionForKey(&kb, SDLK_F10), -1);
    MT_EQ(Keybinds_ActionForKey(&kb, SDLK_ESCAPE), -1);
    MT_EQ(Keybinds_ActionForKey(&kb, SDLK_z), -1); // the old defaults are free now
    Keybinds_Reset(&kb);
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_CHECK(kb.key[i] == Keybinds_DefaultKey(i) && !kb.custom[i]);
    Keybinds_Assign(&kb, KEYBIND_A, SDLK_q, false, &other);
    Keybinds_ResetAction(&kb, KEYBIND_A);
    MT_EQ(kb.key[KEYBIND_A], SDLK_z);
    MT_CHECK(!kb.custom[KEYBIND_A]);
}

MT_TEST(keys_the_game_already_uses_warn_but_are_allowed)
{
    struct Keybinds kb;
    int other;

    MT_CHECK(Keybinds_SharedWarning(SDLK_l) != NULL);
    MT_CHECK(strstr(Keybinds_SharedWarning(SDLK_h), "screen-reader") != NULL);
    MT_CHECK(strstr(Keybinds_SharedWarning(SDLK_COMMA), "camera") != NULL);
    MT_CHECK(Keybinds_SharedWarning(SDLK_TAB) != NULL);
    MT_CHECK(Keybinds_SharedWarning(SDLK_z) == NULL);
    MT_CHECK(Keybinds_SharedWarning(SDLK_q) == NULL);
    Keybinds_Defaults(&kb);
    MT_EQ(Keybinds_Assign(&kb, KEYBIND_A, SDLK_l, false, &other), KEYBIND_ASSIGNED); // allowed
}

MT_TEST(labels_are_short_and_upper_case)
{
    char label[32];

    Keybinds_KeyLabel(SDLK_z, &sNames, label, sizeof(label));
    MT_CHECK(strcmp(label, "Z") == 0);
    Keybinds_KeyLabel(SDLK_RETURN, &sNames, label, sizeof(label));
    MT_CHECK(strcmp(label, "ENTER") == 0);
    Keybinds_KeyLabel(SDLK_BACKSLASH, &sNames, label, sizeof(label));
    MT_CHECK(strcmp(label, "\\") == 0);
    Keybinds_KeyLabel(SDLK_LSHIFT, &sNames, label, sizeof(label));
    MT_CHECK(strcmp(label, "LEFT SHIFT") == 0);
    Keybinds_KeyLabel(SDLK_BACKSPACE, &sNames, label, sizeof(label));
    MT_CHECK(strcmp(label, "BKSP") == 0);
    Keybinds_KeyLabel(12345, &sNames, label, sizeof(label)); // no name
    MT_CHECK(strcmp(label, "?") == 0);
    Keybinds_KeyLabel(SDLK_LSHIFT, &sNames, label, 5); // truncated, still terminated
    MT_CHECK(strcmp(label, "LEFT") == 0);
    MT_CHECK(strlen(Keybinds_ActionLabel(KEYBIND_FAST_FORWARD)) > 0);
    MT_CHECK(strcmp(Keybinds_ConfigName(KEYBIND_A), "key.a") == 0);
}

int main(void)
{
    MT_RUN(defaults_are_the_keys_before_rebinding);
    MT_RUN(nothing_is_written_until_a_binding_changes);
    MT_RUN(file_round_trip);
    MT_RUN(names_are_case_insensitive_and_the_equals_key_works);
    MT_RUN(bad_lines_are_ignored);
    MT_RUN(a_file_binding_one_key_twice_stays_playable);
    MT_RUN(any_damaged_file_yields_usable_keys);
    MT_RUN(assign_reports_conflicts_and_swaps_on_request);
    MT_RUN(reserved_and_invalid_keys_are_refused);
    MT_RUN(every_action_can_be_rebound_and_the_settings_keys_still_work);
    MT_RUN(keys_the_game_already_uses_warn_but_are_allowed);
    MT_RUN(labels_are_short_and_upper_case);
    return MT_REPORT();
}
