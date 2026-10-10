DIORAMA_RULE_JSON := $(sort $(wildcard data/diorama/*.json data/diorama/*/*.json))
DIORAMA_RULE_DIRS := data/diorama data/diorama/maps data/diorama/tilesets data/diorama/buildings
DIORAMA_RULE_CATALOGS := $(sort $(wildcard data/maps/*/map.json)) \
	$(sort $(wildcard data/layouts/*/map.bin)) \
	 $(sort $(wildcard data/tilesets/*/*/metatile_attributes.bin \
	                       data/tilesets/*/*/metatiles.bin \
	                       data/tilesets/*/*/tiles.png \
	                       data/tilesets/*/*/palettes/*.gbapal \
	                       data/tilesets/*/*/palettes/*.pal))
DIORAMA_RULE_COMPILER := tools/diorama_rules/compile_rules.py
DIORAMA_RULE_COMPILER_DEPS := tools/diorama_rules/building_profiles.py \
	tools/diorama_rules/catalog.py \
	tools/diorama_rules/emerald_compositor.py \
	tools/diorama_rules/measured_volumes.py \
	tools/diorama_rules/mountain_art.py \
	tools/diorama_rules/occupancy_model.py \
	tools/diorama_rules/pixel_objects.py \
	tools/diorama_rules/profiles.py \
	tools/diorama_rules/structures.py \
	tools/diorama_rules/terrace_topology.py \
	tools/diorama_rules/tile_shape.py
DIORAMA_RULE_SOURCE := src/data/diorama/diorama_rules.generated.c
DIORAMA_RULE_HEADER := include/diorama/rules.generated.h
DIORAMA_TREE_MODEL := tree_model/emerald_tree.vox
DIORAMA_TREE_COMPILER := tools/diorama_tree/compile_vox.py
DIORAMA_TREE_SOURCE := src/data/diorama/tree_model.generated.c
DIORAMA_TREE_HEADER := include/diorama/tree_model.generated.h

AUTO_GEN_TARGETS += $(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER) \
	$(DIORAMA_TREE_SOURCE) $(DIORAMA_TREE_HEADER)

$(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER) &: $(DIORAMA_RULE_JSON) $(DIORAMA_RULE_DIRS) \
 $(DIORAMA_RULE_CATALOGS) $(DIORAMA_RULE_COMPILER) $(DIORAMA_RULE_COMPILER_DEPS) \
	 data/maps/map_groups.json data/layouts/layouts.json include/constants/metatile_behaviors.h \
	 src/data/tilesets/headers.h src/data/tilesets/graphics.h \
	 src/data/tilesets/metatiles.h src/graphics.c src/tileset_anims.c
	python3 $(DIORAMA_RULE_COMPILER)

$(DIORAMA_TREE_SOURCE) $(DIORAMA_TREE_HEADER) &: $(DIORAMA_TREE_MODEL) $(DIORAMA_TREE_COMPILER)
	python3 $(DIORAMA_TREE_COMPILER)

# The 3DS project's tree art (tools/diorama_trees_3ds/README.md), one texture.
DIORAMA_TREES3DS_SOURCE := src/data/diorama/trees_3ds.generated.c
DIORAMA_TREES3DS_HEADER := include/diorama/trees_3ds.generated.h
DIORAMA_TREES3DS_ART := $(sort $(wildcard tools/diorama_trees_3ds/art/*.png))

AUTO_GEN_TARGETS += $(DIORAMA_TREES3DS_SOURCE) $(DIORAMA_TREES3DS_HEADER)

$(DIORAMA_TREES3DS_SOURCE) $(DIORAMA_TREES3DS_HEADER) &: $(DIORAMA_TREES3DS_ART) \
	 tools/diorama_trees_3ds/pack_trees.py
	python3 tools/diorama_trees_3ds/pack_trees.py --source $(DIORAMA_TREES3DS_SOURCE) \
	  --header $(DIORAMA_TREES3DS_HEADER)

# Modelled buildings (ZallaxDev/pokeemerald-3Ds-dualscreen's generator, see
# tools/diorama_buildings/README.md): every building that reproduces its own
# drawing pixel for pixel becomes a textured model; export_gl.py compiles them
# with palette-slot pages for the renderer.
DIORAMA_BUILDINGS_TOOLS := $(sort $(wildcard tools/diorama_buildings/*.py)) \
	tools/diorama_buildings/relief_fixes.json tools/diorama_rules/emerald_compositor.py
DIORAMA_BUILDINGS_BIN := build/diorama/buildings.bin
DIORAMA_BUILDINGS_SOURCE := src/data/diorama/buildings.generated.c

AUTO_GEN_TARGETS += $(DIORAMA_BUILDINGS_SOURCE)

$(DIORAMA_BUILDINGS_BIN): $(DIORAMA_BUILDINGS_TOOLS) $(DIORAMA_RULE_CATALOGS) \
	 data/layouts/layouts.json src/data/tilesets/headers.h src/data/tilesets/metatiles.h \
	 src/data/tilesets/graphics.h
	@mkdir -p $(@D)
	python3 tools/diorama_buildings/gen_voxel_buildings.py --output $@ > $(@D)/buildings.log

$(DIORAMA_BUILDINGS_SOURCE): $(DIORAMA_BUILDINGS_BIN) tools/diorama_buildings/export_gl.py
	python3 tools/diorama_buildings/export_gl.py $< $@
