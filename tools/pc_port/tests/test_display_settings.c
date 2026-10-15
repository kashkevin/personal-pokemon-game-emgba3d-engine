// Display settings, the PC settings menu's model and the benchmark's
// bookkeeping (include/platform/display_settings.h, pc_menu.h, benchmark.h).
#include <stdlib.h>

#include "minitest.h"
#include "platform/benchmark.h"
#include "platform/display_settings.h"
#include "platform/frame_limiter.h"
#include "platform/pc_menu.h"

static struct DisplaySettings Parse(const char *const *lines, int count)
{
    struct DisplaySettings settings;

    DisplaySettings_Defaults(&settings);
    for (int i = 0; i < count; i++)
        DisplaySettings_ParseLine(&settings, lines[i]);
    DisplaySettings_Finish(&settings);
    return settings;
}

MT_TEST(defaults_are_1080p_windowed_high)
{
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    MT_EQ(s.width, 1920);
    MT_EQ(s.height, 1080);
    MT_EQ(s.mode, DISPLAY_MODE_WINDOWED);
    MT_EQ(s.fullscreenMode, DISPLAY_MODE_BORDERLESS);
    MT_EQ(s.quality, QUALITY_HIGH);
    MT_EQ(s.shadows, SHADOWS_MEDIUM);
    MT_CHECK(s.lighting && s.effects && s.details);
    MT_EQ(s.antialiasing, 0);
    MT_CHECK(!s.resolutionKept && !s.modeKept && !s.qualityKept);
    MT_EQ(DisplaySettings_ShadowMapSize(s.shadows), 2048); // the look before these settings
}

MT_TEST(file_round_trip)
{
    static const char *const lines[] = {
        "resolution=2560x1440\n", "displayMode=fullscreen\n", "quality=custom\n",
        "shadows=high\n", "lighting=0\n", "effects=1\n", "antialiasing=4\n", "details=0\n",
    };
    struct DisplaySettings s = Parse(lines, 8);
    struct DisplaySettings again;
    char buffer[512];
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");
    char *line;

    MT_EQ(s.width, 2560);
    MT_EQ(s.height, 1440);
    MT_EQ(s.mode, DISPLAY_MODE_FULLSCREEN);
    MT_EQ(s.fullscreenMode, DISPLAY_MODE_FULLSCREEN);
    MT_EQ(s.quality, QUALITY_CUSTOM);
    MT_EQ(s.shadows, SHADOWS_HIGH);
    MT_CHECK(!s.lighting && s.effects && !s.details);
    MT_EQ(s.antialiasing, 4);
    DisplaySettings_Write(&s, file);
    fclose(file);
    DisplaySettings_Defaults(&again);
    for (line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        DisplaySettings_ParseLine(&again, line);
    DisplaySettings_Finish(&again);
    MT_EQ(again.width, 2560);
    MT_EQ(again.mode, DISPLAY_MODE_FULLSCREEN);
    MT_EQ(again.quality, QUALITY_CUSTOM);
    MT_EQ(again.shadows, SHADOWS_HIGH);
    MT_CHECK(!again.lighting && again.effects && !again.details);
    MT_EQ(again.antialiasing, 4);
}

MT_TEST(nothing_written_until_chosen)
{
    struct DisplaySettings s;
    char buffer[256] = {0};
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");

    DisplaySettings_Defaults(&s);
    DisplaySettings_Write(&s, file);
    fclose(file);
    MT_EQ(strlen(buffer), 0);
}

MT_TEST(a_preset_decides_its_values)
{
    static const char *const lines[] = {"quality=low\n", "shadows=high\n", "effects=1\n"};
    struct DisplaySettings s = Parse(lines, 3);

    MT_EQ(s.quality, QUALITY_LOW);
    MT_EQ(s.shadows, SHADOWS_OFF);
    MT_CHECK(!s.lighting && !s.effects && !s.details);
    DisplaySettings_SetQuality(&s, QUALITY_ULTRA);
    MT_EQ(s.shadows, SHADOWS_HIGH);
    MT_EQ(s.antialiasing, 4);
    MT_CHECK(s.details);
    MT_EQ(DisplaySettings_MatchQuality(&s), QUALITY_ULTRA);
    s.effects = false;
    MT_EQ(DisplaySettings_MatchQuality(&s), QUALITY_CUSTOM);
    DisplaySettings_SetQuality(&s, QUALITY_MEDIUM);
    MT_CHECK(s.details);
    s.details = false;
    MT_EQ(DisplaySettings_MatchQuality(&s), QUALITY_CUSTOM);
}

MT_TEST(hand_edited_values_are_custom)
{
    static const char *const lines[] = {"shadows=low\n", "effects=0\n"};
    struct DisplaySettings s = Parse(lines, 2);

    MT_EQ(s.quality, QUALITY_CUSTOM);
    MT_EQ(s.shadows, SHADOWS_LOW);
    MT_CHECK(s.lighting && !s.effects);
    MT_CHECK(s.qualityKept);
}

MT_TEST(bad_values_are_ignored)
{
    static const char *const lines[] = {
        "resolution=100x100\n", "resolution=9000x5000\n", "displayMode=sideways\n",
        "quality=extreme\n", "antialiasing=3\n", "shadows=ultra\n",
    };
    struct DisplaySettings s = Parse(lines, 6);

    MT_EQ(s.width, 1920);
    MT_EQ(s.mode, DISPLAY_MODE_WINDOWED);
    MT_CHECK(!s.resolutionKept && !s.modeKept);
    MT_EQ(s.antialiasing, 0);
}

MT_TEST(legacy_files)
{
    struct DisplaySettings s;

    // windowScale=4 was every older file's default, not a choice: 1080p stays.
    DisplaySettings_Defaults(&s);
    DisplaySettings_ApplyLegacy(&s, 0, 4);
    MT_EQ(s.width, 1920);
    MT_CHECK(!s.resolutionKept && !s.modeKept);
    // windowScale=2 (the test harnesses) and fullscreen=1 (F11, the borderless kind).
    DisplaySettings_ApplyLegacy(&s, 1, 2);
    MT_EQ(s.width, 640);
    MT_EQ(s.height, 360);
    MT_EQ(s.mode, DISPLAY_MODE_BORDERLESS);
    MT_CHECK(s.resolutionKept && s.modeKept);
    MT_EQ(DisplaySettings_LegacyWindowScale(&s), 2);
    // New keys win over old ones.
    {
        static const char *const lines[] = {"resolution=3840x2160\n", "displayMode=windowed\n"};
        struct DisplaySettings t = Parse(lines, 2);

        DisplaySettings_ApplyLegacy(&t, 1, 3);
        MT_EQ(t.width, 3840);
        MT_EQ(t.mode, DISPLAY_MODE_WINDOWED);
        MT_EQ(DisplaySettings_LegacyWindowScale(&t), 0);
    }
}

MT_TEST(window_fits_the_display)
{
    int w;
    int h;

    DisplaySettings_FitWindow(1920, 1080, 2544, 1392, &w, &h);
    MT_EQ(w, 1920);
    MT_EQ(h, 1080);
    // 1080p on a 1080p display (title bar, taskbar): same shape, smaller.
    DisplaySettings_FitWindow(1920, 1080, 1904, 992, &w, &h);
    MT_EQ(h, 992);
    MT_EQ(w, 1763);
    // 4K on a 1440p display.
    DisplaySettings_FitWindow(3840, 2160, 2544, 1352, &w, &h);
    MT_EQ(w, 2403);
    MT_EQ(h, 1352);
    // Unknown display size: the resolution itself.
    DisplaySettings_FitWindow(2560, 1440, 0, 0, &w, &h);
    MT_EQ(w, 2560);
}

MT_TEST(render_size_keeps_the_window_shape)
{
    int w;
    int h;

    DisplaySettings_RenderSize(1080, 3840, 2160, &w, &h); // borderless 4K desktop, 1080p chosen
    MT_EQ(w, 1920);
    MT_EQ(h, 1080);
    DisplaySettings_RenderSize(1440, 1920, 1200, &w, &h); // 16:10 desktop, 1440p chosen
    MT_EQ(w, 2304);
    MT_EQ(h, 1440);
    DisplaySettings_RenderSize(1080, 1920, 1080, &w, &h); // the window's own size
    MT_EQ(w, 1920);
    MT_EQ(h, 1080);
    DisplaySettings_RenderSize(0, 1280, 720, &w, &h);
    MT_EQ(w, 1280);
}

MT_TEST(pixel_art_in_whole_steps)
{
    // Within 8/9 of the height: as before at 16:9.
    MT_EQ(DisplaySettings_PixelScale(1280, 720, false), 4);
    MT_EQ(DisplaySettings_PixelScale(1920, 1080, false), 6);
    MT_EQ(DisplaySettings_PixelScale(2560, 1440, false), 8);
    MT_EQ(DisplaySettings_PixelScale(3840, 2160, false), 12);
    // A window of any other size: still a whole number.
    MT_EQ(DisplaySettings_PixelScale(1763, 992, false), 5);
    MT_EQ(DisplaySettings_PixelScale(1000, 300, false), 1);
    // As large as fits.
    MT_EQ(DisplaySettings_PixelScale(2560, 1440, true), 9);
    MT_EQ(DisplaySettings_PixelScale(1920, 1080, true), 6);
    MT_EQ(DisplaySettings_PixelScale(200, 100, true), 1);
}

MT_TEST(menu_changes_and_confirmation)
{
    struct PcMenuState menu;
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DisplaySettings s;
    unsigned int bits;

    DisplaySettings_Defaults(&s);
    PcMenu_Open(&menu, &s, true, false, false, true, true, true);
    MT_EQ(PcMenu_Rows(&menu, rows), PC_MENU_ITEM_COUNT);
    MT_CHECK(rows[0].selected);
    // Right on Resolution: 2560x1440, shown at once, waiting for "keep".
    bits = PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 1000);
    MT_EQ(bits, PC_MENU_CHANGED_DISPLAY);
    MT_EQ(menu.settings.width, 2560);
    MT_CHECK(menu.confirming);
    MT_EQ(PcMenu_ConfirmSecondsLeft(&menu, 1000), 15);
    // Nothing else moves until it is answered.
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 2000), 0);
    MT_EQ(menu.cursor, PC_MENU_RESOLUTION);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 3000), PC_MENU_DISPLAY_KEPT);
    MT_CHECK(!menu.confirming && menu.settings.resolutionKept);
    // Display mode: fullscreen not answered, so it goes back by itself.
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 4000);
    bits = PcMenu_Input(&menu, PC_MENU_INPUT_LEFT, 4000);
    MT_EQ(bits, PC_MENU_CHANGED_DISPLAY);
    MT_EQ(menu.settings.mode, DISPLAY_MODE_FULLSCREEN);
    MT_EQ(PcMenu_Tick(&menu, 18999), 0);
    bits = PcMenu_Tick(&menu, 19000);
    MT_EQ(bits, PC_MENU_CHANGED_DISPLAY | PC_MENU_DISPLAY_REVERTED);
    MT_EQ(menu.settings.mode, DISPLAY_MODE_WINDOWED);
    MT_EQ(menu.settings.width, 2560); // the resolution kept before stays
    // Esc undoes a waiting change.
    PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 20000);
    MT_EQ(menu.settings.mode, DISPLAY_MODE_BORDERLESS);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_BACK, 21000), PC_MENU_CHANGED_DISPLAY | PC_MENU_DISPLAY_REVERTED);
    MT_EQ(menu.settings.mode, DISPLAY_MODE_WINDOWED);
    MT_CHECK(menu.open);
    // Quality: the preset, then one value makes it custom.
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 22000); // 3D render scale
    MT_EQ(menu.cursor, PC_MENU_RENDER_SCALE);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 22000); // VSync
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 22000), PC_MENU_CHANGED_VSYNC);
    MT_CHECK(!menu.vsync);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 22000); // Frame rate limit
    MT_EQ(menu.cursor, PC_MENU_FPS_CAP);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 22000); // Graphics quality
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 22000), PC_MENU_CHANGED_QUALITY);
    MT_EQ(menu.settings.quality, QUALITY_ULTRA);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 22000); // Shadows
    PcMenu_Input(&menu, PC_MENU_INPUT_LEFT, 22000);
    MT_EQ(menu.settings.shadows, SHADOWS_MEDIUM);
    MT_EQ(menu.settings.quality, QUALITY_CUSTOM);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_QUALITY].value, "Custom") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_RESOLUTION].value, "2560 x 1440") == 0);
    // Up from the first row reaches Close; Accept there closes.
    menu.cursor = PC_MENU_RESOLUTION;
    PcMenu_Input(&menu, PC_MENU_INPUT_UP, 23000);
    MT_EQ(menu.cursor, PC_MENU_CLOSE);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 23000), PC_MENU_CLOSED);
    MT_CHECK(!menu.open);
}

MT_TEST(menu_without_camera_rows)
{
    struct PcMenuState menu;
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    PcMenu_Open(&menu, &s, true, false, false, false, false, false);
    MT_EQ(PcMenu_Rows(&menu, rows), PC_MENU_ITEM_COUNT - 5); // no render scale, memory, camera, trees or details
    menu.cursor = PC_MENU_DISPLAY_MODE;
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_VSYNC);
    menu.cursor = PC_MENU_EFFECTS;
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_ANTIALIASING);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_PICTURE_SIZE);
    menu.cursor = PC_MENU_FRAME_RATE;
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_CONTROLS); // the Controls page comes before Close
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_CLOSE);
}

MT_TEST(menu_world_details)
{
    struct PcMenuState menu;
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    PcMenu_Open(&menu, &s, true, false, false, true, true, true);
    menu.cursor = PC_MENU_EFFECTS;
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_DETAILS);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_DETAILS].label, "World details") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_DETAILS].value, "On") == 0);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0), PC_MENU_CHANGED_QUALITY);
    MT_CHECK(!menu.settings.details);
    MT_EQ(menu.settings.quality, QUALITY_CUSTOM);
    MT_CHECK(menu.settings.qualityKept);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_DETAILS].value, "Off") == 0);
}

MT_TEST(menu_from_a_resolution_not_listed)
{
    struct PcMenuState menu;
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    s.width = 640;
    s.height = 360;
    PcMenu_Open(&menu, &s, true, false, false, true, true, true);
    PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 0);
    MT_EQ(menu.settings.width, 1280);
    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    menu.settings.width = 3000;
    menu.settings.height = 1700;
    PcMenu_Input(&menu, PC_MENU_INPUT_LEFT, 0);
    MT_EQ(menu.settings.height, 1440);
}

MT_TEST(frame_rate_limit_setting)
{
    static const char *const lines[] = {"fpsCap=144\n"};
    static const char *const unlimited[] = {"fpsCap=unlimited\n"};
    static const char *const bad[] = {"fpsCap=5\n", "fpsCap=fast\n", "renderScale=10\n", "renderScale=101\n"};
    struct DisplaySettings s = Parse(lines, 1);
    char buffer[256] = {0};
    FILE *file;

    MT_EQ(s.fpsCap, 144);
    MT_CHECK(s.fpsCapKept && !s.renderScaleKept);
    MT_EQ(s.quality, QUALITY_HIGH); // not a quality value
    file = fmemopen(buffer, sizeof(buffer), "w");
    DisplaySettings_Write(&s, file);
    fclose(file);
    MT_CHECK(strstr(buffer, "fpsCap=144\n") != NULL);
    MT_CHECK(strstr(buffer, "renderScale") == NULL);
    s = Parse(unlimited, 1);
    MT_EQ(s.fpsCap, 0);
    MT_CHECK(s.fpsCapKept);
    file = fmemopen(buffer, sizeof(buffer), "w");
    DisplaySettings_Write(&s, file);
    fclose(file);
    MT_CHECK(strstr(buffer, "fpsCap=unlimited\n") != NULL);
    s = Parse(bad, 4);
    MT_EQ(s.fpsCap, 0);
    MT_EQ(s.renderScale, 100);
    MT_CHECK(!s.fpsCapKept && !s.renderScaleKept);
    // The menu's order: 60 120 144 165 180 200 240 unlimited, round again.
    MT_EQ(DisplaySettings_StepFpsCap(0, 1), 60);
    MT_EQ(DisplaySettings_StepFpsCap(60, 1), 120);
    MT_EQ(DisplaySettings_StepFpsCap(144, 1), 165);
    MT_EQ(DisplaySettings_StepFpsCap(240, 1), 0);
    MT_EQ(DisplaySettings_StepFpsCap(60, -1), 0);
    MT_EQ(DisplaySettings_StepFpsCap(0, -1), 240);
    // A limit of the file's own goes to the nearest listed one that way.
    MT_EQ(DisplaySettings_StepFpsCap(90, 1), 120);
    MT_EQ(DisplaySettings_StepFpsCap(90, -1), 60);
    MT_EQ(DisplaySettings_StepFpsCap(500, 1), 0);
    MT_EQ(DisplaySettings_StepFpsCap(500, -1), 240);
    MT_EQ(DisplaySettings_StepFpsCap(40, -1), 0);
}

MT_TEST(render_scale_setting)
{
    static const char *const lines[] = {"renderScale=75\n"};
    struct DisplaySettings s = Parse(lines, 1);
    char buffer[256] = {0};
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");

    MT_EQ(s.renderScale, 75);
    MT_CHECK(s.renderScaleKept);
    DisplaySettings_Write(&s, file);
    fclose(file);
    MT_CHECK(strstr(buffer, "renderScale=75\n") != NULL);
    DisplaySettings_Defaults(&s);
    MT_EQ(s.renderScale, 100);
    MT_EQ(DisplaySettings_ScaledHeight(2160, 100), 2160);
    MT_EQ(DisplaySettings_ScaledHeight(2160, 50), 1080);
    MT_EQ(DisplaySettings_ScaledHeight(1080, 83), 896);
    MT_EQ(DisplaySettings_ScaledHeight(0, 50), 0);
    MT_EQ(DisplaySettings_StepRenderScale(100, 1), 83);
    MT_EQ(DisplaySettings_StepRenderScale(50, 1), 100);
    MT_EQ(DisplaySettings_StepRenderScale(100, -1), 50);
    MT_EQ(DisplaySettings_StepRenderScale(90, 1), 83);
    MT_EQ(DisplaySettings_StepRenderScale(90, -1), 100);
    MT_EQ(DisplaySettings_StepRenderScale(60, 1), 50);
    MT_EQ(DisplaySettings_StepRenderScale(60, -1), 67);
}

MT_TEST(menu_frame_rate_and_render_scale)
{
    struct PcMenuState menu;
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    PcMenu_Open(&menu, &s, true, false, false, true, true, true);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_RENDER_SCALE].label, "3D render scale") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_RENDER_SCALE].value, "100% (native)") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_FPS_CAP].label, "Frame rate limit") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_FPS_CAP].value, "Unlimited") == 0);
    menu.cursor = PC_MENU_FPS_CAP;
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 0), PC_MENU_CHANGED_FPS_CAP);
    MT_EQ(menu.settings.fpsCap, 60);
    MT_CHECK(menu.settings.fpsCapKept);
    PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 0);
    PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 0);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_FPS_CAP].value, "144 fps") == 0);
    menu.cursor = PC_MENU_RENDER_SCALE;
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_RIGHT, 0), PC_MENU_CHANGED_RENDER_SCALE);
    MT_EQ(menu.settings.renderScale, 83);
    MT_CHECK(menu.settings.renderScaleKept && !menu.confirming);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_RENDER_SCALE].value, "83% (upscaled)") == 0);
    MT_EQ(menu.settings.quality, QUALITY_HIGH); // neither is a quality value
}

MT_TEST(memory_budget_setting)
{
    static const char *const lines[] = {"memoryBudget=2048\n"};
    static const char *const automatic[] = {"memoryBudget=auto\n"};
    static const char *const bad[] = {"memoryBudget=64\n", "memoryBudget=lots\n"};
    struct DisplaySettings s = Parse(lines, 1);
    char buffer[256] = {0};
    FILE *file = fmemopen(buffer, sizeof(buffer), "w");

    MT_EQ(s.memoryBudget, 2048);
    MT_CHECK(s.memoryBudgetKept);
    DisplaySettings_Write(&s, file);
    fclose(file);
    MT_CHECK(strstr(buffer, "memoryBudget=2048\n") != NULL);
    s = Parse(automatic, 1);
    MT_EQ(s.memoryBudget, 0);
    MT_CHECK(s.memoryBudgetKept);
    s = Parse(bad, 2);
    MT_CHECK(!s.memoryBudgetKept);
    // The menu's order: auto 512 1024 2048 4096 8192, round again.
    MT_EQ(DisplaySettings_StepMemoryBudget(0, 1), 512);
    MT_EQ(DisplaySettings_StepMemoryBudget(8192, 1), 0);
    MT_EQ(DisplaySettings_StepMemoryBudget(0, -1), 8192);
    MT_EQ(DisplaySettings_StepMemoryBudget(3000, 1), 4096);
    MT_EQ(DisplaySettings_StepMemoryBudget(3000, -1), 2048);
    // Automatic: an eighth of the PC's memory, 256 MB to 1 GB.
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(0, 8192, 3400), 1024);
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(0, 4096, 3400), 512);
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(0, 1024, 3400), 256);
    // Chosen: held to half the PC's memory and to the free address space.
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(2048, 16384, 3400), 2048);
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(8192, 32768, 3400), 2632); // the 32-bit address space
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(8192, 6144, 0), 3072);     // half of 6 GB
    MT_EQ(DisplaySettings_EffectiveMemoryBudget(512, 16384, 500), 128);    // never below 128
    MT_EQ(DisplaySettings_TerrainCacheChunks(128), 100);
    MT_EQ(DisplaySettings_TerrainCacheChunks(1024), 512);
    MT_EQ(DisplaySettings_TerrainCacheChunks(8192), 1024);
}

MT_TEST(menu_memory_budget)
{
    struct PcMenuState menu;
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];
    struct DisplaySettings s;

    DisplaySettings_Defaults(&s);
    PcMenu_Open(&menu, &s, true, false, false, true, true, true);
    menu.memoryEffectiveMB = 1024;
    menu.memoryInUseMB = 210;
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_MEMORY_BUDGET].label, "Memory budget") == 0);
    MT_CHECK(strcmp(rows[PC_MENU_MEMORY_BUDGET].value, "Auto: 1 GB, 210 MB used") == 0);
    menu.cursor = PC_MENU_ANTIALIASING;
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.cursor, PC_MENU_MEMORY_BUDGET);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_LEFT, 0), PC_MENU_CHANGED_MEMORY_BUDGET);
    MT_EQ(menu.settings.memoryBudget, 8192);
    MT_CHECK(menu.settings.memoryBudgetKept);
    menu.memoryEffectiveMB = 2632;
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[PC_MENU_MEMORY_BUDGET].value, "8 GB (2.5 GB usable), 210 MB used") == 0);
    MT_EQ(menu.settings.quality, QUALITY_HIGH);
}

MT_TEST(frame_limiter_beat)
{
    struct FrameLimiter limiter;

    FrameLimiter_Reset(&limiter);
    MT_EQ(FrameLimiter_Next(&limiter, 1000, 100), 0);    // the first frame: at once
    MT_EQ(FrameLimiter_Next(&limiter, 1050, 100), 1100); // early: wait for the beat
    MT_EQ(FrameLimiter_Next(&limiter, 1190, 100), 1200);
    MT_EQ(FrameLimiter_Next(&limiter, 1310, 100), 0);    // a little late: at once, on the beat
    MT_EQ(FrameLimiter_Next(&limiter, 1350, 100), 1400); // the beat held (1300 + 100)
    MT_EQ(FrameLimiter_Next(&limiter, 2000, 100), 0);    // late by more than a period:
    MT_EQ(FrameLimiter_Next(&limiter, 2010, 100), 2100); // a new beat from then
    MT_EQ(FrameLimiter_Next(&limiter, 2020, 0), 0);      // no limit
    MT_EQ(FrameLimiter_Next(&limiter, 2030, 100), 0);    // a limit again: a new beat
    MT_EQ(FrameLimiter_Next(&limiter, 2040, 100), 2130);
}

MT_TEST(benchmark_statistics)
{
    double times[100];
    struct BenchmarkResult result;

    for (int i = 0; i < 100; i++)
        times[i] = i == 7 ? 50.0 : 10.0 + (i % 3);
    Benchmark_Summarize(times, 100, 1.2, &result);
    MT_EQ(result.frames, 100);
    MT_CHECK(result.averageFps > 83.3 && result.averageFps < 83.4);
    MT_CHECK(result.medianMs == 11.0);
    MT_CHECK(result.worstMs == 50.0);
    MT_CHECK(result.p99Ms == 50.0);
    MT_CHECK(result.lowFps == 20.0);
}

MT_TEST(benchmark_timeline)
{
    struct BenchmarkResult results[DISPLAY_RESOLUTION_COUNT];
    unsigned int t = 5000;
    unsigned int phase = BENCHMARK_WARMUP_MS + 2000;
    unsigned int field;
    unsigned int now;

    Benchmark_Start(t, 2);
    MT_CHECK(Benchmark_Active());
    MT_EQ(Benchmark_RenderHeight(t + 1000), 0);
    MT_EQ(Benchmark_FilterKeys(0, t + 8050), 0x0008);  // Start past the intro
    MT_EQ(Benchmark_FilterKeys(0, t + 12050), 0x0001); // A (title screen)
    MT_EQ(Benchmark_FilterKeys(0, t + 13000), 0);
    MT_EQ(Benchmark_FilterKeys(0, t + 15100), 0x0001); // A again (Continue)
    // 2D frames while booting count for nothing.
    for (now = t; now < t + 20000; now += 20)
        Benchmark_Frame(now, 10.0, false);
    MT_EQ(Benchmark_RenderHeight(t + 20000), 0);
    // The field: no more presses; measuring starts after the settle time.
    field = t + 20000;
    Benchmark_Frame(field, 20.0, true);
    MT_EQ(Benchmark_FilterKeys(0, t + 21050), 0);
    MT_EQ(Benchmark_RenderHeight(field + 1000), 0);
    MT_EQ(Benchmark_RenderHeight(field + BENCHMARK_SETTLE_MS), 720);
    MT_EQ(Benchmark_RenderHeight(field + BENCHMARK_SETTLE_MS + phase), 1080);
    MT_EQ(Benchmark_RenderHeight(field + BENCHMARK_SETTLE_MS + 3 * phase + 10), 2160);
    for (now = field + 20; now < field + BENCHMARK_SETTLE_MS + 4 * phase; now += 20)
        Benchmark_Frame(now, 20.0, true);
    MT_CHECK(!Benchmark_Finished(field + BENCHMARK_SETTLE_MS + 4 * phase - 1));
    MT_CHECK(Benchmark_Finished(field + BENCHMARK_SETTLE_MS + 4 * phase));
    MT_EQ(Benchmark_Results(results, DISPLAY_RESOLUTION_COUNT), 4);
    MT_EQ(results[0].height, 720);
    MT_EQ(results[3].width, 3840);
    MT_EQ(results[1].frames, 100); // 2 s at 50 a second, the warm-up left out
    MT_EQ(results[1].frames3d, 100);
    MT_CHECK(results[2].averageFps > 49.0 && results[2].averageFps < 51.0);
}

MT_TEST(benchmark_without_a_field)
{
    struct BenchmarkResult results[DISPLAY_RESOLUTION_COUNT];
    unsigned int t = 1000;
    unsigned int phase = BENCHMARK_WARMUP_MS + 1000;
    unsigned int now;

    Benchmark_Start(t, 1);
    for (now = t; now < t + BENCHMARK_BOOT_MAX_MS + 4 * phase + 40; now += 40)
        Benchmark_Frame(now, 40.0, false);
    MT_EQ(Benchmark_FilterKeys(0, t + BENCHMARK_BOOT_MAX_MS + 100), 0); // the boot gave up
    MT_CHECK(Benchmark_Finished(t + BENCHMARK_BOOT_MAX_MS + 4 * phase + 40));
    MT_EQ(Benchmark_Results(results, DISPLAY_RESOLUTION_COUNT), 4);
    MT_EQ(results[0].frames3d, 0);
    MT_CHECK(results[0].frames > 0);
}

// ---- The Controls page ---------------------------------------------------------------------------

#include <SDL_keycode.h>
#include <strings.h>

static const char *KeyName(int code)
{
    switch (code)
    {
    case SDLK_z: return "Z";
    case SDLK_x: return "X";
    case SDLK_q: return "Q";
    case SDLK_w: return "W";
    case SDLK_RETURN: return "Return";
    case SDLK_F10: return "F10";
    case SDLK_ESCAPE: return "Escape";
    case SDLK_l: return "L";
    default: return "Key";
    }
}

static int KeyCode(const char *name)
{
    (void)name;
    return 0;
}

static const struct KeybindNames sTestNames = {KeyName, KeyCode};

static struct PcMenuState ControlsMenu(void)
{
    struct PcMenuState menu;
    struct DisplaySettings s;
    struct Keybinds kb;

    DisplaySettings_Defaults(&s);
    Keybinds_Defaults(&kb);
    PcMenu_Open(&menu, &s, true, false, false, true, false, false);
    PcMenu_SetKeybinds(&menu, &kb, &sTestNames);
    menu.cursor = PC_MENU_CONTROLS;
    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    return menu;
}

MT_TEST(controls_page_lists_every_action_with_its_key)
{
    struct PcMenuState menu = ControlsMenu();
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];

    MT_EQ(menu.page, PC_MENU_PAGE_CONTROLS);
    MT_CHECK(strcmp(PcMenu_Title(&menu), "Controls") == 0);
    MT_CHECK(strcmp(PcMenu_BackLabel(&menu), "Back") == 0);
    MT_EQ(PcMenu_Rows(&menu, rows), KEYBIND_COUNT + 2);
    MT_CHECK(strcmp(rows[KEYBIND_A].label, "A button") == 0);
    MT_CHECK(strcmp(rows[KEYBIND_A].value, "Z") == 0);   // the current key is shown
    MT_CHECK(strcmp(rows[KEYBIND_START].value, "ENTER") == 0);
    MT_CHECK(strcmp(rows[KEYBIND_COUNT].label, "Restore defaults") == 0);
    MT_CHECK(strcmp(rows[KEYBIND_COUNT + 1].label, "Back") == 0);
    MT_CHECK(rows[0].selected && !rows[0].adjustable);
}

MT_TEST(controls_page_rebinds_a_key_and_reports_the_change)
{
    struct PcMenuState menu = ControlsMenu();
    struct PcMenuRow rows[PC_MENU_ITEM_COUNT];

    MT_CHECK(!PcMenu_IsCapturing(&menu));
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0), 0);      // A button: wait for a key
    MT_CHECK(PcMenu_IsCapturing(&menu));
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[0].value, "Press a key...") == 0);
    MT_CHECK(PcMenu_Message(&menu) != NULL && strstr(PcMenu_Message(&menu), "A button") != NULL);
    MT_EQ(PcMenu_CaptureKey(&menu, SDLK_q), PC_MENU_CHANGED_KEYS);
    MT_CHECK(!PcMenu_IsCapturing(&menu));
    MT_EQ(menu.keybinds.key[KEYBIND_A], SDLK_q);
    MT_CHECK(menu.keybinds.custom[KEYBIND_A]);
    MT_CHECK(strstr(PcMenu_Message(&menu), "A button is now Q") != NULL);
    PcMenu_Rows(&menu, rows);
    MT_CHECK(strcmp(rows[0].value, "Q") == 0);
}

MT_TEST(controls_page_warns_of_a_conflict_and_swaps_only_when_accepted)
{
    struct PcMenuState menu = ControlsMenu();

    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);                   // B button
    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    MT_EQ(PcMenu_CaptureKey(&menu, SDLK_z), 0);                   // Z is the A button's
    MT_CHECK(menu.conflicting && !PcMenu_IsCapturing(&menu));
    MT_CHECK(strstr(PcMenu_Message(&menu), "already the key for A button") != NULL);
    MT_EQ(menu.keybinds.key[KEYBIND_B], SDLK_x);                  // nothing changed yet
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_BACK, 0), 0);          // cancel
    MT_CHECK(!menu.conflicting);
    MT_EQ(menu.keybinds.key[KEYBIND_B], SDLK_x);
    MT_EQ(menu.keybinds.key[KEYBIND_A], SDLK_z);
    MT_EQ(menu.page, PC_MENU_PAGE_CONTROLS);                      // cancelling stays on the page
    // Again, and accept the swap.
    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    PcMenu_CaptureKey(&menu, SDLK_z);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0), PC_MENU_CHANGED_KEYS);
    MT_EQ(menu.keybinds.key[KEYBIND_B], SDLK_z);
    MT_EQ(menu.keybinds.key[KEYBIND_A], SDLK_x);                  // no action is left without a key
    MT_CHECK(strstr(PcMenu_Message(&menu), "traded keys") != NULL);
}

MT_TEST(controls_page_refuses_the_keys_that_reach_the_menu)
{
    struct PcMenuState menu = ControlsMenu();

    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    MT_EQ(PcMenu_CaptureKey(&menu, SDLK_F10), 0);
    MT_CHECK(PcMenu_IsCapturing(&menu));                          // still waiting for a usable key
    MT_CHECK(strstr(PcMenu_Message(&menu), "can't be a game button") != NULL);
    MT_EQ(PcMenu_CaptureKey(&menu, SDLK_ESCAPE), 0);
    MT_EQ(menu.keybinds.key[KEYBIND_A], SDLK_z);
    // Esc cancels the capture without leaving the page, a second Esc leaves the page.
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_BACK, 0), 0);
    MT_CHECK(!PcMenu_IsCapturing(&menu) && menu.page == PC_MENU_PAGE_CONTROLS);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_BACK, 0), 0);
    MT_EQ(menu.page, PC_MENU_PAGE_MAIN);
    MT_CHECK(menu.open);
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_BACK, 0), PC_MENU_CLOSED); // and the menu closes as before
}

MT_TEST(controls_page_restores_the_defaults_and_goes_back)
{
    struct PcMenuState menu = ControlsMenu();

    PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
    PcMenu_CaptureKey(&menu, SDLK_q);
    menu.controlsCursor = KEYBIND_COUNT;                          // Restore defaults
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0), PC_MENU_CHANGED_KEYS);
    for (int i = 0; i < KEYBIND_COUNT; i++)
        MT_CHECK(menu.keybinds.key[i] == Keybinds_DefaultKey(i) && !menu.keybinds.custom[i]);
    menu.controlsCursor = KEYBIND_COUNT + 1;                      // Back
    MT_EQ(PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0), 0);
    MT_EQ(menu.page, PC_MENU_PAGE_MAIN);
    MT_EQ(menu.cursor, PC_MENU_CONTROLS);
}

MT_TEST(the_menu_itself_does_not_follow_the_bindings)
{
    // Whatever the player binds, the menu's own inputs are fixed: the model has no key codes at all in
    // its navigation (PcMenuInput), and the cursor wraps over every row including Restore and Back.
    struct PcMenuState menu = ControlsMenu();

    for (int i = 0; i < KEYBIND_COUNT; i++)
    {
        PcMenu_Input(&menu, PC_MENU_INPUT_ACCEPT, 0);
        PcMenu_CaptureKey(&menu, SDLK_q + i + 100); // an unrelated run of keys
        PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    }
    MT_EQ(menu.controlsCursor, KEYBIND_COUNT);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    PcMenu_Input(&menu, PC_MENU_INPUT_DOWN, 0);
    MT_EQ(menu.controlsCursor, 0);                                // wrapped
    MT_CHECK(menu.open);
}

int main(void)
{
    MT_RUN(defaults_are_1080p_windowed_high);
    MT_RUN(file_round_trip);
    MT_RUN(nothing_written_until_chosen);
    MT_RUN(a_preset_decides_its_values);
    MT_RUN(hand_edited_values_are_custom);
    MT_RUN(bad_values_are_ignored);
    MT_RUN(legacy_files);
    MT_RUN(window_fits_the_display);
    MT_RUN(render_size_keeps_the_window_shape);
    MT_RUN(pixel_art_in_whole_steps);
    MT_RUN(menu_changes_and_confirmation);
    MT_RUN(menu_without_camera_rows);
    MT_RUN(menu_world_details);
    MT_RUN(menu_from_a_resolution_not_listed);
    MT_RUN(frame_rate_limit_setting);
    MT_RUN(render_scale_setting);
    MT_RUN(menu_frame_rate_and_render_scale);
    MT_RUN(memory_budget_setting);
    MT_RUN(menu_memory_budget);
    MT_RUN(frame_limiter_beat);
    MT_RUN(benchmark_statistics);
    MT_RUN(benchmark_timeline);
    MT_RUN(benchmark_without_a_field);
    MT_RUN(controls_page_lists_every_action_with_its_key);
    MT_RUN(controls_page_rebinds_a_key_and_reports_the_change);
    MT_RUN(controls_page_warns_of_a_conflict_and_swaps_only_when_accepted);
    MT_RUN(controls_page_refuses_the_keys_that_reach_the_menu);
    MT_RUN(controls_page_restores_the_defaults_and_goes_back);
    MT_RUN(the_menu_itself_does_not_follow_the_bindings);
    return MT_REPORT();
}
