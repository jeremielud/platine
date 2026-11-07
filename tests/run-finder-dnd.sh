#!/bin/sh
# Teste le glisser-déposer réel du Finder dans un HOME jetable.
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-dnd.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-dnd}
SRC="$HOME_TEST/Source"
DST="$HOME_TEST/Destination"
mkdir -p "$SRC/Dossier/imbrique" "$DST" "$OUT/log" \
    "$HOME_TEST/.config/platine/finder2" "$TMP/bin"
cat >"$TMP/bin/aplay" <<'FIN'
#!/bin/sh
printf '%s\n' "$*" >>"$APLAY_LOG"
FIN
chmod +x "$TMP/bin/aplay"
: >"$TMP/aplay.log"
export PATH="$TMP/bin:$PATH" APLAY_LOG="$TMP/aplay.log"
export PLATINE_ASSETS="$PWD/assets" PLATINE_SONS=1
export PLATINE_SONS_JOURNAL="$TMP/sons.log"
: >"$PLATINE_SONS_JOURNAL"
printf 'alpha\n' >"$SRC/a.txt"
printf 'bravo\n' >"$SRC/b.txt"
printf 'contenu dossier\n' >"$SRC/Dossier/imbrique/fichier.txt"

STATE="$HOME_TEST/.config/platine/finder2"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'W\t%s\t430\t40\t350\t360\t0\n' "$DST" >>"$STATE/fenetres"
printf 'I\t%s\ta.txt\t10\t50\n' "$SRC" >"$STATE/icones"
printf 'I\t%s\tb.txt\t94\t114\n' "$SRC" >>"$STATE/icones"
printf 'I\t%s\tDossier\t178\t50\n' "$SRC" >>"$STATE/icones"
printf 'W\t%s/Dossier\t80\t80\t300\t300\t0\n' "$SRC" >>"$STATE/fenetres"
printf '%s/Dossier\tcommentaire dossier\n' "$SRC" >"$STATE/commentaires"
printf '%s/Dossier/imbrique/fichier.txt\tcommentaire imbriqué\n' "$SRC" >>"$STATE/commentaires"
printf '%s/b.txt\tcommentaire document\n' "$SRC" >>"$STATE/commentaires"

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
assert_file() { [ -f "$1" ] || fail "fichier absent: $1"; }
assert_absent() { [ ! -e "$1" ] || fail "chemin encore présent: $1"; }
assert_son() {
    grep -Fx "$1" "$PLATINE_SONS_JOURNAL" >/dev/null ||
        fail "son absent du journal: $1"
    if [ "$1" = lerr ]; then
        grep -F -- '/alertes/simplebeep.wav' "$APLAY_LOG" >/dev/null ||
            fail "alerte non jouée"
    else
        grep -F -- "$1" "$APLAY_LOG" >/dev/null ||
            fail "son non joué: $1"
    fi
}
reset_sons() {
    : >"$PLATINE_SONS_JOURNAL"
    : >"$APLAY_LOG"
}
assert_content() {
    actual=$(cat "$1")
    [ "$actual" = "$2" ] || fail "contenu inattendu dans $1: $actual"
}
assert_pos() {
    awk -F '\t' -v folder="$1" -v name="$2" -v x="$3" -v y="$4" \
        '$1 == "I" && $2 == folder && $3 == name && $4 == x && $5 == y { ok=1 }
         END { exit !ok }' "$STATE/icones" || \
        fail "position absente: $1/$2 ($3,$4)"
}
assert_comment() {
    awk -F '\t' -v path="$1" -v text="$2" \
        '$1 == path && $2 == text { ok=1 } END { exit !ok }' \
        "$STATE/commentaires" || fail "commentaire absent: $1"
}
assert_geom() {
    awk -F '\t' -v path="$1" \
        '$1 == "W" && $2 == path { ok=1 } END { exit !ok }' \
        "$STATE/fenetres" || fail "géométrie absente: $1"
}
drag() {
    xdotool mousemove "$1" "$2" mousedown 1
    sleep 0.1
    xdotool mousemove "$3" "$4"
    sleep 0.1
    xdotool mousemove "$5" "$6"
    sleep 0.1
    xdotool mouseup 1
    sleep 0.5
}

# a.txt : déplacement simple entre deux fenêtres.
# Les points de saisie visent le centre du glyphe 32x32 de l'icône amorcée :
# écran = graine + (6+16, 1+16) dans le client, client à (+6,+22) de la W.
import -window root "$OUT/avant.png"
reset_sons
drag 58 129 350 180 682 241
assert_absent "$SRC/a.txt"
assert_file "$DST/a.txt"
assert_content "$DST/a.txt" 'alpha'
assert_son fdrp

# Lasso sur b.txt + Dossier, puis glissement au-delà du bord gauche.
# Le groupe doit conserver ses 84 px d'écart au lieu d'empiler les icônes.
xdotool mousemove 27 111 mousedown 1 mousemove 292 186 mouseup 1
sleep 0.3
drag 226 129 120 129 46 129
assert_file "$SRC/b.txt"
assert_file "$SRC/Dossier/imbrique/fichier.txt"
assert_pos "$SRC" 'b.txt' 0 114
assert_pos "$SRC" 'Dossier' 84 50

# Le groupe reste sélectionné : déplacement réel vers Destination.
drag 132 129 300 180 582 241
assert_absent "$SRC/b.txt"
assert_absent "$SRC/Dossier"
assert_file "$DST/b.txt"
assert_file "$DST/Dossier/imbrique/fichier.txt"
assert_content "$DST/b.txt" 'bravo'
assert_pos "$DST" 'b.txt' 40 226
assert_pos "$DST" 'Dossier' 124 162
assert_geom "$DST/Dossier"
assert_comment "$DST/Dossier" 'commentaire dossier'
assert_comment "$DST/Dossier/imbrique/fichier.txt" 'commentaire imbriqué'
assert_comment "$DST/b.txt" 'commentaire document'
import -window root "$OUT/deplacement-groupe.png"

# Option/Alt-glisser : copie de b.txt vers Source, source conservée.
xdotool keydown Alt_L
drag 498 305 500 260 222 311
xdotool keyup Alt_L
assert_file "$DST/b.txt"
assert_file "$SRC/b.txt"
assert_content "$SRC/b.txt" 'bravo'
assert_comment "$SRC/b.txt" 'commentaire document'
assert_comment "$DST/b.txt" 'commentaire document'

# Collision : les deux fichiers doivent rester intacts et le groupe ne doit
# pas être partiellement déplacé.
printf 'source-sentinel\n' >"$SRC/b.txt"
printf 'destination-sentinel\n' >"$DST/b.txt"
reset_sons
drag 498 305 500 260 222 311
assert_content "$SRC/b.txt" 'source-sentinel'
assert_content "$DST/b.txt" 'destination-sentinel'
assert_son lerr
if grep -Fx fdrp "$PLATINE_SONS_JOURNAL" >/dev/null; then
    fail "un dépôt refusé joue le son de réussite"
fi

# Relâchement sur le fond du bureau : comme sous Mac OS 9, l'élément va dans
# le dossier du bureau, posé sous le pointeur.
reset_sons
drag 498 305 500 300 400 450
assert_absent "$DST/b.txt"
assert_content "$HOME_TEST/Desktop/b.txt" 'destination-sentinel'
assert_content "$SRC/b.txt" 'source-sentinel'
assert_comment "$HOME_TEST/Desktop/b.txt" 'commentaire document'
awk -F '\t' '$1 == "I" && $2 == "__bureau__" && $3 == "b.txt" { ok=1 } END { exit !ok }' \
    "$STATE/icones" || fail "position de b.txt absente du bureau"
assert_son fdrp
import -window root "$OUT/final.png"

echo 'Glisser-déposer Finder : OK'
echo "Captures et journaux: $OUT"
