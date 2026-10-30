# The 3DS project's trees for the Diorama renderer

An alternative to the Diorama's voxel trees: the General tileset's trees (and
Dewford Town's small one) drawn as the 3DS project draws them in its voxel
overworld - the trunk and its ground laid flat on the cells, and the crown a cut-out
standing on it, tilted 50 degrees with its high end to the north - from that
project's own tree art. Turned on with F8 in the game (kept in `pokeemerald.cfg`) or
`PKMN_DIORAMA_TREES=3ds` for one run (see
`src/diorama/gl/gl_tree_sprites.c` and `src/diorama/scene/tree_sprites.c`).

Since step 16 (the world geometry overhaul, 2026-10-30) each crown stands as a solid
of voxels made from this art where the cut-out stood - thickest where it is furthest
inside the outline, the art on its front and back - with a trunk of bark under it
(`gl_tree_sprites.c` BuildModels, from the packed texture when the game starts);
`PKMN_DIORAMA_TREE_CARDS=1` draws the cut-outs instead, for comparisons.

During a `DIORAMA=1` build `diorama_rules.mk` runs

    python3 tools/diorama_trees_3ds/pack_trees.py \
        --source src/data/diorama/trees_3ds.generated.c \
        --header include/diorama/trees_3ds.generated.h

which packs `art/*.png` into one 128x128 RGBA texture (layout in `pack_trees.py`),
with the large crown again at 0,64 in the colours of the Fallarbor tileset's
ash-covered trees (step 16: Routes 113 and 114 and Fallarbor Town stand theirs with it).
The Lavaridge tileset's trees, the General tileset's drawn on the volcano's rock
(Route 112, Jagged Pass, Lavaridge Town), stand with the large and small crowns
as they are, without the trunk's grass art under them.
It needs Pillow, as the building generator does.

## Provenance

Copied from **ZallaxDev/pokeemerald-3Ds-dualscreen** at commit `4a699e3`:
`3ds_port/assets/voxel/trees/*.png` and that folder's `README.md` (now `art/README.md`,
unchanged: "Original art for the voxel mode's trees, drawn for Pokémon Emerald 3Ds
Dual Screen"). `pack_trees.py` is a port of `3ds_port/scripts/gen_voxel_trees.py`;
the placement (`voxel_tree.c`: `VoxelTree_Part`, `VoxelTree_GroundMetatile`,
`EmitCell`, `EmitSmallCell`) is ported in `src/diorama/scene/tree_sprites.c` and
`src/diorama/gl/gl_tree_sprites.c`.

Licence: MIT, "Copyright (c) 2026 ZallaxDev and Pokémon Emerald 3Ds Dual Screen
contributors" (that repository's `LICENSE-PORT.md`, which covers the contents of
`3ds_port/` it authored, not Pokémon Emerald, the pret decompilation or data
generated from a ROM). Keep this notice with these files.

## Changes

* RGBA8 rows from the top for OpenGL instead of PICA200-ordered RGBA5551; the same
  one-bit alpha, and the same 5 bits a channel (expanded to 8 as the GPU shows them).
* The crowns brightened 1.12 times when packed, as `ctr_voxel.c` `BrightenCrowns`
  brightens them when the texture loads (the trunks are not: their ground must match
  the grass).
* C source generated at build time instead of a RomFS file.
