#!/bin/sh
# Teste le dossier du bureau (le Desktop Folder de Mac OS 9) dans un HOME jetable :
# création et alias de départ, dossier invisible dans la fenêtre du disque, relecture
# de ce qu'un autre programme y pose, Corbeille en bas à droite, glisser du bureau
# vers une fenêtre, disque immobile, Nouveau dossier sur le bureau.
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder-bureau.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder-bureau}
BUREAU="$HOME_TEST/Desktop"
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$HOME_TEST/Documents" "$HOME_TEST/Applications" "$SRC" "$OUT/log" "$STATE"
export PLATINE_ASSETS="$PWD/assets" PLATINE_SONS=1
export PLATINE_SONS_JOURNAL="$TMP/sons.log"
: >"$PLATINE_SONS_JOURNAL"
printf 'W\t%s\t20\t40\t350\t300\t0\n' "$SRC" >"$STATE/fenetres"
printf 'W\t%s\t20\t40\t350\t300\t0\n' "$HOME_TEST" >>"$STATE/fenetres"

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

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    import -window root "$OUT/echec.png" 2>/dev/null || true
    trap - EXIT
    cleanup
    exit 1
}
# position enregistrée d'une icône (dossier ou « __bureau__ ») : « x y »
pos() {
    awk -F '\t' -v folder="$1" -v name="$2" \
        '$1 == "I" && $2 == folder && $3 == name { print $4, $5; exit }' \
        "$STATE/icones"
}
attendre_pos() {
    i=0
    while [ -z "$(pos "$1" "$2")" ]; do
        i=$((i + 1))
        [ "$i" -lt 60 ] || fail "icône $2 absente de $1"
        sleep 0.1
    done
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

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-finder "$SRC" >"$OUT/log/finder.log" 2>&1 &
APP=$!
timeout 10 xdotool search --sync --onlyvisible --name '^Source$' >/dev/null || fail 'fenêtre Source absente'
sleep 0.5

# 1. Premier lancement : le dossier est créé, avec les alias des anciens dossiers fixes.
[ -d "$BUREAU" ] || fail 'dossier du bureau non créé'
[ -L "$BUREAU/Documents" ] || fail 'alias Documents absent du bureau'
[ -L "$BUREAU/Applications" ] || fail 'alias Applications absent du bureau'
[ ! -e "$BUREAU/Images" ] || fail 'alias Images posé sans dossier Images'
attendre_pos __bureau__ Documents
# 2. La Corbeille, sans place enregistrée, est dans le coin bas droit ; le disque en haut.
[ "$(pos __bureau__ Corbeille)" = '735 344' ] || fail "Corbeille hors du coin bas droit: $(pos __bureau__ Corbeille)"
[ "$(pos __bureau__ 'Eee PC 701')" = '735 24' ] || fail 'disque hors du coin haut droit'
echo 'Création et disposition : OK'

# 3. Un fichier posé par un autre programme paraît sur le bureau (relecture ≤ 2 s).
printf 'note\n' >"$BUREAU/note.txt"
attendre_pos __bureau__ note.txt
echo 'Relecture du dossier : OK'

# 4. Glisser note.txt du bureau vers la fenêtre Source : il quitte le dossier du bureau.
set -- $(pos __bureau__ note.txt)
NX=$(($1 + 16)); NY=$(($2 + 20 + 16))
import -window root "$OUT/avant.png"
drag "$NX" "$NY" 300 200 180 200
[ -f "$SRC/note.txt" ] || fail 'note.txt non déposé dans Source'
[ ! -e "$BUREAU/note.txt" ] || fail 'note.txt encore dans le dossier du bureau'
attendre_pos "$SRC" note.txt
echo 'Bureau vers fenêtre : OK'

# 4 bis. note.txt déposé sur l'alias d'un programme posé sur le bureau : le programme
# l'ouvre (il reçoit le chemin), le document reste en place.
cat >"$HOME_TEST/Applications/Ouvreur" <<FIN
#!/bin/sh
printf '%s\n' "\$@" >"$TMP/ouvreur.txt"
FIN
chmod +x "$HOME_TEST/Applications/Ouvreur"
ln -s "$HOME_TEST/Applications/Ouvreur" "$BUREAU/Ouvreur"
attendre_pos __bureau__ Ouvreur
set -- $(pos "$SRC" note.txt)
SX=$((20 + $1 + 28)); SY=$((40 + $2 + 39))
set -- $(pos __bureau__ Ouvreur)
drag "$SX" "$SY" 500 200 $(($1 + 16)) $(($2 + 20 + 16))
i=0
while [ ! -s "$TMP/ouvreur.txt" ]; do
    i=$((i + 1)); [ "$i" -lt 30 ] || fail "le programme n'a pas reçu le document"
    sleep 0.1
done
[ "$(cat "$TMP/ouvreur.txt")" = "$SRC/note.txt" ] || fail "chemin reçu: $(cat "$TMP/ouvreur.txt")"
[ -f "$SRC/note.txt" ] || fail 'note.txt a quitté Source'
echo 'Dépôt sur un programme : OK'

# 4 ter. Du bureau au bureau : Documents déposé sur l'alias Ouvreur. Les icônes
# glissées ne masquent pas la cible ; Documents revient à sa place.
: >"$TMP/ouvreur.txt"
DOC0=$(pos __bureau__ Documents)
set -- $DOC0
DX=$(($1 + 16)); DY=$(($2 + 20 + 16))
set -- $(pos __bureau__ Ouvreur)
drag "$DX" "$DY" 500 200 $(($1 + 16)) $(($2 + 20 + 16))
i=0
while [ ! -s "$TMP/ouvreur.txt" ]; do
    i=$((i + 1)); [ "$i" -lt 30 ] || fail "Documents déposé sur le programme du bureau : rien"
    sleep 0.1
done
[ "$(cat "$TMP/ouvreur.txt")" = "$BUREAU/Documents" ] || fail "chemin reçu: $(cat "$TMP/ouvreur.txt")"
[ "$(pos __bureau__ Documents)" = "$DOC0" ] || fail "Documents déplacé: $(pos __bureau__ Documents) au lieu de $DOC0"
echo 'Bureau sur programme du bureau : OK'

# 4 quater. Documents posé au milieu, puis ramené dans la colonne de droite : il y
# retrouve sa case (la colonne de droite n'est pas hors d'atteinte).
drag "$DX" "$DY" 600 250 400 250
[ "$(pos __bureau__ Documents)" != "$DOC0" ] || fail 'Documents non déplacé au milieu'
set -- $(pos __bureau__ Documents)
drag $(($1 + 16)) $(($2 + 36)) 600 150 "$DX" "$DY"
[ "$(pos __bureau__ Documents)" = "$DOC0" ] || fail "Documents non revenu dans sa case: $(pos __bureau__ Documents)"
echo 'Retour dans la colonne de droite : OK'

# 5. Le disque ne quitte pas le bureau.
: >"$PLATINE_SONS_JOURNAL"
drag 751 60 500 200 180 260
grep -Fx lerr "$PLATINE_SONS_JOURNAL" >/dev/null || fail 'déplacer le disque devrait être refusé'
[ -d "$HOME_TEST/Source" ] && [ ! -e "$SRC/Eee PC 701" ] || fail 'le disque a été déplacé'
echo 'Disque immobile : OK'

# 6. Nouveau dossier (⌘N) avec le bureau au premier plan : créé dans le dossier du bureau.
xdotool mousemove 500 440 click 1
sleep 0.4
xdotool key alt+n
sleep 0.6
[ -d "$BUREAU/Nouveau dossier" ] || fail 'Nouveau dossier non créé sur le bureau'
attendre_pos __bureau__ 'Nouveau dossier'
echo 'Nouveau dossier sur le bureau : OK'

# 7. La fenêtre du disque ne montre pas le dossier du bureau (Desktop Folder invisible).
kill "$APP"; wait "$APP" 2>/dev/null || true
./build/platine-finder "$HOME_TEST" >>"$OUT/log/finder.log" 2>&1 &
APP=$!
timeout 10 xdotool search --sync --onlyvisible --name '^Eee PC 701$' >/dev/null || fail 'fenêtre du disque absente'
sleep 0.5
attendre_pos "$HOME_TEST" Source
[ -z "$(pos "$HOME_TEST" Desktop)" ] || fail 'le dossier du bureau paraît dans la fenêtre du disque'
import -window root "$OUT/final.png"
echo 'Dossier du bureau invisible : OK'

echo 'Dossier du bureau : OK'
echo "Captures et journaux: $OUT"
