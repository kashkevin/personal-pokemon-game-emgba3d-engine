#include "global.h"
#include "multiplayer_appearance.h"
#include "multiplayer.h"
#include "multiplayer_world.h"
#include "event_object_lock.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "field_weather.h"
#include "graphics.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/event_objects.h"
#include "constants/rgb.h"
#include "constants/songs.h"

// Palette tags outside the object-event (0x11xx) and Pokemon (0x4000-0x7FFF)
// ranges: one for the local trainer, one per remote trainer.
#define LOCAL_PAL_TAG        0x3C43
#define REMOTE_PAL_TAG_BASE  0x3C40

#define SKIN_COUNT    6
#define HAIR_COUNT   12
#define CLOTH_COUNT  16

#define HINT_DELAY_FRAMES (60 * 12)
#define SAVE_RETRY_FRAMES 300

enum { ROW_SKIN, ROW_HAIR, ROW_OUTFIT, ROW_HAT, ROW_SAVE, ROW_CANCEL, ROW_COUNT };

// Colour slots of each base palette (graphics/object_events/palettes/*.pal).
struct BaseSlots
{
    u8 skin[4];   // light, mid, shadow, outline
    u8 hairMain;
    u8 hairShade[2];
    u8 outfit[2]; // light, dark (shirt details and shoes)
    u8 hat[2];    // light, dark (bandana and bag)
};

static const struct BaseSlots sBrendanSlots = { {1, 2, 3, 4}, 9, {5, 6}, {12, 13}, {10, 11} };
static const struct BaseSlots sMaySlots     = { {1, 2, 3, 4}, 7, {8, 8}, {12, 13}, {10, 11} };

static const u16 sSkinColors[SKIN_COUNT][4] =
{
    {0},
    {RGB(31, 27, 24), RGB(31, 24, 20), RGB(27, 19, 15), RGB(16, 9, 8)},
    {RGB(30, 24, 18), RGB(28, 20, 14), RGB(23, 15, 10), RGB(14, 8, 6)},
    {RGB(27, 20, 14), RGB(24, 16, 10), RGB(19, 12, 7), RGB(11, 6, 4)},
    {RGB(22, 15, 10), RGB(19, 12, 7), RGB(14, 8, 5), RGB(8, 4, 3)},
    {RGB(16, 10, 7), RGB(13, 8, 5), RGB(9, 5, 3), RGB(5, 2, 2)},
};

static const u16 sHairColors[HAIR_COUNT][2] =
{
    {0},
    {RGB(7, 7, 9), RGB(3, 3, 5)},       // black
    {RGB(12, 8, 5), RGB(7, 4, 3)},      // dark brown
    {RGB(18, 12, 7), RGB(11, 7, 4)},    // brown
    {RGB(22, 10, 6), RGB(14, 5, 3)},    // auburn
    {RGB(27, 8, 6), RGB(17, 4, 3)},     // red
    {RGB(30, 18, 6), RGB(21, 11, 3)},   // orange
    {RGB(30, 27, 14), RGB(23, 19, 8)},  // blonde
    {RGB(29, 30, 31), RGB(20, 21, 24)}, // platinum
    {RGB(19, 19, 21), RGB(11, 11, 13)}, // gray
    {RGB(8, 14, 28), RGB(4, 7, 18)},    // blue
    {RGB(8, 22, 10), RGB(4, 13, 6)},    // green
};

static const u16 sClothColors[CLOTH_COUNT][2] =
{
    {0},
    {RGB(31, 18, 6), RGB(24, 11, 3)},   // orange
    {RGB(31, 28, 8), RGB(25, 21, 3)},   // yellow
    {RGB(18, 29, 8), RGB(11, 22, 4)},   // lime
    {RGB(8, 24, 10), RGB(4, 17, 6)},    // green
    {RGB(6, 24, 24), RGB(3, 16, 17)},   // teal
    {RGB(12, 22, 31), RGB(7, 15, 26)},  // sky
    {RGB(8, 12, 30), RGB(4, 7, 22)},    // blue
    {RGB(6, 8, 20), RGB(3, 4, 13)},     // navy
    {RGB(19, 9, 27), RGB(13, 5, 20)},   // purple
    {RGB(31, 15, 24), RGB(25, 9, 18)},  // pink
    {RGB(31, 31, 31), RGB(23, 24, 27)}, // white
    {RGB(18, 18, 20), RGB(12, 12, 14)}, // gray
    {RGB(7, 7, 8), RGB(3, 3, 4)},       // black
    {RGB(19, 12, 7), RGB(12, 7, 4)},    // brown
    {RGB(20, 5, 8), RGB(13, 3, 5)},     // maroon
};

static const u8 *const sSkinNames[SKIN_COUNT] =
{
    COMPOUND_STRING("DEFAULT"), COMPOUND_STRING("FAIR"), COMPOUND_STRING("LIGHT"),
    COMPOUND_STRING("TAN"), COMPOUND_STRING("BROWN"), COMPOUND_STRING("DEEP"),
};

static const u8 *const sHairNames[HAIR_COUNT] =
{
    COMPOUND_STRING("DEFAULT"), COMPOUND_STRING("BLACK"), COMPOUND_STRING("DK BROWN"),
    COMPOUND_STRING("BROWN"), COMPOUND_STRING("AUBURN"), COMPOUND_STRING("RED"),
    COMPOUND_STRING("ORANGE"), COMPOUND_STRING("BLONDE"), COMPOUND_STRING("PLATINUM"),
    COMPOUND_STRING("GRAY"), COMPOUND_STRING("BLUE"), COMPOUND_STRING("GREEN"),
};

static const u8 *const sClothNames[CLOTH_COUNT] =
{
    COMPOUND_STRING("DEFAULT"), COMPOUND_STRING("ORANGE"), COMPOUND_STRING("YELLOW"),
    COMPOUND_STRING("LIME"), COMPOUND_STRING("GREEN"), COMPOUND_STRING("TEAL"),
    COMPOUND_STRING("SKY"), COMPOUND_STRING("BLUE"), COMPOUND_STRING("NAVY"),
    COMPOUND_STRING("PURPLE"), COMPOUND_STRING("PINK"), COMPOUND_STRING("WHITE"),
    COMPOUND_STRING("GRAY"), COMPOUND_STRING("BLACK"), COMPOUND_STRING("BROWN"),
    COMPOUND_STRING("MAROON"),
};

static const u8 sText_Title[] = _("TRAINER STYLE");
static const u8 *const sRowLabels[ROW_COUNT] =
{
    COMPOUND_STRING("SKIN"), COMPOUND_STRING("HAIR"), COMPOUND_STRING("OUTFIT"),
    COMPOUND_STRING("HAT/BAG"), COMPOUND_STRING("SAVE"), COMPOUND_STRING("CANCEL"),
};
static const u8 sText_Cursor[] = _(">");
static const u8 sText_Left[] = _("<");
static const u8 sText_Right[] = _(">");
static const u8 sText_Help[] = _("{DPAD_LEFTRIGHT}CHANGE {A_BUTTON}OK {B_BUTTON}QUIT");

// Start menu area: free while the editor replaces the closed Start menu.
static const struct WindowTemplate sEditorWindow =
{
    .bg = 0, .tilemapLeft = 15, .tilemapTop = 1, .width = 14, .height = 14,
    .paletteNum = 15, .baseBlock = 0x139, // 196 tiles: ends below the frame tiles at 0x200
};

static EWRAM_DATA u8 sLocal[MP_APPEARANCE_BYTES] = {0};     // the look this player uses
static EWRAM_DATA u8 sEditor[MP_APPEARANCE_BYTES] = {0};    // live preview while editing
static EWRAM_DATA bool8 sPendingSave = FALSE;               // saved here, host not yet confirmed
static EWRAM_DATA bool8 sEditorActive = FALSE;
static EWRAM_DATA bool8 sHintShown = FALSE;
static EWRAM_DATA u16 sRetryTimer = 0;
static EWRAM_DATA u16 sHintTimer = 0;
static EWRAM_DATA u8 sEditorWindowId = 0;
static EWRAM_DATA u8 sEditorCursor = 0;

void MultiplayerAppearance_Init(void)
{
    memset(sLocal, 0, sizeof(sLocal));
    memset(sEditor, 0, sizeof(sEditor));
    sPendingSave = FALSE;
    sEditorActive = FALSE;
    sHintShown = FALSE;
    sRetryTimer = 0;
    sHintTimer = 0;
    sEditorWindowId = WINDOW_NONE;
    sEditorCursor = 0;
}

static bool32 ValidLook(const u8 *look)
{
    return look != NULL && look[0] <= 1 && look[1] < SKIN_COUNT && look[2] < HAIR_COUNT
        && look[3] < CLOTH_COUNT && look[4] < CLOTH_COUNT && look[5] < CLOTH_COUNT
        && look[6] < CLOTH_COUNT && look[7] == 0;
}

static bool32 IsCustomized(const u8 *look)
{
    return ValidLook(look) && look[0] == 1;
}

void MultiplayerAppearance_BuildPalette(const u16 *base, bool32 isMay, const u8 *look, u16 *out)
{
    const struct BaseSlots *slots = isMay ? &sMaySlots : &sBrendanSlots;
    u32 i;

    for (i = 0; i < 16; i++)
        out[i] = base[i];
    if (!IsCustomized(look))
        return;
    if (look[1] != 0)
    {
        for (i = 0; i < 4; i++)
            out[slots->skin[i]] = sSkinColors[look[1]][i];
    }
    if (look[2] != 0)
    {
        out[slots->hairMain] = sHairColors[look[2]][0];
        out[slots->hairShade[0]] = sHairColors[look[2]][1];
        out[slots->hairShade[1]] = sHairColors[look[2]][1];
    }
    if (look[3] != 0)
    {
        out[slots->outfit[0]] = sClothColors[look[3]][0];
        out[slots->outfit[1]] = sClothColors[look[3]][1];
    }
    if (look[5] != 0)
    {
        out[slots->hat[0]] = sClothColors[look[5]][0];
        out[slots->hat[1]] = sClothColors[look[5]][1];
    }
}

const u8 *MultiplayerAppearance_GetPublished(void)
{
    return sLocal;
}

bool32 MultiplayerAppearance_IsEditorActive(void)
{
    return sEditorActive;
}

// ---------------------------------------------------------------------------
// Recolouring sprites

static bool32 IsTrainerPaletteTag(u16 tag)
{
    return tag == OBJ_EVENT_PAL_TAG_BRENDAN || tag == OBJ_EVENT_PAL_TAG_MAY;
}

static void LoadIfChanged(u32 slot, const u16 *pal)
{
    if (memcmp(&gPlttBufferUnfaded[OBJ_PLTT_ID(slot)], pal, PLTT_SIZE_4BPP) != 0)
    {
        LoadPalette(pal, OBJ_PLTT_ID(slot), PLTT_SIZE_4BPP);
        UpdateSpritePaletteWithWeather(slot, FALSE);
    }
}

static void UsePaletteSlot(struct Sprite *sprite, u32 slot)
{
    if (sprite->oam.paletteNum != slot)
    {
        u32 old = sprite->oam.paletteNum;
        sprite->oam.paletteNum = slot;
        FieldEffectFreePaletteIfUnused(old);
    }
}

// Give a trainer sprite either its own recoloured palette (customTag) or
// the shared original palette of its graphics.
static void ApplyTrainerPalette(struct ObjectEvent *objectEvent, const u8 *look, u16 customTag)
{
    const struct ObjectEventGraphicsInfo *info;
    struct Sprite *sprite;
    u16 pal[16];
    u32 slot;
    bool32 isMay;

    if (!objectEvent->active || objectEvent->spriteId >= MAX_SPRITES)
        return;
    sprite = &gSprites[objectEvent->spriteId];
    info = GetObjectEventGraphicsInfo(objectEvent->graphicsId);
    if (info == NULL || !sprite->inUse || !IsTrainerPaletteTag(info->paletteTag))
        return; // e.g. the underwater sprite keeps its own palette
    isMay = info->paletteTag == OBJ_EVENT_PAL_TAG_MAY;
    if (!IsCustomized(look))
    {
        u32 custom = IndexOfSpritePaletteTag(customTag);
        if (custom != 0xFF && sprite->oam.paletteNum == custom)
        {
            struct SpritePalette original = {isMay ? gObjectEventPal_May : gObjectEventPal_Brendan, info->paletteTag};
            slot = IndexOfSpritePaletteTag(info->paletteTag);
            if (slot == 0xFF)
            {
                slot = LoadSpritePalette(&original);
                if (slot == 0xFF)
                    return;
                UpdateSpritePaletteWithWeather(slot, FALSE);
            }
            UsePaletteSlot(sprite, slot);
        }
        return;
    }
    MultiplayerAppearance_BuildPalette(isMay ? gObjectEventPal_May : gObjectEventPal_Brendan, isMay, look, pal);
    slot = IndexOfSpritePaletteTag(customTag);
    if (slot == 0xFF)
    {
        slot = AllocSpritePalette(customTag);
        if (slot == 0xFF)
            return; // no free palette: keep the original colours
        LoadPalette(pal, OBJ_PLTT_ID(slot), PLTT_SIZE_4BPP);
        UpdateSpritePaletteWithWeather(slot, FALSE);
    }
    else
    {
        LoadIfChanged(slot, pal);
    }
    UsePaletteSlot(sprite, slot);
}

static void ApplyAll(void)
{
    u32 i;

    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
        ApplyTrainerPalette(&gObjectEvents[gPlayerAvatar.objectEventId], sEditorActive ? sEditor : sLocal, LOCAL_PAL_TAG);
    for (i = 0; i < MULTIPLAYER_MAX_PEERS; i++)
    {
        u32 id = GetObjectEventIdByLocalId(MULTIPLAYER_LOCAL_ID_BASE + i);
        if (id < OBJECT_EVENTS_COUNT && Multiplayer_IsRemoteObject(&gObjectEvents[id]))
            ApplyTrainerPalette(&gObjectEvents[id],
                MultiplayerWorld_GetPeerAppearance(Multiplayer_GetRemotePlayerId(i)), REMOTE_PAL_TAG_BASE + i);
    }
}

void MultiplayerAppearance_Tick(bool32 connected)
{
    const u8 *host = connected ? MultiplayerWorld_GetSelfAppearance() : NULL;

    if (!sEditorActive)
    {
        if (sPendingSave)
        {
            // Keep offering the saved look until the host confirms it (also
            // after a look chosen offline, once the player connects).
            if (host != NULL && memcmp(host, sLocal, MP_APPEARANCE_BYTES) == 0)
            {
                sPendingSave = FALSE;
            }
            else if (host != NULL && (sRetryTimer == 0 || --sRetryTimer == 0))
            {
                sRetryTimer = MultiplayerWorld_PostAppearanceSaved() ? SAVE_RETRY_FRAMES : 1;
            }
        }
        else if (host != NULL && ValidLook(host))
        {
            memcpy(sLocal, host, MP_APPEARANCE_BYTES);
        }
    }
    if (gMain.callback2 != CB2_Overworld || gPaletteFade.active)
        return;
    ApplyAll();

    // A one-time hint for players who have not chosen a look yet.
    if (!sHintShown && host != NULL && !IsCustomized(host) && !sPendingSave && !sEditorActive
     && !ArePlayerFieldControlsLocked())
    {
        if (++sHintTimer >= HINT_DELAY_FRAMES && MultiplayerWorld_ShowLocalNotice("Make your own TRAINER look:|START > PLAYER > STYLE"))
            sHintShown = TRUE;
    }
}

// ---------------------------------------------------------------------------
// Style editor (PLAYER > STYLE): the field sprite previews every change live.

static const u8 *RowValueName(u32 row)
{
    switch (row)
    {
    case ROW_SKIN:   return sSkinNames[sEditor[1]];
    case ROW_HAIR:   return sHairNames[sEditor[2]];
    case ROW_OUTFIT: return sClothNames[sEditor[3]];
    case ROW_HAT:    return sClothNames[sEditor[5]];
    default:         return NULL;
    }
}

static void ChangeRow(u32 row, s32 delta)
{
    u8 *field;
    u32 count;

    switch (row)
    {
    case ROW_SKIN:   field = &sEditor[1]; count = SKIN_COUNT; break;
    case ROW_HAIR:   field = &sEditor[2]; count = HAIR_COUNT; break;
    case ROW_OUTFIT: field = &sEditor[3]; count = CLOTH_COUNT; break;
    case ROW_HAT:    field = &sEditor[5]; count = CLOTH_COUNT; break;
    default:         return;
    }
    *field = (*field + count + delta) % count;
    sEditor[0] = (sEditor[1] || sEditor[2] || sEditor[3] || sEditor[5]) ? 1 : 0;
}

static void RenderEditor(void)
{
    u32 row;

    if (sEditorWindowId == WINDOW_NONE)
        return;
    FillWindowPixelBuffer(sEditorWindowId, PIXEL_FILL(1));
    AddTextPrinterParameterized(sEditorWindowId, FONT_NORMAL, sText_Title, 8, 1, TEXT_SKIP_DRAW, NULL);
    for (row = 0; row < ROW_COUNT; row++)
    {
        u32 y = 17 + row * 13;
        if (row == sEditorCursor)
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Cursor, 2, y, TEXT_SKIP_DRAW, NULL);
        AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sRowLabels[row], 10, y, TEXT_SKIP_DRAW, NULL);
        if (row < ROW_SAVE)
        {
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Left, 50, y, TEXT_SKIP_DRAW, NULL);
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, RowValueName(row), 57, y, TEXT_SKIP_DRAW, NULL);
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Right, 102, y, TEXT_SKIP_DRAW, NULL);
        }
    }
    AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Help, 2, 97, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(sEditorWindowId, COPYWIN_FULL);
}

static void CloseEditor(u8 taskId, bool32 save)
{
    if (save)
    {
        memcpy(sLocal, sEditor, MP_APPEARANCE_BYTES);
        sPendingSave = TRUE;
        sRetryTimer = 0;
        sHintShown = TRUE;
        PlaySE(SE_SAVE);
    }
    else
    {
        PlaySE(SE_SELECT);
    }
    sEditorActive = FALSE;
    if (sEditorWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrameToTransparent(sEditorWindowId, TRUE);
        RemoveWindow(sEditorWindowId);
        sEditorWindowId = WINDOW_NONE;
    }
    DestroyTask(taskId);
    ScriptUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
}

static void Task_StyleEditor(u8 taskId)
{
    if (JOY_NEW(B_BUTTON))
    {
        CloseEditor(taskId, FALSE);
    }
    else if (JOY_NEW(A_BUTTON))
    {
        if (sEditorCursor == ROW_SAVE)
            CloseEditor(taskId, TRUE);
        else if (sEditorCursor == ROW_CANCEL)
            CloseEditor(taskId, FALSE);
        else
        {
            sEditorCursor++;
            PlaySE(SE_SELECT);
            RenderEditor();
        }
    }
    else if (JOY_REPEAT(DPAD_UP | DPAD_DOWN))
    {
        sEditorCursor = (sEditorCursor + (JOY_REPEAT(DPAD_UP) ? ROW_COUNT - 1 : 1)) % ROW_COUNT;
        PlaySE(SE_SELECT);
        RenderEditor();
    }
    else if (JOY_REPEAT(DPAD_LEFT | DPAD_RIGHT) && sEditorCursor < ROW_SAVE)
    {
        ChangeRow(sEditorCursor, JOY_REPEAT(DPAD_LEFT) ? -1 : 1);
        PlaySE(SE_SELECT);
        RenderEditor(); // the field sprite updates on the next tick
    }
}

void MultiplayerAppearance_OpenEditor(void)
{
    if (sEditorActive || FuncIsActiveTask(Task_StyleEditor))
        return;
    sEditorWindowId = AddWindow(&sEditorWindow);
    if (sEditorWindowId == WINDOW_NONE)
        return;
    if (CreateTask(Task_StyleEditor, 80) == TASK_NONE)
    {
        RemoveWindow(sEditorWindowId);
        sEditorWindowId = WINDOW_NONE;
        return;
    }
    if (ValidLook(sLocal))
        memcpy(sEditor, sLocal, MP_APPEARANCE_BYTES);
    else
        memset(sEditor, 0, MP_APPEARANCE_BYTES);
    sEditorCursor = 0;
    sEditorActive = TRUE;
    FreezeObjectEvents();
    PlayerFreeze();
    StopPlayerAvatar();
    LockPlayerFieldControls();
    DrawStdWindowFrame(sEditorWindowId, FALSE);
    RenderEditor();
}

#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI (src/native_ui/screens/ui_customize.c) draws the STYLE
// editor in place of its window and follows this state; the input, the live
// field preview and saving stay here.
bool32 MultiplayerAppearance_NativeUiState(struct NativeAppearance *out)
{
    const struct ObjectEventGraphicsInfo *info;
    const u8 *host;
    bool32 isMay;

    memset(out, 0, sizeof(*out));
    if (!sEditorActive || !FuncIsActiveTask(Task_StyleEditor) || sEditorWindowId == WINDOW_NONE)
        return FALSE;
    isMay = gSaveBlock2Ptr->playerGender != MALE;
    memcpy(out->look, sEditor, MP_APPEARANCE_BYTES);
    if (ValidLook(sLocal))
        memcpy(out->start, sLocal, MP_APPEARANCE_BYTES); // what OpenEditor started from
    out->cursor = sEditorCursor;
    out->isMay = isMay;
    out->pendingSave = sPendingSave;
    out->connected = Multiplayer_IsConnected();
    host = out->connected ? MultiplayerWorld_GetSelfAppearance() : NULL;
    out->hostHasLook = host != NULL && ValidLook(sLocal) && memcmp(host, sLocal, MP_APPEARANCE_BYTES) == 0;
    memcpy(out->basePalette, isMay ? gObjectEventPal_May : gObjectEventPal_Brendan, sizeof(out->basePalette));
    MultiplayerAppearance_BuildPalette(out->basePalette, isMay, sEditor, out->palette);
    info = GetObjectEventGraphicsInfo(isMay ? OBJ_EVENT_GFX_MAY_NORMAL : OBJ_EVENT_GFX_BRENDAN_NORMAL);
    if (info != NULL && info->images != NULL)
        out->frames = info->images[0].data; // walking then running, 16x32 each (relative frames)
    return TRUE;
}

// The editor's value rows: how many choices, their names, the colours of a
// choice (the body's own for DEFAULT) and the palette slots a row recolours.
static u32 NativeUiRowCount(u32 row)
{
    switch (row)
    {
    case ROW_SKIN:   return SKIN_COUNT;
    case ROW_HAIR:   return HAIR_COUNT;
    case ROW_OUTFIT:
    case ROW_HAT:    return CLOTH_COUNT;
    default:         return 0;
    }
}

u32 MultiplayerAppearance_NativeUiOptionCount(u32 row)
{
    return NativeUiRowCount(row);
}

const u8 *MultiplayerAppearance_NativeUiOptionName(u32 row, u32 index)
{
    if (index >= NativeUiRowCount(row))
        return NULL;
    switch (row)
    {
    case ROW_SKIN: return sSkinNames[index];
    case ROW_HAIR: return sHairNames[index];
    default:       return sClothNames[index];
    }
}

u32 MultiplayerAppearance_NativeUiLookByte(u32 row)
{
    switch (row)
    {
    case ROW_SKIN:   return 1;
    case ROW_HAIR:   return 2;
    case ROW_OUTFIT: return 3;
    case ROW_HAT:    return 5;
    default:         return 0;
    }
}

u32 MultiplayerAppearance_NativeUiSlots(u32 row, bool32 isMay, u8 *slots)
{
    const struct BaseSlots *s = isMay ? &sMaySlots : &sBrendanSlots;
    u32 n = 0, i;

    switch (row)
    {
    case ROW_SKIN:
        for (i = 0; i < 4; i++)
            slots[n++] = s->skin[i];
        break;
    case ROW_HAIR:
        slots[n++] = s->hairMain;
        slots[n++] = s->hairShade[0];
        if (s->hairShade[1] != s->hairShade[0])
            slots[n++] = s->hairShade[1];
        break;
    case ROW_OUTFIT:
        slots[n++] = s->outfit[0];
        slots[n++] = s->outfit[1];
        break;
    case ROW_HAT:
        slots[n++] = s->hat[0];
        slots[n++] = s->hat[1];
        break;
    }
    return n;
}

// light to dark; DEFAULT is the body's own colours in those slots
u32 MultiplayerAppearance_NativeUiSwatch(u32 row, u32 index, bool32 isMay, u16 *colors)
{
    const u16 *base = isMay ? gObjectEventPal_May : gObjectEventPal_Brendan;
    u8 slots[4];
    u32 n, i;

    if (index >= NativeUiRowCount(row))
        return 0;
    if (index == 0)
    {
        n = MultiplayerAppearance_NativeUiSlots(row, isMay, slots);
        for (i = 0; i < n; i++)
            colors[i] = base[slots[i]];
        return n;
    }
    switch (row)
    {
    case ROW_SKIN:
        for (i = 0; i < 4; i++)
            colors[i] = sSkinColors[index][i];
        return 4;
    case ROW_HAIR:
        colors[0] = sHairColors[index][0];
        colors[1] = sHairColors[index][1];
        return 2;
    default:
        colors[0] = sClothColors[index][0];
        colors[1] = sClothColors[index][1];
        return 2;
    }
}
#endif // ENABLE_NATIVE_UI
