#include "global.h"
#include "main.h"
#include "battle.h"
#include "battle_util.h"
#include "bg.h"
#include "chooseboxmon.h"
#include "contest_effect.h"
#include "data.h"
#include "decompress.h"
#include "event_data.h"
#include "field_screen_effect.h"
#include "gpu_regs.h"
#include "item.h"
#include "move_relearner.h"
#include "list_menu.h"
#include "malloc.h"
#include "menu.h"
#include "menu_helpers.h"
#include "menu_specialized.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "constants/party_menu.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "data/tutor_moves.h"

// The different versions of hearts are selected using animation
// commands.
enum {
    APPEAL_HEART_EMPTY,
    APPEAL_HEART_FULL,
    JAM_HEART_EMPTY,
    JAM_HEART_FULL,
};

#define TAG_MODE_ARROWS 5325
#define TAG_LIST_ARROWS 5425
#define GFXTAG_UI       5525
#define PALTAG_UI       5526

#define BATTLE_INFO 0
#define CONTEST_INFO 1
#define RETURN_FROM_MOVE_SELECT 2

struct RelearnType
{
    bool32 (*isActive)(void);
    bool32 (*hasMoveToRelearn)(struct BoxPokemon*);
    u32 (*getMoves)(struct BoxPokemon *, enum Move *);
    const u8 *moveText;
};

static EWRAM_DATA struct
{
    u8 heartSpriteIds[16];
    u16 movesToLearn[MAX_RELEARNER_MOVES];
    struct ListMenuItem menuItems[MAX_RELEARNER_MOVES + 1];
    u8 mainTask;
    u8 numMenuChoices;
    u8 numToShowAtOnce;
    u8 moveListMenuTask;
    u8 moveListScrollArrowTask;
    u8 moveDisplayArrowTask;
    u16 scrollOffset;
    u8 categoryIconSpriteId;
} *sMoveRelearnerStruct = {0};

static EWRAM_DATA struct {
    u16 listOffset;
    u16 listRow;
} sMoveRelearnerScrollState = {0};

EWRAM_DATA enum MoveRelearnerStates gMoveRelearnerState = MOVE_RELEARNER_LEVEL_UP_MOVES;
EWRAM_DATA enum RelearnMode gRelearnMode = RELEARN_MODE_NONE;

static const u16 sUI_Pal[] = INCGFX_U16("graphics/interface/ui_learn_move.png", ".gbapal");

// The arrow sprites in this spritesheet aren't used. The scroll-arrow system provides its own
// arrow sprites.
static const u8 sUI_Tiles[] = INCGFX_U8("graphics/interface/ui_learn_move.png", ".4bpp");

static const struct OamData sHeartSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x8),
    .tileNum = 0,
    .priority = 0,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct OamData sUnusedOam1 =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x16),
    .tileNum = 0,
    .priority = 0,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct OamData sUnusedOam2 =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x8),
    .tileNum = 0,
    .priority = 0,
    .paletteNum = 0,
    .affineParam = 0,
};

static const struct SpriteSheet sMoveRelearnerSpriteSheet =
{
    .data = sUI_Tiles,
    .size = sizeof(sUI_Tiles),
    .tag = GFXTAG_UI
};

static const struct SpritePalette sMoveRelearnerPalette =
{
    .data = sUI_Pal,
    .tag = PALTAG_UI
};

static const struct ScrollArrowsTemplate sDisplayModeArrowsTemplate =
{
    .firstArrowType = SCROLL_ARROW_LEFT,
    .firstX = 27,
    .firstY = 16,
    .secondArrowType = SCROLL_ARROW_RIGHT,
    .secondX = 117,
    .secondY = 16,
    .fullyUpThreshold = -1,
    .fullyDownThreshold = -1,
    .tileTag = TAG_MODE_ARROWS,
    .palTag = TAG_MODE_ARROWS,
    .palNum = 0,
};

static const struct ScrollArrowsTemplate sMoveListScrollArrowsTemplate =
{
    .firstArrowType = SCROLL_ARROW_UP,
    .firstX = 192,
    .firstY = 8,
    .secondArrowType = SCROLL_ARROW_DOWN,
    .secondX = 192,
    .secondY = 104,
    .fullyUpThreshold = 0,
    .fullyDownThreshold = 0,
    .tileTag = TAG_LIST_ARROWS,
    .palTag = TAG_LIST_ARROWS,
    .palNum = 0,
};

static const union AnimCmd sHeartSprite_AppealEmptyFrame[] =
{
    ANIMCMD_FRAME(8, 5, FALSE, FALSE),
    ANIMCMD_END
};

static const union AnimCmd sHeartSprite_AppealFullFrame[] =
{
    ANIMCMD_FRAME(9, 5, FALSE, FALSE),
    ANIMCMD_END
};

static const union AnimCmd sHeartSprite_JamEmptyFrame[] =
{
    ANIMCMD_FRAME(10, 5, FALSE, FALSE),
    ANIMCMD_END
};

static const union AnimCmd sHeartSprite_JamFullFrame[] =
{
    ANIMCMD_FRAME(11, 5, FALSE, FALSE),
    ANIMCMD_END
};

static const union AnimCmd *const sHeartSpriteAnimationCommands[] =
{
    [APPEAL_HEART_EMPTY] = sHeartSprite_AppealEmptyFrame,
    [APPEAL_HEART_FULL] = sHeartSprite_AppealFullFrame,
    [JAM_HEART_EMPTY] = sHeartSprite_JamEmptyFrame,
    [JAM_HEART_FULL] = sHeartSprite_JamFullFrame,
};

static const struct SpriteTemplate sConstestMoveHeartSprite =
{
    .tileTag = GFXTAG_UI,
    .paletteTag = PALTAG_UI,
    .oam = &sHeartSpriteOamData,
    .anims = sHeartSpriteAnimationCommands,
};

static const struct BgTemplate sMoveRelearnerMenuBackgroundTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0,
    },
    {
        .bg = 1,
        .charBaseIndex = 0,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0,
    },
};

static void StoreMoveText(void);
static void CreateLearnableMovesList(void);
static void CreateUISprites(void);
static void CB2_MoveRelearnerMain(void);
static void Task_WaitForFadeOut(u8 taskId);
static void CB2_InitLearnMoveReturnFromSelectMove(void);
static void InitMoveRelearnerBackgroundLayers(void);
static void AddScrollArrows(void);
static void ShowTeachMoveText();
static s32 GetCurrentSelectedMove(void);
static void FreeMoveRelearnerResources(void);
static void RemoveScrollArrows(void);
static bool32 IsLevelUpMoveRelearnerActive(void);
static bool32 IsEggMoveRelearnerActive(void);
static bool32 IsTMMoveRelearnerActive(void);
static bool32 IsTutorMoveRelearnerActive(void);
static bool32 HasRelearnerLevelUpMoves(struct BoxPokemon *boxMon);
static bool32 HasRelearnerEggMoves(struct BoxPokemon *boxMon);
static bool32 HasRelearnerTMMoves(struct BoxPokemon *boxMon);
static bool32 HasRelearnerTutorMoves(struct BoxPokemon *boxMon);
static u32 GetRelearnerLevelUpMoves(struct BoxPokemon *mon, u16 *moves);
static u32 GetRelearnerEggMoves(struct BoxPokemon *mon, u16 *moves);
static u32 GetRelearnerTMMoves(struct BoxPokemon *mon, u16 *moves);
static u32 GetRelearnerTutorMoves(struct BoxPokemon *mon, u16 *moves);
#ifdef PORTABLE
static u32 GetHatchedMonEggMoves(struct BoxPokemon *mon, u16 *moves);
static u32 GetUsedTutorMoves(struct BoxPokemon *mon, u16 *moves);
static u32 GetSwitchableLevelUpMoves(struct BoxPokemon *mon, u16 *moves);
static void ShowNextListHint(void);
static bool32 SwitchList(s32 step);
static void SetSwitchListRows(void);
#endif
#line 271 // gba-line

static void Task_MoveRelearner_HandleInput(u8 taskId);
static void Task_MoveRelearner_LearnMove(u8 taskId);
static void Task_MoveRelearner_Quit(u8 taskId);
static void SortMovesAlphabetically(u16 *moves, u32 numMoves);
static void QuickSortMoves(u16 *moves, s32 left, s32 right);

static const struct RelearnType sRelearnTypes[MOVE_RELEARNER_COUNT] =
{
    [MOVE_RELEARNER_LEVEL_UP_MOVES] = {
        .isActive = IsLevelUpMoveRelearnerActive,
        .hasMoveToRelearn = HasRelearnerLevelUpMoves,
        .getMoves = GetRelearnerLevelUpMoves,
        .moveText = MoveRelearner_Text_LevelUpMoveLWR
    },
    [MOVE_RELEARNER_EGG_MOVES] = {
        .isActive = IsEggMoveRelearnerActive,
        .hasMoveToRelearn = HasRelearnerEggMoves,
        .getMoves = GetRelearnerEggMoves,
        .moveText = MoveRelearner_Text_EggMoveLWR
    },
    [MOVE_RELEARNER_TM_MOVES] = {
        .isActive = IsTMMoveRelearnerActive,
        .hasMoveToRelearn = HasRelearnerTMMoves,
        .getMoves = GetRelearnerTMMoves,
        .moveText = MoveRelearner_Text_TMMoveLWR
    },
    [MOVE_RELEARNER_TUTOR_MOVES] = {
        .isActive = IsTutorMoveRelearnerActive,
        .hasMoveToRelearn = HasRelearnerTutorMoves,
        .getMoves = GetRelearnerTutorMoves,
        .moveText = MoveRelearner_Text_TutorMoveLWR
    },
};

static void VBlankCB_MoveRelearner(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

// Script arguments: The Pokémon to teach is in VAR_0x8004
void TeachMoveRelearnerMove(void)
{
    LockPlayerFieldControls();
    CreateTask(Task_WaitForFadeOut, 10);
    gRelearnMode = RELEARN_MODE_SCRIPT;
    // Fade to black
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
}

static void Task_WaitForFadeOut(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetMainCallback2(CB2_InitLearnMove);
        gFieldCallback = FieldCB_ContinueScriptHandleMusic;
        DestroyTask(taskId);
    }
}

#define tState      data[0]
#define tPartyIndex data[1]
#define tMove       data[2]
#define tRecoverPp  data[3]
#define tCategory   data[4]

static void CB2_InitLearnMove_Basic(void)
{
    switch (gMain.state)
    {
    case 0:
        ResetSpriteData();
        FreeAllSpritePalettes();
        ClearScheduledBgCopiesToVram();
        SetVBlankCallback(VBlankCB_MoveRelearner);
        gMain.state++;
        break;
    case 1:
        InitMoveRelearnerBackgroundLayers();
        InitMoveRelearnerWindows(gTasks[sMoveRelearnerStruct->mainTask].tCategory == CONTEST_INFO);
        gMain.state++;
        break;
    case 2:
        LoadSpriteSheet(&sMoveRelearnerSpriteSheet);
        LoadSpritePalette(&sMoveRelearnerPalette);
        CreateUISprites();
        gMain.state++;
        break;
    case 3:
        StoreMoveText();
        CreateLearnableMovesList();
        sMoveRelearnerStruct->moveListMenuTask = ListMenuInit(&gMultiuseListMenuTemplate, sMoveRelearnerScrollState.listOffset, sMoveRelearnerScrollState.listRow);
        gMain.state++;
        break;
    case 4:
        ShowTeachMoveText();
        MoveRelearnerShowHideHearts(GetCurrentSelectedMove());
        SetBackdropFromColor(RGB_BLACK);
        BeginNormalPaletteFade(PALETTES_ALL, -2, 16, 0, RGB_BLACK);
        gMain.state++;
        break;
    case 5:
        UpdatePaletteFade();
        if (!gPaletteFade.active)
            gMain.state++;
        break;
    default:
        if (gTasks[sMoveRelearnerStruct->mainTask].tState)
        {
            gTasks[sMoveRelearnerStruct->mainTask].func = Task_MoveRelearner_LearnMove;
        }
        else
        {
            AddScrollArrows();
            gTasks[sMoveRelearnerStruct->mainTask].func = Task_MoveRelearner_HandleInput;
        }
        if (gRelearnMode == RELEARN_MODE_SCRIPT)
            gTasks[sMoveRelearnerStruct->mainTask].tRecoverPp = TRUE;
        else
            gTasks[sMoveRelearnerStruct->mainTask].tRecoverPp = P_SUMMARY_MOVE_RELEARNER_FULL_PP;
        SetVBlankCallback(VBlankCB_MoveRelearner);
        SetMainCallback2(CB2_MoveRelearnerMain);
        break;
    }
}

void CB2_InitLearnMove(void)
{
    ResetTasks();
    sMoveRelearnerStruct = AllocZeroed(sizeof(*sMoveRelearnerStruct));
    sMoveRelearnerStruct->mainTask = CreateTask(TaskDummy, 1);
    sMoveRelearnerScrollState.listOffset = 0;
    sMoveRelearnerScrollState.listRow = 0;
    gTasks[sMoveRelearnerStruct->mainTask].tState = 0;
    gTasks[sMoveRelearnerStruct->mainTask].tPartyIndex = gSpecialVar_0x8004;
    gTasks[sMoveRelearnerStruct->mainTask].tMove = MOVE_NONE;
    if (!C_HIDE_CONTEST_DATA && gRelearnMode == RELEARN_MODE_PSS_PAGE_CONTEST_MOVES)
        gTasks[sMoveRelearnerStruct->mainTask].tCategory = CONTEST_INFO;
    else
        gTasks[sMoveRelearnerStruct->mainTask].tCategory = BATTLE_INFO;
    SetMainCallback2(CB2_InitLearnMove_Basic);
}

static void CB2_InitLearnMoveReturnFromSelectMove(void)
{
    ResetTasks();
    sMoveRelearnerStruct = AllocZeroed(sizeof(*sMoveRelearnerStruct));
    sMoveRelearnerStruct->mainTask = CreateTask(TaskDummy, 1);
    gTasks[sMoveRelearnerStruct->mainTask].tState = GetLearnMoveResumeAfterSummaryScreenState();
    gTasks[sMoveRelearnerStruct->mainTask].tPartyIndex = gSpecialVar_0x8008;
    gTasks[sMoveRelearnerStruct->mainTask].tMove = gSpecialVar_0x8009;
    gTasks[sMoveRelearnerStruct->mainTask].tCategory = gSpecialVar_0x800A;
    SetMainCallback2(CB2_InitLearnMove_Basic);
}

static bool32 GameHasDifferentRelearners(void)
{
    if (P_ENABLE_MOVE_RELEARNERS || P_TM_MOVES_RELEARNER)
        return TRUE;
    if (P_FLAG_EGG_MOVES || P_FLAG_TUTOR_MOVES)
        return TRUE;
    return FALSE;
}

static void StoreMoveText(void)
{
    if (GameHasDifferentRelearners() || gRelearnMode == RELEARN_MODE_SCRIPT)
        StringCopy(gStringVar3, sRelearnTypes[gMoveRelearnerState].moveText);
    else
        StringCopy(gStringVar3, MoveRelearner_Text_MoveLWR);
}

static void InitMoveRelearnerBackgroundLayers(void)
{
    ResetVramOamAndBgCntRegs();
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sMoveRelearnerMenuBackgroundTemplates, ARRAY_COUNT(sMoveRelearnerMenuBackgroundTemplates));
    ResetAllBgsCoordinates();
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 |
                                  DISPCNT_OBJ_1D_MAP |
                                  DISPCNT_OBJ_ON);
    ShowBg(0);
    ShowBg(1);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
}

static void CB2_MoveRelearnerMain(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static bool32 ShouldConsumeTmItem(enum Move move)
{
    if (gMoveRelearnerState != MOVE_RELEARNER_TM_MOVES || gRelearnMode == RELEARN_MODE_SCRIPT)
        return FALSE;
    if (I_REUSABLE_TMS || P_ENABLE_ALL_TM_MOVES)
        return FALSE;
    return TRUE;
}

static void FreeMoveRelearnerResources(void)
{
    RemoveScrollArrows();
    DestroyListMenuTask(sMoveRelearnerStruct->moveListMenuTask, &sMoveRelearnerScrollState.listOffset, &sMoveRelearnerScrollState.listRow);
    FreeAllWindowBuffers();
    FREE_AND_SET_NULL(sMoveRelearnerStruct);
    ResetSpriteData();
    FreeAllSpritePalettes();
}

static void UIAskConfirmation(void)
{
    MoveRelearnerCreateYesNoMenu();
}

static s32 UIWaitConfirmation(void)
{
    return Menu_ProcessInputNoWrapClearOnChoose();
}

static void UIPrintMessage(const u8 *message)
{
    StringExpandPlaceholders(gStringVar4, message);
    MoveRelearnerPrintMessage(gStringVar4);
}

static void UIPlayFanfare(u32 songId)
{
    PlayFanfare(songId);
}

static void UIShowMoveList(u8 taskId)
{
    gSpecialVar_0x8008 = gTasks[taskId].tPartyIndex;
    gSpecialVar_0x8009 = gTasks[taskId].tMove;
    gSpecialVar_0x800A = gTasks[taskId].tCategory;
    ShowSelectMovePokemonSummaryScreen(gParties[B_TRAINER_PLAYER], gTasks[taskId].tPartyIndex, CB2_InitLearnMoveReturnFromSelectMove, gTasks[taskId].tMove);
    DestroyTask(taskId);
    FreeMoveRelearnerResources();
}

static void RedrawMoveList(void)
{
    CreateLearnableMovesList();
    if (sMoveRelearnerScrollState.listOffset > sMoveRelearnerStruct->numMenuChoices - sMoveRelearnerStruct->numToShowAtOnce)
        sMoveRelearnerScrollState.listOffset = sMoveRelearnerStruct->numMenuChoices - sMoveRelearnerStruct->numToShowAtOnce;
    DestroyListMenuTask(sMoveRelearnerStruct->moveListMenuTask, NULL, NULL);
    sMoveRelearnerStruct->moveListMenuTask = ListMenuInit(&gMultiuseListMenuTemplate, sMoveRelearnerScrollState.listOffset, sMoveRelearnerScrollState.listRow);
    ShowTeachMoveText();
}

static void UIEndTask(u8 taskId)
{
    if (gSpecialVar_Result == TRUE && ShouldConsumeTmItem(gTasks[taskId].tMove))
    {
        enum Item item = GetTMHMItemIdFromMoveId(gTasks[taskId].tMove);
        if (!GetItemImportance(item))
            RemoveBagItem(item, 1);
    }
    if (gRelearnMode == RELEARN_MODE_SCRIPT && gSpecialVar_Result == TRUE)
    {
        gTasks[taskId].func = Task_MoveRelearner_Quit;
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        return;
    }
    if (gSpecialVar_Result == TRUE)
        RedrawMoveList();
    else
        ShowTeachMoveText();
    AddScrollArrows();
    gTasks[taskId].func = Task_MoveRelearner_HandleInput;
}

#ifdef PORTABLE
// Move switching: "Forget it anyway?" with the cursor on NO (chooseboxmon.c LearnMove, the warnings before a move goes)
static void UIAskConfirmationNoFirst(void)
{
    static const struct WindowTemplate sYesNoTemplate = {
        .bg = 0, .tilemapLeft = 22, .tilemapTop = 8, .width = 5, .height = 4, .paletteNum = 15, .baseBlock = 0x25A,
    }; // menu_specialized.c sMoveRelearnerYesNoMenuTemplate

    CreateYesNoMenu(&sYesNoTemplate, 1, 0xE, 1);
}
#endif
#line 550 // gba-line

static const struct MoveLearnUI sMoveLearnUI =
{
    .askConfirmation = UIAskConfirmation,
    .waitConfirmation = UIWaitConfirmation,
    .printMessage = UIPrintMessage,
    .playFanfare = UIPlayFanfare,
    .showMoveList = UIShowMoveList,
#ifdef PORTABLE
    .askConfirmationNoFirst = UIAskConfirmationNoFirst,
#endif
#line 558 // gba-line
    .endTask = UIEndTask
};

static void Task_MoveRelearner_Quit(u8 taskId)
{
    if (gPaletteFade.active)
        return;

#ifdef PORTABLE
    if (gRelearnMode == RELEARN_MODE_PARTY_MENU)
    {
        // MOVES in the party menu: back to this Pokemon's menu there
        gLastViewedMonIndex = gTasks[taskId].tPartyIndex;
        SetMainCallback2(CB2_ReturnToPartyMenuFromSummaryScreen);
    }
    else
#endif
#line 566 // gba-line
    if (gInitialSummaryScreenCallback != NULL)
    {
        if (gRelearnMode == RELEARN_MODE_PSS_PAGE_CONTEST_MOVES)
            ShowPokemonSummaryScreen(SUMMARY_MODE_RELEARNER_CONTEST, gParties[B_TRAINER_PLAYER], gTasks[taskId].tPartyIndex, gPartiesCount[B_TRAINER_PLAYER] - 1, gInitialSummaryScreenCallback);
        else
            ShowPokemonSummaryScreen(SUMMARY_MODE_RELEARNER_BATTLE, gParties[B_TRAINER_PLAYER], gTasks[taskId].tPartyIndex, gPartiesCount[B_TRAINER_PLAYER] - 1, gInitialSummaryScreenCallback);
    }
    else
    {
        SetMainCallback2(CB2_ReturnToField);
    }

    FreeMoveRelearnerResources();
    gRelearnMode = RELEARN_MODE_NONE;
    DestroyTask(taskId);
}

static void Task_MoveRelearner_Giveup_Answer(u8 taskId)
{
    switch (Menu_ProcessInputNoWrapClearOnChoose())
    {
    case 0: // Yes
        if (gRelearnMode == RELEARN_MODE_SCRIPT)
            gSpecialVar_Result = FALSE;
        gTasks[taskId].func = Task_MoveRelearner_Quit;
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        break;
    case 1: // No
    case MENU_B_PRESSED:
        gTasks[taskId].func = Task_MoveRelearner_HandleInput;
        ShowTeachMoveText();
        AddScrollArrows();
        break;
    }
}

static void Task_MoveRelearner_Giveup_Prompt(u8 taskId)
{

    if (IsTextPrinterActiveOnWindow(RELEARNERWIN_MSG))
        return;

    MoveRelearnerCreateYesNoMenu();
    gTasks[taskId].func = Task_MoveRelearner_Giveup_Answer;
}



static void Task_MoveRelearner_LearnMove(u8 taskId)
{
    if (IsTextPrinterActiveOnWindow(RELEARNERWIN_MSG))
        return;
    gTasks[taskId].tState = LearnMove(&sMoveLearnUI, taskId);
}

static bool32 UpdateMoveRelearnerState(void)
{
    u32 state;
    struct BoxPokemon *boxmon = GetSelectedBoxMonFromPcOrParty();
    for (u32 i = 1; i < MOVE_RELEARNER_COUNT; i++)
    {
        state = (gMoveRelearnerState + i) % MOVE_RELEARNER_COUNT;
        if (CanBoxMonRelearnMoves(boxmon, state))
        {
            gMoveRelearnerState = state;
            StoreMoveText();
            return TRUE;
        }
    }
    return FALSE;
}

static void Task_MoveRelearner_HandleInput(u8 taskId)
{
    s32 itemId = ListMenu_ProcessInput(sMoveRelearnerStruct->moveListMenuTask);
    ListMenuGetScrollAndRow(sMoveRelearnerStruct->moveListMenuTask, &sMoveRelearnerScrollState.listOffset, &sMoveRelearnerScrollState.listRow);

    switch (itemId)
    {
    case LIST_NOTHING_CHOSEN:
#ifdef PORTABLE
        // Move switching: R goes to the next list with moves (as SELECT), L to the one before it (not with the L=A
        // button mode: L is A then); LEFT / RIGHT keep turning the details between battle and contest data
        if (gRelearnMode != RELEARN_MODE_SCRIPT
         && (JOY_NEW(R_BUTTON) || (JOY_NEW(L_BUTTON) && gSaveBlock2Ptr->optionsButtonMode != OPTIONS_BUTTON_MODE_L_EQUALS_A)))
        {
            if (SwitchList(JOY_NEW(R_BUTTON) ? 1 : -1))
            {
                PlaySE(SE_SUCCESS);
                sMoveRelearnerScrollState.listOffset = 0;
                sMoveRelearnerScrollState.listRow = 0;
                RedrawMoveList();
            }
            else if (GameHasDifferentRelearners())
            {
                PlaySE(SE_FAILURE);
            }
            break;
        }
#endif
#line 646 // gba-line
        if (JOY_NEW(SELECT_BUTTON) && gRelearnMode != RELEARN_MODE_SCRIPT)
        {
            if (UpdateMoveRelearnerState())
            {
                PlaySE(SE_SUCCESS);
                sMoveRelearnerScrollState.listOffset = 0;
                sMoveRelearnerScrollState.listRow = 0;
                RedrawMoveList();
            }
            else if (GameHasDifferentRelearners())
            {
                PlaySE(SE_FAILURE);
            }
        }
        else if (!C_HIDE_CONTEST_DATA)
        {
            if (!(JOY_NEW(DPAD_LEFT | DPAD_RIGHT)) && !GetLRKeysPressed())
                break;

            PlaySE(SE_SELECT);

            if (gTasks[taskId].tCategory == BATTLE_INFO)
            {
                PutWindowTilemap(RELEARNERWIN_DESC_CONTEST);
                gTasks[taskId].tCategory = CONTEST_INFO;
            }
            else
            {
                PutWindowTilemap(RELEARNERWIN_DESC_BATTLE);
                gTasks[taskId].tCategory = BATTLE_INFO;
            }

            MoveRelearnerShowHideHearts(GetCurrentSelectedMove());

            ScheduleBgCopyTilemapToVram(1);

            if (B_SHOW_CATEGORY_ICON == TRUE)
                MoveRelearnerShowHideCategoryIcon(GetCurrentSelectedMove());
            AddScrollArrows();
        }
        break;
    case LIST_CANCEL:
        PlaySE(SE_SELECT);
        RemoveScrollArrows();
        gTasks[taskId].func = Task_MoveRelearner_Giveup_Prompt;
        if (gRelearnMode == RELEARN_MODE_SCRIPT)
            StringExpandPlaceholders(gStringVar4, gText_MoveRelearnerGiveUp);
        else
            StringExpandPlaceholders(gStringVar4, gText_MoveRelearnerStop);
        MoveRelearnerPrintMessage(gStringVar4);
        break;
    default:
        PlaySE(SE_SELECT);
        RemoveScrollArrows();
        StringCopy(gStringVar2, GetMoveName(itemId));
        gTasks[taskId].func = Task_MoveRelearner_LearnMove;
        gTasks[taskId].tMove = GetCurrentSelectedMove();
        gTasks[taskId].tState = GetLearnMoveStartAfterPromptState();
        const u8 *message = gText_MoveRelearnerTeachMoveConfirm;
        if (ShouldConsumeTmItem(gTasks[taskId].tMove))
        {
            enum Item item = GetTMHMItemIdFromMoveId(gTasks[taskId].tMove);
            StringCopy(gStringVar3, GetItemName(item));
            if (!GetItemImportance(item))
                message = gText_MoveRelearnerTeachMoveConfirmUseTm;
        }
        UIPrintMessage(message);
        break;
    }
}

static s32 GetCurrentSelectedMove(void)
{
    return sMoveRelearnerStruct->menuItems[sMoveRelearnerScrollState.listRow + sMoveRelearnerScrollState.listOffset].id;
}

static void ShowTeachMoveText(void)
{
    StringExpandPlaceholders(gStringVar4, gText_TeachWhichMoveToPkmn);
    FillWindowPixelBuffer(RELEARNERWIN_MSG, 0x11);
    AddTextPrinterParameterized(RELEARNERWIN_MSG, FONT_NORMAL, gStringVar4, 0, 1, 0, NULL);
#ifdef PORTABLE
    ShowNextListHint(); // "SELECT: TM moves" when SELECT goes to another list
#endif
#line 727 // gba-line
}

static void CreateUISprites(void)
{
    int i;

    sMoveRelearnerStruct->moveDisplayArrowTask = TASK_NONE;
    sMoveRelearnerStruct->moveListScrollArrowTask = TASK_NONE;

    sMoveRelearnerStruct->categoryIconSpriteId = 0xFF;
    LoadCompressedSpriteSheet(&gSpriteSheet_CategoryIcons);
    LoadSpritePalette(&gSpritePal_CategoryIcons);

    // These are the appeal hearts.
    for (i = 0; i < 8; i++)
        sMoveRelearnerStruct->heartSpriteIds[i] = CreateSprite(&sConstestMoveHeartSprite, (i - (i / 4) * 4) * 8 + 104, (i / 4) * 8 + 36, 0);

    // These are the jam harts.
    // The animation is used to toggle between full/empty heart sprites.
    for (i = 0; i < 8; i++)
    {
        sMoveRelearnerStruct->heartSpriteIds[i + 8] = CreateSprite(&sConstestMoveHeartSprite, (i - (i / 4) * 4) * 8 + 104, (i / 4) * 8 + 52, 0);
        StartSpriteAnim(&gSprites[sMoveRelearnerStruct->heartSpriteIds[i + 8]], 2);
    }

    for (i = 0; i < 16; i++)
        gSprites[sMoveRelearnerStruct->heartSpriteIds[i]].invisible = TRUE;
}

static void AddScrollArrows(void)
{
    if (!C_HIDE_CONTEST_DATA && sMoveRelearnerStruct->moveDisplayArrowTask == TASK_NONE)
        sMoveRelearnerStruct->moveDisplayArrowTask = AddScrollIndicatorArrowPair(&sDisplayModeArrowsTemplate, &sMoveRelearnerStruct->scrollOffset);

    if (sMoveRelearnerStruct->moveListScrollArrowTask == TASK_NONE)
    {
        gTempScrollArrowTemplate = sMoveListScrollArrowsTemplate;
        gTempScrollArrowTemplate.fullyDownThreshold = sMoveRelearnerStruct->numMenuChoices - sMoveRelearnerStruct->numToShowAtOnce;
        sMoveRelearnerStruct->moveListScrollArrowTask = AddScrollIndicatorArrowPair(&gTempScrollArrowTemplate, &sMoveRelearnerScrollState.listOffset);
    }
}

static void RemoveScrollArrows(void)
{
    if (!C_HIDE_CONTEST_DATA && sMoveRelearnerStruct->moveDisplayArrowTask != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sMoveRelearnerStruct->moveDisplayArrowTask);
        sMoveRelearnerStruct->moveDisplayArrowTask = TASK_NONE;
    }

    if (sMoveRelearnerStruct->moveListScrollArrowTask != TASK_NONE)
    {
        RemoveScrollIndicatorArrowPair(sMoveRelearnerStruct->moveListScrollArrowTask);
        sMoveRelearnerStruct->moveListScrollArrowTask = TASK_NONE;
    }
}

static void CreateLearnableMovesList(void)
{
    s32 i;

    struct BoxPokemon *boxmon = GetSelectedBoxMonFromPcOrParty();
    if (gRelearnMode == RELEARN_MODE_SCRIPT || sRelearnTypes[gMoveRelearnerState].isActive())
        sMoveRelearnerStruct->numMenuChoices = sRelearnTypes[gMoveRelearnerState].getMoves(boxmon, sMoveRelearnerStruct->movesToLearn);

    if (P_SORT_MOVES)
        SortMovesAlphabetically(sMoveRelearnerStruct->movesToLearn, sMoveRelearnerStruct->numMenuChoices);

    for (i = 0; i < sMoveRelearnerStruct->numMenuChoices; i++)
    {
        sMoveRelearnerStruct->menuItems[i].name = GetMoveName(sMoveRelearnerStruct->movesToLearn[i]);
        sMoveRelearnerStruct->menuItems[i].id = sMoveRelearnerStruct->movesToLearn[i];
    }

    GetBoxMonData(boxmon, MON_DATA_NICKNAME, gStringVar1);

    sMoveRelearnerStruct->menuItems[sMoveRelearnerStruct->numMenuChoices].name = gText_Cancel;
    sMoveRelearnerStruct->menuItems[sMoveRelearnerStruct->numMenuChoices].id = LIST_CANCEL;
    sMoveRelearnerStruct->numMenuChoices++;
    sMoveRelearnerStruct->numToShowAtOnce = LoadMoveRelearnerMovesList(sMoveRelearnerStruct->menuItems, sMoveRelearnerStruct->numMenuChoices);
#ifdef PORTABLE
    SetSwitchListRows(); // each row with where its move comes from ("Lv 12", "TM24")
#endif
#line 807 // gba-line
}

void MoveRelearnerShowHideHearts(s32 move)
{
    u16 numHearts;
    u16 i;

    if (gTasks[sMoveRelearnerStruct->mainTask].tCategory == BATTLE_INFO || move == LIST_CANCEL)
    {
        for (i = 0; i < 16; i++)
            gSprites[sMoveRelearnerStruct->heartSpriteIds[i]].invisible = TRUE;
    }
    else
    {
        numHearts = (u8)(gContestEffects[GetMoveContestEffect(move)].appeal / 10);

        if (numHearts == 0xFF)
            numHearts = 0;

        for (i = 0; i < 8; i++)
        {
            if (i < numHearts)
                StartSpriteAnim(&gSprites[sMoveRelearnerStruct->heartSpriteIds[i]], 1);
            else
                StartSpriteAnim(&gSprites[sMoveRelearnerStruct->heartSpriteIds[i]], 0);
            gSprites[sMoveRelearnerStruct->heartSpriteIds[i]].invisible = FALSE;
        }

        numHearts = (u8)(gContestEffects[GetMoveContestEffect(move)].jam / 10);

        if (numHearts == 0xFF)
            numHearts = 0;

        for (i = 0; i < 8; i++)
        {
            if (i < numHearts)
                StartSpriteAnim(&gSprites[sMoveRelearnerStruct->heartSpriteIds[i + 8]], 3);
            else
                StartSpriteAnim(&gSprites[sMoveRelearnerStruct->heartSpriteIds[i + 8]], 2);
            gSprites[sMoveRelearnerStruct->heartSpriteIds[i + 8]].invisible = FALSE;
        }
    }
}

void MoveRelearnerShowHideCategoryIcon(s32 moveId)
{
    if (gTasks[sMoveRelearnerStruct->mainTask].tCategory == CONTEST_INFO || moveId == LIST_CANCEL)
    {
        if (sMoveRelearnerStruct->categoryIconSpriteId != 0xFF)
            DestroySprite(&gSprites[sMoveRelearnerStruct->categoryIconSpriteId]);

        sMoveRelearnerStruct->categoryIconSpriteId = 0xFF;
    }
    else
    {
        if (sMoveRelearnerStruct->categoryIconSpriteId == 0xFF)
            sMoveRelearnerStruct->categoryIconSpriteId = CreateSprite(&gSpriteTemplate_CategoryIcons, 66, 40, 0);

        gSprites[sMoveRelearnerStruct->categoryIconSpriteId].invisible = FALSE;
        StartSpriteAnim(&gSprites[sMoveRelearnerStruct->categoryIconSpriteId], GetBattleMoveCategory(moveId));
    }
}

static void QuickSortMoves(u16 *moves, s32 left, s32 right)
{
    if (left >= right)
        return;

    u16 pivot = moves[(left + right) / 2];
    s32 i = left, j = right;

    while (i <= j)
    {
        while (moves[i] != MOVE_NONE && StringCompare(GetMoveName(moves[i]), GetMoveName(pivot)) < 0)
            i++;
        while (moves[j] != MOVE_NONE && StringCompare(GetMoveName(moves[j]), GetMoveName(pivot)) > 0)
            j--;

        if (i <= j)
        {
            u16 temp = moves[i];
            moves[i] = moves[j];
            moves[j] = temp;
            i++;
            j--;
        }
    }

    QuickSortMoves(moves, left, j);
    QuickSortMoves(moves, i, right);
}

static void SortMovesAlphabetically(u16 *moves, u32 numMoves)
{
    if (numMoves > 1)
        QuickSortMoves(moves, 0, numMoves - 1);
}

static bool32 IsTmAvailable(enum Item item)
{
    if (P_ENABLE_ALL_TM_MOVES)
        return TRUE;
    if (gRelearnMode == RELEARN_MODE_SCRIPT)
        return TRUE;
#ifdef PORTABLE
    // Move switching: a TM the player has, in the Bag or in the PC's item storage (TMs are never used up on PC:
    // include/config/item.h I_REUSABLE_TMS)
    return CheckBagHasItem(item, 1) || CheckPCHasItem(item, 1);
#endif
#line 911 // gba-line
    return CheckBagHasItem(item, 1);
}

static u32 GetRelearnerLevelUpMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 level = (P_ENABLE_ALL_LEVEL_UP_MOVES ? MAX_LEVEL : GetLevelFromBoxMonExp(mon));
    u32 numMoves = 0;
#ifdef PORTABLE
    return GetSwitchableLevelUpMoves(mon, moves); // a pre-evolution's moves only up to the level it evolved at
#endif
#line 919 // gba-line
    do
    {
        const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);

        for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END; i++)
        {
            if (learnset[i].level > level)
                break;

            if (BoxMonKnowsMove(mon, learnset[i].move))
                continue;

            bool32 alreadyInList = FALSE;
            for (u32 j = 0; j < numMoves; j++)
            {
                if (learnset[i].move == moves[j])
                    alreadyInList = TRUE;
            }
            if (!alreadyInList)
                moves[numMoves++] = learnset[i].move;
        }

        species = (P_PRE_EVO_MOVES ? GetSpeciesPreEvolution(species) : SPECIES_NONE);
    } while (species != SPECIES_NONE);

    return numMoves;
}

static u32 GetRelearnerEggMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 numMoves = 0;
#ifdef PORTABLE
    return GetHatchedMonEggMoves(mon, moves); // a Pokemon that hatched from an Egg: its own family's egg moves
#endif
#line 951 // gba-line
    while (GetSpeciesPreEvolution(species) != SPECIES_NONE)
        species = GetSpeciesPreEvolution(species);

    const u16 *eggMoves = GetSpeciesEggMoves(species);

    if (eggMoves[0] == MOVE_UNAVAILABLE)
        return 0;

    for (u32 i = 0; eggMoves[i] != MOVE_UNAVAILABLE; i++)
    {
        if (!BoxMonKnowsMove(mon, eggMoves[i]))
            moves[numMoves++] = eggMoves[i];
    }

    return numMoves;
}

static u32 GetRelearnerTMMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 numMoves = 0;

    for (u32 i = 0; i < NUM_ALL_MACHINES; i++)
    {
        enum Item item = GetTMHMItemId(i + 1);
        enum Move move = GetTMHMMoveId(i + 1);

        if (move == MOVE_NONE)
            continue;

        if (!IsTmAvailable(item))
            continue;

        if (!CanLearnTeachableMove(species, move))
            continue;

        if (!BoxMonKnowsMove(mon, move))
            moves[numMoves++] = move;
    }

    return numMoves;
}

static u32 GetRelearnerTutorMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 numMoves = 0;
#ifdef PORTABLE
    return GetUsedTutorMoves(mon, moves); // the moves of the one-time tutors the player has used
#endif
#line 998 // gba-line

    for (u32 i = 0; gTutorMoves[i] != MOVE_UNAVAILABLE; i++)
    {
        enum Move move = gTutorMoves[i];

        if (!CanLearnTeachableMove(species, move))
            continue;

        if (!BoxMonKnowsMove(mon, move))
            moves[numMoves++] = move;
    }

    return numMoves;
}

void Special_HasMoveToRelearn(void)
{
    struct BoxPokemon *boxmon = GetSelectedBoxMonFromPcOrParty();
    if (HasMoveToRelearn(boxmon, gMoveRelearnerState))
        gSpecialVar_Result = TRUE;
    else
        gSpecialVar_Result = FALSE;
}

bool32 CanBoxMonRelearnMoves(struct BoxPokemon *boxMon, enum MoveRelearnerStates state)
{
    if (!sRelearnTypes[state].isActive())
        return FALSE;
    if (GetBoxMonData(boxMon, MON_DATA_IS_EGG))
        return FALSE;
    return sRelearnTypes[state].hasMoveToRelearn(boxMon);
}

bool32 HasMoveToRelearn(struct BoxPokemon *boxMon, enum MoveRelearnerStates state)
{
    return sRelearnTypes[state].hasMoveToRelearn(boxMon);
}

static bool32 HasRelearnerLevelUpMoves(struct BoxPokemon *boxMon)
{
    enum Species species = GetBoxMonData(boxMon, MON_DATA_SPECIES);
    u32 level = (P_ENABLE_ALL_LEVEL_UP_MOVES == TRUE) ? MAX_LEVEL : GetLevelFromBoxMonExp(boxMon);
#ifdef PORTABLE
    u16 moves[MAX_RELEARNER_MOVES];
    return GetSwitchableLevelUpMoves(boxMon, moves) != 0;
#endif
#line 1040 // gba-line

    do
    {
        const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);

        for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END; i++)
        {
            if (learnset[i].level > level)
                break;

            if (!BoxMonKnowsMove(boxMon, learnset[i].move))
                return TRUE;
        }

        species = (P_PRE_EVO_MOVES ? GetSpeciesPreEvolution(species) : SPECIES_NONE);

    } while (species != SPECIES_NONE);

    return FALSE;
}

static bool32 HasRelearnerEggMoves(struct BoxPokemon *boxMon)
{
    enum Species species = GetBoxMonData(boxMon, MON_DATA_SPECIES);
#ifdef PORTABLE
    u16 moves[MAX_RELEARNER_MOVES];
    return GetHatchedMonEggMoves(boxMon, moves) != 0;
#endif
#line 1064 // gba-line
    while (GetSpeciesPreEvolution(species) != SPECIES_NONE)
        species = GetSpeciesPreEvolution(species);

    const u16 *eggMoves = GetSpeciesEggMoves(species);

    if (eggMoves[0] == MOVE_UNAVAILABLE)
        return FALSE;

    for (u32 i = 0; eggMoves[i] != MOVE_UNAVAILABLE; i++)
    {
        if (!BoxMonKnowsMove(boxMon, eggMoves[i]))
            return TRUE;
    }

    return FALSE;
}

static bool32 HasRelearnerTMMoves(struct BoxPokemon *boxMon)
{
    enum Species species = GetBoxMonData(boxMon, MON_DATA_SPECIES);
    for (u32 i = 0; i < NUM_ALL_MACHINES; i++)
    {
        enum Item item = GetTMHMItemId(i + 1);
        enum Move move = GetTMHMMoveId(i + 1);

        if (move == MOVE_NONE)
            continue;

        bool32 tmAvailable = IsTmAvailable(item);
        if (!tmAvailable)
            continue;

        if (!CanLearnTeachableMove(species, move))
            continue;

        if (!BoxMonKnowsMove(boxMon, move))
            return TRUE;
    }

    return FALSE;
}

static bool32 HasRelearnerTutorMoves(struct BoxPokemon *boxMon)
{
    enum Species species = GetBoxMonData(boxMon, MON_DATA_SPECIES);
#ifdef PORTABLE
    u16 moves[MAX_RELEARNER_MOVES];
    return GetUsedTutorMoves(boxMon, moves) != 0;
#endif
#line 1109 // gba-line
    for (u32 i = 0; gTutorMoves[i] != MOVE_UNAVAILABLE; i++)
    {
        enum Move move = gTutorMoves[i];

        if (!CanLearnTeachableMove(species, move))
            continue;

        if (!BoxMonKnowsMove(boxMon, move))
            return TRUE;
    }

    return FALSE;
}

static bool32 IsLevelUpMoveRelearnerActive(void)
{
    return TRUE;
}

static bool32 IsEggMoveRelearnerActive(void)
{
    return (FlagGet(P_FLAG_EGG_MOVES) || P_ENABLE_MOVE_RELEARNERS);
}

static bool32 IsTMMoveRelearnerActive(void)
{
    return (P_TM_MOVES_RELEARNER || P_ENABLE_MOVE_RELEARNERS);
}

static bool32 IsTutorMoveRelearnerActive(void)
{
    return (FlagGet(P_FLAG_TUTOR_MOVES) || P_ENABLE_MOVE_RELEARNERS);
}
#ifdef PORTABLE
#include "international_string_util.h"
#include "region_map.h"
#include "constants/region_map_sections.h"

// ---------------------------------------------------------------------------------------------------------------------
// Move switching as in Legends Z-A (PC game; MOVES in the party menu and the summary screen's relearner). The lists:
// - level-up moves up to the Pokemon's level, its pre-evolutions' too (P_PRE_EVO_MOVES), each pre-evolution's only up
//   to the level at which that form evolved, when it evolves by level (GetLevelUpStages), in level order;
// - egg moves, only for a Pokemon that hatched from an Egg (met at level 0), from every stage of its own line: an Egg
//   made without the incense hatches as Marill or Snorlax, not Azurill or Munchlax (P_INCENSE_BREEDING < GEN_9), with
//   that stage's own egg moves;
// - the TMs and HMs the player has (a TM is never used up on PC: include/config/item.h I_REUSABLE_TMS);
// - tutor moves only from the one-time tutors the player has used (each records it in its flag, as move_tutors.inc
//   does); the Battle Frontier's tutors, which charge Battle Points every time, keep no record and are not offered.
// A move that would not be on a list once switched out, or one with PP Ups, is not switched out before the player says
// so (MoveRelearner_ForgetWarning, chooseboxmon.c LearnMove).

static const struct
{
    u16 move;
    u16 flag;
    u16 mapsec;     // where the tutor is (the details' "the tutor in ...")
} sOneTimeTutors[] =
{
    {MOVE_DOUBLE_EDGE, FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE, MAPSEC_SOOTOPOLIS_CITY},
    {MOVE_DYNAMIC_PUNCH, FLAG_MOVE_TUTOR_TAUGHT_DYNAMICPUNCH, MAPSEC_MOSSDEEP_CITY},
    {MOVE_EXPLOSION, FLAG_MOVE_TUTOR_TAUGHT_EXPLOSION, MAPSEC_PACIFIDLOG_TOWN},
    {MOVE_FURY_CUTTER, FLAG_MOVE_TUTOR_TAUGHT_FURY_CUTTER, MAPSEC_VERDANTURF_TOWN},
    {MOVE_METRONOME, FLAG_MOVE_TUTOR_TAUGHT_METRONOME, MAPSEC_FALLARBOR_TOWN},
    {MOVE_MIMIC, FLAG_MOVE_TUTOR_TAUGHT_MIMIC, MAPSEC_LAVARIDGE_TOWN},
    {MOVE_ROLLOUT, FLAG_MOVE_TUTOR_TAUGHT_ROLLOUT, MAPSEC_MAUVILLE_CITY},
    {MOVE_SLEEP_TALK, FLAG_MOVE_TUTOR_TAUGHT_SLEEP_TALK, MAPSEC_FORTREE_CITY},
    {MOVE_SUBSTITUTE, FLAG_MOVE_TUTOR_TAUGHT_SUBSTITUTE, MAPSEC_LILYCOVE_CITY},
    {MOVE_SWAGGER, FLAG_MOVE_TUTOR_TAUGHT_SWAGGER, MAPSEC_SLATEPORT_CITY},
};

static const u8 *const sRelearnerListNames[MOVE_RELEARNER_COUNT] =
{
    [MOVE_RELEARNER_LEVEL_UP_MOVES] = COMPOUND_STRING("Level-up moves"),
    [MOVE_RELEARNER_EGG_MOVES]      = COMPOUND_STRING("Egg moves"),
    [MOVE_RELEARNER_TM_MOVES]       = COMPOUND_STRING("TM moves"),
    [MOVE_RELEARNER_TUTOR_MOVES]    = COMPOUND_STRING("Tutor moves"),
};

static const u8 sText_CantSwitchBack[] = _("{STR_VAR_2} can't be\nswitched back in later.\pForget it anyway?");
static const u8 sText_PpUpsLost[] = _("{STR_VAR_2}'s PP Ups\nwill be lost.\pForget it anyway?");
static const u8 sText_CantSwitchBackPpUps[] = _("{STR_VAR_2} can't be\nswitched back in later.\pIts PP Ups will be lost.\nForget it anyway?");

static bool32 HatchedFromEgg(struct BoxPokemon *mon)
{
    return GetBoxMonData(mon, MON_DATA_MET_LEVEL) == 0;
}

static bool32 IsMoveInList(const u16 *moves, u32 numMoves, u32 move)
{
    for (u32 i = 0; i < numMoves; i++)
    {
        if (moves[i] == move)
            return TRUE;
    }
    return FALSE;
}

static u32 GetHatchedMonEggMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 numMoves = 0;

    if (!HatchedFromEgg(mon))
        return 0;
    do
    {
        const u16 *eggMoves = GetSpeciesEggMoves(species);

        for (u32 i = 0; eggMoves[i] != MOVE_UNAVAILABLE && numMoves < MAX_RELEARNER_MOVES; i++)
        {
            if (!BoxMonKnowsMove(mon, eggMoves[i]) && !IsMoveInList(moves, numMoves, eggMoves[i]))
                moves[numMoves++] = eggMoves[i];
        }
        species = GetSpeciesPreEvolution(species);
    } while (species != SPECIES_NONE);
    return numMoves;
}

static u32 GetUsedTutorMoves(struct BoxPokemon *mon, u16 *moves)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 numMoves = 0;

    for (u32 i = 0; i < ARRAY_COUNT(sOneTimeTutors); i++)
    {
        if (!FlagGet(sOneTimeTutors[i].flag) || !CanLearnTeachableMove(species, sOneTimeTutors[i].move))
            continue;
        if (!BoxMonKnowsMove(mon, sOneTimeTutors[i].move))
            moves[numMoves++] = sOneTimeTutors[i].move;
    }
    return numMoves;
}

// -------------------------------------------------------------------------------- level-up moves and pre-evolutions
// A Pokemon's level-up moves count up to its level. With P_PRE_EVO_MOVES its earlier forms' count too, but each only
// up to the level at which that form evolved: Charmander evolves at Lv 16, so a Lv 20 Charmeleon gets Charmander's
// moves to Lv 16 (no Scary Face: Charmander's at Lv 19, Charmeleon's own at Lv 21) - what it really could have learned
// on the way. The rules for that cap (PreEvolutionLevelCap):
// - an evolution by level (EVO_LEVEL or EVO_LEVEL_BATTLE_ONLY with a level; with or without other conditions): that
//   level, and never above the next form's own cap (Charizard Lv 50: Charmeleon's moves to Lv 36, Charmander's to 16);
// - any other evolution (a stone, a trade, friendship or another condition with no level: EVO_LEVEL with level 0) has
//   no level of its own: the earlier form may have stayed as it was until the next form's cap, so that cap is its cap
//   (a Lv 40 Ninetales: Vulpix's moves to Lv 40; a Lv 40 Raichu: Pikachu's and Pichu's, a friendship evolution, too);
// - Shedinja (EVO_SPLIT_FROM_EVO) comes when Nincada evolves into Ninjask: that evolution's level (Lv 20);
// - several evolutions into the same form (Feebas: a trade or its beauty; Nincada): the highest cap, as either may have
//   been the one.
// Moves above the Pokemon's own level are never offered (P_ENABLE_ALL_LEVEL_UP_MOVES lifts every cap, as it did).
#define MAX_SWITCH_STAGES 4  // a Pokemon and its earlier forms (three at most in the game's data)

static u32 PreEvolutionLevelCap(enum Species preEvo, enum Species evolved, u32 evolvedCap)
{
    const struct Evolution *evos = GetSpeciesEvolutions(preEvo);
    bool32 found = FALSE;
    u32 cap = 0;

    for (u32 i = 0; evos != NULL && evos[i].method != EVOLUTIONS_END; i++)
    {
        u32 method = evos[i].method, level = evos[i].param, stageCap = evolvedCap;

        if (!IsSpeciesEnabled(evos[i].targetSpecies) || SanitizeSpeciesId(evos[i].targetSpecies) != evolved)
            continue;
        if (method == EVO_SPLIT_FROM_EVO)
        {
            // the clone comes with the evolution into param (Ninjask): that one's method and level
            method = EVO_NONE;
            for (u32 j = 0; evos[j].method != EVOLUTIONS_END; j++)
            {
                if (evos[j].method != EVO_SPLIT_FROM_EVO && evos[j].targetSpecies == evos[i].param)
                {
                    method = evos[j].method;
                    level = evos[j].param;
                    break;
                }
            }
        }
        if ((method == EVO_LEVEL || method == EVO_LEVEL_BATTLE_ONLY) && level != 0 && level < evolvedCap)
            stageCap = level;
        if (stageCap > cap)
            cap = stageCap;
        found = TRUE;
    }
    return found ? cap : evolvedCap;
}

// The Pokemon's form and its earlier ones (its own first), and the level up to which each one's level-up moves count;
// how many count (its own alone without P_PRE_EVO_MOVES). *forms: all of them (the egg moves' stages).
static u32 GetLevelUpStages(struct BoxPokemon *mon, u16 *stages, u8 *caps, u32 *forms)
{
    enum Species species = GetBoxMonData(mon, MON_DATA_SPECIES);
    u32 cap = (P_ENABLE_ALL_LEVEL_UP_MOVES ? MAX_LEVEL : GetLevelFromBoxMonExp(mon)), count = 0;

    while (species != SPECIES_NONE && count < MAX_SWITCH_STAGES)
    {
        enum Species preEvo;

        stages[count] = species;
        caps[count++] = cap;
        if ((preEvo = GetSpeciesPreEvolution(species)) == SPECIES_NONE)
            break;
        if (!P_ENABLE_ALL_LEVEL_UP_MOVES)
            cap = PreEvolutionLevelCap(preEvo, species, cap);
        species = preEvo;
    }
    if (forms != NULL)
        *forms = count;
    return P_PRE_EVO_MOVES ? count : min(count, 1);
}

static bool32 FindLevelUpSourceIn(const u16 *stages, const u8 *caps, u32 count, enum Move move, u8 *level, u16 *stage);

// Where the level-up list has `move`: the first form (its own first) that learns it within its cap, at what level
static bool32 FindLevelUpSource(struct BoxPokemon *mon, enum Move move, u8 *level, u16 *stage)
{
    u16 stages[MAX_SWITCH_STAGES];
    u8 caps[MAX_SWITCH_STAGES];
    u32 count = GetLevelUpStages(mon, stages, caps, NULL);

    return FindLevelUpSourceIn(stages, caps, count, move, level, stage);
}

static bool32 FindLevelUpSourceIn(const u16 *stages, const u8 *caps, u32 count, enum Move move, u8 *level, u16 *stage)
{
    for (u32 s = 0; s < count; s++)
    {
        const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(stages[s]);

        for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= caps[s]; i++)
        {
            if (learnset[i].move != move)
                continue;
            if (level != NULL)
                *level = learnset[i].level;
            if (stage != NULL)
                *stage = stages[s];
            return TRUE;
        }
    }
    return FALSE;
}

// A form's own move (the owner's report of 2026-10-31: Rotom-Wash had no Hydro Pump to take): these forms share their
// base form's learnset, and the game teaches their moves only as the form changes (Rotom's appliances by the catalog,
// party_menu.c; Necrozma's fusions by the N-Solarizer and N-Lunarizer), so one that did not learn it then, or let it
// go, had no way back to it. Each is on the level-up list of that form, never another's. (Kyurem's and Calyrex's fused
// forms have the moves their fusions teach in learnsets of their own.)
static const u16 sFormMoves[][2] =
{
    {SPECIES_ROTOM_HEAT,  MOVE_OVERHEAT},
    {SPECIES_ROTOM_WASH,  MOVE_HYDRO_PUMP},
    {SPECIES_ROTOM_FROST, MOVE_BLIZZARD},
    {SPECIES_ROTOM_FAN,   MOVE_AIR_SLASH},
    {SPECIES_ROTOM_MOW,   MOVE_LEAF_STORM},
    {SPECIES_NECROZMA_DUSK_MANE,  MOVE_SUNSTEEL_STRIKE},
    {SPECIES_NECROZMA_DAWN_WINGS, MOVE_MOONGEIST_BEAM},
};

enum Move MoveRelearner_FormMove(enum Species species)
{
    for (u32 i = 0; i < ARRAY_COUNT(sFormMoves); i++)
    {
        if (sFormMoves[i][0] == species)
            return sFormMoves[i][1];
    }
    return MOVE_NONE;
}

// The level-up list: the form's own move (first, as a move learned on evolving), then every move of those forms within
// their caps that the Pokemon does not know, each once, in level order (a pre-evolution's move among the Pokemon's own
// at its level; level 0, learned on evolving, first)
static u32 GetSwitchableLevelUpMoves(struct BoxPokemon *mon, u16 *moves)
{
    u16 stages[MAX_SWITCH_STAGES];
    u8 caps[MAX_SWITCH_STAGES], levels[MAX_RELEARNER_MOVES];
    u32 count = GetLevelUpStages(mon, stages, caps, NULL), numMoves = 0;
    enum Move formMove = MoveRelearner_FormMove(GetBoxMonData(mon, MON_DATA_SPECIES));

    if (formMove != MOVE_NONE && !BoxMonKnowsMove(mon, formMove))
    {
        moves[numMoves] = formMove;
        levels[numMoves++] = 0;
    }

    for (u32 s = 0; s < count; s++)
    {
        const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(stages[s]);

        for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= caps[s]; i++)
        {
            u32 at = numMoves;

            if (numMoves >= MAX_RELEARNER_MOVES)
                break;
            if (BoxMonKnowsMove(mon, learnset[i].move) || IsMoveInList(moves, numMoves, learnset[i].move))
                continue;
            while (at > 0 && levels[at - 1] > learnset[i].level)
            {
                moves[at] = moves[at - 1];
                levels[at] = levels[at - 1];
                at--;
            }
            moves[at] = learnset[i].move;
            levels[at] = learnset[i].level;
            numMoves++;
        }
    }
    return numMoves;
}

// ------------------------------------------------------------------------------------- the lists and their sources
u32 MoveRelearner_GetListMoves(struct BoxPokemon *boxMon, enum MoveRelearnerStates list, u16 *moves)
{
    if (list >= MOVE_RELEARNER_COUNT || GetBoxMonData(boxMon, MON_DATA_IS_EGG))
        return 0;
    if (gRelearnMode != RELEARN_MODE_SCRIPT && !sRelearnTypes[list].isActive())
        return 0;
    return sRelearnTypes[list].getMoves(boxMon, moves);
}

// MoveRelearner_GetSource with the Pokemon's forms already found (GetLevelUpStages: count with their caps, forms all)
static bool32 GetSourceIn(const u16 *stages, const u8 *caps, u32 count, u32 forms, enum MoveRelearnerStates list,
                          enum Move move, struct MoveSwitchSource *out)
{
    out->kind = MOVE_SOURCE_NONE;
    out->number = 0;
    out->species = SPECIES_NONE;
    switch (list)
    {
    case MOVE_RELEARNER_LEVEL_UP_MOVES:
        if (count > 0 && move != MOVE_NONE && MoveRelearner_FormMove(stages[0]) == move)
        {
            out->kind = MOVE_SOURCE_FORM;
            out->species = stages[0];
            return TRUE;
        }
        if (!FindLevelUpSourceIn(stages, caps, count, move, &out->number, &out->species))
            return FALSE;
        out->kind = MOVE_SOURCE_LEVEL;
        return TRUE;
    case MOVE_RELEARNER_EGG_MOVES:
        out->kind = MOVE_SOURCE_EGG;
        for (u32 n = 0; n < forms; n++)
        {
            const u16 *eggMoves = GetSpeciesEggMoves(stages[n]);

            for (u32 i = 0; eggMoves[i] != MOVE_UNAVAILABLE; i++)
            {
                if (eggMoves[i] == move)
                {
                    out->species = stages[n];
                    return TRUE;
                }
            }
        }
        return TRUE;
    case MOVE_RELEARNER_TM_MOVES:
    {
        u32 index = GetItemTMHMIndex(GetTMHMItemIdFromMoveId(move));

        if (index == 0)
            return FALSE;
        out->kind = index <= NUM_TECHNICAL_MACHINES ? MOVE_SOURCE_TM : MOVE_SOURCE_HM;
        out->number = index <= NUM_TECHNICAL_MACHINES ? index : index - NUM_TECHNICAL_MACHINES;
        return TRUE;
    }
    case MOVE_RELEARNER_TUTOR_MOVES:
        out->kind = MOVE_SOURCE_TUTOR;
        out->number = 0xFF;
        for (u32 i = 0; i < ARRAY_COUNT(sOneTimeTutors); i++)
        {
            if (sOneTimeTutors[i].move == move)
                out->number = i;
        }
        return TRUE;
    default:
        return FALSE;
    }
}

bool32 MoveRelearner_GetSource(struct BoxPokemon *boxMon, enum MoveRelearnerStates list, enum Move move,
                               struct MoveSwitchSource *out)
{
    u16 stages[MAX_SWITCH_STAGES];
    u8 caps[MAX_SWITCH_STAGES];
    u32 forms = 0, count = 0;

    if (list == MOVE_RELEARNER_LEVEL_UP_MOVES || list == MOVE_RELEARNER_EGG_MOVES)
        count = GetLevelUpStages(boxMon, stages, caps, &forms);
    return GetSourceIn(stages, caps, count, forms, list, move, out);
}

u8 *MoveRelearner_SourceText(const struct MoveSwitchSource *source, u8 *dst, bool32 compact)
{
    switch (source->kind)
    {
    case MOVE_SOURCE_LEVEL:
        if (source->number == 0)
            return StringCopy(dst, COMPOUND_STRING("Evo"));
        dst = StringCopy(dst, compact ? COMPOUND_STRING("Lv") : COMPOUND_STRING("Lv "));
        return ConvertIntToDecimalStringN(dst, source->number, STR_CONV_MODE_LEFT_ALIGN, 3);
    case MOVE_SOURCE_TM:
    case MOVE_SOURCE_HM:
        dst = StringCopy(dst, source->kind == MOVE_SOURCE_TM ? COMPOUND_STRING("TM") : COMPOUND_STRING("HM"));
        return ConvertIntToDecimalStringN(dst, source->number, STR_CONV_MODE_LEADING_ZEROS, source->number >= 100 ? 3 : 2);
    case MOVE_SOURCE_EGG:
        return StringCopy(dst, COMPOUND_STRING("Egg"));
    case MOVE_SOURCE_TUTOR:
        return StringCopy(dst, COMPOUND_STRING("Tutor"));
    case MOVE_SOURCE_FORM:
        return StringCopy(dst, COMPOUND_STRING("Form"));
    default:
        *dst = EOS;
        return dst;
    }
}

u8 *MoveRelearner_TutorPlace(u32 tutor, u8 *dst)
{
    if (tutor >= ARRAY_COUNT(sOneTimeTutors))
    {
        *dst = EOS;
        return dst;
    }
    return GetMapName(dst, sOneTimeTutors[tutor].mapsec, 0);
}

bool32 MoveRelearner_ListIsEmpty(void)
{
    return sMoveRelearnerStruct != NULL && sMoveRelearnerStruct->numMenuChoices <= 1;
}

bool32 MoveRelearner_ShownList(u32 *list, u16 *moves, u32 max, u32 *count, u32 *cursor)
{
    if (sMoveRelearnerStruct == NULL || gMain.callback2 != CB2_MoveRelearnerMain || sMoveRelearnerStruct->numMenuChoices == 0)
        return FALSE;
    *list = gMoveRelearnerState;
    *count = min(sMoveRelearnerStruct->numMenuChoices - 1, max);
    for (u32 i = 0; i < *count; i++)
        moves[i] = sMoveRelearnerStruct->menuItems[i].id;
    *cursor = sMoveRelearnerScrollState.listOffset + sMoveRelearnerScrollState.listRow;
    return TRUE;
}

// L / R (and SELECT, UpdateMoveRelearnerState): the next list with moves that way
static bool32 SwitchList(s32 step)
{
    struct BoxPokemon *boxmon = GetSelectedBoxMonFromPcOrParty();

    for (s32 i = 1; i < MOVE_RELEARNER_COUNT; i++)
    {
        u32 state = (gMoveRelearnerState + MOVE_RELEARNER_COUNT + step * i) % MOVE_RELEARNER_COUNT;

        if (CanBoxMonRelearnMoves(boxmon, state))
        {
            gMoveRelearnerState = state;
            StoreMoveText();
            return TRUE;
        }
    }
    return FALSE;
}

// The list's rows (the Classic 2D screen; the 3D build draws its own): on the right of the move's name, where it comes
// from on the level-up list ("Lv12", "Evo") and the TM list ("TM24", "HM03") - the egg and tutor lists say it in their
// message already. The names give the tag its room (a narrower font where needed, SWITCH_NAME_ROOM); a name that has
// no room even so keeps the number alone ("12"), or no tag.
#define SWITCH_ROW_WIDTH 72     // the list window's 80 pixels from the name's x (sMoveRelearnerMovesListTemplate.item_X)
#define SWITCH_NAME_ROOM 54     // the name's room beside a four-character tag

static void PrintSwitchListTag(u8 windowId, u32 move, u8 y)
{
    static const u8 sTagColors[3] = {TEXT_COLOR_WHITE, TEXT_COLOR_BLUE, TEXT_COLOR_LIGHT_BLUE};
    struct MoveSwitchSource source;
    const u8 *name;
    u32 nameWidth;
    u8 tag[12];

    if ((s32)move == LIST_CANCEL || move == MOVE_NONE
     || !MoveRelearner_GetSource(GetSelectedBoxMonFromPcOrParty(), gMoveRelearnerState, move, &source))
        return;
    name = GetMoveName(move);
    nameWidth = GetStringWidth(GetFontIdToFit(name, FONT_NORMAL, 0, SWITCH_NAME_ROOM), name, 0);
    MoveRelearner_SourceText(&source, tag, TRUE);
    if (nameWidth + 2 + GetStringWidth(FONT_SMALL_NARROWER, tag, 0) > SWITCH_ROW_WIDTH)
        ConvertIntToDecimalStringN(tag, source.number, STR_CONV_MODE_LEFT_ALIGN, 3);
    if (nameWidth + 2 + GetStringWidth(FONT_SMALL_NARROWER, tag, 0) > SWITCH_ROW_WIDTH)
        return;
    AddTextPrinterParameterized4(windowId, FONT_SMALL_NARROWER, 8 + SWITCH_ROW_WIDTH - GetStringWidth(FONT_SMALL_NARROWER, tag, 0),
                                 y + 1, 0, 0, sTagColors, TEXT_SKIP_DRAW, tag);
}

static void SetSwitchListRows(void)
{
    if (!MoveRelearner_IsMoveSwitching()
     || (gMoveRelearnerState != MOVE_RELEARNER_LEVEL_UP_MOVES && gMoveRelearnerState != MOVE_RELEARNER_TM_MOVES))
        return;
    gMultiuseListMenuTemplate.itemPrintFunc = PrintSwitchListTag;
    gMultiuseListMenuTemplate.textNarrowWidth = SWITCH_NAME_ROOM;
}

// Right on the message's second line, the list SELECT goes to ("SELECT TM moves"), when it goes anywhere
static void ShowNextListHint(void)
{
    struct BoxPokemon *boxmon;

    if (gRelearnMode == RELEARN_MODE_SCRIPT || !GameHasDifferentRelearners())
        return;
    boxmon = GetSelectedBoxMonFromPcOrParty();
    for (u32 i = 1; i < MOVE_RELEARNER_COUNT; i++)
    {
        u32 state = (gMoveRelearnerState + i) % MOVE_RELEARNER_COUNT;

        if (CanBoxMonRelearnMoves(boxmon, state))
        {
            u8 text[32];

            StringCopy(StringCopy(text, COMPOUND_STRING("{SELECT_BUTTON} ")), sRelearnerListNames[state]);
            AddTextPrinterParameterized(RELEARNERWIN_MSG, FONT_SMALL, text,
                                        GetStringRightAlignXOffset(FONT_SMALL, text, WindowWidthPx(RELEARNERWIN_MSG) - 2),
                                        19, 0, NULL);
            return;
        }
    }
}

bool32 MoveRelearner_IsMoveSwitching(void)
{
    return gRelearnMode == RELEARN_MODE_PARTY_MENU || gRelearnMode == RELEARN_MODE_PSS_PAGE_BATTLE_MOVES
        || gRelearnMode == RELEARN_MODE_PSS_PAGE_CONTEST_MOVES;
}

// Would `move` be on one of the lists for this Pokemon if it did not know it?
bool32 MoveRelearner_CanSwitchBackIn(struct BoxPokemon *boxMon, enum Move move)
{
    enum Species species = GetBoxMonData(boxMon, MON_DATA_SPECIES);

    if (move == MOVE_NONE || GetBoxMonData(boxMon, MON_DATA_IS_EGG))
        return FALSE;
    if (IsLevelUpMoveRelearnerActive()
     && (MoveRelearner_FormMove(species) == move || FindLevelUpSource(boxMon, move, NULL, NULL)))
        return TRUE;
    if (IsEggMoveRelearnerActive() && HatchedFromEgg(boxMon))
    {
        enum Species stage = species;

        do
        {
            const u16 *eggMoves = GetSpeciesEggMoves(stage);

            for (u32 i = 0; eggMoves[i] != MOVE_UNAVAILABLE; i++)
            {
                if (eggMoves[i] == move)
                    return TRUE;
            }
            stage = GetSpeciesPreEvolution(stage);
        } while (stage != SPECIES_NONE);
    }
    if (IsTMMoveRelearnerActive())
    {
        enum Item item = GetTMHMItemIdFromMoveId(move);

        if (item != ITEM_NONE && IsTmAvailable(item) && CanLearnTeachableMove(species, move))
            return TRUE;
    }
    if (IsTutorMoveRelearnerActive())
    {
        for (u32 i = 0; i < ARRAY_COUNT(sOneTimeTutors); i++)
        {
            if (sOneTimeTutors[i].move == move && FlagGet(sOneTimeTutors[i].flag) && CanLearnTeachableMove(species, move))
                return TRUE;
        }
    }
    return FALSE;
}

// An HM's move can be switched out (P_CAN_FORGET_HIDDEN_MOVE is FALSE) by move switching, when it can be switched back
// in: the HM is a key item, never used up. Anywhere else it stays, as in the GBA game (an HM learned from a traded
// Pokemon with no HM in the Bag stays too: one that dived could not come up again).
bool32 MoveRelearner_CanForgetHiddenMove(struct BoxPokemon *boxMon, enum Move move)
{
    return MoveRelearner_IsMoveSwitching() && MoveRelearner_CanSwitchBackIn(boxMon, move);
}

// What the player is told before move switching takes the move in `slot` out (NULL: nothing to tell)
const u8 *MoveRelearner_ForgetWarning(struct BoxPokemon *boxMon, u32 slot)
{
    enum Move move;
    bool32 gone, ppUps;

    if (!MoveRelearner_IsMoveSwitching() || slot >= MAX_MON_MOVES)
        return NULL;
    move = GetBoxMonData(boxMon, MON_DATA_MOVE1 + slot);
    if (move == MOVE_NONE)
        return NULL;
    gone = !MoveRelearner_CanSwitchBackIn(boxMon, move);
    ppUps = ((GetBoxMonData(boxMon, MON_DATA_PP_BONUSES) >> (2 * slot)) & 3) != 0;
    if (gone && ppUps)
        return sText_CantSwitchBackPpUps;
    if (gone)
        return sText_CantSwitchBack;
    if (ppUps)
        return sText_PpUpsLost;
    return NULL;
}
#endif
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI (src/native_ui/screens/ui_move_switch.c): the relearner as it is now, the summary's move list that
// chooses the move to forget for it included. Read at the frame boundary; changes nothing.
bool32 MoveRelearner_NativeUiState(struct NativeMoveRelearner *out)
{
    struct BoxPokemon *boxMon;
    u16 stages[MAX_SWITCH_STAGES], moves[MAX_RELEARNER_MOVES];
    u8 caps[MAX_SWITCH_STAGES];
    u32 forms = 0, count;
    struct Task *task = NULL;
    bool32 pending = FALSE;

    memset(out, 0, sizeof(*out));
    out->forgetSlot = MAX_MON_MOVES;
    out->msgWindow = RELEARNERWIN_MSG;
    if (gMain.callback2 == CB2_InitLearnMove_Basic || gMain.callback2 == CB2_MoveRelearnerMain)
    {
        if (sMoveRelearnerStruct == NULL)
            return FALSE;
        task = &gTasks[sMoveRelearnerStruct->mainTask];
    }
    else if (gMain.callback2 == CB2_InitLearnMove || gMain.callback2 == CB2_InitLearnMoveReturnFromSelectMove)
    {
        // the frame before it starts (from the party menu or the summary; back from the move to forget)
        pending = TRUE;
    }
    else if (gRelearnMode == RELEARN_MODE_NONE || !Summary_NativeUiExitsTo(CB2_InitLearnMoveReturnFromSelectMove))
    {
        return FALSE;
    }
    boxMon = GetSelectedBoxMonFromPcOrParty();
    out->mode = gRelearnMode;
    out->list = gMoveRelearnerState;
    out->switching = MoveRelearner_IsMoveSwitching();
    out->contestData = !C_HIDE_CONTEST_DATA;
    for (u32 i = 0; i < MOVE_RELEARNER_COUNT; i++)
    {
        // the Move Reminder has its one list; elsewhere the lists the game has (SELECT, L and R go through them)
        out->listOn[i] = gRelearnMode == RELEARN_MODE_SCRIPT ? i == gMoveRelearnerState
                       : GameHasDifferentRelearners() ? sRelearnTypes[i].isActive() : i == MOVE_RELEARNER_LEVEL_UP_MOVES;
        out->listCount[i] = out->listOn[i] ? MoveRelearner_GetListMoves(boxMon, i, moves) : 0;
    }
    if (pending)
    {
        out->phase = NATIVE_RELEARNER_LOADING;
        out->returning = gMain.callback2 == CB2_InitLearnMoveReturnFromSelectMove;
        if (out->returning)
        {
            out->teaching = gSpecialVar_0x8009;
            out->contest = gSpecialVar_0x800A == CONTEST_INFO;
            if (GetMoveSlotToReplace() < MAX_MON_MOVES)
                out->forgetSlot = GetMoveSlotToReplace();
        }
    }
    else if (task == NULL)
    {
        // the summary's move list (UIShowMoveList left the move and the details' kind in the special vars)
        out->phase = NATIVE_RELEARNER_FORGET;
        out->teaching = gSpecialVar_0x8009;
        out->contest = gSpecialVar_0x800A == CONTEST_INFO;
    }
    else
    {
        out->contest = task->tCategory == CONTEST_INFO;
        out->teaching = task->tMove;
        if (gMain.callback2 == CB2_InitLearnMove_Basic)
        {
            out->phase = NATIVE_RELEARNER_LOADING;
            out->returning = task->tState != 0;
            if (out->returning && GetMoveSlotToReplace() < MAX_MON_MOVES)
                out->forgetSlot = GetMoveSlotToReplace(); // back with the move chosen to forget
        }
        else if (task->func == Task_MoveRelearner_HandleInput)
        {
            out->phase = NATIVE_RELEARNER_BROWSE;
            out->teaching = MOVE_NONE;
            out->ready = !gPaletteFade.active;
        }
        else if (task->func == Task_MoveRelearner_LearnMove)
        {
            out->phase = NATIVE_RELEARNER_TEACHING;
            out->step = LearnMove_NativeUiStep(task->tState, &out->yesNo, &out->noFirst);
            out->msgStale = task->tState == GetLearnMoveResumeAfterSummaryScreenState();
            if (out->step == NATIVE_LEARN_WARN || out->step == NATIVE_LEARN_FORGOT)
                out->forgetSlot = GetMoveSlotToReplace();
            out->ready = out->yesNo || TextPrinter_NativeUiWindowWaiting(RELEARNERWIN_MSG);
        }
        else if (task->func == Task_MoveRelearner_Giveup_Prompt || task->func == Task_MoveRelearner_Giveup_Answer)
        {
            out->phase = NATIVE_RELEARNER_GIVE_UP;
            out->teaching = MOVE_NONE;
            out->yesNo = task->func == Task_MoveRelearner_Giveup_Answer;
            out->ready = out->yesNo;
        }
        else if (task->func == Task_MoveRelearner_Quit)
        {
            out->phase = NATIVE_RELEARNER_CLOSING;
            out->teaching = MOVE_NONE;
        }
        else
        {
            out->phase = NATIVE_RELEARNER_LOADING;
        }
        if (out->yesNo)
            out->yesNoCursor = Menu_GetCursorPos() != 0;
        // the list, once CB2_InitLearnMove_Basic has built it (its step 3)
        if ((gMain.callback2 == CB2_MoveRelearnerMain || gMain.state > 3) && sMoveRelearnerStruct->numMenuChoices > 0)
        {
            count = GetLevelUpStages(boxMon, stages, caps, &forms);
            out->count = min(sMoveRelearnerStruct->numMenuChoices - 1, NATIVE_RELEARNER_MAX_MOVES);
            for (u32 i = 0; i < out->count; i++)
            {
                struct MoveSwitchSource source;

                out->moves[i].move = sMoveRelearnerStruct->menuItems[i].id;
                GetSourceIn(stages, caps, count, forms, gMoveRelearnerState, out->moves[i].move, &source);
                out->moves[i].source = source.kind;
                out->moves[i].number = source.number;
                out->moves[i].species = source.species;
            }
            out->listCount[gMoveRelearnerState] = out->count;
            out->scroll = sMoveRelearnerScrollState.listOffset;
            out->cursor = min(sMoveRelearnerScrollState.listOffset + sMoveRelearnerScrollState.listRow, out->count);
        }
    }
    // the Pokemon, and what switching each of its moves out would cost
    if (gSpecialVar_0x8004 == PC_MON_CHOSEN)
    {
        BoxMonToMon(boxMon, &out->mon);
        out->isBoxMon = TRUE;
    }
    else
    {
        out->mon = gParties[B_TRAINER_PLAYER][gSpecialVar_0x8004 < PARTY_SIZE ? gSpecialVar_0x8004 : 0];
    }
    for (u32 i = 0; i < MAX_MON_MOVES; i++)
    {
        enum Move move = GetBoxMonData(boxMon, MON_DATA_MOVE1 + i);

        if (move == MOVE_NONE)
            continue;
        out->canComeBack[i] = MoveRelearner_CanSwitchBackIn(boxMon, move);
        out->ppUps[i] = ((GetBoxMonData(boxMon, MON_DATA_PP_BONUSES) >> (2 * i)) & 3) != 0;
        out->hmLocked[i] = CannotForgetMove(move) && !MoveRelearner_CanForgetHiddenMove(boxMon, move);
    }
    return TRUE;
}
#endif // ENABLE_NATIVE_UI
