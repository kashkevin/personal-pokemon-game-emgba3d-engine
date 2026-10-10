# Diorama 3D overworld renderer

Built only with `make -f Makefile_pc DIORAMA=1` (OpenGL 3.3 core through SDL2). The
GBA build and the default native build do not compile it. Every file is wrapped in
`#ifdef ENABLE_DIORAMA`.

## Provenance

Imported from **ZallaxDev/pokeemerald-voxel-project** at commit
`cfb429bd145e81dbeab822f30b91a4e4f009a7d2` (2026-08-13), unchanged unless listed
below:

| Path here | Notes |
| --- | --- |
| `src/diorama/**`, `include/diorama/**` (except `*.generated.h`) | Renderer: scene snapshot and exchange, metatile atlas, terrain mesh, object/terrain/compositor GL passes, rules runtime. |
| `tools/diorama_rules/*.py`, `tools/diorama_tree/*.py`, `diorama_rules.mk` | Offline compilers for the rule tables and the voxel tree model. **Adapted here** for pokeemerald-expansion (see below). |
| `data/diorama/**` | Rule sources (JSON) read by the compiler. |
| `tree_model/emerald_tree.vox` | Voxel tree model compiled into `src/data/diorama/tree_model.generated.c`. |
| `tests/diorama/*.c` | The project's unit tests for the renderer modules. |

Licence: MIT for the multiplatform-port modifications ("Copyright (c) 2026
pokeemerald-multiplatform contributors"; full text in that repository's `LICENSE`).
It does not cover the pret decompilation or any Pokémon game material. Keep this
notice with any distribution of these files.

### Modelled buildings (ZallaxDev/pokeemerald-3Ds-dualscreen)

| Path here | Notes |
| --- | --- |
| `src/diorama/scene/buildings.c`, `include/diorama/buildings.h` | Port of the 3DS project's `3ds_port/src/voxel/voxel_building.c` (commit `4a699e3`): the VXB7 building table reader, read from memory instead of RomFS. |
| `src/diorama/gl/gl_building_renderer.c`, `include/diorama/gl_building_renderer.h` | New: draws the placed models with OpenGL, coloured from the live BG palette (the placement logic follows the 3DS project's `VoxelBuildings_EmitSome`). |
| `tools/diorama_buildings/**` | The 3DS project's building generator; see its README for the files, licence and changes. |

Licence of the 3DS project's port code: MIT, "Copyright (c) 2026 ZallaxDev and
Pokémon Emerald 3Ds Dual Screen contributors" (its `LICENSE-PORT.md`; it does not
cover Pokémon Emerald or the pret decompilation). Keep that notice with these files.

## Generated files (not in Git)

`src/data/diorama/diorama_rules.generated.c`, `include/diorama/rules.generated.h`,
`src/data/diorama/tree_model.generated.c`, `include/diorama/tree_model.generated.h`
and `src/data/diorama/buildings.generated.c` (via `build/diorama/buildings.bin`)
are produced from this tree's maps and tilesets by `diorama_rules.mk` during a
`DIORAMA=1` build. For this fork they come out byte-identical to the voxel
project's tables (rules SHA-256 `9ef5c6e8...`), because the Emerald maps and
tilesets are unchanged.

## Changes for pokeemerald-expansion

* `tools/diorama_rules/catalog.py`: graphics symbols declared with `INCGFX_*`
  (and `ALIGNED(n)`) as well as `INCBIN_*`; FireRed/LeafGreen tilesets, layouts
  (`layout_version` "frlg") and map groups (`*_Frlg`) are skipped, so the catalog
  is Emerald's 75 tilesets, 441 layouts and 518 maps with unchanged ids.
* `tools/diorama_rules/emerald_compositor.py`: JASC `.pal` palettes (converted the
  way gbagfx does; verified identical for all 2112 tileset palettes); 4bpp data
  generated under `build/assets`; only animation callbacks of the Emerald tilesets.
* Game hooks (all `#ifdef ENABLE_DIORAMA`, GBA line numbers kept): `src/main.c`
  (frame begin/end), `src/overworld.c` (publish the overworld snapshot after
  visible-spawn updates), `src/fieldmap.c` (map changed).
