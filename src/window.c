#include "global.h"
#include "window.h"
#include "malloc.h"
#include "bg.h"
#include "blit.h"
#include "decompress.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"
#endif
#line 7

COMMON_DATA void *gWindowBgTilemapBuffers[NUM_BACKGROUNDS] = {0};
extern u32 gWindowTileAutoAllocEnabled;

EWRAM_DATA struct Window gWindows[WINDOWS_MAX] = {0};

static u32 GetNumActiveWindowsOnBg(u32 bgId);
static u32 GetNumActiveWindowsOnBg8Bit(u32 bgId);

static const struct WindowTemplate sDummyWindowTemplate = DUMMY_WIN_TEMPLATE;

static void DummyWindowBgTilemap(void)
{

}

bool32 InitWindowsChecked(const struct WindowTemplate *templates, s32 staticSize)
{
    bool32 terminated;

    if (staticSize >= 0)
    {
        terminated = templates[(staticSize / sizeof(*templates)) - 1].bg == 0xFF;
    }
    else
    {
        terminated = FALSE;
        for (u32 i = 0; i < WINDOWS_MAX; i++)
        {
            if (templates[i].bg == 0xFF)
            {
                terminated = TRUE;
                break;
            }
            else if (0x04 <= templates[i].bg && templates[i].bg < 0xFF)
            {
                break;
            }
        }
    }

    fatal_assertf(terminated, "%p is missing DUMMY_WIN_TEMPLATE terminator", templates);

    bool32 initialized = InitWindowsUnchecked(templates);
    fatal_assertf(initialized, "Could not initialize windows");
    return TRUE;
}

bool32 InitWindowsUnchecked(const struct WindowTemplate *templates)
{
    int i;
    void *bgTilemapBuffer;
    int j;
    u32 bgLayer;
    u16 attrib;
    u8 *allocatedTilemapBuffer;
    int allocatedBaseBlock;

    for (i = 0; i < NUM_BACKGROUNDS; ++i)
    {
        bgTilemapBuffer = GetBgTilemapBuffer(i);
        if (bgTilemapBuffer != NULL)
            gWindowBgTilemapBuffers[i] = DummyWindowBgTilemap;
        else
            gWindowBgTilemapBuffers[i] = bgTilemapBuffer;
    }

    for (i = 0; i < WINDOWS_MAX; ++i)
    {
        gWindows[i].window = sDummyWindowTemplate;
        gWindows[i].tileData = NULL;
    }
#ifdef ENABLE_NATIVE_UI
    TextPrinter_NativeUiForgetWindow(WINDOWS_MAX); // the windows are gone: their last strings too
#endif
#line 79

    for (i = 0, allocatedBaseBlock = 0, bgLayer = templates[i].bg; bgLayer != 0xFF && i < WINDOWS_MAX; ++i, bgLayer = templates[i].bg)
    {
        if (gWindowTileAutoAllocEnabled == TRUE)
        {
            allocatedBaseBlock = BgTileAllocOpUnchecked(bgLayer, 0, templates[i].width * templates[i].height, 0);
            if (allocatedBaseBlock == -1)
                return FALSE;
        }

        if (gWindowBgTilemapBuffers[bgLayer] == NULL)
        {
            attrib = GetBgAttribute(bgLayer, BG_ATTR_METRIC);

            if (attrib != 0xFFFF)
            {
                allocatedTilemapBuffer = AllocZeroedUnchecked(attrib);

                if (allocatedTilemapBuffer == NULL)
                {
                    FreeAllWindowBuffers();
                    return FALSE;
                }

                for (j = 0; j < attrib; ++j)
                    allocatedTilemapBuffer[j] = 0;

                gWindowBgTilemapBuffers[bgLayer] = allocatedTilemapBuffer;
                SetBgTilemapBuffer(bgLayer, allocatedTilemapBuffer);
            }
        }

        allocatedTilemapBuffer = AllocZeroedUnchecked((u16)(32 * (templates[i].width * templates[i].height)));

        if (allocatedTilemapBuffer == NULL)
        {
            if ((GetNumActiveWindowsOnBg(bgLayer) == 0) && (gWindowBgTilemapBuffers[bgLayer] != DummyWindowBgTilemap))
            {
                Free(gWindowBgTilemapBuffers[bgLayer]);
                gWindowBgTilemapBuffers[bgLayer] = allocatedTilemapBuffer;
            }

            return FALSE;
        }

        gWindows[i].tileData = allocatedTilemapBuffer;
        gWindows[i].window = templates[i];

        if (gWindowTileAutoAllocEnabled == TRUE)
        {
            gWindows[i].window.baseBlock = allocatedBaseBlock;
            BgTileAllocOp(bgLayer, allocatedBaseBlock, templates[i].width * templates[i].height, 1);
        }
    }

    return TRUE;
}

u32 AddWindow(const struct WindowTemplate *template)
{
    u32 win;
    u32 bgLayer;
    int allocatedBaseBlock;
    u16 attrib;
    u8 *allocatedTilemapBuffer;
    int i;

    for (win = 0; win < WINDOWS_MAX; ++win)
    {
        if ((bgLayer = gWindows[win].window.bg) == 0xFF)
            break;
    }

    if (win == WINDOWS_MAX)
        return WINDOW_NONE;

    bgLayer = template->bg;
    allocatedBaseBlock = 0;

    if (gWindowTileAutoAllocEnabled == TRUE)
    {
        allocatedBaseBlock = BgTileAllocOpUnchecked(bgLayer, 0, template->width * template->height, 0);

        if (allocatedBaseBlock == -1)
            return WINDOW_NONE;
    }

    if (gWindowBgTilemapBuffers[bgLayer] == NULL)
    {
        attrib = GetBgAttribute(bgLayer, BG_ATTR_METRIC);

        if (attrib != 0xFFFF)
        {
            allocatedTilemapBuffer = AllocZeroed(attrib);

            if (allocatedTilemapBuffer == NULL)
                return WINDOW_NONE;

            for (i = 0; i < attrib; ++i)
                allocatedTilemapBuffer[i] = 0;

            gWindowBgTilemapBuffers[bgLayer] = allocatedTilemapBuffer;
            SetBgTilemapBuffer(bgLayer, allocatedTilemapBuffer);
        }
    }

    allocatedTilemapBuffer = AllocZeroed((u16)(32 * (template->width * template->height)));

    if (allocatedTilemapBuffer == NULL)
    {
        if ((GetNumActiveWindowsOnBg(bgLayer) == 0) && (gWindowBgTilemapBuffers[bgLayer] != DummyWindowBgTilemap))
        {
            Free(gWindowBgTilemapBuffers[bgLayer]);
            gWindowBgTilemapBuffers[bgLayer] = allocatedTilemapBuffer;
        }
        return WINDOW_NONE;
    }

    gWindows[win].tileData = allocatedTilemapBuffer;
    gWindows[win].window = *template;

    if (gWindowTileAutoAllocEnabled == TRUE)
    {
        gWindows[win].window.baseBlock = allocatedBaseBlock;
        BgTileAllocOp(bgLayer, allocatedBaseBlock, gWindows[win].window.width * gWindows[win].window.height, 1);
    }

    return win;
}

int AddWindowWithoutTileMap(const struct WindowTemplate *template)
{
    int win;
    u32 bgLayer;
    int allocatedBaseBlock;

    for (win = 0; win < WINDOWS_MAX; ++win)
    {
        if (gWindows[win].window.bg == 0xFF)
            break;
    }

    if (win == WINDOWS_MAX)
        return WINDOW_NONE;

    bgLayer = template->bg;
    allocatedBaseBlock = 0;

    if (gWindowTileAutoAllocEnabled == TRUE)
    {
        allocatedBaseBlock = BgTileAllocOp(bgLayer, 0, template->width * template->height, 0);

        if (allocatedBaseBlock == -1)
            return WINDOW_NONE;
    }

    gWindows[win].window = *template;

    if (gWindowTileAutoAllocEnabled == TRUE)
    {
        gWindows[win].window.baseBlock = allocatedBaseBlock;
        BgTileAllocOp(bgLayer, allocatedBaseBlock, gWindows[win].window.width * gWindows[win].window.height, 1);
    }

    return win;
}

void RemoveWindow(u32 windowId)
{
    u32 bgLayer = gWindows[windowId].window.bg;
#ifdef ENABLE_NATIVE_UI
    TextPrinter_NativeUiForgetWindow(windowId);
#endif
#line 249

    if (gWindowTileAutoAllocEnabled == TRUE)
        BgTileAllocOp(bgLayer, gWindows[windowId].window.baseBlock, gWindows[windowId].window.width * gWindows[windowId].window.height, 2);

    gWindows[windowId].window = sDummyWindowTemplate;

    if (GetNumActiveWindowsOnBg(bgLayer) == 0)
    {
        if (gWindowBgTilemapBuffers[bgLayer] != DummyWindowBgTilemap)
        {
            Free(gWindowBgTilemapBuffers[bgLayer]);
            gWindowBgTilemapBuffers[bgLayer] = NULL;
        }
    }

    if (gWindows[windowId].tileData != NULL)
    {
        Free(gWindows[windowId].tileData);
        gWindows[windowId].tileData = NULL;
    }
}

void RemoveAllWindowsOnBg(u32 bgId)
{
    u32 i;

    if (bgId > NUM_BACKGROUNDS)
        return;

    for (i = 0; i < WINDOWS_MAX; i++)
    {
        if (gWindows[i].window.bg == bgId)
            RemoveWindow(i);
    }
}

void FreeAllWindowBuffers(void)
{
    int i;

    for (i = 0; i < NUM_BACKGROUNDS; ++i)
    {
        if (gWindowBgTilemapBuffers[i] != NULL && gWindowBgTilemapBuffers[i] != DummyWindowBgTilemap)
        {
            Free(gWindowBgTilemapBuffers[i]);
            gWindowBgTilemapBuffers[i] = NULL;
        }
    }

    for (i = 0; i < WINDOWS_MAX; ++i)
    {
        if (gWindows[i].tileData != NULL)
        {
            Free(gWindows[i].tileData);
            gWindows[i].tileData = NULL;
        }
    }
}

void CopyWindowToVram(u32 windowId, u32 mode)
{
    struct Window windowLocal = gWindows[windowId];
    u16 windowSize = 32 * (windowLocal.window.width * windowLocal.window.height);

    switch (mode)
    {
    case COPYWIN_MAP:
        CopyBgTilemapBufferToVram(windowLocal.window.bg);
        break;
    case COPYWIN_GFX:
        LoadBgTiles(windowLocal.window.bg, windowLocal.tileData, windowSize, windowLocal.window.baseBlock);
        break;
    case COPYWIN_FULL:
        LoadBgTiles(windowLocal.window.bg, windowLocal.tileData, windowSize, windowLocal.window.baseBlock);
        CopyBgTilemapBufferToVram(windowLocal.window.bg);
        break;
    }
}

void CopyWindowRectToVram(u32 windowId, u32 mode, u32 x, u32 y, u32 w, u32 h)
{
    struct Window windowLocal;
    int rectSize;
    int rectPos;

    if (w != 0 && h != 0)
    {
        windowLocal = gWindows[windowId];

        rectSize = ((h - 1) * windowLocal.window.width);
        rectSize += (windowLocal.window.width - x);
        rectSize -= (windowLocal.window.width - (x + w));
        rectSize *= 32;

        rectPos = (y * windowLocal.window.width) + x;

        switch (mode)
        {
        case COPYWIN_MAP:
            CopyBgTilemapBufferToVram(windowLocal.window.bg);
            break;
        case COPYWIN_GFX:
            LoadBgTiles(windowLocal.window.bg, windowLocal.tileData + (rectPos * 32), rectSize, windowLocal.window.baseBlock + rectPos);
            break;
        case COPYWIN_FULL:
            LoadBgTiles(windowLocal.window.bg, windowLocal.tileData + (rectPos * 32), rectSize, windowLocal.window.baseBlock + rectPos);
            CopyBgTilemapBufferToVram(windowLocal.window.bg);
            break;
        }
    }
}

void PutWindowTilemap(u32 windowId)
{
#ifdef PORTABLE
    // Callers pass WINDOW_NONE for windows they never added (e.g. the summary
    // screen's optional windows). The GBA then reads unrelated memory as a window;
    // natively that memory differs and could draw garbage. Skip it.
    if (windowId >= WINDOWS_MAX)
        return;
#endif
#line 363 // gba-line
    struct Window windowLocal = gWindows[windowId];

    WriteSequenceToBgTilemapBuffer(
        windowLocal.window.bg,
        GetBgAttribute(windowLocal.window.bg, BG_ATTR_BASETILE) + windowLocal.window.baseBlock,
        windowLocal.window.tilemapLeft,
        windowLocal.window.tilemapTop,
        windowLocal.window.width,
        windowLocal.window.height,
        windowLocal.window.paletteNum,
        1);
}

void PutWindowRectTilemapOverridePalette(u32 windowId, u8 x, u8 y, u8 width, u8 height, u8 palette)
{
    struct Window windowLocal = gWindows[windowId];
    u16 currentRow = windowLocal.window.baseBlock + (y * windowLocal.window.width) + x + GetBgAttribute(windowLocal.window.bg, BG_ATTR_BASETILE);
    int i;

    for (i = 0; i < height; ++i)
    {
        WriteSequenceToBgTilemapBuffer(
            windowLocal.window.bg,
            currentRow,
            windowLocal.window.tilemapLeft + x,
            windowLocal.window.tilemapTop + y + i,
            width,
            1,
            palette,
            1);

        currentRow += windowLocal.window.width;
    }
}

// Fills a window with transparent tiles.
void ClearWindowTilemap(u32 windowId)
{
#ifdef PORTABLE
    if (windowId >= WINDOWS_MAX)
        return; // see PutWindowTilemap
#endif
#line 401 // gba-line
    struct Window windowLocal = gWindows[windowId];

    FillBgTilemapBufferRect(
        windowLocal.window.bg,
        0,
        windowLocal.window.tilemapLeft,
        windowLocal.window.tilemapTop,
        windowLocal.window.width,
        windowLocal.window.height,
        windowLocal.window.paletteNum);
}

void PutWindowRectTilemap(u32 windowId, u8 x, u8 y, u8 width, u8 height)
{
    struct Window windowLocal = gWindows[windowId];
    u16 currentRow = windowLocal.window.baseBlock + (y * windowLocal.window.width) + x + GetBgAttribute(windowLocal.window.bg, BG_ATTR_BASETILE);
    int i;

    for (i = 0; i < height; ++i)
    {
        WriteSequenceToBgTilemapBuffer(
            windowLocal.window.bg,
            currentRow,
            windowLocal.window.tilemapLeft + x,
            windowLocal.window.tilemapTop + y + i,
            width,
            1,
            windowLocal.window.paletteNum,
            1);

        currentRow += windowLocal.window.width;
    }
}

void BlitBitmapToWindow(u32 windowId, const u8 *pixels, u16 x, u16 y, u16 width, u16 height)
{
    BlitBitmapRectToWindow(windowId, pixels, 0, 0, width, height, x, y, width, height);
}

void BlitBitmapRectToWindow(u32 windowId, const u8 *pixels, u16 srcX, u16 srcY, u16 srcWidth, int srcHeight, u16 destX, u16 destY, u16 rectWidth, u16 rectHeight)
{
    struct Bitmap sourceRect;
    struct Bitmap destRect;

    sourceRect.pixels = (u8 *)pixels;
    sourceRect.width = srcWidth;
    sourceRect.height = srcHeight;

    destRect.pixels = gWindows[windowId].tileData;
    destRect.width = 8 * gWindows[windowId].window.width;
    destRect.height = 8 * gWindows[windowId].window.height;

    BlitBitmapRect4Bit(&sourceRect, &destRect, srcX, srcY, destX, destY, rectWidth, rectHeight, 0);
}

static void UNUSED BlitBitmapRectToWindowWithColorKey(u32 windowId, const u8 *pixels, u16 srcX, u16 srcY, u16 srcWidth, int srcHeight, u16 destX, u16 destY, u16 rectWidth, u16 rectHeight, u8 colorKey)
{
    struct Bitmap sourceRect;
    struct Bitmap destRect;

    sourceRect.pixels = (u8 *)pixels;
    sourceRect.width = srcWidth;
    sourceRect.height = srcHeight;

    destRect.pixels = gWindows[windowId].tileData;
    destRect.width = 8 * gWindows[windowId].window.width;
    destRect.height = 8 * gWindows[windowId].window.height;

    BlitBitmapRect4Bit(&sourceRect, &destRect, srcX, srcY, destX, destY, rectWidth, rectHeight, colorKey);
}

void FillWindowPixelRect(u32 windowId, u8 fillValue, u16 x, u16 y, u16 width, u16 height)
{
    struct Bitmap pixelRect;

    pixelRect.pixels = gWindows[windowId].tileData;
    pixelRect.width = 8 * gWindows[windowId].window.width;
    pixelRect.height = 8 * gWindows[windowId].window.height;

    FillBitmapRect4Bit(&pixelRect, x, y, width, height, fillValue);
}

void CopyToWindowPixelBuffer(u32 windowId, const void *src, u16 size, u16 tileOffset)
{
    if (size != 0)
        CpuCopy16(src, gWindows[windowId].tileData + (32 * tileOffset), size);
    else
        DecompressDataWithHeaderWram(src, gWindows[windowId].tileData + (32 * tileOffset));
}

// Sets all pixels within the window to the fillValue color.
void FillWindowPixelBuffer(u32 windowId, u8 fillValue)
{
    int fillSize = gWindows[windowId].window.width * gWindows[windowId].window.height;
    CpuFastFill8(fillValue, gWindows[windowId].tileData, 32 * fillSize);
#ifdef ENABLE_NATIVE_UI
    TextPrinter_NativeUiClearTranscript(windowId); // what was printed on it is gone (src/native_ui: records boards)
#endif
#line 496 // gba-line
}

#define MOVE_TILES_DOWN(a)                                                      \
{                                                                               \
    destOffset = i + (a);                                                       \
    srcOffset = i + (((width * (distanceLoop & ~7)) | (distanceLoop & 7)) * 4); \
    if (srcOffset < size)                                                       \
        *(u32 *)(tileData + destOffset) = *(u32 *)(tileData + srcOffset);         \
    else                                                                        \
        *(u32 *)(tileData + destOffset) = fillValue32;                           \
    distanceLoop++;                                                             \
}

#define MOVE_TILES_UP(a)                                                        \
{                                                                               \
    destOffset = i + (a);                                                       \
    srcOffset = i + (((width * (distanceLoop & ~7)) | (distanceLoop & 7)) * 4); \
    if (srcOffset < size)                                                       \
        *(u32 *)(tileData - destOffset) = *(u32 *)(tileData - srcOffset);         \
    else                                                                        \
        *(u32 *)(tileData - destOffset) = fillValue32;                           \
    distanceLoop++;                                                             \
}

void ScrollWindow(u32 windowId, u8 direction, u8 distance, u8 fillValue)
{
    struct WindowTemplate window = gWindows[windowId].window;
    u8 *tileData = gWindows[windowId].tileData;
    u32 fillValue32 = (fillValue << 24) | (fillValue << 16) | (fillValue << 8) | fillValue;
    s32 size = window.height * window.width * 32;
    u32 width = window.width;
    s32 i;
    s32 srcOffset, destOffset;
    u32 distanceLoop;

    switch (direction)
    {
    case 0:
        for (i = 0; i < size; i += 32)
        {
            distanceLoop = distance;
            MOVE_TILES_DOWN(0)
            MOVE_TILES_DOWN(4)
            MOVE_TILES_DOWN(8)
            MOVE_TILES_DOWN(12)
            MOVE_TILES_DOWN(16)
            MOVE_TILES_DOWN(20)
            MOVE_TILES_DOWN(24)
            MOVE_TILES_DOWN(28)
        }
        break;
    case 1:
        tileData += size - 4;
        for (i = 0; i < size; i += 32)
        {
            distanceLoop = distance;
            MOVE_TILES_UP(0)
            MOVE_TILES_UP(4)
            MOVE_TILES_UP(8)
            MOVE_TILES_UP(12)
            MOVE_TILES_UP(16)
            MOVE_TILES_UP(20)
            MOVE_TILES_UP(24)
            MOVE_TILES_UP(28)
        }
        break;
    case 2:
        break;
    }
}

void CallWindowFunction(u32 windowId, void ( *func)(u8, u8, u8, u8, u8, u8))
{
    struct WindowTemplate window = gWindows[windowId].window;
    func(window.bg, window.tilemapLeft, window.tilemapTop, window.width, window.height, window.paletteNum);
}

bool32 SetWindowAttribute(u32 windowId, u32 attributeId, u32 value)
{
    switch (attributeId)
    {
    case WINDOW_TILEMAP_LEFT:
        gWindows[windowId].window.tilemapLeft = value;
        return FALSE;
    case WINDOW_TILEMAP_TOP:
        gWindows[windowId].window.tilemapTop = value;
        return FALSE;
    case WINDOW_PALETTE_NUM:
        gWindows[windowId].window.paletteNum = value;
        return FALSE;
    case WINDOW_BASE_BLOCK:
        gWindows[windowId].window.baseBlock = value;
        return FALSE;
    case WINDOW_TILE_DATA:
        gWindows[windowId].tileData = (u8 *)(value);
        return TRUE;
    case WINDOW_BG:
    case WINDOW_WIDTH:
    case WINDOW_HEIGHT:
    default:
        return TRUE;
    }
}

u32 GetWindowAttribute(u32 windowId, u32 attributeId)
{
    switch (attributeId)
    {
    case WINDOW_BG:
        return gWindows[windowId].window.bg;
    case WINDOW_TILEMAP_LEFT:
        return gWindows[windowId].window.tilemapLeft;
    case WINDOW_TILEMAP_TOP:
        return gWindows[windowId].window.tilemapTop;
    case WINDOW_WIDTH:
        return gWindows[windowId].window.width;
    case WINDOW_HEIGHT:
        return gWindows[windowId].window.height;
    case WINDOW_PALETTE_NUM:
        return gWindows[windowId].window.paletteNum;
    case WINDOW_BASE_BLOCK:
        return gWindows[windowId].window.baseBlock;
    case WINDOW_TILE_DATA:
        return (u32)(gWindows[windowId].tileData);
    default:
        return 0;
    }
}

static u32 GetNumActiveWindowsOnBg(u32 bgId)
{
    u32 windowsNum = 0;
    s32 i;
    for (i = 0; i < WINDOWS_MAX; i++)
    {
        if (gWindows[i].window.bg == bgId)
            windowsNum++;
    }
    return windowsNum;
}

static void DummyWindowBgTilemap8Bit(void)
{

}

u32 AddWindow8Bit(const struct WindowTemplate *template)
{
    u32 windowId;
    u8 *memAddress;
    u32 bgLayer;

    for (windowId = 0; windowId < WINDOWS_MAX; windowId++)
    {
        if (gWindows[windowId].window.bg == 0xFF)
            break;
    }
    if (windowId == WINDOWS_MAX)
        return WINDOW_NONE;
    bgLayer = template->bg;
    if (gWindowBgTilemapBuffers[bgLayer] == NULL)
    {
        u16 attribute = GetBgAttribute(bgLayer, BG_ATTR_METRIC);
        if (attribute != 0xFFFF)
        {
            s32 i;
            memAddress = Alloc(attribute);
            if (memAddress == NULL)
                return WINDOW_NONE;
            for (i = 0; i < attribute; i++) // if we're going to zero out the memory anyway, why not call AllocZeroed?
                memAddress[i] = 0;
            gWindowBgTilemapBuffers[bgLayer] = memAddress;
            SetBgTilemapBuffer(bgLayer, memAddress);
        }
    }
    memAddress = Alloc((u16)(64 * (template->width * template->height)));
    if (memAddress == NULL)
    {
        if (GetNumActiveWindowsOnBg8Bit(bgLayer) == 0 && gWindowBgTilemapBuffers[bgLayer] != DummyWindowBgTilemap8Bit)
        {
            Free(gWindowBgTilemapBuffers[bgLayer]);
            gWindowBgTilemapBuffers[bgLayer] = NULL;
        }
        return WINDOW_NONE;
    }
    else
    {
        gWindows[windowId].tileData = memAddress;
        gWindows[windowId].window = *template;
        return windowId;
    }
}

void FillWindowPixelBuffer8Bit(u32 windowId, u8 fillValue)
{
    s32 i;
    s32 size;

    size = (u16)(64 * (gWindows[windowId].window.width * gWindows[windowId].window.height));
    for (i = 0; i < size; i++)
        gWindows[windowId].tileData[i] = fillValue;
}

void FillWindowPixelRect8Bit(u32 windowId, u8 fillValue, u16 x, u16 y, u16 width, u16 height)
{
    struct Bitmap pixelRect;

    pixelRect.pixels = gWindows[windowId].tileData;
    pixelRect.width = 8 * gWindows[windowId].window.width;
    pixelRect.height = 8 * gWindows[windowId].window.height;

    FillBitmapRect8Bit(&pixelRect, x, y, width, height, fillValue);
}

void BlitBitmapRectToWindow4BitTo8Bit(u32 windowId, const u8 *pixels, u16 srcX, u16 srcY, u16 srcWidth, int srcHeight, u16 destX, u16 destY, u16 rectWidth, u16 rectHeight, u8 paletteNum)
{
    struct Bitmap sourceRect;
    struct Bitmap destRect;

    sourceRect.pixels = (u8 *) pixels;
    sourceRect.width = srcWidth;
    sourceRect.height = srcHeight;

    destRect.pixels = gWindows[windowId].tileData;
    destRect.width = 8 * gWindows[windowId].window.width;
    destRect.height = 8 * gWindows[windowId].window.height;

    BlitBitmapRect4BitTo8Bit(&sourceRect, &destRect, srcX, srcY, destX, destY, rectWidth, rectHeight, 0, paletteNum);
}

void CopyWindowToVram8Bit(u32 windowId, u8 mode)
{
    struct Window *window = &gWindows[windowId];
    u16 windowSize = 64 * (window->window.width * window->window.height);

    switch (mode)
    {
    case COPYWIN_MAP:
        CopyBgTilemapBufferToVram(window->window.bg);
        break;
    case COPYWIN_GFX:
        LoadBgTiles(window->window.bg, window->tileData, windowSize, window->window.baseBlock);
        break;
    case COPYWIN_FULL:
        LoadBgTiles(window->window.bg, window->tileData, windowSize, window->window.baseBlock);
        CopyBgTilemapBufferToVram(window->window.bg);
        break;
    }
}

static u32 GetNumActiveWindowsOnBg8Bit(u32 bgId)
{
    u32 windowsNum = 0;
    s32 i;
    for (i = 0; i < WINDOWS_MAX; i++)
    {
        if (gWindows[i].window.bg == bgId)
            windowsNum++;
    }
    return windowsNum;
}

u32 WindowWidthPx(u32 windowId)
{
    return gWindows[windowId].window.width * TILE_WIDTH;
}

u32 WindowTemplateWidthPx(const struct WindowTemplate *template)
{
    return template->width * TILE_WIDTH;
}

#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI (src/native_ui) asks whether a window is on screen: its
// tiles are on its background now (put there and not cleared since).
bool32 Window_NativeUiShown(u8 windowId)
{
    u32 bg;
    u16 *tilemap;
    u32 index;

    if (windowId >= WINDOWS_MAX || gWindows[windowId].window.bg == 0xFF)
        return FALSE;
    bg = gWindows[windowId].window.bg;
    tilemap = GetBgTilemapBuffer(bg);
    if (tilemap == NULL || IsTileMapOutsideWram(bg) || GetBgAttribute(bg, BG_ATTR_TYPE) != BG_TYPE_NORMAL)
        return FALSE;
    index = GetTileMapIndexFromCoords(gWindows[windowId].window.tilemapLeft, gWindows[windowId].window.tilemapTop,
                                      GetBgAttribute(bg, BG_ATTR_SCREENSIZE), GetBgMetricTextMode(bg, 1) * 0x20,
                                      GetBgMetricTextMode(bg, 2) * 0x20);
    return (tilemap[index] & 0x3FF) != 0;
}

// ...and its tiles are on the screen itself: the background's map in VRAM still holds them (a battle transition
// draws on BG0's map directly, erasing a message box the buffer still has), its own first tile in its own palette
// at its corner (the Frontier's squares fill BG0's buffer with tiles of their own, over a message box's).
bool32 Window_NativeUiOnScreen(u8 windowId)
{
    u32 bg, index, own;
    const u16 *tilemap, *screen;

    if (!Window_NativeUiShown(windowId))
        return FALSE;
    bg = gWindows[windowId].window.bg;
    if (!(REG_DISPCNT & (DISPCNT_BG0_ON << bg)))
        return FALSE;
    tilemap = GetBgTilemapBuffer(bg);
    index = GetTileMapIndexFromCoords(gWindows[windowId].window.tilemapLeft, gWindows[windowId].window.tilemapTop,
                                      GetBgAttribute(bg, BG_ATTR_SCREENSIZE), GetBgMetricTextMode(bg, 1) * 0x20,
                                      GetBgMetricTextMode(bg, 2) * 0x20);
    screen = (const u16 *)(BG_VRAM + (((*(vu16 *)(REG_ADDR_BG0CNT + bg * 2)) >> 8 & 0x1F) << 11));
    own = (GetBgAttribute(bg, BG_ATTR_BASETILE) + gWindows[windowId].window.baseBlock) & 0x3FF;
    return screen[index] == tilemap[index] && (screen[index] & 0x3FF) == own
        && (screen[index] >> 12) == gWindows[windowId].window.paletteNum;
}
#endif // ENABLE_NATIVE_UI
