# VM Mac OS 9 — oracle de référence

La VM `~/Projets/eeepc-openbsd/macos9` (QEMU `mac99`, Mac OS 9.2.1, 800×600)
sert d'oracle visuel et comportemental. Les captures sont personnelles et ne
doivent pas être publiées (`audit/` reste hors dépôt).

Lancement : `~/Projets/eeepc-openbsd/macos9/vm.sh` ; écran VNC sur 127.0.0.1:5909
(`remote-viewer vnc://127.0.0.1:5909`), moniteur QEMU sur `vm.sock`.

## Capturer

```sh
./outils/vm9/capture.sh etat
# -> audit/macos9-ref/etat.png
```

Le script utilise `screendump` sur `vm.sock` et ImageMagick.

## Cliquer

```sh
./outils/vm9/click.py 200 35 clic
./outils/vm9/click.py 200 35 double
```

Il délègue à `macos9/gui.py` (recalage visuel du curseur ADB). `ptr.py` localise la
flèche par corrélation de forme quand l'écran clignote.

## Maintenir un clic (états enfoncés)

```sh
./outils/vm9/presse.py 120 200 case
# -> audit/macos9-ref/presse/case-repos.png, case-enfonce.png, case-dehors.png
```

Le pointeur est recalé, le bouton maintenu, l'écran capturé enfoncé, puis le pointeur
sort de 60 px avant le relâchement : comme sous Mac OS, un relâchement hors du contrôle
l'annule (TrackControl), l'état du contrôle ne change pas.

## Comparer

```sh
./outils/vm9/diff.py audit/macos9-ref/finder-active.png \
    docs/img/finder-bureau.png /tmp/diff.png
```

## États de référence déjà capturés

- `desktop.png` : bureau 800×600, icônes, Control Strip.
- `finder-active.png` : fenêtre Finder active, barre de titre, scrollbar.
- `titlebar.png`, `titlebar-buttons.png` : barre de titre et cases 4×/8×.
- `apple-menu.png` : menu Pomme avec icônes.
- `get-info.png` : Lire les informations (nom, kind, taille, où, dates,
  étiquette, commentaires).
- `current.png`, `desktop-clean2.png` : états de travail.

## Mesures déjà codées

- Barre de menus 20 px, fond #ddd, blanc en haut, #999 puis noir en bas,
  titres Charcoal 12 gras, horloge ; Platine dessine la Pomme des assets (Tux en repli).
- Barre de titre 21 px, cases 11×11, rayures #fff/#777, plaque #ccc.
- Menus : bord noir/blanc/noir/blanc, items 16 px gras.
- Coins d'écran 8×8 avec le masque exact de Mac OS 9.
- Finder : contenu blanc, status, icônes 32×32, étiquettes à fond blanc sur
  le bureau, scrollbar 16 px.
