# NOTICE — Apple assets (read before redistributing)

Mac OS 9 is copyrighted by Apple Inc. This repository hosts Platine together
with the asset files the desktop actually loads at runtime — the icon packs,
the desktop pattern and wallpapers, the bitmap fonts and interface sounds —
which are all **derived from Mac OS 9**.

* They are **the property of Apple Inc.**, nothing else.
* They are hosted here **strictly for the personal use of the owner of a
  Mac OS 9 licence** (the project author), inside their own project, on
  their own machine — not for reuse, not for redistribution, and not as a
  substitute for owning Mac OS 9.
* If you are not the author: do not reuse these files, do not extract them,
  do not repackage them. Get your own copy of Mac OS 9 instead, or use your
  own artwork. Everything else in this repository is governed solely by its
  own terms (the code is Platine's work).

What is hosted and what stays out:

* **Hosted** (Apple property, strictly personal): `assets/` (icon packs,
  desktop pattern, fonts, sounds, prepared with `outils/preparer-assets.sh`),
  `systeme/polices/`, `systeme/curseurs/`, `systeme/themes/Mac-OS-9/`, and
  the real screenshots in `docs/img/public/`, taken by
  `outils/captures-publiques.sh` with those exact assets.
* **Not hosted** (extraction work, private): `ressources/` — everything
  forked out of the Mac OS 9 disks and applications whole (`rsrc` files,
  third-party programs, WinAmp, After Dark, …), and `audit/` — the machine
  audits and the Mac OS 9 "oracle" screens of the reference VM used for
  pixel-level fidelity checks. They remain on the author's machine, untracked.

The Platine code itself never *is* the Apple material: the window manager,
the Toolbox (`libptk`), the Finder and the applications — TextEdit,
Calculator, the QuickTime Player recreation, the StuffIt Expander
recreation — are our own C/X11 work; artwork drawn from scratch (for
example the Instantané camera icon in `outils/dessiner-icones.py`) is ours
too. If you build without `assets/` the desktop still runs, drawing neutral
chrome in place of the Apple material.

"Apple", "Macintosh" and "Mac OS" are trademarks of Apple Inc.; Platine is
an independent userspace project and is not affiliated with, endorsed by or
sponsored by Apple Inc.
