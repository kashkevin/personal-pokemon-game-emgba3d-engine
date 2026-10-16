#!/usr/bin/env python3
"""List compressed graphics with their uncompressed references for the native
decompression self-test (pokeemerald-mp --selftest-decompress LIST).

The GBA build compresses each asset from an uncompressed file:
  build/assets/X.smol / X.fastSmol  <- build/assets/X
  build/assets/X.smolTM             <- X (tilemaps come from the source tree)
                                       or build/assets/X if generated
Usage: make_decompress_list.py [ASSETS_DIR] > list.txt
"""
import os
import sys

assets = sys.argv[1] if len(sys.argv) > 1 else "build/assets"
pairs = []
missing = 0
for root, _dirs, files in os.walk(assets):
    for name in files:
        path = os.path.join(root, name)
        for suffix in (".smol", ".fastSmol", ".smolTM"):
            if name.endswith(suffix):
                base = path[: -len(suffix)]
                candidates = [base]
                if suffix == ".smolTM":
                    candidates.append(os.path.relpath(base, assets))
                ref = next((c for c in candidates if os.path.isfile(c)), None)
                if ref is None:
                    missing += 1
                else:
                    pairs.append((path, ref))
                break
for compressed, ref in sorted(pairs):
    print(compressed, ref)
print(f"{len(pairs)} pairs, {missing} without a reference", file=sys.stderr)
