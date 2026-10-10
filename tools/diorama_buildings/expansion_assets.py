"""pokeemerald-expansion asset formats for the building generators.

The 3DS project's generators read pret's layout: tiles.4bpp beside tiles.png and
binary palettes/NN.gbapal. pokeemerald-expansion writes the 4bpp data under
build/assets and keeps palettes as JASC NN.pal. These helpers give the
generators the same bytes from either layout (the conversions are the Diorama
rule compiler's, tools/diorama_rules/emerald_compositor.py)."""

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "diorama_rules"))
from emerald_compositor import _palette_bytes, generated_4bpp  # noqa: E402


def tiles_4bpp(base):
    """The tileset's 4bpp tile bytes (b"" if it has none)."""
    path = os.path.join(base, "tiles.4bpp")
    if not os.path.exists(path):
        path = str(generated_4bpp(os.path.join(base, "tiles.png")))
    return open(path, "rb").read() if os.path.exists(path) else b""


def palette_words(base, index):
    """Sixteen BGR555 words of palette NN, or None if the tileset has none."""
    folder = os.path.join(base, "palettes")
    if not (os.path.exists(os.path.join(folder, "%02d.gbapal" % index))
            or os.path.exists(os.path.join(folder, "%02d.pal" % index))):
        return None
    raw = _palette_bytes(__import__("pathlib").Path(folder), index)[1]
    return struct.unpack("<16H", raw[:32])


def emerald_layouts(root):
    """layouts.json's layouts without pokeemerald-expansion's FireRed/LeafGreen
    ones. Those come after Emerald's, so every Emerald layout keeps its index
    (and its layout id, index + 1)."""
    import json
    layouts = json.load(open(os.path.join(root, "data", "layouts", "layouts.json"),
                             encoding="utf-8"))["layouts"]
    return [e for e in layouts if e.get("layout_version", "emerald") != "frlg"]
