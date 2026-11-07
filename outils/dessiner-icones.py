#!/usr/bin/env python3
"""dessiner-icones.py : icônes propres à Platine (pas d'origine Mac OS 9), dessinées au
pixel dans l'esprit Platinum, en PNG dans systeme/icones/ — preparer-assets.sh les
convertit (argb 32/16 pour le Finder, xpm 16 pour les menus et la barre, xpm 32 pour les
alertes).

  app-instantane : Instantané, appareil photo (boîtier gris biseauté, viseur,
                   objectif bleu à reflet, flash, déclencheur rouge)
"""
import os
import sys
from PIL import Image

NOIR = (0, 0, 0, 255)


def appareil(taille):
    """appareil photo, dessiné au pixel près pour 32 et 16"""
    im = Image.new('RGBA', (taille, taille), (0, 0, 0, 0))
    p = im.putpixel
    gris = {k: (k, k, k, 255) for k in (0x22, 0x55, 0x77, 0x88, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff)}

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                p((x, y), c)

    def cadre(x0, y0, x1, y1, c):
        for x in range(x0, x1 + 1):
            p((x, y0), c)
            p((x, y1), c)
        for y in range(y0, y1 + 1):
            p((x0, y), c)
            p((x1, y), c)

    def disque(cx, cy, r, c):
        for y in range(taille):
            for x in range(taille):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    p((x, y), c)

    if taille == 32:
        # viseur, puis boîtier aux coins coupés
        rect(10, 5, 19, 8, gris[0xbb])
        cadre(10, 5, 19, 9, NOIR)
        rect(11, 6, 18, 6, gris[0xee])
        rect(3, 8, 28, 9, (0xcc, 0x33, 0x33, 255))	# déclencheur, à gauche
        rect(3, 8, 6, 9, (0xcc, 0x33, 0x33, 255))
        rect(7, 8, 28, 9, (0, 0, 0, 0))
        cadre(3, 7, 6, 9, NOIR)
        rect(4, 8, 5, 8, (0xee, 0x55, 0x55, 255))
        rect(1, 10, 30, 27, gris[0xcc])
        for x in range(2, 30):
            p((x, 9), NOIR)
            p((x, 28), NOIR)
        for y in range(10, 28):
            p((0, y), NOIR)
            p((31, y), NOIR)
        p((1, 10), NOIR); p((30, 10), NOIR); p((1, 27), NOIR); p((30, 27), NOIR)
        # biseau : lumière en haut à gauche, ombre en bas à droite
        for x in range(2, 30):
            p((x, 10), gris[0xff])
            p((x, 27), gris[0x77])
            p((x, 26), gris[0xaa])
        for y in range(11, 27):
            p((1, y), gris[0xff])
            p((30, y), gris[0x77])
            p((29, y), gris[0xaa])
        # bande gainée
        rect(2, 15, 28, 22, gris[0x88])
        for x in range(2, 29):
            if x % 2 == 0:
                p((x, 16), gris[0x77])
                p((x, 21), gris[0x77])
        # flash, en haut à droite
        rect(23, 12, 28, 14, gris[0xff])
        cadre(22, 11, 29, 14, NOIR)
        rect(23, 12, 24, 13, (0xff, 0xff, 0xcc, 255))
        # objectif : bague noire, monture, verre bleu, reflet
        disque(15.5, 18.5, 8.5, NOIR)
        disque(15.5, 18.5, 7.5, gris[0x55])
        disque(15.5, 18.5, 6.0, gris[0x22])
        disque(15.5, 18.5, 5.0, (0x33, 0x33, 0x99, 255))
        disque(15.5, 18.5, 3.2, (0x66, 0x66, 0xcc, 255))
        disque(15.5, 18.5, 1.6, (0x22, 0x22, 0x66, 255))
        rect(12, 15, 13, 15, gris[0xff])
        p((12, 16), gris[0xff])
        p((18, 21), (0x99, 0x99, 0xff, 255))
    else:
        # viseur et déclencheur sur le dessus
        rect(5, 2, 9, 4, gris[0xbb])
        cadre(5, 2, 9, 4, NOIR)
        rect(6, 3, 8, 3, gris[0xee])
        rect(1, 3, 3, 3, (0xcc, 0x33, 0x33, 255))
        # boîtier : trait noir, lumière en haut à gauche, ombre en bas à droite
        rect(0, 4, 15, 13, gris[0xcc])
        cadre(0, 4, 15, 13, NOIR)
        for x in range(1, 15):
            p((x, 5), gris[0xff])
            p((x, 12), gris[0x88])
        for y in range(5, 13):
            p((1, y), gris[0xff])
            p((14, y), gris[0x88])
        # flash
        rect(11, 6, 13, 7, gris[0xff])
        p((11, 7), (0xff, 0xff, 0xcc, 255))
        # objectif
        disque(7.0, 9.0, 3.9, NOIR)
        disque(7.0, 9.0, 3.0, (0x33, 0x33, 0x99, 255))
        disque(7.0, 9.0, 1.5, (0x66, 0x66, 0xcc, 255))
        p((5, 7), gris[0xff])
        p((5, 8), gris[0xff])
        p((6, 7), gris[0xff])
    return im


def main():
    dossier = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'systeme', 'icones')
    os.makedirs(dossier, exist_ok=True)
    appareil(32).save(os.path.join(dossier, 'app-instantane-32.png'))
    appareil(16).save(os.path.join(dossier, 'app-instantane-16.png'))


if __name__ == '__main__':
    main()
