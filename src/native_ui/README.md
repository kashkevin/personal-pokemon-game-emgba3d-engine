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
| `VDraw` | `gba_easy_draw.c` | the GBA picture without the hidden background layers (`gPlatformPictureHideBgMask`) |
| frame boundary | `NativeUi_FilterKeys` | the buttons reaching the game: a screen may take some (its own rows), queued mouse presses land one edge at a time when the screen reads buttons (`ready`) |
| present | `gl_compositor.c` | the 3D field; the GBA picture unless the policy hides it (`DioramaGL_SetPictureFilter`) |
| present, overlay | `NativeUi_Draw` (then the F10 menu) | the screen, at the window's resolution |

## Files

| File | Role |
| --- | --- |
| `ui_kit.c` | the design's pixel-art kit (port of `preview/js/core.js`): colours, ramps, masks, patterns, the palette bevel `paint`, label faces |
| `ui_draw.c` | the drawing context: 640×360 logical canvas at the largest whole-number scale (×2 720p, ×3 1080p, ×4 1440p, ×6 4K; Small one lower, never below ×2), growing on the long axis for other shapes; quads on the logical grid, one buffer and a few draw calls a frame, shapes rasterised once into an atlas (2D array texture, LRU layers), engine fonts in an index texture, patterns in the shader |
| `ui_text.c` | the engine's own fonts drawing engine strings (no conversion), Route Caps 5×7 and Micro 3×5 labels |
| `ui_widgets.c` | the design's shared widgets (port of `preview/js/widgets.js`); prompts show the game's real keys and XInput buttons |
| `ui_gfx.c` | the design's menu icons (embedded) and the game's own art: party icons, front pictures (with the species' own front animation) and item icons, the compressed ones decoded at the frame boundary (`UiGfx_Prepare*` in a screen's capture); CSS-style filters |
| `ui_title_bg.c` | the Emerald title remaster (versions A/B/C; the game shows C: the intro, then B's loop), one shader pass at the logical size |
| `native_ui.c` | the controller: screen registry, capture, picture policy, press queue, mouse, fades, switches, timing log |
| `screens/ui_title.c` | 21 title: remaster background, EMERALD wordmark, PRESS START |
| `screens/ui_main_menu.c` | 21 main menu: the engine's rows, CONTINUE with the save's summary and party, QUIT GAME (the PC's own row: asks, then closes the window), how the window was opened |
| `screens/ui_pause.c` | 4 pause menu (the engine's entries, the player submenu, Safari balls) and 10 save (the journal; "saved" only once the engine reports success **and** the save file is written) |
| `screens/ui_options.c` | 11 options: the engine's six options; the second tab opens the PC's F10 menu |
| `screens/ui_party.c` | 1 party: the engine's party menu in every mode the single and double layouts use (field, items, TMs, giving, battle, choosing for a challenge); the grid's directions and clicks become the engine's cursor presses; SELECT moves a Pokémon; the multi battles' layouts keep the game's screen |
| `screens/ui_summary.c` | 5 summary: the clipboard over the engine's summary in all its modes (party, box, learning a move, the move order); the game's four pages (CONTEST MOVES takes the planned RIBBONS tab); the memo as the game writes it; Mega readiness from the game's forms, stones and Key Stone; IVs and EVs only where the game's setting shows them; Lucky from the multiplayer trades |
| `screens/ui_mon.c` | one Pokémon as the screens show it (`ui_mon.h`): names, types, status, moves and PP, EXP |
| `screens/ui_testcard.c` | the kit's test card (fidelity checks) |
| `data/*` | generated: palette, label faces, icons (`tools/native_ui/gen_ui_data.js`), title layers (`gen_title_assets.py`) — in the UI art-direction workspace |

Engine-side read-only accessors (all `#ifdef ENABLE_NATIVE_UI`, at the end of their files):
`title_screen.c`, `main_menu.c`, `start_menu.c`, `field_screen_effect.c`, `option_menu.c`,
`party_menu.c`, `pokemon_summary_screen.c`, `text.c` (each window's last string, a copy of it as printed, whether its printer waits
for a button), `window.c` (whether a window is on screen), `pokemon_icon.c`, `multiplayer.c`,
`platform/sdl2.c` (save file writes, the F10 menu).

## Development switches

Read once at start-up; not for players, not in the launcher or `pokeemerald.cfg`.

| Variable | Effect |
| --- | --- |
| `PKMN_NATIVE_UI=0` | the old GBA presentation everywhere (**rollback**) |
| `PKMN_NATIVE_UI_SCREENS=title,main_menu,pause,options` | only these screens replaced |
| `PKMN_NATIVE_UI_TESTCARD=1` | the kit's test card over the game |
| `PKMN_NATIVE_UI_SIZE=small` | UI size Small |
| `PKMN_NATIVE_UI_PROMPTS=pad` | the controller's prompts always (or `keyboard`) |
| `PKMN_NATIVE_UI_LOG=1` | screen changes, clicks and the UI pass's timing in the log |

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

## Testing (tools in the UI art-direction workspace, `tools/native_ui/`)

| Tool | What it checks |
| --- | --- |
| `capture_testcard.js` + `native_shot.sh` + `compare_testcard.py` | the test card against the preview's own rendering, pixel for pixel, at any window size and UI size |
| `hit_test.sh` | mouse regions at every scale |
| `native_run.sh` | a scripted run (keys, clicks, captures) under a virtual display |

Software OpenGL in the cloud gives pictures and behaviour, not timings: frame-time numbers come only
from a real graphics card.
