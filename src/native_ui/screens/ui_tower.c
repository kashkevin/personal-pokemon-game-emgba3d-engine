#ifdef ENABLE_NATIVE_UI
/*
 * Battle Tower: Legends' pane (docs/battle-tower-legends.md in the app
 * repository, section 10). While the host's Players page names a Tower screen
 * (the screen's bytes 238-239: NativeMpMenu towerStyle and towerArg), the link
 * device beside the host's sheet (screens/ui_players.c) shows the Tower from
 * what this game itself knows: its save's Tower record (the climb's floor, the
 * highest floor cleared, the checkpoint, the prizes claimed), the floor table
 * compiled into it (each floor's kind, region, difficulty and its trainers'
 * names, classes and pictures), its Battle Points, its Bag and its party. The
 * screens: home (the next boss), the 100-floor map, a floor's preview (never
 * the team), the shop, the Duo, the records, Hyper Training and the prizes.
 * The host's rows stay the buttons and every rule stays with the host and the
 * engine (multiplayer_menu.c, tower_legends.c). Project-owned.
 */
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "data.h"
#include "item.h"
#include "pokemon.h"
#include "tower_legends.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/species.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_tower.h"
#include "native_ui/ui_widgets.h"

/* the link device's LCD tones (screens/ui_players.c) */
#define LCD_MID UI_HEX(0x9CC47C)
#define LCD_DARK UI_HEX(0x2C4A2A)

#define CLEARED_HEX 0x58B048
#define NEXT_HEX 0xE0B028
#define BOSS_HEX 0xB83838
#define SPECIAL_HEX 0x8058C8
#define OWED_HEX 0xE89030

static const uint32_t sRegionHex[2] = {0xE85848, 0x4C88E0}; /* TOWER_REGION_KANTO, TOWER_REGION_HOENN */
static const u16 sBagItems[4] = {ITEM_BOTTLE_CAP, ITEM_GOLD_BOTTLE_CAP, ITEM_ABILITY_PATCH, ITEM_RARE_CANDY};
static const u8 sMilestoneFloors[TOWER_MILESTONE_COUNT] = {25, 50, 75, 100};

/* the first-clear prizes, by claim bit (TowerLegends_PrizeFloor: 10, 20, 25, ... 100) */
static const u8 sPrize10[] = _("10 Rare Candy, ¥10,000");
static const u8 sPrize20[] = _("5 Bottle Caps, ¥20,000");
static const u8 sPrize25[] = _("A shiny POKéMON");
static const u8 sPrize30[] = _("15 Rare Candy, ¥30,000");
static const u8 sPrize40[] = _("Gold Bottle Cap, ¥40,000");
static const u8 sPrize50[] = _("Perfect-IV POKéMON, ¥50,000");
static const u8 sPrize60[] = _("20 Rare Candy, ¥60,000");
static const u8 sPrize70[] = _("2 Gold Bottle Caps, ¥70,000");
static const u8 sPrize75[] = _("Shiny perfect-IV POKéMON");
static const u8 sPrize80[] = _("Ability Patch, ¥80,000");
static const u8 sPrize90[] = _("Patch, 3 Gold Caps, ¥90,000");
static const u8 sPrize100[] = _("Shiny legend, ¥100,000");
static const u8 *const sPrizes[12] = {sPrize10, sPrize20, sPrize25, sPrize30, sPrize40, sPrize50,
                                      sPrize60, sPrize70, sPrize75, sPrize80, sPrize90, sPrize100};
static const u8 sText_OtherPrize[] = _("Battle Points and money");
static const u8 sText_Duo[] = _("Two players of this world, three POKéMON each, against the floor's two trainers.");
static const u8 sText_DuoInvite[] = _("Choose a player on the list. Each climb and its prizes stay your own.");

struct TowerTrainer
{
    bool8 there;
    u16 pic;
    u8 name[TRAINER_NAME_LENGTH + 1];
    u8 cls[sizeof(gTrainerClasses[0].name)];
};

static struct
{
    u8 style, arg;
    u8 listFirst, listLast;          /* the floors the host's rows list (a floor map), 0: none */
    bool8 listed[TOWER_FLOOR_COUNT + 1];
    u8 progress, highest, checkpoint;
    u16 claims, bp;
    bool8 pending;
    u8 focus;                       /* the floor whose trainers the card shows, 0: none */
    struct TowerTrainer trainers[2]; /* its solo trainer, or the Duo's two */
    u16 bag[ARRAY_COUNT(sBagItems)];
    u8 partyCount;
    u16 species[PARTY_SIZE];
    u32 personality[PARTY_SIZE];
    bool8 egg[PARTY_SIZE];
    u8 trainable[PARTY_SIZE];       /* the stats Hyper Training can still raise */
} sT;

/* ---------------------------------------------------------------- capture */

static u32 NextBoss(u32 from)
{
    for (u32 f = from; f < TOWER_FLOOR_COUNT; f++)
    {
        u8 kind = TowerLegends_Floor(f)->kind;

        if (kind == TOWER_FLOOR_BOSS || kind == TOWER_FLOOR_FINAL)
            return f;
    }
    return TOWER_FLOOR_COUNT;
}

static void CopyEngine(u8 *dst, int size, const u8 *src)
{
    int n = 0;

    for (; n < size - 1 && src[n] != EOS; n++)
        dst[n] = src[n];
    dst[n] = EOS;
}

static void TakeTrainer(struct TowerTrainer *out, u32 floor, u32 which)
{
    const struct Trainer *trainer = TowerLegends_FloorTrainer(floor, which);

    out->there = trainer != NULL;
    if (trainer == NULL)
        return;
    out->pic = trainer->trainerPic;
    CopyEngine(out->name, sizeof(out->name), trainer->trainerName);
    CopyEngine(out->cls, sizeof(out->cls), gTrainerClasses[trainer->trainerClass].name);
    UiGfx_PrepareTrainerPic(out->pic);
}

/* a row that lists a floor starts with its number ("  12+ YOUNGSTER JOEY"): that floor, else 0 */
static u32 RowFloor(const u8 *row)
{
    u32 floor = 0, digits = 0;

    while (*row == ' ')
        row++;
    for (; *row >= '0' && *row <= '9' && digits < 4; row++, digits++)
        floor = floor * 10 + (*row - '0');
    return digits >= 1 && digits <= 3 && (*row == ' ' || *row == '+' || *row == '-' || *row == '>')
         && floor >= 1 && floor <= TOWER_FLOOR_COUNT ? floor : 0;
}

void UiTower_Capture(const struct NativeMpMenu *menu)
{
    u32 style = menu->towerStyle, arg = menu->towerArg;

    sT.style = style;
    sT.arg = arg <= TOWER_FLOOR_COUNT ? arg : 0;
    memset(sT.listed, 0, sizeof(sT.listed));
    sT.listFirst = sT.listLast = 0;
    for (u32 i = 0; style == TOWER_STYLE_MAP && i < menu->count && i < ARRAY_COUNT(menu->rows); i++)
    {
        u32 floor = RowFloor(menu->rows[i]);

        if (floor == 0)
            continue;
        sT.listed[floor] = TRUE;
        if (sT.listFirst == 0 || floor < sT.listFirst)
            sT.listFirst = floor;
        if (floor > sT.listLast)
            sT.listLast = floor;
    }
    sT.progress = TowerLegends_Progress();
    sT.highest = TowerLegends_Highest();
    sT.checkpoint = TowerLegends_Checkpoint();
    sT.claims = TowerLegends_Claims();
    sT.pending = TowerLegends_HasPendingResult();
    sT.bp = gSaveBlock2Ptr->frontier.battlePoints;
    sT.trainers[0].there = sT.trainers[1].there = FALSE;
    sT.focus = 0;
    if (style == TOWER_STYLE_HOME || (style == TOWER_STYLE_PREVIEW && sT.arg == 0))
        sT.focus = NextBoss(sT.progress > TOWER_FLOOR_COUNT ? TOWER_FLOOR_COUNT : sT.progress);
    else if (style == TOWER_STYLE_PREVIEW || style == TOWER_STYLE_DUO)
        sT.focus = sT.arg;
    if (sT.focus != 0 && style == TOWER_STYLE_DUO)
    {
        TakeTrainer(&sT.trainers[0], sT.focus, 1);
        TakeTrainer(&sT.trainers[1], sT.focus, 2);
    }
    else if (sT.focus != 0)
    {
        TakeTrainer(&sT.trainers[0], sT.focus, 0);
    }
    for (u32 i = 0; i < ARRAY_COUNT(sBagItems); i++)
    {
        sT.bag[i] = CountTotalItemQuantityInBag(sBagItems[i]);
        UiGfx_PrepareItemIcon(sBagItems[i]);
    }
    sT.partyCount = 0;
    for (u32 i = 0; style == TOWER_STYLE_TRAINING && i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        u32 n = sT.partyCount, species = GetMonData(mon, MON_DATA_SPECIES);

        if (species == SPECIES_NONE)
            continue;
        sT.species[n] = species;
        sT.personality[n] = GetMonData(mon, MON_DATA_PERSONALITY);
        sT.egg[n] = GetMonData(mon, MON_DATA_IS_EGG);
        sT.trainable[n] = 0;
        for (u32 stat = 0; stat < NUM_STATS; stat++)
            if (!GetMonData(mon, MON_DATA_HYPER_TRAINED_HP + stat) && GetMonData(mon, MON_DATA_HP_IV + stat) < MAX_PER_STAT_IVS)
                sT.trainable[n]++;
        sT.partyCount++;
    }
}

/* ---------------------------------------------------------------- helpers */

static u32 Bits(u32 v)
{
    u32 n = 0;

    for (; v != 0; v &= v - 1)
        n++;
    return n;
}

static int PrizeBit(u32 floor)
{
    for (u32 bit = 0; TowerLegends_PrizeFloor(bit) != 0; bit++)
        if (TowerLegends_PrizeFloor(bit) == floor)
            return bit;
    return -1;
}

static u32 NextMilestone(void)
{
    for (u32 i = 0; i < TOWER_MILESTONE_COUNT; i++)
        if (sMilestoneFloors[i] > sT.highest)
            return sMilestoneFloors[i];
    return 0;
}

static int Caps(int x, int y, const char *s, UiRGBA c, enum UiAlign align)
{
    return UiCaps_Draw(x, y, s, c, UI_NONE, UI_CAPS_PLAIN, align, UI_NONE);
}

/* a capsule label */
static int Chip(int x, int y, const char *label, UiRGBA colour)
{
    struct UiRamp r = UiRampOf(colour, 0.4, 0.25, 0.6);
    int w = UiCaps_Width(label) + 10;

    UiG_Paint(UiM_Capsule(w, 11, true, true), x, y, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(x + (w >> 1), y + 2, label, UI_P_TEXT_W, r.ink, UI_CAPS_SHADOW, UI_ALIGN_CENTER, UI_NONE);
    return x + w;
}

static void Header(int x, int y, int w, const char *left, const char *right)
{
    Caps(x + 8, y + 5, left, UiMix(UI_P_FIELD_INK, UI_P_TEXT_MUTED, 0.3), UI_ALIGN_LEFT);
    if (right != NULL)
        Caps(x + w - 8, y + 5, right, UI_P_TEXT, UI_ALIGN_RIGHT);
    UiG_HLine(x + 6, y + 15, w - 12, UI_P_CREAM_LO);
}

/* one row of a card: a label on the left, its value on the right */
static void Row(int x, int y, int w, const char *label, const char *value)
{
    Caps(x + 8, y, label, UI_P_TEXT_MUTED, UI_ALIGN_LEFT);
    Caps(x + w - 8, y, value, UI_P_TEXT, UI_ALIGN_RIGHT);
}

static void Wrapped(int x, int y, int w, const u8 *text, int maxLines)
{
    const u8 *starts[4];
    int lengths[4];
    int n = UiText_Wrap(text, w, UI_FONT_SMALL, starts, lengths, maxLines < 4 ? maxLines : 4);

    for (int i = 0; i < n; i++)
    {
        u8 line[96];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, x, y + i * 12, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
}

static void Number(int font, int x, int y, const char *ascii, UiRGBA fg, UiRGBA sh, enum UiAlign align)
{
    u8 text[24];

    UiText_FromAscii(text, sizeof(text), ascii);
    UiText_Draw(font, x, y, text, fg, sh, align, 0);
}

/* a trainer's picture on their region's tint; with details, their name, class, region, the floor's level and difficulty */
static void TrainerBlock(int x, int y, const struct TowerTrainer *trainer, u32 floor, bool details)
{
    const struct TowerLegendsFloor *f = TowerLegends_Floor(floor);
    uint32_t hex = sRegionHex[f != NULL && f->region == TOWER_REGION_HOENN];
    struct UiPattern diag = UiPat_Diag(8, 3, 0, 1);
    char caps[16];
    int rx = x + 77;

    UiG_Paint(UiM_Rounded4(70, 70, 2, 2, 2, 2), x, y, UiMix(UI_HEX(hex), UI_HEX(0xFFFFFF), 0.7),
              &(struct UiPaint){UiMix(UI_HEX(hex), UI_P_INK, 0.45), UI_NONE, UI_NONE, 0, 0, &diag,
                                UiMix(UI_HEX(hex), UI_HEX(0xFFFFFF), 0.56)});
    if (trainer->there)
        UiGfx_TrainerPic(trainer->pic, x + 3, y + 3, NULL);
    if (!details || f == NULL || !trainer->there)
        return;
    UiText_Draw(UI_FONT_NORMAL, rx, y - 2, trainer->name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    UiText_Draw(UI_FONT_SMALL, rx, y + 16, trainer->cls, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    snprintf(caps, sizeof(caps), "LV %u", (unsigned)floor);
    Caps(Chip(rx, y + 31, f->region == TOWER_REGION_HOENN ? "HOENN" : "KANTO", UI_HEX(hex)) + 6, y + 33, caps, UI_P_TEXT,
         UI_ALIGN_LEFT);
    for (int i = 0; i < 5; i++)
        Caps(rx + i * 9, y + 47, UI_CAPS_STAR, i < f->difficulty ? UiMix(UI_HEX(NEXT_HEX), UI_P_INK, 0.15) : UI_P_CREAM_LO,
             UI_ALIGN_LEFT);
    if (f->checkpoint)
        Chip(rx, y + 58, "CHECKPOINT", UI_HEX(BOSS_HEX));
}

/* ---------------------------------------------------------------- the LCD: the climb at a glance */

void UiTower_Lcd(int t, int x, int y, int w, int h)
{
    static const char *const labels[3] = {"FLOOR", "BEST", "BP"};
    int colW = w / 3;
    char text[32];

    (void)t;
    Caps(x + 6, y + 5, "BATTLE TOWER", LCD_DARK, UI_ALIGN_LEFT);
    Caps(x + w - 6, y + 5, "LEGENDS", LCD_DARK, UI_ALIGN_RIGHT);
    UiG_HLine(x + 4, y + 14, w - 8, LCD_MID);
    for (int i = 0; i < 3; i++)
    {
        int cx = x + colW * i + colW / 2;

        if (i > 0)
            UiG_VLine(x + colW * i, y + 18, 44, LCD_MID);
        Caps(cx, y + 20, labels[i], LCD_DARK, UI_ALIGN_CENTER);
        if (i == 0 && sT.progress > TOWER_FLOOR_COUNT)
            snprintf(text, sizeof(text), "TOP");
        else if (i == 1 && sT.highest == 0)
            snprintf(text, sizeof(text), "-");
        else
            snprintf(text, sizeof(text), "%u", (unsigned)(i == 0 ? sT.progress : i == 1 ? sT.highest : sT.bp));
        Number(UI_FONT_NORMAL, cx, y + 33, text, LCD_DARK, LCD_MID, UI_ALIGN_CENTER);
    }
    UiG_HLine(x + 4, y + h - 17, w - 8, LCD_MID);
    if (sT.checkpoint != 0)
        snprintf(text, sizeof(text), "CHECKPOINT %u", (unsigned)sT.checkpoint);
    else
        snprintf(text, sizeof(text), "NO CHECKPOINT YET");
    Caps(x + 6, y + h - 12, text, LCD_DARK, UI_ALIGN_LEFT);
    if (sT.pending)
        Caps(x + w - 6, y + h - 12, "RESULT TO SEND", LCD_DARK, UI_ALIGN_RIGHT);
}

/* ---------------------------------------------------------------- the card: one screen per style */

static void Home(int x, int y, int w)
{
    bool top = sT.progress > TOWER_FLOOR_COUNT;
    u32 milestone = NextMilestone();
    char caps[32], value[32];

    snprintf(caps, sizeof(caps), "FLOOR %u", (unsigned)sT.focus);
    Header(x, y, w, top ? "THE TOP" : "NEXT BOSS", caps);
    TrainerBlock(x + 5, y + 21, &sT.trainers[0], sT.focus, true);
    UiG_HLine(x + 6, y + 96, w - 12, UI_P_CREAM_LO);
    if (milestone != 0)
        snprintf(value, sizeof(value), "FLOOR %u", (unsigned)milestone);
    else
        snprintf(value, sizeof(value), "ALL REACHED");
    Row(x, y + 102, w, "NEXT MILESTONE", value);
    snprintf(value, sizeof(value), "%u/12", (unsigned)Bits(sT.claims & 0xFFF));
    Row(x, y + 118, w, "PRIZES CLAIMED", value);
    snprintf(value, sizeof(value), "%u/4", (unsigned)Bits((sT.claims >> TOWER_CLAIM_MON_SHIFT) & 0xF));
    Row(x, y + 134, w, "MILESTONE POK" UI_CAPS_E_ACUTE "MON", value);
    if (sT.pending)
        snprintf(value, sizeof(value), "RESULT TO SEND");
    else if (top)
        snprintf(value, sizeof(value), "ALL 100 CLEARED");
    else
        snprintf(value, sizeof(value), "FLOOR %u NEXT", (unsigned)sT.progress);
    Row(x, y + 150, w, "CLIMB", value);
}

static void Map(int t, int x, int y, int w, int h)
{
    static const char *const legend[4] = {"WON", "NEXT", "BOSS", "SPECIAL"};
    static const uint32_t legendHex[4] = {CLEARED_HEX, NEXT_HEX, BOSS_HEX, SPECIAL_HEX};
    int gx = x + (w - 169) / 2, gy = y + 19, lx = x + 9;
    char caps[24];

    if (sT.listFirst != 0)
        snprintf(caps, sizeof(caps), "LIST %u-%u", (unsigned)sT.listFirst, (unsigned)sT.listLast);
    Header(x, y, w, "FLOORS 1-100", sT.listFirst != 0 ? caps : NULL);
    for (u32 f = 1; f <= TOWER_FLOOR_COUNT; f++)
    {
        const struct TowerLegendsFloor *floor = TowerLegends_Floor(f);
        int cx = gx + (int)((f - 1) % 10) * 17, cy = gy + (int)((f - 1) / 10) * 15, bit = PrizeBit(f);
        bool cleared = f <= sT.highest, next = f == sT.progress;
        UiRGBA fill = next ? UI_HEX(NEXT_HEX) : cleared ? UI_HEX(CLEARED_HEX) : UiMix(UI_P_CREAM_LO, UI_P_CREAM, 0.45);

        UiG_Rect(cx, cy, 16, 14, fill);
        if (floor->kind == TOWER_FLOOR_BOSS || floor->kind == TOWER_FLOOR_FINAL)
            UiG_Rect(cx, cy, 16, 2, UI_HEX(BOSS_HEX));
        else if (floor->kind == TOWER_FLOOR_SPECIAL)
            UiG_Rect(cx, cy, 16, 2, UI_HEX(SPECIAL_HEX));
        snprintf(caps, sizeof(caps), "%u", (unsigned)f);
        Caps(cx + 8, cy + 5, caps, next ? UI_P_INK : cleared ? UI_P_TEXT_W : UI_P_TEXT_MUTED, UI_ALIGN_CENTER);
        if (bit >= 0 && (sT.claims & (1u << bit)))
            UiG_Rect(cx + 13, cy + 11, 3, 3, UiMix(UI_HEX(NEXT_HEX), UI_HEX(0xFFFFFF), 0.3)); /* its prize claimed */
        if (sT.listed[f])
        {
            /* the floors the host's list shows */
            UiG_HLine(cx - 1, cy - 1, 18, UI_P_FIELD_INK);
            UiG_HLine(cx - 1, cy + 14, 18, UI_P_FIELD_INK);
            UiG_VLine(cx - 1, cy - 1, 16, UI_P_FIELD_INK);
            UiG_VLine(cx + 16, cy - 1, 16, UI_P_FIELD_INK);
        }
        if (next && (t / 16) % 2 == 0)
        {
            UiG_HLine(cx, cy, 16, UI_P_CURSOR);
            UiG_HLine(cx, cy + 13, 16, UI_P_CURSOR);
            UiG_VLine(cx, cy, 14, UI_P_CURSOR);
            UiG_VLine(cx + 15, cy, 14, UI_P_CURSOR);
        }
    }
    for (int i = 0; i < 4; i++)
    {
        UiG_Rect(lx, y + h - 12, 7, 7, UI_HEX(legendHex[i]));
        lx = Caps(lx + 10, y + h - 12, legend[i], UI_P_TEXT_MUTED, UI_ALIGN_LEFT) + 7;
    }
}

static void Preview(int x, int y, int w)
{
    u32 floor = sT.focus;
    const struct TowerLegendsFloor *f = TowerLegends_Floor(floor);
    int bit = PrizeBit(floor);
    char caps[24], value[32];

    snprintf(caps, sizeof(caps), "FLOOR %u", (unsigned)floor);
    if (f == NULL)
    {
        Header(x, y, w, "FLOOR", NULL);
        return;
    }
    Header(x, y, w, caps, f->kind == TOWER_FLOOR_FINAL ? "FINAL BOSS" : f->kind == TOWER_FLOOR_BOSS ? "BOSS"
                          : f->kind == TOWER_FLOOR_SPECIAL ? "SPECIAL" : "TRAINER");
    TrainerBlock(x + 5, y + 21, &sT.trainers[0], floor, true);
    UiG_HLine(x + 6, y + 96, w - 12, UI_P_CREAM_LO);
    snprintf(value, sizeof(value), "LV %u", (unsigned)floor);
    Row(x, y + 101, w, "EVERY POK" UI_CAPS_E_ACUTE "MON", value);
    if (floor <= sT.highest)
        snprintf(value, sizeof(value), "CLEARED");
    else if (floor == sT.progress)
        snprintf(value, sizeof(value), "YOUR NEXT FLOOR");
    else
        snprintf(value, sizeof(value), "AFTER FLOOR %u", (unsigned)floor - 1);
    Row(x, y + 115, w, "YOUR CLIMB", value);
    Row(x, y + 129, w, "THEIR TEAM", "SEEN IN BATTLE");
    UiG_HLine(x + 6, y + 141, w - 12, UI_P_CREAM_LO);
    Row(x, y + 146, w, "FIRST-CLEAR PRIZE", bit < 0 ? "" : (sT.claims & (1u << bit)) ? "CLAIMED" : floor <= sT.highest ? "OWED" : "");
    UiText_Draw(UI_FONT_SMALL, x + 8, y + 157, bit >= 0 ? sPrizes[bit] : sText_OtherPrize, UI_P_TEXT, UI_P_TEXT_SH,
                UI_ALIGN_LEFT, 0);
}

static void Shop(int x, int y, int w)
{
    struct UiRamp gold = UiRampOf(UI_HEX(NEXT_HEX), 0.4, 0.25, 0.6);
    char text[24];

    Header(x, y, w, "TOWER SHOP", "BATTLE POINTS");
    UiG_Paint(UiM_Capsule(w - 16, 22, true, true), x + 8, y + 20, gold.base, &(struct UiPaint){gold.ink, gold.hi, gold.lo, 0, 0, NULL, UI_NONE});
    snprintf(text, sizeof(text), "%u BP", (unsigned)sT.bp);
    Number(UI_FONT_NORMAL, x + w / 2, y + 22, text, UI_P_TEXT_W, gold.ink, UI_ALIGN_CENTER);
    Caps(x + 8, y + 49, "IN YOUR BAG", UiMix(UI_P_FIELD_INK, UI_P_TEXT_MUTED, 0.3), UI_ALIGN_LEFT);
    for (u32 i = 0; i < ARRAY_COUNT(sBagItems); i++)
    {
        int ry = y + 60 + (int)i * 27;

        if (i > 0)
            UiG_HLine(x + 8, ry - 2, w - 16, UiMix(UI_P_CREAM, UI_P_CREAM_LO, 0.6));
        UiGfx_ItemIcon(sBagItems[i], x + 10, ry, NULL);
        UiText_Draw(UI_FONT_SMALL, x + 40, ry + 6, GetItemName(sBagItems[i]), UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        snprintf(text, sizeof(text), UI_CAPS_TIMES "%u", (unsigned)sT.bag[i]);
        Caps(x + w - 10, ry + 9, text, UI_P_TEXT, UI_ALIGN_RIGHT);
    }
    Caps(x + w / 2, y + 170, "PRICES ARE ON THE LIST", UI_P_TEXT_MUTED, UI_ALIGN_CENTER);
}

static void Duo(int x, int y, int w)
{
    char caps[24], value[24];

    snprintf(caps, sizeof(caps), "FLOOR %u", (unsigned)sT.focus);
    Header(x, y, w, "DUO", sT.focus != 0 ? caps : NULL);
    Wrapped(x + 8, y + 19, w - 16, sText_Duo, 3);
    if (sT.focus != 0)
    {
        for (int i = 0; i < 2; i++)
        {
            int bx = i == 0 ? x + 14 : x + w - 84;

            TrainerBlock(bx, y + 62, &sT.trainers[i], sT.focus, false);
            if (!sT.trainers[i].there)
                continue;
            UiText_Draw(UI_FONT_SMALL, bx + 35, y + 135, sT.trainers[i].name, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
            UiText_Draw(UI_FONT_SMALL, bx + 35, y + 148, sT.trainers[i].cls, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        }
        Caps(x + w / 2, y + 94, "&", UI_P_TEXT_MUTED, UI_ALIGN_CENTER);
        snprintf(value, sizeof(value), "EVERY POK" UI_CAPS_E_ACUTE "MON LV %u", (unsigned)sT.focus);
        Caps(x + w / 2, y + 166, value, UI_P_TEXT, UI_ALIGN_CENTER);
        return;
    }
    UiG_HLine(x + 6, y + 60, w - 12, UI_P_CREAM_LO);
    if (sT.progress > TOWER_FLOOR_COUNT)
        snprintf(value, sizeof(value), "ALL 100 CLEARED");
    else
        snprintf(value, sizeof(value), "FLOOR %u", (unsigned)sT.progress);
    Row(x, y + 66, w, "YOUR CLIMB", value);
    snprintf(value, sizeof(value), "LV %u", (unsigned)sT.cap);
    Row(x, y + 82, w, "YOUR EXP CAP", value);
    UiG_HLine(x + 6, y + 96, w - 12, UI_P_CREAM_LO);
    Wrapped(x + 8, y + 101, w - 16, sText_DuoInvite, 3);
}

static void Records(int x, int y, int w)
{
    char value[32];

    Header(x, y, w, "RECORDS", NULL);
    if (sT.highest != 0)
        snprintf(value, sizeof(value), "FLOOR %u", (unsigned)sT.highest);
    else
        snprintf(value, sizeof(value), "NONE YET");
    Row(x, y + 22, w, "HIGHEST FLOOR", value);
    if (sT.progress > TOWER_FLOOR_COUNT)
        snprintf(value, sizeof(value), "ALL 100 CLEARED");
    else
        snprintf(value, sizeof(value), "FLOOR %u", (unsigned)sT.progress);
    Row(x, y + 40, w, "CLIMB", value);
    if (sT.checkpoint != 0)
        snprintf(value, sizeof(value), "FLOOR %u", (unsigned)sT.checkpoint);
    else
        snprintf(value, sizeof(value), "NONE YET");
    Row(x, y + 58, w, "CHECKPOINT", value);
    snprintf(value, sizeof(value), "%u/12", (unsigned)Bits(sT.claims & 0xFFF));
    Row(x, y + 76, w, "PRIZES CLAIMED", value);
    snprintf(value, sizeof(value), "%u/4", (unsigned)Bits((sT.claims >> TOWER_CLAIM_MON_SHIFT) & 0xF));
    Row(x, y + 94, w, "MILESTONE POK" UI_CAPS_E_ACUTE "MON", value);
    snprintf(value, sizeof(value), "%u", (unsigned)sT.bp);
    Row(x, y + 112, w, "BATTLE POINTS", value);
    snprintf(value, sizeof(value), "LV %u", (unsigned)sT.cap);
    Row(x, y + 130, w, "EXP CAP", value);
    Row(x, y + 148, w, "LAST RESULT", sT.pending ? "WAITING FOR THE HOST" : "SETTLED");
    UiG_HLine(x + 6, y + 162, w - 12, UI_P_CREAM_LO);
    Caps(x + w / 2, y + 168, "KEPT IN YOUR SAVE", UI_P_TEXT_MUTED, UI_ALIGN_CENTER);
}

static void Training(int t, int x, int y, int w)
{
    char text[24];

    Header(x, y, w, "HYPER TRAINING", NULL);
    for (int i = 0; i < 3; i++)
    {
        int cx = x + 8 + i * ((w - 16) / 3) + (w - 16) / 6;

        UiGfx_ItemIcon(sBagItems[i], cx - 12, y + 19, NULL);
        snprintf(text, sizeof(text), UI_CAPS_TIMES "%u", (unsigned)sT.bag[i]);
        Caps(cx, y + 45, text, sT.bag[i] != 0 ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_ALIGN_CENTER);
    }
    UiG_HLine(x + 6, y + 56, w - 12, UI_P_CREAM_LO);
    if (sT.partyCount == 0)
        return;
    for (int i = 0; i < sT.partyCount; i++)
    {
        int cx = x + 6 + (i % 2) * ((w - 12) / 2), cy = y + 60 + (i / 2) * 38;

        UiGfx_MonIcon(sT.species[i], sT.personality[i], sT.egg[i], (t / 16) % 2, cx, cy, NULL);
        if (sT.egg[i])
            snprintf(text, sizeof(text), "EGG");
        else if (sT.trainable[i] == 0)
            snprintf(text, sizeof(text), "ALL BEST");
        else
            snprintf(text, sizeof(text), "%u TO TRAIN", (unsigned)sT.trainable[i]);
        Caps(cx + 34, cy + 14, text, sT.egg[i] || sT.trainable[i] == 0 ? UI_P_TEXT_MUTED : UI_P_TEXT, UI_ALIGN_LEFT);
    }
}

static void Prizes(int x, int y, int w)
{
    char caps[16];

    snprintf(caps, sizeof(caps), "%u/12", (unsigned)Bits(sT.claims & 0xFFF));
    Header(x, y, w, "FIRST-CLEAR PRIZES", caps);
    for (u32 bit = 0; bit < 12; bit++)
    {
        u32 floor = TowerLegends_PrizeFloor(bit);
        int ry = y + 19 + (int)bit * 13, milestone = -1;
        bool claimed = (sT.claims & (1u << bit)) != 0, owed;
        uint32_t hex;

        for (int m = 0; m < TOWER_MILESTONE_COUNT; m++)
            if (sMilestoneFloors[m] == floor)
                milestone = m;
        /* owed: cleared and not settled yet, or a milestone Pokemon not delivered (no room in the party or the PC) */
        owed = (!claimed && floor <= sT.highest)
            || (claimed && milestone >= 0 && !(sT.claims & (1u << (TOWER_CLAIM_MON_SHIFT + milestone))));
        hex = owed ? OWED_HEX : claimed ? CLEARED_HEX : 0;
        if (hex != 0)
            UiG_Paint(UiM_Circle(7), x + 8, ry + 2, UI_HEX(hex), &(struct UiPaint){UiMix(UI_HEX(hex), UI_P_INK, 0.5), UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        else
            UiG_Paint(UiM_Circle(7), x + 8, ry + 2, UI_P_CREAM, &(struct UiPaint){UI_P_CREAM_LO, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        snprintf(caps, sizeof(caps), "%u", (unsigned)floor);
        Caps(x + 38, ry + 2, caps, claimed || owed ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_ALIGN_RIGHT);
        UiText_Draw(UI_FONT_SMALL, x + 44, ry - 1, sPrizes[bit], claimed || owed ? UI_P_TEXT : UI_P_TEXT_MUTED, UI_P_TEXT_SH,
                    UI_ALIGN_LEFT, 0);
    }
}

void UiTower_Card(int t, int x, int y, int w, int h)
{
    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_CREAM, &(struct UiPaint){UI_P_FIELD_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    switch (sT.style)
    {
    case TOWER_STYLE_MAP:
        Map(t, x, y, w, h);
        break;
    case TOWER_STYLE_PREVIEW:
        Preview(x, y, w);
        break;
    case TOWER_STYLE_SHOP:
        Shop(x, y, w);
        break;
    case TOWER_STYLE_DUO:
        Duo(x, y, w);
        break;
    case TOWER_STYLE_RECORDS:
        Records(x, y, w);
        break;
    case TOWER_STYLE_TRAINING:
        Training(t, x, y, w);
        break;
    case TOWER_STYLE_PRIZES:
        Prizes(x, y, w);
        break;
    default:
        Home(x, y, w);
        break;
    }
}
#endif // ENABLE_NATIVE_UI
