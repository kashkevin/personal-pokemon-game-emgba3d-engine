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
#ifdef PORTABLE
#include "character_customization.h"
static void CharCustomEditor_Open(void);
#endif
#line 26 // gba-line

// Palette tags outside the object-event (0x11xx) and Pokemon (0x4000-0x7FFF)
// ranges: one for the local trainer, one per remote trainer.
#define LOCAL_PAL_TAG        0x3C43
#define REMOTE_PAL_TAG_BASE  0x3C40

#define SKIN_COUNT    6
#define HAIR_COUNT   12
#define CLOTH_COUNT  16

#define HINT_DELAY_FRAMES (60 * 12)
#ifdef PORTABLE
// The notice banner's font has no '>' glyph, so the arrows of "START > PLAYER > STYLE" drew as blanks
// ("START  PLAYER  STYLE", KNOWN_ISSUES M27). PC wording without them; the GBA text is unchanged.
#define APPEARANCE_HINT "Make your own TRAINER look:|START menu: PLAYER, then STYLE"
#else
#define APPEARANCE_HINT "Make your own TRAINER look:|START > PLAYER > STYLE"
#endif
#line 37 // gba-line
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
#ifdef PORTABLE
    // The save keeps the look (character_customization.c): a new game, Continue
    // and a soft reset all start from what the save holds.
    if (!sEditorActive && !CharCustom_ReadSavedLook(sLocal, &sPendingSave))
    {
        memset(sLocal, 0, MP_APPEARANCE_BYTES);
        sPendingSave = FALSE;
    }
#endif
#line 316 // gba-line

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
#ifdef PORTABLE
    if (!sEditorActive)
        CharCustom_WriteSavedLook(sLocal, sPendingSave);
#endif
#line 337 // gba-line
    if (gMain.callback2 != CB2_Overworld || gPaletteFade.active)
        return;
    ApplyAll();
#ifdef PORTABLE
    CharCustom_RefreshLocalAvatar(); // a changed character shows at once
#endif
#line 340 // gba-line

    // A one-time hint for players who have not chosen a look yet.
    if (!sHintShown && host != NULL && !IsCustomized(host) && !sPendingSave && !sEditorActive
     && !ArePlayerFieldControlsLocked())
    {
        if (++sHintTimer >= HINT_DELAY_FRAMES && MultiplayerWorld_ShowLocalNotice(APPEARANCE_HINT))
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
#ifdef PORTABLE
    CharCustomEditor_Open(); // the editor with the character selection (below)
    return;
#endif
#line 468 // gba-line
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
#include "constants/trainers.h"

// The character selection (claude/character-customization), when this build
// has it: its editor (Task_CharacterEditor) puts a CHARACTER row first and its
// roster's characters have their own field frames, palettes and pictures. Its
// header is found only then; without it everything below is the STYLE editor.
#if defined(PORTABLE) && defined(__has_include)
#if __has_include("character_customization.h")
#include "character_customization.h"
#define NATIVE_UI_CHARACTERS 1
#endif
#endif

// The body's own field frames (with a character chosen, the engine's lookup
// returns the character's for the player's ids)
static const struct ObjectEventGraphicsInfo *NativeUiBodyGraphics(bool32 isMay)
{
#ifdef NATIVE_UI_CHARACTERS
    return GetVanillaObjectEventGraphicsInfo(isMay ? OBJ_EVENT_GFX_MAY_NORMAL : OBJ_EVENT_GFX_BRENDAN_NORMAL);
#else
    return GetObjectEventGraphicsInfo(isMay ? OBJ_EVENT_GFX_MAY_NORMAL : OBJ_EVENT_GFX_BRENDAN_NORMAL);
#endif
}

// The native PC UI (src/native_ui/screens/ui_customize.c) draws the STYLE
// editor in place of its window and follows this state; the input, the live
// field preview and saving stay here.
bool32 MultiplayerAppearance_NativeUiState(struct NativeAppearance *out)
{
    const u8 *host;
    bool32 isMay;

    memset(out, 0, sizeof(*out));
    if (!sEditorActive || sEditorWindowId == WINDOW_NONE)
        return FALSE;
    isMay = gSaveBlock2Ptr->playerGender != MALE;
#ifdef NATIVE_UI_CHARACTERS
    {
        struct CharCustomEditorState cc;

        CharCustom_GetEditorState(&cc);
        if (!cc.active)
            return FALSE;
        out->characters = TRUE;
        out->character = cc.character;
        out->characterCount = cc.count;
        out->coloursEnabled = cc.coloursEnabled;
        out->cursor = cc.cursor == CC_EDITOR_ROW_CHARACTER ? NATIVE_LOOK_CHARACTER : cc.cursor - 1;
        if (!CharCustom_ReadSavedLook(out->start, NULL) && ValidLook(sLocal))
            memcpy(out->start, sLocal, MP_APPEARANCE_BYTES);
        out->startCharacter = CharCustom_FromLook(out->start);
    }
#else
    if (!FuncIsActiveTask(Task_StyleEditor))
        return FALSE;
    if (ValidLook(sLocal))
        memcpy(out->start, sLocal, MP_APPEARANCE_BYTES); // what OpenEditor started from
    out->cursor = sEditorCursor;
    out->coloursEnabled = TRUE;
#endif
    memcpy(out->look, sEditor, MP_APPEARANCE_BYTES);
    out->isMay = isMay;
    out->pendingSave = sPendingSave;
    out->connected = Multiplayer_IsConnected();
    host = out->connected ? MultiplayerWorld_GetSelfAppearance() : NULL;
    out->hostHasLook = host != NULL && ValidLook(sLocal) && memcmp(host, sLocal, MP_APPEARANCE_BYTES) == 0;
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

#ifdef ENABLE_NATIVE_UI
bool32 MultiplayerAppearance_NativeUiCharacter(u32 character, struct NativeCharacter *out)
{
    memset(out, 0, sizeof(*out));
#ifdef NATIVE_UI_CHARACTERS
    {
        const struct CharCustomCharacter *c = character != CC_CHARACTER_NONE ? CharCustom_Get(character) : NULL;

        if (c == NULL)
            return FALSE;
        out->name = c->name;
        out->variant = c->variant;
        out->gender = c->gender;
        out->kind = c->kind == CC_KIND_PRESET ? 0 : c->kind == CC_KIND_KIT1 ? 1 : 2;
        out->frontPic = c->frontPic;
        return TRUE;
    }
#else
    (void)character;
    return FALSE;
#endif
}

// A roster card clicked (screen 9): the editor's own selection, as LEFT and
// RIGHT on its CHARACTER row would (CharCustom_EditorSelect), with their sound
bool32 MultiplayerAppearance_NativeUiSelectCharacter(u32 character)
{
#ifdef NATIVE_UI_CHARACTERS
    if (CharCustom_EditorSelect(character))
    {
        PlaySE(SE_SELECT);
        return TRUE;
    }
#else
    (void)character;
#endif
    return FALSE;
}

// The body's walking frames of a direction's cycle (step 0-3); running adds 9;
// right is left mirrored
static const u8 sNativeUiWalkFrames[4][4] = {{0, 3, 0, 4}, {2, 7, 2, 8}, {1, 5, 1, 6}, {2, 7, 2, 8}};

bool32 MultiplayerAppearance_NativeUiFieldFrame(u32 character, bool32 isMay, const u8 *look, u32 dir, u32 gait, u32 step,
                                                struct NativeFieldFrame *out)
{
    const struct ObjectEventGraphicsInfo *info;
    u32 frame;

    memset(out, 0, sizeof(*out));
    dir &= 3;
    step &= 3;
#ifdef NATIVE_UI_CHARACTERS
    if (character != CC_CHARACTER_NONE && CharCustom_IsValid(character))
    {
        // its own frames, picked by its own animation table (the engine's standard one)
        static const u8 sDirs[4] = {0, 2, 1, 3}; // down, left, up, right as SOUTH, WEST, NORTH, EAST
        struct CharCustomFieldSprite fs;
        const union AnimCmd *anim;
        u32 index;

        CharCustom_GetFieldSprite(character, isMay ? FEMALE : MALE, &fs);
        if (fs.images == NULL || fs.anims == NULL)
            return FALSE;
        index = gait == NATIVE_GAIT_RUN ? ANIM_RUN_SOUTH : gait == NATIVE_GAIT_WALK ? ANIM_STD_GO_SOUTH : ANIM_STD_FACE_SOUTH;
        anim = fs.anims[index + sDirs[dir]];
        if (anim == NULL)
            return FALSE;
        if (gait == NATIVE_GAIT_STAND || anim[0].type < 0)
            step = 0;
        else
        {
            u32 i;

            for (i = 0; i < step && anim[i + 1].type >= 0; i++)
                ;
            step = i;
        }
        out->tiles = fs.images[anim[step].frame.imageValue].data;
        out->width = fs.width;
        out->height = fs.height;
        out->flip = anim[step].frame.hFlip;
        if (fs.palette != NULL)
            memcpy(out->palette, fs.palette, sizeof(out->palette));
        else
            MultiplayerAppearance_BuildPalette(isMay ? gObjectEventPal_May : gObjectEventPal_Brendan, isMay, NULL, out->palette);
        return out->tiles != NULL;
    }
#else
    (void)character;
#endif
    info = NativeUiBodyGraphics(isMay);
    if (info == NULL || info->images == NULL)
        return FALSE;
    frame = sNativeUiWalkFrames[dir][gait == NATIVE_GAIT_STAND ? 0 : step] + (gait == NATIVE_GAIT_RUN ? 9 : 0);
    out->tiles = (const u8 *)info->images[0].data + frame * (16 * 32 / 2);
    out->width = 16;
    out->height = 32;
    out->flip = dir == 3;
    MultiplayerAppearance_BuildPalette(isMay ? gObjectEventPal_May : gObjectEventPal_Brendan, isMay,
                                       look != NULL && ValidLook(look) ? look : NULL, out->palette);
    return TRUE;
}

// The player's own frame as the others see it: the saved character, or the
// trainer with the saved look's colours
bool32 MultiplayerAppearance_NativeUiPlayerFrame(u32 dir, u32 gait, u32 step, struct NativeFieldFrame *out)
{
    return MultiplayerAppearance_NativeUiFieldFrame(MultiplayerAppearance_NativeUiLocalCharacter(),
                                                    gSaveBlock2Ptr->playerGender != MALE, ValidLook(sLocal) ? sLocal : NULL,
                                                    dir, gait, step, out);
}

u32 MultiplayerAppearance_NativeUiLocalCharacter(void)
{
#ifdef NATIVE_UI_CHARACTERS
    return CharCustom_LocalCharacter();
#else
    return 0;
#endif
}

u16 MultiplayerAppearance_NativeUiFrontPic(u32 character, bool32 isMay)
{
#ifdef NATIVE_UI_CHARACTERS
    const struct CharCustomCharacter *c = character != CC_CHARACTER_NONE ? CharCustom_Get(character) : NULL;

    if (c != NULL)
        return c->frontPic;
#else
    (void)character;
#endif
    return isMay ? TRAINER_PIC_MAY : TRAINER_PIC_BRENDAN;
}

// The bigger front a character shows whole on the player's own Trainer Card
// (CharCustom_DrawTrainerCardPic draws it there)
bool32 MultiplayerAppearance_NativeUiCardPic(const void **tiles, const u16 **palette, u32 *w, u32 *h)
{
#ifdef NATIVE_UI_CHARACTERS
    const struct CharCustomCharacter *c = CharCustom_Get(CharCustom_LocalCharacter());

    if (c != NULL && c->cardPic != NULL && c->cardPal != NULL && c->cardWidth > 0 && c->cardHeight > 0)
    {
        *tiles = c->cardPic;
        *palette = c->cardPal;
        *w = c->cardWidth;
        *h = c->cardHeight;
        return TRUE;
    }
#endif
    *tiles = NULL;
    *palette = NULL;
    *w = 0;
    *h = 0;
    return FALSE;
}
#endif // ENABLE_NATIVE_UI

#ifdef PORTABLE
// ---------------------------------------------------------------------------
// Native PC build: the style editor with the character selection
// (include/character_customization.h). CHARACTER picks a trainer from the
// approved roster; SKIN, HAIR, OUTFIT and HAT/BAG recolour the default trainer
// (a character keeps its own colours). The field sprite and the 3D view show
// every choice at once; SAVE keeps it in the save and offers it to the host
// like any other look.

static const struct WindowTemplate sCharacterEditorWindow =
{
    // Right of the player: 96 x 144 px. Tiles 0x008-0x0DF, which the closed
    // Start menu's info windows use (the multiplayer menu takes the same block).
    .bg = 0, .tilemapLeft = 17, .tilemapTop = 1, .width = 12, .height = 18,
    .paletteNum = 15, .baseBlock = 0x008,
};

static const u8 sText_Character[] = _("CHARACTER");
static const u8 sText_DefaultTrainer[] = _("DEFAULT");
static const u8 sText_RecolourBrendan[] = _("Recolour: BRENDAN");
static const u8 sText_RecolourMay[] = _("Recolour: MAY");
static const u8 sText_OwnColours[] = _("-");
static const u8 sText_Slash[] = _("/");
static const u8 sText_EditorHelp[] = _("{DPAD_LEFTRIGHT}CHANGE {A_BUTTON}OK");
static const u8 sCharacterEditorGrey[] = {TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY, TEXT_COLOR_WHITE};
static const u8 sCharacterEditorRowOld[] = {
    [CC_EDITOR_ROW_SKIN] = ROW_SKIN, [CC_EDITOR_ROW_HAIR] = ROW_HAIR,
    [CC_EDITOR_ROW_OUTFIT] = ROW_OUTFIT, [CC_EDITOR_ROW_HAT] = ROW_HAT,
};
static const u8 *const sCharacterEditorRowLabels[CC_EDITOR_ROW_COUNT] =
{
    [CC_EDITOR_ROW_CHARACTER] = sText_Character,
    [CC_EDITOR_ROW_SKIN] = COMPOUND_STRING("SKIN"),
    [CC_EDITOR_ROW_HAIR] = COMPOUND_STRING("HAIR"),
    [CC_EDITOR_ROW_OUTFIT] = COMPOUND_STRING("OUTFIT"),
    [CC_EDITOR_ROW_HAT] = COMPOUND_STRING("HAT/BAG"),
    [CC_EDITOR_ROW_SAVE] = COMPOUND_STRING("SAVE"),
    [CC_EDITOR_ROW_CANCEL] = COMPOUND_STRING("CANCEL"),
};

static EWRAM_DATA u8 sEditorColours[4] = {0}; // the default trainer's colours while a character is shown
static EWRAM_DATA u8 sEditorFacing = 0;

const u8 *MultiplayerAppearance_GetLocalLook(void)
{
    static u8 sSaved[MP_APPEARANCE_BYTES];

    if (sEditorActive)
        return sEditor;
    if (CharCustom_ReadSavedLook(sSaved, NULL))
        return sSaved;
    return sLocal;
}

static bool32 CharacterEditor_RowEnabled(u32 row)
{
    return row < CC_EDITOR_ROW_SKIN || row > CC_EDITOR_ROW_HAT || CharCustom_FromLook(sEditor) == CC_CHARACTER_NONE;
}

static void CharacterEditor_Render(void)
{
    u32 character = CharCustom_FromLook(sEditor), row, y;
    const u8 *name, *variant;
    s32 width;

    if (sEditorWindowId == WINDOW_NONE)
        return;
    FillWindowPixelBuffer(sEditorWindowId, PIXEL_FILL(1));
    AddTextPrinterParameterized(sEditorWindowId, FONT_NORMAL, sText_Title, 4, 1, TEXT_SKIP_DRAW, NULL);

    // CHARACTER: label and number, then the name and the game or artist
    if (sEditorCursor == CC_EDITOR_ROW_CHARACTER)
        AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Cursor, 1, 17, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Character, 7, 17, TEXT_SKIP_DRAW, NULL);
    if (character != CC_CHARACTER_NONE)
    {
        ConvertIntToDecimalStringN(gStringVar1, character, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringAppend(gStringVar1, sText_Slash);
        ConvertIntToDecimalStringN(gStringVar2, CharCustom_Count() - 1, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringAppend(gStringVar1, gStringVar2);
        width = GetStringWidth(FONT_SMALL, gStringVar1, 0);
        AddTextPrinterParameterized3(sEditorWindowId, FONT_SMALL, 94 - width, 17, sCharacterEditorGrey, TEXT_SKIP_DRAW, gStringVar1);
        name = CharCustom_GetName(character);
        variant = CharCustom_GetVariant(character);
    }
    else
    {
        name = sText_DefaultTrainer;
        variant = gSaveBlock2Ptr->playerGender == FEMALE ? sText_RecolourMay : sText_RecolourBrendan;
    }
    AddTextPrinterParameterized(sEditorWindowId, FONT_NORMAL, sText_Left, 2, 29, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(sEditorWindowId, FONT_NORMAL, sText_Right, 89, 29, TEXT_SKIP_DRAW, NULL);
    width = GetStringWidth(FONT_NORMAL, name, 0);
    AddTextPrinterParameterized(sEditorWindowId, FONT_NORMAL, name, width < 78 ? 9 + (78 - width) / 2 : 9, 29, TEXT_SKIP_DRAW, NULL);
    // the game or artist, in a narrower font when it would not fit the window
    width = GetStringWidth(FONT_SMALL_NARROW, variant, 0);
    AddTextPrinterParameterized3(sEditorWindowId, width <= 92 ? FONT_SMALL_NARROW : FONT_SMALL_NARROWER, 4, 43,
                                 sCharacterEditorGrey, TEXT_SKIP_DRAW, variant);

    // Colours of the default trainer, then SAVE and CANCEL
    for (row = CC_EDITOR_ROW_SKIN; row < CC_EDITOR_ROW_COUNT; row++)
    {
        y = 57 + (row - CC_EDITOR_ROW_SKIN) * 12;
        if (row == sEditorCursor)
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Cursor, 1, y, TEXT_SKIP_DRAW, NULL);
        if (CharacterEditor_RowEnabled(row))
            AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sCharacterEditorRowLabels[row], 7, y, TEXT_SKIP_DRAW, NULL);
        else
            AddTextPrinterParameterized3(sEditorWindowId, FONT_SMALL, 7, y, sCharacterEditorGrey, TEXT_SKIP_DRAW, sCharacterEditorRowLabels[row]);
        if (row > CC_EDITOR_ROW_HAT)
            continue;
        if (!CharacterEditor_RowEnabled(row))
        {
            AddTextPrinterParameterized3(sEditorWindowId, FONT_SMALL, 68, y, sCharacterEditorGrey, TEXT_SKIP_DRAW, sText_OwnColours);
            continue;
        }
        AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Left, 47, y, TEXT_SKIP_DRAW, NULL);
        AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL_NARROW, RowValueName(sCharacterEditorRowOld[row]), 53, y, TEXT_SKIP_DRAW, NULL);
        AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_Right, 91, y, TEXT_SKIP_DRAW, NULL);
    }
    AddTextPrinterParameterized(sEditorWindowId, FONT_SMALL, sText_EditorHelp, 2, 131, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(sEditorWindowId, COPYWIN_FULL);
}

static void CharacterEditor_SetCharacter(u32 character)
{
    if (CharCustom_FromLook(sEditor) == CC_CHARACTER_NONE)
    {
        sEditorColours[0] = sEditor[1];
        sEditorColours[1] = sEditor[2];
        sEditorColours[2] = sEditor[3];
        sEditorColours[3] = sEditor[5];
    }
    if (character != CC_CHARACTER_NONE)
    {
        CharCustom_SetInLook(sEditor, character);
        return;
    }
    // Back to the default trainer, with the colours chosen before.
    memset(sEditor, 0, MP_APPEARANCE_BYTES);
    sEditor[1] = sEditorColours[0];
    sEditor[2] = sEditorColours[1];
    sEditor[3] = sEditorColours[2];
    sEditor[5] = sEditorColours[3];
    sEditor[0] = (sEditor[1] || sEditor[2] || sEditor[3] || sEditor[5]) ? 1 : 0;
}

static void CharacterEditor_ChangeCharacter(s32 delta)
{
    u32 count = CharCustom_Count(), character = CharCustom_FromLook(sEditor), tries;

    for (tries = 0; tries < count; tries++)
    {
        character = (character + count + delta) % count;
        if (character == CC_CHARACTER_NONE || CharCustom_IsValid(character))
            break;
    }
    CharacterEditor_SetCharacter(character);
}

static void CharacterEditor_MoveCursor(s32 delta)
{
    u32 i;

    for (i = 0; i < CC_EDITOR_ROW_COUNT; i++)
    {
        sEditorCursor = (sEditorCursor + CC_EDITOR_ROW_COUNT + delta) % CC_EDITOR_ROW_COUNT;
        if (CharacterEditor_RowEnabled(sEditorCursor))
            break;
    }
}

static void CharacterEditor_Close(u8 taskId, bool32 save)
{
    if (save)
    {
        memcpy(sLocal, sEditor, MP_APPEARANCE_BYTES);
        sPendingSave = TRUE;
        sRetryTimer = 0;
        sHintShown = TRUE;
        CharCustom_WriteSavedLook(sLocal, TRUE);
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
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
        ObjectEventTurn(&gObjectEvents[gPlayerAvatar.objectEventId], sEditorFacing);
}

static void Task_CharacterEditor(u8 taskId)
{
    if (JOY_NEW(B_BUTTON))
    {
        CharacterEditor_Close(taskId, FALSE);
    }
    else if (JOY_NEW(A_BUTTON))
    {
        if (sEditorCursor == CC_EDITOR_ROW_SAVE)
            CharacterEditor_Close(taskId, TRUE);
        else if (sEditorCursor == CC_EDITOR_ROW_CANCEL)
            CharacterEditor_Close(taskId, FALSE);
        else
        {
            CharacterEditor_MoveCursor(1);
            PlaySE(SE_SELECT);
            CharacterEditor_Render();
        }
    }
    else if (JOY_REPEAT(DPAD_UP | DPAD_DOWN))
    {
        CharacterEditor_MoveCursor(JOY_REPEAT(DPAD_UP) ? -1 : 1);
        PlaySE(SE_SELECT);
        CharacterEditor_Render();
    }
    else if (JOY_REPEAT(DPAD_LEFT | DPAD_RIGHT) && sEditorCursor < CC_EDITOR_ROW_SAVE)
    {
        s32 delta = JOY_REPEAT(DPAD_LEFT) ? -1 : 1;

        if (sEditorCursor == CC_EDITOR_ROW_CHARACTER)
            CharacterEditor_ChangeCharacter(delta);
        else
            ChangeRow(sCharacterEditorRowOld[sEditorCursor], delta);
        PlaySE(SE_SELECT);
        CharacterEditor_Render(); // the field sprite updates on the next tick
    }
}

static void CharCustomEditor_Open(void)
{
    if (sEditorActive || FuncIsActiveTask(Task_CharacterEditor))
        return;
    sEditorWindowId = AddWindow(&sCharacterEditorWindow);
    if (sEditorWindowId == WINDOW_NONE)
        return;
    if (CreateTask(Task_CharacterEditor, 80) == TASK_NONE)
    {
        RemoveWindow(sEditorWindowId);
        sEditorWindowId = WINDOW_NONE;
        return;
    }
    memcpy(sEditor, MultiplayerAppearance_GetLocalLook(), MP_APPEARANCE_BYTES);
    if (!ValidLook(sEditor))
        memset(sEditor, 0, MP_APPEARANCE_BYTES);
    sEditorColours[0] = sEditor[1];
    sEditorColours[1] = sEditor[2];
    sEditorColours[2] = sEditor[3];
    sEditorColours[3] = sEditor[5];
    sEditorCursor = CC_EDITOR_ROW_CHARACTER;
    sEditorActive = TRUE;
    // The player faces the camera while choosing (turned back on closing).
    sEditorFacing = DIR_SOUTH;
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
    {
        sEditorFacing = gObjectEvents[gPlayerAvatar.objectEventId].facingDirection;
        ObjectEventTurn(&gObjectEvents[gPlayerAvatar.objectEventId], DIR_SOUTH);
    }
    FreezeObjectEvents();
    PlayerFreeze();
    StopPlayerAvatar();
    LockPlayerFieldControls();
    DrawStdWindowFrame(sEditorWindowId, FALSE);
    CharacterEditor_Render();
}

// The native PC UI (screen 9): picks a character in the open editor, as LEFT
// and RIGHT on CHARACTER would (the field sprite follows on the next frame).
bool32 CharCustom_EditorSelect(u32 character)
{
    if (!sEditorActive || !FuncIsActiveTask(Task_CharacterEditor)
     || (character != CC_CHARACTER_NONE && !CharCustom_IsValid(character)))
        return FALSE;
    if (character != CharCustom_FromLook(sEditor))
    {
        CharacterEditor_SetCharacter(character);
        if (!CharacterEditor_RowEnabled(sEditorCursor))
            sEditorCursor = CC_EDITOR_ROW_CHARACTER;
        CharacterEditor_Render();
    }
    return TRUE;
}

// The native PC UI (screen 9) reads the editor from here; its input stays the editor's.
void CharCustom_GetEditorState(struct CharCustomEditorState *out)
{
    const u8 *look = sEditorActive ? sEditor : MultiplayerAppearance_GetLocalLook();

    memset(out, 0, sizeof(*out));
    out->active = sEditorActive;
    out->cursor = sEditorCursor;
    out->character = CharCustom_FromLook(look);
    out->count = CharCustom_Count();
    out->colours[0] = look[1];
    out->colours[1] = look[2];
    out->colours[2] = look[3];
    out->colours[3] = look[5];
    out->coloursEnabled = out->character == CC_CHARACTER_NONE;
}
#endif // PORTABLE
