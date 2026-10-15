#include "platform/keybinds.h"

#include <SDL_keycode.h>
#include <ctype.h>
#include <string.h>

static const struct
{
    const char *config;
    const char *label;
    int defaultKey;
} sActions[KEYBIND_COUNT] = {
    [KEYBIND_A]            = {"key.a", "A button", SDLK_z},
    [KEYBIND_B]            = {"key.b", "B button", SDLK_x},
    [KEYBIND_START]        = {"key.start", "Start", SDLK_RETURN},
    [KEYBIND_SELECT]       = {"key.select", "Select", SDLK_BACKSLASH},
    [KEYBIND_L]            = {"key.l", "L button", SDLK_a},
    [KEYBIND_R]            = {"key.r", "R button", SDLK_s},
    [KEYBIND_UP]           = {"key.up", "Up", SDLK_UP},
    [KEYBIND_DOWN]         = {"key.down", "Down", SDLK_DOWN},
    [KEYBIND_LEFT]         = {"key.left", "Left", SDLK_LEFT},
    [KEYBIND_RIGHT]        = {"key.right", "Right", SDLK_RIGHT},
    [KEYBIND_FAST_FORWARD] = {"key.fastforward", "Fast forward (hold)", SDLK_SPACE},
};

static bool ValidAction(int action)
{
    return action >= 0 && action < KEYBIND_COUNT;
}

int Keybinds_DefaultKey(int action)
{
    return ValidAction(action) ? sActions[action].defaultKey : 0;
}

const char *Keybinds_ActionLabel(int action)
{
    return ValidAction(action) ? sActions[action].label : "";
}

const char *Keybinds_ConfigName(int action)
{
    return ValidAction(action) ? sActions[action].config : "";
}

static void MarkCustom(struct Keybinds *kb, int action)
{
    kb->custom[action] = kb->key[action] != sActions[action].defaultKey;
}

void Keybinds_Defaults(struct Keybinds *kb)
{
    int i;

    for (i = 0; i < KEYBIND_COUNT; i++)
    {
        kb->key[i] = sActions[i].defaultKey;
        kb->custom[i] = false;
    }
}

bool Keybinds_IsReserved(int key)
{
    return key == 0 || key == SDLK_ESCAPE || (key >= SDLK_F1 && key <= SDLK_F12);
}

int Keybinds_ActionForKey(const struct Keybinds *kb, int key)
{
    int i;

    if (key == 0)
        return -1;
    for (i = 0; i < KEYBIND_COUNT; i++)
    {
        if (kb->key[i] == key)
            return i;
    }
    return -1;
}

const char *Keybinds_SharedWarning(int key)
{
    switch (key)
    {
    case SDLK_COMMA:
    case SDLK_PERIOD:
    case SDLK_MINUS:
    case SDLK_EQUALS:
    case SDLK_KP_MINUS:
    case SDLK_KP_PLUS:
        return "This key also moves the 3D camera.";
    case SDLK_l:
    case SDLK_h:
    case SDLK_u:
    case SDLK_t:
    case SDLK_y:
        return "This key is also a screen-reader hotkey.";
    case SDLK_TAB:
        return "This key also opens the details view in menus.";
    default:
        return NULL;
    }
}

enum KeybindResult Keybinds_Assign(struct Keybinds *kb, int action, int key, bool swap, int *other)
{
    int holder;

    if (other != NULL)
        *other = -1;
    if (!ValidAction(action) || key == 0)
        return KEYBIND_INVALID;
    if (Keybinds_IsReserved(key))
        return KEYBIND_RESERVED;
    if (kb->key[action] == key)
        return KEYBIND_UNCHANGED;
    holder = Keybinds_ActionForKey(kb, key);
    if (holder >= 0)
    {
        if (other != NULL)
            *other = holder;
        if (!swap)
            return KEYBIND_CONFLICT;
        kb->key[holder] = kb->key[action];
        kb->key[action] = key;
        MarkCustom(kb, holder);
        MarkCustom(kb, action);
        return KEYBIND_SWAPPED;
    }
    kb->key[action] = key;
    MarkCustom(kb, action);
    return KEYBIND_ASSIGNED;
}

void Keybinds_ResetAction(struct Keybinds *kb, int action)
{
    if (ValidAction(action))
    {
        kb->key[action] = sActions[action].defaultKey;
        kb->custom[action] = false;
    }
}

void Keybinds_Reset(struct Keybinds *kb)
{
    Keybinds_Defaults(kb);
}

static bool HasDuplicate(const struct Keybinds *kb)
{
    int i, j;

    for (i = 1; i < KEYBIND_COUNT; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (kb->key[i] == kb->key[j])
                return true;
        }
    }
    return false;
}

void Keybinds_Finish(struct Keybinds *kb)
{
    int i, j;

    for (i = 1; i < KEYBIND_COUNT; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (kb->key[i] == kb->key[j])
            {
                Keybinds_ResetAction(kb, i);
                break;
            }
        }
    }
    if (HasDuplicate(kb))
        Keybinds_Reset(kb);
}

static int CompareName(const char *a, const char *b, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);

        if (x != y)
            return x - y;
    }
    return b[length];
}

bool Keybinds_ParseLine(struct Keybinds *kb, const char *line, const struct KeybindNames *names)
{
    const char *equals;
    char value[64];
    size_t nameLength, valueLength;
    int action, code;

    if (strlen(line) < 4 || CompareName(line, "key.", 4) != 0)
        return false;
    equals = strchr(line, '=');
    if (equals == NULL)
        return true;
    nameLength = (size_t)(equals - line);
    for (action = 0; action < KEYBIND_COUNT; action++)
    {
        if (strlen(sActions[action].config) == nameLength && CompareName(line, sActions[action].config, nameLength) == 0)
            break;
    }
    if (action == KEYBIND_COUNT)
        return true; /* a key line of a later version: ignored, not rewritten */
    valueLength = strcspn(equals + 1, "\r\n");
    while (valueLength > 0 && equals[valueLength] == ' ')
        valueLength--;
    if (valueLength == 0 || valueLength >= sizeof(value))
        return true;
    memcpy(value, equals + 1, valueLength);
    value[valueLength] = '\0';
    code = names->nameToCode(value);
    if (code == 0 || Keybinds_IsReserved(code))
        return true;
    kb->key[action] = code;
    MarkCustom(kb, action);
    return true;
}

void Keybinds_Write(const struct Keybinds *kb, FILE *file, const struct KeybindNames *names)
{
    int i;

    for (i = 0; i < KEYBIND_COUNT; i++)
    {
        const char *name;

        if (!kb->custom[i])
            continue;
        name = names->codeToName(kb->key[i]);
        if (name != NULL && name[0] != '\0')
            fprintf(file, "%s=%s\n", sActions[i].config, name);
    }
}

void Keybinds_KeyLabel(int key, const struct KeybindNames *names, char *out, int size)
{
    const char *name = names->codeToName(key);
    int n = 0;

    if (size <= 0)
        return;
    if (name == NULL || name[0] == '\0')
        name = "?";
    if (strcmp(name, "Return") == 0)
        name = "Enter";
    else if (strcmp(name, "Escape") == 0)
        name = "Esc";
    else if (strcmp(name, "Backspace") == 0)
        name = "Bksp";
    for (; name[n] != '\0' && n < size - 1; n++)
        out[n] = (char)toupper((unsigned char)name[n]);
    out[n] = '\0';
}
