# Menus (`platine-wm`) — mesures Mac OS 9.2

Relevées sur la VM (menu Fichier, menu contextuel du bureau).

- Élément 16 px, séparateur 6 px (#888 au rang 2, blanc au rang 3, toute la largeur).
- Texte à 21 px du bord noir, dernier rang des capitales à 11 px du haut de l'élément.
- Menu déroulant : pas de rang noir propre, la barre de menus en tient lieu.
  Contextuel et sous-menu : rang noir en haut.
- Liseré blanc en haut et à gauche, #99 en bas et à droite, bord noir, ombre #22 de 1 px
  décalée (rang 1 ou 2 selon le type, colonne 2 en bas) : silhouette découpée par Shape.
- Surbrillance : toute la largeur intérieure, #333399 ; colonne droite #000088.
- Raccourci : même police, ⌘ en colonne à 40 px du bord noir. Bord noir à 12 px après le
  texte, 48 s'il y a un raccourci ou une flèche.
- Flèche de sous-menu : pleine, 6×11, à 2 px du haut de l'élément, pointe à 7 px du bord.
- Élément grisé #777 ; choix : l'élément clignote deux fois.

Syntaxe du modèle (`_PLATINE_MENU`, `_PLATINE_CONTEXT`) : `~` grisé, `√ ` coche,
`\x1f` début de sous-menu, `\x1e` entre sous-éléments, `\t` raccourci.

Navigation clavier : les fenêtres de menu (menu principal, sous-menu et menu contextuel)
doivent sélectionner `KeyPressMask`. Avec `XGrabKeyboard`, les touches sont livrées à la
fenêtre de grab ; sans ce masque, les flèches et Entrée étaient perdues. Corrigé le
2026-11-04 (`open_popup`, `open_submenu`, `open_context`).

Non mesuré : aspect d'une case de fenêtre enfoncée (`ptk_title_button`, approximation), et
le léger délai d'ouverture des sous-menus (immédiat ici).

# Sons

`ptk_son(code)` : activés par `PLATINE_SONS=1`, `PLATINE_SONS_JOURNAL=fichier` note chaque
code joué (sert aux tests du contrat 7). `make test` reste silencieux.
