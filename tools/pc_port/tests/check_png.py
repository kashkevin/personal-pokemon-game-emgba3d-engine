#!/usr/bin/env python3
"""Decodes the PNGs test_png_write.c wrote (png-<pattern>-<w>x<h>[-flip].png in
the folder given) with Python's own zlib and checks every pixel against the
pattern the name gives: an independent decoder for the picture writer,
src/platform/png_write.c."""
import pathlib
import re
import struct
import sys
import zlib

NAME = re.compile(r"png-(gradient|noise|flat)-(\d+)x(\d+)(-flip)?\.png$")


def pattern(kind: str, width: int, height: int) -> list:
    rows = []
    state = 2463534242
    for y in range(height):
        row = []
        for x in range(width):
            if kind == "gradient":
                row.append(((x * 7 + y * 3) & 255, (x * 5 + y * 11) & 255, (x ^ y) & 255))
            elif kind == "noise":
                state ^= (state << 13) & 0xFFFFFFFF
                state ^= state >> 17
                state ^= (state << 5) & 0xFFFFFFFF
                row.append((state & 255, (state >> 8) & 255, (state >> 16) & 255))
            else:
                row.append((40, 120, 200))
        rows.append(row)
    return rows


def check(path: pathlib.Path) -> str:
    match = NAME.search(path.name)
    kind, width, height, flip = match.group(1), int(match.group(2)), int(match.group(3)), match.group(4)
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return "not a PNG"
    at = 8
    idat = b""
    header = None
    while at < len(data):
        length, chunk = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        (crc,) = struct.unpack(">I", data[at + 8 + length:at + 12 + length])
        if zlib.crc32(chunk + body) & 0xFFFFFFFF != crc:
            return f"bad CRC in {chunk!r}"
        if chunk == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif chunk == b"IDAT":
            idat += body
        at += 12 + length
    if header != (width, height, 8, 2, 0, 0, 0):
        return f"header {header}"
    raw = zlib.decompress(idat)
    stride = width * 3 + 1
    if len(raw) != stride * height:
        return "wrong image size"
    expected = pattern(kind, width, height)
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        if row[0] != 0:
            return "unexpected filter"
        want = expected[height - 1 - y] if flip else expected[y]
        if bytes(v for pixel in want for v in pixel) != row[1:]:
            return f"row {y} differs"
    return ""


def main(folder: str) -> int:
    files = sorted(p for p in pathlib.Path(folder).glob("png-*.png") if NAME.search(p.name))
    if not files:
        print("check_png: no pictures to check")
        return 1
    failed = 0
    for path in files:
        problem = check(path)
        if problem:
            print(f"check_png: {path.name}: {problem}")
            failed += 1
    print(f"check_png: {len(files) - failed} of {len(files)} pictures decoded by zlib, pixels match")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
