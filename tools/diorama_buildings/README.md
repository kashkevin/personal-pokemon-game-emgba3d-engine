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
* `auto_buildings.py` (new, project-owned; tests in `test_auto_buildings.py`): every
  building of an outdoor map that no spec models (about seventy in Hoenn: Slateport,
  Mauville, Lilycove, Fortree, Mossdeep, Pacifidlog, Dewford, Fallarbor, Verdanturf, the
  route houses, the Battle Frontier), found from its door and its blocked cells and
  modelled as a `Relief` read off its own drawing: its front the bottom rows of every
  column as tall as its facade (the first long edge over the door, or an override), its
  top the rest, laid back to the north edge of its cells. Each is proved in the GBA
  projection like every other model. `OVERRIDES` there holds what the search cannot see
  (tall buildings, a cliff behind a house, doors that are no building);
  `python3 auto_buildings.py --sheet DIR --modelled build/diorama/buildings.bin` draws
  what it finds to look over (game art: not committed). Sootopolis's houses stand in the
  crater's rock, where the door search runs on into the rock: they are found by their
  roofs instead (`CRATER_HOUSES`: its towers and boxes, with or without a door, 3 x 4
  cells and a tower's tip) and modelled the same way. The depth of a roof the drawing does not show
  (behind it, seen from above) is laid with the middle of its own roof (`roof_band`):
  `voxel_building.py`'s `Relief` takes `fill`, the drawing's rows to repeat there, where
  it repeated the roof's last rows - its eave, which read as a second roof.
* The owner's report of 2026-10-24 (project-owned; tests in `test_auto_buildings.py`
  `GymTests`, `DoorSearchTests`, `SpeckTests`): `gen_voxel_buildings.py` `find_placements`
  looks for a spec's copies wherever any cell of its core is the reference's own metatile
  (`anchored_at`; it only looked where the first cell was, so the gyms Dewford, Fortree and
  Sootopolis paint with their own roof stood flat), and `drop_overlaps` keeps one model per
  cell outdoors. `auto_buildings.py`: General's gym pieces and the Contest Hall's door are
  building cells, a walkable lintel walled in over a door is the building's, the search does
  not run into a spec model's cells, and `OVERRIDES` bounds Lavaridge's gym, Route 110's
  gatehouses, Stern's shipyard and the Fossil Maniac's house. `voxel_building.py`
  `keep_specks`: pin holes the per-pixel ground test punched in a drawing are filled.
* `window_glow.py` (new, project-owned; tests in `test_window_glow.py`): the windows of
  the buildings of outdoor maps, found on their pages (glass-coloured panes in a darker
  frame, on faces that stand upright, never a wall's own colour; and dark blue panes
  solid in a much lighter frame, as Mossdeep's houses and Devon's slits draw them), which
  glow in the evening and at night. `python3 window_glow.py buildings.bin OUT_DIR PAGE...` draws
  pages with what it found marked, to look over (game art: not committed). A change to
  it or to `export_gl.py` compiles the table again without remodelling the buildings.

* `dimensional.py` (new, project-owned; tests in `test_dimensional.py`): the owner's
  choice of 2026-10-12 ("option B": genuinely dimensional, even where the normal
  camera then differs a little from the drawing). A building `auto_buildings.py`
  finds is given a real roof (`ROOFS` there, by map and door): `pitched` - a hipped
  roof with a ridge, laid course by course (the 3DS port's `HipRoof` and `Strip`)
  from a clean swatch of its own drawn roof, appended below its drawing; `flat` -
  its drawing laid flat as before with a parapet round it, still the drawing at 45
  degrees; `dome`, `cone`, `pyramid4`, `pyramid8` - a shell over its plan, its
  drawn roof spread over it; `tent` - a Battle Tent's dome to the ground with its
  entrance standing out; `split` - two buildings drawn as one. The facade stays
  the Relief's and is still proved pixel for pixel; the roof is tagged `~behind`.
  Every building outdoors (spec-modelled ones too) then has its windows - the
  panes `window_glow.py` finds, grown to their frame - and its doors - where the
  door's own opening animation draws (`field_door.c`, `graphics/door_anims`) - set
  back into the wall with reveals and a sill; the proof leaves those rectangles
  out. `voxel_building.py` changes for it: `Relief` takes `lid` (the columns whose
  top the roof replaces) and `ortho_check` takes `ignore` (the detail rectangles).
  `LANDMARKS` in `auto_buildings.py` holds buildings with no door on their own
  cells (Trainer Hill), and `OVERRIDES` bounds the landmarks the door search runs
  on from (the Battle Pyramid, the cable car stations, the Trick House).

The runtime side is `src/diorama/scene/buildings.c` (a port of the 3DS project's
`3ds_port/src/voxel/voxel_building.c`) and `src/diorama/gl/gl_building_renderer.c`.
