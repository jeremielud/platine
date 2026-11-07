#!/bin/sh
# Copier/Coller et Couper/Coller (⌘C ⌘V ⌘X) avec conflits de noms assistés.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-coller.XXXXXX)
cc -O2 -Wall -Wextra -o "$TMP/clipboard-client" tests/clipboard-client.c -lX11
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-coller}
SRC="$HOME_TEST/Source"
DST="$HOME_TEST/Destination"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC" "$DST" "$OUT/log" "$STATE"
printf 'alpha\n' >"$SRC/a.txt"
printf 'bravo\n' >"$SRC/gros.txt"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'W\t%s\t430\t40\t350\t360\t0\n' "$DST" >>"$STATE/fenetres"
printf 'I\t%s\ta.txt\t10\t50\n' "$SRC" >"$STATE/icones"
printf 'I\t%s\tgros.txt\t138\t50\n' "$SRC" >>"$STATE/icones"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
CLIP=
cleanup() {
    [ -n "$CLIP" ] && kill "$CLIP" 2>/dev/null || true
    [ -n "$APP" ] && kill "$APP" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$CLIP" 2>/dev/null || true
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
D=:$(cat "$TMP/display")
export DISPLAY="$D"
export HOME="$HOME_TEST"

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-finder "$SRC" "$DST" >"$OUT/log/finder.log" 2>&1 &
APP=$!
xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
xdotool search --sync --onlyvisible --name '^Destination$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
# clients à (+26,+62) ; glyphe d'une icône (x,y) : écran = x+48, y+79
# barre de titre de Destination : y écran 48
active_dest() { xdotool mousemove 640 48 click 1; sleep 0.3; }

# 1) Copier a.txt dans Destination (source conservée).
xdotool mousemove 58 129 click 1
sleep 0.3
xdotool keydown alt; sleep 0.15; xdotool key c; sleep 0.1; xdotool keyup alt
sleep 0.3
active_dest
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.6
[ -f "$DST/a.txt" ] || fail "a.txt non collé dans Destination"
[ "$(cat "$DST/a.txt")" = 'alpha' ] || fail "contenu du collage incorrect"
[ -f "$SRC/a.txt" ] || fail "la copie a perdu la source"

# 2) Couper gros.txt vers Destination (source déplacée).
xdotool search --onlyvisible --name '^Source$' windowactivate >/dev/null 2>&1 || true
xdotool mousemove 186 129 click 1
sleep 0.3
xdotool keydown alt; sleep 0.15; xdotool key x; sleep 0.1; xdotool keyup alt
sleep 0.3
active_dest
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.6
[ -f "$DST/gros.txt" ] || fail "gros.txt non déplacé dans Destination"
[ ! -e "$SRC/gros.txt" ] || fail "le couper a conservé la source"

# 3) Le couper est consommé : re-coller ne fait rien.
nb=$(ls "$DST" | wc -l)
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.5
[ "$(ls "$DST" | wc -l)" = "$nb" ] || fail "le couper n'a pas été consommé"

# 4) Conflit accepté : Remplacer (Entrée = bouton par défaut).
xdotool mousemove 58 129 click 1
sleep 0.3
xdotool keydown alt; sleep 0.15; xdotool key c; sleep 0.1; xdotool keyup alt
sleep 0.3
printf 'DESTIN\n' >"$DST/a.txt"
active_dest
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.5
xdotool search --onlyvisible --name '^Alerte$' >/dev/null 2>&1 ||
    fail "pas d'alerte de conflit"
xdotool key Return
sleep 0.5
[ "$(cat "$DST/a.txt")" = 'alpha' ] || fail "Remplacer n'a pas remplacé"

# 5) Conflit refusé : Arrêter (Échap) laisse le fichier intact.
printf 'MARQUEUR\n' >"$DST/a.txt"
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.5
xdotool search --onlyvisible --name '^Alerte$' >/dev/null 2>&1 ||
    fail "pas d'alerte de conflit (2e)"
xdotool key Escape
sleep 0.5
[ "$(cat "$DST/a.txt")" = 'MARQUEUR' ] || fail "Arrêter a quand même remplacé"

# 6) Changement de fenêtre propriétaire, puis publication en text/uri-list.
active_dest
xdotool keydown alt; sleep 0.15; xdotool key a; xdotool key c; sleep 0.1; xdotool keyup alt
xdotool mousemove 180 48 click 1; sleep 0.3
xdotool keydown alt; sleep 0.15; xdotool key a; xdotool key c; sleep 0.1; xdotool keyup alt
sleep 0.3
timeout 5 "$TMP/clipboard-client" get >"$TMP/clipboard.txt" ||
    fail "le Finder ne publie pas CLIPBOARD"
grep -Fq "file://$SRC/a.txt" "$TMP/clipboard.txt" ||
    fail "URI du fichier copié absente de CLIPBOARD"

# 7) Un client X11 externe fournit un fichier à coller.
printf 'externe\n' >"$SRC/externe.txt"
"$TMP/clipboard-client" own "$SRC/externe.txt" & CLIP=$!
sleep 0.3
active_dest
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.8
[ "$(cat "$DST/externe.txt")" = 'externe' ] || fail "collage X11 externe absent"
kill "$CLIP"
wait "$CLIP" 2>/dev/null || true
CLIP=

# 8) Repli sur UTF8_STRING si le client ne fournit pas text/uri-list.
printf 'texte seul\n' >"$SRC/texte-seul.txt"
"$TMP/clipboard-client" own-text "$SRC/texte-seul.txt" & CLIP=$!
sleep 0.3
active_dest
xdotool keydown alt; sleep 0.15; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.8
[ "$(cat "$DST/texte-seul.txt")" = 'texte seul' ] || fail "repli UTF-8 absent"
import -window root "$OUT/final.png"

echo 'Copier/Coller, Couper/Coller et presse-papiers X11 : OK'
echo "Captures et journaux: $OUT"
