#!/bin/sh
# Vérifie le Smart Scrolling : maintien sur la flèche basse.
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-scroll.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-scroll}
SRC="$HOME_TEST/Beaucoup"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC" "$OUT/log" "$STATE"
i=0
while [ "$i" -lt 80 ]; do
    printf 'fichier %02d\n' "$i" >"$SRC/fichier-$(printf '%02d' "$i").txt"
    i=$((i + 1))
done

printf 'W\t%s\t20\t40\t400\t300\t0\n' "$SRC" >"$STATE/fenetres"

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
export DISPLAY=:$(cat "$TMP/display")
export HOME="$HOME_TEST"

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-finder "$SRC" >"$OUT/log/finder.log" 2>&1 &
APP=$!
xdotool search --sync --onlyvisible --name '^Beaucoup$' >/dev/null
sleep 0.6

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}

# Flèche basse : à 8 px du bord droit du client et 22 px du bas (cadre 6 + titre 22).
ARROW_X=$((20 + 6 + 400 - 8))
ARROW_Y=$((40 + 22 + 300 - 22))
import -window root "$OUT/avant.png"
xdotool mousemove "$ARROW_X" "$ARROW_Y" mousedown 1
sleep 1.5
xdotool mouseup 1
sleep 0.3
import -window root "$OUT/apres.png"

DIFF=$(compare -metric AE "$OUT/avant.png" "$OUT/apres.png" null: 2>&1 || true)
case "$DIFF" in
    ''|0) fail "l'ascenseur n'a pas défilé (diff=$DIFF)" ;;
esac
echo "Smart Scrolling : OK (pixels modifiés : $DIFF)"
echo "Captures et journaux: $OUT"
