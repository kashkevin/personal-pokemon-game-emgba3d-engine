"""Names as the .party format spells them, checked against tools/trainerproc/main.c's own conversion.

trainerproc turns a human name into a constant (fprint_constant / fprint_species): letters upper-cased, digits kept, an
apostrophe dropped, anything else an underscore (species: runs of other characters become one underscore, `%` and the
right single quote dropped, the gender signs `_M`/`_F`, `é` an `E`); a name that already starts with `PREFIX_` is kept.
`party_name()` gives the house-style name (the game's own name, as the dump spells it) when trainerproc turns it back
into exactly the wanted constant, and the constant itself otherwise, so every name in the generated file converts to the
intended value.
"""


def _is_constant(s, prefix):
    return s.startswith(prefix) and len(s) > len(prefix) and s[len(prefix)] == "_"


def trainerproc_constant(prefix, s):
    """tools/trainerproc/main.c fprint_constant()."""
    out = [] if _is_constant(s, prefix) else [prefix + "_"]
    if not s:
        return "".join(out) + "NONE"
    for c in s.encode("utf-8"):
        ch = chr(c)
        if "A" <= ch <= "Z" or "0" <= ch <= "9":
            out.append(ch)
        elif "a" <= ch <= "z":
            out.append(ch.upper())
        elif ch == "'":
            pass
        else:
            out.append("_")
    return "".join(out)


_MALE = "♂".encode("utf-8")
_FEMALE = "♀".encode("utf-8")
_E_ACUTE = "é".encode("utf-8")
_RSQUO = "’".encode("utf-8")


def trainerproc_species(s):
    """tools/trainerproc/main.c fprint_species()."""
    out = [] if _is_constant(s, "SPECIES") else ["SPECIES_"]
    if not s:
        return "".join(out) + "NONE"
    b = s.encode("utf-8")
    underscore = False
    i = 0
    while i < len(b):
        ch = chr(b[i])
        if "A" <= ch <= "Z" or "0" <= ch <= "9" or "a" <= ch <= "z":
            if underscore:
                out.append("_")
            underscore = False
            out.append(ch.upper())
        elif ch in "'%":
            pass
        elif b.startswith(_RSQUO, i):
            i += len(_RSQUO) - 1
        elif b.startswith(_MALE, i):
            underscore = False
            out.append("_M")
            i += len(_MALE) - 1
        elif b.startswith(_FEMALE, i):
            underscore = False
            out.append("_F")
            i += len(_FEMALE) - 1
        elif b.startswith(_E_ACUTE, i):
            if underscore:
                out.append("_")
            underscore = False
            out.append("E")
            i += len(_E_ACUTE) - 1
        else:
            underscore = True
        i += 1
    return "".join(out)


def title_from_constant(const, prefix):
    """TRAINER_CLASS_LEADER_FRLG -> 'Leader Frlg' (the house style of src/data/trainers*.party)."""
    words = const[len(prefix) + 1:].split("_")
    special = {"HP": "HP", "PKMN": "Pkmn"}
    return " ".join(special.get(w, w.capitalize()) for w in words)


def _safe_ascii(name):
    return name and all(32 <= ord(c) < 127 for c in name) and name == name.strip() and "  " not in name


def party_name(prefix, const, game_name):
    """The name to write for `const` in the .party file: the game's own name when it converts back exactly."""
    convert = trainerproc_species if prefix == "SPECIES" else (lambda s: trainerproc_constant(prefix, s))
    if _safe_ascii(game_name) and convert(game_name) == const:
        return game_name
    derived = title_from_constant(const, prefix)
    if convert(derived) == const:
        return derived
    if convert(const) == const:
        return const
    raise ValueError("no spelling of %s converts back to it" % const)


def party_field(prefix, const):
    """Class / Pic / Music / AI / Nature / Gender values: the title-case house style, checked to convert back."""
    name = title_from_constant(const, prefix)
    if trainerproc_constant(prefix, name) != const:
        name = const
    if trainerproc_constant(prefix, name) != const:
        raise ValueError("no spelling of %s converts back to it" % const)
    return name
