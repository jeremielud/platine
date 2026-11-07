# Tableaux de bord (2026-11-04)

Les panneaux sont un seul programme, `platine-tableau`, posé dans
`Dossier Système/Tableaux de bord/` sous un nom par tableau (le nom du fichier choisit
le panneau, décision D9) ; le dossier est aussi accessible par **Pomme > Tableaux de
bord** (alias dans `Éléments du menu Pomme`).

## Icônes

- Icônes 32×32 Mac OS 9 pour Apparence, Clavier, **Diction (icône Speech, la bouche)**,
  Son, Souris, Écran, Control Strip, Date et heure, Économiseur d'énergie.
- Les gris des panneaux (souris, écran, bouche Speech) sont conservés **opaques** :
  l'ancienne conversion des gris 0x11..0x77 en noir translucide les transformait en
  silhouettes sombres et délavait les couleurs.
- Icône spéciale `dossier-tableaux` pour le dossier lui-même.
- Le WM reprend les icônes 16×16 dans la barre de titre, le menu d'application et le
  sous-menu Pomme.

## Panneaux

- **Apparence** : Apple platine, surbrillance **Pourpre** par défaut, variante Lavande,
  polices, bureau, son, options.
- **Clavier**, **Son**, **Souris**, **Écran**, **Diction** : voir les captures de la VM
  et `docs/05-macos9-metriques.md`.
- **Control Strip** : afficher/masquer, groupes façon OS 9, police fixe ;
  position/largeur en glissant l'onglet ; rechargement à chaud par `SIGUSR1` ;
  raccourci configurable par capture clavier (Échap annule), `commande + contrôle + S`
  par défaut.
- **Date et heure** : trois onglets (Date et heure / Fuseau / Horloge), groupes
  Toolbox, champs de réglage, bouton `Régler` (`privilege date AAAAMMJJhhmm`),
  bouton `Heure du réseau` (`privilege ntp` : `ntpdate -b -p 1 -t 2`, plafonné à 8 s,
  sans ntpd permanent), ligne d'état (succès, échec, saisie incomplète avec son
  d'erreur), heure affichée avec l'abréviation du fuseau. Le menu Fuseau (12 zones,
  `Europe/Paris` par défaut) s'applique **immédiatement** : clé `fuseau` des réglages
  et `/etc/localtime` (`privilege fuseau`) pour la console et syslog. Secondes et
  format 24 h. Pas de barre de défilement.
  Horloge matérielle : RTC en UTC (`kern.rtc_offset=0`) ; sous NetBSD le noyau la
  réécrit à chaque `settimeofday` (`date`, `ntpdate -b`), il n'existe pas de `hwclock`.
- **Économiseur d'énergie** : deux onglets Toolbox (Veille / Options).
  Veille : extinction de l'écran par DPMS (`ecran_minutes`, appliquée immédiatement),
  bouton `Suspendre` (ACPI S3, comme Spécial ▸ Suspendre). Options : action du
  couvercle (éteindre l'écran / suspendre / rien) et du bouton d'alimentation
  (demander — alerte Mac OS 9 via `platine-alerte alimentation`, défaut, repli sur
  l'extinction sans session Platine — / éteindre / suspendre / rien), écrites dans `/etc/platine-energie` par
  `privilege energie` et lues à chaque événement par les scripts powerd de Platine
  (`systeme/netbsd/powerd`, installés dans `/etc/powerd/scripts`, originaux gardés en
  `.netbsd`). Le 701 (i915) n'a pas de variable wscons `backlight` : le couvercle
  éteint l'écran par `xset dpms force off` dans la session de l'utilisateur de la
  console, et le rallume à l'ouverture. L'onglet Planification (vide) et la veille
  automatique de l'ordinateur (dépendait de xautolock/platine-veille, absents de
  NetBSD) ont été retirés. Pas de barre de défilement.
- **Dossier** : fenêtre 480×380, grille 3×3, barres de défilement Finder de 16 px
  (trait + 15) uniquement si nécessaire.

## Réglages

`~/.config/platine/reglages` contient notamment :

```
fuseau Europe/Paris
horloge_24h 1
horloge_secondes 0
ecran_minutes 5
surbrillance Pourpre
```

`platine-session` exporte `TZ` depuis `fuseau` au démarrage ; ensuite,
`ptk_fuseau_suivre()` (libptk) relit la clé dès que le fichier change (inode, taille,
date à la nanoseconde) et met à jour `TZ` + `tzset()` dans le WM (horloge), le Finder
et Lire les informations ; les applications lancées ensuite par le WM héritent du
nouveau `TZ`. `ptk_fuseau_valide()` refuse les chemins absolus, `..` et les zones
absentes de `/usr/share/zoneinfo`. `platine-session` applique aussi `ecran_minutes` /
`veille_minutes` au démarrage. À l'initialisation de chaque application,
`libptk` applique `surbrillance`, `variante`, `police_grande`, `police_petite`,
`police_vues` et `lissage`. Geneva et Charcoal utilisent leurs bitmaps par
défaut ; les autres choix et le lissage chargent les TTF de `assets/polices`.

`theme_sonore Aucun` coupe les sons d'interface. Les alertes restent actives et
jouent `alertes/<son_alerte>.wav` (`simplebeep` par défaut). Les cases
`son_menus`, `son_fenetres`, `son_controles` et `son_finder` filtrent leur
catégorie à chaque lecture. Le volume d'alerte et les autres options
d'Apparence restent seulement enregistrés. Un changement de réglage est pris
en compte au prochain lancement de l'application.

## Limites

- La police de la Control Strip est fixe ; le raccourci est configurable depuis le panneau.
- Pas de planification ni de veille automatique (l'onglet Planification a été retiré) ;
  la suspension S3 reste à valider sur le 701.
- Le volume d'alerte et certaines options d'Apparence sont enregistrés sans effet.
- Les tableaux propres aux portables (Batterie, Trackpad) viendront de la VM PowerBook
  (feuille de route).
