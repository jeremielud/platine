# Graphisme OpenGL sur le 701 — mesures et règles

Ce que la Théière (`platine-theiere`) a appris de l'i915GM, et ce que tout
programme graphique Platine (jeux, démos, visualiseurs) en hérite par la
Toolbox GL (`toolbox/ptk_gl.{h,c}`) et par la session.

## Le matériel

- Intel 915GM, pilote Mesa 21.3.7 classique (`Mesa DRI Intel(R) 915GM
  x86/MMX/SSE2`), rendu direct ; **OpenGL 1.4** annoncé, mais
  `GL_ARB_vertex_buffer_object`, `GLX_MESA_swap_control`, `GLX_SGI_swap_control`
  et `GLX_EXT_swap_control` présents ;
- **pas de traitement des sommets matériel** : transformation et éclairage de
  chaque sommet se font sur le processeur (Celeron M 900 MHz, un cœur) ;
- noyau NetBSD à `hz = 100` : `select`/`usleep` arrondissent au tick de 10 ms.

## Mesures (2026-11-07, Théière 480×340, temps processeur sur 10 s)

| Version | i/s | Processeur |
|---|---|---|
| `glEvalMesh2` dans une liste d'affichage | 37 | 80 % |
| géométrie précalculée, cadence par `select` 16 ms | 31 | — |
| géométrie précalculée, synchro verticale | 60 | 27 % |
| sommets uniques indexés en VBO, sans `GL_NORMALIZE` | 60 | 26 % |
| fenêtre repliée (WindowShade) ou iconifiée | — | 0 % |

Décomposition, à 60 i/s :

| Variante | Processeur |
|---|---|
| boucle vide (effacement + échange), 160×120 → 780×440 | 6 → 8 % |
| théière sans éclairage | 23 % |
| théière sans spéculaire | 26 % |
| grille 3 (512 sommets) | 12 % |
| grille 7, celle de GLUT (2 048 sommets) | 25 % |
| grille 14 (7 200 sommets) | 77 %, 51 i/s |

Sans effet mesurable : VBO ou tableaux côté client, triangles indexés ou bande
unique cousue, élimination des faces arrière, taille de la fenêtre. Le coût suit
le **nombre de sommets distincts** : environ 1 % du processeur par 120 sommets
à 60 i/s, au-dessus d'une base de 6 à 8 %.

Programmes tiers : `glxgears` tourne à 60 i/s pour 16 % avec la synchro
verticale, à 234 i/s pour 63 % sans (`vblank_mode=0`).

## Règles

1. **La synchro verticale donne la cadence.** Intervalle d'échange 1, et
   `glXSwapBuffers` attend le retour de trame. Pas de minuterie pour rythmer
   les images : à 100 Hz, un délai de 16 ms en devient 20, et un reste de
   quelques millisecondes fait manquer une trame sur deux (31 i/s).
2. **Le mouvement suit l'horloge** (`dt`), jamais le nombre d'images ni les
   événements : la vitesse reste la même quand la souris bouge, quand la
   cadence baisse ou après une pause.
3. **Hors écran, rien.** Fenêtre iconifiée, repliée ou entièrement recouverte :
   aucun rendu, sommeil sur la connexion X ; à la reprise, `dt = 0`, pas de saut.
4. **Géométrie calculée une fois.** Ni évaluateurs (Mesa les réévalue sur le
   processeur à chaque appel, même compilés en liste), ni mode immédiat image
   par image. Sommets entrelacés position + normale, indices, VBO par
   l'extension ARB (le pilote dit 1.4, il ne faut pas tester la version).
5. **Budget de sommets.** C'est le seul levier qui reste sur l'i915GM : compter
   environ 120 sommets distincts par point de processeur à 60 i/s. Un jeu qui
   vise 25 % du processeur a donc environ 2 000 sommets par image ; au-delà,
   réduire la finesse (niveaux de détail) plutôt que chercher un autre chemin
   d'envoi.
6. **Normales unitaires, pas d'échelle dans la matrice** : intégrer les échelles
   aux sommets et se passer de `GL_NORMALIZE`.
7. **L'éclairage fixe est bon marché** (3 points pour une lumière
   directionnelle avec spéculaire) : inutile de s'en priver.

## Ce que fournit Platine

- **Toolbox GL** (`ptk_gl.h`) : `ptk_gl_ouvrir` (fenêtre GLX citoyenne du
  bureau : nom, icône, menus dans la barre du WM, `WM_DELETE_WINDOW`, synchro
  verticale), `ptk_gl_image` (événements, cadence, pause hors écran, `t` et
  `dt`), `ptk_gl_echanger` (échange, i/s, titre facultatif, retour à l'attente
  si le pilote ignore l'intervalle), `ptk_gl_maillage`/`ptk_gl_dessiner`
  (géométrie statique indexée, VBO ARB ou tableaux), `ptk_gl_perspective`.
  Édition de liens : `build/ptk_gl.o -lGL -lX11 -lm`, ni Xft ni briques de dessin.
- **Session** (`systeme/platine-session`) : `vblank_mode=2` — synchro verticale
  par défaut pour tous les programmes GL, jeux tiers compris ; un programme peut
  demander l'intervalle 0, une valeur posée avant la session est respectée.

Les règles 2, 3 et 5 restent du ressort des programmes tiers : la session ne
peut que leur imposer la synchro.
