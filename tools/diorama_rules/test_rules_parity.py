#!/usr/bin/env python3

import subprocess
import tempfile
import unittest
from pathlib import Path

from compile_rules import ARCHETYPES, CLASS_NAMES, TERRAIN_CLASSES, compile_data


ROOT = Path(__file__).resolve().parents[2]
TERRACE_TOPOLOGY = 128   # DIORAMA_MEASURED_TERRACE_TOPOLOGY (include/diorama/rules.h)


def grouped_hull(record, structure_classes):
    """Whether the resolver's structure step makes a record's cell the body of a grouped hull (a 2-wide tree of one to
    three rows, its template's cell: shape extruded, archetype grouped-hull, its body rows tall; rules.c
    ApplyStaticTerrain). The classifier's record keeps the ground it stands on."""
    return (record["structureId"] != 0 and record["ownerKind"] == "template"
            and structure_classes.get(record["structureId"]) == "grouped-hull"
            and record["structureWidth"] == 2 and 1 <= record["structureHeight"] <= 3)


def resolved_height_q16(record, structure_classes):
    """The height the driver prints for a record (its ground when below 0, else its feature), as
    DioramaRules_ResolveCell works it out on one cell (src/diorama/rules.c ApplyStaticTerrain): the terrace
    solver's levels (groundQ16, topQ16) for a record it placed, else the record's heightQ16 (a ledge stands on its
    feature); no feature for measured bands without a structure (DisableMeasuredVolume); a 2-wide grouped hull's
    body rows (1 or 2) for a cell of one. KNOWN_ISSUES MS-170, MS-189: the test compared heightQ16 alone, from
    before the terraces and grouped hulls, and failed on 77,325 rows of 73 layouts (and the grouped hulls' shape and
    archetype on 8,340)."""
    if record["measuredFlags"] & TERRACE_TOPOLOGY:
        ground, feature = record["groundQ16"], record["topQ16"] - record["groundQ16"]
    else:
        ground, feature = min(record["heightQ16"], 0), max(record["heightQ16"], 0)
        if record["shape"] == "ledge":
            ground = feature
    if record["structureId"] == 0:
        if record["measuredBands"]:
            feature = 0
    elif grouped_hull(record, structure_classes):
        feature = 16 * (2 if record["structureHeight"] >= 2 else 1)
    return ground if ground < 0 else feature


def assert_generated_parity(testcase: unittest.TestCase, data=None):
    data = data or compile_data(ROOT)
    with tempfile.TemporaryDirectory() as directory:
        driver = Path(directory) / "rules_parity"
        subprocess.run([
            "gcc", "-m32", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
            "-D", "ENABLE_DIORAMA", "-iquote", str(ROOT / "include"),
            str(ROOT / "tests/diorama/rules_parity_driver.c"),
            str(ROOT / "src/diorama/rules.c"),
            str(ROOT / "src/data/diorama/diorama_rules.generated.c"),
            "-lm", "-o", str(driver),
        ], check=True, cwd=ROOT)
        output = subprocess.run([str(driver)], check=True, capture_output=True,
                                text=True, cwd=ROOT).stdout.splitlines()

    expected = [(layout["id"], record) for layout in data["layouts"]
                for record in layout["terrainRecords"]]
    terrain_output = [line for line in output if not line.startswith(("O\t", "P\t"))]
    testcase.assertEqual(len(terrain_output), len(expected))
    structure_classes = {row["id"]: row["class"] for row in data["structureCandidates"]}
    shape_ids = {name: index for index, name in enumerate(
        ("flat", "extruded", "cliff", "ledge", "stairs", "water", "bridge",
         "billboard", "cutout", "roof", "building-part", "hidden"))}
    archetype_ids = {name: index for index, name in enumerate(ARCHETYPES, 1)}
    terrain_ids = {name: index for index, name in enumerate(TERRAIN_CLASSES, 1)}
    source_kinds = {"pin:": 1, "behavior:": 3, "pattern:": 7,
                    "context:": 8, "animation:": 10}
    for line, (layout_id, record) in zip(terrain_output, expected):
        fields = line.split("\t")
        testcase.assertEqual(len(fields), 41)
        hull = grouped_hull(record, structure_classes)
        source_kind = next((value for prefix, value in source_kinds.items()
                            if record["source"].startswith(prefix)), 6)
        exact = [
            layout_id, record["cellOffset"], record["expectedMetatile"],
            record["mapGroup"], record["mapNumber"], record["class"],
            resolved_height_q16(record, structure_classes), record["artMode"], record["pool"],
            int(record["authored"]), record["source"],
        ]
        testcase.assertEqual(fields[:11], [str(value) for value in exact])
        testcase.assertAlmostEqual(float(fields[11]), record["confidence"], places=6)
        pixel_object = next((obj for obj in data["pixelObjects"]
                             if obj["layoutId"] == layout_id
                             and obj["structureId"] == record["structureId"]
                             and ((obj["mapGroup"], obj["mapNumber"])
                                  == (record["mapGroup"], record["mapNumber"])
                                  or (obj["mapGroup"], obj["mapNumber"]) == (255, 255))), None)
        tail = [
            record["evidenceFlags"], "|".join(record["evidence"]),
            record["ambiguityFlags"], "|".join(record["ambiguity"]),
            ({"replacement": 1, "source-base": 2}[pixel_object["groundMode"]]
             if pixel_object is not None else int(record["propGroundMode"] == "manual")),
            (pixel_object["groundMetatile"] if pixel_object is not None
             else record["propGroundMetatile"]),
            shape_ids["extruded" if hull else record["shape"]],
            archetype_ids["grouped-hull" if hull else record["archetype"]],
            terrain_ids[record["terrainClass"]], source_kind,
            {"x": 0, "z": 1, "cross": 2}[record["axis"]],
            record["cliffEdgeMask"], record["cliffBaseMask"],
            record["cliffTransitionMask"], record["cliffCornerMask"],
            record["claimOwner"], record["regionId"], record["structureId"],
            record["structureTemplateId"], record["structureX"], record["structureY"],
            record["structureWidth"], record["structureHeight"],
            record["structureLocalX"], record["structureLocalY"],
            {"none": 0, "template": 1, "authored-special": 2,
             "prop-candidate": 3, "generic-volume": 4, "region": 5}[record["ownerKind"]],
            int(record["doorFold"]),
            {"none": 0, "transparent": 1, "black": 2}[record["voidKind"]],
            record["structurePriority"],
        ]
        testcase.assertEqual(fields[12:], [str(value) for value in tail])

    generated = iter(output[len(expected):])
    pool_ids = {row["id"]: index for index, row in enumerate(data["pools"], 1)}
    class_ids = {name: index for index, name in enumerate(CLASS_NAMES, 1)}
    for obj in data["pixelObjects"]:
        fields = next(generated).split("\t")
        testcase.assertEqual(fields[:20], [str(value) for value in [
            "O", obj["id"], obj["structureId"], obj["supportStructureId"],
            obj["layoutId"], obj["groundMetatile"], obj["spriteDepthBiasMillionths"],
            obj["mapGroup"], obj["mapNumber"], {"cutout": 1, "relief": 2}[obj["kind"]],
            pool_ids[obj["pool"]], class_ids[obj["class"]], obj["componentCount"],
            obj["width"], obj["height"],
            {"replacement": 1, "source-base": 2}[obj["groundMode"]],
            obj["supportOffsetQ16"], len(obj["pixels"]),
            sum(len(previous["maskRows"]) for previous in data["pixelObjects"]
                if previous["id"] < obj["id"]), len(obj["maskRows"]),
        ]])
        testcase.assertEqual(fields[20:], [format(value, "x") for value in obj["maskRows"]])
        for index, pixel in enumerate(obj["pixels"]):
            testcase.assertEqual(next(generated).split("\t"), [str(value) for value in [
                "P", obj["id"], index, pixel["sourceCellOffset"],
                pixel["expectedMetatile"], pixel["expectedTileEntry"], pixel["sourceTile"],
                pixel["xQ32"], pixel["yQ32"], pixel["zQ32"], pixel["sizeXQ32"],
                pixel["sizeYQ32"], pixel["sizeZQ32"], pixel["sourceLayer"],
                pixel["sourceSubtile"], pixel["sourcePalette"], pixel["sourceColor"],
                pixel["sourceU"], pixel["sourceV"], pixel["sourceX"], pixel["sourceY"],
                pixel["component"],
            ]])
    testcase.assertEqual(list(generated), [])


class GeneratedRulesParityTests(unittest.TestCase):
    def test_every_compiled_classifier_record_matches_c(self):
        assert_generated_parity(self)


if __name__ == "__main__":
    unittest.main()
