#!/bin/sh
# Métadonnées type/créateur : store finder2/types, colonne Kind de la vue
# liste, ligne Type de « Lire les informations », suivi lors des copies.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder build/platine-info

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-types.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-types}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC/dossierb" "$OUT/log" "$STATE"
printf '1\n' >"$SRC/notes.txt"
printf 'x\n' >"$SRC/photo.jpg"
printf '123\n' >"$SRC/archive.zip"
printf 'W\t%s\t20\t40\t560\t380\t2\n' "$SRC" >"$STATE/fenetres"
# métadonnée amorcée : notes.txt = TEXT/Toy, comme si un Édition l'avait posée
printf '%s\tTEXT\tToy\n' "$SRC/notes.txt" >"$STATE/types"

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

# Lignes (tri nom) : archive.zip (0), dossierb (1), notes.txt (2), photo.jpg (3).
# Colonne Kind : client x 445..bord ; client à (+26,+62), rang 19, y0=129.
kind_cell() {
    convert "$1" -crop 94x19+474+$((129 + $2 * 19)) +repage "$3"
}
import -window root "$OUT/liste.png"
kind_cell "$OUT/liste.png" 0 "$OUT/k-archive.png"
kind_cell "$OUT/liste.png" 2 "$OUT/k-notes.png"
kind_cell "$OUT/liste.png" 3 "$OUT/k-photo.png"

# Kind différenciés : la boîte englobante du texte (tronquée au fond) doit
# avoir une largeur différente de plus de 8 px entre deux libellés distincts.
paire_diff() {
    w1=$(convert "$1" -colorspace Gray -threshold 60% -format '%@' info: | cut -dx -f1)
    w2=$(convert "$2" -colorspace Gray -threshold 60% -format '%@' info: | cut -dx -f1)
    d=$((w1 - w2)); [ $d -lt 0 ] && d=$((-d))
    [ "$d" -gt 4 ] || fail "deux genres identiques ($3) w1=$w1 w2=$w2"
}
paire_diff "$OUT/k-archive.png" "$OUT/k-notes.png" "Archive vs Document texte"
paire_diff "$OUT/k-notes.png" "$OUT/k-photo.png" "Document texte vs Image"
paire_diff "$OUT/k-archive.png" "$OUT/k-photo.png" "Archive vs Image"

# Les icônes 16×16 suivent la même famille que le libellé Kind.
icone_cell() {
    convert "$1" -crop 16x16+47+$((130 + $2 * 19)) +repage "$3"
}
images_differentes() {
    n=$(compare -metric AE "$1" "$2" null: 2>&1 || true)
    [ "$n" -gt 20 ] 2>/dev/null || fail "icônes identiques ($3), écart=$n"
}
icone_cell "$OUT/liste.png" 0 "$OUT/i-archive.png"
icone_cell "$OUT/liste.png" 2 "$OUT/i-notes.png"
icone_cell "$OUT/liste.png" 3 "$OUT/i-photo.png"
images_differentes "$OUT/i-archive.png" "$OUT/i-notes.png" "archive/texte"
images_differentes "$OUT/i-notes.png" "$OUT/i-photo.png" "texte/image"
images_differentes "$OUT/i-archive.png" "$OUT/i-photo.png" "archive/image"

# Copier notes.txt dans dossierb : le store suit (entrée dupliquée).
xdotool mousemove 100 176 click 1   # ligne notes.txt (rang 2)
sleep 0.3
xdotool keydown alt; sleep 0.1; xdotool key c; sleep 0.1; xdotool keyup alt
sleep 0.3
xdotool mousemove 100 157 click --repeat 2 --delay 200 1   # dossierb rang 1
sleep 0.8
xdotool keydown alt; sleep 0.1; xdotool key v; sleep 0.1; xdotool keyup alt
sleep 0.6
grep -q "$SRC/notes.txt	TEXT	Toy" "$STATE/types" ||
    fail "entrée d'origine perdue dans le store"
grep -q "$SRC/dossierb/notes.txt	TEXT	Toy" "$STATE/types" ||
    fail "le store n'a pas suivi la copie"

# dossierb (cascade 40,40) recouvre Source : on le ferme pour la suite.
xdotool keydown alt; sleep 0.1; xdotool key w; sleep 0.1; xdotool keyup alt
sleep 0.5

# Lire les informations : la ligne Type diffère entre notes.txt et photo.jpg.
info_type() {
    sleep 1.2
    xdotool search --name " Info" 2>/dev/null | head -1 >"$TMP/iid"
    [ -s "$TMP/iid" ] || fail "fenêtre Info absente ($1)"
    IID=$(cat "$TMP/iid")
    import -window "$IID" "$OUT/info-$1.png"
    convert "$OUT/info-$1.png" -crop 235x16+89+86 +repage "$OUT/info-$1-type.png"
    xdotool keydown alt; sleep 0.1; xdotool key w; sleep 0.1; xdotool keyup alt
    sleep 0.4
}
# revenir à Source, sélectionner, ⌘I
xdotool mousemove 200 48 click 1    # barre de titre Source
sleep 0.3
xdotool mousemove 100 176 click 1
sleep 0.2
xdotool keydown alt; sleep 0.1; xdotool key i; sleep 0.1; xdotool keyup alt
sleep 0.8
info_type notes
xdotool mousemove 200 48 click 1    # barre de titre Source
sleep 0.3
xdotool mousemove 100 195 click 1   # photo.jpg rang 3
sleep 0.2
xdotool keydown alt; sleep 0.1; xdotool key i; sleep 0.1; xdotool keyup alt
sleep 0.8
info_type photo
paire_diff "$OUT/info-notes-type.png" "$OUT/info-photo-type.png" \
    "Info Type notes vs photo"

echo 'Types et genres : OK'
echo "Captures et journaux: $OUT"
