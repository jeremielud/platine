#!/bin/sh
# /etc/acpi/handler.sh (bureau Platine) : bouton d'alimentation, couvercle et touches Fn
# de l'Eee PC 701. Appelé par acpid pour chaque événement : <classe> <nom> <valeur> <sup>
PATH="/usr/share/acpid:/usr/local/bin:$PATH"
alias log='logger -t acpid'
U=jeremie
dans_x() { su $U -c "DISPLAY=:0 XAUTHORITY=/home/$U/.Xauthority $*" >/dev/null 2>&1 & }
volume() { amixer -q set Master "$@"; osd_volume; }
OSD=/tmp/runtime-$U/platine-osd
osd() { [ -p "$OSD" ] && (timeout 1 sh -c "echo '$*' > $OSD" &) ; }
osd_volume() {
	r=$(amixer get Master)
	case $r in *"[off]"*) osd silence ;; *) osd volume "$(echo "$r" | sed -n 's/.*\[\([0-9]*\)%\].*/\1/p' | head -1)" ;; esac
}
lumiere() { # lumiere +10|-10 (pourcentage)
	for b in /sys/class/backlight/*; do
		m=$(cat $b/max_brightness); v=$(cat $b/brightness)
		v=$(( v + $1 * m / 100 )); [ $v -lt $((m/20)) ] && v=$((m/20)); [ $v -gt $m ] && v=$m
		echo $v > $b/brightness
		osd luminosite $(( v * 100 / m ))
		break
	done
}
case "$1:$2:$3:$4" in
button/power:*)
	# Comme Mac OS 9 : demander ce qu'il faut faire (sans X : éteindre)
	if pgrep -x Xorg >/dev/null; then dans_x platine-arret bouton; else poweroff; fi ;;
button/sleep:*|button/suspend:*)	log 'veille (Fn+F1)'; echo mem > /sys/power/state ;;
button/lid:*:close:*)
	[ "$(cat /etc/platine-couvercle 2>/dev/null || echo 1)" = 1 ] && echo mem > /sys/power/state ;;
button/mute:*)	volume toggle ;;
button/volumeup:*)	volume 5%+ unmute ;;
button/volumedown:*)	volume 5%- unmute ;;
button/wlan:*)
	if rfkill list wlan | grep -q 'Soft blocked: yes'; then rfkill unblock wlan; else rfkill block wlan; fi ;;
video/brightnessup:*)	lumiere 10 ;;
video/brightnessdown:*)	lumiere -10 ;;
*)	log "événement ignoré : $*" ;;
esac
exit 0
