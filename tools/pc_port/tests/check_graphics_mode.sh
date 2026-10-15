#!/usr/bin/env bash
# Graphics mode checks (the launcher's 3D Diorama / Classic 2D choice) against a built game:
#   ./check_graphics_mode.sh ./pokeemerald-mp-diorama    # or ./pokeemerald-mp
# Needs a display (xvfb-run is used when DISPLAY is unset). Each check prints PASS or FAIL.
set -u
GAME=$(realpath "$1")
KIND=$("$GAME" --probe-graphics 2>/dev/null </dev/null | sed -n 's/.*"graphics":"\([a-z0-9]*\)".*/\1/p' | head -1)
RUN=""
[ -z "${DISPLAY:-}" ] && RUN="xvfb-run -a"
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
fail=0
check() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1 ($2)"; fail=1; fi; }
export PKMN_GRAPHICS_NO_DIALOG=1

# 1. Supported: one JSON line, exit 0, and the probe reads or writes no file.
mkdir "$WORK/p"; out=$(cd "$WORK/p" && $RUN "$GAME" --probe-graphics 2>/dev/null); code=$?
[ $code = 0 ] && echo "$out" | grep -q '"supported":true' && check "probe: usable graphics -> exit 0, supported" ok || check "probe: usable graphics" "exit $code: $out"
[ -z "$(ls -A "$WORK/p")" ] && check "probe writes no file" ok || check "probe writes no file" "$(ls -A "$WORK/p")"

# 2. Failed initialization (a test hook): exit 78 and a reason, never a silent 0/1.
out=$(PKMN_GRAPHICS_FORCE_FAIL=test $RUN "$GAME" --probe-graphics 2>/dev/null); code=$?
[ $code = 78 ] && echo "$out" | grep -q '"supported":false' && echo "$out" | grep -q '"reason":"forced"' \
  && check "probe: failed initialization -> exit 78 with a reason" ok || check "probe: failed initialization" "exit $code: $out"

# 3. A normal start whose graphics fail: the explanation, exit 78, and no save or settings created.
msg=$(PKMN_GRAPHICS_FORCE_FAIL=test $RUN "$GAME" --save "$WORK/s.sav" --config "$WORK/c.cfg" 2>&1); code=$?
[ $code = 78 ] && check "start with failing graphics exits 78 (not silently)" ok || check "start with failing graphics exits 78" "exit $code"
echo "$msg" | grep -q "could not start" && echo "$msg" | grep -q "untouched" && check "start with failing graphics explains itself" ok || check "explanation printed" "$msg"
[ ! -e "$WORK/s.sav" ] && [ ! -e "$WORK/c.cfg" ] && check "failed start creates no save or settings file" ok || check "failed start creates no file" "$(ls "$WORK")"

# 4. The Classic 2D game keeps the 3D view's settings lines when it rewrites the settings file.
if [ "$KIND" = 2d ]; then
  printf 'cameraMode=diorama\ntreeStyle=1\nadventureZoom=3\nadventureTilt=-2\ndioramaZoom=1\ndioramaTilt=2\nvolume=5\n' > "$WORK/k.cfg"
  timeout -s TERM 8 $RUN "$GAME" --save "$WORK/k.sav" --config "$WORK/k.cfg" >/dev/null 2>&1
  missing=""; for line in cameraMode=diorama treeStyle=1 adventureZoom=3 adventureTilt=-2 dioramaZoom=1 dioramaTilt=2; do
    grep -qx "$line" "$WORK/k.cfg" || missing="$missing $line"; done
  grep -q "^volume=5" "$WORK/k.cfg" && [ -z "$missing" ] && check "Classic 2D keeps the 3D settings lines" ok || check "Classic 2D keeps the 3D settings lines" "missing:$missing"
fi
exit $fail
