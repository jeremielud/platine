/*
 * platine-strip — la Control Strip de Mac OS 9 : objet système flottant, indépendant du
 * Finder, avec ses modules. Mesures relevées sur la VM (docs/07-control-strip.md) :
 * hauteur 24 px, case de repli, flèches, modules de 30 px (icône à +4, flèche à +22),
 * séparateurs noirs, onglet biseauté ; module enfoncé sombre ; panneau de volume.
 *
 * Clic sur la case : replie sur l'onglet ; clic sur l'onglet : déplie ; glisser l'onglet :
 * change la largeur ; Option-glisser l'onglet : monte ou descend la barre.
 * PLATINE_APERCU=1 : les réglages ne touchent pas à la machine (aperçu sur le PC).
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glob.h>
#include <poll.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../../toolbox/ptk_sys.h"
#include "../../../toolbox/ptk.h"
#include "motifs.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Control Strip", "sdev", "ext-control-strip", PTK_VERSION, "");


#define H 24
#define X_MODULES 29
#define L_MODULE 31	/* 30 px + séparateur */
#define L_FLECHE 14
#define L_ONGLET 18
#define VOL_L 22
#define VOL_H 66
#define COCHE "\xe2\x88\x9a "

/* Un module : intégré (clé connue du code) ou externe — un fichier du dossier
 * « System/Control Strip Modules », comme les Control Strip Modules de
 * Mac OS 9, posé par exemple par l'Installateur (voir modules_externes) */
#define ELEMENTS_MAX 12
typedef struct {
	const char *cle, *nom, *icone;
	Pixmap pix, mask;
	/* module externe */
	char ext_nom[64], ext_icone[512], ext_etat[128];
	char ext_lab[ELEMENTS_MAX][96], ext_cmd[ELEMENTS_MAX][512];
	int ext_n;
} Module;

#define MODS_MAX 32
static Module mods[MODS_MAX] = {
	{ .cle = "airport", .nom = "AirPort", .icone = "strip-airport" },
	{ .cle = "batterie", .nom = "Batterie", .icone = "strip-batterie" },
	{ .cle = "diction", .nom = "Diction", .icone = "strip-diction" },
	{ .cle = "energie", .nom = "Économiseur d'énergie", .icone = "strip-veille" },
	{ .cle = "lumiere", .nom = "Luminosité", .icone = "strip-luminosite" },
	{ .cle = "ecran", .nom = "Résolution", .icone = "strip-ecran" },
	{ .cle = "processeur", .nom = "Processeur", .icone = NULL },	/* icône dessinée : activité en direct */
	{ .cle = "son", .nom = "Volume", .icone = "strip-son" },
};
#define N_INTEGRES 8
static int nmods = N_INTEGRES;
#define NMODS nmods

static Display *dpy;
static Ptk ptk;
static Window root, win, vol_win, bulle;
static Pixmap back, vol_back;
static GC bit_gc;
static Atom a_context, a_command, a_utf8;
static int sw, sh, wy;
static int replie, nvis = N_INTEGRES, premier, enfonce = -1, attente_menu, apercu;
static int volume = 60, vol_x, vol_drag;
static char dernier_menu[32], cfg[512];
/* glissement de l'onglet : position de départ */
static int tire, tire_x0, tire_y0, tire_y, tire_n, tire_bouge;
static int cote;	/* 0 à gauche ; 1 à droite, en miroir (onglet vers l'intérieur) */
static int survol = -1, attente_bulle, etat_age;

static KeyCode touche_code;
static unsigned touche_mods = Mod1Mask | ControlMask;

static volatile sig_atomic_t reload_demande;

static void
sig_reload(int n)
{
	(void)n;
	reload_demande = 1;
}

/*
 * Modules externes : « System/Control Strip Modules/<fichier> » :
 *
 *	PLATINE-MODULE-1
 *	nom=Instantané
 *	icone=Instantané.xpm            fichier voisin, sinon icône des assets
 *	etat=⌘⇧3 pour capturer          texte de la bulle
 *	element=Capturer l'écran…|@System/Extensions/Instantané|maintenant
 *	element=-
 *	element=Ouvrir le dossier Captures|ouvrir:Captures
 *
 * Commande d'un élément : « @chemin|repli » (programme du volume, suivi
 * d'arguments après un second « | »), ou « ouvrir:dossier » (le Finder
 * l'ouvre, dans le volume).
 */
static int
mod_cmp(const void *a, const void *b)
{
	return strcoll(((const Module *)a)->ext_nom, ((const Module *)b)->ext_nom);
}

static void
modules_externes(void)
{
	char dossier[PATH_MAX];
	DIR *d;
	struct dirent *e;
	int i;

	for (i = N_INTEGRES; i < nmods; i++) {
		if (mods[i].pix)
			XFreePixmap(dpy, mods[i].pix);
		if (mods[i].mask)
			XFreePixmap(dpy, mods[i].mask);
	}
	nmods = N_INTEGRES;
	if (!ptk_dans_volume("System/Control Strip Modules", dossier, sizeof(dossier)) ||
	    (d = opendir(dossier)) == NULL)
		return;
	while (nmods < MODS_MAX && (e = readdir(d)) != NULL) {
		char ch[PATH_MAX], l[700];
		Module *m = &mods[nmods];
		FILE *f;

		if (e->d_name[0] == '.' || (size_t)snprintf(ch, sizeof(ch), "%s/%s", dossier,
		    e->d_name) >= sizeof(ch) || (f = fopen(ch, "r")) == NULL)
			continue;
		memset(m, 0, sizeof(*m));
		if (fgets(l, sizeof(l), f) == NULL || strncmp(l, "PLATINE-MODULE-1", 16) != 0) {
			fclose(f);
			continue;
		}
		if ((size_t)snprintf(m->ext_nom, sizeof(m->ext_nom), "%s", e->d_name) >=
		    sizeof(m->ext_nom))
			m->ext_nom[sizeof(m->ext_nom) - 1] = '\0';
		while (fgets(l, sizeof(l), f) != NULL) {
			char *v = strchr(l, '=');

			if (v == NULL)
				continue;
			*v++ = '\0';
			v[strcspn(v, "\n")] = '\0';
			if (!strcmp(l, "nom"))
				snprintf(m->ext_nom, sizeof(m->ext_nom), "%s", v);
			else if (!strcmp(l, "etat"))
				snprintf(m->ext_etat, sizeof(m->ext_etat), "%s", v);
			else if (!strcmp(l, "icone")) {
				if (strchr(v, '/') == NULL && (size_t)snprintf(m->ext_icone,
				    sizeof(m->ext_icone), "%s/%s", dossier, v) < sizeof(m->ext_icone) &&
				    access(m->ext_icone, R_OK) == 0)
					;
				else
					snprintf(m->ext_icone, sizeof(m->ext_icone), "%s%s", v,
					    strstr(v, ".xpm") ? "" : ".xpm");
			} else if (!strcmp(l, "element") && m->ext_n < ELEMENTS_MAX) {
				char *c = strchr(v, '|');

				if (c != NULL)
					*c++ = '\0';
				snprintf(m->ext_lab[m->ext_n], sizeof(m->ext_lab[0]), "%s", v);
				snprintf(m->ext_cmd[m->ext_n], sizeof(m->ext_cmd[0]), "%s", c ? c : "");
				m->ext_n++;
			}
		}
		fclose(f);
		m->cle = "externe";
		m->nom = m->ext_nom;
		m->icone = m->ext_icone[0] ? m->ext_icone : NULL;
		nmods++;
	}
	closedir(d);
	/* après les modules intégrés, par ordre alphabétique, comme le dossier */
	qsort(mods + N_INTEGRES, (size_t)(nmods - N_INTEGRES), sizeof(Module), mod_cmp);
	for (i = N_INTEGRES; i < nmods; i++) {
		mods[i].nom = mods[i].ext_nom;
		mods[i].icone = mods[i].ext_icone[0] ? mods[i].ext_icone : NULL;
		if (mods[i].icone == NULL ||
		    ptk_load_xpm(&ptk, mods[i].icone, &mods[i].pix, &mods[i].mask) != 0)
			ptk_load_xpm(&ptk, "menu/application.xpm", &mods[i].pix, &mods[i].mask);
	}
}

static unsigned long
gris(int v)
{
	char n[8];

	snprintf(n, sizeof(n), "#%02x%02x%02x", v, v, v);
	return ptk_color(&ptk, n).pixel;
}

static void
point(Drawable d, int x, int y, unsigned long c)
{
	XSetForeground(dpy, ptk.gc, c);
	XDrawPoint(dpy, d, ptk.gc, x, y);
}

static void
plein(Drawable d, int x, int y, int w, int h, int v)
{
	XSetForeground(dpy, ptk.gc, gris(v));
	XFillRectangle(dpy, d, ptk.gc, x, y, w, h);
}

static unsigned long
couleur_motif(char c)
{
	switch (c) {
	case '0': return gris(0);
	case '2': return gris(0x22);
	case '4': return gris(0x44);
	case '5': return gris(0x55);
	case '8': return gris(0x80);
	case 'a': return gris(0xa0);
	case 'c': return gris(0xc0);
	case 'd': return gris(0xdd);
	case 'f': return gris(0xff);
	case 'L': return ptk_color(&ptk, "#ccccff").pixel;
	}
	return 0;
}

static void
motif(Drawable d, const char *const *m, int rangs, int x, int y)
{
	for (int r = 0; r < rangs; r++)
		for (int c = 0; m[r][c]; c++)
			if (m[r][c] != '.')
				point(d, x + c, y + r, couleur_motif(m[r][c]));
}

/* le même motif retourné, pour la barre placée à droite */
static void
motif_miroir(Drawable d, const char *const *m, int rangs, int x, int y)
{
	for (int r = 0; r < rangs; r++) {
		int l = (int)strlen(m[r]);

		for (int c = 0; c < l; c++)
			if (m[r][c] != '.')
				point(d, x + l - 1 - c, y + r, couleur_motif(m[r][c]));
	}
}

/* cellule en relief, ou enfoncée (sombre) : x0 = première colonne, l = largeur */
static void
cellule(Drawable d, int x0, int l, int creux)
{
	if (creux) {
		plein(d, x0, 1, l, 22, 0x80);
		plein(d, x0, 1, l, 2, 0x40);
		plein(d, x0, 1, 2, 21, 0x40);
		plein(d, x0 + 1, 21, l - 1, 2, 0xc0);
		point(d, x0, 22, gris(0x80));
		return;
	}
	plein(d, x0, 1, l, 22, 0xc0);
	plein(d, x0 + 1, 2, l - 3, 1, 0xff);
	plein(d, x0 + 1, 2, 1, 19, 0xff);
	plein(d, x0 + l - 2, 3, 1, 19, 0xa0);
	plein(d, x0 + 2, 21, l - 3, 1, 0xa0);
	plein(d, x0 + l - 1, 2, 1, 21, 0x80);
	plein(d, x0 + 1, 22, l - 1, 1, 0x80);
}

/* cellule de flèche : creuse et grisée quand il n'y a rien à faire défiler */
static void
cellule_fleche(Drawable d, int x0, int actif)
{
	if (actif) {
		cellule(d, x0, 13, 0);
		return;
	}
	plein(d, x0, 1, 13, 22, 0xc0);
	plein(d, x0, 1, 12, 1, 0x80);
	plein(d, x0, 1, 1, 21, 0x80);
	plein(d, x0 + 12, 2, 1, 21, 0xff);
	plein(d, x0, 22, 1, 1, 0xc0);
	plein(d, x0 + 1, 22, 12, 1, 0xff);
}

/* première colonne du k-ième module affiché */
static int
x_module(int k)
{
	return (cote ? L_ONGLET + L_FLECHE : X_MODULES) + k * L_MODULE;
}

static int
largeur_totale(void)
{
	if (replie)
		return L_ONGLET;
	return X_MODULES + nvis * L_MODULE + L_FLECHE + L_ONGLET;
}

/* la silhouette : tout le rectangle, sauf les coins biseautés de l'onglet */
static void
forme(void)
{
	int w = largeur_totale(), x0 = cote ? 0 : w - L_ONGLET;
	Pixmap m = XCreatePixmap(dpy, win, w, H, 1);

	XSetForeground(dpy, bit_gc, 1);
	XFillRectangle(dpy, m, bit_gc, 0, 0, w, H);
	XSetForeground(dpy, bit_gc, 0);
	for (int r = 0; r < H; r++)
		for (int c = 0; map_onglet[r][c]; c++)
			if (map_onglet[r][c] == '.')
				XDrawPoint(dpy, m, bit_gc, x0 + (cote ? L_ONGLET - 1 - c : c), r);
	XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, m, ShapeSet);
	XFreePixmap(dpy, m);
}

/* module Processeur : les 14 dernières mesures (une toutes les 2 s) en mini-courbe 16×16 */
#define CPU_N 14
static int cpu_hist[CPU_N], cpu_n;
static long cpu_prochain;

static void
cpu_icone(Drawable d, int x, int y)
{
	XSetForeground(dpy, ptk.gc, gris(0));
	XDrawRectangle(dpy, d, ptk.gc, x, y, 15, 15);
	XSetForeground(dpy, ptk.gc, gris(0xff));
	XFillRectangle(dpy, d, ptk.gc, x + 1, y + 1, 14, 14);
	XSetForeground(dpy, ptk.gc, ptk_color(&ptk, "#333399").pixel);
	for (int k = 0; k < cpu_n; k++) {
		int v = (cpu_hist[k] * 14 + 99) / 100;

		if (v > 0)
			XFillRectangle(dpy, d, ptk.gc, x + 1 + (CPU_N - cpu_n + k), y + 15 - v, 1, v);
	}
}

static void dessiner(void);

static void
cpu_mesurer(void)
{
	struct timespec t;
	long ms;
	int c;

	clock_gettime(CLOCK_MONOTONIC, &t);
	ms = t.tv_sec * 1000 + t.tv_nsec / 1000000;
	if (ms < cpu_prochain)
		return;
	cpu_prochain = ms + 2000;
	if ((c = ptk_cpu_charge()) < 0)
		return;
	if (cpu_n == CPU_N)
		memmove(cpu_hist, cpu_hist + 1, sizeof(cpu_hist) - sizeof(cpu_hist[0]));
	else
		cpu_n++;
	cpu_hist[cpu_n - 1] = c;
	for (int i = premier; i < premier + nvis && i < NMODS; i++)
		if (!strcmp(mods[i].cle, "processeur") && !replie) {
			dessiner();
			break;
		}
}

static void
dessiner(void)
{
	int w = largeur_totale(), x;

	if (back)
		XFreePixmap(dpy, back);
	back = XCreatePixmap(dpy, win, w, H, DefaultDepth(dpy, ptk.screen));
	plein(back, 0, 0, w, H, 0);
	if (!replie) {
		int fg = cote ? L_ONGLET : 15;

		if (cote)
			motif_miroir(back, map_repli, H, w - 15, 0);
		else
			motif(back, map_repli, H, 0, 0);
		cellule_fleche(back, fg, premier > 0);
		motif(back, map_gauche, 10, fg + 1, 7);
		x = x_module(0);
		for (int i = premier; i < premier + nvis && i < NMODS; i++) {
			cellule(back, x, 30, enfonce == i);
			if (!strcmp(mods[i].cle, "processeur"))
				cpu_icone(back, x + 4, 4);
			else if (mods[i].pix)
				ptk_draw_pixmap(&ptk, back, mods[i].pix, mods[i].mask, x + 4, 4, 16, 16);
			for (int r = 0; r < 8; r++) {
				int n = r < 4 ? r + 1 : 8 - r;
				XSetForeground(dpy, ptk.gc, gris(0));
				XFillRectangle(dpy, back, ptk.gc, x + 22, 8 + r, n, 1);
			}
			x += L_MODULE;
		}
		cellule_fleche(back, x, premier + nvis < NMODS);
		motif(back, map_droite, 10, x + 2, 7);
	}
	if (cote)
		motif_miroir(back, map_onglet, H, 0, 0);
	else
		motif(back, map_onglet, H, w - L_ONGLET, 0);
	XSetWindowBackgroundPixmap(dpy, win, back);
	XClearWindow(dpy, win);
}

static int largeur_fenetre;

static void
reagencer(void)
{
	int w = largeur_totale();

	if (premier + nvis > NMODS)
		premier = NMODS - nvis;
	/* X répète le fond d'une fenêtre qui grandit : le nouveau fond doit être posé avant */
	if (w > largeur_fenetre)
		dessiner();
	XMoveResizeWindow(dpy, win, cote ? sw - w : 0, wy, w, H);
	forme();
	if (w <= largeur_fenetre)
		dessiner();
	largeur_fenetre = w;
}

static void
enregistrer(void)
{
	FILE *f = fopen(cfg, "w");

	if (f) {
		const char *nom = XKeysymToString(XKeycodeToKeysym(dpy, touche_code, 0));

		/* 5e champ : le nombre de modules à cet instant, pour reconnaître
		 * ceux qui apparaîtront ensuite (ajustement des visibles) */
		fprintf(f, "%d %d %d %d %d\n", replie, nvis, wy, cote, nmods);
		fprintf(f, "touche %s %u\n", nom != NULL ? nom : "s", touche_mods);
		fclose(f);
	}
}

/* commande système ; rien en aperçu */
static void
agir(const char *cmd)
{
	if (apercu) {
		fprintf(stderr, "aperçu : %s\n", cmd);
		return;
	}
	if (fork() == 0) {
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

/* programme de l'arborescence du volume (Dossier Système), sinon la commande
 * de repli ; « suite » ajoute des arguments */
static void
agir_prog(const char *ref, const char *suite)
{
	char res[PATH_MAX], c[PATH_MAX + 64];

	ptk_resoudre(ref, res, sizeof(res));
	snprintf(c, sizeof(c), "%s%s%s", res, suite != NULL ? " " : "",
	    suite != NULL ? suite : "");
	agir(c);
}

static int
lire_cmd(const char *cmd, char *out, size_t n)
{
	FILE *f = popen(cmd, "r");
	size_t k = 0;

	out[0] = 0;
	if (f) {
		k = fread(out, 1, n - 1, f);
		out[k] = 0;
		pclose(f);
	}
	return (int)k;
}

static int
lire_fichier_int(const char *motif_g, const char *nom, int def)
{
	glob_t g;
	char c[512];
	int v = def;

	if (glob(motif_g, 0, NULL, &g) == 0 && g.gl_pathc) {
		snprintf(c, sizeof(c), "%s/%s", g.gl_pathv[0], nom);
		FILE *f = fopen(c, "r");
		if (f) {
			if (fscanf(f, "%d", &v) != 1)
				v = def;
			fclose(f);
		}
	}
	globfree(&g);
	return v;
}

/* --- bulles de survol : nom et état, sans relancer les commandes à chaque mouvement --- */
static void
fermer_bulle(void)
{
	XUnmapWindow(dpy, bulle);
	survol = -1;
	attente_bulle = 0;
}

static void
etat_module(int i, char *s, size_t n)
{
	const char *c = mods[i].cle;
	char v[256];

	if (!strcmp(c, "airport")) {
		int actif = lire_cmd(SYS_WIFI_STATUT, v, sizeof(v)) > 0 &&
		    strstr(v, "wpa_state=INTERFACE_DISABLED") == NULL;

		snprintf(s, n, "%s", actif ? "activé" : "désactivé");
		if (actif && lire_cmd(SYS_WIFI_SSID, v, sizeof(v)) > 0) {
			v[strcspn(v, "\n")] = 0;
			if (v[0])
				snprintf(s, n, "connecté à %s", v);
		}
	} else if (!strcmp(c, "batterie")) {
		if (lire_cmd(SYS_BATTERIE, v, sizeof(v)) > 0)
			snprintf(s, n, "charge : %d %%", atoi(v));
		else
			snprintf(s, n, "pas de batterie");
	} else if (!strcmp(c, "diction")) {
		snprintf(s, n, "%s", lire_cmd("platine-diction etat 2>/dev/null", v, sizeof(v)) > 0 &&
		    !strncmp(v, "marche", 6) ? "activée" : "désactivée");
	} else if (!strcmp(c, "energie"))
		snprintf(s, n, "veille et écran");
	else if (!strcmp(c, "lumiere"))
		snprintf(s, n, "8 niveaux");
	else if (!strcmp(c, "ecran"))
		snprintf(s, n, "%d × %d", sw, sh);
	else if (!strcmp(c, "processeur")) {
		long u, t;

		ptk_memoire(&u, &t);
		snprintf(s, n, "%d %%, mémoire %ld / %ld Mo", cpu_n ? cpu_hist[cpu_n - 1] : 0,
		    u / 1024, ptk_memoire_installee_mo(t));
	}
	else if (!strcmp(c, "son")) {
		if (lire_cmd(SYS_VOL_LIRE, v, sizeof(v)) > 0)
			volume = atoi(v);
		snprintf(s, n, "%d %%", volume);
	} else if (!strcmp(c, "externe"))
		snprintf(s, n, "%s", mods[i].ext_etat[0] ? mods[i].ext_etat : "module");
}

static void
afficher_bulle(void)
{
	char etat[128], t[256];
	int w, x, y;

	etat_module(survol, etat, sizeof(etat));
	snprintf(t, sizeof(t), "%s — %s", mods[survol].nom, etat);
	w = ptk_text_width(&ptk, ptk.font_small, t) + 12;
	x = (cote ? sw - largeur_totale() : 0) + x_module(survol - premier);
	x = x < 0 ? 0 : x + w > sw ? sw - w : x;
	y = wy >= 26 ? wy - 25 : wy + H + 2;
	XMoveResizeWindow(dpy, bulle, x, y, w, 22);
	XSetForeground(dpy, ptk.gc, ptk.black.pixel);
	XFillRectangle(dpy, bulle, ptk.gc, 0, 0, w, 22);
	XSetForeground(dpy, ptk.gc, ptk_color(&ptk, "#ffffe1").pixel);
	XFillRectangle(dpy, bulle, ptk.gc, 1, 1, w - 2, 20);
	ptk_text(&ptk, bulle, ptk.font_small, 6, 15, t, ptk.text);
	XMapRaised(dpy, bulle);
	etat_age = 0;
}

static void
survoler(XMotionEvent *e)
{
	int w = largeur_totale(), i = -1;

	if (!replie && !tire && !attente_menu && e->x >= x_module(0) && e->x < x_module(nvis))
		i = premier + (e->x - x_module(0)) / L_MODULE;
	if (i < 0 || i >= NMODS || (cote ? e->x < L_ONGLET : e->x >= w - L_ONGLET)) {
		fermer_bulle();
		return;
	}
	if (i != survol) {
		XUnmapWindow(dpy, bulle);
		survol = i;
		attente_bulle = 4;
		etat_age = 20;
	}
}

static void
surveiller_bulle(void)
{
	if (survol < 0)
		return;
	etat_age++;
	if (attente_bulle > 0 && --attente_bulle == 0)
		afficher_bulle();
	else if (attente_bulle == 0 && etat_age >= 20)
		afficher_bulle();
}

/* --- menus : le WM les dessine, comme tous les menus du système ------------------ */
static void
ouvrir_menu(int i, const char *cle, const char *contenu)
{
	XEvent ev;
	int x = (cote ? sw - largeur_totale() : 0) + x_module(i - premier);

	snprintf(dernier_menu, sizeof(dernier_menu), "%s", cle);
	XChangeProperty(dpy, win, a_context, a_utf8, 8, PropModeReplace,
	    (const unsigned char *)contenu, (int)strlen(contenu));
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = a_context;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = x;
	ev.xclient.data.l[1] = wy;
	XUngrabPointer(dpy, CurrentTime);
	XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
	attente_menu = 3;
}

static void
menu_module(int i)
{
	char m[2048], t[256];
	const char *c = mods[i].cle;

	m[0] = 0;
	if (!strcmp(c, "airport")) {
		char ssid[64] = "", etat[1024];
		FILE *f;
		int actif = lire_cmd(SYS_WIFI_STATUT, etat, sizeof(etat)) > 0 &&
		    strstr(etat, "wpa_state=INTERFACE_DISABLED") == NULL;

		lire_cmd(SYS_WIFI_SSID, ssid, sizeof(ssid));
		ssid[strcspn(ssid, "\n")] = 0;
		snprintf(m, sizeof(m),
		    "~AirPort : %s\n-\n%sActiver AirPort\n%sDésactiver AirPort\n",
		    ssid[0] ? "connecté" : actif ? "activé" : "désactivé",
		    actif ? "~" : "", actif ? "" : "~");
		if (actif && (f = popen(SYS_WIFI_LIST, "r")) != NULL) {
			char ligne[512];

			strncat(m, "-\n", sizeof(m) - strlen(m) - 1);
			while (fgets(ligne, sizeof(ligne), f) != NULL) {
				char *save = NULL, *id = strtok_r(ligne, "\t", &save);
				char *nom = strtok_r(NULL, "\t\n", &save);

				if (id == NULL || nom == NULL || strspn(id, "0123456789") != strlen(id))
					continue;
				snprintf(t, sizeof(t), "%s%s\n",
				    strcmp(nom, ssid) == 0 ? COCHE : "", nom);
				strncat(m, t, sizeof(m) - strlen(m) - 1);
			}
			pclose(f);
		}
	} else if (!strcmp(c, "batterie")) {
		char bat[16];
		int pc = lire_cmd(SYS_BATTERIE, bat, sizeof(bat)) > 0 ? atoi(bat) : -1;
		if (pc < 0)
			snprintf(m, sizeof(m), "~Pas de batterie\n-\nÉconomiseur d'énergie…\n");
		else
			snprintf(m, sizeof(m), "~Charge : %d %%\n-\nÉconomiseur d'énergie…\n", pc);
	} else if (!strcmp(c, "diction")) {
		char etat[32] = "";
		int actif = lire_cmd("platine-diction etat 2>/dev/null", etat, sizeof(etat)) > 0 &&
		    strncmp(etat, "marche", 6) == 0;

		snprintf(m, sizeof(m), "%sDiction activée\n%sDiction désactivée\n-\nLire la sélection\n",
		    actif ? COCHE : "", actif ? "" : COCHE);
	} else if (!strcmp(c, "energie")) {
		snprintf(m, sizeof(m), "Suspendre maintenant\nÉteindre l'écran\n-\nÉconomiseur d'énergie…\n");
	} else if (!strcmp(c, "lumiere")) {
		for (int n = 1; n <= 8; n++) {
			snprintf(t, sizeof(t), "Niveau %d\n", n);
			strncat(m, t, sizeof(m) - strlen(m) - 1);
		}
	} else if (!strcmp(c, "processeur")) {
		long u, t;

		ptk_memoire(&u, &t);
		snprintf(m, sizeof(m), "~Processeur : %d %%\n~Mémoire : %ld / %ld Mo\n-\nPlus d'informations…\n",
		    cpu_n ? cpu_hist[cpu_n - 1] : 0, u / 1024, ptk_memoire_installee_mo(t));
	} else if (!strcmp(c, "ecran")) {
		FILE *f = popen("xrandr 2>/dev/null", "r");
		char ligne[512], vus[24][32];
		int nv = 0;

		while (f != NULL && fgets(ligne, sizeof(ligne), f) != NULL) {
			char mode[32], *x;
			int deja = 0;

			if (sscanf(ligne, " %31s", mode) != 1 ||
			    mode[0] < '0' || mode[0] > '9' ||
			    (x = strchr(mode, 'x')) == NULL)
				continue;
			for (int k = 0; k < nv; k++)
				if (strcmp(vus[k], mode) == 0)
					deja = 1;
			if (deja || nv >= (int)(sizeof(vus) / sizeof(vus[0])))
				continue;
			snprintf(vus[nv++], sizeof(vus[0]), "%s", mode);
			*x = 0;
			snprintf(t, sizeof(t), "%s%s × %s\n",
			    strchr(ligne, '*') != NULL ? COCHE : "", mode, x + 1);
			strncat(m, t, sizeof(m) - strlen(m) - 1);
		}
		if (f != NULL)
			pclose(f);
		if (nv == 0) {
			snprintf(t, sizeof(t), "%s%d × %d\n", COCHE, sw, sh);
			strncat(m, t, sizeof(m) - strlen(m) - 1);
		}
		strncat(m, "-\nMoniteurs…\n", sizeof(m) - strlen(m) - 1);
	} else if (!strcmp(c, "externe")) {
		for (int k = 0; k < mods[i].ext_n; k++) {
			snprintf(t, sizeof(t), "%s\n", mods[i].ext_lab[k]);
			strncat(m, t, sizeof(m) - strlen(m) - 1);
		}
		/* le menu se rattache au module par son rang (clé copiée à part :
		 * ouvrir_menu la recopie dans dernier_menu) */
		char cle[32];

		snprintf(cle, sizeof(cle), "externe:%d", i);
		ouvrir_menu(i, cle, m);
		return;
	}
	ouvrir_menu(i, c, m);
}

static void
commande(const char *lab)
{
	char c[256];

	if (*lab == '~')
		lab++;
	if (!strncmp(lab, COCHE, strlen(COCHE)))
		lab += strlen(COCHE);
	if (!strncmp(dernier_menu, "externe:", 8)) {
		int i = atoi(dernier_menu + 8);

		for (int k = 0; i >= N_INTEGRES && i < nmods && k < mods[i].ext_n; k++) {
			const char *cmd = mods[i].ext_cmd[k];

			if (strcmp(mods[i].ext_lab[k], lab) != 0)
				continue;
			if (!strncmp(cmd, "ouvrir:", 7)) {
				/* le Finder ouvre ce dossier du dossier personnel (Captures…) */
				char d[PATH_MAX];

				if (ptk_perso(cmd + 7, d, sizeof(d))) {
					mkdir(d, 0755);
					if (apercu)
						fprintf(stderr, "aperçu : ouvrir %s\n", d);
					else
						XChangeProperty(dpy, root, XInternAtom(dpy, "_PLATINE_OUVRIR", False),
						    a_utf8, 8, PropModeReplace, (const unsigned char *)d,
						    (int)strlen(d));
				}
			} else {
				/* « @chemin|repli|arguments » */
				char ref[512], *args;

				snprintf(ref, sizeof(ref), "%s", cmd);
				args = strchr(ref, '|') ? strchr(strchr(ref, '|') + 1, '|') : NULL;
				if (args != NULL)
					*args++ = '\0';
				agir_prog(ref, args);
			}
			break;
		}
		return;
	}
	if (!strcmp(lab, "Suspendre maintenant"))
		agir(SYS_PRIVILEGE " veille");
	else if (!strcmp(lab, "Éteindre l'écran"))
		agir("sleep 0.5; xset dpms force off");
	else if (!strncmp(lab, "Niveau ", 7)) {
		snprintf(c, sizeof(c), SYS_PRIVILEGE " luminosite %d", atoi(lab + 7) * 100 / 8);
		agir(c);
	} else if (!strcmp(lab, "Activer AirPort"))
		agir(SYS_PRIVILEGE " radio on");
	else if (!strcmp(lab, "Désactiver AirPort"))
		agir(SYS_PRIVILEGE " radio off");
	else if (!strcmp(lab, "Diction activée"))
		agir("platine-diction marche");
	else if (!strcmp(lab, "Diction désactivée"))
		agir("platine-diction arret");
	else if (!strcmp(lab, "Lire la sélection"))
		agir("platine-diction lire");
	else if (!strcmp(lab, "Économiseur d'énergie…"))
		agir_prog("@System/Control Panels/Économiseur d'énergie|platine-tableau economiseur", NULL);
	else if (!strcmp(lab, "Plus d'informations…"))
		agir_prog("@System/Extensions/À propos de cet ordinateur|platine-about", "plus");
	else if (!strcmp(lab, "Moniteurs…"))
		agir_prog("@System/Control Panels/Écran|platine-tableau ecran", NULL);
	else if (!strcmp(dernier_menu, "ecran")) {
		int w, h;

		if (sscanf(lab, "%d × %d", &w, &h) == 2) {
			snprintf(c, sizeof(c), "xrandr -s %dx%d", w, h);
			agir(c);
		}
	} else if (!strcmp(dernier_menu, "airport")) {
		FILE *f = popen(SYS_WIFI_LIST, "r");
		char ligne[512];

		while (f != NULL && fgets(ligne, sizeof(ligne), f) != NULL) {
			char *save = NULL, *id = strtok_r(ligne, "\t", &save);
			char *nom = strtok_r(NULL, "\t\n", &save);

			if (id == NULL || nom == NULL || strcmp(nom, lab) != 0 ||
			    strspn(id, "0123456789") != strlen(id))
				continue;
			snprintf(c, sizeof(c), "wpa_cli -i %s select_network %d",
			    SYS_WIFI_IFACE, atoi(id));
			agir(c);
			break;
		}
		if (f != NULL)
			pclose(f);
	}
}

/* --- volume : curseur vertical dans un panneau posé sur le module ------------------ */
static void
dessiner_volume(void)
{
	Drawable d = vol_back;
	int t = 2 + (100 - volume) * 47 / 100;	/* 2 (haut) … 49 (bas) */

	plein(d, 0, 0, VOL_L, VOL_H, 0);
	plein(d, 1, 1, 19, 64, 0xdd);
	/* rainure */
	plein(d, 7, 3, 1, 59, 0xaa);
	plein(d, 8, 3, 1, 59, 0);
	plein(d, 9, 3, 3, 59, 0xaa);
	plein(d, 12, 3, 1, 59, 0);
	plein(d, 13, 3, 1, 60, 0xff);
	plein(d, 8, 2, 4, 1, 0xaa);
	plein(d, 9, 3, 3, 1, 0);
	plein(d, 8, 62, 1, 1, 0xff);
	plein(d, 9, 62, 3, 1, 0);
	plein(d, 9, 63, 4, 1, 0xff);
	/* curseur : 13 px de haut, trois rainures */
	plein(d, 3, t, 15, 13, 0);
	plein(d, 2, t + 1, 17, 11, 0);
	plein(d, 3, t + 1, 15, 11, 0xaa);
	plein(d, 3, t + 1, 14, 1, 0xcc);
	plein(d, 3, t + 1, 1, 10, 0xcc);
	point(d, 3, t + 1, gris(0xee));
	plein(d, 4, t + 11, 14, 1, 0x55);
	plein(d, 17, t + 2, 1, 10, 0x55);
	point(d, 3, t + 11, gris(0xaa));
	point(d, 17, t + 1, gris(0xaa));
	for (int g = 0; g < 3; g++) {
		plein(d, 6, t + 3 + 2 * g, 8, 1, 0xcc);
		point(d, 6, t + 3 + 2 * g, gris(0xee));
		plein(d, 7, t + 4 + 2 * g, 8, 1, 0x22);
	}
	XSetWindowBackgroundPixmap(dpy, vol_win, vol_back);
	XClearWindow(dpy, vol_win);
}

static void
regler_depuis_y(int y)
{
	int v = 100 - (y - 8) * 100 / 47;

	volume = v < 0 ? 0 : v > 100 ? 100 : v;
	dessiner_volume();
	char c[64];
	snprintf(c, sizeof(c), SYS_VOL_ECRIRE, SYS_VOL_BRUT(volume));
	agir(c);
}

static void
ouvrir_volume(int i)
{
	char s[512];

	vol_x = (cote ? sw - largeur_totale() : 0) + x_module(i - premier) + (30 - VOL_L) / 2;
	if (lire_cmd(SYS_VOL_LIRE, s, sizeof(s)) > 0)
		volume = atoi(s);
	/* l'indicateur central (platine-osd) se tait pendant ce réglage */
	XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_PLATINE_VOLUME", False), XA_CARDINAL, 8,
	    PropModeReplace, (unsigned char *)"1", 1);
	XMoveWindow(dpy, vol_win, vol_x, wy - VOL_H + 3);
	XMapRaised(dpy, vol_win);
	dessiner_volume();
	XGrabPointer(dpy, vol_win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

static void
fermer_volume(void)
{
	XDeleteProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_PLATINE_VOLUME", False));
	XUngrabPointer(dpy, CurrentTime);
	XUnmapWindow(dpy, vol_win);
	vol_drag = 0;
	enfonce = -1;
	dessiner();
}

/* --- événements ------------------------------------------------------------------ */
static void
appui(XButtonEvent *e)
{
	int w = largeur_totale(), i;
	int sur_onglet = cote ? e->x < L_ONGLET : e->x >= w - L_ONGLET;

	if (sur_onglet) {
		tire = 1;
		tire_x0 = e->x_root;
		tire_y0 = e->y_root;
		tire_y = wy;
		tire_n = nvis;
		tire_bouge = 0;
		return;
	}
	if (cote ? e->x >= w - 15 : e->x < 15) {
		replie = 1;
		reagencer();
		enregistrer();
		ptk_son(&ptk, "tabp");
		return;
	}
	if (e->x < x_module(0)) {
		if (premier > 0) {
			premier--;
			dessiner();
		}
		return;
	}
	i = (e->x - x_module(0)) / L_MODULE;
	if (i >= nvis) {
		if (premier + nvis < NMODS) {
			premier++;
			dessiner();
		}
		return;
	}
	i += premier;
	enfonce = i;
	dessiner();
	XFlush(dpy);
	if (!strcmp(mods[i].cle, "son"))
		ouvrir_volume(i);
	else
		menu_module(i);
}

static void
glisse_onglet(XMotionEvent *e, unsigned etat)
{
	int dx = e->x_root - tire_x0, dy = e->y_root - tire_y0;

	if (abs(dx) + abs(dy) < 3 && !tire_bouge)
		return;
	tire_bouge = 1;
	if (replie)
		return;
	/* Option-glisser (Alt, Win, Ctrl ou Maj ici) : la barre suit le pointeur, en hauteur
	 * et d'un bord à l'autre de l'écran */
	if (etat & (Mod1Mask | Mod4Mask | ControlMask | ShiftMask)) {
		int y = tire_y + dy, c = e->x_root > sw / 2;

		wy = y < 20 ? 20 : y > sh - H ? sh - H : y;
		if (c != cote) {
			cote = c;
			reagencer();
		} else
			XMoveWindow(dpy, win, cote ? sw - largeur_totale() : 0, wy);
		return;
	}
	if (cote)
		dx = -dx;
	int n = tire_n + (dx >= 0 ? dx + L_MODULE / 2 : dx - L_MODULE / 2) / L_MODULE;
	n = n < 1 ? 1 : n > NMODS ? NMODS : n;
	if (n != nvis) {
		nvis = n;
		reagencer();
	}
}

static void
relache_onglet(void)
{
	tire = 0;
	if (!tire_bouge) {
		replie = !replie;
		reagencer();
		ptk_son(&ptk, replie ? "tabp" : "tabr");
	}
	ptk_son(&ptk, "btnr");
	enregistrer();
}

/* le menu du WM tient le pointeur : quand il le rend, le module se relève */
static void
surveiller_menu(void)
{
	if (!attente_menu)
		return;
	if (attente_menu > 1) {
		attente_menu--;
		return;
	}
	if (XGrabPointer(dpy, win, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess) {
		XUngrabPointer(dpy, CurrentTime);
		attente_menu = 0;
		enfonce = -1;
		dessiner();
		ptk_son(&ptk, "btnr");
	}
}

/* flotte au-dessus des fenêtres, mais jamais au-dessus d'un menu ouvert */
static void
remonter(void)
{
	if (attente_menu || tire)
		return;
	if (XGrabPointer(dpy, win, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess) {
		XUngrabPointer(dpy, CurrentTime);
		XRaiseWindow(dpy, win);
	}
}

/* nombre de modules visibles enregistré, et total de modules à ce moment
 * (-1 : ancien format sans total) */
static int cfg_n = -1, cfg_tot = -1;

/* les modules apparus depuis le dernier enregistrement s'ajoutent aux visibles
 * (comme un module tout juste installé sous Mac OS 9) ; ancien format : 7 ou
 * plus voulait dire « tous » */
static void
ajuster_visibles(void)
{
	if (cfg_n < 0) {
		/* jamais enregistré : tous les modules */
		nvis = nmods;
		return;
	}
	if (cfg_tot >= 0 && cfg_tot < nmods)
		nvis = cfg_n + (nmods - cfg_tot);
	else if (cfg_tot < 0 && cfg_n >= N_INTEGRES - 1)
		nvis = nmods;
	if (nvis > nmods)
		nvis = nmods;
	if (nvis < 1)
		nvis = 1;
	if (cfg_tot != nmods) {
		cfg_tot = nmods;
		cfg_n = nvis;
		enregistrer();
	}
}

static void
charger_config(void)
{
	FILE *f = fopen(cfg, "r");
	char ligne[128], nom[32];
	int r = replie, n = nvis, y = wy, cc = cote;
	unsigned m;

	if (f != NULL) {
		int tot = -1;

		if (fgets(ligne, sizeof(ligne), f) != NULL &&
		    sscanf(ligne, "%d %d %d %d %d", &r, &n, &y, &cc, &tot) >= 3) {
			cote = cc != 0;
			replie = r != 0;
			nvis = n < 1 ? 1 : n;
			cfg_n = n;
			cfg_tot = tot;
			wy = y < 20 || y > sh - H ? sh - H : y;
		}
		if (fgets(ligne, sizeof(ligne), f) != NULL &&
		    sscanf(ligne, "touche %31s %u", nom, &m) == 2) {
			KeySym ks = XStringToKeysym(nom);
			KeyCode code = ks != NoSymbol ? XKeysymToKeycode(dpy, ks) : 0;

			if (code != 0) {
				touche_code = code;
				touche_mods = m & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
			}
		}
		fclose(f);
	}
}

static void
installer_raccourci(KeyCode ancien_code, unsigned ancien_mods)
{
	unsigned verrous[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
	size_t i;

	if (ancien_code)
		for (i = 0; i < sizeof(verrous) / sizeof(verrous[0]); i++)
			XUngrabKey(dpy, ancien_code, ancien_mods | verrous[i], root);
	for (i = 0; i < sizeof(verrous) / sizeof(verrous[0]); i++)
		XGrabKey(dpy, touche_code, touche_mods | verrous[i], root, False,
		    GrabModeAsync, GrabModeAsync);
}

int
main(void)
{
	XSetWindowAttributes a;
	XEvent ev;
	const char *home = getenv("HOME");
	int doit_remonter = 0;

	apercu = getenv("PLATINE_APERCU") != NULL;
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "platine-strip : pas d'affichage X\n");
		return 1;
	}
	if (ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;
	root = DefaultRootWindow(dpy);
	sw = DisplayWidth(dpy, ptk.screen);
	sh = DisplayHeight(dpy, ptk.screen);
	wy = sh - H;
	a_context = XInternAtom(dpy, "_PLATINE_CONTEXT", False);
	a_command = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	snprintf(cfg, sizeof(cfg), "%s/.config/platine-strip", home ? home : "/tmp");
	touche_code = XKeysymToKeycode(dpy, XK_s);
	charger_config();
	signal(SIGUSR1, sig_reload);
	/* le tableau Control Strip signale ce processus précis : le nom de
	 * processus ne suffit plus (l'extension et le tableau s'appellent tous
	 * deux « Control Strip » dans l'arborescence du volume) */
	{
		char pf[PATH_MAX];
		FILE *f;

		snprintf(pf, sizeof(pf), "%s/.cache", home ? home : "/tmp");
		mkdir(pf, 0755);
		snprintf(pf, sizeof(pf), "%s/.cache/platine-strip.pid", home ? home : "/tmp");
		if ((f = fopen(pf, "w")) != NULL) {
			fprintf(f, "%ld\n", (long)getpid());
			fclose(f);
		}
	}
	/* agir() et les sons lancent des enfants jamais attendus : sans ceci, un zombie par
	 * réglage de volume ou par son (131 relevés sur le 701 après une journée) */
	ptk_ramasser_enfants();	/* pas SIG_IGN : hérité par les programmes lancés */
	installer_raccourci(0, 0);
	XSelectInput(dpy, root, KeyPressMask);
	modules_externes();
	ajuster_visibles();
	for (int i = 0; i < N_INTEGRES; i++) {
		char n[64];
		if (mods[i].icone == NULL)
			continue;
		snprintf(n, sizeof(n), "%s.xpm", mods[i].icone);
		if (ptk_load_xpm(&ptk, n, &mods[i].pix, &mods[i].mask) != 0)
			mods[i].pix = 0;
	}
	a.override_redirect = True;
	a.event_mask = ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
	    PointerMotionMask | EnterWindowMask | LeaveWindowMask | VisibilityChangeMask;
	a.background_pixel = gris(0xc0);
	win = XCreateWindow(dpy, root, cote ? sw - largeur_totale() : 0, wy, largeur_totale(), H, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWEventMask | CWBackPixel, &a);
	XStoreName(dpy, win, "Control Strip");
	a.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	vol_win = XCreateWindow(dpy, root, 0, 0, VOL_L, VOL_H, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWEventMask, &a);
	vol_back = XCreatePixmap(dpy, vol_win, VOL_L, VOL_H, DefaultDepth(dpy, ptk.screen));
	a.event_mask = ExposureMask;
	bulle = XCreateWindow(dpy, root, 0, 0, 1, 22, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWEventMask, &a);
	XStoreName(dpy, bulle, "Control Strip — info");
	{
		Pixmap b = XCreatePixmap(dpy, win, 1, 1, 1);
		bit_gc = XCreateGC(dpy, b, 0, NULL);
		XFreePixmap(dpy, b);
	}
	reagencer();
	XMapRaised(dpy, win);
	XDefineCursor(dpy, win, XCreateFontCursor(dpy, XC_left_ptr));
	XDefineCursor(dpy, vol_win, XCreateFontCursor(dpy, XC_left_ptr));

	for (;;) {
		struct pollfd pf = { ConnectionNumber(dpy), POLLIN, 0 };

		if (reload_demande) {
			KeyCode ancien_code = touche_code;
			unsigned ancien_mods = touche_mods;

			reload_demande = 0;
			charger_config();
			/* un module posé ou retiré (Installateur) : relecture du dossier,
			 * les nouveaux modules visibles */
			modules_externes();
			ajuster_visibles();
			installer_raccourci(ancien_code, ancien_mods);
			fermer_bulle();
			reagencer();
			XRaiseWindow(dpy, win);
			XFlush(dpy);
		}
		if (!XPending(dpy)) {
			int pr;

			XFlush(dpy);
			pr = poll(&pf, 1, 120);
			if (pr < 0) {
				if (errno == EINTR)
					continue;
				pr = 0;
			}
			if (pr == 0) {
				cpu_mesurer();
				surveiller_menu();
				surveiller_bulle();
				if (doit_remonter) {
					doit_remonter = 0;
					remonter();
				}
				continue;
			}
		}
		if (reload_demande)
			continue;
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case KeyPress:
			if (ev.xkey.keycode == touche_code &&
			    (ev.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) == touche_mods) {
				fermer_bulle();
				replie = !replie;
				reagencer();
				enregistrer();
				ptk_son(&ptk, replie ? "tabp" : "tabr");
			}
			break;
		case ButtonPress:
			fermer_bulle();
			if (ev.xbutton.window == vol_win) {
				if (ev.xbutton.x < 0 || ev.xbutton.x >= VOL_L ||
				    ev.xbutton.y < 0 || ev.xbutton.y >= VOL_H - 3)
					fermer_volume();
				else {
					vol_drag = 1;
					regler_depuis_y(ev.xbutton.y);
				}
			} else {
				ptk_son(&ptk, "btnp");
				appui(&ev.xbutton);
			}
			break;
		case ButtonRelease:
			if (ev.xbutton.window == vol_win) {
				vol_drag = 0;
				ptk_son(&ptk, "btnr");
			} else if (tire)
				relache_onglet();
			break;
		case MotionNotify:
			if (ev.xmotion.window == vol_win) {
				if (vol_drag)
					regler_depuis_y(ev.xmotion.y);
			} else {
				if (tire)
					glisse_onglet(&ev.xmotion, ev.xmotion.state);
				else
					survoler(&ev.xmotion);
			}
			break;
		case EnterNotify:
			if (!tire && !attente_menu)
				survoler((XMotionEvent *)&ev.xcrossing);
			break;
		case LeaveNotify:
			if (ev.xcrossing.window == win)
				fermer_bulle();
			break;
		case Expose:
			if (ev.xexpose.window == bulle && survol >= 0)
				afficher_bulle();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				doit_remonter = 1;
			break;
		case ClientMessage:
			if (ev.xclient.message_type == a_command) {
				Atom t;
				int f;
				unsigned long n, r;
				unsigned char *d = NULL;
				if (XGetWindowProperty(dpy, win, a_command, 0, 256, True, AnyPropertyType,
				    &t, &f, &n, &r, &d) == Success && d) {
					commande((const char *)d);
					XFree(d);
				}
			}
			break;
		}
	}
}
