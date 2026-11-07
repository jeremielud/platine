#!/bin/sh
# poser-arborescence.sh BINAIRES UTILISATEUR [RACINE] : pose les programmes Platine à la
# racine du disque, comme Mac OS X (décision D11) — noms anglais sur le disque, que le
# Finder montre en français :
#
#   /Applications/                    TextEdit, Théière, StuffIt Expander,
#                                     QuickTime/Lecteur QuickTime, Utilities/Démo Toolbox
#   /System/Finder, /System/Système   le Finder et le gestionnaire de fenêtres
#   /System/Extensions/               Control Strip, Alerte, Lire les informations,
#                                     À propos de cet ordinateur, Affichage à l'écran,
#                                     Écran de démarrage, Instantané (⌘⇧3)
#   /System/Control Panels/           les tableaux de bord (un seul programme, liens
#                                     physiques : le nom choisit le tableau)
#   /System/Apple Menu Items/         Calculette, Pense-bêtes, Key Caps, Profileur système,
#                                     alias Tableaux de bord, TextEdit… : le menu Pomme
#   /System/Startup Items/            ouverts par la session après le Finder
#   /System/Control Strip Modules/    modules ajoutés au Control Strip (Installateur)
#   /System/Fonts                     alias des polices Mac OS 9 de X
#   /Library/Desktop Pictures         alias des images de Mac OS 9 (Images du bureau)
#   ~/Desktop, ~/Documents, ~/Downloads, ~/Pictures, ~/Music, ~/Movies, ~/Library
#
# Une ancienne installation (D9 : ~/Applications, ~/Dossier Système, ~/Bureau, ~/Images…)
# est migrée : ce que l'utilisateur y avait ajouté est déplacé sans rien écraser.
# Chaque programme est le vrai binaire, portant son identité (section .platine).
# Ce qui relève de BSD (session, privilèges, outils) reste dans /opt/platine.
# Lancé en root sur le 701 par outils/installer-netbsd.sh.
set -eu
B=${1:?dossier des binaires compilés}
U=${2:?utilisateur}
R=${3:-}	# racine (tests : un dossier jetable)
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
H=$(awk -F: -v u="$U" '$1 == u { print $6 }' /etc/passwd)
G=$(id -gn "$U")
[ -d "$H" ] || { echo "pas de dossier personnel pour $U" >&2; exit 1; }
A="$R/Applications"
S="$R/System"
L="$R/Library"

# un binaire en place, même en cours d'exécution : copie puis renommage
poser() { # poser binaire destination
	d=$(dirname "$2")
	mkdir -p "$d"
	cp "$B/$1" "$2.neuf"
	chmod 755 "$2.neuf"
	chown "$U:$G" "$2.neuf"
	mv -f "$2.neuf" "$2"
}
alias_vers() { # alias_vers cible lien
	rm -f "$2"
	ln -s "$1" "$2"
	chown -h "$U:$G" "$2"
}
# déplace le contenu de $1 dans $2 sans rien écraser (une ancienne installation)
reprendre() { # reprendre ancien nouveau
	[ -d "$1" ] && [ ! -L "$1" ] || return 0
	mkdir -p "$2"
	for e in "$1"/* "$1"/.[!.]*; do
		[ -e "$e" ] || [ -L "$e" ] || continue
		n=$(basename "$e")
		if [ ! -e "$2/$n" ] && [ ! -L "$2/$n" ]; then mv "$e" "$2/$n"; fi
	done
}

# --- l'arborescence : à la racine, accessible à l'utilisateur pour l'Installateur ---
mkdir -p "$A/Utilities" "$A/QuickTime" "$S/Extensions" "$S/Control Panels" \
    "$S/Apple Menu Items" "$S/Startup Items" "$S/Control Strip Modules" "$L" \
    "$H/.config/platine"
for d in "$A" "$S" "$L"; do
	chown root:"$G" "$d"
	chmod 775 "$d"
done

poser platine-textedit "$A/TextEdit"
poser platine-theiere "$A/Théière"
poser platine-expander "$A/StuffIt Expander"
poser platine-demo "$A/Utilities/Démo Toolbox"
# Mac OS 9.2.1 : Applications (Mac OS 9)/QuickTime/QuickTime Player
poser platine-quicktime "$A/QuickTime/Lecteur QuickTime"

poser platine-finder "$S/Finder"
poser platine-wm "$S/Système"
poser platine-strip "$S/Extensions/Control Strip"
poser platine-alerte "$S/Extensions/Alerte"
poser platine-info "$S/Extensions/Lire les informations"
poser platine-about "$S/Extensions/À propos de cet ordinateur"
poser platine-osd "$S/Extensions/Affichage à l'écran"
poser platine-demarrage "$S/Extensions/Écran de démarrage"
poser platine-instantane "$S/Extensions/Instantané"
poser platine-calculette "$S/Apple Menu Items/Calculette"
# Mac OS 9.2.1 : Stickies, Key Caps et Apple System Profiler sont des programmes rangés
# dans le dossier Apple Menu Items lui-même, comme Calculator
poser platine-pensebetes "$S/Apple Menu Items/Pense-bêtes"
poser platine-keycaps "$S/Apple Menu Items/Key Caps"
poser platine-profileur "$S/Apple Menu Items/Profileur système"

T="$S/Control Panels"
poser platine-tableau "$T/Apparence"
for t in Clavier Diction Écran Son Souris "Control Strip" "Date et heure" \
    "Économiseur d'énergie"; do
	ln -f "$T/Apparence" "$T/$t"
done

# menu Pomme (décision D8) : il montre le contenu de ce dossier — alias des
# programmes, comme les éléments d'un menu Pomme Mac OS 9
P="$S/Apple Menu Items"
alias_vers "../Control Panels" "$P/Tableaux de bord"
alias_vers "../../Applications/TextEdit" "$P/TextEdit"
alias_vers "../../Applications/Théière" "$P/Théière"
alias_vers "../../Applications/StuffIt Expander" "$P/StuffIt Expander"
alias_vers "../../Applications/QuickTime/Lecteur QuickTime" "$P/Lecteur QuickTime"
alias_vers "../Extensions/Instantané" "$P/Instantané"	# ouvre le panneau de capture
if [ -x /usr/X11R7/bin/xterm ]; then alias_vers /usr/X11R7/bin/xterm "$P/Terminal"; fi
if [ -d /usr/X11R7/lib/X11/fonts/macos9 ]; then
	alias_vers /usr/X11R7/lib/X11/fonts/macos9 "$S/Fonts"
fi
if [ -d /opt/platine/ressources/images ]; then
	alias_vers /opt/platine/ressources/images "$L/Desktop Pictures"
fi

# --- migration de l'ancienne disposition (D9, dans le dossier personnel) ---
[ -L "$H/Applications" ] && rm -f "$H/Applications"
rm -rf "$H/.local/share/platine/Eee PC"
if [ -d "$H/Applications" ]; then
	reprendre "$H/Applications/Utilitaires" "$A/Utilities"
	rm -rf "$H/Applications/Utilitaires"
	reprendre "$H/Applications" "$A"
	rm -rf "$H/Applications"
fi
O="$H/Dossier Système"
if [ -d "$O" ]; then
	reprendre "$O/Extensions" "$S/Extensions"
	reprendre "$O/Modules Control Strip" "$S/Control Strip Modules"
	reprendre "$O/Éléments du menu Pomme" "$S/Apple Menu Items"
	reprendre "$O/Éléments de démarrage" "$S/Startup Items"
	rm -rf "$O"
fi
# dossiers personnels : noms anglais sur le disque, montrés en français
for p in "Bureau:Desktop" "Téléchargements:Downloads" "Images:Pictures" \
    "Musique:Music" "Vidéos:Movies"; do
	ancien=${p%%:*}
	neuf=${p#*:}
	if [ -d "$H/$ancien" ] && [ ! -e "$H/$neuf" ]; then mv "$H/$ancien" "$H/$neuf"; fi
done
mkdir -p "$H/Library"
alias_vers "../.config/platine" "$H/Library/Preferences"

# dossier du bureau (Desktop Folder, invisible dans le Finder) : à sa création, les
# alias de départ — un alias jeté ne revient pas à la réinstallation
D="$H/Desktop"
if [ ! -d "$D" ]; then
	mkdir -p "$D"
	chown "$U:$G" "$D"
	if [ -d "$H/Documents" ]; then alias_vers "../Documents" "$D/Documents"; fi
	if [ -d "$H/Pictures" ]; then alias_vers "../Pictures" "$D/Images"; fi
	alias_vers "$R/Applications" "$D/Applications"
	alias_vers "$R/Applications/QuickTime/Lecteur QuickTime" "$D/Lecteur QuickTime"
	alias_vers "$R/Applications/StuffIt Expander" "$D/StuffIt Expander"
fi
# alias du bureau d'avant D11 : vers les nouveaux chemins
for l in "$D"/*; do
	[ -L "$l" ] || continue
	c=$(readlink "$l")
	case "$c" in
	../Images) alias_vers "../Pictures" "$l" ;;
	../Applications*) alias_vers "$R/Applications${c#../Applications}" "$l" ;;
	esac
done

chown "$U:$G" "$A/Utilities" "$A/QuickTime" "$S/Extensions" "$T" "$S/Apple Menu Items" \
    "$S/Startup Items" "$S/Control Strip Modules" "$H/.config/platine" "$H/Library"

# plus rien des programmes dans /opt/platine/bin : il ne garde que le côté BSD
for b in platine-wm platine-finder platine-strip platine-about platine-info \
    platine-demo platine-osd platine-demarrage platine-calculette platine-tableau \
    platine-alerte platine-textedit platine-theiere platine-expander platine-instantane \
    platine-keycaps platine-pensebetes platine-profileur platine-quicktime; do
	rm -f "/opt/platine/bin/$b"
done
echo "arborescence posée à la racine${R:+ ($R)}"
