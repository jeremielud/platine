# commun.sh : lu par les scripts powerd de Platine (/etc/powerd/scripts).
# Réglages dans /etc/platine-energie, écrits par « privilege energie CLÉ VALEUR » :
#   couvercle    ecran | veille | rien     (défaut : ecran)
#   alimentation demander | eteindre | veille | rien  (défaut : demander)
PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/X11R7/bin; export PATH

energie() {	# energie CLÉ DÉFAUT
	v=$(awk -v k="$1" '$1 == k { print $2; exit }' /etc/platine-energie 2>/dev/null)
	echo "${v:-$2}"
}

# commande dans la session X de l'utilisateur de la console (xinit sur ttyE0)
# code 1 si aucune session X n'est ouverte
dans_x() {
	u=$(ls -l /dev/console | awk '{ print $3 }')
	[ -n "$u" ] && [ "$u" != root ] || return 1
	# le WM : « Système » dans /System, platine-wm sinon ;
	# par ps : le pgrep -x de NetBSD ne retrouve pas ces noms (relevé sur le 701)
	ps -ax -U "$u" -o ucomm= | awk '{ sub(/ +$/, "") } $0 == "Système" || $0 == "platine-wm" { t = 1 }
		END { exit !t }' || return 1
	h=$(awk -F: -v u="$u" '$1 == u { print $6 }' /etc/passwd)
	su "$u" -c "DISPLAY=:0 XAUTHORITY=$h/.Xauthority PLATINE_ASSETS=/opt/platine/assets \
		PATH=/opt/platine/bin:/usr/X11R7/bin:/bin:/usr/bin LANG=fr_FR.UTF-8 LC_ALL=fr_FR.UTF-8 $*" >/dev/null 2>&1
}

suspendre() {
	sysctl -w hw.acpi.sleep.state=3 >/dev/null
}

# alerte Platine dans la session X : l'Alerte de /System, sinon platine-alerte
alerte_x() {
	u=$(ls -l /dev/console | awk '{ print $3 }')
	h=$(awk -F: -v u="$u" '$1 == u { print $6 }' /etc/passwd)
	a="/System/Extensions/Alerte"
	[ -x "$a" ] || a="$h/System/Extensions/Alerte"
	[ -x "$a" ] || a=platine-alerte
	dans_x "'$a' $*"
}
