/*
 * Display and graphics settings of the native PC game: see
 * include/platform/display_settings.h. Project-owned (no third-party code).
 */
#include <string.h>

#include "platform/display_settings.h"

const struct DisplayResolution gDisplayResolutions[DISPLAY_RESOLUTION_COUNT] = {
    {1280, 720},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
};

static const char *const sModeNames[DISPLAY_MODE_COUNT] = {"windowed", "borderless", "fullscreen"};
static const char *const sQualityNames[QUALITY_COUNT] = {"low", "medium", "high", "ultra", "custom"};
static const char *const sShadowNames[SHADOWS_COUNT] = {"off", "low", "medium", "high"};

struct QualityValues
{
    unsigned char shadows;
    bool lighting;
    bool effects;
    bool details;
    unsigned char antialiasing;
};

static const struct QualityValues sPresets[QUALITY_CUSTOM] = {
    [QUALITY_LOW]    = {SHADOWS_OFF, false, false, false, 0},
    [QUALITY_MEDIUM] = {SHADOWS_LOW, true, false, true, 0},
    [QUALITY_HIGH]   = {SHADOWS_MEDIUM, true, true, true, 0},
    [QUALITY_ULTRA]  = {SHADOWS_HIGH, true, true, true, 4},
};

void DisplaySettings_Defaults(struct DisplaySettings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->width = gDisplayResolutions[DISPLAY_DEFAULT_RESOLUTION].width;
    settings->height = gDisplayResolutions[DISPLAY_DEFAULT_RESOLUTION].height;
    settings->mode = DISPLAY_MODE_WINDOWED;
    settings->fullscreenMode = DISPLAY_MODE_BORDERLESS;
    DisplaySettings_SetQuality(settings, QUALITY_HIGH);
}

static int NameIndex(const char *value, const char *const *names, int count)
{
    for (int i = 0; i < count; i++)
    {
        size_t length = strlen(names[i]);

        if (strncmp(value, names[i], length) == 0
         && (value[length] == '\0' || value[length] == '\n' || value[length] == '\r'))
            return i;
    }
    return -1;
}

/* A single quality value in a file that names no preset (edited by hand) is
 * a custom choice. */
static bool QualityValueRead(struct DisplaySettings *settings)
{
    settings->qualityKept = true;
    if (!settings->qualityNamed)
        settings->quality = QUALITY_CUSTOM;
    return true;
}

static bool ValidResolution(int width, int height)
{
    return width >= DISPLAY_MIN_WIDTH && width <= DISPLAY_MAX_WIDTH
        && height >= DISPLAY_MIN_HEIGHT && height <= DISPLAY_MAX_HEIGHT;
}

bool DisplaySettings_ParseLine(struct DisplaySettings *settings, const char *line)
{
    int width;
    int height;
    unsigned int value;
    int index;

    if (sscanf(line, "resolution=%dx%d", &width, &height) == 2)
    {
        if (ValidResolution(width, height))
        {
            settings->width = width;
            settings->height = height;
            settings->resolutionKept = true;
        }
        return true;
    }
    if (strncmp(line, "displayMode=", 12) == 0)
    {
        index = NameIndex(line + 12, sModeNames, DISPLAY_MODE_COUNT);
        if (index >= 0)
        {
            settings->mode = index;
            if (index != DISPLAY_MODE_WINDOWED)
                settings->fullscreenMode = index;
            settings->modeKept = true;
        }
        return true;
    }
    if (strncmp(line, "fullscreenMode=", 15) == 0)
    {
        index = NameIndex(line + 15, sModeNames, DISPLAY_MODE_COUNT);
        if (index > DISPLAY_MODE_WINDOWED)
            settings->fullscreenMode = index;
        return true;
    }
    if (strncmp(line, "quality=", 8) == 0)
    {
        index = NameIndex(line + 8, sQualityNames, QUALITY_COUNT);
        if (index >= 0)
        {
            settings->quality = index;
            settings->qualityKept = true;
            settings->qualityNamed = true;
        }
        return true;
    }
    if (strncmp(line, "shadows=", 8) == 0)
    {
        index = NameIndex(line + 8, sShadowNames, SHADOWS_COUNT);
        if (index >= 0)
            settings->shadows = index;
        return QualityValueRead(settings);
    }
    if (sscanf(line, "lighting=%u", &value) == 1)
    {
        settings->lighting = value != 0;
        return QualityValueRead(settings);
    }
    if (sscanf(line, "effects=%u", &value) == 1)
    {
        settings->effects = value != 0;
        return QualityValueRead(settings);
    }
    if (sscanf(line, "details=%u", &value) == 1)
    {
        settings->details = value != 0;
        return QualityValueRead(settings);
    }
    if (sscanf(line, "antialiasing=%u", &value) == 1)
    {
        if (value == 0 || value == 2 || value == 4 || value == 8)
            settings->antialiasing = value;
        return QualityValueRead(settings);
    }
    return false;
}

void DisplaySettings_Finish(struct DisplaySettings *settings)
{
    /* A preset decides its own values; only "custom" keeps the file's. */
    if (settings->quality != QUALITY_CUSTOM)
        DisplaySettings_SetQuality(settings, settings->quality);
    settings->qualityNamed = false;
}

void DisplaySettings_ApplyLegacy(struct DisplaySettings *settings, int fullscreen, int windowScale)
{
    if (!settings->modeKept && fullscreen > 0)
    {
        settings->mode = DISPLAY_MODE_BORDERLESS;
        settings->fullscreenMode = DISPLAY_MODE_BORDERLESS;
        settings->modeKept = true;
    }
    if (!settings->resolutionKept && windowScale >= 2 && windowScale <= 6 && windowScale != 4)
    {
        settings->width = 320 * windowScale;
        settings->height = 180 * windowScale;
        settings->resolutionKept = true;
    }
}

int DisplaySettings_LegacyWindowScale(const struct DisplaySettings *settings)
{
    for (int scale = 2; scale <= 6; scale++)
    {
        if (settings->width == 320 * scale && settings->height == 180 * scale)
            return scale;
    }
    return 0;
}

void DisplaySettings_Write(const struct DisplaySettings *settings, FILE *file)
{
    if (settings->resolutionKept)
        fprintf(file, "resolution=%dx%d\n", settings->width, settings->height);
    if (settings->modeKept)
    {
        fprintf(file, "displayMode=%s\n", sModeNames[settings->mode]);
        fprintf(file, "fullscreenMode=%s\n", sModeNames[settings->fullscreenMode]);
    }
    if (settings->qualityKept)
    {
        fprintf(file, "quality=%s\n", sQualityNames[settings->quality]);
        fprintf(file, "shadows=%s\n", sShadowNames[settings->shadows]);
        fprintf(file, "lighting=%u\n", settings->lighting ? 1 : 0);
        fprintf(file, "effects=%u\n", settings->effects ? 1 : 0);
        fprintf(file, "details=%u\n", settings->details ? 1 : 0);
        fprintf(file, "antialiasing=%u\n", settings->antialiasing);
    }
}

void DisplaySettings_SetQuality(struct DisplaySettings *settings, unsigned char preset)
{
    if (preset >= QUALITY_CUSTOM)
    {
        settings->quality = QUALITY_CUSTOM;
        return;
    }
    settings->quality = preset;
    settings->shadows = sPresets[preset].shadows;
    settings->lighting = sPresets[preset].lighting;
    settings->effects = sPresets[preset].effects;
    settings->details = sPresets[preset].details;
    settings->antialiasing = sPresets[preset].antialiasing;
}

unsigned char DisplaySettings_MatchQuality(const struct DisplaySettings *settings)
{
    for (int preset = 0; preset < QUALITY_CUSTOM; preset++)
    {
        if (sPresets[preset].shadows == settings->shadows
         && sPresets[preset].lighting == settings->lighting
         && sPresets[preset].effects == settings->effects
         && sPresets[preset].details == settings->details
         && sPresets[preset].antialiasing == settings->antialiasing)
            return preset;
    }
    return QUALITY_CUSTOM;
}

void DisplaySettings_FitWindow(int width, int height, int boundsWidth, int boundsHeight,
                               int *outWidth, int *outHeight)
{
    if (boundsWidth <= 0 || boundsHeight <= 0 || (width <= boundsWidth && height <= boundsHeight))
    {
        *outWidth = width;
        *outHeight = height;
        return;
    }
    /* The same shape, as large as the space allows. */
    if ((long long)width * boundsHeight > (long long)height * boundsWidth)
    {
        *outWidth = boundsWidth;
        *outHeight = (int)((long long)height * boundsWidth / width);
    }
    else
    {
        *outHeight = boundsHeight;
        *outWidth = (int)((long long)width * boundsHeight / height);
    }
    if (*outWidth < DISPLAY_MIN_WIDTH)
        *outWidth = DISPLAY_MIN_WIDTH;
    if (*outHeight < DISPLAY_MIN_HEIGHT)
        *outHeight = DISPLAY_MIN_HEIGHT;
}

void DisplaySettings_RenderSize(int height, int outputWidth, int outputHeight,
                                int *renderWidth, int *renderHeight)
{
    if (height <= 0 || outputWidth <= 0 || outputHeight <= 0 || height == outputHeight)
    {
        *renderWidth = outputWidth;
        *renderHeight = outputHeight;
        return;
    }
    *renderHeight = height;
    *renderWidth = (int)(((long long)outputWidth * height + outputHeight / 2) / outputHeight);
    if (*renderWidth < 1)
        *renderWidth = 1;
}

int DisplaySettings_PixelScale(int outputWidth, int outputHeight, bool fill)
{
    int scale;

    if (fill)
    {
        scale = outputWidth / 240;
        if (outputHeight / 160 < scale)
            scale = outputHeight / 160;
    }
    else
    {
        scale = outputHeight * 8 / 9 / 160;
        if (outputWidth / 240 < scale)
            scale = outputWidth / 240;
    }
    return scale < 1 ? 1 : scale;
}

int DisplaySettings_ShadowMapSize(unsigned char shadows)
{
    switch (shadows)
    {
    case SHADOWS_LOW:
        return 1024;
    case SHADOWS_MEDIUM:
        return 2048;
    case SHADOWS_HIGH:
        return 4096;
    default:
        return 0;
    }
}

const char *DisplaySettings_ModeName(unsigned char mode)
{
    return mode < DISPLAY_MODE_COUNT ? sModeNames[mode] : "?";
}

const char *DisplaySettings_QualityName(unsigned char quality)
{
    return quality < QUALITY_COUNT ? sQualityNames[quality] : "?";
}

const char *DisplaySettings_ShadowName(unsigned char shadows)
{
    return shadows < SHADOWS_COUNT ? sShadowNames[shadows] : "?";
}

int DisplaySettings_ResolutionIndex(int width, int height)
{
    for (int i = 0; i < DISPLAY_RESOLUTION_COUNT; i++)
    {
        if (gDisplayResolutions[i].width == width && gDisplayResolutions[i].height == height)
            return i;
    }
    return -1;
}
