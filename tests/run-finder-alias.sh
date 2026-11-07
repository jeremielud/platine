#!/bin/sh
# Alias : création par Fichier > Créer un alias (⌘M), ouverture vers la
# cible, alerte quand la cible est introuvable.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-alias.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-alias}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC/cible2" "$OUT/log" "$STATE"
printf 'contenu\n' >"$SRC/cible.txt"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'I\t%s\tcible.txt\t35\t50\n' "$SRC" >"$STATE/icones"
printf 'I\t%s\tcible2\t163\t50\n' "$SRC" >>"$STATE/icones"

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
D=:$(cat "$TMP/display")
export DISPLAY="$D"
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
# client à (+26,+62) ; glyphe d'une icône amorcée (x,y) : écran = x+48, y+79

# 1) Créer un alias du fichier (sélection + ⌘M, acheminé par le WM).
xdotool mousemove 83 129 click 1
sleep 0.3
xdotool key alt+m
sleep 0.6
AL="$SRC/cible.txt alias"
[ -f "$AL" ] || fail "« cible.txt alias » non créé"
head -c 4 "$AL" | grep -q '^PLAL' || fail "magic PLAL absent"
grep -q "$SRC/cible.txt" "$AL" || fail "chemin de la cible absent de l'alias"

# 2) Créer un alias du dossier.
xdotool mousemove 211 129 click 1
sleep 0.3
xdotool key alt+m
sleep 0.6
AD="$SRC/cible2 alias"
[ -f "$AD" ] || fail "« cible2 alias » non créé"
grep -q "$SRC/cible2" "$AD" || fail "chemin de la cible absent de l'alias"

# 3) Nouveaux éléments : « cible.txt alias » (35,114), « cible2 alias » (163,114).
import -window root "$OUT/apres-creation.png"

# 4) Double-clic sur l'alias du dossier : sa fenêtre s'ouvre.
xdotool mousemove 211 193 click --repeat 2 --delay 200 1
sleep 1
xdotool search --onlyvisible --name '^cible2$' >/dev/null 2>&1 ||
    fail "l'alias du dossier n'ouvre pas la fenêtre de la cible"

# 5) Alias brisé : la cible disparaît, double-clic = alerte (Entrée/Echap la ferme).
# La fenêtre de la cible (ouverte au 4) recouvre le bureau : on la ferme (⌘W).
xdotool key alt+w
sleep 0.6
printf 'PLAL%s/nulle-part\n' "$SRC" >"$AD"
xdotool mousemove 211 193 click --repeat 2 --delay 200 1
sleep 1
import -window root "$OUT/alerte-alias-brisé.png"
xdotool search --onlyvisible --name '^Alerte$' >/dev/null 2>&1 ||
    fail "aucune alerte pour l'alias brisé"
xdotool key Escape
sleep 0.5
xdotool search --onlyvisible --name '^Alerte$' >/dev/null 2>&1 &&
    fail "l'alerte ne se ferme pas" || true

# 6) L'alias du fichier (régulier) se comporte comme un fichier : déplacement.
mv "$AL" "$SRC/cible2/"
[ -f "$SRC/cible2/cible.txt alias" ] || fail "l'alias ne se déplace pas comme un fichier"

echo 'Alias : OK'
echo "Captures et journaux: $OUT"
