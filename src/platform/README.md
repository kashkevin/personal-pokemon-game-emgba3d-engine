# Native PC platform layer

These files let the game run as a native executable (no GBA emulator). They are
built only by `Makefile_pc`; the GBA `Makefile` skips `src/platform/`. Game code
selects PC behaviour with `#ifdef PORTABLE`.

## Provenance

Imported from **ZallaxDev/pokeemerald-voxel-project** at commit
`cfb429bd145e81dbeab822f30b91a4e4f009a7d2` (2026-08-13). That project descends from
Kurausukun/pokeemerald `pc_port` (merged with pret in August 2025, `2eca86d50`) by
way of gradenGnostic/pokeemerald-multiplatform and the KamiKitsune420
accessibility fork. Its `LICENSE` is MIT for the multiplatform-port modifications
("Copyright (c) 2026 pokeemerald-multiplatform contributors") and explicitly does
not cover the pret decompilation or any Pokémon game material.

| File | Origin | Notes |
| --- | --- | --- |
| `sdl2.c` | voxel project | SDL2 window, timing, input, audio output. Modified here: accessibility (NVDA/footstep) hooks and SDL_image removed; atomic save persistence; `--save`; key register feed; Ctrl+R sends the A+B+START+SELECT chord; RTC moved to `siirtc_pc.c`; host entropy for RNG seeding; in-process soft reset; `Platform_SpinWaitForVBlank`; `--bridge` and the multiplayer bridge's frame hook and reset-chord guard. |
| `gba_easy_draw.c`, `gba_fast_draw.c` | voxel project (NT_x86 renderer, 2024) | GBA PPU emulation from VRAM/OAM/palette/registers. Both targets use "easy draw"; "fast draw" lacks 256-colour text backgrounds. |
| `dma.c` | voxel project | DMA emulation incl. HBlank DMA. |
| `bios.c` | voxel project | BIOS calls. **Its header says the implementations are "based on the VBA-M source code" (VBA-M is GPL-2.0-or-later); `ArcTan`, `ArcTan2` and `Sqrt` are "taken from mGBA's source" (MPL-2.0).** See Licensing below. |
| `cgb_audio.c`, `music_player.c`, `sound_mixer.c`, `m4a_pc.c`, `m4a_tables_pc.c` | voxel project (Kurausukun 2021 C port of the m4a engine) | Replaces `src/m4a.c`, `src/m4a_tables.c` and `src/m4a_1.s` natively. Track arrays moved from `sound/music_player_table.inc` to C. |
| `include/platform.h`, `include/platform/*.h`, `include/music_player.h`, `include/sound_mixer.h`, `include/cgb_audio.h`, `include/cgb_tables.h`, `include/mp2k_common.h`, `include/gba/m4a_internal_pc.h` | voxel project | Platform and PC m4a headers. |
| `stub.c` | rewritten here from the voxel stub list | Link-cable and GameCube multiboot stand-ins only. |
| `agb_flash_pc.c` | project-owned | Replaces the four GBA flash chip drivers; the save image stays in GBA flash format. |
| `siirtc_pc.c` | project-owned | Replaces `src/siirtc.c` with the host clock. |
| `gba_support.c` | project-owned | `Div`, `BitUnPack`, ROM header fields, reset hooks. |
| `fault_compat.c` | project-owned | Gives integer division by zero the GBA (libgcc) results instead of crashing. |
| `null_trap.c` | project-owned | Linux development tool that logs NULL-pointer accesses. |
| `game_memory.c` | project-owned | In-process soft reset: snapshots the game's data/bss sections before the game starts and restores them on `SoftReset`. |
| `crash_report.c` | project-owned | Writes `crash.log` (fault, game callbacks, stack trace) when the game crashes. |
| `mp_bridge.c`, `include/platform/mp_bridge.h` | project-owned | Native multiplayer bridge: a port of the app repository's `bridge/mgba.lua` (and the reset-chord guard of its `native/windows-runtime/sdl-multiplayer.h`) to C, speaking the same loopback protocol to the unchanged companion. Unit tests in `tools/pc_port/tests/test_mp_bridge.c`. |
| `native_selftest.c` | project-owned | `--selftest-decompress LIST`: decodes every compressed asset with the game's decompressor and compares with the GBA build's uncompressed files. |
| `display_settings.c`, `include/platform/display_settings.h` | project-owned | Display and graphics settings in `pokeemerald.cfg` (resolution, windowed/borderless/fullscreen, quality, anti-aliasing) and the sizing rules: the window fitted to the display, the 3D scene's render size at the window's shape, the 240x160 picture's whole-number scale. Unit tests in `tools/pc_port/tests/test_display_settings.c`. |
| `pc_menu.c`, `include/platform/pc_menu.h` | project-owned | The PC settings menu's model (F10): rows, values, the 15-second confirmation after a resolution or display mode change. Drawn by `src/diorama/gl/gl_pc_overlay.c`. |
| `benchmark.c`, `include/platform/benchmark.h` | project-owned | `--benchmark [seconds]`: the 3D view's frame rate at 720p, 1080p, 1440p and 2160p into `benchmark.log`. |
| `battle_sprites.c`, `include/platform/battle_sprites.h` | project-owned | Animated battle sprites: the battlers' pictures (their sprites, a move's copies of them, the copy a move puts on BG1/BG2) drawn from the sprite library's pack in place of the GBA tiles, inside `gba_easy_draw.c`'s picture, with the entries' own position, priority, affine matrix, flips, mosaic, windows, blending and palette fades. Game-side hooks (under `ENABLE_BATTLE_SPRITES`, `#line`-kept GBA line numbers) only record what the battle code loaded. GBA sprites stay for anything the pack does not hold. |
| `battle_sprite_library.c`, `include/platform/battle_sprite_library.h` | sprite library (project-owned; `integration/battle-sprites` of the private sprite library) | The pack loader, unchanged apart from include paths. |
| `bsl_species_map.h` | generated by the sprite library's runtime export | `SPECIES_*` -> library target ids. |
| `include/platform/third_party/stb_image.h` | stb v2.30 (Sean Barrett), public domain / MIT | Only its zlib decoder, compiled static inside `battle_sprite_library.c`. See `include/platform/third_party/README.md`. |

## Build notes

* Soft reset: `Makefile_pc` renames the `.data`/`.bss` of every game object to the
  game sections (`game_data`/`game_bss` on ELF, `.data$game`/`.bss$game` on PE).
  Objects listed in `PRESERVED_OBJS` keep ordinary sections and survive a reset.
  `make -f Makefile_pc check-game-sections` verifies the layout.
* Windows struct layout: MinGW defaults to the MSVC layout (bit-fields of different
  types are not packed together; 8-byte members are 8-aligned). The Windows build
  uses `-mno-ms-bitfields -mno-align-double` so game data matches the GBA build;
  `tools/pc_port/abi_layout.py` checks it. Because SDL2.dll and Windows use the MSVC
  layout, platform code must not exchange structs with bit-fields or 64-bit
  members with them (none of those in use today has any).
* Assembler data: `tools/pc_port/asm_to_x86.sed` converts the GBA build's data
  sources; script data becomes read-only, and on Windows `.rodata` becomes `.rdata`
  so it is linked read-only.
* Busy-wait loops that the GBA's VBlank interrupt would end (for example the
  synchronous map loader waiting on DMA3 requests) call
  `Platform_SpinWaitForVBlank`, which lets the frame end once a caller is clearly
  spinning.

## Licensing

* Port modifications from the voxel lineage: MIT (notice above; keep it with any
  distribution of these files).
* `bios.c`: GPL-2.0-or-later (VBA-M derived) and MPL-2.0 (mGBA derived) material.
  Before the native game is given to anyone, either replace these functions with
  an independent implementation written from the GBATEK specification, or ship
  the corresponding source under those licenses. Tracked in
  `docs/dependency-ledger.md` of the app repository.
* SDL2 (linked at build time, shipped as `SDL2.dll` on Windows): zlib license.
* Game code and assets: unchanged terms of pret/pokeemerald and
  pokeemerald-expansion; no assets are added by this layer.
