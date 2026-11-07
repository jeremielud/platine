/* Théière : la théière de l'Utah telle que GLUT la dessine, en GLX pur, pour
 * éprouver ce que le i915GM du 701 donne sous NetBSD/Mesa (matériel ou
 * logiciel) et comment une fenêtre GL cohabite avec le bureau.
 *
 * Le modèle et son fonctionnement sont ceux de glut_teapot.c (Mark J. Kilgard
 * / Silicon Graphics, 1993, licence libre SGI B ; données de Newell, domaine
 * public) : teapot() transcrit tel quel — 10 patchs Bézier réfléchis par
 * symétrie, évalués comme glEvalMesh2 sous GL_AUTO_NORMAL (calcul de Mesa,
 * fait une fois, en sommets uniques indexés) sur la grille 7 de
 * glutSolidTeapot — ni GLUT ni GLU.
 *
 * Mouvement : rotation propre réglée sur l'horloge (40°/s, indépendante des
 * images et des événements), glisser à la souris pour la tourner librement
 * autour de son centre. La caméra est à distance fixe, réglée pour que la
 * sphère englobante tienne dans la fenêtre : la taille suit la fenêtre, sans
 * zoom quand la théière tourne. Le titre porte le moteur et les i/s.
 *
 * Fenêtre, cadence (synchro verticale), pause hors écran, i/s et tampon de
 * sommets : la Toolbox GL (toolbox/ptk_gl.h), commune aux programmes
 * OpenGL de Platine — voir docs/11-graphisme-gl.md.
 */
#include "ptk_gl.h"

#include <X11/keysym.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Théière", "thei", "application", PTK_VERSION, "");


#define FEN_W 480
#define FEN_H 340
#define FOV 40.0	/* champ vertical, degrés */
#define SUBDIV 7	/* grille de glutSolidTeapot : teapot(7, …, GL_FILL) */
#define VITESSE 40.0	/* rotation propre, degrés par seconde */
#define DEG_PIXEL 0.5	/* rotation à la souris, degrés par pixel */

static const float lumiere[4] = { 0.0f, 3.0f, 3.0f, 0.0f };	/* teapots.c */

/* --- Modèle de glut_teapot.c, transcrit tel quel --- */

/* Indices des 16 points de contrôle de chacun des 10 patchs. */
static const int patchdata[10][16] = {
	{ /* rim (col) */
	  102, 103, 104, 105,
	    4,   5,   6,   7,
	    8,   9,  10,  11,
	   12,  13,  14,  15,
	},
	{ /* corps */
	   12,  13,  14,  15,
	   16,  17,  18,  19,
	   20,  21,  22,  23,
	   24,  25,  26,  27,
	},
	{ /* corps */
	   24,  25,  26,  27,
	   29,  30,  31,  32,
	   33,  34,  35,  36,
	   37,  38,  39,  40,
	},
	{ /* couvercle */
	   96,  96,  96,  96,
	   97,  98,  99, 100,
	  101, 101, 101, 101,
	    0,   1,   2,   3,
	},
	{ /* couvercle */
	    0,   1,   2,   3,
	  106, 107, 108, 109,
	  110, 111, 112, 113,
	  114, 115, 116, 117,
	},
	{ /* fond */
	  118, 118, 118, 118,
	  124, 122, 119, 121,
	  123, 126, 125, 120,
	   40,  39,  38,  37,
	},
	{ /* anse 1 */
	   41,  42,  43,  44,
	   45,  46,  47,  48,
	   49,  50,  51,  52,
	   53,  54,  55,  56,
	},
	{ /* anse 2 */
	   53,  54,  55,  56,
	   57,  58,  59,  60,
	   61,  62,  63,  64,
	   28,  65,  66,  67,
	},
	{ /* bec 1 */
	   68,  69,  70,  71,
	   72,  73,  74,  75,
	   76,  77,  78,  79,
	   80,  81,  82,  83,
	},
	{ /* bec 2 */
	   80,  81,  82,  83,
	   84,  85,  86,  87,
	   88,  89,  90,  91,
	   92,  93,  94,  95,
	},
};

/* 127 points de contrôle, dans le repère de glut_teapot.c : x rayon, y autour
 * de l'anneau, z hauteur (0 à 3,15). Les réflexions de la génération rendent
 * les coordonnées symétriques ; le point 28 est le seul sommet du dos déjà
 * reflété dans la table. */
static const float cpdata[][3] = {
	{ 0.2, 0, 2.7 }, { 0.2, -0.112, 2.7 }, { 0.112, -0.2, 2.7 }, { 0, -0.2, 2.7 },
	{ 1.3375, 0, 2.53125 }, { 1.3375, -0.749, 2.53125 }, { 0.749, -1.3375, 2.53125 }, { 0, -1.3375, 2.53125 },
	{ 1.4375, 0, 2.53125 }, { 1.4375, -0.805, 2.53125 }, { 0.805, -1.4375, 2.53125 }, { 0, -1.4375, 2.53125 },
	{ 1.5, 0, 2.4 }, { 1.5, -0.84, 2.4 }, { 0.84, -1.5, 2.4 }, { 0, -1.5, 2.4 },
	{ 1.75, 0, 1.875 }, { 1.75, -0.98, 1.875 }, { 0.98, -1.75, 1.875 }, { 0, -1.75, 1.875 },
	{ 2, 0, 1.35 }, { 2, -1.12, 1.35 }, { 1.12, -2, 1.35 }, { 0, -2, 1.35 },
	{ 2, 0, 0.9 }, { 2, -1.12, 0.9 }, { 1.12, -2, 0.9 }, { 0, -2, 0.9 },
	{ -2, 0, 0.9 },
	{ 2, 0, 0.45 }, { 2, -1.12, 0.45 }, { 1.12, -2, 0.45 }, { 0, -2, 0.45 },
	{ 1.5, 0, 0.225 }, { 1.5, -0.84, 0.225 }, { 0.84, -1.5, 0.225 }, { 0, -1.5, 0.225 },
	{ 1.5, 0, 0.15 }, { 1.5, -0.84, 0.15 }, { 0.84, -1.5, 0.15 }, { 0, -1.5, 0.15 },
	{ -1.6, 0, 2.025 }, { -1.6, -0.3, 2.025 }, { -1.5, -0.3, 2.25 }, { -1.5, 0, 2.25 },
	{ -2.3, 0, 2.025 }, { -2.3, -0.3, 2.025 }, { -2.5, -0.3, 2.25 }, { -2.5, 0, 2.25 },
	{ -2.7, 0, 2.025 }, { -2.7, -0.3, 2.025 }, { -3, -0.3, 2.25 }, { -3, 0, 2.25 },
	{ -2.7, 0, 1.8 }, { -2.7, -0.3, 1.8 }, { -3, -0.3, 1.8 }, { -3, 0, 1.8 },
	{ -2.7, 0, 1.575 }, { -2.7, -0.3, 1.575 }, { -3, -0.3, 1.35 }, { -3, 0, 1.35 },
	{ -2.5, 0, 1.125 }, { -2.5, -0.3, 1.125 }, { -2.65, -0.3, 0.9375 }, { -2.65, 0, 0.9375 },
	{ -2, -0.3, 0.9 }, { -1.9, -0.3, 0.6 }, { -1.9, 0, 0.6 },
	{ 1.7, 0, 1.425 }, { 1.7, -0.66, 1.425 }, { 1.7, -0.66, 0.6 }, { 1.7, 0, 0.6 },
	{ 2.6, 0, 1.425 }, { 2.6, -0.66, 1.425 }, { 3.1, -0.66, 0.825 }, { 3.1, 0, 0.825 },
	{ 2.3, 0, 2.1 }, { 2.3, -0.25, 2.1 }, { 2.4, -0.25, 2.025 }, { 2.4, 0, 2.025 },
	{ 2.7, 0, 2.4 }, { 2.7, -0.25, 2.4 }, { 3.3, -0.25, 2.4 }, { 3.3, 0, 2.4 },
	{ 2.8, 0, 2.475 }, { 2.8, -0.25, 2.475 }, { 3.525, -0.25, 2.49375 }, { 3.525, 0, 2.49375 },
	{ 2.9, 0, 2.475 }, { 2.9, -0.15, 2.475 }, { 3.45, -0.15, 2.5125 }, { 3.45, 0, 2.5125 },
	{ 2.8, 0, 2.4 }, { 2.8, -0.15, 2.4 }, { 3.2, -0.15, 2.4 }, { 3.2, 0, 2.4 },
	{ 0, 0, 3.15 }, { 0.8, 0, 3.15 }, { 0.8, -0.45, 3.15 }, { 0.45, -0.8, 3.15 },
	{ 0, -0.8, 3.15 }, { 0, 0, 2.85 },
	{ 1.4, 0, 2.4 }, { 1.4, -0.784, 2.4 }, { 0.784, -1.4, 2.4 }, { 0, -1.4, 2.4 },
	{ 0.4, 0, 2.55 }, { 0.4, -0.224, 2.55 }, { 0.224, -0.4, 2.55 }, { 0, -0.4, 2.55 },
	{ 1.3, 0, 2.55 }, { 1.3, -0.728, 2.55 }, { 0.728, -1.3, 2.55 }, { 0, -1.3, 2.55 },
	{ 1.3, 0, 2.4 }, { 1.3, -0.728, 2.4 }, { 0.728, -1.3, 2.4 }, { 0, -1.3, 2.4 },
	{ 0, 0, 0 }, { 1.425, -0.798, 0 }, { 1.5, 0, 0.075 }, { 1.425, 0, 0 },
	{ 0.798, -1.425, 0 }, { 0, -1.5, 0.075 }, { 0, -1.425, 0 }, { 1.5, -0.84, 0.075 },
	{ 0.84, -1.5, 0.075 }
};

/* --- teapot() de glut_teapot.c, transcrit tel quel --- */

/* Les évaluateurs, évalués une fois. Mesa garde glEvalMesh2 tel quel dans une
 * liste d'affichage et réévalue les Bézier sur le processeur à chaque appel :
 * sur le Celeron du 701, 37 i/s pour 80 % de CPU. eval_mesh2 fait une fois
 * le calcul de Mesa pour glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, p)
 * suivi de glMapGrid2f(grid, 0, 1, grid, 0, 1) et glEvalMesh2(GL_FILL, 0,
 * grid, 0, grid) sous GL_AUTO_NORMAL — u suit les colonnes (pas de 3), v les
 * rangées (pas de 12), normale = dS/du × dS/dv — et range les sommets dans un
 * tampon que chaque image dessine d'un seul appel. */

static void
bernstein(float t, float b[4], float db[4])
{
	float u = 1.0f - t;

	b[0] = u * u * u;
	b[1] = 3.0f * u * u * t;
	b[2] = 3.0f * u * t * t;
	b[3] = t * t * t;
	db[0] = -3.0f * u * u;
	db[1] = 3.0f * u * u - 6.0f * u * t;
	db[2] = 6.0f * u * t - 3.0f * t * t;
	db[3] = 3.0f * t * t;
}

/* S(u, v) et les dérivées dS/du, dS/dv */
static void
eval_bezier(const float p[4][4][3], float u, float v, float s[3], float du[3],
    float dv[3])
{
	float bu[4], dbu[4], bv[4], dbv[4];
	int c, j, k;

	bernstein(u, bu, dbu);
	bernstein(v, bv, dbv);
	for (c = 0; c < 3; c++) {
		s[c] = du[c] = dv[c] = 0.0f;
		for (j = 0; j < 4; j++)
			for (k = 0; k < 4; k++) {
				s[c] += bu[k] * bv[j] * p[j][k][c];
				du[c] += dbu[k] * bv[j] * p[j][k][c];
				dv[c] += bu[k] * dbv[j] * p[j][k][c];
			}
	}
}

static float
croix_unitaire(const float a[3], const float b[3], float n[3])
{
	float lg;

	n[0] = a[1] * b[2] - a[2] * b[1];
	n[1] = a[2] * b[0] - a[0] * b[2];
	n[2] = a[0] * b[1] - a[1] * b[0];
	lg = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (lg > 1e-6f) {
		n[0] /= lg;
		n[1] /= lg;
		n[2] /= lg;
	}
	return lg;
}

/* S(u, v) et sa normale automatique dS/du × dS/dv. Aux pôles (bouton du
 * couvercle, centre du fond : une rangée de points confondus) dS/du est nul ;
 * la normale limite y est alors dS/dv(0) × dS/dv(1), les deux tangentes qui
 * partent du pôle, orientée comme la normale voisine. */
static void
eval_coord2(const float p[4][4][3], float u, float v, float s[3], float n[3])
{
	float du[3], dv[3], x[3], dv0[3], dv1[3], m[3];
	float w = v < 0.5f ? v + 1e-2f : v - 1e-2f;

	eval_bezier(p, u, v, s, du, dv);
	if (croix_unitaire(du, dv, n) > 1e-6f)
		return;
	eval_bezier(p, 0.0f, v, x, du, dv0);
	eval_bezier(p, 1.0f, v, x, du, dv1);
	croix_unitaire(dv0, dv1, n);
	eval_bezier(p, 0.5f, w, x, du, dv);
	croix_unitaire(du, dv, m);
	if (n[0] * m[0] + n[1] * m[1] + n[2] * m[2] < 0.0f) {
		n[0] = -n[0];
		n[1] = -n[1];
		n[2] = -n[2];
	}
}

/* Sommets uniques et indices : (grid + 1)² sommets par patch, partagés par
 * les deux triangles de chaque case. L'i915GM n'a pas de traitement des
 * sommets matériel : Mesa transforme et éclaire chaque sommet envoyé sur le
 * processeur. Les bandes de glEvalMesh2 en envoient 2 × (grid + 1) par
 * rangée, soit 3 584 par image ; indexés, 2 048 seulement, chacun éclairé
 * une fois. */
#define NB_PATCHS 32	/* 10 patchs : 6 × 4 réflexions + 4 × 2 */
#define SOMMETS_PATCH ((SUBDIV + 1) * (SUBDIV + 1))
#define NB_SOMMETS (NB_PATCHS * SOMMETS_PATCH)
#define NB_INDICES (NB_PATCHS * SUBDIV * SUBDIV * 6)

static GLfloat sommets[NB_SOMMETS][6];	/* x y z, nx ny nz */
static GLushort indices[NB_INDICES];
static int nb_sommets, nb_indices;

/* Le transformé de teapot() — glRotatef(270, 1, 0, 0), glScalef(0,5),
 * glTranslatef(0, 0, −1,5) — appliqué une fois aux sommets : (x, y, z) devient
 * (0,5 x, 0,5 (z − 1,5), −0,5 y) et les normales tournent sans changer de
 * longueur. Plus d'échelle dans la matrice, donc plus de GL_NORMALIZE : les
 * normales restent unitaires sous des rotations et translations seules. */
static void
eval_mesh2(const float p[4][4][3], int grid)
{
	int i, j, base = nb_sommets;

	for (j = 0; j <= grid; j++)
		for (i = 0; i <= grid; i++) {
			float s[3], n[3];
			GLfloat *v = sommets[nb_sommets++];

			eval_coord2(p, (float)i / grid, (float)j / grid, s, n);
			v[0] = 0.5f * s[0];
			v[1] = 0.5f * (s[2] - 1.5f);
			v[2] = -0.5f * s[1];
			v[3] = n[0];
			v[4] = n[2];
			v[5] = -n[1];
		}
	/* les deux triangles de chaque case, dans l'ordre de la bande de
	 * glEvalMesh2 : (i, j), (i, j+1), (i+1, j) puis (i+1, j), (i, j+1),
	 * (i+1, j+1) — même sens de parcours, donc même face avant */
	for (j = 0; j < grid; j++)
		for (i = 0; i < grid; i++) {
			GLushort a = base + j * (grid + 1) + i;
			GLushort b = a + (grid + 1);

			indices[nb_indices++] = a;
			indices[nb_indices++] = b;
			indices[nb_indices++] = a + 1;
			indices[nb_indices++] = a + 1;
			indices[nb_indices++] = b;
			indices[nb_indices++] = b + 1;
		}
}

/* glutSolidTeapot(1.0) = teapot(7, 1.0, GL_FILL) : pour chacun des 10 patchs,
 * p tel quel, q (y nié) et, pour les six premiers, r (x nié) et s (x et y
 * niés), chacun évalué comme glEvalMesh2 sous GL_AUTO_NORMAL — les normales
 * viennent de dS/du × dS/dv, cohérentes grâce à l'inversion de l'ordre des
 * colonnes dans les réflexions q et r. */
static void
teapot(GLint grid)
{
	float p[4][4][3], q[4][4][3], r[4][4][3], s[4][4][3];
	long i, j, k, l;

	for (i = 0; i < 10; i++) {
		for (j = 0; j < 4; j++) {
			for (k = 0; k < 4; k++) {
				for (l = 0; l < 3; l++) {
					p[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
					q[j][k][l] = cpdata[patchdata[i][j * 4 + (3 - k)]][l];
					if (l == 1)
						q[j][k][l] *= -1.0f;
					if (i < 6) {
						r[j][k][l] = cpdata[patchdata[i][j * 4 + (3 - k)]][l];
						if (l == 0)
							r[j][k][l] *= -1.0f;
						s[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
						if (l == 0)
							s[j][k][l] *= -1.0f;
						if (l == 1)
							s[j][k][l] *= -1.0f;
					}
				}
			}
		}
		eval_mesh2(p, grid);
		eval_mesh2(q, grid);
		if (i < 6) {
			eval_mesh2(r, grid);
			eval_mesh2(s, grid);
		}
	}
}

/* --- Cadrage : sphère englobante, la caméra ne bouge pas --- */

/* Après le transformé de teapot() : x' = 0,5 x, y' = 0,5 (z − 1,5) (hauteur),
 * z' = −0,5 y. La surface est dans l'enveloppe convexe de ses points de
 * contrôle : la sphère centrée sur la boîte englobante qui les contient tous
 * contient la théière quelle que soit son orientation. On tourne autour de ce
 * centre et la caméra reste à distance fixe : pas de zoom pendant la rotation. */
static float centre[3], rayon;

/* appelle f sur chaque point de contrôle effectivement dessiné, réflexions
 * comprises, dans le repère de teapot() après son transformé */
static void
pour_chaque_point(void (*f)(const float v[3]))
{
	int i, j, c;

	for (i = 0; i < 10; i++)
		for (j = 0; j < 16; j++) {
			const float *cp = cpdata[patchdata[i][j]];

			for (c = 0; c < (i < 6 ? 4 : 2); c++) {
				float sy = (c & 1) ? -1.0f : 1.0f;
				float sx = (c & 2) ? -1.0f : 1.0f;
				float v[3] = { 0.5f * sx * cp[0],
				    0.5f * (cp[2] - 1.5f),
				    -0.5f * sy * cp[1] };

				f(v);
			}
		}
}

static float bmin[3] = { 1e9f, 1e9f, 1e9f }, bmax[3] = { -1e9f, -1e9f, -1e9f };

static void
etendre_boite(const float v[3])
{
	int a;

	for (a = 0; a < 3; a++) {
		if (v[a] < bmin[a])
			bmin[a] = v[a];
		if (v[a] > bmax[a])
			bmax[a] = v[a];
	}
}

static void
etendre_rayon(const float v[3])
{
	float dx = v[0] - centre[0], dy = v[1] - centre[1], dz = v[2] - centre[2];
	float d = sqrtf(dx * dx + dy * dy + dz * dz);

	if (d > rayon)
		rayon = d;
}

static void
mesurer_theiere(void)
{
	int a;

	pour_chaque_point(etendre_boite);
	for (a = 0; a < 3; a++)
		centre[a] = 0.5f * (bmin[a] + bmax[a]);
	rayon = 0.0f;
	pour_chaque_point(etendre_rayon);
}

/* distance de caméra : la sphère englobante tient dans la fenêtre */
static double
distance_camera(double aspect)
{
	double t = tan(FOV / 2.0 * M_PI / 180.0);
	double tmin = aspect < 1.0 ? t * aspect : t;	/* demi-champ le plus étroit */

	/* une sphère de rayon R vue sous le demi-angle a : d = R / sin(a) */
	return rayon / sin(atan(tmin)) / 0.98;
}

/* --- Fenêtre et rendu, par la Toolbox GL --- */

struct etat {
	double angle_y, angle_x;
	int drag, lx, ly;
};

static void
evenement(PtkGl *g, XEvent *ev, void *user)
{
	struct etat *e = user;

	if (ev->type == ButtonPress && ev->xbutton.button == Button1) {
		e->drag = 1;
		e->lx = ev->xbutton.x;
		e->ly = ev->xbutton.y;
	} else if (ev->type == ButtonRelease && ev->xbutton.button == Button1)
		e->drag = 0;
	else if (ev->type == MotionNotify && e->drag) {
		/* la théière suit la souris : un demi-degré par pixel, autour
		 * de son axe vertical et de l'horizontale de l'écran */
		e->angle_y = fmod(e->angle_y + DEG_PIXEL *
		    (ev->xmotion.x - e->lx), 360.0);
		e->angle_x = fmod(e->angle_x + DEG_PIXEL *
		    (ev->xmotion.y - e->ly), 360.0);
		e->lx = ev->xmotion.x;
		e->ly = ev->xmotion.y;
	} else if (ev->type == KeyPress &&
	    XLookupKeysym(&ev->xkey, 0) == XK_Escape)
		g->vivant = 0;
}

static void
commande(PtkGl *g, const char *cmd, void *user)
{
	(void)user;
	if (strcmp(cmd, "Fermer") == 0 || strcmp(cmd, "Quitter") == 0)
		g->vivant = 0;
	else if (strcmp(cmd, "À propos de la théière") == 0)
		printf("théière : théière de l'Utah, 10 patchs Bézier réfléchis "
		    "par symétrie, le modèle de glut_teapot.c (Newell, domaine "
		    "public)\n");
}

int
main(void)
{
	PtkGl g;
	PtkGlMaillage m;
	struct etat e = { 35.0, 20.0, 0, 0, 0 };

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (ptk_gl_ouvrir(&g, "Théière", "app",
	    "Fichier|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Aide|À propos de la théière", FEN_W, FEN_H) != 0)
		return 1;
	g.titre_fps = 1;
	g.evenement = evenement;
	g.commande = commande;
	g.user = &e;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glFrontFace(GL_CW);	/* les patchs de glut_teapot.c tournent en sens horaire */
	{
		/* l'éclairage des teapots.c du Red Book : lumière blanche
		 * directionnelle, ambiance du modèle 0,2, matériau brillant */
		float noir[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float blanc[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float ambiance[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
		float amb[4] = { 0.12f, 0.13f, 0.16f, 1.0f };
		float dif[4] = { 0.58f, 0.64f, 0.78f, 1.0f };
		float spe[4] = { 0.7f, 0.7f, 0.7f, 1.0f };

		glLightfv(GL_LIGHT0, GL_AMBIENT, noir);
		glLightfv(GL_LIGHT0, GL_DIFFUSE, blanc);
		glLightfv(GL_LIGHT0, GL_SPECULAR, blanc);
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambiance);
		glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);
		glMaterialfv(GL_FRONT, GL_AMBIENT, amb);
		glMaterialfv(GL_FRONT, GL_DIFFUSE, dif);
		glMaterialfv(GL_FRONT, GL_SPECULAR, spe);
		glMaterialf(GL_FRONT, GL_SHININESS, 60.0f);
	}
	glClearColor(0.725f, 0.725f, 0.725f, 1.0f);
	teapot(SUBDIV);
	ptk_gl_maillage(&g, &m, &sommets[0][0], nb_sommets, indices,
	    nb_indices);
	mesurer_theiere();

	while (ptk_gl_image(&g)) {
		double aspect = (double)g.largeur / g.hauteur;

		/* rotation propre réglée sur l'horloge, suspendue pendant le
		 * glisser */
		if (!e.drag)
			e.angle_y = fmod(e.angle_y + VITESSE * g.dt, 360.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		ptk_gl_perspective(FOV, aspect, 0.5, 60.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glLightfv(GL_LIGHT0, GL_POSITION, lumiere);	/* repère caméra */
		glTranslatef(0.0f, 0.0f, (float)-distance_camera(aspect));
		glRotatef((float)e.angle_x, 1.0f, 0.0f, 0.0f);
		glRotatef((float)e.angle_y, 0.0f, 1.0f, 0.0f);
		glTranslatef(-centre[0], -centre[1], -centre[2]);
		ptk_gl_dessiner(&m);
		ptk_gl_echanger(&g);
	}
	ptk_gl_fermer(&g);
	return 0;
}
