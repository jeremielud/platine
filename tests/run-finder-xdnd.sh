#!/bin/sh
# Glisser-déposer XDND du Finder vers une cible X11 externe.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-xdnd.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-xdnd}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC" "$OUT/log" "$STATE"
cc -O2 -Wall -Wextra -o "$TMP/dnd-target" tests/dnd-target.c -lX11
cc -O2 -Wall -Wextra -o "$TMP/dnd-source" tests/dnd-source.c -lX11
DST="$HOME_TEST/Destination"
mkdir -p "$DST"
printf 'alpha\n' >"$SRC/a.txt"
printf 'externe\n' >"$SRC/entrant espace.txt"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'W\t%s\t390\t40\t350\t360\t0\n' "$DST" >>"$STATE/fenetres"
printf 'I\t%s\ta.txt\t10\t50\n' "$SRC" >"$STATE/icones"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
TARGET=
cleanup() {
    [ -n "$TARGET" ] && kill "$TARGET" 2>/dev/null || true
    [ -n "$APP" ] && kill "$APP" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$TARGET" 2>/dev/null || true
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
export PLATINE_ASSETS="$PWD/assets" PLATINE_SONS=0
./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
./build/platine-finder "$SRC" "$DST" >"$OUT/log/finder.log" 2>&1 & APP=$!
xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
DEST_WIN=$(xdotool search --sync --onlyvisible --name '^Destination$' | head -n 1)
"$TMP/dnd-target" "$TMP/recu.txt" >"$OUT/log/cible.log" 2>&1 & TARGET=$!
xdotool search --sync --onlyvisible --name '^Cible XDND$' >/dev/null
sleep 0.5

xdotool mousemove 58 129 mousedown 1
sleep 0.1
xdotool mousemove 350 180
sleep 0.1
xdotool mousemove 600 240
sleep 0.3
xdotool mouseup 1
i=0
while [ ! -s "$TMP/recu.txt" ]; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo 'ÉCHEC: cible XDND sans réponse' >&2; exit 1; }
    sleep 0.05
done
wait "$TARGET"
TARGET=
grep -Fq "file://$SRC/a.txt" "$TMP/recu.txt" || {
    echo 'ÉCHEC: URI source absente' >&2
    exit 1
}
[ -f "$SRC/a.txt" ] || { echo 'ÉCHEC: la copie externe a supprimé la source' >&2; exit 1; }

# Sens entrant, avec XdndTypeList et décodage d'un espace dans l'URI.
timeout 5 "$TMP/dnd-source" "$DEST_WIN" "$SRC/entrant espace.txt" ||
    { echo 'ÉCHEC: le Finder a refusé le dépôt XDND entrant' >&2; exit 1; }
i=0
while [ ! -f "$DST/entrant espace.txt" ]; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo 'ÉCHEC: fichier XDND entrant absent' >&2; exit 1; }
    sleep 0.05
done
[ "$(cat "$DST/entrant espace.txt")" = 'externe' ] ||
    { echo 'ÉCHEC: contenu du dépôt entrant incorrect' >&2; exit 1; }
[ -f "$SRC/entrant espace.txt" ] ||
    { echo 'ÉCHEC: le dépôt entrant a supprimé la source' >&2; exit 1; }
import -window root "$OUT/final.png"
echo 'Glisser-déposer XDND dans les deux sens : OK'
echo "Captures et journaux: $OUT"
