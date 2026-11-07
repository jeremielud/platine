#!/usr/bin/env python3
"""Diff visuel entre une capture Mac OS 9 et une capture Platine."""
import sys
from PIL import Image, ImageChops

if len(sys.argv) < 3:
    print("usage: diff.py référence.png platine.png [sortie.png]")
    raise SystemExit(1)
a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
if a.size != b.size:
    b = b.resize(a.size)
d = ImageChops.difference(a, b)
out = sys.argv[3] if len(sys.argv) > 3 else "diff.png"
d.save(out)
print("diff max:", d.getextrema())
print("sortie:", out)
