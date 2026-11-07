#!/bin/sh
# Vue liste : tri par en-têtes (clic = colonne, reclic = inverser),
# persistance du tri par dossier dans finder2/fenetres.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-liste.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-liste}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC/dossierb" "$OUT/log" "$STATE"
printf '1\n' >"$SRC/alpha.txt";                  touch -d '2026-01-02 10:00' "$SRC/alpha.txt"
printf '123\n' >"$SRC/zulu.txt";                 touch -d '2026-03-10 12:00' "$SRC/zulu.txt"
printf '1234567890123456\n' >"$SRC/mike.txt";    touch -d '2026-06-15 09:00' "$SRC/mike.txt"
touch -d '2026-02-01 08:00' "$SRC/dossierb"
printf 'W\t%s\t20\t40\t560\t380\t2\n' "$SRC" >"$STATE/fenetres"

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

lance_finder() {
    ./build/platine-finder "$SRC" >>"$OUT/log/finder.log" 2>&1 &
    APP=$!
    xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
    sleep 0.5
}

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
lance_finder

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}

# Coordonnées écran : client (+26,+62). En-têtes client y 46..67 -> y écran 118.
# Lignes : LISTE_Y0=67, rang 19 : ligne i à l'écran y = 129 + 19i.
# Le nom d'un fichier commence au client x=42 (icône à 21) ; on croppe 98 px.
ROW0Y=129
ligne() { convert "$1" -crop 98x19+68+$((ROW0Y)) +repage "$2"; }
assert_ligne() {
    import -window root "$OUT/courant.png"
    ligne "$OUT/courant.png" "$OUT/l0.png"
    n=$(compare -metric AE -fuzz 25% "$1" "$OUT/l0.png" null: 2>&1 || true)
    [ "$n" -lt 100 ] 2>/dev/null || fail "ligne 0 inattendue ($2), écart=$n"
}

# Tri initial : nom croissant (alpha, dossierb, mike, zulu).
import -window root "$OUT/tri-nom.png"
ligne "$OUT/tri-nom.png" "$OUT/ref-alpha.png";   convert "$OUT/tri-nom.png" -crop 98x19+68+$((ROW0Y + 19)) +repage "$OUT/ref-dossierb.png"
ligne "$OUT/tri-nom.png" "$OUT/ref-mike.png.tmp"; convert "$OUT/tri-nom.png" -crop 98x19+68+$((ROW0Y + 2 * 19)) +repage "$OUT/ref-mike.png"
ligne "$OUT/tri-nom.png" "$OUT/ref-zulu.png.tmp"; convert "$OUT/tri-nom.png" -crop 98x19+68+$((ROW0Y + 3 * 19)) +repage "$OUT/ref-zulu.png"
rm -f "$OUT/ref-mike.png.tmp" "$OUT/ref-zulu.png.tmp"

assert_ligne "$OUT/ref-alpha.png" "tri initial = nom croissant"

# Date : premier clic décroissant (le plus récent d'abord), reclic croissant.
xdotool mousemove 264 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-mike.png" "date décroissante (mike en tête)"
xdotool mousemove 264 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-alpha.png" "date croissante (alpha en tête)"

# Taille : décroissant (dossierb ~4 Ko d'abord), puis croissant.
xdotool mousemove 438 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-dossierb.png" "taille décroissante (dossierb en tête)"
xdotool mousemove 438 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-alpha.png" "taille croissante (alpha en tête)"

# Nom puis bouton de sens : zulu en tête.
xdotool mousemove 126 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-alpha.png" "nom croissant (alpha en tête)"
xdotool mousemove 562 118 click 1
sleep 0.4
assert_ligne "$OUT/ref-zulu.png" "bouton de sens : nom décroissant (zulu en tête)"

# Persistance : le tri est écrit dans fenetres (9 champs, puis la disposition des icônes) et survit à un
# redémarrage du Finder.
grep -P "^W\t.*\t\d+\t\d+\t\d+\t\d+\t2\t0\t1\t\d+\t\d+$" "$STATE/fenetres" >/dev/null ||
    fail "fenetres sans tri persisté (attendu vue=2 tri=0 inv=1)"
kill "$APP" 2>/dev/null || true
wait "$APP" 2>/dev/null || true
lance_finder
assert_ligne "$OUT/ref-zulu.png" "tri persistant après redémarrage (zulu en tête)"

echo 'Vue liste, tri par en-têtes : OK'
echo "Captures et journaux: $OUT"
