# L'essence de Mac OS 9 — contrats à reproduire

Ce document n'est pas une description exhaustive de Mac OS 9. C'est la liste des
contrats observables qui font qu'un utilisateur reconnaît l'interface, et qui
doivent être implémentés **une seule fois**, dans la couche système.

Chaque contrat doit avoir un test de conformité (capture, événement, son, état).

| # | Contrat | Implication pour Platine | Test |
|---|---|---|---|
| 1 | **Barre de menus globale**, 20 px, propriété du premier plan : menu Pomme, menus de l'application, horloge, menu d'application | Service de menus dans le WM ; les applications publient un modèle, le WM dessine et renvoie les commandes | Changer d'application change la barre ; aucune barre dans les fenêtres |
| 2 | **Premier plan** : clavier à l'application active, clic qui active et passe, ⌘Tab, menu d'application | Modèle de focus explicite ; pas de XTest/XSendEvent pour contourner | Clic sur fenêtre inactive ; ⌘Tab ; menu d'application |
| 3 | **Fenêtres spatiales** : un dossier = une fenêtre, géométrie et positions d'icônes persistantes | Finder spatial ; base de métadonnées par dossier | Rouvrir un dossier ; déplacer une icône ; redémarrer |
| 4 | **Métadonnées Mac** : type/créateur, icône par type, alias, étiquettes, commentaires, corbeille | Base de types + icônes ; alias ; corbeille ; Lire les informations | Créer un document, changer son icône, le mettre à la corbeille |
| 5 | **Dialogues** : modalité par application, alerte avec icône, bouton par défaut, Entrée/Échap, progression annulable | Bibliothèque de dialogues commune ; pile modale par application | Alerte, Échap, Entrée, annulation |
| 6 | **Contrôles Platinum** : boutons, ascenseurs, onglets, cases, radios, champs, listes ; états normal/enfoncé/grisé/actif | Widgets dessinés par `libptk`, métriques exactes, navigation clavier | Capture de chaque état ; test clavier |
| 7 | **Sons d'état** : 68 sons d'interface par code (`fnew`, `wmov`, `wcls`, `wzmi`, `wcol`…), 15 alertes, volume | Démon son ; API `ptk_sound_play(code)` ; événements système | Journal des sons émis pour un scénario |
| 8 | **Apparence = moteur de thème** : Platinum, variantes de couleurs, motifs, polices, options (Smart Scrolling, WindowShade) | Moteur de thème procédural, pas des PNG 9 tranches | Changer de variante ; changer de police ; redémarrer |
| 9 | **Extensions et panneaux** : plugins, Extensions Manager, jeux, Startup Items, menu Pomme alimenté par un dossier | ABI de plugins ; manifeste ; activation/désactivation | Activer/désactiver une extension ; redémarrer |
| 10 | **Control Strip** : objet système flottant, modules, repli, indépendant du Finder | Processus `platine-strip` ; modules plugins | Replier/déplier ; cliquer un module |
| 11 | **Démarrage/arrêt** : carillon, Happy Mac, « Bienvenue sur Mac OS », progression, extensions ; arrêt propre, redémarrage, force quit | Session + splash + service son ; arrêt synchronisé | Boot complet ; arrêt ; redémarrage ; ⌘⌥Échap |
| 12 | **Cohérence** : mêmes raccourcis, mêmes métriques, mêmes sons ; une application ne casse pas le shell | Une seule implémentation des contrats ; shell/Finder séparés | Crash d'une application ; le bureau survit |

## État au 2026-11-07

| # | État | Scénarios |
|---|---|---|
| 1 | livré : barre de 20 px, menu Pomme alimenté par son dossier, horloge, menus des applications Platine ; menu générique pour les autres | `xvfb`, `pomme` |
| 2 | livré : clic qui active et passe, `WM_TAKE_FOCUS`, menu d'application ; pas de ⌘Tab (Mac OS 9 n'en a pas) ; sélecteur à calquer sur Mac OS 9 | `focus` |
| 3 | livré : une fenêtre par dossier, géométrie, vue, positions, disposition et tri persistants ; bureau sur un vrai dossier (D10) | `finder`, `grille`, `finder-bureau` |
| 4 | en grande partie : type/créateur, icônes par famille, alias, commentaires, Corbeille ; manquent les icônes personnalisées et les étiquettes en couleur | `finder-types`, `finder-alias`, `finder-trash` |
| 5 | livré : alertes, bouton par défaut, Retour/Échap/⌘., Standard File (Ouvrir, Enregistrer, Choisir un dossier), progression avec Arrêter | `toolbox`, `enregistrer`, `installateur` |
| 6 | livré : contrôles au pixel, états actif/inactif/grisé, clavier ; états enfoncés de la case, du radio et des petites flèches à relever | `toolbox` |
| 7 | livré : codes Platinum, alertes, filtrage par catégorie, journal de test | `reglages`, `toolbox`, `strip` |
| 8 | partiel : surbrillance, variante, polices, lissage, motif du bureau ; pas encore de moteur de thème complet | `reglages` |
| 9 | partiel : menu Pomme par dossier, Éléments de démarrage, modules Control Strip externes ; pas d'Extensions Manager | `pomme`, `installateur` |
| 10 | livré : processus séparé, modules, repli, onglet, modules externes | `strip` |
| 11 | partiel : arrêt et redémarrage propres, application têtue, Forcer à quitter ; l'écran de démarrage Mac OS 9 est en pause sur le 701 | `session` |
| 12 | livré : une seule Toolbox pour tous ; le Finder forcé à quitter est relancé | `session` |

## Ce qu'on ne reproduit pas

- Le multitâche coopératif : on garde un OS préemptif, on émule les contrats
  visibles (premier plan, modalité).
- La compatibilité binaire 68k/PEF, la ROM, HFS/HFS+, AppleTalk.
- Les resource forks comme format natif : on peut les importer, pas en faire le
  stockage du système.
- Le mode mono-utilisateur sans sécurité : on garde des comptes utilisateurs.

## Sources

- `../eeepc-openbsd/macos9/rapport-fonctionnement.md`
- `../eeepc-openbsd/macos9/rapport-barres-menus-extensions.md`
- Captures et mesures de `audit/` (hors dépôt).
