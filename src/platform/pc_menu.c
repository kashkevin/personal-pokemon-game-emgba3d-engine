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
    [PC_MENU_VSYNC]        = "VSync",
    [PC_MENU_QUALITY]      = "Graphics quality",
    [PC_MENU_SHADOWS]      = "Shadows",
    [PC_MENU_LIGHTING]     = "Lighting",
    [PC_MENU_EFFECTS]      = "Effects",
    [PC_MENU_DETAILS]      = "World details",
    [PC_MENU_ANTIALIASING] = "Anti-aliasing",
    [PC_MENU_PICTURE_SIZE] = "Game picture",
    [PC_MENU_FRAME_RATE]   = "Show frame rate",
    [PC_MENU_CAMERA]       = "Camera",
    [PC_MENU_TREES]        = "Trees",
    [PC_MENU_CLOSE]        = "Close",
};

static bool Visible(const struct PcMenuState *menu, int item)
{
    return menu->hasCamera
        || (item != PC_MENU_CAMERA && item != PC_MENU_TREES && item != PC_MENU_DETAILS);
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
    case PC_MENU_VSYNC:
        menu->vsync = !menu->vsync;
        return PC_MENU_CHANGED_VSYNC;
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

    for (int item = 0; item < PC_MENU_ITEM_COUNT; item++)
    {
        struct PcMenuRow *row;

        if (!Visible(menu, item))
            continue;
        row = &rows[count++];
        row->label = sLabels[item];
        row->value[0] = '\0';
        row->selected = item == menu->cursor;
        row->adjustable = item != PC_MENU_CLOSE;
        switch (item)
        {
        case PC_MENU_RESOLUTION:
            snprintf(row->value, sizeof(row->value), "%d x %d", settings->width, settings->height);
            break;
        case PC_MENU_DISPLAY_MODE:
            snprintf(row->value, sizeof(row->value), "%s",
                     Capitalized(DisplaySettings_ModeName(settings->mode), name, sizeof(name)));
            break;
        case PC_MENU_VSYNC:
            snprintf(row->value, sizeof(row->value), "%s", menu->vsync ? "On" : "Off");
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
