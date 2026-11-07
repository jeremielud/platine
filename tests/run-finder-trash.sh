#!/bin/sh
# Teste le dépôt réel sur la Corbeille du bureau dans un HOME jetable.
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-trash.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-trash}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
TRASH="$HOME_TEST/.local/share/Trash"
mkdir -p "$SRC/Dossier/imbrique" "$OUT/log" "$STATE"
printf 'alpha\n' >"$SRC/a.txt"
printf 'bravo\n' >"$SRC/b.txt"
printf 'charlie\n' >"$SRC/c.txt"
printf 'contenu dossier\n' >"$SRC/Dossier/imbrique/fichier.txt"

printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'I\t%s\ta.txt\t10\t50\n' "$SRC" >"$STATE/icones"
printf 'I\t%s\tb.txt\t138\t114\n' "$SRC" >>"$STATE/icones"
printf 'I\t%s\tDossier\t178\t50\n' "$SRC" >>"$STATE/icones"
printf 'I\t%s\tc.txt\t10\t118\n' "$SRC" >>"$STATE/icones"
printf '%s/Dossier\tcommentaire dossier\n' "$SRC" >"$STATE/commentaires"
printf '%s/Dossier/imbrique/fichier.txt\tcommentaire imbriqué\n' "$SRC" \
    >>"$STATE/commentaires"
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
./build/platine-finder "$SRC" >"$OUT/log/finder.log" 2>&1 &
APP=$!
xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
assert_absent() { [ ! -e "$1" ] || fail "chemin encore présent: $1"; }
assert_comment() {
    awk -F '\t' -v path="$1" -v text="$2" \
        '$1 == path && $2 == text { ok=1 } END { exit !ok }' \
        "$STATE/commentaires" || fail "commentaire absent: $1"
}
assert_icon() {
    awk -F '\t' -v folder="$1" -v name="$2" \
        '$1 == "I" && $2 == folder && $3 == name { ok=1 } END { exit !ok }' \
        "$STATE/icones" || fail "icône nommée $2 absente de $1"
}
trash_file() {
    set -- "$TRASH/files/$1"-*
    [ -e "$1" ] || fail "absent de la corbeille: $1"
    printf '%s\n' "$1"
}

# Le bureau est seul : la Corbeille occupe le coin bas droit (735,364).
# b.txt est déposé dessus, maintenu un instant pour capturer le survol.
xdotool mousemove 186 193 mousedown 1
sleep 0.1
xdotool mousemove 400 200
sleep 0.1
xdotool mousemove 751 381
# Moins de 700 ms : le délai des dossiers à ressort ouvrirait la Corbeille
# avant le relâchement et le dépôt n'aurait plus de cible.
sleep 0.2
import -window root "$OUT/survol-corbeille.png"
xdotool mouseup 1
sleep 0.5

assert_absent "$SRC/b.txt"
item=$(trash_file 'b.txt')
[ -f "$item" ] || fail "b.txt n'est pas un fichier dans la corbeille"
[ "$(cat "$item")" = 'bravo' ] || fail 'contenu de b.txt incorrect'
info="$TRASH/info/${item##*/}.trashinfo"
[ -f "$info" ] || fail "fichier .trashinfo absent: $info"
grep -F "Path=$SRC/b.txt" "$info" >/dev/null || \
    fail ".trashinfo sans chemin d'origine"
grep -F '[Trash Info]' "$info" >/dev/null || fail '.trashinfo sans en-tête'
assert_comment "$item" 'commentaire document'

# Dossier complet : contenu et commentaire suivis.
xdotool mousemove 226 129 mousedown 1
sleep 0.1
xdotool mousemove 500 210
sleep 0.1
xdotool mousemove 751 381
sleep 0.1
xdotool mouseup 1
sleep 0.5

assert_absent "$SRC/Dossier"
item=$(trash_file 'Dossier')
[ -d "$item" ] || fail "Dossier n'est pas un répertoire dans la corbeille"
[ -f "$item/imbrique/fichier.txt" ] || fail 'contenu du dossier perdu'
[ "$(cat "$item/imbrique/fichier.txt")" = 'contenu dossier' ] || \
    fail 'contenu du dossier altéré'
[ -f "$TRASH/info/${item##*/}.trashinfo" ] || \
    fail 'fichier .trashinfo du dossier absent'
assert_comment "$item" 'commentaire dossier'

# c.txt : jeté puis ressorti de la corbeille par glisser-déposer.
xdotool mousemove 58 197 mousedown 1
sleep 0.1
xdotool mousemove 400 220
sleep 0.1
xdotool mousemove 751 381
sleep 0.1
xdotool mouseup 1
sleep 0.5
assert_absent "$SRC/c.txt"
item=$(trash_file 'c.txt')
[ "$(cat "$item")" = 'charlie' ] || fail 'contenu de c.txt incorrect'
import -window root "$OUT/final.png"

# Vider la corbeille demande confirmation ; Annuler (Échap) ne supprime rien.
xdotool mousemove 330 10 click 1
sleep 0.3
import -window root "$OUT/menu-special.png"
xdotool mousemove 340 28 click 1
sleep 0.5
import -window root "$OUT/alerte-vider.png"
# Échap = bouton Annuler (clavier, indépendant de la géométrie de l'alerte).
xdotool key Escape
sleep 0.5
[ -e "$(trash_file 'b.txt')" ] || fail 'Annuler a vidé la corbeille'
[ -e "$(trash_file 'Dossier')" ] || fail 'Annuler a supprimé le dossier'

echo 'Corbeille par glisser-déposer : OK'
echo "Captures et journaux: $OUT"

# --- Restauration depuis la corbeille -------------------------------------
# Double-clic sur le glyphe de l'icône Corbeille du bureau (32×32 à 735,216
# dans la fenêtre bureau, qui commence sous la barre de menus : +20 en y).
xdotool mousemove 751 381
sleep 0.2
xdotool click --repeat 2 --delay 150 1
if ! timeout 5 xdotool search --sync --onlyvisible --name '^Corbeille$' >/dev/null; then fail 'la fenêtre Corbeille ne s ouvre pas'; fi
sleep 0.5

TID=$(xdotool search --onlyvisible --name '^Corbeille$' | head -n 1)
eval "$(xdotool getwindowgeometry --shell "$TID")"

# Les noms affichés sont les noms d'origine, sans horodatage.
assert_icon "$TRASH/files" 'c.txt'

# c.txt ressort de la corbeille vers la fenêtre Source.
set -- $(awk -F '\t' -v folder="$TRASH/files" -v name='c.txt' \
    '$1 == "I" && $2 == folder && $3 == name { print $4, $5; exit }' \
    "$STATE/icones")
[ $# -eq 2 ] || fail 'position de c.txt absente dans la corbeille'
CTT_X=$((X + $1  + 22))
CTT_Y=$((Y + $2  + 17))

# Lâcher sur le bureau hors icône : comme sous Mac OS 9, l'élément se pose sur
# le bureau, dans son dossier (~/Bureau) ; la remise en place est Fichier ▸ Ranger.
xdotool mousemove "$CTT_X" "$CTT_Y" mousedown 1
sleep 0.1
xdotool mousemove 560 220
sleep 0.1
xdotool mousemove 600 300
sleep 0.1
xdotool mouseup 1
sleep 0.5
[ -f "$HOME_TEST/Desktop/c.txt" ] || fail 'c.txt non posé sur le bureau'
[ "$(cat "$HOME_TEST/Desktop/c.txt")" = 'charlie' ] || fail 'contenu de c.txt altéré'
assert_icon __bureau__ 'c.txt'
[ -z "$(ls "$TRASH"/info | grep -i '^c\.txt-' || true)" ] || \
    fail '.trashinfo de c.txt encore présent'

# Titre de la fenêtre : focus sans modifier la sélection.
xdotool mousemove $((X + WIDTH / 2)) $((Y + 8)) click 1
sleep 0.3
# Les dossiers sont triés avant les fichiers : Dossier, puis b.txt.
xdotool key Right Right
sleep 0.2
import -window root "$OUT/corbeille.png"

xdotool key alt+y
sleep 0.6
[ -f "$SRC/b.txt" ] || fail 'b.txt non restauré'
[ "$(cat "$SRC/b.txt")" = 'bravo' ] || fail 'contenu restauré incorrect'
assert_comment "$SRC/b.txt" 'commentaire document'
[ -z "$(ls "$TRASH"/info | grep -i '^b\.txt-' || true)" ] || \
    fail '.trashinfo de b.txt encore présent'

xdotool mousemove $((X + WIDTH / 2)) $((Y + 8)) click 1
sleep 0.2
xdotool key Right
xdotool key alt+y
sleep 0.6
[ -d "$SRC/Dossier" ] || fail 'Dossier non restauré'
[ -f "$SRC/Dossier/imbrique/fichier.txt" ] || fail 'contenu restauré perdu'
assert_comment "$SRC/Dossier" 'commentaire dossier'
[ -z "$(ls "$TRASH"/files)" ] || fail 'la corbeille devrait être vide'
import -window root "$OUT/restauration.png"

# --- Vidage confirmé ------------------------------------------------------
# La fenêtre Corbeille recouvre Source : on la ferme (⌘W) pour rendre
# Source entièrement visible et éviter que les clics partent ailleurs.
xdotool mousemove $((X + WIDTH / 2)) $((Y + 8)) click 1
sleep 0.3
xdotool key alt+w
sleep 0.5
eval "$(xdotool search --onlyvisible --name '^Source$' \
    getwindowgeometry --shell)"
xdotool mousemove $((X + WIDTH / 2)) $((Y + 8)) click 1
sleep 0.3
# Nouveau dossier dans Source, puis jeté et corbeille vidée pour de bon.
# Raccourci ⌘N acheminé par le WM (Mod1 = ⌘).
xdotool key alt+n
sleep 0.6
[ -d "$SRC/Nouveau dossier" ] || fail 'Nouveau dossier non créé'
set -- $(awk -F '\t' -v folder="$SRC" -v name='Nouveau dossier' \
    '$1 == "I" && $2 == folder && $3 == name { print $4, $5; exit }' \
    "$STATE/icones")
[ $# -eq 2 ] || fail 'position du nouveau dossier absente'
xdotool mousemove $((X + $1  + 22)) $((Y + $2  + 17)) mousedown 1
sleep 0.1
xdotool mousemove 500 220
sleep 0.1
xdotool mousemove 751 381
sleep 0.1
xdotool mouseup 1
sleep 0.5
[ -z "$(ls "$TRASH"/files)" ] && fail 'le dossier jeté est absent'
xdotool mousemove 330 10 click 1
sleep 0.3
xdotool mousemove 340 28 click 1
sleep 0.5
# Bouton par défaut (Vider/OK) : Entrée.
xdotool key Return
sleep 0.6
[ -z "$(ls "$TRASH"/files)" ] || fail "la corbeille n'a pas été vidée"
[ -z "$(ls "$TRASH"/info)" ] || fail 'les .trashinfo subsistent après vidage'
import -window root "$OUT/vidage.png"

echo 'Restauration depuis la corbeille : OK'
echo 'Vidage confirmé de la corbeille : OK'
