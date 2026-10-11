# Environmental object tools (graphics overhaul, phase 2)

Development tools for the 3D view's world detail objects (`src/diorama/scene/props.c`,
see `src/diorama/README.md`). They read this tree's tilesets and maps; their pictures
are game art and are not committed.

* `metatile_sheet.py PRIMARY_DIR SECONDARY_DIR OUT.png [--secondary-sheet] [--layer upper]`:
  a tileset's metatiles with their ids (the upper layer alone over a checker shows
  what stands on the ground).
* `map_sheet.py LAYOUT OUT.png [--x --y --width --height] [--only IDS]`: a map
  (`LittlerootTown`) drawn with each cell's metatile id, to match what the game shows
  to its metatiles.
* `prop_candidates.py TILESET OUT.png`: the metatiles of a tileset that look like
  standing objects (a partly covered upper layer, used on the maps), with how many
  cells use them and how many of those block the player. A list to look through.
* `inventory.c`: counts the rule compiler's object classes over every map (what the
  rules already know). Built against the generated rules:

  ```
  gcc -m32 -std=gnu99 -O1 -DENABLE_DIORAMA -iquote include tools/diorama_objects/inventory.c \
      src/diorama/rules.c build/pc-tests/diorama/diorama_rules.generated.o -lm -o inventory
  ```

  (`make -f Makefile_pc check-diorama` makes the generated object first.)

Run the Python tools from the repository root with `tools/diorama_objects` on
`PYTHONPATH` (`map_sheet.py` and `prop_candidates.py` import `metatile_sheet.py`).
