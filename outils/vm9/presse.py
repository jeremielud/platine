#!/usr/bin/env python3
"""Relève l'état enfoncé d'un contrôle de la VM Mac OS 9.

Usage : presse.py x y nom [dx dy] [dossier]

Le pointeur est amené sur (x, y) par ptr.py (la flèche est localisée par sa
forme à chaque pas) ; s'il n'y est pas à 1 px près, rien n'est appuyé. Puis le
bouton est maintenu, l'écran capturé (nom-enfonce.png), le pointeur sort de
(dx, dy) — 0 30 par défaut, choisir une zone neutre de la même fenêtre — et la
capture nom-dehors.png est prise avant le relâchement : un relâchement hors du
contrôle l'annule (TrackControl), l'état du contrôle ne change pas.
nom-repos.png est la capture sans maintien.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ptr  # noqa: E402

if len(sys.argv) < 4:
    print(__doc__)
    raise SystemExit(1)
x, y, nom = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
dx, dy = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (0, 30)
out = os.path.abspath(sys.argv[6] if len(sys.argv) > 6 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "audit", "macos9-ref", "presse"))
os.makedirs(out, exist_ok=True)


def shot(suffixe):
    chemin = os.path.join(out, "%s-%s.ppm" % (nom, suffixe))
    ptr.send("screendump " + chemin, 0.4)
    for _ in range(50):
        if os.path.exists(chemin) and os.path.getsize(chemin) > 1000:
            break
        time.sleep(0.05)
    time.sleep(0.1)
    from PIL import Image
    Image.open(chemin).save(chemin[:-4] + ".png")
    os.unlink(chemin)


p = ptr.goto(x, y)
if p is None or abs(p[0] - x) > 1 or abs(p[1] - y) > 1:
    print("pointeur non recalé (%s au lieu de (%d, %d)) : rien n'est appuyé" % (p, x, y))
    raise SystemExit(2)
print("pointeur :", p)
shot("repos")
ptr.send("mouse_button 1", 0.35)
shot("enfonce")
ptr.send("mouse_move %d %d" % (round(dx / ptr.GAIN), round(dy / ptr.GAIN)), 0.4)
shot("dehors")
ptr.send("mouse_button 0", 0.3)
print(os.path.join(out, nom + "-enfonce.png"))
