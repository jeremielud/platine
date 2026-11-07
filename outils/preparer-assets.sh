#!/bin/sh
# Prépare les assets personnels de Platine à partir des ressources Mac OS 9
# extraites par l'utilisateur. Ces fichiers ne sont pas publiés.
# Usage : ./outils/preparer-assets.sh
set -eu
cd "$(dirname "$0")/.."

SRC=ressources/macos9/extrait
OUT=assets
mkdir -p "$OUT/icones"

conv() {
	src=$1
	dst=$2
	convert "$src" "$dst"
}

# Icônes 32×32 du Finder
for n in dossier document-texte disque corbeille corbeille-pleine attention stop note; do
	conv "$SRC/icones/$n.png" "$OUT/icones/$n-32.xpm"
done
conv "$SRC/icones/dossier-ouvert.png" "$OUT/icones/dossier-ouvert-32.xpm"

# Icônes 16×16 (menu d'application, barre de titre)
for n in dossier document-texte disque corbeille app-finder; do
	conv "$SRC/icones/$n-16.png" "$OUT/icones/$n-16.xpm"
done

# Modules de la Control Strip
for n in airport batterie diction veille luminosite ecran son; do
	conv "$SRC/icones/barre-$n.png" "$OUT/icones/strip-$n.xpm"
done

# Tux de Larry Ewing, recadré en 16×16 comme l'icône Apple de Mac OS 9
convert outils/tux/tux-128.png -trim +repage -resize 11x15 \
    -background none -gravity center -extent 11x15 \
    "$OUT/icones/tux-11x15.xpm"
cp outils/tux/tux.xpm "$OUT/icones/tux.xpm"

# Sons d'interface (jeu Platinum, codes Appearance dans le nom)
mkdir -p "$OUT/sons"
cp "$SRC"/sons/platinum/*.wav "$OUT/sons/"

# Fond de bureau Mac OS 9 par défaut (128×128), sans transparence.
conv "$SRC/motifs/systeme-mac-os-default.png" "$OUT/fond.ppm"

echo "Assets dans $OUT"
ls -l "$OUT/icones" | head

# Tout ce qui se relève sur une capture de la VM (usage personnel) :
# pomme du menu Pomme, bannière d'« À propos de cet ordinateur », icônes du menu Pomme.
python3 - "$SRC" "$OUT" <<'PY'
import sys, os, glob
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
def xpm(im, chemin):
    """Écrit une image RGBA en XPM (pixels à alpha nul = transparents)."""
    im = im.convert('RGBA'); w, h = im.size; px = im.load()
    cs = {}; lignes = []
    car = ''.join(chr(c) for c in range(35, 127) if chr(c) not in '"\\')
    for y in range(h):
        l = ''
        for x in range(w):
            r, g, b, a = px[x, y]
            k = None if a < 128 else (r, g, b)
            if k not in cs:
                n = len(cs); cs[k] = car[n // len(car)] + car[n % len(car)]
            l += cs[k]
        lignes.append(l)
    with open(chemin, 'w') as f:
        f.write('/* XPM */\nstatic char *i[] = {\n"%d %d %d 2",\n' % (w, h, len(cs)))
        for k, v in cs.items():
            f.write('"%s c %s",\n' % (v, 'None' if k is None else '#%02x%02x%02x' % k))
        f.write(',\n'.join('"%s"' % l for l in lignes) + '\n};\n')
ref = Image.open('audit/macos9-ref/finder-active.png').convert('RGB')
p = ref.crop((14, 1, 31, 18)).convert('RGBA'); d = p.load()
for y in range(17):
    for x in range(17):
        if d[x, y][:3] == (221, 221, 221): d[x, y] = (0, 0, 0, 0)
xpm(p, os.path.join(out, 'icones', 'pomme.xpm'))
# Bannière : le logo Finder de Mac OS 9 (colonnes 40-115) et « Platine » à la place du texte
from PIL import ImageDraw, ImageFont
b = Image.open('audit/macos9-ref/about6.png').convert('RGB').crop((173, 88, 627, 178))
fond = Image.new('RGB', b.size, (255, 255, 255))
police = None
for c in ('/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf',
          '/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf'):
    if os.path.exists(c):
        police = ImageFont.truetype(c, 300)
        break
masque = Image.new('L', (1600, 400), 0)
ImageDraw.Draw(masque).text((10, 10), 'Platine', fill=255, font=police)
masque = masque.crop(masque.getbbox())
# même hauteur et même allure resserrée que « Mac OS 9.2 »
hc = 62; lc = int(masque.width * hc / masque.height * 0.86)
masque = masque.resize((lc, hc), Image.LANCZOS)
# logo Finder (71 px, colonnes 40-110 de la bannière d'origine) et texte, centrés ensemble
x0 = (fond.width - (71 + 15 + lc)) // 2
fond.paste(b.crop((40, 0, 111, 90)), (x0, 0))
fond.paste((0, 0, 0), (x0 + 71 + 15, 16), masque)
with open(os.path.join(out, 'apropos.ppm'), 'wb') as f:
    f.write(b'P6\n%d %d\n255\n' % fond.size); f.write(fond.tobytes())
os.makedirs(os.path.join(out, 'icones', 'menu'), exist_ok=True)
for c in glob.glob(os.path.join(src, 'icones', '*.png')):
    n = os.path.basename(c)[:-4]
    if n.endswith('-16') or any(n.startswith(t) for t in ('app-', 'appleworks', 'tableau-', 'dossier', 'jeu-')):
        im = Image.open(c).convert('RGBA')
        if im.size != (16, 16): im = im.resize((16, 16), Image.LANCZOS)
        xpm(im, os.path.join(out, 'icones', 'menu', n.replace('-16', '') + '.xpm'))
# icône 16×16 du panneau Control Strip (pas de -16.png dans les ressources)
im = Image.open(os.path.join(src, 'extensions', 'control-strip.png')).convert('RGBA').resize((16, 16), Image.LANCZOS)
xpm(im, os.path.join(out, 'icones', 'menu', 'tableau-control-strip.xpm'))
for a in glob.glob('archive/platine1/icones/appleworks*.png'):
    im = Image.open(a).convert('RGBA').resize((16, 16), Image.LANCZOS)
    xpm(im, os.path.join(out, 'icones', 'menu', 'appleworks.xpm'))
    break
# Polices bitmap de Mac OS 9 (NFNT) : pixel exact. Gras de QuickDraw : double frappe +1 px,
# chasse +1.
import sys as _s
_s.path.insert(0, 'ressources/macos9/outils')
import rsrc, nfnt
os.makedirs(os.path.join(out, 'polices'), exist_ok=True)
gen = rsrc.lire('ressources/macos9/brut/untitled/System Folder/Fonts/Geneva:rsrc')
def bdf(F, px, gras, chemin, italique=0):
    gl = []
    asc = F['h'] - F['desc']
    for code, (rows, off, wid) in sorted(F['g'].items()):
        if code < 32:
            continue
        try:
            u = ord(bytes([code]).decode('mac_roman')) if code >= 0 else None
        except Exception:
            continue
        if u is None:
            continue
        l = len(rows[0]) if rows else 0
        lignes = []
        for r in rows:
            r = list(r)
            if gras:
                r = [(r[i] if i < l else 0) | (r[i - 1] if i > 0 else 0) for i in range(l + 1)]
            lignes.append(r)
        w = l + (1 if gras else 0)
        if italique and lignes:
            # QuickDraw : chaque ligne se décale d'un pixel toutes les deux lignes depuis la ligne de base
            dec = [(asc - 1 - r) // 2 for r in range(len(lignes))]
            m = min(dec)
            lignes = [[0] * (d - m) + r + [0] * (max(dec) - d) for d, r in zip(dec, lignes)]
            w += max(dec) - m
            off += m
        gl.append((u, off, wid + (1 if gras else 0), w, lignes))
    with open(chemin, 'w') as f:
        f.write('STARTFONT 2.1\nFONT -platine-Platine Geneva-%s-r-normal--%d-%d-75-75-p-60-iso10646-1\n' % ('bold' if gras else 'medium', px, px * 10))
        f.write('SIZE %d 75 75\nFONTBOUNDINGBOX 40 %d -2 %d\n' % (px, F['h'], -F['desc']))
        f.write('STARTPROPERTIES 14\nFOUNDRY "platine"\nFAMILY_NAME "Platine Geneva"\nWEIGHT_NAME "%s"\nSLANT "R"\nSETWIDTH_NAME "Normal"\nADD_STYLE_NAME ""\nPIXEL_SIZE %d\nPOINT_SIZE %d\nRESOLUTION_X 75\nRESOLUTION_Y 75\nCHARSET_REGISTRY "ISO10646"\nCHARSET_ENCODING "1"\nFONT_ASCENT %d\nFONT_DESCENT %d\nENDPROPERTIES\n' % ('Bold' if gras else 'Medium', px, px * 10, F['asc'], F['desc']))
        f.write('CHARS %d\n' % len(gl))
        for u, off, wid, w, lignes in gl:
            f.write('STARTCHAR U%04X\nENCODING %d\nSWIDTH %d 0\nDWIDTH %d 0\n' % (u, u, wid * 72, wid))
            if w == 0:
                f.write('BBX 1 1 0 0\nBITMAP\n00\nENDCHAR\n')
                continue
            f.write('BBX %d %d %d %d\nBITMAP\n' % (w, F['h'], off, -F['desc']))
            for r in lignes:
                octets = bytearray((w + 7) // 8)
                for i, b in enumerate(r):
                    if b:
                        octets[i // 8] |= 0x80 >> (i % 8)
                f.write(octets.hex().upper() + '\n')
            f.write('ENDCHAR\n')
        f.write('ENDFONT\n')
for px, rid in ((9, 18766), (10, 28416), (12, 1902)):
    F = nfnt.lire(gen['NFNT'][rid][1])
    bdf(F, px, 0, os.path.join(out, 'polices', 'geneva-%d.bdf' % px))
    bdf(F, px, 1, os.path.join(out, 'polices', 'geneva-%d-gras.bdf' % px))
    bdf(F, px, 0, os.path.join(out, 'polices', 'geneva-%d-italique.bdf' % px), 1)
PY

# Logo du système pour « À propos » : outils/logos (sources officielles NetBSD, Haiku, OpenBSD)
convert outils/logos/netbsd-source.png -crop 62x32+18+8 +repage -fuzz 12% -transparent white -trim +repage \
    -background none -gravity center -resize 16x16 -extent 16x16 -channel A -threshold 50% +channel -colors 24 "$OUT/icones/os-netbsd-16.xpm"
convert outils/logos/haiku-source.png -crop 66x32+72+36 +repage -fuzz 35% -transparent white -trim +repage -background none \
    -gravity center -resize 16x16 -extent 16x16 -channel A -threshold 50% +channel -colors 24 "$OUT/icones/os-haiku-16.xpm"
convert outils/logos/openbsd-source.gif -coalesce -delete 1--1 -crop 196x190+362+24 +repage -fuzz 28% -fill none \
    -draw 'alpha 0,0 floodfill' -draw 'alpha 195,0 floodfill' -draw 'alpha 0,189 floodfill' -draw 'alpha 195,189 floodfill' \
    -trim +repage -background none -gravity center -resize 16x16 -extent 16x16 -channel A -threshold 50% +channel -colors 24 "$OUT/icones/os-openbsd-16.xpm"
convert outils/tux/tux-128.png -trim +repage -resize 16x16 -background none -gravity center -extent 16x16 \
    -channel A -threshold 50% +channel -colors 24 "$OUT/icones/os-linux-16.xpm"
python3 -I outils/ttf-vers-bdf.py systeme/polices/Charcoal.ttf charcoal 12 "$OUT/polices/charcoal-12.bdf"
# Alternatives du panneau Apparence et rendu lissé, chargés directement par libptk.
for f in Charcoal Chicago Geneva Monaco; do cp "systeme/polices/$f.ttf" "$OUT/polices/$f.ttf"; done

# Icônes avec alpha : les 32 bits du fichier System (outils/icones32.py), sinon la version 8 bits
python3 -I ressources/macos9/outils/icones32.py "ressources/macos9/brut/untitled/System Folder" "$SRC/icones32" >/dev/null
python3 -I - "$SRC" "$OUT/icones" <<'PY'
import os, sys
from PIL import Image
src, out = sys.argv[1:3]
def argb(chemin, nom):
    im = Image.open(chemin).convert('RGBA')
    d = bytearray(b'ARGB' + im.width.to_bytes(2, 'big') + im.height.to_bytes(2, 'big'))
    for r, g, b, a in im.getdata():
        d += bytes((b * a // 255, g * a // 255, r * a // 255, a))
    open(os.path.join(out, nom + '.argb'), 'wb').write(d)
for nom in ('dossier', 'document', 'disque', 'corbeille', 'corbeille-pleine'):
    # le disque dur des Mac de bureau : icône sans ressource 8 bits correspondante
    p = os.path.join(src, 'icones32', 'sans-id-28.png' if nom == 'disque' else nom + '.png')
    argb(p if os.path.exists(p) else os.path.join(src, 'icones', nom + '.png'), nom + '-32')
for nom in ('dossier', 'document'):
    argb(os.path.join(src, 'icones32', nom + '-16.png'), nom + '-16')
# Familles de documents du Finder, en 32×32 et pour la vue liste.
for nom in ('document-texte', 'document-image', 'document-wav', 'document-film',
            'document-archive', 'document-web', 'document-script'):
    for suffix in ('', '-16'):
        argb(os.path.join(src, 'icones', nom + suffix + '.png'), nom + suffix)
# Icônes 32×32 (et 16×16 pour la vue liste) des tableaux de bord, plus le dossier
for nom in ('tableau-apparence', 'tableau-clavier', 'tableau-diction', 'tableau-general',
            'tableau-moniteurs', 'tableau-son', 'tableau-souris',
            'tableau-date', 'tableau-economiseur', 'dossier-tableaux'):
    for suffix, taille in (('', '-32'), ('-16', '-16')):
        p = os.path.join(src, 'icones', nom + suffix + '.png')
        if os.path.exists(p):
            argb(p, nom + taille)
# Programmes (icône portée par leur identité embarquée) et dossiers spéciaux de
# l'arborescence Macintosh : Dossier Système, Applications, Extensions…
for nom in ('application', 'app-texte', 'app-calculette', 'app-finder', 'app-profil',
            'app-terminal', 'app-album', 'app-bloc-notes', 'app-cd-audio', 'app-classilla',
            'app-graphique', 'app-image', 'app-macamp', 'app-navigateur', 'app-pense-betes',
            'app-quicktime', 'app-sherlock', 'app-touches', 'app-afterdark',
            'dossier-systeme', 'dossier-applications', 'dossier-extensions', 'dossier-polices',
            'dossier-preferences', 'dossier-demarrage'):
    for suffix, taille in (('', '-32'), ('-16', '-16')):
        p = os.path.join(src, 'icones', nom + suffix + '.png')
        if os.path.exists(p):
            argb(p, nom + taille)
# Control Strip : l'icône du panneau est celle de l'extension, l'icône 16×16
# est réduite pour la vue liste.
p = os.path.join(src, 'extensions', 'control-strip.png')
argb(p, 'tableau-control-strip-32')
im = Image.open(p).convert('RGBA').resize((16, 16), Image.LANCZOS)
tmp = os.path.join(out, '.control-strip-16.png')
im.save(tmp)
argb(tmp, 'tableau-control-strip-16')
os.unlink(tmp)
PY

# Démarrage : curseurs animés, icônes d'extensions, logo Platine dans le panneau de la fenêtre, Happy Mac
python3 -I - "$SRC" "$OUT" <<'PY'
import os, sys
sys.path.insert(0, 'ressources/macos9/outils')
import rsrc
from PIL import Image
src, out = sys.argv[1:3]
sy = rsrc.lire('ressources/macos9/brut/untitled/System Folder/System:rsrc')
os.makedirs(os.path.join(out, 'curseurs'), exist_ok=True)
for nom, ids in (('balle', range(-20708, -20700)), ('montre', range(-6078, -6071))):
    for i, r in enumerate(ids):
        open(os.path.join(out, 'curseurs', '%s-%d.cur' % (nom, i)), 'wb').write(sy['CURS'][r][1][:68])
def argb(im, nom):
    im = im.convert('RGBA')
    d = bytearray(b'ARGB' + im.width.to_bytes(2, 'big') + im.height.to_bytes(2, 'big'))
    for r, g, b, a in im.getdata():
        d += bytes((b * a // 255, g * a // 255, r * a // 255, a))
    open(os.path.join(out, 'icones', nom + '.argb'), 'wb').write(d)
for f in os.listdir(os.path.join(src, 'extensions')):
    if f.endswith('.png'):
        argb(Image.open(os.path.join(src, 'extensions', f)), 'ext-' + f[:-4])
# logo : le visage du Finder relevé sur la VM, « Platine » de la bannière À propos à la place de « Mac OS 9.2 »
ref = Image.open('audit/macos9-ref/demarrage-etapes.png').convert('RGB')
panneau = Image.new('RGB', (347, 220), 'white')
panneau.paste(ref.crop((330, 125, 470, 245)), (106, 14))
ap = Image.open(os.path.join(out, 'apropos.ppm')).convert('RGB')
# colonnes de texte : à droite du logo de la bannière
w, h = ap.size
txt = ap.crop((int(w * 0.33), 0, w, h))
gris = txt.convert('L').point(lambda v: 255 if v < 128 else 0)
txt = txt.crop(gris.getbbox())
panneau.paste(txt, ((347 - txt.width) // 2, 200 - txt.height))
panneau.save(os.path.join(out, 'demarrage.ppm'))
# Happy Mac
hm = Image.open('outils/logos/happymac-640.png').convert('RGB')
m = Image.eval(hm.convert('L'), lambda v: 0 if abs(v - 0x88) < 6 else 255)
hm.crop(m.getbbox()).save(os.path.join(out, 'happymac.ppm'))
PY

# Calculette : pur noir et blanc, le motif 1 bit de la ressource ppat -15999 (aa55…, un damier)
python3 -I - "$OUT" <<'PY'
import os, sys
sys.path.insert(0, 'ressources/macos9/outils')
import rsrc
from PIL import Image
d = rsrc.lire('ressources/macos9/brut/apps/Calculator:rsrc')['ppat'][-15999][1]
motif = d[20:28]
assert motif == bytes.fromhex('aa55aa55aa55aa55')
im = Image.new('RGB', (8, 8))
for y in range(8):
    for x in range(8):
        im.putpixel((x, y), (0, 0, 0) if motif[y] >> (7 - x) & 1 else (255, 255, 255))
im.save(os.path.join(sys.argv[1], 'calculette-fond.ppm'))
PY

# Tableaux de bord : images découpées dans les captures de la VM, motifs et sons d'alerte
python3 -I - "$SRC" "$OUT" <<'PY'
import os, shutil, sys
from PIL import Image
src, out = sys.argv[1:3]
R = 'audit/macos9-ref/tableaux/'
os.makedirs(os.path.join(out, 'tableaux'), exist_ok=True)
def coupe(fichier, boite, nom):
    Image.open(R + fichier).convert('RGB').crop(boite).save(os.path.join(out, 'tableaux', nom + '.ppm'))
coupe('souris.png', (48, 101, 80, 124), 'souris-tablette')
coupe('souris.png', (106, 88, 133, 124), 'souris-lente')
coupe('souris.png', (176, 88, 208, 124), 'souris-rapide')
coupe('souris.png', (46, 197, 73, 245), 'souris-double')
coupe('souris.png', (97, 196, 108, 220), 'fleches-1')
coupe('souris.png', (146, 196, 156, 220), 'fleches-2')
coupe('souris.png', (194, 196, 205, 220), 'fleches-3')
coupe('moniteurs.png', (168, 213, 331, 264), 'ecran-entete')
coupe('moniteurs.png', (192, 387, 352, 413), 'ecran-degrade')
coupe('diction.png', (160, 118, 200, 160), 'diction-icone')
coupe('a-themes.png', (193, 142, 587, 314), 'themes')
coupe('a-bureau.png', (196, 148, 386, 288), 'bureau-apercu')
os.makedirs(os.path.join(out, 'motifs'), exist_ok=True)
for f in os.listdir(os.path.join(src, 'motifs')):
    if f.endswith('.png'):
        Image.open(os.path.join(src, 'motifs', f)).convert('RGB').save(os.path.join(out, 'motifs', f[:-4] + '.ppm'))
os.makedirs(os.path.join(out, 'alertes'), exist_ok=True)
for f in os.listdir(os.path.join(src, 'sons', 'alertes')):
    shutil.copy(os.path.join(src, 'sons', 'alertes', f), os.path.join(out, 'alertes', f))
PY


# Icônes propres à Platine (systeme/icones, dessinées par outils/dessiner-icones.py) :
# argb 32/16 pour le Finder, xpm 32 pour les alertes, xpm 16 pour les menus et la barre
python3 -I outils/dessiner-icones.py
for f in systeme/icones/*-32.png; do
	n=$(basename "$f" -32.png)
	python3 -I - "$f" "systeme/icones/$n-16.png" "$OUT/icones/$n" <<'PY'
import sys
from PIL import Image
def argb(chemin, sortie):
    im = Image.open(chemin).convert('RGBA')
    d = bytearray(b'ARGB' + im.width.to_bytes(2, 'big') + im.height.to_bytes(2, 'big'))
    for r, g, b, a in im.getdata():
        d += bytes((b * a // 255, g * a // 255, r * a // 255, a))
    open(sortie, 'wb').write(d)
argb(sys.argv[1], sys.argv[3] + '-32.argb')
argb(sys.argv[2], sys.argv[3] + '-16.argb')
PY
	convert "$f" -channel A -threshold 50% +channel "$OUT/icones/$n-32.xpm"
	convert "systeme/icones/$n-16.png" -channel A -threshold 50% +channel "$OUT/icones/menu/$n.xpm"
done

# Lecteur QuickTime : boutons de QuickTime Player 5 (repos, survol, enfoncé), métal
# brossé de la fenêtre et panneau À propos
python3 -I - "$SRC/quicktime" "$OUT" <<'PY'
import os, sys, glob
from PIL import Image
src, out = sys.argv[1:3]
os.makedirs(os.path.join(out, 'icones', 'quicktime'), exist_ok=True)
for p in glob.glob(os.path.join(src, '*-[0-9].png')):
    im = Image.open(p).convert('RGBA')
    d = bytearray(b'ARGB' + im.width.to_bytes(2, 'big') + im.height.to_bytes(2, 'big'))
    for r, g, b, a in im.getdata():
        d += bytes((b * a // 255, g * a // 255, r * a // 255, a))
    open(os.path.join(out, 'icones', 'quicktime', os.path.basename(p)[:-4] + '.argb'), 'wb').write(d)
Image.open(os.path.join(src, 'metal.png')).convert('RGB').save(os.path.join(out, 'quicktime-metal.ppm'))
Image.open(os.path.join(src, 'apropos.png')).convert('RGB').save(os.path.join(out, 'quicktime-apropos.ppm'))
PY

# StuffIt Expander : l'icône du programme vient de la ressource icns du rsrc extrait,
# l'usage reste personnel (les assets Apple ne sont jamais publiés)
python3 -I - "ressources/macos9/brut/apps/Stuffit Expander:rsrc" "$OUT" <<'PY'
import sys, os
sys.path.insert(0, 'ressources/macos9/outils')
import icns, rsrc
from PIL import Image
r = rsrc.lire(sys.argv[1])
e = icns.elements(r['icns'][128][1])
n = 128
it = e['it32']
if len(it) == n * n * 3 + 4:
    it = it[4:]
rr, gg, bb = icns._rle(it, n * n)
im = Image.new('RGBA', (n, n))
im.putdata([(rr[i], gg[i], bb[i], e['t8mk'][i]) for i in range(n * n)])
for taille in (32, 16):
    a = im.resize((taille, taille), Image.LANCZOS).convert('RGBA')
    d = bytearray(b'ARGB' + taille.to_bytes(2, 'big') + taille.to_bytes(2, 'big'))
    for r_, g_, b_, al in a.getdata():
        d += bytes((b_ * al // 255, g_ * al // 255, r_ * al // 255, al))
    open(os.path.join(sys.argv[2], 'icones', 'app-expander-%d.argb' % taille), 'wb').write(d)
# xpm : barre de titre et menus (16), transparence '.' comme les autres apps
a = im.resize((16, 16), Image.NEAREST).convert('RGBA')
px = a.load()
car = ''.join(chr(c) for c in range(33, 127) if chr(c) not in ' ."\\#:')
teintes, couleurs = {}, []
rangées = []
for y in range(16):
    ligne = []
    for x in range(16):
        r_, g_, b_, al = px[x, y]
        if al < 128:
            ligne.append('.')
            continue
        t = '#%02x%02x%02x' % (r_, g_, b_)
        if t not in teintes:
            teintes[t] = car[len(couleurs)]
            couleurs.append(t)
        ligne.append(teintes[t])
    rangées.append(''.join(ligne))
lignes = ['16 16 %d 1' % (len(couleurs) + 1), '. c None']
for i, t in enumerate(couleurs):
    lignes.append('%s c %s' % (car[i], t))
lignes += rangées
lignes = ['/* XPM */', 'static char * app_expander[] = {'] + \
    ['"%s",' % l.replace('"', '\\"') for l in lignes] + ['};']
open(os.path.join(sys.argv[2], 'icones', 'menu', 'app-expander.xpm'), 'w').write(
    '\n'.join(lignes) + '\n')
PY


