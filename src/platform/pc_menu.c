/*
 * The PC settings menu's model: see include/platform/pc_menu.h.
 * Project-owned (no third-party code).
 */
#include <stdio.h>
#include <string.h>

#include "platform/pc_menu.h"

static const char *const sLabels[PC_MENU_ITEM_COUNT] = {
    [PC_MENU_RESOLUTION]   = "Resolution",
    [PC_MENU_DISPLAY_MODE] = "Display mode",
    [PC_MENU_RENDER_SCALE] = "3D render scale",
    [PC_MENU_VSYNC]        = "VSync",
    [PC_MENU_FPS_CAP]      = "Frame rate limit",
    [PC_MENU_QUALITY]      = "Graphics quality",
    [PC_MENU_SHADOWS]      = "Shadows",
    [PC_MENU_LIGHTING]     = "Lighting",
    [PC_MENU_EFFECTS]      = "Effects",
    [PC_MENU_DETAILS]      = "World details",
    [PC_MENU_ANTIALIASING] = "Anti-aliasing",
    [PC_MENU_MEMORY_BUDGET] = "Memory budget",
    [PC_MENU_PICTURE_SIZE] = "Game picture",
    [PC_MENU_FRAME_RATE]   = "Show frame rate",
    [PC_MENU_CAMERA]       = "Camera",
    [PC_MENU_TREES]        = "Trees",
    [PC_MENU_CONTROLS]     = "Controls",
    [PC_MENU_CLOSE]        = "Close",
};

static bool Visible(const struct PcMenuState *menu, int item)
{
    return menu->hasCamera
        || (item != PC_MENU_CAMERA && item != PC_MENU_TREES && item != PC_MENU_DETAILS
         && item != PC_MENU_RENDER_SCALE && item != PC_MENU_MEMORY_BUDGET);
}

void PcMenu_Open(struct PcMenuState *menu, const struct DisplaySettings *settings, bool vsync,
                 bool pictureFill, bool showFrameRate, bool hasCamera, bool cameraDiorama, bool trees3ds)
{
    memset(menu, 0, sizeof(*menu));
    menu->open = true;
    menu->settings = *settings;
    menu->vsync = vsync;
    menu->pictureFill = pictureFill;
    menu->showFrameRate = showFrameRate;
    menu->hasCamera = hasCamera;
    menu->cameraDiorama = cameraDiorama;
    menu->trees3ds = trees3ds;
    Keybinds_Defaults(&menu->keybinds);
}

void PcMenu_SetKeybinds(struct PcMenuState *menu, const struct Keybinds *keybinds, const struct KeybindNames *names)
{
    menu->keybinds = *keybinds;
    menu->keyNames = names;
}

bool PcMenu_IsCapturing(const struct PcMenuState *menu)
{
    return menu->open && menu->page == PC_MENU_PAGE_CONTROLS && menu->capturing;
}

const char *PcMenu_Title(const struct PcMenuState *menu)
{
    return menu->page == PC_MENU_PAGE_CONTROLS ? "Controls" : "Display & Graphics";
}

const char *PcMenu_Message(const struct PcMenuState *menu)
{
    return menu->page == PC_MENU_PAGE_CONTROLS && menu->message[0] != '\0' ? menu->message : NULL;
}

const char *PcMenu_BackLabel(const struct PcMenuState *menu)
{
    return menu->page == PC_MENU_PAGE_CONTROLS ? "Back" : "Close";
}

/* ---- The Controls page ---------------------------------------------------------------------- */

static int Wrap(int value, int count);

#define CONTROLS_ROW_RESTORE KEYBIND_COUNT
#define CONTROLS_ROW_BACK    (KEYBIND_COUNT + 1)
#define CONTROLS_ROW_COUNT   (KEYBIND_COUNT + 2)

static void KeyText(const struct PcMenuState *menu, int key, char *out, int size)
{
    if (menu->keyNames != NULL)
        Keybinds_KeyLabel(key, menu->keyNames, out, size);
    else
        snprintf(out, (size_t)size, "?");
}

static void SetMessage(struct PcMenuState *menu, const char *format, const char *a, const char *b, const char *warning)
{
    int used = snprintf(menu->message, sizeof(menu->message), format, a, b);

    if (warning != NULL && used > 0 && (size_t)used < sizeof(menu->message))
        snprintf(menu->message + used, sizeof(menu->message) - (size_t)used, "|%s", warning);
}

static void CancelCapture(struct PcMenuState *menu)
{
    menu->capturing = false;
    menu->conflicting = false;
    menu->message[0] = '\0';
}

static unsigned int ControlsInput(struct PcMenuState *menu, enum PcMenuInput input)
{
    int action = menu->controlsCursor;
    char label[40];

    if (menu->capturing)
    {
        if (input == PC_MENU_INPUT_BACK)
        {
            CancelCapture(menu);
            snprintf(menu->message, sizeof(menu->message), "Cancelled. Nothing changed.");
        }
        return 0; /* keys go to PcMenu_CaptureKey meanwhile */
    }
    if (menu->conflicting)
    {
        if (input == PC_MENU_INPUT_ACCEPT)
        {
            int holder = menu->pendingOther;
            enum KeybindResult result = Keybinds_Assign(&menu->keybinds, action, menu->pendingKey, true, &holder);

            menu->conflicting = false;
            if (result == KEYBIND_SWAPPED)
            {
                SetMessage(menu, "%s and %s traded keys.", Keybinds_ActionLabel(action), Keybinds_ActionLabel(holder), NULL);
                return PC_MENU_CHANGED_KEYS;
            }
            CancelCapture(menu);
        }
        else if (input == PC_MENU_INPUT_BACK)
        {
            CancelCapture(menu);
            snprintf(menu->message, sizeof(menu->message), "Cancelled. Nothing changed.");
        }
        return 0;
    }
    switch (input)
    {
    case PC_MENU_INPUT_UP:
        menu->controlsCursor = Wrap(action - 1, CONTROLS_ROW_COUNT);
        menu->message[0] = '\0';
        return 0;
    case PC_MENU_INPUT_DOWN:
        menu->controlsCursor = Wrap(action + 1, CONTROLS_ROW_COUNT);
        menu->message[0] = '\0';
        return 0;
    case PC_MENU_INPUT_ACCEPT:
        if (action == CONTROLS_ROW_BACK)
        {
            menu->page = PC_MENU_PAGE_MAIN;
            menu->message[0] = '\0';
            return 0;
        }
        if (action == CONTROLS_ROW_RESTORE)
        {
            Keybinds_Reset(&menu->keybinds);
            snprintf(menu->message, sizeof(menu->message), "Default keys restored.");
            return PC_MENU_CHANGED_KEYS;
        }
        menu->capturing = true;
        KeyText(menu, menu->keybinds.key[action], label, sizeof(label));
        SetMessage(menu, "Press the new key for %s (now %s).|Esc cancels.", Keybinds_ActionLabel(action), label, NULL);
        return 0;
    case PC_MENU_INPUT_BACK:
        menu->page = PC_MENU_PAGE_MAIN;
        menu->message[0] = '\0';
        return 0;
    default:
        return 0;
    }
}

unsigned int PcMenu_CaptureKey(struct PcMenuState *menu, int key)
{
    int action = menu->controlsCursor, holder = -1;
    char label[40], holderLabel[40];
    enum KeybindResult result;

    if (!PcMenu_IsCapturing(menu) || action < 0 || action >= KEYBIND_COUNT)
        return 0;
    KeyText(menu, key, label, sizeof(label));
    result = Keybinds_Assign(&menu->keybinds, action, key, false, &holder);
    switch (result)
    {
    case KEYBIND_ASSIGNED:
        menu->capturing = false;
        SetMessage(menu, "%s is now %s.", Keybinds_ActionLabel(action), label, Keybinds_SharedWarning(key));
        return PC_MENU_CHANGED_KEYS;
    case KEYBIND_UNCHANGED:
        menu->capturing = false;
        SetMessage(menu, "%s stays %s.", Keybinds_ActionLabel(action), label, NULL);
        return 0;
    case KEYBIND_CONFLICT:
        menu->capturing = false;
        menu->conflicting = true;
        menu->pendingKey = key;
        menu->pendingOther = holder;
        KeyText(menu, key, holderLabel, sizeof(holderLabel));
        SetMessage(menu, "%s is already the key for %s.|Enter swaps the two keys, Esc cancels.",
                   holderLabel, Keybinds_ActionLabel(holder), NULL);
        return 0;
    case KEYBIND_RESERVED:
        SetMessage(menu, "%s can't be a game button (F1-F12 and Esc are kept for the menus).|Press another key, or Esc to cancel.",
                   label, "", NULL);
        return 0;
    default:
        return 0;
    }
}

static int Wrap(int value, int count)
{
    return ((value % count) + count) % count;
}

static void StepResolution(struct DisplaySettings *settings, int direction)
{
    int index = DisplaySettings_ResolutionIndex(settings->width, settings->height);

    if (index < 0)
    {
        /* A resolution of the file's own: to the nearest listed one that way. */
        index = direction > 0 ? 0 : DISPLAY_RESOLUTION_COUNT - 1;
        for (int i = 0; i < DISPLAY_RESOLUTION_COUNT; i++)
        {
            bool larger = gDisplayResolutions[i].height > settings->height;

            if (direction > 0 ? larger : !larger)
            {
                index = i;
                if (direction > 0)
                    break;
            }
        }
    }
    else
    {
        index = Wrap(index + direction, DISPLAY_RESOLUTION_COUNT);
    }
    settings->width = gDisplayResolutions[index].width;
    settings->height = gDisplayResolutions[index].height;
    settings->resolutionKept = true;
}

static void QualityValueChanged(struct DisplaySettings *settings)
{
    settings->quality = DisplaySettings_MatchQuality(settings);
    settings->qualityKept = true;
}

static unsigned int Change(struct PcMenuState *menu, int item, int direction, unsigned int nowMs)
{
    struct DisplaySettings *settings = &menu->settings;
    struct DisplaySettings before = *settings;
    static const unsigned char sSamples[] = {0, 2, 4, 8};
    int index;

    switch (item)
    {
    case PC_MENU_RESOLUTION:
        StepResolution(settings, direction);
        break;
    case PC_MENU_DISPLAY_MODE:
        settings->mode = Wrap(settings->mode + direction, DISPLAY_MODE_COUNT);
        if (settings->mode != DISPLAY_MODE_WINDOWED)
            settings->fullscreenMode = settings->mode;
        settings->modeKept = true;
        break;
    case PC_MENU_RENDER_SCALE:
        settings->renderScale = DisplaySettings_StepRenderScale(settings->renderScale, direction);
        settings->renderScaleKept = true;
        return PC_MENU_CHANGED_RENDER_SCALE;
    case PC_MENU_VSYNC:
        menu->vsync = !menu->vsync;
        return PC_MENU_CHANGED_VSYNC;
    case PC_MENU_FPS_CAP:
        settings->fpsCap = DisplaySettings_StepFpsCap(settings->fpsCap, direction);
        settings->fpsCapKept = true;
        return PC_MENU_CHANGED_FPS_CAP;
    case PC_MENU_QUALITY:
        index = settings->quality == QUALITY_CUSTOM ? QUALITY_HIGH : settings->quality;
        DisplaySettings_SetQuality(settings, Wrap(index + direction, QUALITY_CUSTOM));
        settings->qualityKept = true;
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_SHADOWS:
        settings->shadows = Wrap(settings->shadows + direction, SHADOWS_COUNT);
        QualityValueChanged(settings);
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_LIGHTING:
        settings->lighting = !settings->lighting;
        QualityValueChanged(settings);
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_EFFECTS:
        settings->effects = !settings->effects;
        QualityValueChanged(settings);
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_DETAILS:
        settings->details = !settings->details;
        QualityValueChanged(settings);
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_ANTIALIASING:
        index = 0;
        for (int i = 0; i < 4; i++)
        {
            if (sSamples[i] == settings->antialiasing)
                index = i;
        }
        settings->antialiasing = sSamples[Wrap(index + direction, 4)];
        QualityValueChanged(settings);
        return PC_MENU_CHANGED_QUALITY;
    case PC_MENU_MEMORY_BUDGET:
        settings->memoryBudget = DisplaySettings_StepMemoryBudget(settings->memoryBudget, direction);
        settings->memoryBudgetKept = true;
        return PC_MENU_CHANGED_MEMORY_BUDGET;
    case PC_MENU_PICTURE_SIZE:
        menu->pictureFill = !menu->pictureFill;
        return PC_MENU_CHANGED_PICTURE;
    case PC_MENU_FRAME_RATE:
        menu->showFrameRate = !menu->showFrameRate;
        return PC_MENU_CHANGED_FRAME_RATE;
    case PC_MENU_CAMERA:
        menu->cameraDiorama = !menu->cameraDiorama;
        return PC_MENU_CHANGED_CAMERA;
    case PC_MENU_TREES:
        menu->trees3ds = !menu->trees3ds;
        return PC_MENU_CHANGED_TREES;
    default:
        return 0;
    }
    /* A resolution or display mode: shown at once, kept only when confirmed. */
    if (settings->width == before.width && settings->height == before.height
     && settings->mode == before.mode)
        return 0;
    menu->previous = before;
    menu->confirming = true;
    menu->confirmStartMs = nowMs;
    return PC_MENU_CHANGED_DISPLAY;
}

static unsigned int Revert(struct PcMenuState *menu)
{
    menu->settings.width = menu->previous.width;
    menu->settings.height = menu->previous.height;
    menu->settings.mode = menu->previous.mode;
    menu->settings.fullscreenMode = menu->previous.fullscreenMode;
    menu->settings.resolutionKept = menu->previous.resolutionKept;
    menu->settings.modeKept = menu->previous.modeKept;
    menu->confirming = false;
    return PC_MENU_CHANGED_DISPLAY | PC_MENU_DISPLAY_REVERTED;
}

unsigned int PcMenu_Input(struct PcMenuState *menu, enum PcMenuInput input, unsigned int nowMs)
{
    if (!menu->open)
        return 0;
    if (menu->confirming)
    {
        if (input == PC_MENU_INPUT_ACCEPT)
        {
            menu->confirming = false;
            return PC_MENU_DISPLAY_KEPT;
        }
        if (input == PC_MENU_INPUT_BACK)
            return Revert(menu);
        return 0;
    }
    if (menu->page == PC_MENU_PAGE_CONTROLS)
        return ControlsInput(menu, input);
    switch (input)
    {
    case PC_MENU_INPUT_UP:
    case PC_MENU_INPUT_DOWN:
    {
        int step = input == PC_MENU_INPUT_UP ? -1 : 1;

        do
            menu->cursor = Wrap(menu->cursor + step, PC_MENU_ITEM_COUNT);
        while (!Visible(menu, menu->cursor));
        return 0;
    }
    case PC_MENU_INPUT_LEFT:
        return Change(menu, menu->cursor, -1, nowMs);
    case PC_MENU_INPUT_RIGHT:
        return Change(menu, menu->cursor, 1, nowMs);
    case PC_MENU_INPUT_ACCEPT:
        if (menu->cursor == PC_MENU_CLOSE)
        {
            menu->open = false;
            return PC_MENU_CLOSED;
        }
        if (menu->cursor == PC_MENU_CONTROLS)
        {
            menu->page = PC_MENU_PAGE_CONTROLS;
            menu->controlsCursor = 0;
            menu->capturing = menu->conflicting = false;
            menu->message[0] = '\0';
            return 0;
        }
        return Change(menu, menu->cursor, 1, nowMs);
    case PC_MENU_INPUT_BACK:
        menu->open = false;
        return PC_MENU_CLOSED;
    }
    return 0;
}

unsigned int PcMenu_Tick(struct PcMenuState *menu, unsigned int nowMs)
{
    if (menu->confirming && nowMs - menu->confirmStartMs >= PC_MENU_CONFIRM_MS)
        return Revert(menu);
    return 0;
}

int PcMenu_ConfirmSecondsLeft(const struct PcMenuState *menu, unsigned int nowMs)
{
    unsigned int elapsed;

    if (!menu->confirming)
        return -1;
    elapsed = nowMs - menu->confirmStartMs;
    if (elapsed >= PC_MENU_CONFIRM_MS)
        return 0;
    return (int)((PC_MENU_CONFIRM_MS - elapsed + 999) / 1000);
}

static void Megabytes(unsigned int megabytes, char *buffer, size_t size)
{
    if (megabytes >= 1024 && megabytes % 1024 == 0)
        snprintf(buffer, size, "%u GB", megabytes / 1024);
    else if (megabytes >= 1024)
        snprintf(buffer, size, "%u.%u GB", megabytes / 1024, (megabytes % 1024) * 10 / 1024);
    else
        snprintf(buffer, size, "%u MB", megabytes);
}

static void MemoryBudgetValue(const struct PcMenuState *menu, char *value, size_t size)
{
    char budget[16];
    char effective[16];
    char used[16];
    char text[96];
    size_t length;

    Megabytes(menu->settings.memoryBudget, budget, sizeof(budget));
    Megabytes(menu->memoryEffectiveMB, effective, sizeof(effective));
    Megabytes(menu->memoryInUseMB, used, sizeof(used));
    if (menu->settings.memoryBudget == 0)
        snprintf(text, sizeof(text), menu->memoryEffectiveMB != 0 ? "Auto: %s" : "Auto", effective);
    else if (menu->memoryEffectiveMB != 0 && menu->memoryEffectiveMB < menu->settings.memoryBudget)
        snprintf(text, sizeof(text), "%s (%s usable)", budget, effective);
    else
        snprintf(text, sizeof(text), "%s", budget);
    length = strlen(text);
    if (menu->memoryInUseMB != 0 && length + strlen(used) + 7 < size)
        snprintf(text + length, sizeof(text) - length, ", %s used", used);
    length = strlen(text);
    if (length >= size)
        length = size - 1;
    memcpy(value, text, length);
    value[length] = '\0';
}

static const char *Capitalized(const char *name, char *buffer, size_t size)
{
    snprintf(buffer, size, "%s", name);
    if (buffer[0] >= 'a' && buffer[0] <= 'z')
        buffer[0] -= 'a' - 'A';
    return buffer;
}

int PcMenu_Rows(const struct PcMenuState *menu, struct PcMenuRow *rows)
{
    const struct DisplaySettings *settings = &menu->settings;
    char name[16];
    int count = 0;

    if (menu->page == PC_MENU_PAGE_CONTROLS)
    {
        for (int row = 0; row < CONTROLS_ROW_COUNT; row++)
        {
            struct PcMenuRow *r = &rows[count++];

            r->value[0] = '\0';
            r->selected = row == menu->controlsCursor;
            r->adjustable = false;
            if (row < KEYBIND_COUNT)
            {
                r->label = Keybinds_ActionLabel(row);
                if (menu->capturing && r->selected)
                    snprintf(r->value, sizeof(r->value), "Press a key...");
                else
                    KeyText(menu, menu->keybinds.key[row], r->value, (int)sizeof(r->value));
            }
            else
                r->label = row == CONTROLS_ROW_RESTORE ? "Restore defaults" : "Back";
        }
        return count;
    }
    for (int item = 0; item < PC_MENU_ITEM_COUNT; item++)
    {
        struct PcMenuRow *row;

        if (!Visible(menu, item))
            continue;
        row = &rows[count++];
        row->label = sLabels[item];
        row->value[0] = '\0';
        row->selected = item == menu->cursor;
        row->adjustable = item != PC_MENU_CLOSE && item != PC_MENU_CONTROLS;
        switch (item)
        {
        case PC_MENU_RESOLUTION:
            snprintf(row->value, sizeof(row->value), "%d x %d", settings->width, settings->height);
            break;
        case PC_MENU_DISPLAY_MODE:
            snprintf(row->value, sizeof(row->value), "%s",
                     Capitalized(DisplaySettings_ModeName(settings->mode), name, sizeof(name)));
            break;
        case PC_MENU_RENDER_SCALE:
            if (settings->renderScale >= 100)
                snprintf(row->value, sizeof(row->value), "100%% (native)");
            else
                snprintf(row->value, sizeof(row->value), "%u%% (upscaled)", settings->renderScale);
            break;
        case PC_MENU_VSYNC:
            snprintf(row->value, sizeof(row->value), "%s", menu->vsync ? "On" : "Off");
            break;
        case PC_MENU_FPS_CAP:
            if (settings->fpsCap == 0)
                snprintf(row->value, sizeof(row->value), "Unlimited");
            else
                snprintf(row->value, sizeof(row->value), "%u fps", settings->fpsCap);
            break;
        case PC_MENU_QUALITY:
            snprintf(row->value, sizeof(row->value), "%s",
                     Capitalized(DisplaySettings_QualityName(settings->quality), name, sizeof(name)));
            break;
        case PC_MENU_SHADOWS:
            snprintf(row->value, sizeof(row->value), "%s",
                     Capitalized(DisplaySettings_ShadowName(settings->shadows), name, sizeof(name)));
            break;
        case PC_MENU_LIGHTING:
            snprintf(row->value, sizeof(row->value), "%s", settings->lighting ? "On" : "Off");
            break;
        case PC_MENU_EFFECTS:
            snprintf(row->value, sizeof(row->value), "%s", settings->effects ? "On" : "Off");
            break;
        case PC_MENU_DETAILS:
            snprintf(row->value, sizeof(row->value), "%s", settings->details ? "On" : "Off");
            break;
        case PC_MENU_ANTIALIASING:
            if (settings->antialiasing == 0)
                snprintf(row->value, sizeof(row->value), "Off");
            else
                snprintf(row->value, sizeof(row->value), "%ux MSAA", settings->antialiasing);
            break;
        case PC_MENU_MEMORY_BUDGET:
            MemoryBudgetValue(menu, row->value, sizeof(row->value));
            break;
        case PC_MENU_PICTURE_SIZE:
            snprintf(row->value, sizeof(row->value), "%s", menu->pictureFill ? "Largest" : "Standard");
            break;
        case PC_MENU_FRAME_RATE:
            snprintf(row->value, sizeof(row->value), "%s", menu->showFrameRate ? "On" : "Off");
            break;
        case PC_MENU_CAMERA:
            snprintf(row->value, sizeof(row->value), "%s", menu->cameraDiorama ? "Diorama" : "Adventure");
            break;
        case PC_MENU_TREES:
            snprintf(row->value, sizeof(row->value), "%s", menu->trees3ds ? "3DS" : "Voxel");
            break;
        }
    }
    return count;
}
