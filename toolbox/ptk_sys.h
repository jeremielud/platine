/* ptk_sys.h : commandes propres au système, en un seul endroit (Linux ou NetBSD). */
#ifndef PTK_SYS_H
#define PTK_SYS_H

#ifdef __NetBSD__
#define SYS_WIFI_IFACE	"ath0"
#define SYS_WIFI_STATUT	"wpa_cli -i " SYS_WIFI_IFACE " status 2>/dev/null"
#define SYS_WIFI_LIST	"wpa_cli -i " SYS_WIFI_IFACE " list_networks 2>/dev/null"
#define SYS_WIFI_SSID	SYS_WIFI_STATUT " | sed -n 's/^ssid=//p'"
#define SYS_VOL_LIRE	"mixerctl -n outputs.master 2>/dev/null | awk -F, '{ print int($1 * 100 / 255 + 0.5) }'"
#define SYS_VOL_ECRIRE	"mixerctl -w outputs.master=%d >/dev/null"
#define SYS_VOL_BRUT(v)	((v) * 255 / 100)
#define SYS_BATTERIE	"envstat -d acpibat0 2>/dev/null | awk '/last full cap:/ { f = $4 } /^ *charge:/ { c = $2 } " \
			"END { if (f > 0) printf \"%d\", c * 100 / f }'"
#define SYS_FABRICANT	"sysctl -n machdep.dmi.system-vendor 2>/dev/null"
#define SYS_MODELE	"sysctl -n machdep.dmi.system-product 2>/dev/null"
#define SYS_SON		"audioctl -a 2>/dev/null | awk -F= '/^name=/ { n = $2 } /^version=/ { v = $2 } END { print n, v }'"
#define SYS_IP		"ifconfig -a 2>/dev/null | awk '$1 == \"inet\" && $2 !~ /^127/ { sub(\"/.*\", \"\", $2); print $2; exit }'"
#define SYS_LECTEUR_SON	"audioplay"
#define SYS_LECTEUR_OPT	"-q"
#define SYS_PRIVILEGE	"/opt/platine/libexec/platine-privilege"
#else
#define SYS_WIFI_IFACE	"wlan0"
#define SYS_WIFI_STATUT	"wpa_cli -i " SYS_WIFI_IFACE " status 2>/dev/null"
#define SYS_WIFI_LIST	"wpa_cli -i " SYS_WIFI_IFACE " list_networks 2>/dev/null"
#define SYS_WIFI_SSID	SYS_WIFI_STATUT " | sed -n 's/^ssid=//p'"
#define SYS_VOL_LIRE	"amixer get Master 2>/dev/null | sed -n 's/.*\\[\\([0-9]*\\)%\\].*/\\1/p' | head -1"
#define SYS_VOL_ECRIRE	"amixer -q set Master %d%%"
#define SYS_VOL_BRUT(v)	(v)
#define SYS_BATTERIE	"cat /sys/class/power_supply/BAT*/capacity 2>/dev/null | head -1"
#define SYS_FABRICANT	"cat /sys/class/dmi/id/sys_vendor 2>/dev/null"
#define SYS_MODELE	"cat /sys/class/dmi/id/product_name 2>/dev/null"
#define SYS_SON		"lspci 2>/dev/null | sed -n 's/.*Audio device: //p' | head -1"
#define SYS_IP		"ip -4 -o addr show scope global 2>/dev/null | sed -n 's/.* inet \\([0-9.]*\\).*/\\1/p' | head -1"
#define SYS_LECTEUR_SON	"aplay"
#define SYS_LECTEUR_OPT	"-q"
#define SYS_PRIVILEGE	"doas -n /usr/local/libexec/platine/privilege"
#endif

#endif
