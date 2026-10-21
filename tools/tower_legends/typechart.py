"""The engine's own type chart (src/data/types_info.h gTypeEffectivenessTable), read as text.

The table's matchup macros (STL_RS and friends) depend on B_UPDATED_TYPE_MATCHUPS (include/config/battle.h), which this
module resolves from the config headers (GEN_LATEST from include/config/general.h).
"""
import os
import re


class TypeChart:
    def __init__(self, engine_root, type_values):
        def read(rel):
            with open(os.path.join(engine_root, rel), encoding="utf-8", errors="replace") as f:
                return f.read()

        general = read("include/config/general.h")
        battle = read("include/config/battle.h")
        table = read("src/data/types_info.h")
        latest = int(re.search(r"#define\s+GEN_LATEST\s+GEN_(\d+)", general).group(1))
        setting = re.search(r"#define\s+B_UPDATED_TYPE_MATCHUPS\s+(\w+)", battle).group(1)
        updated = latest if setting == "GEN_LATEST" else int(re.fullmatch(r"GEN_(\d+)", setting).group(1))
        self.generation = updated

        macros = {"______": 1.0}
        for m in re.finditer(r"#define\s+(\w+)\s+\(B_UPDATED_TYPE_MATCHUPS\s*>=\s*GEN_(\d+)\s*\?\s*X\(([\d.]+)\)\s*:\s*"
                             r"X\(([\d.]+)\)\)", table):
            macros[m.group(1)] = float(m.group(3)) if updated >= int(m.group(2)) else float(m.group(4))
        start = table.index("gTypeEffectivenessTable")
        body = table[start:table.index("};", start)]         # gTypesInfo follows with rows of the same shape
        order = sorted(type_values.items(), key=lambda kv: kv[1])
        self.types = [name for name, value in order]
        self.chart = {}
        for m in re.finditer(r"\[(TYPE_\w+)\]\s*=\s*\{([^}]*)\}", body):
            cells = [c.strip() for c in m.group(2).split(",") if c.strip()]
            if len(cells) != len(self.types):
                raise ValueError("type chart row %s has %d cells" % (m.group(1), len(cells)))
            row = {}
            for defender, cell in zip(self.types, cells):
                x = re.fullmatch(r"X\(([\d.]+)\)", cell)
                row[type_values[defender]] = float(x.group(1)) if x else macros[cell]
            self.chart[type_values[m.group(1)]] = row
        if len(self.chart) != len(self.types):
            raise ValueError("type chart incomplete")
        skip = {"TYPE_NONE", "TYPE_MYSTERY", "TYPE_STELLAR"}
        self.battle_types = [type_values[t] for t in self.types if t not in skip]

    def eff(self, attack, defenders):
        out = 1.0
        for d in defenders:
            out *= self.chart[attack][d]
        return out
