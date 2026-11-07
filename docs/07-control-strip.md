# Control Strip (`platine-strip`)

Processus séparé, fenêtre sans gestionnaire, indépendant du Finder (contrat 10 de
`00-essence.md`). Mesures relevées sur la VM Mac OS 9.2 (fichier de capture 800×600) :

- Hauteur 24 px : rangée noire en haut et en bas, colonne noire à gauche.
- Case de repli x=0..13, séparateur x=14 ; flèche gauche x=15..27, séparateur x=28.
- Module : 30 px + séparateur noir ; relief : colonne #c0, puis blanc à gauche et en haut,
  #a0 puis #80 à droite et en bas. Icône 16×16 à +4 ; flèche noire de 4×8 à +22, y=8.
- Module enfoncé : fond #80, bord haut et gauche #40, bas et droite #c0.
- Flèche sans objet : cellule creuse (#80 en haut à gauche, blanc en bas à droite).
- Onglet de 18 px biseauté : silhouette découpée par l'extension Shape.
- Volume : panneau 22×66 (bord noir, fond #dd, ombre noire à droite) qui mord de 3 px sur
  le module ; rainure de 60 px ; curseur de 13 px à trois rainures.

Comportement : menus dessinés par le WM (`_PLATINE_CONTEXT`), case = repli, onglet = dépli,
glisser l'onglet = largeur, Option-glisser = hauteur. Réglages dans `~/.config/platine-strip`.

Préfixes de menu ajoutés au WM : `~` élément grisé, `√ ` coche dans la marge.
`PLATINE_APERCU=1` : les réglages ne touchent pas à la machine.

Tableau de bord **Control Strip** (`platine-tableau controle`, entrée du menu Pomme et
fichier du dossier « Tableaux de bord ») :

- Afficher/Masquer la barre ; le panneau reprend la disposition de Mac OS 9 (groupes
  Afficher/Masquer et Réglages de la police), sans curseur de largeur ni barre de défilement ;
- écrit `~/.config/platine-strip` puis envoie `SIGUSR1` au processus, qui relit le fichier
  et se redimensionne à chaud (`charger_config` + `reagencer`) ;
- la position et le nombre de modules se règlent en glissant l'onglet de la barre ;
- la police et la taille sont affichées mais fixes : la barre utilise ses polices bitmap
  d'origine ;
- « Définir la touche… » capture la combinaison suivante ; Échap annule et les modificateurs
  seuls sont ignorés. Le raccourci initial est `commande + contrôle + S` ;
- le raccourci est enregistré sur la seconde ligne facultative de `~/.config/platine-strip`,
  sous la forme `touche <nom X11> <masque décimal>` (`touche s 12` par défaut). Une ancienne
  configuration sans cette ligne conserve le raccourci initial. Le processus remplace son
  raccourci X11 au `SIGUSR1`, y compris avec Caps Lock ou Num Lock actifs.

## Personnalisation et retours (2026-11-05)

- Un survol d'environ une demi-seconde affiche une bulle avec le nom et l'état du module.
  L'état est mis en cache et actualisé périodiquement ; les commandes système ne sont pas
  relancées à chaque mouvement. Cliquer, quitter la barre ou ouvrir un menu masque la bulle.
- L'appui et le relâchement utilisent les sons Toolbox `btnp`/`btnr`, le repli et le dépli
  utilisent `tabp`/`tabr`. Ils respectent la catégorie « Contrôles » du thème sonore.
- `tests/run-strip.sh` vérifie les bulles, les sons, la capture avec annulation, la persistance,
  le rechargement et l'absence de déclenchement par la touche seule.

## Modules reliés (2026-11-05)

- **AirPort** distingue désactivé, activé et connecté ; active/désactive la radio et affiche les
  réseaux connus, avec coche sur le réseau courant. Choisir un réseau appelle `wpa_cli
  select_network` avec son identifiant numérique, jamais avec son nom inséré dans une commande.
- **Diction** affiche son état réel, démarre/arrête `platine-diction`, lit la sélection sur demande
  et conserve le réglage `diction` pour la session suivante.
- **Résolution** énumère les modes `xrandr`, coche le mode courant, applique le mode choisi et ouvre
  le tableau Écran. Les entrées Économiseur des modules Batterie et Énergie ouvrent aussi leur panneau.
- `tests/run-strip.sh` pilote ces menus sous Xvfb avec de faux outils système et `PLATINE_APERCU=1` ;
  il vérifie également le cycle et la persistance du service Diction. Aucun test ne change le réseau,
  l'affichage ou la synthèse vocale de la machine hôte.

Jalon C validé sur PC par la suite complète `make test`, puis sur le 701 NetBSD dans une session
Xvfb isolée : raccourci initial, capture et rechargement de `touche F7 5`, bulle et codes sonores
`btnp`, `btnr`, `tabp`, `tabr`. La session réelle `:0` n'a pas été remplacée.

## Modules externes (2026-11-07)

Comme les « Control Strip Modules » de Mac OS 9, un module peut être ajouté sans toucher au
Control Strip : un fichier dans `Dossier Système/Modules Control Strip`, posé par exemple par
l'Installateur (composant de destination `Dossier Système`). Le Strip lit ce dossier au
démarrage et à chaque SIGUSR1 — l'Installateur le lui envoie après une installation ou une
désinstallation (pid dans `~/.cache/platine-strip.pid`) ; un nouveau module est visible
aussitôt, après les modules intégrés, par ordre alphabétique.

```
PLATINE-MODULE-1
nom=Instantané
icone=Instantané.xpm            fichier 16×16 voisin, sinon icône des assets
etat=Option Majuscule 3 pour capturer      texte de la bulle
element=Capturer l'écran…|@Dossier Système/Extensions/Instantané|platine-instantane|maintenant
element=-
element=Ouvrir le dossier Captures|ouvrir:Captures
```

Commande d'un élément : `@chemin|repli|arguments` (programme du volume, sinon le repli du
PATH) ou `ouvrir:dossier` (le Finder ouvre ce dossier du volume, créé s'il manque).
Le paquet « Installation d'Instantané » apporte ainsi son module (icône d'appareil photo).
