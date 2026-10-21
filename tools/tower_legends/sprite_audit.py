#!/usr/bin/env python3
"""Battle Tower: Legends' sprite audit: every picture the 100 floors show, from the art the game is built from.

For each floor's trainers (the solo trainer and the Duo's two) the trainer's front picture, and for each of their
Pokemon (and the Mega form a held stone turns it into) the front picture, the back picture, the icon and both palettes,
as the engine's own tables name them (src/data/graphics/trainers.h; the species tables under src/data/pokemon/ and
src/data/graphics/pokemon.h, with this build's P_GBA_STYLE_SPECIES_GFX and P_GBA_STYLE_SPECIES_ICONS). A picture fails
when its table entry or its file is missing, when it does not decode, when it is empty (every pixel the transparent
colour) or when it is the placeholder art (graphics/pokemon/question_mark/). Writes OUT/sprite_audit.json and contact
sheets OUT/floors-001-025.png ... (a row a floor: the trainers, then their Pokemon) for a person to look at. Exit 0 when
every picture passed.

    tools/tower_legends/sprite_audit.py [--data tools/tower_legends/tower_legends.json] --out build/tower-sprites
"""
import argparse
import hashlib
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DATA = os.path.join(ROOT, "tools", "tower_legends", "tower_legends.json")
CELL = 64


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as source:
        return source.read()


def config_flag(name):
    match = re.search(rf"#define\s+{name}\s+(TRUE|FALSE)", read("include/config/pokemon.h"))
    return match is not None and match.group(1) == "TRUE"


def graphics_paths(text, gba_style):
    """symbol -> file, for the INCGFX/INCBIN definitions of a graphics table (the branch this build compiles)."""
    out = {}
    for symbol, path in re.findall(r"\b(g\w+)\[\]\s*=\s*INC(?:GFX|BIN)_U(?:8|16|32)\(\s*\"([^\"]+)\"", text):
        if symbol in out and ("_gba" in path) != gba_style:
            continue
        if symbol not in out or ("_gba" in path) == gba_style:
            out[symbol] = path
    return out


def trainer_pictures():
    """TRAINER_PIC_* -> (front picture file, palette file)."""
    text = read("src/data/graphics/trainers.h")
    files = graphics_paths(text, False)
    out = {}
    for pic, front, palette in re.findall(r"\[(TRAINER_PIC_\w+)\]\s*=\s*\{\s*\.frontPic\s*=\s*TRAINER_FRONT_PIC\(\s*(\w+)\s*,\s*(\w+)", text):
        out[pic] = (files.get(front), files.get(palette))
    return out


def expand_macros(body, macros):
    """The species entry with its form macros (VIVILLON_MISC_INFO(IcySnow, ...)) written out."""
    for name, (params, text) in macros.items():
        for call in re.finditer(rf"\b{name}\(([^)]*)\)", body):
            args = [arg.strip() for arg in call.group(1).split(",")]
            expanded = text
            for param, arg in zip(params, args):
                expanded = re.sub(rf"\s*##\s*{param}\b", arg, expanded)
                expanded = re.sub(rf"\b{param}\s*##\s*", arg, expanded)
                expanded = re.sub(rf"\b{param}\b", arg, expanded)
            body += "\n" + expanded
    return body


def species_pictures():
    """SPECIES_* -> {front, back, icon, palette, shiny}: files, from the species tables."""
    gfx, icons = config_flag("P_GBA_STYLE_SPECIES_GFX"), config_flag("P_GBA_STYLE_SPECIES_ICONS")
    table = read("src/data/graphics/pokemon.h")
    files = graphics_paths(table, gfx)
    icon_files = graphics_paths(table, icons)
    out = {}
    folder = os.path.join(ROOT, "src", "data", "pokemon", "species_info")
    for name in sorted(os.listdir(folder)):
        text = read(os.path.join("src", "data", "pokemon", "species_info", name))
        macros = {}
        for macro in re.finditer(r"#define\s+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)", text):
            macros[macro.group(1)] = ([p.strip() for p in macro.group(2).split(",")], macro.group(3).replace("\\\n", "\n"))
        for match in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", text):
            body = expand_macros(text[match.end():text.find("\n    },", match.end())], macros)
            entry = {}
            for key, field in (("front", "frontPic"), ("back", "backPic"), ("icon", "iconSprite"), ("palette", "palette"),
                               ("shiny", "shinyPalette")):
                symbols = re.findall(rf"\.{field}\s*=\s*(g\w+)", body)
                if symbols:
                    entry[key] = (icon_files if key == "icon" else files).get(symbols[0])
            out[match.group(1)] = entry
    return out


def load_picture(path, frame_height=None):
    """The picture as RGBA (the first frame of a 64x128 animation), its pixels' hash, and whether anything is drawn."""
    image = Image.open(os.path.join(ROOT, path))
    transparent = image.info.get("transparency", 0) if image.mode == "P" else None
    if image.mode == "P":
        pixels = image.load()
        drawn = any(pixels[x, y] != transparent for y in range(image.height) for x in range(image.width))
    else:
        drawn = image.getbbox() is not None
    rgba = image.convert("RGBA")
    if image.mode == "P":
        data = rgba.load()
        raw = image.load()
        for y in range(image.height):
            for x in range(image.width):
                if raw[x, y] == transparent:
                    data[x, y] = (0, 0, 0, 0)
    if frame_height and rgba.height > frame_height:
        rgba = rgba.crop((0, 0, rgba.width, frame_height))
    return rgba, hashlib.sha256(rgba.tobytes()).hexdigest(), drawn


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=DATA)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    with open(args.data) as source:
        data = json.load(source)
    trainers, species = trainer_pictures(), species_pictures()
    placeholders = set()
    for path in ("graphics/pokemon/question_mark/circled/anim_front.png", "graphics/pokemon/question_mark/circled/back.png",
                 "graphics/pokemon/question_mark/double/anim_front.png", "graphics/pokemon/question_mark/double/back.png"):
        if os.path.exists(os.path.join(ROOT, path)):
            placeholders.add(load_picture(path, CELL)[1])
    problems, checked, seen = [], 0, {}

    def check(what, path, frame=CELL):
        nonlocal checked
        checked += 1
        if path is None:
            problems.append(f"{what}: no picture in the engine's tables")
            return None
        if path in seen:
            return seen[path]
        if not os.path.exists(os.path.join(ROOT, path)):
            problems.append(f"{what}: {path} is missing")
            seen[path] = None
            return None
        try:
            picture, digest, drawn = load_picture(path, frame)
        except Exception as error:      # a file that does not decode
            problems.append(f"{what}: {path} does not decode ({error})")
            seen[path] = None
            return None
        if not drawn:
            problems.append(f"{what}: {path} is empty (only the transparent colour)")
        if digest in placeholders:
            problems.append(f"{what}: {path} is the placeholder art")
        seen[path] = picture
        return picture

    rows = []
    for floor in data["floors"]:
        n = floor["floor"]
        row = []
        for role, trainer in [("solo", floor["solo"])] + list(zip(("duo A", "duo B"), floor.get("duo", []))):
            front, palette = trainers.get(trainer["pic"], (None, None))
            picture = check(f"floor {n} {role} {trainer['name']} ({trainer['pic']})", front)
            if palette is None or not os.path.exists(os.path.join(ROOT, palette)):
                problems.append(f"floor {n} {role} {trainer['pic']}: no palette")
            if role == "solo":
                row.append(picture)
            for mon in trainer["mons"]:
                for form in [mon["species"]] + ([mon["mega"]] if mon.get("mega") else []):
                    entry = species.get(form, {})
                    what = f"floor {n} {role} {form}"
                    picture = check(what + " front", entry.get("front"))
                    check(what + " back", entry.get("back"))
                    check(what + " icon", entry.get("icon"), 32)
                    for key in ("palette", "shiny"):
                        checked += 1
                        if entry.get(key) is None or not os.path.exists(os.path.join(ROOT, entry[key])):
                            problems.append(f"{what}: no {key} palette")
                    if role == "solo":
                        row.append(picture)
        rows.append((n, row))
    # the contact sheets: 25 floors each, a row a floor
    for first in range(0, len(rows), 25):
        band = rows[first:first + 25]
        width = max(len(row) for _, row in band) * (CELL + 4) + 40
        sheet = Image.new("RGBA", (width, len(band) * (CELL + 4) + 4), (248, 240, 216, 255))
        for i, (n, row) in enumerate(band):
            y = 4 + i * (CELL + 4)
            for j, picture in enumerate(row):
                x = 40 + j * (CELL + 4)
                if picture is None:
                    sheet.paste((232, 56, 32, 255), (x, y, x + CELL, y + CELL))
                else:
                    sheet.alpha_composite(picture, (x, y))
        sheet.save(os.path.join(args.out, f"floors-{band[0][0]:03d}-{band[-1][0]:03d}.png"))
    report = {"hash": data.get("hash"), "pictures_checked": checked, "files": len(seen), "problems": problems,
              "passed": not problems}
    with open(os.path.join(args.out, "sprite_audit.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps({key: report[key] for key in ("pictures_checked", "files", "passed")}), f"{len(problems)} problems")
    for problem in problems[:40]:
        print(" ", problem)
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
