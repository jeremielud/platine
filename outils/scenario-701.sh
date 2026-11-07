#!/bin/sh
# scenario-701.sh : sur le 701, en root : reproduit à la souris (XTest) et capture dans /tmp/sc-N.xwd.
export PATH=/opt/platine/bin:/sbin:/usr/sbin:/bin:/usr/bin:/usr/X11R7/bin DISPLAY=:0 XAUTHORITY=/home/jeremie/.Xauthority
c() { xwd -root -silent > /tmp/sc-$1.xwd; }
v=$(mixerctl -n outputs.master); mixerctl -w outputs.master=120,120 >/dev/null; sleep 0.6; c osd; mixerctl -w outputs.master=$v >/dev/null; sleep 2.2
xclic clic 22 10; sleep 0.8; c 1-pomme
xclic clic 60 30; sleep 3; c 2-apropos
xwininfo -root -tree | grep -iE 'platine|propos|Eee PC' | cut -c1-90
# déplacer la fenêtre À propos par son titre
xclic glisse 400 ${1:-60} 420 ${2:-120}; sleep 1.5; c 3-deplace
xclic clic 300 300; sleep 1; c 4-clic-finder
# fermer À propos (case Fermer, coin haut gauche du cadre déplacé) : le Finder doit redevenir actif
xclic clic ${3:-191} ${4:-129}; sleep 1.2; c 5-ferme
xwininfo -root -tree | grep -c 'propos'
# clic dans le Finder, puis sur le bureau, puis de nouveau dans le Finder
xclic clic 300 300; sleep 0.8; c 6-finder
xclic clic 620 400; sleep 0.8; c 7-bureau
xclic clic 300 300; sleep 0.8; c 8-finder-retour
