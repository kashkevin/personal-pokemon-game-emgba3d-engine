#!/usr/bin/env python3
"""Compile the authored MagicaVoxel tree into color-aware greedy quads.

With it, the tree's coarser models for distant trees (KNOWN_ISSUES MS-244, the owner's decision #58): the voxels
taken in blocks of LOD_BLOCKS voxels a side, standing on the tree's lowest voxel (so a coarser tree stands on the
ground where the tree does) and clipped to the tree's own box (so it is as tall and as wide); a block solid where at
least LOD_FILL of it is, or where its voxels cover LOD_COVER of its face seen from the front or the side (the trunk,
a voxel thick, kept), in its commonest colour; the grid's other offsets the ones whose silhouettes best match the
tree's; then quads as the tree's own, in the tree's own voxel units (gl_terrain_renderer.c draws the trees near the
camera with the whole model and the farther ones, and the sun's shadows, with these)."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


class VoxError(ValueError):
    pass


def read_vox(path: Path) -> tuple[tuple[int, int, int], dict[tuple[int, int, int], int], list[int]]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"VOX ":
        raise VoxError(f"{path}: not a MagicaVoxel file")
    if struct.unpack_from("<I", data, 4)[0] > 150:
        raise VoxError(f"{path}: unsupported VOX version")

    size = None
    voxels = None
    palette = None
    offset = 8
    while offset + 12 <= len(data):
        chunk_id = data[offset:offset + 4]
        content_size, _children_size = struct.unpack_from("<II", data, offset + 4)
        content = offset + 12
        end = content + content_size
        if end > len(data):
            raise VoxError(f"{path}: truncated {chunk_id!r} chunk")
        if chunk_id == b"SIZE":
            if content_size != 12:
                raise VoxError(f"{path}: invalid SIZE chunk")
            size = struct.unpack_from("<III", data, content)
        elif chunk_id == b"XYZI":
            count = struct.unpack_from("<I", data, content)[0]
            if content_size != 4 + count * 4:
                raise VoxError(f"{path}: invalid XYZI chunk")
            voxels = {}
            for index in range(count):
                x, y, z, color = struct.unpack_from("<BBBB", data, content + 4 + index * 4)
                if color == 0:
                    raise VoxError(f"{path}: voxel uses reserved palette index 0")
                voxels[(x, y, z)] = color
        elif chunk_id == b"RGBA":
            if content_size != 1024:
                raise VoxError(f"{path}: invalid RGBA chunk")
            palette = list(struct.unpack_from("<256I", data, content))
        offset = end

    if size is None or voxels is None or palette is None:
        raise VoxError(f"{path}: SIZE, XYZI and RGBA chunks are required")
    for coordinate in voxels:
        if any(coordinate[axis] >= size[axis] for axis in range(3)):
            raise VoxError(f"{path}: voxel {coordinate} exceeds model size {size}")
    return size, voxels, palette


LOD_BLOCKS = (2, 4)  # voxels a side of a coarser model's block
LOD_FILL = 0.5       # the share of a block's voxels that makes it solid...
LOD_COVER = 0.75     # ...or the share of its face they cover, seen from the front or the side


def bounds(voxels: dict[tuple[int, int, int], int]):
    """The tree's own box: its lowest and highest voxels' corners on each axis."""
    low = tuple(min(coordinate[axis] for coordinate in voxels) for axis in range(3))
    high = tuple(max(coordinate[axis] for coordinate in voxels) + 1 for axis in range(3))
    return low, high


def block_range(key: int, block: int, shift: int, low: int, high: int) -> tuple[int, int]:
    """The voxels a block covers on one axis, clipped to the tree's box."""
    start = key * block - shift
    return max(start, low), min(start + block, high)


def downsample(voxels: dict[tuple[int, int, int], int], block: int, shift: tuple[int, int, int],
               box=None):
    """The model in blocks of `block` voxels a side, block k on an axis covering voxels k * block - shift up to the
    next, clipped to the tree's box: a block solid where at least LOD_FILL of its voxels are, or where they cover
    LOD_COVER of its face seen along x or along y (the .vox's horizontal axes), in their commonest colour (the
    lower palette index on a tie). Returns {block: colour}."""
    low, high = box if box is not None else bounds(voxels)
    groups: dict[tuple[int, int, int], list] = {}
    for coordinate, color in voxels.items():
        key = tuple((coordinate[axis] + shift[axis]) // block for axis in range(3))
        groups.setdefault(key, []).append((coordinate, color))
    out = {}
    for key, members in groups.items():
        extent = [hi - lo for lo, hi in (block_range(key[axis], block, shift[axis], low[axis], high[axis])
                                         for axis in range(3))]
        seen_along_x = len({(y, z) for (x, y, z), _ in members}) / (extent[1] * extent[2])
        seen_along_y = len({(x, z) for (x, y, z), _ in members}) / (extent[0] * extent[2])
        if (len(members) >= LOD_FILL * extent[0] * extent[1] * extent[2]
                or max(seen_along_x, seen_along_y) >= LOD_COVER):
            colors: dict[int, int] = {}
            for _, color in members:
                colors[color] = colors.get(color, 0) + 1
            out[key] = min(colors, key=lambda color: (-colors[color], color))
    return out


def block_faces(voxels: dict[tuple[int, int, int], int], block: int, shift: tuple[int, int, int], box=None):
    """A coarser model's quads in the tree's own voxel units, each edge clipped to the tree's box."""
    low, high = box if box is not None else bounds(voxels)
    small = downsample(voxels, block, shift, (low, high))
    grid = tuple((high[axis] + shift[axis] + block - 1) // block for axis in range(3))

    def to_voxels(value: int, axis: int) -> int:
        return min(max(value * block - shift[axis], low[axis]), high[axis])

    faces = []
    for axis, sign, plane, u0, u1, v0, v1, color in greedy_faces(grid, small):
        u_axis, v_axis = (axis + 1) % 3, (axis + 2) % 3
        faces.append((axis, sign, to_voxels(plane, axis), to_voxels(u0, u_axis), to_voxels(u1, u_axis),
                      to_voxels(v0, v_axis), to_voxels(v1, v_axis), color))
    return faces


def filled(voxels: dict[tuple[int, int, int], int], block: int, shift: tuple[int, int, int], box):
    """The voxels a coarser model's blocks fill (clipped to the tree's box)."""
    low, high = box
    cells = set()
    for key in downsample(voxels, block, shift, box):
        ranges = [range(*block_range(key[axis], block, shift[axis], low[axis], high[axis])) for axis in range(3)]
        cells.update((x, y, z) for x in ranges[0] for y in ranges[1] for z in ranges[2])
    return cells


def silhouettes_match(voxels, cells) -> float:
    """How well a coarser model's silhouettes - from the front, the side and above - match the tree's: the mean of
    their intersections over their unions."""
    total = 0.0
    for axis in range(3):
        mine = {tuple(value for other, value in enumerate(cell) if other != axis) for cell in cells}
        tree = {tuple(value for other, value in enumerate(cell) if other != axis) for cell in voxels}
        total += len(mine & tree) / len(mine | tree)
    return total / 3


def lod_shift(voxels: dict[tuple[int, int, int], int], block: int) -> tuple[int, int, int]:
    """The grid for a coarser model: standing on the tree's lowest voxel; across, the offsets whose silhouettes best
    match the tree's (then the fewer quads, then the smaller offsets)."""
    box = bounds(voxels)
    shift_z = -box[0][2] % block
    best = None
    for shift_x in range(block):
        for shift_y in range(block):
            shift = (shift_x, shift_y, shift_z)
            score = (-silhouettes_match(voxels, filled(voxels, block, shift, box)),
                     len(block_faces(voxels, block, shift, box)), shift)
            if best is None or score < best:
                best = score
    return best[2]


def lod_faces(voxels: dict[tuple[int, int, int], int]):
    """Each coarser model's quads in the tree's own voxel units."""
    box = bounds(voxels)
    return [block_faces(voxels, block, lod_shift(voxels, block), box) for block in LOD_BLOCKS]


def greedy_faces(size: tuple[int, int, int], voxels: dict[tuple[int, int, int], int]):
    faces = []
    for axis in range(3):
        u_axis = (axis + 1) % 3
        v_axis = (axis + 2) % 3
        width = size[u_axis]
        height = size[v_axis]
        for plane in range(size[axis] + 1):
            mask = [None] * (width * height)
            for v in range(height):
                for u in range(width):
                    coordinate = [0, 0, 0]
                    coordinate[u_axis] = u
                    coordinate[v_axis] = v
                    before = 0
                    after = 0
                    if plane > 0:
                        coordinate[axis] = plane - 1
                        before = voxels.get(tuple(coordinate), 0)
                    if plane < size[axis]:
                        coordinate[axis] = plane
                        after = voxels.get(tuple(coordinate), 0)
                    if before and not after:
                        mask[v * width + u] = (before, 1)
                    elif after and not before:
                        mask[v * width + u] = (after, -1)

            v = 0
            while v < height:
                u = 0
                while u < width:
                    key = mask[v * width + u]
                    if key is None:
                        u += 1
                        continue
                    run_width = 1
                    while u + run_width < width and mask[v * width + u + run_width] == key:
                        run_width += 1
                    run_height = 1
                    while v + run_height < height and all(
                        mask[(v + run_height) * width + candidate] == key
                        for candidate in range(u, u + run_width)
                    ):
                        run_height += 1
                    for clear_v in range(v, v + run_height):
                        for clear_u in range(u, u + run_width):
                            mask[clear_v * width + clear_u] = None
                    faces.append((axis, key[1], plane, u, u + run_width,
                                  v, v + run_height, key[0]))
                    u += run_width
                v += 1
    return faces


def render_header(size: tuple[int, int, int], voxels: dict[tuple[int, int, int], int]) -> str:
    occupied_min, occupied_max = bounds(voxels)
    return f"""#ifndef GUARD_DIORAMA_TREE_MODEL_GENERATED_H
#define GUARD_DIORAMA_TREE_MODEL_GENERATED_H

#include <stdint.h>

#define DIORAMA_TREE_MODEL_SIZE_X {size[0]}
#define DIORAMA_TREE_MODEL_SIZE_Z {size[1]}
#define DIORAMA_TREE_MODEL_SIZE_Y {size[2]}
#define DIORAMA_TREE_MODEL_MIN_X {occupied_min[0]}
#define DIORAMA_TREE_MODEL_MIN_Z {occupied_min[1]}
#define DIORAMA_TREE_MODEL_MIN_Y {occupied_min[2]}
#define DIORAMA_TREE_MODEL_MAX_X {occupied_max[0]}
#define DIORAMA_TREE_MODEL_MAX_Z {occupied_max[1]}
#define DIORAMA_TREE_MODEL_MAX_Y {occupied_max[2]}

struct DioramaTreeModelFace
{{
    uint8_t axis;
    int8_t sign;
    uint8_t plane;
    uint8_t uMin;
    uint8_t uMax;
    uint8_t vMin;
    uint8_t vMax;
    uint32_t rgba;
}};

extern const struct DioramaTreeModelFace gDioramaTreeModelFaces[];
extern const uint32_t gDioramaTreeModelFaceCount;
extern const uint32_t gDioramaTreeModelVoxelCount;

/* The coarser models (compile_vox.py LOD_BLOCKS): level l's quads are gDioramaTreeModelLodFaces from
 * gDioramaTreeModelLodFirst[l - 1], gDioramaTreeModelLodCount[l - 1] of them, in the whole model's voxel units. */
#define DIORAMA_TREE_MODEL_LODS {len(LOD_BLOCKS)}
extern const struct DioramaTreeModelFace gDioramaTreeModelLodFaces[];
extern const uint32_t gDioramaTreeModelLodFirst[DIORAMA_TREE_MODEL_LODS];
extern const uint32_t gDioramaTreeModelLodCount[DIORAMA_TREE_MODEL_LODS];

#endif
"""


def render_source(faces, palette: list[int], voxel_count: int, lods=()) -> str:
    lines = [
        "#ifdef ENABLE_DIORAMA",
        "#include <stdint.h>",
        '#include "diorama/tree_model.generated.h"',
        "",
        "const struct DioramaTreeModelFace gDioramaTreeModelFaces[] =",
        "{",
    ]
    for axis, sign, plane, u0, u1, v0, v1, color_index in faces:
        rgba = palette[color_index - 1]
        lines.append(
            f"    {{{axis}, {sign}, {plane}, {u0}, {u1}, {v0}, {v1}, "
            f"UINT32_C(0x{rgba:08X})}},"
        )
    lines.extend((
        "};",
        "",
        "const uint32_t gDioramaTreeModelFaceCount =",
        "    sizeof(gDioramaTreeModelFaces) / sizeof(gDioramaTreeModelFaces[0]);",
        f"const uint32_t gDioramaTreeModelVoxelCount = {voxel_count};",
        "",
        "const struct DioramaTreeModelFace gDioramaTreeModelLodFaces[] =",
        "{",
    ))
    first, counts = [], []
    for model in lods:
        first.append(sum(counts))
        counts.append(len(model))
        for axis, sign, plane, u0, u1, v0, v1, color_index in model:
            rgba = palette[color_index - 1]
            lines.append(
                f"    {{{axis}, {sign}, {plane}, {u0}, {u1}, {v0}, {v1}, "
                f"UINT32_C(0x{rgba:08X})}},"
            )
    lines.extend((
        "};",
        "",
        "const uint32_t gDioramaTreeModelLodFirst[DIORAMA_TREE_MODEL_LODS] = {" + ", ".join(map(str, first)) + "};",
        "const uint32_t gDioramaTreeModelLodCount[DIORAMA_TREE_MODEL_LODS] = {" + ", ".join(map(str, counts)) + "};",
        "#endif",
        "",
    ))
    return "\n".join(lines)


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise VoxError(f"{path}: generated file is stale")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--input", type=Path, default=Path("tree_model/emerald_tree.vox"))
    parser.add_argument("--output-c", type=Path,
                        default=Path("src/data/diorama/tree_model.generated.c"))
    parser.add_argument("--output-h", type=Path,
                        default=Path("include/diorama/tree_model.generated.h"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        input_path = args.input if args.input.is_absolute() else root / args.input
        output_c = args.output_c if args.output_c.is_absolute() else root / args.output_c
        output_h = args.output_h if args.output_h.is_absolute() else root / args.output_h
        size, voxels, palette = read_vox(input_path)
        faces = greedy_faces(size, voxels)
        lods = lod_faces(voxels)
        write_or_check(output_c, render_source(faces, palette, len(voxels), lods), args.check)
        write_or_check(output_h, render_header(size, voxels), args.check)
        if not args.check:
            print(f"tree model: {len(voxels)} voxels -> {len(faces)} greedy quads; coarser models "
                  + ", ".join(f"{block}-voxel blocks {len(model)} quads" for block, model in zip(LOD_BLOCKS, lods)))
    except (OSError, struct.error, VoxError) as error:
        print(f"tree model: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
