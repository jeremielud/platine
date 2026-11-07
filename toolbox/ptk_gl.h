/*
 * ptk_gl.h — Toolbox GL de Platine : ce qu'il faut à un programme OpenGL
 * (jeu, démo, visualiseur) pour bien tourner sur le bureau et sur le 701.
 *
 * Tirée des mesures de la Théière sur l'i915GM (OpenGL 1.4, Mesa classique,
 * Celeron M 900 MHz mono-cœur, noyau NetBSD à 100 Hz) — voir
 * docs/11-graphisme-gl.md :
 *
 *  - la synchro verticale donne la cadence (glXSwapBuffers attend le retour de
 *    trame) : pas de minuterie, dont le noyau arrondit les délais à 10 ms ;
 *  - le mouvement suit l'horloge (dt), pas le nombre d'images ;
 *  - fenêtre iconifiée ou recouverte : aucun rendu, sommeil sur les événements ;
 *  - l'i915GM n'a pas de traitement des sommets matériel : chaque sommet
 *    envoyé est transformé et éclairé sur le processeur. Géométrie statique
 *    calculée une fois, sommets uniques et indices, dans un tampon de sommets
 *    (GL_ARB_vertex_buffer_object, présent bien que le pilote annonce 1.4) ;
 *  - ni évaluateurs (glEvalMesh) ni mode immédiat image par image.
 *
 * Utilisation :
 *
 *	PtkGl g;
 *	if (ptk_gl_ouvrir(&g, "Théière", "app", "Fichier|Fermer\t⌘W", 480, 340))
 *		return 1;
 *	g.titre_fps = 1;
 *	while (ptk_gl_image(&g)) {	// événements, cadence, pause
 *		angle += 40.0 * g.dt;	// mouvement à l'horloge
 *		... dessin, ptk_gl_dessiner(&maillage) ...
 *		ptk_gl_echanger(&g);	// échange, i/s
 *	}
 *	ptk_gl_fermer(&g);
 */
#ifndef PTK_GL_H
#define PTK_GL_H

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

typedef struct PtkGl PtkGl;

struct PtkGl {
	Display *dpy;
	Window win;
	GLXContext ctx;
	int largeur, hauteur;	/* zone cliente, à jour à chaque image */
	int vivant;		/* 0 : ptk_gl_image rend 0, la boucle s'arrête */

	double t;		/* horloge, secondes */
	double dt;		/* temps écoulé depuis l'image précédente ;
				 * 0 à la reprise après une pause */
	int fps;		/* images par seconde, mesurées chaque seconde */

	const char *rendu;	/* GL_RENDERER */
	const char *nature;	/* « matériel (rendu direct) », « logiciel »… */
	int vsync;		/* la synchro verticale donne la cadence */
	int vbo;		/* tampons de sommets disponibles */
	int titre_fps;		/* titre « Nom — moteur — N i/s » */

	/* rappels facultatifs : événement X de la fenêtre (souris, clavier…),
	 * commande de la barre de menus du WM. Sans rappel de commande,
	 * « Fermer » et « Quitter » arrêtent la boucle. */
	void (*evenement)(PtkGl *g, XEvent *ev, void *user);
	void (*commande)(PtkGl *g, const char *cmd, void *user);
	void *user;

	/* interne */
	char nom[64];
	int visible, montree, images;
	double t_titre;
	Atom wm_protocols, wm_delete, a_cmd;
};

/* Fenêtre GLX double tampon avec profondeur, citoyenne du bureau (nom, icône
 * et menus dans la barre du WM), synchro verticale demandée. 0 si tout va. */
int ptk_gl_ouvrir(PtkGl *g, const char *nom, const char *icone,
    const char *menu, int largeur, int hauteur);

/* Traite les événements et attend le moment de dessiner : retour de trame
 * (synchro), au plus 1/60 s (sans synchro), ou réapparition de la fenêtre
 * (pause). Met à jour t, dt, largeur, hauteur. 0 quand il faut quitter. */
int ptk_gl_image(PtkGl *g);

/* Échange les tampons, compte les images, titre si titre_fps. */
void ptk_gl_echanger(PtkGl *g);

void ptk_gl_fermer(PtkGl *g);

double ptk_gl_horloge(void);

/* Géométrie statique : sommets entrelacés x y z nx ny nz (6 flottants),
 * triangles indexés. Copiée une fois dans un tampon de sommets si possible,
 * sinon dessinée depuis les tableaux (qui doivent alors rester valides). */
typedef struct {
	GLuint tampons[2];
	const GLfloat *sommets;
	const GLushort *indices;
	int nb_indices;
	int vbo;
} PtkGlMaillage;

void ptk_gl_maillage(PtkGl *g, PtkGlMaillage *m, const GLfloat *sommets,
    int nb_sommets, const GLushort *indices, int nb_indices);
void ptk_gl_dessiner(const PtkGlMaillage *m);

/* Perspective à la gluPerspective, champ vertical en degrés. */
void ptk_gl_perspective(double fovy, double aspect, double zn, double zf);

#endif
