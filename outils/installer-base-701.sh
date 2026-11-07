#!/bin/sh
# Réinstalle le système du 701 de zéro, depuis le PC, le 701 ayant démarré sur la clé USB :
#   outils/installer-base-701.sh SAUVEGARDE.tar [hôte]
# EFFACE le SSD interne. Reprend de la sauvegarde seulement les réglages de la machine
# (réseau, comptes, clés SSH, clavier, X, chargeur) ; ni données ni ancien bureau.
set -eu
cd "$(dirname "$0")/.."
SAUV=$1
H=${2:-eeepc}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
umask 077

PAQUETS="alpine-base linux-lts linux-firmware-none syslinux mkinitfs e2fsprogs util-linux
 eudev udev-init-scripts udev-init-scripts-openrc dbus dbus-x11 openssh doas
 wpa_supplicant dhcpcd wireless-tools iw chrony acpid alsa-utils pciutils usbutils tzdata
 kbd-bkeymaps rsync xorg-server xinit xauth xf86-video-intel xf86-input-libinput
 setxkbmap xset xsetroot xrdb xinput xprop xrandr libx11 libxext libxft libxpm libxrender
 libxcursor fontconfig font-dejavu font-misc-misc font-cursor-misc font-alias
 xterm xcalc mousepad gpicview mupdf mpv xdotool xwininfo xwd xclip espeak-ng"

# Réglages à reprendre (absents de la sauvegarde : ignorés). L'archive est filtrée sans être
# extraite sur le PC : une extraction par un utilisateur normal perdrait propriétaires et modes,
# et /etc et /root/.ssh arriveraient sur le 701 sans être à root (X et SSH cassés).
python3 -I - "$SAUV" > "$T/reprise.tar" <<'PY'
import io, re, sys, tarfile
GARDER = re.compile(r'\./(etc/(hostname|hosts|timezone|localtime|keymap/.*|conf\.d/(loadkmap|wpa_supplicant)'
    r'|network/interfaces|wpa_supplicant/wpa_supplicant\.conf|X11/xorg\.conf\.d/.*|X11/Xwrapper\.config'
    r'|ssh/ssh_host_.*|ssh/sshd_config\.d/.*|doas\.conf|doas\.d/.*|modules|mkinitfs/mkinitfs\.conf'
    r'|update-extlinux\.conf|apk/repositories|acpi/PWRF/.*)|boot/extlinux\.conf|boot/happymac-640\.png'
    r'|root/\.ssh/.*|home/jeremie/\.ssh/.*|home/jeremie/\.profile|home/jeremie/\.hushlogin'
    r'|home/jeremie/\.config/user-dirs\.dirs)')
NIVEAU = re.compile(r'\./etc/runlevels/([a-z]+/.+)')
comptes, niveaux = b'', []
with tarfile.open(sys.argv[1]) as e, tarfile.open(fileobj=sys.stdout.buffer, mode='w|') as s:
    for m in e:
        n = NIVEAU.fullmatch(m.name)
        if n and '/platine-' not in n[1]:
            niveaux.append(n[1])
        elif m.name == './etc/shadow':
            comptes = b''.join(l for l in e.extractfile(m).read().splitlines(True)
                               if l.startswith((b'root:', b'jeremie:')))
        elif GARDER.fullmatch(m.name):
            s.addfile(m, e.extractfile(m) if m.isfile() else None)
    if comptes.count(b'\n') != 2:
        sys.exit('comptes root et jeremie absents de la sauvegarde')
    for nom, d in (('./comptes', comptes), ('./niveaux', ('\n'.join(niveaux) + '\n').encode())):
        m = tarfile.TarInfo(nom)
        m.size, m.mode = len(d), 0o600
        s.addfile(m, io.BytesIO(d))
PY
ssh "$H" 'umask 022; rm -rf /tmp/reprise; mkdir -m 700 /tmp/reprise; tar -C /tmp/reprise -xf -' < "$T/reprise.tar"

ssh "$H" PAQUETS="'$PAQUETS'" sh -s <<'FIN'
set -eu
umask 022
# seulement depuis la clé : racine en mémoire, jamais sur le système installé
[ "$(awk '$2 == "/" { print $3 }' /proc/mounts | tail -1)" = tmpfs ] || { echo "pas sur la clé USB" >&2; exit 1; }
DISQUE=
for s in /sys/block/sd*; do
	case $(readlink -f "$s") in *ata*) ;; *) continue ;; esac
	[ "$(cat "$s/removable")" = 0 ] || continue
	ko=$(( $(cat "$s/size") / 2 ))
	[ "$ko" -gt 3000000 ] && [ "$ko" -lt 5000000 ] || continue
	[ -z "$DISQUE" ] || { echo "plusieurs disques possibles" >&2; exit 1; }
	DISQUE=/dev/${s##*/}
done
[ -n "$DISQUE" ] || { echo "SSD interne introuvable" >&2; exit 1; }
if grep -q "^$DISQUE" /proc/mounts; then echo "$DISQUE est monté" >&2; exit 1; fi
echo "SSD interne : $DISQUE"

cp /tmp/reprise/etc/apk/repositories /etc/apk/repositories
apk -q update
apk -q add sfdisk e2fsprogs syslinux rsync util-linux

dd if=/dev/zero of="$DISQUE" bs=1M count=4 2>/dev/null
echo 'label: dos
,,83,*' | sfdisk -q "$DISQUE"
sleep 2
P=${DISQUE}1
[ -b "$P" ] || mdev -s
[ -b "$P" ]
mkfs.ext4 -q -F -L alpine -O ^64bit "$P"
mkdir -p /mnt
mount "$P" /mnt
mkdir -p /mnt/etc/apk /mnt/proc /mnt/sys /mnt/dev /mnt/tmp
cp -r /etc/apk/keys /mnt/etc/apk/
cp /tmp/reprise/etc/apk/repositories /mnt/etc/apk/repositories
mount -t proc proc /mnt/proc
mount --rbind /sys /mnt/sys
mount --rbind /dev /mnt/dev
# shellcheck disable=SC2086
apk -q --root /mnt --initdb --update-cache add $PAQUETS

# réglages repris et comptes
cp -a /tmp/reprise/etc/. /mnt/etc/
cp -a /tmp/reprise/boot/. /mnt/boot/
mkdir -p /mnt/root/.ssh
cp -a /tmp/reprise/root/.ssh/. /mnt/root/.ssh/
chroot /mnt adduser -D -u 1000 -s /bin/ash jeremie
for g in wheel audio input video netdev; do chroot /mnt addgroup jeremie "$g"; done
cp -a /tmp/reprise/home/jeremie/. /mnt/home/jeremie/
for d in Bureau Documents Images Musique Téléchargements Vidéos; do mkdir -p "/mnt/home/jeremie/$d"; done
chroot /mnt chown -R jeremie:jeremie /home/jeremie
while IFS= read -r ligne; do
	nom=${ligne%%:*}
	awk -F: -v n="$nom" -v l="$ligne" '$1 == n { print l; next } { print }' /mnt/etc/shadow > /mnt/etc/shadow.neuf
	cat /mnt/etc/shadow.neuf > /mnt/etc/shadow
	rm -f /mnt/etc/shadow.neuf
done < /tmp/reprise/comptes
chmod 700 /mnt/root/.ssh

# services de l'ancien système, sans ceux de l'ancien bureau
rm -rf /mnt/etc/runlevels/*
while read -r e; do
	mkdir -p "/mnt/etc/runlevels/${e%/*}"
	if [ -e "/mnt/etc/init.d/${e##*/}" ]; then ln -sf "/etc/init.d/${e##*/}" "/mnt/etc/runlevels/$e"; fi
done < /tmp/reprise/niveaux

# ouverture de session sur tty1
sed -i 's|^tty1::respawn:.*|tty1::respawn:/opt/platine/bin/platine-connexion|' /mnt/etc/inittab

# démarrage
U=$(blkid -s UUID -o value "$P")
sed -i "s/UUID=[0-9a-f-]*/UUID=$U/" /mnt/boot/extlinux.conf /mnt/etc/update-extlinux.conf
cat > /mnt/etc/fstab <<F
UUID=$U	/	ext4	noatime,errors=remount-ro	0 1
/swap	none	swap	sw	0 0
tmpfs	/tmp	tmpfs	nosuid,nodev,size=128m	0 0
F
dd if=/dev/zero of=/mnt/swap bs=1M count=256 2>/dev/null
chmod 600 /mnt/swap
mkswap /mnt/swap >/dev/null
chroot /mnt sh -c 'mkinitfs "$(ls /lib/modules | tail -1)"' >/dev/null
for f in ldlinux.c32 libcom32.c32 libutil.c32 menu.c32 vesamenu.c32 mboot.c32; do
	cp "/mnt/usr/share/syslinux/$f" /mnt/boot/
done
extlinux --install /mnt/boot >/dev/null
dd if=/mnt/usr/share/syslinux/mbr.bin of="$DISQUE" bs=440 count=1 conv=notrunc 2>/dev/null
rm -rf /tmp/reprise
df -h /mnt | tail -1
chroot /mnt apk info -q | wc -l
FIN
./outils/installer-701.sh "$H" /mnt
ssh "$H" 'chroot /mnt fc-cache -f; sync; echo base prête'
