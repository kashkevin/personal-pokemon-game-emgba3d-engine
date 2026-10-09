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
| `sdl2.c` | voxel project | SDL2 window, timing, input, audio output. Modified here: accessibility (NVDA/footstep) hooks and SDL_image removed; atomic save persistence; `--save`; key register feed; Ctrl+R goes through the game's reset chord; RTC moved to `siirtc_pc.c`; host entropy for RNG seeding. |
| `gba_easy_draw.c`, `gba_fast_draw.c` | voxel project (NT_x86 renderer, 2024) | GBA PPU emulation from VRAM/OAM/palette/registers. |
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
