#!/bin/sh
# WM_TAKE_FOCUS : le client reçoit le message, prend lui-même le focus et le
# clavier lui parvient. Sans ce protocole, les compteurs restent à zéro.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-focus.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/focus}
mkdir -p "$HOME_TEST" "$OUT/log"
cc -O2 -Wall -Wextra -o "$TMP/focus-client" tests/focus-client.c -lX11
: >"$TMP/focus.log"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
A=
B=
cleanup() {
    [ -n "${A:-}" ] && kill "$A" 2>/dev/null || true
    [ -n "${B:-}" ] && kill "$B" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$A" 2>/dev/null || true
    wait "$B" 2>/dev/null || true
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
FOCUS_LOG="$TMP/focus.log" "$TMP/focus-client" 'Focus A' 60 80 >"$OUT/log/a.log" 2>&1 & A=$!
AW=$(xdotool search --sync --onlyvisible --name '^Focus A$' | head -1)
FOCUS_LOG="$TMP/focus.log" "$TMP/focus-client" 'Focus B' 360 220 >"$OUT/log/b.log" 2>&1 & B=$!
BW=$(xdotool search --sync --onlyvisible --name '^Focus B$' | head -1)
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "Journal conservé: $TMP/focus.log" >&2
    exit 1
}
compte() { grep -c "$2" "$1" 2>/dev/null || true; }
attend() {
    i=0
    while [ "$(compte "$1" "$2")" -lt "$3" ]; do
        i=$((i + 1))
        [ "$i" -lt 100 ] || fail "événement absent : $2"
        sleep 0.05
    done
}
attend "$TMP/focus.log" '^Focus A WM_TAKE_FOCUS$' 1
attend "$TMP/focus.log" '^Focus B WM_TAKE_FOCUS$' 1

xdotool mousemove 120 160 click 1
attend "$TMP/focus.log" '^Focus A WM_TAKE_FOCUS$' 2
[ "$(xdotool getwindowfocus)" = "$AW" ] || fail "Focus A n'a pas pris le focus X11"
xdotool key x
attend "$TMP/focus.log" '^Focus A KEY$' 1

xdotool mousemove 420 310 click 1
attend "$TMP/focus.log" '^Focus B WM_TAKE_FOCUS$' 2
[ "$(xdotool getwindowfocus)" = "$BW" ] || fail "Focus B n'a pas pris le focus X11"
xdotool key y
attend "$TMP/focus.log" '^Focus B KEY$' 1
import -window root "$OUT/focus.png"

echo 'WM_TAKE_FOCUS et clavier : OK'
echo "Captures et journaux: $OUT"
