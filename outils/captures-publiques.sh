#!/bin/sh
# captures-publiques.sh : captures de l'écran public de Platine pour
# docs/img/public/, dans Xvfb, AVEC les assets du dépôt (fond, icônes, polices,
# sons de Mac OS 9) — c'est l'aspect vraiment rendu. La NOTICE s'applique : les
# assets et ces captures restent la propriété d'Apple, usage strictement
# personnel, aucune rediffusion (les captures Apple-dérivées en plus de ces
# pages, ou le bureau du 701 avec les mêmes assets, restent hors publication).
set -eu
cd "$(dirname "$0")/.."
B="$PWD/build"
ASSETS="${PLATINE_ASSETS:-$PWD/assets}"
OUT="$PWD/docs/img/public"
mkdir -p "$OUT"

T=$(mktemp -d ${TMPDIR:-/tmp}/platine-captures.XXXXXX)
export HOME="$T/home"
export PLATINE_VOLUME="$T/vol"
export PLATINE_ASSETS="$ASSETS"
unset PLATINE_SONS PLATINE_SONS_JOURNAL
[ -d "$ASSETS" ] || { echo "assets absents : $ASSETS" >&2; exit 1; }
mkdir -p "$PLATINE_VOLUME" "$HOME"

WM= PROG= INS= EXP= XVFB=
quitter() {
	for pid in $WM $PROG $INS $EXP $XVFB; do
		kill "$pid" 2>/dev/null || true
	done
}
trap quitter EXIT INT TERM

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset \
	3>"$T/display" >"$T/xvfb.log" 2>&1 &
XVFB=$!
i=0
until [ -s "$T/display" ]; do
	i=$((i + 1)); [ $i -lt 100 ] || { echo "Xvfb ne démarre pas" >&2; exit 1; }
	sleep 0.05
done
export DISPLAY=":$(cat "$T/display")"

shot() {
	import -window root "$OUT/$1.png"
}

"$B/platine-wm" >"$T/wm.log" 2>&1 &
WM=$!
sleep 0.6

# 1) le bureau du Finder, seul (icônes du volume, horloge)
"$B/platine-finder" >"$T/finder.log" 2>&1 &
PROG=$!
sleep 1.4
shot finder
kill $PROG 2>/dev/null || true
PROG=

# 2) le minimum du Finder : une fenêtre de dossier de Applications (binaires vrais)
mkdir -p "$PLATINE_VOLUME/Applications/QuickTime"
cp "$B/platine-textedit" "$PLATINE_VOLUME/Applications/TextEdit"
cp "$B/platine-expander" "$PLATINE_VOLUME/Applications/StuffIt Expander"
cp "$B/platine-quicktime" "$PLATINE_VOLUME/Applications/QuickTime/Lecteur QuickTime" 2>/dev/null || true
"$B/platine-finder" "$PLATINE_VOLUME/Applications" >"$T/finder-app.log" 2>&1 &
PROG=$!
sleep 1.4
shot finder-applications
kill $PROG 2>/dev/null || true
PROG=

# 3) la fenêtre de la Toolbox (Démo)
"$B/platine-demo" >"$T/demo.log" 2>&1 &
PROG=$!
sleep 1.2
shot toolbox
kill $PROG 2>/dev/null || true
PROG=

# 4) TextEdit avec un document
mkdir -p "$PLATINE_VOLUME/Documents"
cat >"$PLATINE_VOLUME/Documents/journal.txt" <<'FIN'
Cher journal,

aujourd'hui je tourne sur un Eee PC 701, sous NetBSD, 512 Mo de RAM.
Le Finder et moi sommes un ensemble de programmes ordinaires, en
userspace, qui copient l'arborescence classique Macintosh.

Chaque application porte son identité embarquée, comme un fichier
Macintosh classique le porte dans ses forks.

J'écris ceci ce 7 novembre avec TextEdit de Platine.
FIN
"$B/platine-textedit" "$PLATINE_VOLUME/Documents/journal.txt" \
	>"$T/edit.log" 2>&1 &
PROG=$!
sleep 1.2
shot textedit
kill $PROG 2>/dev/null || true
PROG=

# 5) l'Expander : une archive posée à côté d'elle dans « Documents »
( cd "$PLATINE_VOLUME/Documents" && printf 'A\n' >alpha.txt && \
	printf 'B\n' >beta.txt && \
	tar czf "Mes recettes.tar.gz" alpha.txt beta.txt )
"$B/platine-expander" "$PLATINE_VOLUME/Documents/Mes recettes.tar.gz" \
	>"$T/exp.log" 2>&1 &
EXP=$!
sleep 1.6
shot expander
kill $EXP 2>/dev/null || true
EXP=

# 6) l'Installateur, page principale (sans lisez-moi, sans assets)
rm -rf "$T/contenu"; mkdir -p "$T/contenu"
cp "$B/platine-theiere" "$T/contenu/Théière"
{
	echo "nom=Théière"
	echo "version=1.0, Platine"
	echo "lieu=Applications"
	echo "dossier=Théière ƒ"
	echo "composant=Théière|La théière de l'Utah en OpenGL, le modèle de GLUT.|1|Théière"
} >"$T/manifeste"
sh outils/faire-installateur.sh "$B/platine-installateur" "$T/contenu" \
	"$T/manifeste" "$T/Installation de Théière" >/dev/null
"$T/Installation de Théière" >"$T/inst.log" 2>&1 &
INS=$!
sleep 1.4
shot installateur
kill $INS 2>/dev/null || true
INS=

echo "Captures posées dans $OUT (avec les assets du dépôt, NOTICE.md)"
