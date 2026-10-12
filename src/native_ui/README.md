# Native PC UI

The approved Gen 4/5-style interface (the UI art direction's preview, `artifacts/ui-art-direction/preview`)
drawn natively by the game at the window's resolution, in place of the GBA menus' 240×160 picture.
Built only with the 3D renderer (`Makefile_pc DIORAMA=1` defines `ENABLE_NATIVE_UI`); the GBA ROM and
the classic 2D build compile none of it.

**The engine keeps every rule.** Every replaced screen's engine code still runs underneath: its state
machine, its cursor, what each button does, saving, multiplayer. The UI reads that state at the frame
boundary, draws the design, hides the GBA picture of the screen and turns mouse clicks into the
engine's own button presses. It never decides anything the engine decides.

## How a frame goes

| When | Where | What |
| --- | --- | --- |
| frame boundary, game parked | `NativeUi_CaptureFrame` (`sdl2.c`, before `VDraw`) | the active screen is detected and its state copied (`screens/*`, through `include/native_ui/ui_engine.h`); the picture policy is set |
| `VDraw` | `gba_easy_draw.c` | the GBA picture without the hidden background layers (`gPlatformPictureHideBgMask`) and OAM entries (`gPlatformPictureHideObj`, a screen's `hideObjects`) |
| frame boundary | `NativeUi_FilterKeys` | the buttons reaching the game: a screen may take some (its own rows), queued mouse presses land one edge at a time when the screen reads buttons (`ready`) |
| present | `gl_compositor.c` | the 3D field; the GBA picture unless the policy hides it (`DioramaGL_SetPictureFilter`) |
| present, overlay | `NativeUi_Draw` (then the F10 menu) | the screen, at the window's resolution |

## Files

| File | Role |
| --- | --- |
| `ui_kit.c` | the design's pixel-art kit (port of `preview/js/core.js`): colours, ramps, masks, patterns, the palette bevel `paint`, label faces |
| `ui_draw.c` | the drawing context: 640×360 logical canvas at the largest whole-number scale (×2 720p, ×3 1080p, ×4 1440p, ×6 4K; Small one lower, never below ×2), growing on the long axis for other shapes; quads on the logical grid, one buffer and a few draw calls a frame, shapes rasterised once into an atlas (2D array texture, LRU layers), engine fonts in an index texture, patterns in the shader; a horizontal squash of the quads drawn since a mark (card flips) |
| `ui_text.c` | the engine's own fonts drawing engine strings (no conversion), Route Caps 5×7 and Micro 3×5 labels |
| `ui_widgets.c` | the design's shared widgets (port of `preview/js/widgets.js`, pixel art at whole-number scales); prompts show the game's real keys and XInput buttons |
| `ui_gfx.c` | the design's menu icons (embedded) and the game's own art: party icons, front pictures (with the species' own front animation, at whole-number scales, faded), trainer front pictures and item icons, the compressed ones decoded at the frame boundary (`UiGfx_Prepare*` in a screen's capture); field sprites of any size with any 16-colour palette (a recoloured trainer, a doll); CSS-style filters |
| `ui_title_bg.c` | the Emerald title remaster (versions A/B/C; the game shows C: the intro, then B's loop), one shader pass at the logical size |
| `ui_bench.c` | `PKMN_NATIVE_UI_BENCH`: the frame-time benchmark (UI on against off) |
| `native_ui.c` | the controller: screen registry, capture, picture policy, press queue, mouse, PC keys that are not game buttons (Tab and the pad's Y: a screen's `pcKey`), typing for a screen that takes it (`textMode`: letters type instead of pressing the game's buttons), fades, switches, timing log |
| `screens/ui_title.c` | 21 title: remaster background, EMERALD wordmark, PRESS START |
| `screens/ui_main_menu.c` | 21 main menu: the engine's rows, CONTINUE with the save's summary and party, QUIT GAME (the PC's own row: asks, then closes the window), how the window was opened |
| `screens/ui_pause.c` | 4 pause menu (the engine's entries, the player submenu, Safari balls) and 10 save (the journal; "saved" only once the engine reports success **and** the save file is written) |
| `screens/ui_options.c` | 11 options: the engine's six options; the second tab opens the PC's F10 menu |
| `screens/ui_party.c` | 1 party: the engine's party menu in every mode the single and double layouts use (field, items, TMs, giving, battle, choosing for a challenge); the grid's directions and clicks become the engine's cursor presses; SELECT moves a Pokémon; the multi battles' layouts keep the game's screen |
| `screens/ui_bag.c` | 2 bag: the engine's bag in every place it opens (field, battle, giving, selling, depositing, berries): pockets, the list with the game's CLOSE BAG row, descriptions and TM move data, the item menu as the game lays it out, quantities, YES/NO, messages, moving (SELECT) and the game's own sort (START); FIND is marked planned, favourites are left out (not in the game); Tab, the pad's Y or the Details prompt opens the item details |
| `screens/ui_item_detail.c` | 16 item details: the display case and index card for the bag's item (the UI's own view; the game has no such screen and gets no buttons while it is up): the game's prices, description, what the item does (from its field, battle and hold data), who holds it and the party's HP, TM moves with the party's real compatibility, a Mega Stone's Mega form from the game's form changes and the Mega Ring; browsing the pocket (arrows, L/R, wheel, the strip); back on the bag at the item shown last (the game's own presses) |
| `screens/ui_customize.c` | 9 character customization: the dressing room over the STYLE editor (`multiplayer_appearance.c`, START > PLAYER > STYLE): the mirror (the player's own field sprite at 3x walking, running, turning, recoloured live with the edited look by the engine's own palette recolour; the front picture, not recoloured), the design's hang tags (the editor's four colour rows behind BODY, HAIR, HAT and TOP; presets, body, the styles, bottoms, shoes, bag and accessories PLANNED: they need layered art), the colour choices with the engine's names and colours, the look's eight bytes as the host receives them and what the host has; SAVE asks "Is this OK?" and B with changes "Discard your changes?" first, then the engine's own SAVE row or B |
| `screens/ui_players.c` | 12 players (and 8 trade): the link device over the host-driven Players menu (`multiplayer_menu.c`, START > PLAYER > PLAYERS): every page the host sends (the players list, a player's actions, records, teams, challenges, the trade pages) with its title, status and up to six rows (rows without an action as information), the engine's cursor, "waiting for the host", B as the host's Back or Close; beside it what this game knows: you (your field sprite with your saved look in the LCD's tones, name, place), the session's connected players from the bridge (slots, where they are), your card's public facts. FRIENDS, ALL, friendship ranks and Lucky Friends PLANNED |
| `screens/ui_trainer_card.c` | 7 Trainer Card: the collectible card over the engine's Trainer Card (`trainer_card.c`), from START > PLAYER and the Frontier Pass: the front with the engine's trainer picture and your walking field sprite (saved look), name, ID No., money, Pokédex caught (and seen), the save's play time with the engine's blinking colon, the profile words, the engine's badge art in sockets and its four stars picking the card colour; the back with the records the engine prints (Hall of Fame debut, link battles with a W/L bar, trades, Pokéblocks and contests with friends, Battle Points), only those it has; the design's LINK, CHAMP, TRADES and HUNTS pages say they are the host's (PLAYERS > Your Trainer Card), FRIENDS PLANNED; the left panes: who is in this world (bridge) and the four star rules with the ones met. The engine's keys (back: A closes, B flips back); click flips, right click closes; the flip squashes the card in whole pixels (`UiG_SquashX`) |
| `screens/ui_shop.c` | 15 Poké Mart: the mart's own look (awning, pegboard, the counter with its sign and the stock on two shelves, luggage-style price tags) over the engine's mart (`shop.c`): the field's BUY / SELL / QUIT with the board FOR SALE and the clerk's line from the field's message box, the buy screen (the list with the engine's prices, POKéNEWS sale prices and SOLD OUT, how many with its price, YES / NO, the clerk's lines, the Premier Ball bonus), money and how many in the Bag; SELL is the bag's selling mode; decoration shops keep the game's picture. Clicks walk the engine's cursor, the wheel scrolls |
| `screens/ui_naming.c` | 29 naming: the name tag (the engine's title, underscore slots, the block cursor, the player's walking sprite, the Pokémon's icon and gender, the Box icon) and the keyboard's three pages in the design's keycaps over the engine's naming screen (`naming_screen.c`): its own grid (eight keys a row, six on OTHERS, blank keys type a space) and its PAGE / BACK / OK column, its cursor, the lower page after a first capital. Typing on the PC keyboard (`textMode` / `text`): each character becomes the engine's presses (page, cursor steps, A), then the cursor waits on OK; Enter is A, Backspace and Esc B, Page Up / Down the pages. Clicks on keys, tabs and PAGE / BACK / OK; the wheel turns pages |
| `screens/ui_starters.c` | 28 starter selection: Birch's open bag (straps, flap with the region tag and page dots, the tray lined in the region's colour, cradles with GRASS / FIRE / WATER tags, the hand and the label, the round window with the Pokémon's own front animation) over the engine's six-page starter choice (`starter_choose.c`); the research card with the engine's name, category, types, Pokédex entry and first ability; the engine's page lines and question with YES / NO. Clicks on the balls, the tag's arrows and dots, YES / NO; the wheel turns pages |
| `screens/ui_evolution.c` | 25 evolution: the night stage (a twinkling starfield, the stepped spotlight and stage rings that turn while it evolves) over the engine's evolution scene (`evolution_scene.c`), which keeps every rule, message and sound: the Pokémon as it is, sparkles circling it, the old form in colour alternating with the new one's pale silhouette as fast as the engine alternates its two sprites (the larger one in front), the new form white in bursts of sparkles with its colours returning as the engine's flash fades, then the new form; B held (or a click, right click) stops an evolution the engine lets stop; a new move's messages and YES / NO are the engine's (clicks, the wheel), its move choice is the summary's (screen 5). Evolutions inside a trade keep the trade's picture |
| `screens/ui_game_corner.c` | 22 Game Corner prizes: casino carpet, the Coin Case counter with reel digits, the money window and the prize board with its chasing marquee over the Mauville Game Corner's four clerks (their scripts: the coin box, a multichoice or YES / NO, the field's message box): the clerk talked to picks the counter (TM, prize, coin); its rows are the engine's multichoice rows (items, dolls and prices read from their text; dolls drawn from their field sprites), the Shiny Charm (the owner's guaranteed prize, `MP_SHINY_CHARM_COINS`, OWNED once had) heads the prize counter as the next clerk's; under a TM the party's real compatibility, at the coin counter the Coin Case's room. Clicks walk the engine's cursor, another clerk's rows and tabs say so |
| `screens/ui_items.c` | the Items group's shared pieces (`ui_items.h`): money, price tags and strings, big Route Caps signs, the money window, the coin |
| `screens/ui_notice.c` | 27 connection notices: the game's multiplayer banner (`multiplayer_world.c`: host notices, wager money, this ROM's hints) as the design's toast over the calm field, in and out with the banner; the game's own banner whenever a script or menu has the screen |
| `screens/ui_battle.c` | 3 battle HUD: the databoxes (what each healthbox shows, noted as the game draws it: a moving HP bar, the level, the status), the foe trainer's party pips, the message plate, the commands on the design's grid (the engine's cursor follows), the moves with type, PP, the game's description and effectiveness mark, the gimmick toggle (Mega Evolution), a double battle's targets, the YES / NO boxes, the last used Ball (R); singles and doubles; the GBA picture keeps the battlers, scenery and move animations, its message box and menus (BG0) and the interface sprites are left out. The Safari Zone, the catching tutorial and the Battle Arena keep the game's picture; ability pop-ups stay the game's; the level-up box and a trainer battle's rewards are screen 26 (`ui_results.c`) |
| `screens/ui_results.c` | 26 battle results, drawn by the battle HUD: the level-up box in the engine's place (its gains, then its totals; the engine's box and banner left out of the picture, BG1 / BG2), and when a trainer battle ends the rewards ledger with the defeated trainer's plate (won: the prize the engine pays, the money before and after, each party Pokémon's level, Exp. bar and the Exp. it gained in this battle, UP, NO EXP., TRADED; lost with money paid: OUT OF POKéMON and the money paid), from the money and Exp. noted during the battle's intro. Link and multiplayer battles show none (their records are the host's) |
| `screens/ui_summary.c` | 5 summary: the clipboard over the engine's summary in all its modes (party, box, learning a move, the move order); the game's four pages (CONTEST MOVES takes the planned RIBBONS tab); the memo as the game writes it; Mega readiness from the game's forms, stones and Key Stone; IVs and EVs only where the game's setting shows them; Lucky from the multiplayer trades |
| `screens/ui_pokedex.c` | 6 Pokédex: the red device over the engine's (classic) Pokédex: the Hoenn and National lists in the game's order, search results, the entry pane (caught: picture, category, types, both units, forms count, entry; seen: silhouette; unseen: ?), the START menu, the entry page's AREA, CRY, SIZE, CANCEL (the game's own AREA, CRY and SIZE pages and its search keep the game's picture); FIND marked planned, the forms catalogue marked pending; mouse goals hold the game's direction (its own scrolling) |
| `screens/ui_storage.c` | 14 PC storage: the storage terminal over the engine's Pokémon Storage System in all its modes (withdraw, deposit, move Pokémon, move items, a script's choice): the box on pixel wallpapers with the design's slide, the party column, the monitor and printout of the Pokémon under the hand, the hand carrying a Pokémon or an item, the game's menus, YES/NO, messages, the box list for JUMP and deposits, markings, item info; mouse goals walk the game's own cursor (it moves, slides the party out and back itself); several Pokémon moved at once keep the game's picture |
| `screens/ui_storage_walls.c` | the storage wallpapers: the design's eight ported line for line, the game's other eight and Walda's in the same manner (no game wallpaper art used) |
| `screens/ui_mon.c` | one Pokémon as the screens show it (`ui_mon.h`): names, types, status, moves and PP, EXP |
| `screens/ui_testcard.c` | the kit's test card (fidelity checks) |
| `data/*` | generated: palette, label faces, icons (`tools/native_ui/gen_ui_data.js`), title layers (`gen_title_assets.py`) — in the UI art-direction workspace |

Engine-side read-only accessors (all `#ifdef ENABLE_NATIVE_UI`, at the end of their files):
`title_screen.c`, `main_menu.c`, `start_menu.c`, `field_screen_effect.c`, `option_menu.c`,
`party_menu.c`, `pokemon_summary_screen.c`, `item_menu.c`, `pokedex.c`, `pokemon_storage_system.c`, `battle_interface.c` (what each healthbox shows,
noted where the game draws it, and the interface sprites' tiles), `battle_controller_player.c` (which choice waits),
`sprite.c` (a sheet tag's tiles), `menu_helpers.c` (the shared YES/NO and message, and whose they are), `text.c` (each window's last string, a copy of it as printed, whether its printer waits
for a button), `window.c` (whether a window is on screen), `pokemon_icon.c`, `multiplayer.c`, `multiplayer_appearance.c` (the STYLE editor's
rows, cursor, look and colours; the player's field sprite with the saved look), `multiplayer_menu.c` (the Players menu: the host's page,
the cursor, waiting), `multiplayer_world.c` (the notice banner up now), `trainer_card.c` (the card's face, what A and B do, its facts, star rules, badge art), `shop.c` (the mart menu, the buy screen's phase, stock, prices, quantity), `naming_screen.c` (the template, the keyboards, the cursor, the page, the name so far), `starter_choose.c` (the phase, the selection, the eighteen starters), `evolution_scene.c` (the scene's phase, the two species, which form is in front, the flash's whiteness, the new move's YES / NO),
`script_menu.c` (a script's multichoice and YES / NO: the list, its rows, the cursor), `battle_script_commands.c` (the level-up box: its page, whose, the stats before and now; a won trainer battle's prize), `battle_main.c` (the battle's intro runs), `coins.c` (the coin box is up), `script.c` and `scrcmd.c` (the field script waits for A or B), `event_object_movement.c` (a field sprite palette by tag),
`platform/sdl2.c` (save file writes, the F10 menu, the XInput pad's Y, which the game does not use).

## Development switches

Read once at start-up; not for players, not in the launcher or `pokeemerald.cfg`.

| Variable | Effect |
| --- | --- |
| `PKMN_NATIVE_UI=0` | the old GBA presentation everywhere (**rollback**) |
| `PKMN_NATIVE_UI_SCREENS=title,main_menu,pause,options` | only these screens replaced |
| `PKMN_NATIVE_UI_TESTCARD=1` | the kit's test card over the game |
| `PKMN_NATIVE_UI_SIZE=small` | UI size Small |
| `PKMN_NATIVE_UI_PROMPTS=pad` | the controller's prompts always (or `keyboard`) |
| `PKMN_NATIVE_UI_LOG=1` | screen changes, clicks, queued presses, the battle HUD's phases and the UI pass's timing (CPU, and GPU by timer query) in the log |
| `PKMN_NATIVE_UI_BENCH=<file.csv>` | the UI's frame-time benchmark (`ui_bench.c`): measures the title screen and the main menu (5 s each), then CONTINUEs and measures the field, the pause menu, the bag and the party (8 s each), pressing the game's own buttons one at a time and waiting for each screen to be up (by the screens' own detection, which also runs with the UI off) before measuring it; appends per-phase frame times (mean, median, 99th percentile, 1 % low FPS), the UI pass's CPU and GPU time and how much of the phase the expected screen was up (`screen_ok_pct`) to the file, then quits. A screen that does not come up ends the visit (`not reached`). Run it with the UI on and with `PKMN_NATIVE_UI=0` to compare (the save should CONTINUE outdoors) |

## Adding a screen

1. A read-only accessor next to the engine state (`include/native_ui/ui_engine.h`), guarded by
   `ENABLE_NATIVE_UI`. Use `gMain.callback2` and task functions, never freed heap pointers. Put it at
   the end of the file; a hook in the middle of a file is followed by `#line <its original next line>`
   (`Alloc` and `errorf` put file:line strings in the ROM): the GBA ROM must stay byte-identical
   (`tools/pc_port/compare_gba_rom.py` names what moved).
2. `screens/ui_<name>.c`: `detect`, `capture` (copy everything `draw` needs), `draw` (a line-by-line
   port of the preview's screen with the real data), `click` (queue the engine's presses), `ready`
   (the engine reads buttons now), `picture` (`UI_PICTURE_HIDE_ALL` for full screens, `HIDE_BG0` for
   field menus). Register it in `native_ui.c`.
3. Screen files use `_("...")` strings: they must not include SDL's headers (its `'\\'` key literal
   confuses `tools/preproc`); `native_ui.c` offers what they need.
4. Real data only: a field the game does not have is left out or shown as unavailable; a feature it
   does not have is marked PLANNED and never pretends to work.
5. A cursor the design moves differently from the engine (a grid over the engine's list) stays the
   engine's: the screen's `filterKeys` takes the directions, moves its own cursor and queues the
   engine's presses that take the engine's cursor to the same place (`ui_party.c`), so every rule
   stays the engine's.
6. A view the game does not have (the item details) is the UI's own: it reads the screen's capture,
   its `filterKeys` keeps every button from the game while it is up, and it never queues a press
   except to put the engine's cursor where the view left off.

## Testing (tools in the UI art-direction workspace, `tools/native_ui/`)

| Tool | What it checks |
| --- | --- |
| `capture_testcard.js` + `native_shot.sh` + `compare_testcard.py` | the test card against the preview's own rendering, pixel for pixel, at any window size and UI size |
| `hit_test.sh` | mouse regions at every scale |
| `native_run.sh` | a scripted run (keys, clicks, captures) under a virtual display |

Software OpenGL in the cloud gives pictures and behaviour, not timings: frame-time numbers come only
from a real graphics card.
