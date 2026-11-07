#!/bin/sh
# TextEdit : chargement, édition, enregistrement, presse-papiers X11 partagé
# avec la Toolbox, alerte de fermeture de Mac OS 9 et lancement depuis le Finder.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder build/platine-textedit

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-textedit.XXXXXX)
cc -O2 -Wall -Wextra -o "$TMP/clipboard-client" tests/clipboard-client.c -lX11
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/textedit}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
DOC="$HOME_TEST/notes.txt"
mkdir -p "$SRC" "$OUT/log" "$STATE"
printf 'premiere\n' >"$DOC"
printf 'lisez moi\n' >"$SRC/lisezmoi.txt"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'I\t%s\tlisezmoi.txt\t10\t50\n' "$SRC" >"$STATE/icones"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
TE=
FINDER=
CLIP=
cleanup() {
    [ -n "$CLIP" ] && kill "$CLIP" 2>/dev/null || true
    [ -n "$TE" ] && kill "$TE" 2>/dev/null || true
    [ -n "$FINDER" ] && kill "$FINDER" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$CLIP" 2>/dev/null || true
    wait "$TE" 2>/dev/null || true
    wait "$FINDER" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1))
    [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
D=:$(cat "$TMP/display")
export DISPLAY="$D"
export HOME="$HOME_TEST"

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-textedit "$DOC" >"$OUT/log/textedit.log" 2>&1 &
TE=$!
xdotool search --sync --onlyvisible --name '^notes.txt$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
present() { xdotool search --name "^$1\$" >/dev/null 2>&1; }
attendre_absent() {
    i=0; while present "$1"; do i=$((i+1)); [ $i -lt 50 ] || return 1; sleep 0.1; done
}
activer() {
    WID=$(xdotool search --name "^$1\$" | head -1)
    xdotool mousemove --window "$WID" 240 240 click 1
    sleep 0.3
}
cmd() {
    xdotool keydown alt
    sleep 0.15
    xdotool key "$1"
    sleep 0.1
    xdotool keyup alt
    sleep 0.2
}

# 1) Chargement : Tout sélectionner (⌘A) puis Copier (⌘C) publie le texte
#    dans CLIPBOARD, lisible par un client X11 quelconque.
activer notes.txt
cmd a
cmd c
timeout 5 "$TMP/clipboard-client" get-text >"$TMP/clip.txt" ||
    fail "TextEdit ne publie pas CLIPBOARD en UTF8_STRING"
[ "$(cat "$TMP/clip.txt")" = 'premiere' ] ||
    fail "contenu publié inattendu : $(cat "$TMP/clip.txt")"

# 2) Coller (⌘V) depuis un client X11 : la pseudo-sélection est remplacée
#    (⌘ maintenue ne l'efface pas, comme sous Mac OS 9).
# entrant.txt reste hors de Source : le Finder y auto-place les nouveaux
# fichiers et décalerait lisezmoi.txt de l'étape 7.
printf 'colle moi\n' >"$HOME_TEST/entrant.txt"
"$TMP/clipboard-client" own-contenu "$HOME_TEST/entrant.txt" & CLIP=$!
sleep 0.3
cmd v
sleep 0.5
kill "$CLIP"; wait "$CLIP" 2>/dev/null || true; CLIP=
cmd s
sleep 0.3
[ "$(cat "$DOC")" = "$(printf 'colle moi')" ] ||
    fail "le collage n'a pas remplacé le texte : $(cat "$DOC")"

# 3) Saisie puis Enregistrer (⌘S).
xdotool type " et voila"
sleep 0.3
cmd s
sleep 0.3
[ "$(cat "$DOC")" = "$(printf 'colle moi\n et voila')" ] ||
    fail "enregistrement inattendu : $(cat "$DOC")"

# 4) Fermer (⌘W) avec modifications : alerte, Échap = Annuler, rien ne bouge.
xdotool type "x"
sleep 0.3
cmd w
sleep 0.5
present notes.txt || fail "la fenêtre a fermé sans demander"
import -window root "$OUT/fermeture-alerte.png"
xdotool key Escape
sleep 0.5
present notes.txt || fail "Annuler a fermé la fenêtre"
[ "$(cat "$DOC")" = "$(printf 'colle moi\n et voila')" ] || fail "Annuler a modifié le fichier"

# 5) Retour = Enregistrer : le document est écrit puis la fenêtre se ferme.
cmd w
sleep 0.5
import -window root "$OUT/fermeture-enregistrer.png"
xdotool key Return
attendre_absent notes.txt || fail "Enregistrer n'a pas fermé la fenêtre"
[ "$(cat "$DOC")" = "$(printf 'colle moi\n et voilax')" ] ||
    fail "Enregistrer n'a pas écrit : $(cat "$DOC")"

# 6) « Ne pas enregistrer » au clic : la fenêtre ferme, le fichier reste.
./build/platine-textedit "$DOC" >>"$OUT/log/textedit.log" 2>&1 & TE=$!
xdotool search --sync --onlyvisible --name '^notes.txt$' >/dev/null
sleep 0.5
activer notes.txt
xdotool type "z"
sleep 0.3
cmd w
sleep 0.5
import -window root "$OUT/fermeture-pas-enregistrer.png"
xdotool mousemove 304 309 click 1
attendre_absent notes.txt || fail "« Ne pas enregistrer » n'a pas fermé la fenêtre"
[ "$(cat "$DOC")" = "$(printf 'colle moi\n et voilax')" ] ||
    fail "« Ne pas enregistrer » a modifié le fichier"

# 7) Le Finder ouvre les documents texte avec TextEdit.
./build/platine-finder "$SRC" >"$OUT/log/finder.log" 2>&1 & FINDER=$!
xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
sleep 0.8
# position réelle du client selon le serveur (le WM peut l'honorer ou la
# déplacer ; xdotool rapporte autre chose), puis glyphe de l'icône (10,50) :
# centre = client + icône + (22,17)
SWID=$(xdotool search --name '^Source$' | head -1)
SX=$(xwininfo -id "$SWID" | sed -n 's/^ *Absolute upper-left X: *//p')
SY=$(xwininfo -id "$SWID" | sed -n 's/^ *Absolute upper-left Y: *//p')
xdotool mousemove $((SX + 32)) $((SY + 67)) click --repeat 2 --delay 200 1
timeout 10 xdotool search --sync --onlyvisible --name '^lisezmoi.txt$' >/dev/null ||
    fail "le double-clic n'a pas ouvert lisezmoi.txt dans TextEdit"
sleep 0.5
import -window root "$OUT/finder-ouverture.png"

# 8) Nouveau (⌘N) sans modification : pas d'alerte, « Sans titre ».
activer lisezmoi.txt
cmd n
sleep 0.3
present 'Sans titre' || fail "Nouveau n'a pas remis la fenêtre à Sans titre"
import -window root "$OUT/final.png"

echo 'TextEdit : édition, enregistrement, presse-papiers et Finder : OK'
echo "Captures et journaux: $OUT"
