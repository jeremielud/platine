#!/bin/sh
# Contour XOR : 1 px comme Mac OS 9, aucun résidu au relâchement, et hauteur
# limitée à la barre de titre quand une fenêtre repliée est déplacée.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-outline.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/outline}
mkdir -p "$HOME_TEST" "$OUT/log"
cc -O2 -Wall -Wextra -o "$TMP/focus-client" tests/focus-client.c -lX11

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
cleanup() {
    [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$HOME_TEST"
export PLATINE_ASSETS="$PWD/assets"
./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
FOCUS_LOG="$TMP/focus.log" "$TMP/focus-client" 'Contour' 60 80 >"$OUT/log/app.log" 2>&1 & APP=$!
AW=$(xdotool search --sync --onlyvisible --name '^Contour$' | head -1)
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "Captures conservées: $OUT" >&2
    exit 1
}
lignes() {
    python3 - "$1" "$2" "$3" <<'PY'
from PIL import Image
import sys
im = Image.open(sys.argv[1]).convert("RGB")
for y in range(int(sys.argv[2]), int(sys.argv[3])):
    row = [im.getpixel((x, y)) for x in range(170, 390)]
    if sum(a != b for a, b in zip(row, row[1:])) > 180:
        print(y)
PY
}

# Poignée de la fenêtre 240×150 à 60,80 : déplacement de +98,+53.
xdotool mousemove 302 247 mousedown 1 mousemove 400 300
sleep 0.4
import -window root "$OUT/resize.png"
[ "$(lignes "$OUT/resize.png" 285 325 | wc -l)" -eq 1 ] ||
    fail "le contour de redimensionnement n'est pas une ligne pointillée de 1 px"
xdotool mouseup 1
sleep 0.5
import -window root "$OUT/apres-resize.png"
[ -z "$(lignes "$OUT/apres-resize.png" 285 325)" ] ||
    fail "le contour de redimensionnement laisse un résidu"

# Une fenêtre qui se redessine sous le contour (ici tout l'écran, xrefresh) :
# agrandir, rafraîchir, puis rétrécir — l'ancien contour tombe sur le bureau et
# ne doit pas y rester une fois la fenêtre relâchée (maillage résiduel).
xdotool mousemove 400 300 mousedown 1 mousemove 520 380
sleep 0.3
xrefresh
sleep 0.3
xdotool mousemove 380 300
sleep 0.3
xdotool mouseup 1
sleep 0.6
import -window root "$OUT/apres-rafraichi.png"
xrefresh
sleep 0.5
import -window root "$OUT/rafraichi-repeint.png"
N=$(compare -metric AE "$OUT/apres-rafraichi.png" "$OUT/rafraichi-repeint.png" null: 2>&1 || true)
[ "$N" -eq 0 ] 2>/dev/null || fail "le contour laisse un maillage après un rafraîchissement ($N pixels)"
xdotool mousemove 382 302 mousedown 1 mousemove 400 300 mouseup 1
sleep 0.5

# Repli par la case dédiée, puis déplacement de la barre seule.
xdotool mousemove 399 90 click 1
sleep 0.6
import -window root "$OUT/replie.png"
python3 - "$OUT/replie.png" <<'PY' || fail "la fenêtre ne s'est pas repliée"
from PIL import Image
import sys
im = Image.open(sys.argv[1]).convert("RGB")
assert all(im.getpixel((x, 120)) != (255, 255, 255) for x in range(70, 400))
PY
xdotool mousemove 200 90 mousedown 1 mousemove 260 140
sleep 0.4
import -window root "$OUT/deplacement-replie.png"
[ "$(lignes "$OUT/deplacement-replie.png" 135 170 | wc -l)" -eq 1 ] ||
    fail "le bas du contour replié n'accompagne pas la barre de titre"
[ -z "$(lignes "$OUT/deplacement-replie.png" 180 370)" ] ||
    fail "le contour replié conserve la hauteur de la fenêtre dépliée"
xdotool mouseup 1
sleep 0.5
import -window root "$OUT/apres-deplacement-replie.png"
[ -z "$(lignes "$OUT/apres-deplacement-replie.png" 120 370)" ] ||
    fail "le déplacement replié laisse un résidu"

echo 'Contour XOR, resize et fenêtre repliée : OK'
echo "Captures et journaux: $OUT"
