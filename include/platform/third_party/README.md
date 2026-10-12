# Third-party code (native platform layer)

| File | Project | Version | Licence | Source | SHA-256 |
| --- | --- | --- | --- | --- | --- |
| `stb_image.h` | stb by Sean Barrett | v2.30 | public domain (Unlicense) or MIT, at the user's choice | https://raw.githubusercontent.com/nothings/stb/master/stb_image.h, downloaded 2026-10-12 | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |

Used only by `src/platform/battle_sprite_library.c` (the battle sprite pack loader), which
compiles stb_image's zlib decoder `static` with PNG-only options: nothing else in the game sees
it, so it cannot clash with another copy of stb_image. Unmodified copy.
