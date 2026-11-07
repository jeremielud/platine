#!/usr/bin/env python3
"""Pointeur absolu fiable pour la VM Mac OS 9.

Le curseur ADB est dessiné dans le framebuffer : on le localise par
corrélation de forme (flèche noire + liseré clair), puis on bouge la souris
par petits pas relatifs. Plus fiable que la différence d'images quand le
contenu de l'écran clignote.
"""
import os
import socket
import time

import numpy as np
from PIL import Image

VM = os.environ.get("VM", os.path.expanduser(
    "~/Projets/eeepc-openbsd/macos9"))
SOCK = os.path.join(VM, "vm.sock")
GAIN = 1.15

# Flèche Mac OS 9 : 13x12, extraite d'une capture propre.
ARROW = [
    "##...........",
    "###..........",
    "####.........",
    "#####........",
    "######.......",
    "###..........",
    ".##..........",
    "..##.........",
    "..##.........",
    "...##........",
    "...##........",
    "....##.......",
]
T = np.array([[c == "#" for c in row] for row in ARROW], dtype=bool)
TH, TW = T.shape

sock = socket.socket(socket.AF_UNIX)
sock.connect(SOCK)


def send(cmd, delay=0.03):
    sock.sendall((cmd + "\n").encode())
    time.sleep(delay)


def grab():
    path = os.path.join(VM, "ptr-probe.ppm")
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    send("screendump " + path, 0.05)
    for _ in range(200):
        if os.path.exists(path) and os.path.getsize(path) > 1000:
            time.sleep(0.05)
            try:
                im = Image.open(path)
                im.load()
                os.unlink(path)
                return np.array(im.convert("L"))
            except Exception:
                pass
        time.sleep(0.04)
    raise RuntimeError("screendump impossible")


def locate(a=None):
    a = grab() if a is None else a
    H, W = a.shape
    dark = (a < 90).astype(np.int32)
    score = np.zeros((H - TH + 1, W - TW + 1), dtype=np.int32)
    for ty in range(TH):
        for tx in range(TW):
            view = dark[ty:H - TH + ty + 1, tx:W - TW + tx + 1]
            score += view * (3 if T[ty, tx] else -2)
    y, x = np.unravel_index(int(np.argmax(score)), score.shape)
    best = int(score[y, x])
    if best < 55:
        return None, best
    return (int(x), int(y)), best


def reset():
    for _ in range(40):
        send("mouse_move -80 -80", 0.006)
    time.sleep(0.4)
    p, _ = locate()
    return p


def goto(x, y, tries=14):
    for _ in range(tries):
        p, _ = locate()
        if p is None:
            send("mouse_move 120 120", 0.2)
            continue
        dx, dy = x - p[0], y - p[1]
        if abs(dx) <= 1 and abs(dy) <= 1:
            return p
        sx, sy = round(dx / GAIN), round(dy / GAIN)
        if abs(sx) < 2:
            sx = 2 if dx > 0 else -2
        if abs(sy) < 2:
            sy = 2 if dy > 0 else -2
        n = max(abs(sx), abs(sy)) // 6 + 1
        for i in range(n):
            send("mouse_move %d %d" %
                (sx * (i + 1) // n - sx * i // n,
                 sy * (i + 1) // n - sy * i // n), 0.01)
        time.sleep(0.26)
    p, _ = locate()
    return p


def click(n=1):
    for _ in range(n):
        send("mouse_button 1", 0.08)
        send("mouse_button 0", 0.12)
    time.sleep(0.3)


if __name__ == "__main__":
    import sys
    if len(sys.argv) >= 3:
        p = goto(int(sys.argv[1]), int(sys.argv[2]))
        print(p)
        if len(sys.argv) >= 4 and sys.argv[3]:
            click(2 if sys.argv[3] == "double" else 1)
    else:
        print("pos:", locate())
