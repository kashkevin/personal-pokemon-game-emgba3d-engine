# Modelled buildings for the Diorama renderer

Buildings, interior furniture, hedges and railings modelled from their own drawing,
as ZallaxDev/pokeemerald-3Ds-dualscreen models them for its voxel overworld. Run by
`diorama_rules.mk` during a `DIORAMA=1` build:

    python3 tools/diorama_buildings/gen_voxel_buildings.py --output build/diorama/buildings.bin
    python3 tools/diorama_buildings/export_gl.py build/diorama/buildings.bin src/data/diorama/buildings.generated.c

`gen_voxel_buildings.py --verify` renders every model in the GBA's projection and
fails unless it reproduces its drawing pixel for pixel; `--preview DIR` also renders
perspective views. Read `gen_voxel_buildings.py`, `voxel_building.py` and
`voxel_building_specs.py` for how the models are made.

## Provenance

Copied from **ZallaxDev/pokeemerald-3Ds-dualscreen** at commit `4a699e3`
(`3ds_port/scripts/` and `3ds_port/assets/voxel/relief_fixes.json`):
`gen_voxel_buildings.py`, `voxel_building.py`, `voxel_building_specs.py`,
`voxel_props.py`, `voxel_art.py`, `voxel_cells.py`, `voxel_relief_fixes.py`,
`dump_region_art.py`, `relief_fixes.json`.

Licence: MIT, "Copyright (c) 2026 ZallaxDev and Pokémon Emerald 3Ds Dual Screen
contributors" (that repository's `LICENSE-PORT.md`, which covers its original port
code and tooling only, not Pokémon Emerald, the pret decompilation or data generated
from a ROM). Keep this notice with these files. The project's own voxel code credits
gradenGnostic/pokeemerald-multiplatform (MIT) for the logic it reuses.

## Changes for this engine

* `expansion_assets.py` (new): pokeemerald-expansion keeps 4bpp tile data under
  `build/assets` and palettes as JASC `.pal`; layouts.json also lists FireRed/LeafGreen
  layouts (after Emerald's, so Emerald layout ids are unchanged). The art readers
  (`dump_region_art.py`, `voxel_art.py`) and every reader of layouts.json use it.
* `voxel_relief_fixes.py`: reads `relief_fixes.json` from this folder.
* `export_gl.py` (new): the 3DS port stores page texels as RGBA5551 in the PICA200's
  tiled order; the PC renderer needs palette slots in plain rows so the game's live
  palettes (fades, day/night tint) colour the buildings exactly as they colour the
  terrain. The rest of `buildings.bin` is kept byte for byte. It also writes the
  windows `window_glow.py` finds.
* `window_glow.py` (new, project-owned; tests in `test_window_glow.py`): the windows of
  the buildings of outdoor maps, found on their pages (glass-coloured panes in a darker
  frame, on faces that stand upright, never a wall's own colour), which glow in the
  evening and at night. `python3 window_glow.py buildings.bin OUT_DIR PAGE...` draws
  pages with what it found marked, to look over (game art: not committed). A change to
  it or to `export_gl.py` compiles the table again without remodelling the buildings.

The runtime side is `src/diorama/scene/buildings.c` (a port of the 3DS project's
`3ds_port/src/voxel/voxel_building.c`) and `src/diorama/gl/gl_building_renderer.c`.
