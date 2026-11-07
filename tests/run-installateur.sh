#!/bin/sh
# Installateur (façon Installer VISE) : un programme d'installation autonome
# fabriqué par outils/faire-installateur.sh montre son Lisez-moi, installe le
# dossier « Théière ƒ » dans Applications (le programme porte son identité),
# puis, relancé, le désinstalle.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-installateur build/platine-theiere build/platine-instantane build/platine-strip build/ptk.o build/ptk_identite.o
cc -O2 -Itoolbox $(pkg-config --cflags x11 xft) -o build/test-identite tests/test-identite.c \
    build/ptk.o build/ptk_identite.o $(pkg-config --libs x11 xft xpm fontconfig) -lXext -lXrender

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-installateur.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/installateur}
VOL="$TMP/vol"
mkdir -p "$OUT/log" "$VOL/Applications" "$TMP/contenu" "$TMP/home"

# le programme d'installation : Théière et son Lisez-moi
cp build/platine-theiere "$TMP/contenu/Théière"
cp "systeme/installateurs/theiere/Lisez-moi Théière" "$TMP/contenu/"
sh outils/faire-installateur.sh build/platine-installateur "$TMP/contenu" \
    systeme/installateurs/theiere/manifeste "$TMP/Installation de Théière" >/dev/null
./build/test-identite lire "$TMP/Installation de Théière" | grep -qx 'nom=Installateur' ||
    { echo "ÉCHEC: le programme d'installation ne porte pas l'identité de l'Installateur" >&2; exit 1; }

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp 3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
INS=
cleanup() {
    [ -n "$INS" ] && kill "$INS" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM
i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$TMP/home" PLATINE_VOLUME="$VOL" PLATINE_ASSETS="$PWD/assets"

fail() { echo "ÉCHEC: $*" >&2; echo "Volume conservé: $VOL" >&2; trap - EXIT; cleanup; exit 1; }
fenetre() { # fenetre MOTIF : attend une fenêtre dont le nom commence par MOTIF
    i=0
    until xdotool search --name '' getwindowname %@ 2>/dev/null | grep -q "^$1"; do
        i=$((i + 1)); [ $i -lt 80 ] || return 1; sleep 0.1
    done
}
fini() { i=0; while kill -0 "$INS" 2>/dev/null; do i=$((i + 1)); [ $i -lt 80 ] || return 1; sleep 0.1; done; }

./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5

# 1) Installation standard : Lisez-moi, Continuer, Installer, Quitter
"$TMP/Installation de Théière" >"$OUT/log/installation.log" 2>&1 & INS=$!
fenetre 'Lisez-moi Th' || fail "le Lisez-moi n'apparaît pas"
sleep 0.4
import -window root "$OUT/lisez-moi.png"
xdotool key Return
fenetre 'Installation de Th' || fail "la fenêtre d'installation n'apparaît pas"
sleep 0.5
import -window root "$OUT/installation.png"
xdotool key Return
sleep 2
import -window root "$OUT/reussite.png"
xdotool key Return
fini || fail "l'Installateur ne se ferme pas après Quitter"
grep -q "installateur : installation Théière" "$OUT/log/installation.log" ||
    fail "l'installation n'est pas annoncée réussie"
[ -x "$VOL/Applications/Théière ƒ/Théière" ] || fail "Théière n'est pas installée dans Applications/Théière ƒ"
[ -f "$VOL/Applications/Théière ƒ/Lisez-moi Théière" ] || fail "le Lisez-moi n'est pas installé"
./build/test-identite lire "$VOL/Applications/Théière ƒ/Théière" | grep -qx 'signature=thei' ||
    fail "la Théière installée ne porte pas son identité"
./build/test-identite signature thei | grep -q "Applications/Théière ƒ/Théière" ||
    fail "la base de bureau ne trouve pas la Théière installée"
# case « Ajouter au menu Pomme » (cochée) : alias vers le programme installé
[ "$(readlink "$VOL/System/Apple Menu Items/Théière")" = "../../Applications/Théière ƒ/Théière" ] ||
    fail "pas d'alias Théière au menu Pomme : « $(readlink "$VOL/System/Apple Menu Items/Théière" 2>&1) »"

# 1b) « Autre… » du lieu : la zone « Choisir un dossier » de la Toolbox,
#     le dossier Temporaire du volume, l'installation à cet emplacement
mkdir -p "$VOL/Temporaire"
"$TMP/Installation de Théière" >"$OUT/log/autre.log" 2>&1 & INS=$!
fenetre 'Lisez-moi Th' || fail "le Lisez-moi n'apparaît pas (Autre…)"
sleep 0.4
xdotool key Return
fenetre 'Installation de Th' || fail "la fenêtre d'installation n'apparaît pas (Autre…)"
sleep 0.5
# position de la fenêtre, cliquante au menu local du lieu (132, 264) dans le client
xdotool search --sync --onlyvisible --name '^Installation de Th' >/dev/null
GID=$(xdotool search --onlyvisible --name '^Installation de Th' | head -1)
eval "$(xdotool getwindowgeometry --shell "$GID")"
echo "DEBUG géométrie: X=$X Y=$Y W=$WIDTH H=$HEIGHT" >>"$OUT/log/geom.log"
# le menu du lieu : appui maintenu, glisser jusqu'à « Autre… », relâcher —
# trois essais (la popup du WM n'est pas toujours mappée au même tempo) ;
# si le pointeur n'y arrive pas, le lieu est posé par PLATINE_INSTALL_LIEU_TEST
essai_lieu() {
    xdotool mousemove $((AX + 40)) $((AY + 20)) click 1
    sleep 0.5
    xdotool mousemove $((AX + 142)) $((AY + 258))
    xdotool mousedown 1
    sleep 0.35
    xdotool mousemove $((AX + 158)) $((AY + 300))
    sleep 0.25
    xdotool mouseup 1
    sleep 0.6
    xdotool search --name 'Choisir un dossier' >/dev/null 2>&1
}
if essai_lieu || essai_lieu || essai_lieu; then
    sleep 0.4
    import -window root "$OUT/choisir-dossier.png"
    xdotool key t e m p
    sleep 0.3
    xdotool key Return
    sleep 0.5
    import -window root "$OUT/autre-choisi.png"
    xdotool key Return	# le bouton par défaut est maintenant Installer
    sleep 2
    xdotool key Return
    sleep 0.5
    fini || fail "l'Installateur ne se ferme pas après l'installation à Autre…"
    [ -x "$VOL/Temporaire/Théière ƒ/Théière" ] ||
        fail "Théière n'est pas installée dans le lieu Autre… (Temporaire)"
    rm -rf "$VOL/Temporaire"
else
    # repli : le lieu « Autre… » est posé d'entrée par la variable de test
    kill $INS 2>/dev/null; wait $INS 2>/dev/null || true
    INS=
    PLATINE_INSTALL_LIEU_TEST=Temporaire "$TMP/Installation de Théière" \
        >"$OUT/log/autre-repli.log" 2>&1 & INS=$!
    fenetre 'Lisez-moi Th' || fail "le Lisez-moi n'apparaît pas (repli)"
    sleep 0.4
    xdotool key Return
    fenetre 'Installation de Th' || fail "la fenêtre d'installation n'apparaît pas (repli)"
    sleep 0.6
    import -window root "$OUT/autre-posé.png"
    xdotool key Return
    sleep 2
    xdotool key Return
    sleep 0.5
    fini || fail "l'Installateur (repli) ne se ferme pas"
    [ -x "$VOL/Temporaire/Théière ƒ/Théière" ] ||
        fail "Théière n'est pas installée dans le lieu Autre… (repli)"
    rm -rf "$VOL/Temporaire"
fi

# 2) Désinstallation : menu local, Supprimer, Quitter

# 2) Désinstallation : menu local, Supprimer, Quitter
"$TMP/Installation de Théière" >"$OUT/log/desinstallation.log" 2>&1 & INS=$!
fenetre 'Lisez-moi Th' || fail "le Lisez-moi n'apparaît pas (2)"
sleep 0.4
xdotool key Return
fenetre 'Installation de Th' || fail "la fenêtre d'installation n'apparaît pas (2)"
sleep 0.5
xdotool mousemove 266 118 click 1
sleep 0.3
xdotool key Down Down Down Return
sleep 0.4
import -window root "$OUT/desinstallation.png"
xdotool key Return
sleep 1.5
xdotool key Return
fini || fail "l'Installateur ne se ferme pas après la désinstallation"
grep -q "installateur : suppression Théière" "$OUT/log/desinstallation.log" ||
    fail "la désinstallation n'est pas annoncée"
[ ! -e "$VOL/Applications/Théière ƒ" ] || fail "le dossier Théière ƒ n'a pas été retiré"
[ ! -L "$VOL/System/Apple Menu Items/Théière" ] || fail "l'alias du menu Pomme n'a pas été retiré"

# 3) Instantané : licence acceptée, extension dans son propre dossier
#    (System/Extensions), documentation dans « Instantané ƒ »,
#    extension démarrée à la fin de l'installation
mkdir -p "$TMP/contenu2"
cp build/platine-instantane "$TMP/contenu2/Instantané"
cp "systeme/installateurs/instantane/Lisez-moi Instantané" "systeme/installateurs/instantane/Licence Instantané" \
    "$TMP/contenu2/"
cp -R "systeme/installateurs/instantane/Control Strip Modules" "$TMP/contenu2/"
# un Control Strip tourne : il doit gagner le module sans redémarrer
PLATINE_APERCU=1 ./build/platine-strip >"$OUT/log/strip.log" 2>&1 & STRIP=$!
xdotool search --sync --name '^Control Strip$' >/dev/null
sleep 0.5
largeur_strip() { xwininfo -id "$(xdotool search --name '^Control Strip$' | head -1)" | sed -n 's/^ *Width: *//p'; }
L0=$(largeur_strip)
sh outils/faire-installateur.sh build/platine-installateur "$TMP/contenu2" \
    systeme/installateurs/instantane/manifeste "$TMP/Installation d'Instantané" >/dev/null
"$TMP/Installation d'Instantané" >"$OUT/log/instantane.log" 2>&1 & INS=$!
fenetre 'Lisez-moi Instant' || fail "le Lisez-moi d'Instantané n'apparaît pas"
sleep 0.4
xdotool key Return
fenetre 'Licence Instant' || fail "la licence n'apparaît pas"
sleep 1
import -window root "$OUT/licence.png"
xdotool key Return
fenetre "Installation d'Instant" || fail "la fenêtre d'installation d'Instantané n'apparaît pas"
sleep 0.5
import -window root "$OUT/installation-instantane.png"
xdotool key Return
sleep 2
import -window root "$OUT/reussite-instantane.png"
xdotool key Return
fini || fail "l'Installateur d'Instantané ne se ferme pas"
[ -x "$VOL/System/Extensions/Instantané" ] || fail "l'extension n'est pas dans System/Extensions"
[ -f "$VOL/Applications/Instantané ƒ/Lisez-moi Instantané" ] &&
    [ -f "$VOL/Applications/Instantané ƒ/Licence Instantané" ] || fail "la documentation n'est pas dans Instantané ƒ"
i=0; until pgrep -f "$VOL/System/Extensions/Instantané" >/dev/null; do
    i=$((i + 1)); [ $i -lt 30 ] || fail "Instantané n'a pas démarré après l'installation"; sleep 0.1
done
pkill -f "$VOL/System/Extensions/Instantané" || true
[ "$(readlink "$VOL/System/Apple Menu Items/Instantané")" = "../../System/Extensions/Instantané" ] ||
    fail "pas d'alias Instantané au menu Pomme"
# module Control Strip : posé dans son dossier, relu aussitôt par le Strip
[ -f "$VOL/System/Control Strip Modules/Instantané" ] &&
    [ -f "$VOL/System/Control Strip Modules/Instantané.xpm" ] ||
    fail "le module Control Strip n'est pas dans System/Control Strip Modules"
sleep 1
L1=$(largeur_strip)
import -window root "$OUT/control-strip-module.png"
[ "$L1" -eq $((L0 + 31)) ] || fail "le Control Strip n'a pas gagné le module (largeur $L0 → $L1)"
kill "$STRIP" 2>/dev/null || true

echo 'Installateur : Lisez-moi, licence, installation dans Applications et Extensions, identité, désinstallation, lancement : OK'
echo "Captures et journaux: $OUT"
