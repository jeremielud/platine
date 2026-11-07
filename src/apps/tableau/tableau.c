/*
 * platine-tableau NOM : les tableaux de bord de Platine, copies de ceux de Mac OS 9.2 relevées sur la VM.
 * NOM : apparence, clavier, diction, ecran, son, souris. Tous les contrôles viennent de la Toolbox.
 * Les réglages vont dans ~/.config/platine/reglages, relus par libptk et platine-session.
 */
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <X11/Xatom.h>
#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_sys.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Tableaux de bord", "tabl", "@nom", PTK_VERSION, "");


static PtkApp app;
static PtkWindow *fen;
static XftFont *gras9;
typedef struct { Pixmap p; int w, h, x, y, page; } Image;
static Image images[16];
static int n_images;
typedef struct { XftFont **f; int x, y, page, centre; char t[160]; int gris; } Texte;
static Texte textes[64];
static int n_textes;
static int decal_x, decal_y;	/* décalage des pages d'onglets, relevé sur la référence */
static void (*peindre_extra)(PtkWindow *);
static PtkControl *onglets;

/* --- Réglages --- */
static char *
chemin(void)
{
	static char c[512];
	const char *h = getenv("HOME");

	snprintf(c, sizeof(c), "%s/.config/platine/reglages", h != NULL ? h : ".");
	return c;
}

static void
lire(const char *cle, char *v, size_t n, const char *def)
{
	FILE *f = fopen(chemin(), "r");
	char l[256];
	size_t lc = strlen(cle);

	snprintf(v, n, "%s", def);
	if (f == NULL)
		return;
	while (fgets(l, sizeof(l), f) != NULL)
		if (strncmp(l, cle, lc) == 0 && l[lc] == ' ') {
			l[strcspn(l, "\n")] = '\0';
			snprintf(v, n, "%s", l + lc + 1);
			break;
		}
	fclose(f);
}

static int
lire_int(const char *cle, int def)
{
	char v[32], d[32];

	snprintf(d, sizeof(d), "%d", def);
	lire(cle, v, sizeof(v), d);
	return atoi(v);
}

static void
ecrire(const char *cle, const char *val)
{
	char tmp[540], l[256], cmd[600];
	size_t lc = strlen(cle);
	FILE *e, *s;

	snprintf(cmd, sizeof(cmd), "mkdir -p \"$HOME/.config/platine\"");
	if (system(cmd) != 0)
		return;
	snprintf(tmp, sizeof(tmp), "%s.neuf", chemin());
	if ((s = fopen(tmp, "w")) == NULL)
		return;
	if ((e = fopen(chemin(), "r")) != NULL) {
		while (fgets(l, sizeof(l), e) != NULL)
			if (!(strncmp(l, cle, lc) == 0 && l[lc] == ' '))
				fputs(l, s);
		fclose(e);
	}
	fprintf(s, "%s %s\n", cle, val);
	fclose(s);
	rename(tmp, chemin());
}

static void
ecrire_int(const char *cle, int v)
{
	char t[32];

	snprintf(t, sizeof(t), "%d", v);
	ecrire(cle, t);
}

static void
lancer(const char *fmt, const char *a)
{
	char c[512];

	snprintf(c, sizeof(c), fmt, a);
	if (system(c) != 0)
		return;
}

/* --- Aide à la construction --- */
static void
texte(XftFont **f, int x, int y, int page, int centre, const char *t)
{
	Texte *s;

	if (n_textes >= 64)
		return;
	s = &textes[n_textes++];
	s->f = f;
	s->x = x + (page >= 0 ? decal_x : 0);
	s->y = y + (page >= 0 ? decal_y : 0);
	s->page = page;
	s->centre = centre;
	s->gris = 0;
	snprintf(s->t, sizeof(s->t), "%s", t);
}

static void
image(const char *nom, int x, int y, int page)
{
	Image *i;

	if (n_images >= 16)
		return;
	i = &images[n_images];
	i->p = ptk_load_ppm(&app.ptk, nom, &i->w, &i->h);
	if (i->p == None)
		return;
	i->x = x + (page >= 0 ? decal_x : 0);
	i->y = y + (page >= 0 ? decal_y : 0);
	i->page = page;
	n_images++;
}

static PtkControl *
ctl(int kind, int x, int y, int w, int h, const char *label, int page)
{
	PtkControl *c = ptk_control_add(fen, kind, x + (page >= 0 ? decal_x : 0), y + (page >= 0 ? decal_y : 0), w, h, label);

	ptk_control_set_page(c, page);
	if (page >= 0 && onglets != NULL)
		c->visible = page == onglets->value;
	return c;
}

static PtkControl *
curseur(int x, int y, int w, int vmax, int val, int plat, void (*a)(PtkWindow *, PtkControl *, void *), int page)
{
	PtkControl *c = ctl(PTK_CURSEUR, x, y, w, 20, "", page);

	c->min = 0;
	c->max = vmax;
	c->value = val;
	c->checked = plat;
	if (a != NULL)
		ptk_control_set_action(c, a, NULL);
	return c;
}

static void
dessin(PtkWindow *w, void *u)
{
	Ptk *p = w->p;
	int i, page = onglets != NULL ? onglets->value : -1;

	(void)u;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	(void)page;
	/* le panneau d'onglets est dessiné par la Toolbox : images et textes passent par peindre_apres */
	for (i = 0; i < n_images; i++)
		if (images[i].page < 0 && images[i].p != None)
			XCopyArea(p->dpy, images[i].p, w->surface, p->gc, 0, 0, (unsigned)images[i].w, (unsigned)images[i].h,
			    images[i].x, images[i].y);
	for (i = 0; i < n_textes; i++)
		if (textes[i].page < 0) {
			XftFont *f = *textes[i].f;
			int x = textes[i].centre ? textes[i].x - ptk_text_width(p, f, textes[i].t) / 2 : textes[i].x;

			ptk_text(p, w->surface, f, x, textes[i].y, textes[i].t, p->text);
		}
	if (peindre_extra != NULL)
		peindre_extra(w);
}

/* --- Souris --- */
static void
a_souris(PtkWindow *w, PtkControl *c, void *u)
{
	char a[32];

	(void)w;
	(void)u;
	ecrire_int("vitesse_souris", c->value + 1);
	snprintf(a, sizeof(a), "%.2f", 3.0 / (c->value + 1));
	lancer("xinput set-prop /dev/wsmouse 'Device Accel Constant Deceleration' %s 2>/dev/null", a);
}

static void
a_double(PtkWindow *w, PtkControl *c, void *u)
{
	static const int ms[3] = { 800, 533, 266 };
	char a[32];

	(void)w;
	(void)u;
	ecrire_int("double_clic", ms[c->value]);
	snprintf(a, sizeof(a), "%d", ms[c->value]);
	lancer("xprop -root -f _PLATINE_DOUBLE_CLIC 32c -set _PLATINE_DOUBLE_CLIC %s", a);
}

static void
tab_souris(void)
{
	int d = lire_int("double_clic", 533);

	image("tableaux/souris-tablette.ppm", 16, 37, -1);
	image("tableaux/souris-lente.ppm", 74, 26, -1);
	image("tableaux/souris-rapide.ppm", 144, 26, -1);
	image("tableaux/souris-double.ppm", 14, 134, -1);
	image("tableaux/fleches-1.ppm", 65, 133, -1);
	image("tableaux/fleches-2.ppm", 114, 133, -1);
	image("tableaux/fleches-3.ppm", 162, 133, -1);
	texte(&app.ptk.font_small, 96, 15, -1, 1, "Vitesse de suivi");
	texte(&app.ptk.font_small, 28, 97, -1, 1, "Très lente");
	texte(&app.ptk.font_small, 84, 97, -1, 1, "Lente");
	texte(&app.ptk.font_small, 160, 97, -1, 1, "Rapide");
	texte(&app.ptk.font_small, 96, 124, -1, 1, "Vitesse du double-clic");
	ctl(PTK_GROUPE, 5, 108, 182, 1, "", -1);
	curseur(13, 64, 165, 5, lire_int("vitesse_souris", 3) - 1, 0, a_souris, -1);
	curseur(40, 160, 137, 2, d >= 700 ? 0 : d >= 400 ? 1 : 2, 0, a_double, -1);
}

/* --- Clavier --- */
static const struct { const char *nom, *xkb; } dispositions[] = {
	{ "Allemand", "de" }, { "Américain", "us" }, { "Belge", "be" }, { "Britannique", "gb" }, { "Canadien français", "ca fr" },
	{ "Espagnol", "es" }, { "Français", "fr" }, { "Italien", "it" }, { "Néerlandais", "nl" }, { "Suisse allemand", "ch de" },
	{ "Suisse romand", "ch fr" },
};
#define ND ((int)(sizeof(dispositions) / sizeof(dispositions[0])))
static PtkControl *c_rep, *c_del;

static void
a_disposition(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire("clavier", dispositions[c->value].xkb);
	lancer("setxkbmap %s", dispositions[c->value].xkb);
}

static void
a_repetition(PtkWindow *w, PtkControl *c, void *u)
{
	static const int vit[5] = { 5, 10, 18, 30, 45 }, del[5] = { 150, 250, 400, 650, 1000 };
	char a[64];

	(void)w;
	(void)c;
	(void)u;
	ecrire_int("repetition_vitesse", vit[c_rep->value]);
	if (c_del->value >= 5) {
		ecrire("repetition_delai", "0");
		lancer("xset r off%s", "");
		return;
	}
	ecrire_int("repetition_delai", del[c_del->value]);
	snprintf(a, sizeof(a), "%d %d", del[c_del->value], vit[c_rep->value]);
	lancer("xset r on; xset r rate %s", a);
}

static void
tab_clavier(void)
{
	char items[512] = "", k[32];
	int i, sel = 6, v = lire_int("repetition_vitesse", 30), d = lire_int("repetition_delai", 400);
	PtkControl *l;

	lire("clavier", k, sizeof(k), "fr");
	for (i = 0; i < ND; i++) {
		if (i > 0)
			strncat(items, "|", sizeof(items) - strlen(items) - 1);
		strncat(items, dispositions[i].nom, sizeof(items) - strlen(items) - 1);
		if (strcmp(dispositions[i].xkb, k) == 0)
			sel = i;
	}
	ctl(PTK_GROUPE, 6, 18, 245, 154, "Dispositions du clavier", -1);
	texte(&app.ptk.font_small, 16, 34, -1, 0, "La disposition choisie s'applique tout de suite");
	texte(&app.ptk.font_small, 16, 47, -1, 0, "à toutes les applications.");
	l = ctl(PTK_LISTE, 18, 58, 222, 104, "", -1);
	l->police = app.ptk.font_menu;
	ptk_control_set_items(l, items);
	l->value = sel;
	l->defil = sel > 2 ? sel - 2 : 0;
	ptk_control_set_action(l, a_disposition, NULL);
	ctl(PTK_GROUPE, 6, 188, 245, 72, "Répétition des touches", -1);
	texte(&app.ptk.font_small, 66, 203, -1, 1, "Vitesse de répétition");
	texte(&app.ptk.font_small, 186, 203, -1, 1, "Délai avant répétition");
	texte(&app.ptk.font_small, 26, 248, -1, 1, "Lente");
	texte(&app.ptk.font_small, 110, 248, -1, 1, "Rapide");
	texte(&app.ptk.font_small, 150, 248, -1, 1, "Court");
	texte(&app.ptk.font_small, 206, 248, -1, 1, "Long");
	texte(&app.ptk.font_small, 232, 248, -1, 1, "Non");
	c_rep = curseur(23, 213, 96, 4, v >= 45 ? 4 : v >= 30 ? 3 : v >= 18 ? 2 : v >= 10 ? 1 : 0, 0, a_repetition, -1);
	c_del = curseur(137, 213, 96, 5, d == 0 ? 5 : d >= 1000 ? 4 : d >= 650 ? 3 : d >= 400 ? 2 : d >= 250 ? 1 : 0, 0, a_repetition, -1);
}

/* --- Écran --- */
static void
tab_ecran(void)
{
	char buf[1024] = "", ligne[128];
	FILE *f;
	PtkControl *l;
	int n = 0;

	if ((f = popen("xrandr 2>/dev/null | awk '/^ +[0-9]+x[0-9]+/ { sub(/x/, \" x \", $1); print $1 }'", "r")) != NULL) {
		while (fgets(ligne, sizeof(ligne), f) != NULL) {
			ligne[strcspn(ligne, "\n")] = '\0';
			if (n++ > 0)
				strncat(buf, "|", sizeof(buf) - strlen(buf) - 1);
			strncat(buf, ligne, sizeof(buf) - strlen(buf) - 1);
		}
		pclose(f);
	}
	image("tableaux/ecran-entete.ppm", 22, 11, -1);
	ctl(PTK_GROUPE, 14, 83, 224, 137, "Couleurs", -1);
	{
		PtkControl *g = ctl(PTK_RADIO, 24, 102, 70, 16, "Gris", -1), *c = ctl(PTK_RADIO, 24, 124, 76, 16, "Couleurs", -1);

		g->enabled = 0;
		c->checked = 1;
		l = ctl(PTK_LISTE, 104, 96, 130, 83, "", -1);
		ptk_control_set_items(l, "Millions");
		image("tableaux/ecran-degrade.ppm", 46, 185, -1);
	}
	ctl(PTK_GROUPE, 246, 83, 236, 137, "Résolution", -1);
	texte(&app.ptk.font_small, 256, 113, -1, 0, "Afficher :");
	{
		PtkControl *pp = ctl(PTK_POPUP, 308, 100, 143, 20, "Recommandées", -1);

		ptk_control_set_items(pp, "Recommandées|Toutes");
		ptk_control_set_text(pp, "Recommandées");
	}
	l = ctl(PTK_LISTE, 262, 134, 209, 78, "", -1);
	ptk_control_set_items(l, buf[0] ? buf : "800 x 480");
}

/* --- Son --- */
static char sons[32][48];
static int n_sons;
static PtkControl *c_liste_son;

static void
jouer_alerte(void)
{
	char c[256];

	if (c_liste_son == NULL || c_liste_son->value < 0 || c_liste_son->value >= n_sons)
		return;
	snprintf(c, sizeof(c), "audioplay -q '/opt/platine/assets/alertes/%s.wav' >/dev/null 2>&1 &", sons[c_liste_son->value]);
	if (system(c) != 0)
		return;
}

static void
a_alerte(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire("son_alerte", sons[c->value]);
	jouer_alerte();
}

static void
a_jouer(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	jouer_alerte();
}

static int volume_avant;
static PtkControl *c_vol, *c_muet;

static void
appliquer_volume(void)
{
	char a[32];

	ecrire_int("volume", c_vol->value);
	snprintf(a, sizeof(a), "%d", c_muet->checked ? 0 : c_vol->value * 255 / 100);
	lancer("mixerctl -w outputs.master=%s >/dev/null 2>&1", a);
}

static void
a_volume(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	appliquer_volume();
}

static void
a_vol_alerte(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire_int("volume_alerte", c->value);
}

/* les noms du tableau de bord d'origine, le fichier extrait étant en minuscules */
static const char *
nom_son(const char *f)
{
	static const struct { const char *f, *n; } t[] = {
		{ "chutoy", "ChuToy" }, { "glass", "Glass" }, { "indigo", "Indigo" }, { "laugh", "Laugh" },
		{ "logjam", "Logjam" }, { "pong2003", "Pong2003" }, { "purr", "Purr" }, { "simplebeep", "SimpleBeep" },
		{ "single-click", "Single Click" }, { "sosumi", "Sosumi" }, { "submarine", "Submarine" }, { "temple", "Temple" },
		{ "uh-oh", "Uh oh" }, { "voltage", "Voltage" }, { "whit", "Whit" },
	};
	size_t i;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (strcmp(t[i].f, f) == 0)
			return t[i].n;
	return f;
}

static void
fin_son(void)
{
	Display *d = XOpenDisplay(NULL);

	if (d != NULL) {
		XDeleteProperty(d, DefaultRootWindow(d), XInternAtom(d, "_PLATINE_VOLUME", False));
		XCloseDisplay(d);
	}
}

static void
fin_signal(int n)
{
	(void)n;
	exit(0);
}

static void
tab_son(void)
{
	/* ce tableau règle lui-même le volume : pas d'indicateur central par-dessus (platine-osd) */
	XChangeProperty(app.dpy, app.root, XInternAtom(app.dpy, "_PLATINE_VOLUME", False), XA_CARDINAL, 8, PropModeReplace,
	    (const unsigned char *)"1", 1);
	atexit(fin_son);
	signal(SIGTERM, fin_signal);
	DIR *d = opendir("/opt/platine/assets/alertes");
	struct dirent *e;
	char items[1024] = "", choisi[48];
	int i, j, sel = 0;
	PtkControl *c;

	lire("son_alerte", choisi, sizeof(choisi), "simplebeep");
	while (d != NULL && (e = readdir(d)) != NULL && n_sons < 32) {
		char *pt = strrchr(e->d_name, '.');

		if (pt == NULL || strcmp(pt, ".wav") != 0)
			continue;
		*pt = '\0';
		snprintf(sons[n_sons++], sizeof(sons[0]), "%s", e->d_name);
	}
	if (d != NULL)
		closedir(d);
	for (i = 0; i < n_sons; i++)
		for (j = i + 1; j < n_sons; j++)
			if (strcmp(sons[i], sons[j]) > 0) {
				char t[48];

				memcpy(t, sons[i], 48);
				memcpy(sons[i], sons[j], 48);
				memcpy(sons[j], t, 48);
			}
	for (i = 0; i < n_sons; i++) {
		char u[64];

		snprintf(u, sizeof(u), "%s%s\tintégré", i ? "|" : "", nom_son(sons[i]));
		strncat(items, u, sizeof(items) - strlen(items) - 1);
		if (strcmp(sons[i], choisi) == 0)
			sel = i;
	}
	onglets = ctl(PTK_ONGLETS, -1, 10, 402, 306, "", -1);
	ptk_control_set_items(onglets, "Alertes|Entrée|Sortie");
	onglets->page = -1;
	texte(&app.ptk.font_menu, 13, 55, 0, 0, "Choisissez un son d'alerte :");
	c_liste_son = c = ctl(PTK_LISTE, 13, 63, 276, 194, "Nom\tType", 0);
	ptk_control_set_items(c, items);
	c->value = sel;
	c->defil = sel > 6 ? sel - 6 : 0;
	ptk_control_set_action(c, a_alerte, NULL);
	c = ctl(PTK_BUTTON, 303, 102, 84, 22, "Écouter", 0);
	ptk_control_set_action(c, a_jouer, NULL);
	texte(&app.ptk.font_menu, 13, 284, 0, 0, "Volume d'alerte :");
	curseur(130, 269, 135, 100, lire_int("volume_alerte", 100), 1, a_vol_alerte, 0);
	texte(&app.ptk.font_small, 40, 70, 1, 0, "Aucune source d'entrée sur le 701 : le micro n'est pas géré.");
	texte(&app.ptk.font_small, 40, 70, 2, 0, "Haut-parleurs intégrés (Realtek ALC662).");
	texte(&app.ptk.font_menu, 13, 336, -1, 0, "Volume général :");
	volume_avant = lire_int("volume", 60);
	c_vol = curseur(130, 321, 135, 100, volume_avant, 1, a_volume, -1);
	c_muet = ctl(PTK_CHECKBOX, 318, 325, 76, 16, "Silence", -1);
	ptk_control_set_action(c_muet, a_volume, NULL);
}

/* --- Diction --- */
static void
a_diction(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire_int("diction", c->checked && strcmp(c->label, "Oui") == 0);
}

static void
tab_diction(void)
{
	int moteur = system("command -v espeak >/dev/null 2>&1 || command -v espeak-ng >/dev/null 2>&1") == 0;
	PtkControl *pp, *o, *n, *b;

	image("tableaux/diction-icone.ppm", 16, 18, -1);
	ctl(PTK_GROUPE, 56, 12, 336, 180, "", -1);
	texte(&app.ptk.font_menu, 66, 28, -1, 0, "Options :");
	pp = ctl(PTK_POPUP, 125, 14, 180, 20, "", -1);
	ptk_control_set_items(pp, "Éléments parlables");
	ptk_control_set_text(pp, "Éléments parlables");
	texte(&app.ptk.font_small, 70, 58, -1, 0, "L'option Éléments parlables ouvre (ou lance) les éléments");
	texte(&app.ptk.font_small, 70, 71, -1, 0, "du dossier Éléments parlables quand on énonce leur nom.");
	texte(&app.ptk.font_small, 70, 100, -1, 0, "Éléments parlables :");
	o = ctl(PTK_RADIO, 175, 90, 60, 14, "Oui", -1);
	n = ctl(PTK_RADIO, 175, 106, 60, 14, "Non", -1);
	b = ctl(PTK_CHECKBOX, 70, 124, 300, 14, "Reconnaître « OK » et « Annuler »", -1);
	(lire_int("diction", 0) ? o : n)->checked = 1;
	b->checked = 1;
	ptk_control_set_action(o, a_diction, NULL);
	ptk_control_set_action(n, a_diction, NULL);
	if (!moteur) {
		o->enabled = n->enabled = b->enabled = 0;
		texte(&app.ptk.font_small, 70, 165, -1, 0, "Aucun moteur de synthèse vocale n'est installé (espeak).");
	}
}

/* --- Control Strip --- */
static int strip_r, strip_n, strip_y, strip_c, strip_capture;
static int strip_tot = -1;	/* modules au dernier enregistrement du Strip, conservé tel quel */
static char strip_touche[32] = "s";
static unsigned strip_mods = Mod1Mask | ControlMask;
static PtkControl *strip_libelle, *strip_bouton;

static void
strip_lire(void)
{
	const char *h = getenv("HOME");
	char c[512], nom[32];
	FILE *f;
	int r = 0, n = 7, y = 456, cc = 0;
	unsigned m;

	strip_r = 0;
	strip_n = 7;
	strip_y = 456;
	strip_c = 0;
	snprintf(strip_touche, sizeof(strip_touche), "s");
	strip_mods = Mod1Mask | ControlMask;
	snprintf(c, sizeof(c), "%s/.config/platine-strip", h != NULL ? h : "/tmp");
	if ((f = fopen(c, "r")) != NULL) {
		char l[128];
		int tot = -1;

		if (fgets(l, sizeof(l), f) != NULL &&
		    sscanf(l, "%d %d %d %d %d", &r, &n, &y, &cc, &tot) >= 3) {
			strip_r = r != 0;
			strip_n = n < 1 ? 1 : n > 7 ? 7 : n;
			strip_y = y;
			strip_c = cc != 0;
			strip_tot = tot;
		}
		if (fscanf(f, " touche %31s %u", nom, &m) == 2 && XStringToKeysym(nom) != NoSymbol) {
			snprintf(strip_touche, sizeof(strip_touche), "%s", nom);
			strip_mods = m & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
		}
		fclose(f);
	}
}

static void
strip_ecrire(void)
{
	const char *h = getenv("HOME");
	char c[512];
	FILE *f;

	system("mkdir -p \"$HOME/.config\"");
	snprintf(c, sizeof(c), "%s/.config/platine-strip", h != NULL ? h : "/tmp");
	if ((f = fopen(c, "w")) != NULL) {
		if (strip_tot >= 0)
			fprintf(f, "%d %d %d %d %d\n", strip_r, strip_n, strip_y, strip_c, strip_tot);
		else
			fprintf(f, "%d %d %d %d\n", strip_r, strip_n, strip_y, strip_c);
		fprintf(f, "touche %s %u\n", strip_touche, strip_mods);
		fclose(f);
	}
	/* rechargement à chaud : le Control Strip lit le fichier sur SIGUSR1 ; il
	 * a laissé son pid, seul moyen sûr de le viser (même nom de processus que
	 * ce tableau dans l'arborescence) */
	snprintf(c, sizeof(c), "%s/.cache/platine-strip.pid", h != NULL ? h : "/tmp");
	if ((f = fopen(c, "r")) != NULL) {
		long pid = 0;

		if (fscanf(f, "%ld", &pid) == 1 && pid > 1 && pid != (long)getpid())
			kill((pid_t)pid, SIGUSR1);
		fclose(f);
	}
}

static void
strip_afficher_touche(void)
{
	char t[256], m[96] = "";

	if (strip_mods & ControlMask)
		strncat(m, "contrôle + ", sizeof(m) - strlen(m) - 1);
	if (strip_mods & Mod1Mask)
		strncat(m, "commande + ", sizeof(m) - strlen(m) - 1);
	if (strip_mods & Mod4Mask)
		strncat(m, "super + ", sizeof(m) - strlen(m) - 1);
	if (strip_mods & ShiftMask)
		strncat(m, "majuscule + ", sizeof(m) - strlen(m) - 1);
	snprintf(t, sizeof(t), "%s%s", m, strip_touche);
	ptk_control_set_text(strip_libelle, t);
	ptk_window_invalidate(fen);
}

static void
a_strip_touche(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	strip_capture = 1;
	strip_bouton->enabled = 0;
	ptk_control_set_text(strip_libelle, "Appuyez sur la combinaison… (Échap pour annuler)");
	ptk_window_invalidate(fen);
}

static void
evenement_strip(PtkWindow *w, PtkEvent *e, void *u)
{
	const char *nom;

	(void)w;
	(void)u;
	if (!strip_capture || e->type != PTK_EV_KEY)
		return;
	if (e->keysym == XK_Escape) {
		strip_capture = 0;
		strip_bouton->enabled = 1;
		strip_afficher_touche();
		return;
	}
	if (e->keysym == XK_Shift_L || e->keysym == XK_Shift_R ||
	    e->keysym == XK_Control_L || e->keysym == XK_Control_R ||
	    e->keysym == XK_Alt_L || e->keysym == XK_Alt_R ||
	    e->keysym == XK_Super_L || e->keysym == XK_Super_R)
		return;
	nom = XKeysymToString(e->keysym);
	if (nom == NULL)
		return;
	snprintf(strip_touche, sizeof(strip_touche), "%s", nom);
	strip_mods = e->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
	strip_capture = 0;
	strip_bouton->enabled = 1;
	strip_ecrire();
	strip_afficher_touche();
}

static void
a_strip_afficher(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	strip_lire();
	strip_r = 0;
	strip_ecrire();
}

static void
a_strip_masquer(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	strip_lire();
	strip_r = 1;
	strip_ecrire();
}

static void
a_strip_position(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	strip_lire();
	strip_c = c->value;
	strip_ecrire();
}

static void
a_strip_modules(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	strip_lire();
	strip_n = c->value + 1;
	strip_ecrire();
}

static void
tab_controle(void)
{
	PtkControl *r1, *r2, *p, *m, *f;

	strip_lire();
	ctl(PTK_GROUPE, 10, 10, 340, 91, "Afficher/masquer", -1);
	r1 = ctl(PTK_RADIO, 22, 28, 300, 16, "Afficher", -1);
	r2 = ctl(PTK_RADIO, 22, 48, 300, 16, "Masquer", -1);
	r1->checked = !strip_r;
	r2->checked = strip_r;
	ptk_control_set_action(r1, a_strip_afficher, NULL);
	ptk_control_set_action(r2, a_strip_masquer, NULL);
	strip_libelle = ctl(PTK_STATIC, 22, 68, 300, 14, "", -1);
	strip_libelle->police = app.ptk.font_small;
	strip_afficher_touche();
	f = ctl(PTK_BUTTON, 214, 78, 126, 20, "Définir la touche…", -1);
	strip_bouton = f;
	ptk_control_set_action(f, a_strip_touche, NULL);
	ptk_window_set_event(fen, evenement_strip, NULL);

	ctl(PTK_GROUPE, 10, 109, 340, 58, "Réglages de la police", -1);
	texte(&app.ptk.font_menu, 22, 126, -1, 0, "Police :");
	p = ctl(PTK_POPUP, 78, 116, 110, 20, "", -1);
	ptk_control_set_items(p, "Geneva");
	ptk_control_set_text(p, "Geneva");
	p->enabled = 0;
	texte(&app.ptk.font_menu, 222, 126, -1, 0, "Taille :");
	m = ctl(PTK_POPUP, 276, 116, 56, 20, "", -1);
	ptk_control_set_items(m, "10");
	ptk_control_set_text(m, "10");
	m->enabled = 0;
	texte(&app.ptk.font_small, 22, 148, -1, 0, "La Control Strip utilise ses polices bitmap d'origine.");

	texte(&app.ptk.font_small, 22, 184, -1, 0,
	    "La barre se déplace et se redimensionne en glissant son onglet.");
	texte(&app.ptk.font_small, 22, 202, -1, 0,
	    "Les réglages sont appliqués immédiatement.");
}

/* --- Apparence --- */
static char motifs[64][40];
static int peindre_motif;
static int n_motifs;
static PtkControl *c_motifs;

static void
a_varie(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire(c->label, c->text);
}

static void
a_definir_bureau(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	if (c_motifs->value >= 0 && c_motifs->value < n_motifs) {
		ecrire("motif_bureau", motifs[c_motifs->value]);
		lancer("xprop -root -f _PLATINE_MOTIF 8s -set _PLATINE_MOTIF '%s'", motifs[c_motifs->value]);
	}
}

static void
a_option(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	ecrire_int(c->items, c->checked);
}

static void
apres_onglet(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
}

static void
popup_regle(int x, int y, int l, const char *cle, const char *items, const char *def, int page)
{
	PtkControl *c = ctl(PTK_POPUP, x, y, l, 19, cle, page);
	char v[64];

	lire(cle, v, sizeof(v), def);
	ptk_control_set_items(c, items);
	ptk_control_set_text(c, v);
	ptk_control_set_action(c, a_varie, NULL);
}

static void
tab_apparence(void)
{
	DIR *d = opendir("/opt/platine/assets/motifs");
	struct dirent *e;
	char items[2000] = "", cour[40];
	int i, j, sel = 0;
	PtkControl *c;

	onglets = ctl(PTK_ONGLETS, -1, 10, 466, 301, "", -1);
	ptk_control_set_items(onglets, "Thèmes|Apparence|Polices|Bureau|Son|Options");
	ptk_control_set_action(onglets, apres_onglet, NULL);
	onglets->value = 1;
	decal_x = 1;
	decal_y = -3;

	/* Thèmes */
	image("tableaux/themes.ppm", 24, 52, 0);
	texte(&app.ptk.font_menu, 25, 244, 0, 0, "Thème actuel : Mac OS par défaut");
	texte(&app.ptk.font_small, 25, 262, 0, 0, "Pour créer un thème, modifiez les réglages des autres onglets.");

	/* Apparence */
	texte(&app.ptk.font_menu, 42, 69, 1, 0, "Apparence :");
	popup_regle(121, 56, 180, "apparence", "Apple platine", "Apple platine", 1);
	texte(&app.ptk.font_small, 121, 86, 1, 0, "pour l'aspect des menus, icônes, fenêtres et contrôles");
	texte(&app.ptk.font_menu, 20, 110, 1, 0, "Surbrillance :");
	popup_regle(121, 97, 180, "surbrillance", "Pourpre|Lavande|Bleu|Vert|Rose|Orange|Or|Gris", "Pourpre", 1);
	texte(&app.ptk.font_small, 121, 127, 1, 0, "pour le texte sélectionné");
	texte(&app.ptk.font_menu, 44, 151, 1, 0, "Variante :");
	popup_regle(121, 138, 180, "variante", "Lavande|Azul|Bondi|Cuivre|Cramoisi|Émeraude|Bleu de France|Or|Lierre|Prune|Rose|Saphir|Argent|Sarcelle|Turquoise", "Lavande", 1);
	texte(&app.ptk.font_small, 121, 168, 1, 0, "pour les menus et les contrôles");

	/* Polices */
	texte(&app.ptk.font_menu, 40, 69, 2, 0, "Grande police système :");
	popup_regle(214, 56, 150, "police_grande", "Charcoal|Chicago|Geneva", "Charcoal", 2);
	texte(&app.ptk.font_small, 214, 86, 2, 0, "pour les menus et les titres");
	texte(&app.ptk.font_menu, 40, 115, 2, 0, "Petite police système :");
	popup_regle(214, 102, 150, "police_petite", "Geneva|Charcoal|Monaco", "Geneva", 2);
	texte(&app.ptk.font_small, 214, 132, 2, 0, "pour les textes explicatifs et les libellés");
	texte(&app.ptk.font_menu, 40, 161, 2, 0, "Police des présentations :");
	popup_regle(214, 148, 150, "police_vues", "Geneva|Monaco", "Geneva", 2);
	texte(&app.ptk.font_small, 214, 178, 2, 0, "pour les listes et les icônes");
	ctl(PTK_GROUPE, 25, 199, 410, 1, "", 2);
	c = ctl(PTK_CHECKBOX, 46, 215, 300, 14, "Lisser toutes les polices à l'écran", 2);
	ptk_control_set_items(c, "lissage");
	c->checked = lire_int("lissage", 0);
	ptk_control_set_action(c, a_option, NULL);

	/* Bureau */
	while (d != NULL && (e = readdir(d)) != NULL && n_motifs < 64) {
		char *pt = strrchr(e->d_name, '.');

		if (pt == NULL || strcmp(pt, ".ppm") != 0)
			continue;
		*pt = '\0';
		snprintf(motifs[n_motifs++], sizeof(motifs[0]), "%s", e->d_name);
	}
	if (d != NULL)
		closedir(d);
	for (i = 0; i < n_motifs; i++)
		for (j = i + 1; j < n_motifs; j++)
			if (strcmp(motifs[i], motifs[j]) > 0) {
				char t[40];

				memcpy(t, motifs[i], 40);
				memcpy(motifs[i], motifs[j], 40);
				memcpy(motifs[j], t, 40);
			}
	/* « Mac OS » : le motif d'origine, en tête de liste */
	if (n_motifs < 64) {
		memmove(motifs[1], motifs[0], (size_t)n_motifs * sizeof(motifs[0]));
		snprintf(motifs[0], sizeof(motifs[0]), "Mac OS");
		n_motifs++;
	}
	lire("motif_bureau", cour, sizeof(cour), "Mac OS");
	for (i = 0; i < n_motifs; i++) {
		char u[48], *q;

		snprintf(u, sizeof(u), "%s%s", i ? "|" : "", motifs[i]);
		for (q = u; *q != '\0'; q++)
			if (*q == '-')
				*q = ' ';
		q = u + (i ? 1 : 0);
		if (*q >= 'a' && *q <= 'z')
			*q = (char)(*q - 'a' + 'A');
		strncat(items, u, sizeof(items) - strlen(items) - 1);
		if (strcmp(motifs[i], cour) == 0)
			sel = i;
	}
	texte(&app.ptk.font_menu, 240, 62, 3, 0, "Motifs :");
	c_motifs = c = ctl(PTK_LISTE, 240, 67, 195, 132, "", 3);
	ptk_control_set_items(c, items);
	c->value = sel;
	c->defil = sel > 3 ? sel - 3 : 0;
	peindre_motif = 1;
	c = ctl(PTK_BUTTON, 345, 282, 100, 21, "Définir", 3);
	ptk_control_set_action(c, a_definir_bureau, NULL);

	/* Son */
	texte(&app.ptk.font_menu, 26, 69, 4, 0, "Thème sonore :");
	popup_regle(129, 56, 171, "theme_sonore", "Platine|Aucun", "Platine", 4);
	texte(&app.ptk.font_small, 46, 106, 4, 0, "Jouer les effets sonores pour :");
	{
		static const char *lib[4] = { "Menus (ouverture, choix)", "Fenêtres (déplacement, taille)",
		    "Contrôles (boutons, cases, flèches)", "Finder (clic, glisser-déposer)" };
		static const char *cle[4] = { "son_menus", "son_fenetres", "son_controles", "son_finder" };

		for (i = 0; i < 4; i++) {
			c = ctl(PTK_CHECKBOX, 46, 116 + i * 23, 320, 14, lib[i], 4);
			ptk_control_set_items(c, cle[i]);
			c->checked = lire_int(cle[i], 1);
			ptk_control_set_action(c, a_option, NULL);
		}
	}

	/* Options */
	c = ctl(PTK_CHECKBOX, 46, 59, 300, 14, "Défilement intelligent", 5);
	ptk_control_set_items(c, "defilement_intelligent");
	c->checked = lire_int("defilement_intelligent", 1);
	ptk_control_set_action(c, a_option, NULL);
	texte(&app.ptk.font_small, 66, 88, 5, 0, "Place les flèches en bas et à droite des barres, et ajuste le pouce au contenu.");
	c = ctl(PTK_CHECKBOX, 46, 112, 340, 14, "Double-clic sur le titre pour replier les fenêtres", 5);
	ptk_control_set_items(c, "double_clic_replie");
	c->checked = lire_int("double_clic_replie", 0);
	ptk_control_set_action(c, a_option, NULL);
}

/* --- Date et heure --- */
static PtkControl *c_date, *c_heure;
static int t_heure_jour, t_heure_actuelle, t_etat_date;

static void
date_afficher(void)
{
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);

	strftime(textes[t_heure_jour].t, sizeof(textes[0].t), "%A %d %B %Y", tm);
	strftime(textes[t_heure_actuelle].t, sizeof(textes[0].t), "%H:%M:%S (%Z)", tm);
}

static void
date_etat(const char *m)
{
	snprintf(textes[t_etat_date].t, sizeof(textes[0].t), "%s", m);
	date_afficher();
	ptk_window_invalidate(fen);
}

static int
privilege(const char *action, const char *arg)
{
	char c[512];

	snprintf(c, sizeof(c), SYS_PRIVILEGE " %s %s >/dev/null 2>&1", action, arg);
	return system(c) == 0;
}

static void
a_regler_date(PtkWindow *w, PtkControl *c, void *u)
{
	char digits[16] = "";
	size_t n = 0;
	const char *p;

	(void)w;
	(void)c;
	(void)u;
	for (p = c_date->text; *p != '\0' && n < 12; p++)
		if (isdigit((unsigned char)*p))
			digits[n++] = *p;
	for (p = c_heure->text; *p != '\0' && n < 12; p++)
		if (isdigit((unsigned char)*p))
			digits[n++] = *p;
	digits[n] = '\0';
	if (n != 12) {
		ptk_son(&app.ptk, "lerr");
		date_etat("Date ou heure incomplète (AAAA-MM-JJ et HH:MM).");
		return;
	}
	if (privilege("date", digits))
		date_etat("Heure réglée ; l'horloge matérielle a été mise à jour.");
	else {
		ptk_son(&app.ptk, "lerr");
		date_etat("Échec du réglage de l'heure (droits root ?).");
	}
}

static void
a_heure_reseau(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	date_etat("Interrogation du serveur de temps…");
	XFlush(app.ptk.dpy);
	if (privilege("ntp", "")) {
		time_t now = time(NULL);
		char d[32], h[32];

		strftime(d, sizeof(d), "%Y-%m-%d", localtime(&now));
		strftime(h, sizeof(h), "%H:%M", localtime(&now));
		ptk_control_set_text(c_date, d);
		ptk_control_set_text(c_heure, h);
		date_etat("Heure prise sur le réseau ; horloge matérielle à jour.");
	} else {
		ptk_son(&app.ptk, "lerr");
		date_etat("Serveur de temps injoignable (réseau ?).");
	}
}

static void
a_fuseau(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)u;
	if (!ptk_fuseau_valide(c->text)) {
		ptk_son(&app.ptk, "lerr");
		return;
	}
	ecrire("fuseau", c->text);
	/* la session et ses applications suivent la clé ; /etc/localtime sert la console et les journaux */
	privilege("fuseau", c->text);
	ptk_fuseau_suivre();
	date_afficher();
	ptk_window_invalidate(fen);
}

static void
tab_date(void)
{
	time_t now;
	struct tm *tm;
	char d[32], h[32], z[64];
	PtkControl *c;

	ptk_fuseau_suivre();
	now = time(NULL);
	tm = localtime(&now);
	strftime(d, sizeof(d), "%Y-%m-%d", tm);
	strftime(h, sizeof(h), "%H:%M", tm);
	onglets = ctl(PTK_ONGLETS, -1, 10, 436, 280, "", -1);
	ptk_control_set_items(onglets, "Date et heure|Fuseau|Horloge");
	onglets->value = 0;
	decal_x = decal_y = 0;

	/* Date et heure */
	ctl(PTK_GROUPE, 16, 40, 408, 200, "Date et heure", 0);
	texte(&app.ptk.font_menu, 24, 72, 0, 0, "Date actuelle :");
	t_heure_jour = n_textes;
	texte(&app.ptk.font_small, 150, 72, 0, 0, "");
	texte(&app.ptk.font_menu, 24, 96, 0, 0, "Heure actuelle :");
	t_heure_actuelle = n_textes;
	texte(&app.ptk.font_small, 150, 96, 0, 0, "");
	texte(&app.ptk.font_menu, 24, 134, 0, 0, "Nouvelle date :");
	c_date = ctl(PTK_TEXTFIELD, 150, 122, 150, 22, "", 0);
	ptk_control_set_text(c_date, d);
	texte(&app.ptk.font_small, 308, 137, 0, 0, "AAAA-MM-JJ");
	texte(&app.ptk.font_menu, 24, 168, 0, 0, "Nouvelle heure :");
	c_heure = ctl(PTK_TEXTFIELD, 150, 156, 90, 22, "", 0);
	ptk_control_set_text(c_heure, h);
	texte(&app.ptk.font_small, 248, 171, 0, 0, "HH:MM");
	c = ctl(PTK_BUTTON, 150, 192, 80, 22, "Régler", 0);
	ptk_control_set_action(c, a_regler_date, NULL);
	c = ctl(PTK_BUTTON, 240, 192, 170, 22, "Heure du réseau", 0);
	ptk_control_set_action(c, a_heure_reseau, NULL);
	t_etat_date = n_textes;
	texte(&app.ptk.font_small, 24, 230, 0, 0, "L'horloge matérielle est tenue en temps universel.");
	date_afficher();

	/* Fuseau */
	ctl(PTK_GROUPE, 16, 40, 408, 120, "Fuseau", 1);
	texte(&app.ptk.font_menu, 24, 74, 1, 0, "Fuseau horaire :");
	lire("fuseau", z, sizeof(z), getenv("TZ") != NULL ? getenv("TZ") : "Europe/Paris");
	c = ctl(PTK_POPUP, 150, 62, 210, 19, "fuseau", 1);
	ptk_control_set_items(c, "Europe/Paris|Europe/Brussels|Europe/Zurich|Europe/London|"
	    "Europe/Madrid|Europe/Berlin|UTC|America/Montreal|America/New_York|"
	    "America/Los_Angeles|Asia/Tokyo|Australia/Sydney");
	ptk_control_set_text(c, z);
	ptk_control_set_action(c, a_fuseau, NULL);
	texte(&app.ptk.font_small, 24, 100, 1, 0, "Le changement s'applique immédiatement à l'horloge");
	texte(&app.ptk.font_small, 24, 116, 1, 0, "de la barre des menus et aux dates du Finder.");

	/* Horloge */
	ctl(PTK_GROUPE, 16, 40, 408, 145, "Horloge", 2);
	texte(&app.ptk.font_menu, 24, 74, 2, 0, "Horloge de la barre de menus :");
	c = ctl(PTK_CHECKBOX, 24, 96, 320, 14, "Format 24 heures", 2);
	ptk_control_set_items(c, "horloge_24h");
	c->checked = lire_int("horloge_24h", 1);
	ptk_control_set_action(c, a_option, NULL);
	c = ctl(PTK_CHECKBOX, 24, 122, 320, 14, "Afficher les secondes", 2);
	ptk_control_set_items(c, "horloge_secondes");
	c->checked = lire_int("horloge_secondes", 0);
	ptk_control_set_action(c, a_option, NULL);
	texte(&app.ptk.font_small, 44, 156, 2, 0, "Le changement est repris au prochain rafraîchissement de l'horloge.");
}

/* --- Économiseur d'énergie --- */
static const int minutes[] = { 0, 1, 2, 5, 10, 15, 30, 60 };
static const char *minutes_lib =
    "Jamais|1 minute|2 minutes|5 minutes|10 minutes|15 minutes|30 minutes|1 heure";

static int
index_minutes(int m)
{
	int i;

	for (i = 0; i < (int)(sizeof(minutes) / sizeof(minutes[0])); i++)
		if (minutes[i] == m)
			return i;
	return 5;
}

static void
appliquer_ecran(int m)
{
	char c[128];

	if (m <= 0 || m >= 60)
		snprintf(c, sizeof(c), "xset s off -dpms");
	else
		snprintf(c, sizeof(c), "xset s off dpms %d %d %d",
		    m * 60, m * 60, m * 60);
	if (system(c) != 0)
		return;
}

static void
a_ecran(PtkWindow *w, PtkControl *c, void *u)
{
	int m = minutes[c->value];

	(void)w;
	(void)u;
	ecrire_int("ecran_minutes", m);
	appliquer_ecran(m);
}

/* Couvercle et bouton d'alimentation : /etc/platine-energie, lu par les scripts powerd */
static const char *couv_val[] = { "ecran", "veille", "rien" };
static const char *alim_val[] = { "demander", "eteindre", "veille", "rien" };
static int t_etat_energie;

static int
energie_index(const char *cle, const char **vals, int n, int def)
{
	FILE *f = fopen("/etc/platine-energie", "r");
	char k[32], v[32];
	int i, r = def;

	if (f == NULL)
		return def;
	while (fscanf(f, "%31s %31s", k, v) == 2)
		if (strcmp(k, cle) == 0)
			for (i = 0; i < n; i++)
				if (strcmp(v, vals[i]) == 0)
					r = i;
	fclose(f);
	return r;
}

static void
a_energie(PtkWindow *w, PtkControl *c, void *u)
{
	const char *cle = u;
	int couv = strcmp(cle, "couvercle") == 0;
	const char **vals = couv ? couv_val : alim_val;
	char arg[64];

	(void)w;
	if (c->value < 0 || c->value > (couv ? 2 : 3))
		return;
	snprintf(arg, sizeof(arg), "%s %s", cle, vals[c->value]);
	if (privilege("energie", arg))
		snprintf(textes[t_etat_energie].t, sizeof(textes[0].t), "Réglage enregistré.");
	else {
		ptk_son(&app.ptk, "lerr");
		snprintf(textes[t_etat_energie].t, sizeof(textes[0].t),
		    "Échec : droits root indisponibles (platine-privilege).");
	}
	ptk_window_invalidate(fen);
}

static void
a_dormir(PtkWindow *w, PtkControl *c, void *u)
{
	(void)w;
	(void)c;
	(void)u;
	lancer(SYS_PRIVILEGE " veille%s", "");
}

static PtkControl *
popup_choix(int x, int y, int l, const char *items, int val, int page)
{
	PtkControl *c = ctl(PTK_POPUP, x, y, l, 20, "", page);
	char tmp[256], *p, *e;
	int i = 0;

	ptk_control_set_items(c, items);
	c->value = val;
	snprintf(tmp, sizeof(tmp), "%s", items);
	for (p = tmp; p != NULL; p = e != NULL ? e + 1 : NULL, i++) {
		e = strchr(p, '|');
		if (e != NULL)
			*e = '\0';
		if (i == val) {
			ptk_control_set_text(c, p);
			break;
		}
	}
	return c;
}

static void
tab_economiseur(void)
{
	PtkControl *c;
	int e = lire_int("ecran_minutes", 5);

	onglets = ctl(PTK_ONGLETS, -1, 10, 396, 280, "", -1);
	ptk_control_set_items(onglets, "Veille|Options");
	onglets->value = 0;
	decal_x = 0;
	decal_y = 30;

	/* Veille */
	ctl(PTK_GROUPE, 12, 10, 374, 100, "Veille de l'écran", 0);
	texte(&app.ptk.font_menu, 24, 30, 0, 0, "Mettre l'écran en veille après :");
	c = popup_choix(266, 20, 110, minutes_lib, index_minutes(e), 0);
	ptk_control_set_action(c, a_ecran, NULL);
	texte(&app.ptk.font_small, 24, 58, 0, 0, "Extinction de l'écran (DPMS), appliquée immédiatement.");
	texte(&app.ptk.font_small, 24, 76, 0, 0, "Un mouvement de souris ou une touche le rallume.");

	ctl(PTK_GROUPE, 12, 118, 374, 96, "Suspension de l'ordinateur", 0);
	texte(&app.ptk.font_small, 24, 140, 0, 0, "Suspension ACPI (S3) : la mémoire reste alimentée.");
	texte(&app.ptk.font_small, 24, 158, 0, 0, "Aussi dans le menu Spécial du Finder.");
	c = ctl(PTK_BUTTON, 236, 178, 140, 22, "Suspendre", 0);
	ptk_control_set_action(c, a_dormir, NULL);

	/* Options */
	ctl(PTK_GROUPE, 12, 10, 374, 150, "Matériel", 1);
	texte(&app.ptk.font_menu, 24, 36, 1, 0, "Quand je ferme le couvercle :");
	c = popup_choix(226, 24, 150, "Éteindre l'écran|Suspendre|Ne rien faire",
	    energie_index("couvercle", couv_val, 3, 0), 1);
	ptk_control_set_action(c, a_energie, "couvercle");
	texte(&app.ptk.font_menu, 24, 72, 1, 0, "Bouton d'alimentation :");
	c = popup_choix(226, 60, 150, "Demander|Éteindre|Suspendre|Ne rien faire",
	    energie_index("alimentation", alim_val, 4, 0), 1);
	ptk_control_set_action(c, a_energie, "alimentation");
	texte(&app.ptk.font_small, 24, 104, 1, 0, "Appliqué immédiatement par powerd ;");
	texte(&app.ptk.font_small, 24, 120, 1, 0, "rouvrir le couvercle rallume toujours l'écran.");
	t_etat_energie = n_textes;
	texte(&app.ptk.font_small, 24, 142, 1, 0, "");
}

/* Textes et images d'onglet : posés après le panneau, sur une pseudo-commande de la Toolbox */
static void
apres_controles(PtkWindow *w)
{
	Ptk *p = w->p;
	int i, page = onglets != NULL ? onglets->value : -1;

	for (i = 0; i < n_images; i++)
		if (images[i].page >= 0 && images[i].page == page)
			XCopyArea(p->dpy, images[i].p, w->surface, p->gc, 0, 0, (unsigned)images[i].w, (unsigned)images[i].h,
			    images[i].x, images[i].y);
	/* Bureau : aperçu du motif sélectionné, cadre noir de 190×140 */
	if (peindre_motif && page == 3 && c_motifs != NULL) {
		char nom[80];
		int tw, th, x, y;
		Pixmap t;

		if (c_motifs->value <= 0)
			snprintf(nom, sizeof(nom), "fond.ppm");
		else
			snprintf(nom, sizeof(nom), "motifs/%s.ppm", motifs[c_motifs->value]);
		t = ptk_load_ppm(p, nom, &tw, &th);
		ptk_rect(p, w->surface, 26 + decal_x, 57 + decal_y, 192, 142, p->black);
		if (t != None) {
			ptk_clip(p, 27 + decal_x, 58 + decal_y, 190, 140);
			XSetClipRectangles(p->dpy, p->gc, 0, 0, &p->clip, 1, Unsorted);
			for (y = 58 + decal_y; y < 198 + decal_y; y += th)
				for (x = 27 + decal_x; x < 217 + decal_x; x += tw)
					XCopyArea(p->dpy, t, w->surface, p->gc, 0, 0, (unsigned)tw, (unsigned)th, x, y);
			XSetClipMask(p->dpy, p->gc, None);
			ptk_unclip(p);
			XFreePixmap(p->dpy, t);
		}
	}
	for (i = 0; i < n_textes; i++)
		if (textes[i].page >= 0 && textes[i].page == page) {
			XftFont *f = *textes[i].f;
			int x = textes[i].centre ? textes[i].x - ptk_text_width(p, f, textes[i].t) / 2 : textes[i].x;

			ptk_text(p, w->surface, f, x, textes[i].y, textes[i].t, p->text);
		}
}

static void
commande(PtkWindow *w, const char *c, void *u)
{
	(void)w;
	(void)u;
	if (strncmp(c, "Fermer", 6) == 0 || strncmp(c, "Quitter", 7) == 0)
		ptk_app_quit(&app);
}

int
main(int argc, char **argv)
{
	static const struct { const char *cle, *titre, *icone; int w, h; void (*f)(void); } t[] = {
		{ "apparence", "Apparence", "tableau-apparence", 464, 310, tab_apparence },
		{ "clavier", "Clavier", "tableau-clavier", 260, 298, tab_clavier },
		{ "diction", "Diction", "tableau-diction", 408, 221, tab_diction },
		{ "ecran", "Écran", "tableau-moniteurs", 494, 238, tab_ecran },
		{ "son", "Son", "tableau-son", 400, 347, tab_son },
		{ "souris", "Souris", "tableau-souris", 192, 186, tab_souris },
		{ "controle", "Control Strip", "tableau-control-strip", 360, 230, tab_controle },
		{ "date", "Date et heure", "tableau-date", 440, 300, tab_date },
		{ "economiseur", "Économiseur d'énergie", "tableau-economiseur", 420, 320, tab_economiseur },
	};
	size_t i;
	const char *nom = argc > 1 ? argv[1] : "";

	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");	/* « 0.50 » pour xinput, pas « 0,50 » */
	/* sans argument, le nom du fichier choisit le panneau : chaque élément du
	 * dossier Tableaux de bord est ce même programme sous le nom du tableau
	 * (« Apparence », « Date et heure »…), comme un tableau de bord OS 9 */
	if (argc < 2) {
		const char *base = strrchr(argv[0], '/');

		base = base != NULL ? base + 1 : argv[0];
		for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
			if (strcmp(t[i].titre, base) == 0)
				nom = t[i].cle;
	}
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (strcmp(t[i].cle, nom) == 0)
			break;
	if (i == sizeof(t) / sizeof(t[0])) {
		fprintf(stderr, "usage: platine-tableau apparence|clavier|diction|ecran|son|souris|controle|date|economiseur\n");
		return 2;
	}
	if (ptk_app_init(&app, t[i].titre, t[i].icone) != 0)
		return 1;
	gras9 = ptk_police_bitmap(&app.ptk, "geneva-10-gras.bdf", 10);
	if (gras9 == NULL)
		gras9 = app.ptk.font_small;
	fen = ptk_window_new(&app, t[i].titre, (800 - t[i].w) / 2, 40, t[i].w, t[i].h);
	if (fen == NULL)
		return 1;
	ptk_window_set_icon(fen, t[i].icone);
	{
		XSizeHints h;

		memset(&h, 0, sizeof(h));
		h.flags = PPosition | PMinSize | PMaxSize;
		h.x = (800 - t[i].w) / 2;
		h.y = 40;
		h.min_width = h.max_width = t[i].w;
		h.min_height = h.max_height = t[i].h;
		XSetWMNormalHints(app.dpy, fen->win, &h);
	}
	ptk_window_set_menu(fen, "Fichier|Fermer\t⌘W|Quitter\t⌘Q\nÉdition|~Annuler\t⌘Z|-|~Couper\t⌘X|~Copier\t⌘C|~Coller\t⌘V\nAide|Aide Platine");
	ptk_window_set_draw(fen, dessin, NULL);
	ptk_window_set_command(fen, commande, NULL);
	t[i].f();
	if (onglets != NULL)
		ptk_onglets_afficher(fen, onglets);
	fen->draw_apres = apres_controles;
	ptk_window_show(fen);
	ptk_app_run(&app);
	return 0;
}
