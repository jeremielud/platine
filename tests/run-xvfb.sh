#!/bin/sh
# Lance platine-wm et platine-test dans Xvfb 800x480, capture le résultat.
set -eu
cd "$(dirname "$0")/.."

make

D=":9$$"
OUT=${OUT:-/tmp/platine-tests}
mkdir -p "$OUT" "$OUT/log"

Xvfb "$D" -screen 0 800x480x24 -nolisten tcp >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
XTERM=
cleanup() {
	[ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
	[ -n "${XTERM:-}" ] && kill "$XTERM" 2>/dev/null || true
	[ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
	kill "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1
DISPLAY=$D ./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.7
DISPLAY=$D xterm -geometry 50x12+300+250 -e sleep 30 >"$OUT/log/xterm.log" 2>&1 &
XTERM=$!
sleep 0.4
DISPLAY=$D ./build/platine-test >"$OUT/log/test.log" 2>&1 &
APP=$!
sleep 1.2

DISPLAY=$D import -window root "$OUT/bureau.png"

# Clic dans xterm : le WM doit donner le focus et rejouer le clic.
DISPLAY=$D xdotool mousemove 520 410 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/focus-xterm.png"

# Clic dans la fenêtre Test : retour du focus.
DISPLAY=$D xdotool mousemove 200 200 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/focus-test.png"

# Menu d'application (sélecteur d'activité).
DISPLAY=$D xdotool mousemove 730 10 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/selecteur.png"
DISPLAY=$D xdotool mousemove 730 49 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/selecteur-choix.png"
DISPLAY=$D xdotool mousemove 200 200 click 1
sleep 0.4

# Ouvre le menu Fichier et capture le menu déroulant.
DISPLAY=$D xdotool mousemove 45 10 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/menu.png"

# Choisit « Nouveau » (premier item du menu Fichier).
DISPLAY=$D xdotool mousemove 60 30 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/apres-commande.png"

# Navigation clavier dans le menu : Fichier > Ouvrir (deux fois Bas, Entrée).
DISPLAY=$D xdotool mousemove 45 10 click 1
sleep 0.3
DISPLAY=$D xdotool key Down Down Return
sleep 0.5
DISPLAY=$D import -window root "$OUT/menu-clavier.png"

# Déplace la fenêtre par sa barre de titre (50,70 -> 150,130).
DISPLAY=$D xdotool mousemove 120 80 mousedown 1 mousemove 220 140 mouseup 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/deplacement.png"

# Redimensionnement par la poignée en bas à droite.
DISPLAY=$D xdotool mousemove 559 438 mousedown 1 mousemove 620 440 mouseup 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/redimension.png"

# Zoom (case en haut à droite de la fenêtre redimensionnée).
DISPLAY=$D xdotool mousemove 620 140 click 1
sleep 0.4
DISPLAY=$D import -window root "$OUT/zoom.png"

# Repli par double-clic sur la barre de titre (fenêtre zoomée : y=20..40).
DISPLAY=$D xdotool mousemove 300 30 click --repeat 2 --delay 120 1
sleep 0.5
DISPLAY=$D import -window root "$OUT/repli.png"

# Restaure.
DISPLAY=$D xdotool mousemove 300 30 click --repeat 2 --delay 120 1
sleep 0.5
DISPLAY=$D import -window root "$OUT/restaure.png"

# Ferme la fenêtre Test par sa case Fermer.
DISPLAY=$D xdotool mousemove 13 30 click 1
sleep 0.6
DISPLAY=$D import -window root "$OUT/fermeture.png"

echo "Captures dans $OUT"
echo "--- wm.log"; cat "$OUT/log/wm.log"
echo "--- test.log"; cat "$OUT/log/test.log"
