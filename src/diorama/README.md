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
| `src/diorama/scene/buildings.c`, `include/diorama/buildings.h` | Port of the 3DS project's `3ds_port/src/voxel/voxel_building.c` (commit `4a699e3`): the VXB7 building table reader, read from memory instead of RomFS, and its front-face query (`CollectFronts`, `FrontCellAt`, `VoxelBuildings_FrontAt` as `DioramaBuildings_FrontAt`). |
| `src/diorama/gl/gl_building_renderer.c`, `include/diorama/gl_building_renderer.h` | New: draws the placed models with OpenGL, coloured from the live BG palette (the placement logic follows the 3DS project's `VoxelBuildings_EmitSome`). |
| `src/diorama/gl/gl_object_renderer.c` (`ModelPush`) | Port of the 3DS project's `ModelPush` (`3ds_port/src/voxel/voxel_entities.c`): a walker's card comes a pixel in front of the furniture it stands before. The upright-card depth (`UprightDepth` in the sprite shader) is new. |
| `src/field_door.c` (`DioramaDoor_Get`), `gl_object_renderer.c` (`FindDoor`, `DrawDoorOpening`, `DoorDusk`) | Port of the 3DS project's open door (`PortDoor_Get` in its `src/field_door.c`; `EmitDoor`, `DoorLight` and `VoxelBuildings_DoorWall` in `3ds_port/src/voxel/`): a black opening on a modelled building's wall widens as the door opens, and a walker going through darkens and is gone. Changes: swinging doors open too, not only sliding ones; the expansion's door sizes (1x1, 1x2, 2x2 left and right); no opening on an unmodelled building. |
| `src/diorama/gl/gl_object_renderer.c` (`DrawCastShadow`) | Port of the 3DS project's `EmitCastShadow` (`voxel_entities.c`): in the sun a walker's shadow is its own picture laid on the ground along the sun, in the 3DS's dark blue at alpha 0.36 scaled by the sun's strength and by the light on the walker. Changes: flat ground (no relief lift per corner); with no sun (indoors, bad weather) the Diorama's round shadow is kept. |
| `src/diorama/gl/gl_grade.c`, `include/diorama/gl_grade.h` | The 3DS project's colour grade (`3ds_port/src/voxel/voxel_grade.c`): saturation 1.30 about the luma, in the 3D view only. The 3DS grades its textures; here the terrain, building and sprite shaders grade each texel before lighting. `PKMN_DIORAMA_GRADE=0` turns it off. |
| `src/diorama/gl/gl_light.c`, `include/diorama/gl_light.h` | Port of the 3DS project's outdoor light (`3ds_port/src/voxel/ctr_voxel.c`: `LightFor`, `FogLight`, `SetGrade`; `voxel.v.pica`; `VoxelWorld_Weather` in `voxel_world.c`; commit `4a699e3`): the tint warm in the sun and cool in shade, picked by a point's brightness (fully the shade's at 0.62 or below), and the distance haze from 1.05x the eye-to-player distance, both as the weather has them (sun, rain, fog, ash and sand, shade; a cave's cold fog). Indoors (a building or a secret base) none of it. The sun dapples (`MakeDapple` with `DappleLattice`, `Quintic`, `DappleNoise`; `DappleUniforms`; `VoxelWorld_Weather`): the same 128x128 noise, laid along the sun, drifting 0.25 tiles a second, anchored to the world across map crossings. Changes: per fragment in the terrain, building and sprite shaders instead of per vertex (a sprite takes the dapple at its feet); the haze colour follows the palette's night tint and fades (the `TreeTint` line); at night (the game's clock; the 3DS has none) dapples at half strength and no rays or dust. `PKMN_DIORAMA_LIGHT=0` turns it off. |
| `src/diorama/gl/gl_air.c`, `include/diorama/gl_air.h` | Port of the 3DS project's light in the air: sunlit dust (`ctr_voxel.c` `DrawMotes`, `MoteRandom`), sun rays (`DrawRays`, `MakeRays`, `RayStrength`) and bloom (`3ds_video.c` `VoxelBloomPrepare`, `VoxelBloomCompose`), at the strengths `LightFor` gives each weather. Changes: the dust fills the scene's own 33x33 cells (83 specks, the 3DS's density) and fades at the box's edge, since this camera can see past the cells into the dark where dust read as stars; the rays are computed per pixel (the 3DS interpolates an 8x5 mesh) and only drawn over the scene, not that dark; the bloom's copy is a `glBlitFramebuffer`. `PKMN_DIORAMA_AIR=0` turns the three off. |
| `src/diorama/gl/gl_shadow.c`, `include/diorama/gl_shadow.h` | New: the sun's depth map. The sun's direction and the 0.70 ambient are the 3DS project's (`voxel_lighting.h`); the 3DS bakes its shadows on the CPU, this draws them on the GPU. |
| `tools/diorama_buildings/**` | The 3DS project's building generator; see its README for the files, licence and changes. |

Licence of the 3DS project's port code: MIT, "Copyright (c) 2026 ZallaxDev and
Pokémon Emerald 3Ds Dual Screen contributors" (its `LICENSE-PORT.md`; it does not
cover Pokémon Emerald or the pret decompilation). Its voxel code credits the MIT
pokeemerald-multiplatform port for the logic it reuses (its `3ds_port/src/voxel/
NOTICE.md`), the notice above. Keep both notices with these files.

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
  visible-spawn updates), `src/fieldmap.c` (map changed), `src/field_door.c`
  (the open door).

## Changes to the voxel project's behaviour

* Scripted scenes (controls locked: a door warp, a cutscene, the nurse healing)
  stay in 3D, as in the 3DS project; the voxel project drew them in 2D. A frame
  the 3D view cannot draw still falls back (`snapshot_exchange.c`,
  `presentation_transition.c`). The camera interpolates in them as in free play.
* Screen fades on the field (a warp's fade out and in) stay in 3D: the scene is
  coloured from the faded palettes. `PKMN_DIORAMA_FADE_2D=1` restores the cut to
  the 2D view for the fade (`scene_snapshot.c`).
* The 3D view is drawn while the player is hidden (gone through a door); the voxel
  project required a visible player (`gl_object_renderer.c`).
* The voxel tree model's baked colours follow the palette's fade and night tint:
  a per-channel line fitted from the trees' BG palette each frame
  (`gl_terrain_renderer.c`, `TreeTint`); by day it is the identity.
