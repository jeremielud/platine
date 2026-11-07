"""ttf-vers-bdf.py POLICE.ttf FAMILLE TAILLE_PX SORTIE.bdf : police bitmap X (Latin-1) rendue
par FreeType en monochrome avec les instructions TrueType de la police — le rendu net des
menus de Mac OS 9, au lieu du rendu non hinté des polices « core » TrueType de X."""
import sys
from PIL import ImageFont
src, famille, px, sortie = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
f = ImageFont.truetype(src, px)
asc, desc = f.getmetrics()
# Chasses en pixels de la table hdmx : celles qu'utilise Mac OS à cette taille (les
# avances fractionnaires arrondies faisaient se chevaucher lettres et espaces).
hdmx, cmap = {}, {}
try:
    from fontTools.ttLib import TTFont
    tt = TTFont(src)
    cmap = tt.getBestCmap()
    if 'hdmx' in tt and px in tt['hdmx'].hdmx: hdmx = tt['hdmx'].hdmx[px]
except Exception:
    pass
glyphes = []
# Latin-1 et les signes typographiques de Mac Roman utiles aux menus
EXTRAS = sorted({0x2318, 0x21e7, 0x2325, 0x2303, 0x232b, 0x2713} | {ord(bytes([c]).decode('mac_roman')) for c in range(0x80, 0x100)})
for code in list(range(0x20, 0x100)) + [c for c in EXTRAS if c in cmap]:
    if 0x7f <= code < 0xa0: continue
    ch = chr(code)
    masque, (ox, oy) = f.getmask2(ch, mode='1')
    nom = cmap.get(ord(ch))
    avance = hdmx[nom] if nom in hdmx else round(f.getlength(ch))
    # colonnes vides rognées ; l'encre reste dans la chasse quand elle y tient (cellules
    # des polices bitmap du Mac : pas de débord sur la lettre voisine ni sur l'espace)
    if masque.size[0] and masque.size[1]:
        cols = [x for x in range(masque.size[0]) if any(masque.getpixel((x, y)) for y in range(masque.size[1]))]
        if cols:
            g, d = cols[0], cols[-1] + 1
            masque = masque.crop((g, 0, d, masque.size[1]))
            ox += g
            w = d - g
            if w <= avance:
                if ox < 0: ox = 0
                if ox + w > avance: ox = avance - w
    l, h = masque.size
    lignes = []
    for y in range(h):
        bits = 0
        for x in range(l):
            if masque.getpixel((x, y)): bits |= 1 << (((l + 7) // 8) * 8 - 1 - x)
        lignes.append('%0*X' % (((l + 7) // 8) * 2, bits) if l else '')
    if not l or not h:
        # glyphe vide (espace) : un pixel blanc plutôt qu'une boîte nulle
        glyphes.append((code, avance, 1, 1, 0, 0, ['00']))
        continue
    # getmask2 : décalage du coin haut gauche depuis l'origine (haut de la ligne)
    glyphes.append((code, avance, l, h, ox, asc - oy - h, lignes))
with open(sortie, 'w') as o:
    o.write('STARTFONT 2.1\nFONT -platine-%s-medium-r-normal--%d-%d-75-75-p-%d-iso10646-1\n' % (famille, px, px * 10, px * 6))
    o.write('SIZE %d 75 75\nFONTBOUNDINGBOX %d %d 0 %d\n' % (px, px * 2, asc + desc, -desc))
    o.write('STARTPROPERTIES 4\nFONT_ASCENT %d\nFONT_DESCENT %d\nCHARSET_REGISTRY "ISO10646"\nCHARSET_ENCODING "1"\nENDPROPERTIES\n' % (asc, desc))
    o.write('CHARS %d\n' % len(glyphes))
    for code, av, l, h, x0, yb, lignes in glyphes:
        o.write('STARTCHAR U%04X\nENCODING %d\nSWIDTH %d 0\nDWIDTH %d 0\nBBX %d %d %d %d\nBITMAP\n' % (code, code, av * 1000 // px, av, l, h, x0, yb))
        o.write(''.join(li + '\n' for li in lignes))
        o.write('ENDCHAR\n')
    o.write('ENDFONT\n')
print(sortie, len(glyphes), 'glyphes')
