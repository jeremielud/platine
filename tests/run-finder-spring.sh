#!/bin/sh
# Dossiers à ressort : maintenir sur un dossier pendant un glisser l'ouvre.
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-spring.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-spring}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC/Cible" "$OUT/log" "$STATE"
printf 'garde\n' >"$SRC/f.txt"

printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'I\t%s\tf.txt\t163\t50\n' "$SRC" >"$STATE/icones"
printf 'I\t%s\tCible\t35\t50\n' "$SRC" >>"$STATE/icones"

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
xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}

FILE_X=211
FILE_Y=129
FOLDER_X=83
FOLDER_Y=129

# 1) Survole bref (200 ms) : la fenêtre du dossier ne doit pas s'ouvrir.
xdotool mousemove "$FILE_X" "$FILE_Y" mousedown 1
sleep 0.1
xdotool mousemove 400 200
sleep 0.1
xdotool mousemove "$FOLDER_X" "$FOLDER_Y"
sleep 0.2
if xdotool search --onlyvisible --name '^Cible$' >/dev/null 2>&1; then
    fail "la fenêtre s'est ouverte avant le délai de maintien"
fi
# Échap annule le glisser.
xdotool key Escape
sleep 0.3
[ -f "$SRC/f.txt" ] || fail 'Échap a quand même déplacé f.txt'
# Le bouton de la phase 1 n'a pas eu son relâchement : sans ce mouseup,
# le serveur ignore le mousedown de la phase 2 (bouton déjà enfoncé).
xdotool mouseup 1
sleep 0.3

# 2) Maintien : la fenêtre Cible s'ouvre sous le curseur, et le dépôt y va.
xdotool mousemove "$FILE_X" "$FILE_Y" mousedown 1
sleep 0.1
xdotool mousemove 400 200
sleep 0.1
xdotool mousemove "$FOLDER_X" "$FOLDER_Y"
sleep 1.2
if ! xdotool search --onlyvisible --name '^Cible$' >/dev/null 2>&1; then
    cat "$OUT/log/finder.log" >&2
    fail "la fenêtre du dossier ne s'est pas ouverte"
fi
import -window root "$OUT/ouverture.png"
xdotool mouseup 1
sleep 0.6
[ -f "$SRC/Cible/f.txt" ] || fail 'f.txt non déposé dans le dossier ouvert'
[ ! -e "$SRC/f.txt" ] || fail 'f.txt est encore à la source'
import -window root "$OUT/apres.png"

echo 'Dossiers à ressort : OK'
echo "Captures et journaux: $OUT"
