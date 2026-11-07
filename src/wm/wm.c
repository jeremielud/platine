/*
 * platine-wm — gestionnaire de fenêtres et barre de menus de Platine.
 *
 * Premier incrément (Phase 1) : cadres Platinum, focus, déplacement,
 * redimensionnement, zoom, repli, barre de menus globale avec menus
 * déroulants, horloge et sélecteur d'application.
 *
 * Ce n'est pas encore un WM complet ICCCM/EWMH : c'est une base propre,
 * testable, destinée à être étendue.
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>
#include <X11/Xft/Xft.h>

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <X11/extensions/shape.h>

#include "../../toolbox/ptk.h"
#include "../../toolbox/ptk_sys.h"
#include "motifs.h"

#include "../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Système", "zsys", "application", PTK_VERSION, "");


#define MAX_MENUS 12
#define MAX_ITEMS 24
#define ITEM_LEN 512
#define MENU_ITEM_H 16
#define MENU_SEP_H 6
#define APP_MAX_W 220

typedef struct Client Client;
struct Client {
	Window win;
	Window frame;
	Window grow;
	int x, y;          /* position du cadre */
	int w, h;          /* taille du client */
	int bw;            /* cadre Platinum : 6 px à gauche, à droite et en bas */
	int title_h;
	int min_w, min_h, max_w, max_h;	/* tailles demandées par l'application (0 : libre) */
	int shaded;
	int icone_pressee;
	int style_calc;	/* cadre de la Calculette : barre noire, coins arrondis, case de fermeture seule */
	int style_note;	/* cadre d'un pense-bête : bord de 1 px, barre de 12 px à la couleur de la note */
	char note_couleur[16];
	int maxed;
	int mapped;
	int active;
	int take_focus;
	int ignore_unmap;
	int old_x, old_y, old_w, old_h;
	char name[256];
	char app[128];
	char icon[32];
	char *raw_menu;
	Client *next;
};

typedef struct {
	int count;
	char titles[MAX_MENUS][64];
	int n_items[MAX_MENUS];
	char items[MAX_MENUS][MAX_ITEMS][ITEM_LEN];
} MenuModel;

typedef struct {
	Window win;
	int x, y, w, h;
	int count;
	char items[MAX_ITEMS][ITEM_LEN];
	int selected;
	int flash;
	int ignore_release;
	int entre;	/* le pointeur est passé sur un élément depuis l'ouverture */
	Pixmap buf;	/* tout le menu se dessine en mémoire : pas de clignotement */
	int top;	/* 1 : rangée noire propre (contextuel, sous-menu) ; 0 : sous la barre */
	int open_rx, open_ry;
	struct timeval open_tv;
	int has_icons;
	int kind;           /* 0 = menus app, 1 = Pomme, 2 = sélecteur */
	Client *client;
	Window client_win;
	int menu_index;
} Popup;

static Display *dpy;
static int screen;
static Window root;
static int sw, sh;
static Ptk ptk;
static GC gc;
static Cursor cursor;

static Client *clients;
static Client *focused;
static Client *drag_client;
static int drag_mode; /* 1 = déplacement, 2 = redimensionnement */
static int drag_dx, drag_dy;
static int drag_x, drag_y, drag_w, drag_h;
static Time last_click_time;
static Client *last_click_client;

/*
 * Liseré pointillé de déplacement, comme Mac OS 9 : une fenêtre en forme
 * de cadre pointillé, déplacée sans redessiner la vraie fenêtre.
 */
/*
 * Contour de déplacement et d'agrandissement : pointillé blanc de 1 px dessiné en XOR
 * directement à l'écran (comme Mac OS 9 et mlvwm). Rien en dessous n'a à se
 * redessiner : c'est instantané, même sur le 701.
 */
static int drag_new_w, drag_new_h;
static GC outline_gc;
static int outline_on, outline_x, outline_y, outline_w, outline_h;
/* zone balayée par le contour : voir outline_reparer */
static int ob_x0, ob_y0, ob_x1, ob_y1;

static void
outline_borne(int x, int y, int w, int h)
{
	if (x < ob_x0) ob_x0 = x;
	if (y < ob_y0) ob_y0 = y;
	if (x + w > ob_x1) ob_x1 = x + w;
	if (y + h > ob_y1) ob_y1 = y + h;
}

static void
outline_xor(int x, int y, int w, int h)
{
	XRectangle r[4] = {
		{ (short)x, (short)y, (unsigned short)w, 1 },
		{ (short)x, (short)(y + h - 1), (unsigned short)w, 1 },
		{ (short)x, (short)(y + 1), 1, (unsigned short)(h - 2) },
		{ (short)(x + w - 1), (short)(y + 1), 1, (unsigned short)(h - 2) },
	};

	XFillRectangles(dpy, DefaultRootWindow(dpy), outline_gc, r, 4);
}

static void
outline_destroy(void)
{
	if (!outline_on)
		return;
	outline_xor(outline_x, outline_y, outline_w, outline_h);
	outline_on = 0;
	XFlush(dpy);
}

static void
outline_show(int x, int y, int w, int h)
{
	outline_destroy();
	if (w < 4 || h < 4)
		return;
	if (outline_gc == NULL) {
		static const char damier[] = { 0x01, 0x02 };
		XGCValues v;
		Pixmap st = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), damier, 2, 2);

		v.function = GXxor;
		v.foreground = WhitePixel(dpy, DefaultScreen(dpy)) ^ BlackPixel(dpy, DefaultScreen(dpy));
		v.subwindow_mode = IncludeInferiors;
		v.fill_style = FillStippled;
		v.stipple = st;
		outline_gc = XCreateGC(dpy, DefaultRootWindow(dpy),
		    GCFunction | GCForeground | GCSubwindowMode | GCFillStyle | GCStipple, &v);
	}
	outline_on = 1;
	ob_x0 = x; ob_y0 = y; ob_x1 = x + w; ob_y1 = y + h;
	outline_x = x;
	outline_y = y;
	outline_w = w;
	outline_h = h;
	outline_xor(x, y, w, h);
	XFlush(dpy);
}

static void outline_reparer(void);

/* déplace ou redimensionne le contour */
static void
outline_set(int x, int y, int w, int h)
{
	if (!outline_on)
		return;
	if (x == outline_x && y == outline_y && w == outline_w && h == outline_h)
		return;
	outline_xor(outline_x, outline_y, outline_w, outline_h);
	outline_borne(x, y, w, h);
	outline_x = x;
	outline_y = y;
	outline_w = w;
	outline_h = h;
	outline_xor(x, y, w, h);
	XFlush(dpy);
}

static Window menubar;
static Window corners[4];
static Popup popup;
static Popup parent_popup;	/* menu dont le sous-menu ouvert dépend */
static int have_parent;
static int menu_choisi;
static int son_depl_pid;
static struct timeval son_depl_t;
static int popup_open;

static Atom wm_protocols, wm_delete, wm_state, wm_take_focus;
static Atom a_utf8, a_platine_menu, a_platine_app, a_platine_command;
static Atom a_platine_style, a_platine_icon, a_platine_context, a_platine_geometry, a_platine_bureau, a_platine_chemin, a_platine_lance;
static Atom a_net_wm_name;

static int running = 1;
static time_t last_clock;

static Pixmap pix_tux, pix_tux_mask;
static Pixmap pix_app16, pix_app16_mask;

/* Icônes 16×16 des tableaux de bord et du dossier qui les contient : le WM les
 * utilise dans la barre de titre et dans le menu d'application. */
static struct MenuIcon { const char *nom; Pixmap pix, mask; } icones_app[] = {
	{ "tableau-apparence", 0, 0 }, { "tableau-clavier", 0, 0 },
	{ "tableau-diction", 0, 0 }, { "tableau-general", 0, 0 }, { "tableau-moniteurs", 0, 0 },
	{ "tableau-son", 0, 0 }, { "tableau-souris", 0, 0 },
	{ "tableau-control-strip", 0, 0 }, { "tableau-date", 0, 0 },
	{ "tableau-economiseur", 0, 0 }, { "dossier-tableaux", 0, 0 },
};
#define NICONES_APP ((int)(sizeof(icones_app) / sizeof(icones_app[0])))
static Pixmap pix_folder16, pix_folder16_mask;
static Pixmap pix_doc16, pix_doc16_mask;
static Pixmap pix_disk16, pix_disk16_mask;
static Pixmap pix_trash16, pix_trash16_mask;

static const char *generic_menu =
    "Fichier|Fermer\t⌘W|Quitter\t⌘Q\n"
    "Aide|À propos de cette application";

static const char *default_menu =
    "Fichier|Nouveau dossier\t⌘N|Ouvrir\t⌘O|-|Fermer\t⌘W|"
    "Placer dans la corbeille\t⌫\n"
    "Édition|Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V\n"
    "Présentation|Icônes|Liste|Boutons\n"
    "Aller|Ordinateur|Applications|Documents\n"
    "Fenêtre|Fermer\t⌘W|Tout fermer\n"
    "Spécial|Vider la Corbeille…|-|Suspendre|Redémarrer|Éteindre\n"
    "Aide|Aide Platine";

static void draw_menubar(void);
static void raise_corners(void);
static void draw_frame(Client *c);
static void popup_draw(void);
static void focus_client(Client *c);
static void popup_draw_row(int i);
static void split_item(const char *item, char *label, size_t lsz,
    char *sc, size_t ssz);
static void close_popup(void);
static void open_popup(Client *c, int menu_index, int kind, int x);
static void send_command(Client *c, const char *cmd);

static void
sig_handler(int sig)
{
	(void)sig;
	running = 0;
}

static int
xerror(Display *d, XErrorEvent *e)
{
	(void)d;
	if (e->error_code != BadWindow && e->error_code != BadMatch &&
	    e->error_code != BadDrawable && e->error_code != BadValue)
		fprintf(stderr, "platine-wm: erreur X %d, requête %d, ressource 0x%lx\n",
		    e->error_code, e->request_code, e->resourceid);
	return 0;
}

static int
xioerror(Display *d)
{
	(void)d;
	fprintf(stderr, "platine-wm: connexion X perdue\n");
	exit(1);
}

static char *
get_prop(Window w, Atom prop, Atom *type_out)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	char *out = NULL;

	if (XGetWindowProperty(dpy, w, prop, 0, 8192, False, AnyPropertyType,
	    &type, &format, &n, &after, &data) == Success && data != NULL) {
		if (type_out)
			*type_out = type;
		out = malloc(n + 1);
		if (out) {
			memcpy(out, data, n);
			out[n] = '\0';
		}
		XFree(data);
	}
	return out;
}

static void
set_utf8_prop(Window w, Atom prop, const char *value)
{
	XChangeProperty(dpy, w, prop, a_utf8, 8, PropModeReplace,
	    (const unsigned char *)value, (int)strlen(value));
}

static void
set_wm_state(Client *c, long state)
{
	long data[2];

	data[0] = state;
	data[1] = None;
	XChangeProperty(dpy, c->win, wm_state, wm_state, 32, PropModeReplace,
	    (unsigned char *)data, 2);
}

static void
launch_finder(const char *path)
{
	char exe[PATH_MAX], bin[PATH_MAX];
	ssize_t n;
	pid_t pid;

	/* le Finder du Dossier Système, sinon celui posé à côté du WM, sinon le PATH */
	if (ptk_dans_volume("System/Finder", bin, sizeof(bin)) &&
	    access(bin, X_OK) == 0) {
		pid = fork();
		if (pid == 0) {
			execl(bin, "Finder", path, (char *)NULL);
			_exit(1);
		}
		return;
	}
	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		char *slash;

		exe[n] = '\0';
		slash = strrchr(exe, '/');
		if (slash != NULL) {
			*slash = '\0';
			snprintf(bin, sizeof(bin), "%s/platine-finder",
			    exe);
			pid = fork();
			if (pid == 0) {
				execl(bin, "platine-finder", path,
				    (char *)NULL);
				_exit(1);
			}
			return;
		}
	}
	pid = fork();
	if (pid == 0) {
		execlp("platine-finder", "platine-finder", path,
		    (char *)NULL);
		_exit(1);
	}
}

static void
send_command_win(Window w, const char *cmd)
{
	XEvent ev;

	set_utf8_prop(w, a_platine_command, cmd);
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = a_platine_command;
	ev.xclient.format = 32;
	XSendEvent(dpy, w, False, NoEventMask, &ev);
	XFlush(dpy);
}

/* la fenêtre « Bureau » du Finder (non gérée par le WM : enfant direct de la racine) */
static Window
fenetre_bureau(void)
{
	Window r, p, *enf = NULL, trouve = None;
	unsigned int n = 0, i;

	if (!XQueryTree(dpy, root, &r, &p, &enf, &n))
		return None;
	for (i = 0; i < n && trouve == None; i++) {
		char *nom = NULL;

		if (XFetchName(dpy, enf[i], &nom) && nom != NULL) {
			if (strcmp(nom, "Bureau") == 0)
				trouve = enf[i];
			XFree(nom);
		}
	}
	if (enf)
		XFree(enf);
	return trouve;
}

static void arret_demarrer(int mode, int force);

static void
send_command(Client *c, const char *cmd)
{
	XEvent ev;

	if (c == NULL) {
		fprintf(stderr, "platine-wm: commande Pomme: %s\n", cmd);
		if (strcmp(cmd, "about") == 0) {
			char res[PATH_MAX], sh[PATH_MAX + 64];

			ptk_resoudre("@System/Extensions/À propos de cet ordinateur|platine-about",
			    res, sizeof(res));
			snprintf(sh, sizeof(sh), "%s &", res);
			system(sh);
		} else if (strcmp(cmd, "apps") == 0) {
			char apps[PATH_MAX];

			if (ptk_dans_volume("Applications", apps, sizeof(apps)))
				launch_finder(apps);
		}
		else {
			/* aucune fenêtre active : les menus sont ceux du Finder, la commande va à son
			 * bureau (Redémarrer, Éteindre, Nouveau dossier… se perdaient) */
			Window w = fenetre_bureau();

			if (w != None)
				send_command_win(w, cmd);
			else if (strcmp(cmd, "Redémarrer") == 0)
				arret_demarrer(2, 0);
			else if (strcmp(cmd, "Éteindre") == 0)
				arret_demarrer(1, 0);
		}
		return;
	}
	set_utf8_prop(c->win, a_platine_command, cmd);
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = c->win;
	ev.xclient.message_type = a_platine_command;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 0;
	XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	XFlush(dpy);
}

static void
parse_model(const char *raw, MenuModel *m)
{
	char buf[8192];
	char *line, *lsave;
	int i;

	memset(m, 0, sizeof(*m));
	if (raw == NULL || *raw == '\0')
		raw = default_menu;
	snprintf(buf, sizeof(buf), "%s", raw);

	for (line = strtok_r(buf, "\n", &lsave); line != NULL;
	    line = strtok_r(NULL, "\n", &lsave)) {
		char *p, *isave, *item;

		if (m->count >= MAX_MENUS)
			break;
		p = strchr(line, '|');
		if (p == NULL)
			continue;
		*p++ = '\0';
		snprintf(m->titles[m->count], sizeof(m->titles[0]), "%s", line);
		i = m->count;
		m->n_items[i] = 0;
		for (item = strtok_r(p, "|", &isave); item != NULL;
		    item = strtok_r(NULL, "|", &isave)) {
			if (m->n_items[i] >= MAX_ITEMS)
				break;
			snprintf(m->items[i][m->n_items[i]],
			    sizeof(m->items[i][0]), "%s", item);
			m->n_items[i]++;
		}
		m->count++;
	}
}

/* Contenu du menu Pomme : icône, libellé, commande (NULL : sous-menu).
 * Règle (docs/02-decisions.md) : toute application Platine y figure, avec son
 * lanceur dans le dossier Applications ; l'entrée ne sort que si le programme
 * est installé. */
static const struct { const char *ic, *lab, *cmd; } pomme[] = {
	{ "app-album", "Album", "gpicview ~/Images" },
	{ "app-calculette", "Calculette", "@System/Apple Menu Items/Calculette|platine-calculette" },
	{ "app-classilla", "Classilla", "netsurf-gtk3" },
	{ "dossier", "Jeux", NULL },
	{ "dossier-tableaux", "Tableaux de bord", NULL },
	{ "app-quicktime", "Lecteur QuickTime", "#TVOD|platine-quicktime" },
	{ "app-terminal", "Terminal", "xterm -ls" },
	/* par signature : trouvés où qu'ils soient rangés, comme un alias Mac OS 9 */
	{ "app-texte", "TextEdit", "#ttxt|platine-textedit" },
	{ "application", "Théière", "#thei|platine-theiere" },
};
static const struct { const char *ic, *lab, *cmd; } pomme_jeux[] = {
	{ "application", "Olympe", "platine-sdl olympe" },
	{ "application", "ScummVM", "platine-sdl scummvm" },
};
static const struct { const char *ic, *lab, *cmd; } pomme_tableaux[] = {
	{ "tableau-apparence", "Apparence", "@System/Control Panels/Apparence|platine-tableau apparence" },
	{ "tableau-clavier", "Clavier", "@System/Control Panels/Clavier|platine-tableau clavier" },
	{ "tableau-diction", "Diction", "@System/Control Panels/Diction|platine-tableau diction" },
	{ "tableau-moniteurs", "Écran", "@System/Control Panels/Écran|platine-tableau ecran" },
	{ "tableau-son", "Son", "@System/Control Panels/Son|platine-tableau son" },
	{ "tableau-souris", "Souris", "@System/Control Panels/Souris|platine-tableau souris" },
	{ "tableau-control-strip", "Control Strip", "@System/Control Panels/Control Strip|platine-tableau controle" },
	{ "tableau-date", "Date et heure", "@System/Control Panels/Date et heure|platine-tableau date" },
	{ "tableau-economiseur", "Économiseur d'énergie", "@System/Control Panels/Économiseur d'énergie|platine-tableau economiseur" },
};

static const char *
pomme_commande(const char *lab)
{
	size_t i;

	for (i = 0; i < sizeof(pomme) / sizeof(pomme[0]); i++)
		if (pomme[i].cmd != NULL && strcmp(pomme[i].lab, lab) == 0)
			return pomme[i].cmd;
	for (i = 0; i < sizeof(pomme_jeux) / sizeof(pomme_jeux[0]); i++)
		if (strcmp(pomme_jeux[i].lab, lab) == 0)
			return pomme_jeux[i].cmd;
	for (i = 0; i < sizeof(pomme_tableaux) / sizeof(pomme_tableaux[0]); i++)
		if (strcmp(pomme_tableaux[i].lab, lab) == 0)
			return pomme_tableaux[i].cmd;
	return NULL;
}

/* élément avec icône : « \x02icône\x02libellé » ; sous-menu ajouté après \x1f */
/* le programme d'une entrée du menu Pomme est-il installé ? « platine-sdl jeu » : le jeu aussi */
static int
programme_present(const char *nom, size_t lg)
{
	const char *chemin = getenv("PATH");
	char f[512];

	while (chemin != NULL && *chemin != '\0') {
		size_t d = strcspn(chemin, ":");

		snprintf(f, sizeof(f), "%.*s/%.*s", (int)d, chemin, (int)lg, nom);
		if (access(f, X_OK) == 0)
			return 1;
		chemin += d;
		if (*chemin == ':')
			chemin++;
	}
	return 0;
}

static int
commande_presente(const char *ref)
{
	char res[PATH_MAX];
	const char *cmd = ptk_resoudre(ref, res, sizeof(res));
	size_t lg = strcspn(cmd, " ");

	/* installé dans l'arborescence du volume : chemin cité, déjà vérifié */
	if (cmd[0] == '\'')
		return 1;
	if (cmd[0] == '\0')
		return 0;

	if (!programme_present(cmd, lg))
		return 0;
	if (strncmp(cmd, "platine-sdl ", 12) == 0)
		return programme_present(cmd + 12, strcspn(cmd + 12, " "));
	return 1;
}

/*
 * Menu Pomme alimenté par le dossier « System/Éléments du menu
 * Pomme », comme sous Mac OS 9 : ses éléments (programmes, documents, alias,
 * dossiers) par ordre alphabétique ; un dossier, ou un alias de dossier,
 * donne un sous-menu de son contenu. Relu à chaque ouverture du menu : ce
 * qu'on pose dans le dossier apparaît aussitôt. Sans ce dossier (ancienne
 * installation, développement), la table pomme[] ci-dessus.
 */
#define POMME_MAX 96
static struct { char lab[128]; char chemin[PATH_MAX]; } pomme_el[POMME_MAX];
static int pomme_n;

static int
pomme_dossier(char *out, size_t n)
{
	struct stat st;

	return ptk_dans_volume("System/Apple Menu Items", out, n) &&
	    stat(out, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
pomme_cmp(const void *a, const void *b)
{
	return strcoll(*(char *const *)a, *(char *const *)b);
}

/* noms triés d'un dossier, sans les fichiers cachés ; à libérer */
static int
pomme_lister(const char *dossier, char **noms, int max)
{
	DIR *d = opendir(dossier);
	struct dirent *e;
	int n = 0;

	if (d == NULL)
		return 0;
	while (n < max && (e = readdir(d)) != NULL)
		if (e->d_name[0] != '.' && (noms[n] = strdup(e->d_name)) != NULL)
			n++;
	closedir(d);
	qsort(noms, (size_t)n, sizeof(noms[0]), pomme_cmp);
	return n;
}

/* icône 16×16 de menu : celle que porte le programme (identité embarquée,
 * alias suivis), celle du tableau de bord, du dossier, ou générique */
static const char *
pomme_icone(const char *chemin, int dossier)
{
	static const struct { const char *nom, *ic; } tableaux[] = {
		{ "Apparence", "tableau-apparence" }, { "Clavier", "tableau-clavier" },
		{ "Diction", "tableau-diction" }, { "Écran", "tableau-moniteurs" },
		{ "Son", "tableau-son" }, { "Souris", "tableau-souris" },
		{ "Control Strip", "tableau-control-strip" }, { "Date et heure", "tableau-date" },
		{ "Économiseur d'énergie", "tableau-economiseur" },
	};
	static char ic[64];
	char reel[PATH_MAX], f[PATH_MAX];
	const char *base;
	PtkIdentite id;
	size_t i;

	if (realpath(chemin, reel) == NULL)
		snprintf(reel, sizeof(reel), "%s", chemin);
	base = strrchr(reel, '/');
	base = base != NULL ? base + 1 : reel;
	if (dossier)
		return strcmp(base, "Tableaux de bord") == 0 ||
		    strcmp(base, "Control Panels") == 0 ? "dossier-tableaux" : "dossier";
	if (ptk_identite_lire(reel, &id)) {
		if (id.icone[0] == '@') {
			for (i = 0; i < sizeof(tableaux) / sizeof(tableaux[0]); i++)
				if (strcmp(base, tableaux[i].nom) == 0)
					return tableaux[i].ic;
		} else if (id.icone[0] != '\0' &&
		    (size_t)snprintf(f, sizeof(f), "%s/icones/menu/%s.xpm", ptk.assets_dir,
		    id.icone) < sizeof(f) && access(f, R_OK) == 0) {
			snprintf(ic, sizeof(ic), "%s", id.icone);
			return ic;
		}
		return "application";
	}
	/* programmes du système sans identité, aux icônes connues */
	if (strcmp(base, "xterm") == 0)
		return "app-terminal";
	return access(reel, X_OK) == 0 ? "application" : "document";
}

static void
pomme_noter(const char *lab, const char *chemin)
{
	if (pomme_n >= POMME_MAX)
		return;
	snprintf(pomme_el[pomme_n].lab, sizeof(pomme_el[pomme_n].lab), "%s", lab);
	snprintf(pomme_el[pomme_n].chemin, sizeof(pomme_el[pomme_n].chemin), "%s", chemin);
	pomme_n++;
}

static const char *
pomme_chemin(const char *lab)
{
	int i;

	for (i = 0; i < pomme_n; i++)
		if (strcmp(pomme_el[i].lab, lab) == 0)
			return pomme_el[i].chemin;
	return NULL;
}

static int
apple_model_dossier(MenuModel *m, int n, const char *racine)
{
	char *noms[64];
	int k, nb = pomme_lister(racine, noms, 64);

	pomme_n = 0;
	for (k = 0; k < nb; k++) {
		char ch[PATH_MAX], *it;
		struct stat st;
		int dossier;

		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", racine, noms[k]) >= sizeof(ch) ||
		    stat(ch, &st) != 0 || n >= MAX_ITEMS) {
			free(noms[k]);
			continue;
		}
		dossier = S_ISDIR(st.st_mode);
		it = m->items[0][n++];
		snprintf(it, ITEM_LEN, "\x02%s\x02%s", pomme_icone(ch, dossier), noms[k]);
		pomme_noter(noms[k], ch);
		if (dossier) {
			/* sous-menu : le contenu du dossier, un niveau */
			char *sous[32];
			int j, ns = pomme_lister(ch, sous, 32), mis = 0;

			for (j = 0; j < ns; j++) {
				char sc[PATH_MAX], el[192];
				struct stat ss;

				if ((size_t)snprintf(sc, sizeof(sc), "%s/%s", ch, sous[j]) < sizeof(sc) &&
				    stat(sc, &ss) == 0) {
					snprintf(el, sizeof(el), "%s\x02%s\x02%s", mis ? "\x1e" : "\x1f",
					    pomme_icone(sc, S_ISDIR(ss.st_mode)), sous[j]);
					if (strlen(it) + strlen(el) < ITEM_LEN) {
						strcat(it, el);
						pomme_noter(sous[j], sc);
						mis++;
					}
				}
				free(sous[j]);
			}
		}
		free(noms[k]);
	}
	return n;
}

/* élément du menu Pomme choisi : un dossier s'ouvre dans le Finder, un
 * programme se lance, un document est confié au Finder */
static void
pomme_ouvrir(const char *chemin)
{
	struct stat st;
	char reel[PATH_MAX];

	/* un alias (lien) ouvre son original, comme sous Mac OS 9 */
	if (realpath(chemin, reel) != NULL)
		chemin = reel;
	if (stat(chemin, &st) == 0 && !S_ISDIR(st.st_mode) && access(chemin, X_OK) == 0) {
		if (fork() == 0) {
			setsid();
			execl(chemin, chemin, (char *)NULL);
			_exit(127);
		}
		return;
	}
	XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_PLATINE_OUVRIR", False),
	    XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
	    (const unsigned char *)chemin, (int)strlen(chemin));
}

static void
apple_model(MenuModel *m)
{
	size_t i, j;
	int n = 0;
	char racine[PATH_MAX];

	memset(m, 0, sizeof(*m));
	m->count = 1;
	snprintf(m->titles[0], sizeof(m->titles[0]), "Pomme");
	snprintf(m->items[0][n++], ITEM_LEN, "À propos de cet ordinateur");
	snprintf(m->items[0][n++], ITEM_LEN, "-");
	if (pomme_dossier(racine, sizeof(racine))) {
		m->n_items[0] = apple_model_dossier(m, n, racine);
		return;
	}
	pomme_n = 0;
	for (i = 0; i < sizeof(pomme) / sizeof(pomme[0]); i++) {
		char *it = m->items[0][n];
		int sous_menus = 0;

		if (pomme[i].cmd != NULL && !commande_presente(pomme[i].cmd))
			continue;
		snprintf(it, ITEM_LEN, "\x02%s\x02%s", pomme[i].ic, pomme[i].lab);
		if (pomme[i].cmd == NULL) {
			int jeux = strcmp(pomme[i].lab, "Jeux") == 0;
			size_t nb = jeux ? sizeof(pomme_jeux) / sizeof(pomme_jeux[0]) :
			    sizeof(pomme_tableaux) / sizeof(pomme_tableaux[0]);

			for (j = 0; j < nb; j++) {
				char sous[128];

				if (!commande_presente(jeux ? pomme_jeux[j].cmd : pomme_tableaux[j].cmd))
					continue;
				snprintf(sous, sizeof(sous), "%s\x02%s\x02%s", sous_menus ? "\x1e" : "\x1f",
				    jeux ? pomme_jeux[j].ic : pomme_tableaux[j].ic,
				    jeux ? pomme_jeux[j].lab : pomme_tableaux[j].lab);
				strncat(it, sous, ITEM_LEN - strlen(it) - 1);
				sous_menus++;
			}
			if (sous_menus == 0)
				continue;
		}
		n++;
	}
	m->n_items[0] = n;
}

static const char *
menu_source(Client *c)
{
	if (c == NULL) {
		/* aucune fenêtre active : le menu que le bureau du Finder publie lui-même */
		static char *bureau;
		Window w = fenetre_bureau();
		char *m = w != None ? get_prop(w, a_platine_menu, NULL) : NULL;

		if (m != NULL) {
			free(bureau);
			bureau = m;
			return bureau;
		}
		return default_menu;
	}
	if (c->raw_menu != NULL)
		return c->raw_menu;
	return generic_menu;
}

static const char *
client_app_name(Client *c)
{
	if (c == NULL)
		return "Finder";
	if (c->app[0])
		return c->app;
	if (c->name[0])
		return c->name;
	return "Application";
}

static void
update_client_names(Client *c)
{
	char *s;

	s = get_prop(c->win, a_net_wm_name, NULL);
	if (s == NULL)
		s = get_prop(c->win, XA_WM_NAME, NULL);
	if (s != NULL) {
		snprintf(c->name, sizeof(c->name), "%s", s);
		free(s);
	}
	s = get_prop(c->win, a_platine_app, NULL);
	if (s != NULL) {
		snprintf(c->app, sizeof(c->app), "%s", s);
		free(s);
	}
	s = get_prop(c->win, a_platine_icon, NULL);
	if (s != NULL) {
		snprintf(c->icon, sizeof(c->icon), "%s", s);
		free(s);
	}
	s = get_prop(c->win, a_platine_menu, NULL);
	if (s != NULL) {
		free(c->raw_menu);
		c->raw_menu = s;
	}
}

static void
raise_corners(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (corners[i] != None)
			XRaiseWindow(dpy, corners[i]);
	}
}

static void
create_corners(void)
{
	static const unsigned char tl[8] = { 0x1f, 0x07, 0x03, 0x01,
	    0x01, 0x00, 0x00, 0x00 };
	static const unsigned char tr[8] = { 0xf0, 0xe0, 0xc0, 0x80,
	    0x80, 0x00, 0x00, 0x00 };
	static const unsigned char bl[8] = { 0x01, 0x01, 0x01, 0x01,
	    0x01, 0x03, 0x07, 0xff };
	static const unsigned char br[8] = { 0x00, 0x00, 0x00, 0x80,
	    0x80, 0xc0, 0xe0, 0xf8 };
	const unsigned char *bits[4] = { tl, tr, bl, br };
	int px[4] = { 0, sw - 8, 0, sw - 8 };
	int py[4] = { 0, 0, sh - 8, sh - 8 };
	int i;

	/* les coins du haut sont dessinés sur la barre de menus */
	for (i = 2; i < 4; i++) {
		XSetWindowAttributes a;
		Pixmap mask;
		a.override_redirect = True;
		a.background_pixel = ptk.black.pixel;
		a.event_mask = 0;
		corners[i] = XCreateWindow(dpy, root, px[i], py[i], 8, 8, 0,
		    CopyFromParent, InputOutput, CopyFromParent,
		    CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
		mask = XCreateBitmapFromData(dpy, root, (const char *)bits[i],
		    8, 8);
		XShapeCombineMask(dpy, corners[i], ShapeBounding, 0, 0, mask,
		    ShapeSet);
		XShapeCombineMask(dpy, corners[i], ShapeInput, 0, 0, None,
		    ShapeSet);
		XFreePixmap(dpy, mask);
		XMapRaised(dpy, corners[i]);
	}
}

static void
draw_tux(Ptk *p, Drawable d, int x, int y)
{
	if (pix_tux != None) {
		ptk_draw_pixmap(p, d, pix_tux, pix_tux_mask, x, y, 11, 15);
		return;
	}
	ptk_fill(p, d, x + 3, y + 1, 6, 2, p->black);
	ptk_fill(p, d, x + 2, y + 3, 8, 7, p->black);
	ptk_fill(p, d, x + 4, y + 5, 4, 4, p->white);
	ptk_fill(p, d, x + 3, y + 3, 1, 1, p->white);
	ptk_fill(p, d, x + 8, y + 3, 1, 1, p->white);
	ptk_fill(p, d, x + 5, y + 4, 2, 1, p->white);
	ptk_line(p, d, x + 3, y + 10, x + 2, y + 11, p->black);
	ptk_line(p, d, x + 8, y + 10, x + 9, y + 11, p->black);
}

/* icône 16×16 d'une application par son nom (_PLATINE_ICON) : la table des
 * tableaux de bord, sinon « menu/<nom>.xpm » chargé une fois — toute
 * application qui porte une icône l'a dans sa barre de titre et la barre de menus */
static int
icone_app_nommee(const char *nom, Pixmap *pix, Pixmap *mask)
{
	static struct { char nom[48]; Pixmap pix, mask; } cache[32];
	static int n;
	char f[96];
	int i;

	for (i = 0; i < NICONES_APP; i++)
		if (strcmp(nom, icones_app[i].nom) == 0 && icones_app[i].pix != None) {
			*pix = icones_app[i].pix;
			*mask = icones_app[i].mask;
			return 1;
		}
	for (i = 0; i < n; i++)
		if (strcmp(cache[i].nom, nom) == 0) {
			*pix = cache[i].pix;
			*mask = cache[i].mask;
			return cache[i].pix != None;
		}
	if (n == (int)(sizeof(cache) / sizeof(cache[0])) || strchr(nom, '/') != NULL)
		return 0;
	snprintf(cache[n].nom, sizeof(cache[n].nom), "%s", nom);
	snprintf(f, sizeof(f), "menu/%s.xpm", nom);
	if (ptk_load_xpm(&ptk, f, &cache[n].pix, &cache[n].mask) != 0)
		cache[n].pix = cache[n].mask = None;
	*pix = cache[n].pix;
	*mask = cache[n].mask;
	n++;
	return *pix != None;
}

static void
draw_app_icon(Ptk *p, Drawable d, int x, int y, const char *kind)
{
	Pixmap pix = pix_app16, mask = pix_app16_mask;

	if (kind == NULL || *kind == '\0')
		kind = "app";
	if (strcmp(kind, "folder") == 0) {
		pix = pix_folder16;
		mask = pix_folder16_mask;
	} else if (strcmp(kind, "doc") == 0) {
		pix = pix_doc16;
		mask = pix_doc16_mask;
	} else if (strcmp(kind, "disk") == 0) {
		pix = pix_disk16;
		mask = pix_disk16_mask;
	} else if (strcmp(kind, "trash") == 0) {
		pix = pix_trash16;
		mask = pix_trash16_mask;
	} else {
		Pixmap ip, im;

		if (icone_app_nommee(kind, &ip, &im)) {
			pix = ip;
			mask = im;
		}
	}
	if (pix != None) {
		ptk_draw_pixmap(p, d, pix, mask, x, y, 16, 16);
		return;
	}
	ptk_fill(p, d, x, y, 16, 16, p->window);
	ptk_icon(p, d, x, y, 16, kind);
}

static PtkColor
gris_wm(int v)
{
	char n[8];

	snprintf(n, sizeof(n), "#%02x%02x%02x", v, v, v);
	return ptk_color(&ptk, n);
}

/* titre sélectionné : 1 rangée #6666cc, 17 de sélection, 1 de #000088 */
static void
titre_selection(int x, int w)
{
	ptk_fill(&ptk, menubar, x, 0, w, 1, ptk_color(&ptk, "#6666cc"));
	ptk_fill(&ptk, menubar, x, 1, w, 17, ptk.selection);
	ptk_fill(&ptk, menubar, x, 18, w, 1, ptk_color(&ptk, "#000088"));
}

static Pixmap bar_pix;
static struct timeval horloge_fin;
/* application en cours de lancement : son nom seul, centré dans la barre, jusqu'à sa fenêtre (relevé VM) */
static char lance_nom[64];
static time_t lance_fin;
static int horloge_date;	/* un clic sur l'horloge affiche la date, un autre l'heure */
static Pixmap pix_pomme, pix_pomme_mask;

/* positions du côté droit, de la droite vers la gauche, mesurées sur la VM */
static int
reglage_int(const char *cle, int def)
{
	const char *h = getenv("HOME");
	char c[512], l[256];
	size_t lc = strlen(cle);
	FILE *f;

	snprintf(c, sizeof(c), "%s/.config/platine/reglages", h != NULL ? h : ".");
	if ((f = fopen(c, "r")) == NULL)
		return def;
	while (fgets(l, sizeof(l), f) != NULL)
		if (strncmp(l, cle, lc) == 0 && l[lc] == ' ') {
			int v = atoi(l + lc + 1);

			fclose(f);
			return v;
		}
	fclose(f);
	return def;
}

static void
barre_mesures(char *clockbuf, size_t n, int *sep_x, int *clock_x, int *app_x, int *app_tx)
{
	time_t now = time(NULL);
	struct tm *tm;
	const char *app;
	int h24, sec;

	ptk_fuseau_suivre();
	tm = localtime(&now);
	app = client_app_name(focused);
	h24 = reglage_int("horloge_24h", 1);
	sec = reglage_int("horloge_secondes", 0);

	if (horloge_date)
		snprintf(clockbuf, n, "%d/%d/%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year % 100);
	else if (h24) {
		if (sec)
			snprintf(clockbuf, n, "%02d:%02d:%02d", tm->tm_hour,
			    tm->tm_min, tm->tm_sec);
		else
			snprintf(clockbuf, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
	} else {
		int h = tm->tm_hour % 12;

		if (h == 0)
			h = 12;
		if (sec)
			snprintf(clockbuf, n, "%d:%02d:%02d %s", h, tm->tm_min,
			    tm->tm_sec, tm->tm_hour < 12 ? "AM" : "PM");
		else
			snprintf(clockbuf, n, "%d:%02d %s", h, tm->tm_min,
			    tm->tm_hour < 12 ? "AM" : "PM");
	}
	*app_tx = sw - 18 - ptk_text_width(&ptk, ptk.font_menu_bold, app);
	*sep_x = *app_tx - 22 - 11;
	*app_x = *sep_x + 6;
	*clock_x = *sep_x - 7 - ptk_text_width(&ptk, ptk.font_menu_bold, clockbuf);
}

static void
draw_menubar(void)
{
	MenuModel m;
	char clockbuf[32];
	int i, x, app_x, app_tx, clock_x, sep_x;
	Window reel;
	int app_active = popup_open && popup.kind == 2;
	const char *app;

	if (menubar == None)
		return;
	if (bar_pix == None)
		bar_pix = XCreatePixmap(dpy, menubar, (unsigned)sw, (unsigned)ptk.menubar_h,
		    (unsigned)DefaultDepth(dpy, screen));
	reel = menubar;
	menubar = bar_pix;
	if (lance_nom[0] != '\0') {
		int tw = ptk_text_width(&ptk, ptk.font_menu_bold, lance_nom);

		ptk_fill(&ptk, menubar, 0, 0, sw, ptk.menubar_h, ptk.menu);
		ptk_fill(&ptk, menubar, 0, 0, sw, 1, ptk.white);
		ptk_fill(&ptk, menubar, 0, ptk.menubar_h - 2, sw, 1, ptk_color(&ptk, "#999999"));
		ptk_fill(&ptk, menubar, 0, ptk.menubar_h - 1, sw, 1, ptk.black);
		ptk_text(&ptk, menubar, ptk.font_menu_bold, (sw - tw) / 2,
		    (ptk.menubar_h + ptk.font_menu_bold->ascent - ptk.font_menu_bold->descent) / 2,
		    lance_nom, ptk.text);
		menubar = reel;
		XCopyArea(dpy, bar_pix, menubar, ptk.gc, 0, 0, (unsigned)sw, (unsigned)ptk.menubar_h, 0, 0);
		XFlush(dpy);
		return;
	}

	/* Le menu de l'application au premier plan, ou le Finder. */
	parse_model(menu_source(focused), &m);

	barre_mesures(clockbuf, sizeof(clockbuf), &sep_x, &clock_x, &app_x, &app_tx);
	app = client_app_name(focused);

	/* VM : blanc en haut et à gauche, #99 à droite et en bas, puis noir */
	ptk_fill(&ptk, menubar, 0, 0, sw, ptk.menubar_h, ptk.menu);
	ptk_fill(&ptk, menubar, 0, 0, sw, 1, ptk.white);
	ptk_fill(&ptk, menubar, 0, 0, 1, 19, ptk.white);
	ptk_fill(&ptk, menubar, sw - 1, 1, 1, 18, gris_wm(0x99));
	ptk_fill(&ptk, menubar, 0, 18, sw, 1, gris_wm(0x99));
	ptk_fill(&ptk, menubar, 0, 19, sw, 1, ptk.black);

	/* menu Pomme, x de 9 à 36 ; la pomme garde ses couleurs sur la sélection */
	if (popup_open && popup.kind == 1)
		titre_selection(9, 28);
	if (pix_pomme != None)
		ptk_draw_pixmap(&ptk, menubar, pix_pomme, pix_pomme_mask, 14, 1, 17, 17);
	else
		draw_tux(&ptk, menubar, 18, 2);

	x = 37;
	for (i = 0; i < m.count; i++) {
		int w = ptk_text_width(&ptk, ptk.font_menu_bold,
		    m.titles[i]) + 14;
		int baseline = (ptk.menubar_h +
		    ptk.font_menu_bold->ascent -
		    ptk.font_menu_bold->descent) / 2;

		if (x + w > clock_x - 8)
			break;
		if (popup_open && popup.kind == 0 && popup.menu_index == i) {
			titre_selection(x, w);
			ptk_text(&ptk, menubar, ptk.font_menu_bold, x + 6,
			    baseline, m.titles[i], ptk.selection_text);
		} else {
			ptk_text(&ptk, menubar, ptk.font_menu_bold, x + 6,
			    baseline, m.titles[i], ptk.text);
		}
		x += w;
	}

	/* Horloge, ou date après un clic */
	ptk_text(&ptk, menubar, ptk.font_menu_bold, clock_x,
	    (ptk.menubar_h + ptk.font_menu_bold->ascent -
	    ptk.font_menu_bold->descent) / 2, clockbuf, ptk.text);

	/* rainure devant le menu d'application */
	{
		int r, c;

		for (r = 0; r < 20; r++)
			for (c = 0; c < 6; c++) {
				unsigned v;

				sscanf(map_rainure[r] + 2 * c, "%2x", &v);
				ptk_fill(&ptk, menubar, sep_x + c, r, 1, 1, gris_wm((int)v));
			}
	}

	if (app_active)
		titre_selection(sep_x + 6, sw - sep_x - 6);
	if (focused == NULL || strcmp(client_app_name(focused),
	    "Finder") == 0) {
		ptk_draw_pixmap(&ptk, menubar, pix_app16, pix_app16_mask,
		    sep_x + 11, 2, 16, 16);
	} else {
		draw_app_icon(&ptk, menubar, sep_x + 11, 2, focused->icon);
	}
	{
		int baseline = (ptk.menubar_h + ptk.font_menu_bold->ascent -
		    ptk.font_menu_bold->descent) / 2;

		ptk_text(&ptk, menubar, ptk.font_menu_bold, app_tx, baseline, app,
		    app_active ? ptk.selection_text : ptk.text);
	}
	/* coins arrondis de l'écran, lissés */
	{
		int r, c;

		for (r = 0; r < 6; r++)
			for (c = 0; c < 8; c++) {
				unsigned v;

				sscanf(map_coin_g[r] + 2 * c, "%2x", &v);
				ptk_fill(&ptk, menubar, c, r, 1, 1, gris_wm((int)v));
				sscanf(map_coin_d[r] + 2 * c, "%2x", &v);
				ptk_fill(&ptk, menubar, sw - 8 + c, r, 1, 1, gris_wm((int)v));
			}
	}
	menubar = reel;
	XCopyArea(dpy, bar_pix, menubar, ptk.gc, 0, 0, (unsigned)sw, (unsigned)ptk.menubar_h, 0, 0);
	XFlush(dpy);
}

static void
draw_growbox(Ptk *p, Drawable d, int active)
{
	static Pixmap img[2];
	int r, c;

	if (img[active != 0] == None) {
		Pixmap b = XCreatePixmap(dpy, d, 20, 20, (unsigned)DefaultDepth(dpy, screen));

		if (!active) {
			/* fenêtre inactive : encoche vide, trait #55 en haut à gauche */
			ptk_fill(p, b, 0, 0, 20, 20, ptk_color(p, "#dddddd"));
			ptk_fill(p, b, 0, 0, 16, 1, ptk_color(p, "#555555"));
			ptk_fill(p, b, 0, 0, 1, 16, ptk_color(p, "#555555"));
		} else {
			for (r = 0; r < 20; r++)
				for (c = 0; c < 20; c++) {
					char n[8];
					const char *h = map_grow[r] + 2 * c;

					snprintf(n, sizeof(n), "#%c%c%c%c%c%c", h[0], h[1], h[0], h[1], h[0], h[1]);
					ptk_fill(p, b, c, r, 1, 1, ptk_color(p, n));
				}
		}
		img[active != 0] = b;
	}
	XCopyArea(dpy, img[active != 0], d, p->gc, 0, 0, 20, 20, 0, 0);
}

static void
client_icon(Client *c, Pixmap *pix, Pixmap *mask)
{
	*pix = pix_app16;
	*mask = pix_app16_mask;
	if (c == NULL)
		return;
	if (strcmp(c->icon, "aucune") == 0) {
		*pix = None;
		*mask = None;
		return;
	}
	if (strcmp(c->icon, "folder") == 0) {
		*pix = pix_folder16;
		*mask = pix_folder16_mask;
	} else if (strcmp(c->icon, "doc") == 0) {
		*pix = pix_doc16;
		*mask = pix_doc16_mask;
	} else if (strcmp(c->icon, "disk") == 0) {
		*pix = pix_disk16;
		*mask = pix_disk16_mask;
	} else if (strcmp(c->icon, "trash") == 0) {
		*pix = pix_trash16;
		*mask = pix_trash16_mask;
	} else {
		Pixmap ip, im;

		if (icone_app_nommee(c->icon, &ip, &im)) {
			*pix = ip;
			*mask = im;
		}
	}
}

/* relevé sur la VM : largeur retranchée à gauche et à droite, rangée par rangée, en haut et en bas */
static void
draw_frame_calc(Client *c)
{
	static const int haut[5] = { 5, 3, 2, 1, 1 }, bas[5] = { 0, 1, 1, 2, 3 };
	int fw = c->w + 2 * c->bw + 1, fh = c->h + c->title_h + c->bw + 1, y;
	XRectangle r[12];
	int n = 0;
	Pixmap t = XCreatePixmap(dpy, c->frame, (unsigned)fw, (unsigned)fh, (unsigned)DefaultDepth(dpy, screen));
	PtkColor texte = c->active ? ptk.white : ptk.black;

	for (y = 0; y < 5; y++) {
		r[n].x = (short)haut[y];
		r[n].y = (short)y;
		r[n].width = (unsigned short)(fw - 2 * haut[y]);
		r[n++].height = 1;
	}
	r[n].x = 0;
	r[n].y = 5;
	r[n].width = (unsigned short)fw;
	r[n++].height = (unsigned short)(fh - 5 - 5);
	for (y = 0; y < 5; y++) {
		int e = y == 4 ? 5 : bas[y];

		r[n].x = (short)e;
		r[n].y = (short)(fh - 5 + y);
		r[n].width = (unsigned short)(fw - 2 * e);
		r[n++].height = 1;
	}
	XShapeCombineRectangles(dpy, c->frame, ShapeBounding, 0, 0, r, n, ShapeSet, YXBanded);
	ptk_fill(&ptk, t, 0, 0, fw, fh, ptk.black);
	if (c->active) {
		/* case de fermeture : cadre blanc de 11 px */
		ptk_rect(&ptk, t, 9, 4, 11, 11, ptk.white);
	} else {
		static const int blanc[5] = { 6, 4, 3, 2, 2 };

		for (y = 0; y < 5; y++)
			ptk_fill(&ptk, t, blanc[y], y + 1, fw - 2 * blanc[y] - 2, 1, ptk.white);
		ptk_fill(&ptk, t, 1, 6, fw - 4, 12, ptk.white);
	}
	/* pur noir et blanc : police bitmap, jamais de lissage */
	ptk_text(&ptk, t, ptk.font_title, 30, 14, c->name, texte);
	XCopyArea(dpy, t, c->frame, ptk.gc, 0, 0, (unsigned)fw, (unsigned)fh, 0, 0);
	XFreePixmap(dpy, t);
	XFlush(dpy);
}

/* poignée de redimensionnement : 20 px (Platinum), 10 px sur un pense-bête */
static int
grow_taille(const Client *c)
{
	return c->style_note ? 10 : 20;
}

/* couleur d'un pense-bête, et sa teinte foncée (barre de titre active) */
static PtkColor
note_teinte(const Client *c, int fonce)
{
	unsigned r = 0xff, g = 0xff, b = 0x99;
	char hex[16];

	if (sscanf(c->note_couleur, "#%2x%2x%2x", &r, &g, &b) != 3) {
		r = 0xff;
		g = 0xff;
		b = 0x99;
	}
	if (fonce) {
		r = r * 13 / 16;
		g = g * 13 / 16;
		b = b * 13 / 16;
	}
	snprintf(hex, sizeof(hex), "#%02x%02x%02x", r, g, b);
	return ptk_color(&ptk, hex);
}

static void
draw_grow_note(Client *c)
{
	PtkColor fond = note_teinte(c, 0), trait = note_teinte(c, 1);
	int i;

	ptk_fill(&ptk, c->grow, 0, 0, 10, 10, fond);
	if (!c->active)
		return;
	/* deux traits en diagonale, comme le coin d'un pense-bête de Mac OS 9 */
	for (i = 0; i < 2; i++) {
		ptk_line(&ptk, c->grow, 9, 2 + i * 4, 2 + i * 4, 9, ptk.black);
		ptk_line(&ptk, c->grow, 9, 3 + i * 4, 3 + i * 4, 9, trait);
	}
}

static void
draw_frame_note(Client *c)
{
	int fw = c->w + 2 * c->bw + 1, fh = (c->shaded ? c->title_h : c->h + c->title_h + c->bw) + 1;
	Pixmap t = XCreatePixmap(dpy, c->frame, (unsigned)fw, (unsigned)fh, (unsigned)DefaultDepth(dpy, screen));
	XRectangle r = { 0, 0, (unsigned short)fw, (unsigned short)fh };

	XShapeCombineRectangles(dpy, c->frame, ShapeBounding, 0, 0, &r, 1, ShapeSet, Unsorted);
	ptk_fill(&ptk, t, 0, 0, fw, fh, ptk.black);
	ptk_fill(&ptk, t, 1, 1, fw - 2, fh - 2, note_teinte(c, 0));
	if (c->active) {
		/* barre plus foncée, case de fermeture à gauche, case de repli à droite */
		ptk_fill(&ptk, t, 1, 1, fw - 2, c->title_h - 1, note_teinte(c, 1));
		ptk_line(&ptk, t, 1, c->title_h - 1, fw - 2, c->title_h - 1, ptk.black);
		ptk_rect(&ptk, t, 4, 2, 8, 8, ptk.black);
		ptk_fill(&ptk, t, 5, 3, 6, 6, note_teinte(c, 0));
		ptk_rect(&ptk, t, fw - 12, 2, 8, 8, ptk.black);
		ptk_fill(&ptk, t, fw - 11, 3, 6, 6, note_teinte(c, 0));
		ptk_line(&ptk, t, fw - 11, 5, fw - 6, 5, ptk.black);
	} else
		ptk_line(&ptk, t, 1, c->title_h - 1, fw - 2, c->title_h - 1, note_teinte(c, 1));
	XCopyArea(dpy, t, c->frame, ptk.gc, 0, 0, (unsigned)fw, (unsigned)fh, 0, 0);
	XFreePixmap(dpy, t);
	XFlush(dpy);
}

static void
draw_frame(Client *c)
{
	if (c->style_note) {
		draw_frame_note(c);
		if (!c->shaded && c->grow != None)
			draw_grow_note(c);
		return;
	}
	if (c->style_calc) {
		draw_frame_calc(c);
		return;
	}
	int fw = c->w + 2 * c->bw;
	int fh = c->shaded ? c->title_h :
	    c->h + c->title_h + c->bw;
	Pixmap ip, im;
	Window reel = c->frame;
	Pixmap tampon = XCreatePixmap(dpy, reel, (unsigned)fw + 1, (unsigned)fh + 1,
	    (unsigned)DefaultDepth(dpy, screen));

	c->frame = tampon;
	client_icon(c, &ip, &im);
	/*
	 * Cadre Platinum, relevé au pixel sur Mac OS 9.2. De l'extérieur vers l'intérieur :
	 * noir, blanc, #cc, #cc, #99, noir à gauche et en haut (barre de titre comprise) ;
	 * noir, #99, #cc, #cc, blanc, noir à droite et en bas ; ombre de 1 px décalée de 2 px.
	 */
	{
		int shaded = c->shaded;
		PtkColor g99 = ptk_color(&ptk, "#999999"), gcc = ptk_color(&ptk, "#cccccc");
		PtkColor g55 = ptk_color(&ptk, "#555555"), gdd = ptk_color(&ptk, "#dddddd");
		XRectangle forme[3] = {
			{ 0, 0, (unsigned short)fw, (unsigned short)fh },
			{ (short)fw, 2, 1, (unsigned short)(fh - 1) },
			{ 2, (short)fh, (unsigned short)(fw - 1), 1 },
		};

		XShapeCombineRectangles(dpy, reel, ShapeBounding, 0, 0, forme, 3,
		    ShapeSet, Unsorted);
		if (c->active) {
			ptk_fill(&ptk, c->frame, 0, 0, fw + 1, fh + 1, ptk.black);
			ptk_fill(&ptk, c->frame, 1, 1, fw - 3, 1, ptk.white);
			ptk_fill(&ptk, c->frame, 1, 1, 1, fh - 3, ptk.white);
			ptk_fill(&ptk, c->frame, fw - 2, 1, 1, 1, gcc);
			ptk_fill(&ptk, c->frame, 1, fh - 2, 1, 1, gcc);
			ptk_fill(&ptk, c->frame, 2, 2, fw - 4, fh - 4, gcc);
			ptk_fill(&ptk, c->frame, fw - 2, 2, 1, fh - 3, g99);
			ptk_fill(&ptk, c->frame, 2, fh - 2, fw - 3, 1, g99);
			if (!shaded) {
				ptk_fill(&ptk, c->frame, 4, 20, fw - 9, 1, g99);
				ptk_fill(&ptk, c->frame, 4, 20, 1, fh - 25, g99);
				ptk_fill(&ptk, c->frame, 5, 21, fw - 10, 1, ptk.black);
				ptk_fill(&ptk, c->frame, 5, 21, 1, fh - 26, ptk.black);
				ptk_fill(&ptk, c->frame, fw - 6, 21, 1, fh - 26, ptk.black);
				ptk_fill(&ptk, c->frame, 5, fh - 6, fw - 10, 1, ptk.black);
				ptk_fill(&ptk, c->frame, fw - 5, 21, 1, fh - 25, ptk.white);
				ptk_fill(&ptk, c->frame, 5, fh - 5, fw - 9, 1, ptk.white);
			}
		} else {
			ptk_fill(&ptk, c->frame, 0, 0, fw + 1, fh + 1, g55);
			ptk_fill(&ptk, c->frame, 1, 1, fw - 2, fh - 2, gdd);
			if (!shaded) {
				ptk_fill(&ptk, c->frame, 5, 21, fw - 10, 1, g55);
				ptk_fill(&ptk, c->frame, 5, 21, 1, fh - 26, g55);
				ptk_fill(&ptk, c->frame, fw - 6, 21, 1, fh - 26, g55);
				ptk_fill(&ptk, c->frame, 5, fh - 6, fw - 10, 1, g55);
			}
		}
	}
	ptk.titre_icone_presse = c->icone_pressee;
	ptk.titre_sans_zoom = c->min_w > 0 && c->min_w == c->max_w && c->min_h > 0 && c->min_h == c->max_h;
	ptk_titlebar(&ptk, c->frame, 0, 0, fw, c->title_h,
	    c->name[0] ? c->name : "Sans titre", c->active, ip, im);
	ptk.titre_sans_zoom = 0;
	ptk.titre_icone_presse = 0;
	c->frame = reel;
	XCopyArea(dpy, tampon, reel, ptk.gc, 0, 0, (unsigned)fw + 1, (unsigned)fh + 1, 0, 0);
	XFreePixmap(dpy, tampon);
	if (!c->shaded && c->grow != None && !c->style_note)
		draw_growbox(&ptk, c->grow, c->active);
	XFlush(dpy);
}

static void
publish_geometry(Client *c)
{
	long data[4];

	data[0] = c->x;
	data[1] = c->y;
	data[2] = c->w;
	data[3] = c->h;
	XChangeProperty(dpy, c->win, a_platine_geometry, XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)data, 4);
}

/* l'application interdit tout redimensionnement : ni encoche ni zoom */
static int
taille_fixe(const Client *c)
{
	return c->min_w > 0 && c->min_w == c->max_w && c->min_h > 0 && c->min_h == c->max_h;
}

/* taille permise : minimum de lisibilité et limites posées par l'application */
static void
limiter_taille(const Client *c, int *w, int *h)
{
	if (*w < 120)
		*w = 120;
	if (*h < 60)
		*h = 60;
	if (c->min_w > 0 && *w < c->min_w)
		*w = c->min_w;
	if (c->min_h > 0 && *h < c->min_h)
		*h = c->min_h;
	if (c->max_w > 0 && *w > c->max_w)
		*w = c->max_w;
	if (c->max_h > 0 && *h > c->max_h)
		*h = c->max_h;
}

static void
configure_client(Client *c, int x, int y, int w, int h)
{
	int fw, fh;
	/* cadre d'avant : s'il rétrécit ou bouge, ce qu'il découvre doit être repeint */
	int ax = c->x, ay = c->y, aw = c->w + 2 * c->bw + 2;
	int ah = (c->shaded ? c->title_h : c->h + c->title_h + c->bw) + 2;

	limiter_taille(c, &w, &h);
	if (y < ptk.menubar_h)
		y = ptk.menubar_h;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	fw = w + 2 * c->bw;
	fh = c->shaded ? c->title_h : h + c->title_h + c->bw;
	XMoveResizeWindow(dpy, c->frame, x, y, (unsigned)(fw + 1),
	    (unsigned)(fh + 1));
	if (!c->shaded) {
		XMoveResizeWindow(dpy, c->win, c->bw, c->title_h,
		    (unsigned)w, (unsigned)h);
		if (c->grow != None) {
			XMoveResizeWindow(dpy, c->grow, fw - grow_taille(c) - 1, fh - grow_taille(c) - 1,
			    (unsigned)grow_taille(c), (unsigned)grow_taille(c));
			XRaiseWindow(dpy, c->grow);
		}
	}
	draw_frame(c);
	if ((x != ax || y != ay || fw + 2 < aw || fh + 2 < ah) && !outline_on &&
	    ob_x1 <= ob_x0) {
		ob_x0 = ax;
		ob_y0 = ay;
		ob_x1 = ax + aw;
		ob_y1 = ay + ah;
		outline_reparer();
	}
}

static void
toggle_shade(Client *c)
{
	if (outline_on) {
		outline_destroy();
		outline_reparer();
	}
	if (!c->shaded) {
		c->old_h = c->h;
		c->shaded = 1;
		c->ignore_unmap++;
		XUnmapWindow(dpy, c->win);
		if (c->grow != None)
			XUnmapWindow(dpy, c->grow);
		XResizeWindow(dpy, c->frame,
		    (unsigned)(c->w + 2 * c->bw + 1),
		    (unsigned)(c->title_h + 1));
		set_wm_state(c, IconicState);
	} else {
		c->shaded = 0;
		XResizeWindow(dpy, c->frame,
		    (unsigned)(c->w + 2 * c->bw + 1),
		    (unsigned)(c->h + c->title_h + c->bw + 1));
		XMapWindow(dpy, c->win);
		if (c->grow != None) {
			XMoveResizeWindow(dpy, c->grow,
			    c->w + 2 * c->bw - grow_taille(c) - 1,
			    c->h + c->title_h + c->bw - grow_taille(c) - 1,
			    (unsigned)grow_taille(c), (unsigned)grow_taille(c));
			XMapRaised(dpy, c->grow);
		}
		set_wm_state(c, NormalState);
		focus_client(c);
	}
	draw_frame(c);
}

static void
toggle_zoom(Client *c)
{
	if (taille_fixe(c))
		return;
	if (!c->maxed) {
		c->old_x = c->x;
		c->old_y = c->y;
		c->old_w = c->w;
		c->old_h = c->h;
		c->maxed = 1;
		configure_client(c, c->max_w > 0 ? c->x : 0, ptk.menubar_h, sw - 2 * c->bw - 1,
		    sh - ptk.menubar_h - c->title_h - c->bw - 1);
	} else {
		c->maxed = 0;
		configure_client(c, c->old_x, c->old_y, c->old_w, c->old_h);
	}
}

static int
client_protocol(Client *c, Atom protocol)
{
	Atom *protocols = NULL;
	int n = 0, i, found = 0;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		for (i = 0; i < n; i++)
			if (protocols[i] == protocol)
				found = 1;
		XFree(protocols);
	}
	return found;
}

static void
close_client(Client *c)
{
	XEvent ev;

	if (client_protocol(c, wm_delete)) {
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wm_protocols;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = (long)wm_delete;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
		XFlush(dpy);
	} else {
		XKillClient(dpy, c->win);
	}
}

/*
 * Session : arrêt et redémarrage façon Mac OS 9. Le Finder (ou platine-alerte) envoie
 * _PLATINE_ARRET à la racine ; le WM demande à chaque application de se fermer
 * (WM_DELETE_WINDOW), attend qu'il ne reste que le Finder, puis éteint. Une application
 * qui ne se ferme pas en ARRET_DELAI secondes interrompt l'arrêt, avec une alerte qui
 * propose de continuer quand même (data.l[1] = 1 : on force).
 */
#define ARRET_DELAI 30
static Atom a_platine_arret;
static int arret_mode;	/* 0 : rien ; 1 : éteindre ; 2 : redémarrer */
static int arret_force;
static time_t arret_debut;

static int
client_finder(Client *c)
{
	return strcmp(client_app_name(c), "Finder") == 0;
}

static void
lancer_argv(char *const argv[])
{
	pid_t pid = fork();

	if (pid == 0) {
		setsid();
		execvp(argv[0], argv);
		_exit(127);
	}
}

static long
client_pid(Client *c)
{
	Atom type;
	int fmt;
	unsigned long n, reste;
	unsigned char *d = NULL;
	long pid = 0;

	if (XGetWindowProperty(dpy, c->win, XInternAtom(dpy, "_NET_WM_PID", False), 0, 1, False,
	    XA_CARDINAL, &type, &fmt, &n, &reste, &d) == Success && d != NULL) {
		if (n == 1 && fmt == 32)
			pid = *(long *)d;
		XFree(d);
	}
	return pid;
}

static void
arret_tuer(Client *c)
{
	long pid = client_pid(c);

	if (pid > 1 && pid != (long)getpid())
		kill((pid_t)pid, SIGKILL);
	XKillClient(dpy, c->win);
}

static void
arret_demarrer(int mode, int force)
{
	Client *c;

	if (mode == 0) {
		arret_mode = 0;
		return;
	}
	if (arret_mode == 0 || force)
		arret_debut = time(NULL);
	arret_mode = mode;
	arret_force = force;
	fprintf(stderr, "platine-wm: %s demandé%s\n", mode == 2 ? "redémarrage" : "arrêt",
	    force ? " (forcé)" : "");
	for (c = clients; c; c = c->next)
		if (!client_finder(c)) {
			if (force)
				arret_tuer(c);
			else
				close_client(c);
		}
	XFlush(dpy);
}

/* appelée à chaque tour de boucle */
static void
arret_suivre(void)
{
	Client *c, *reste = NULL;
	time_t now = time(NULL);

	if (arret_mode == 0)
		return;
	for (c = clients; c; c = c->next)
		if (!client_finder(c)) {
			reste = c;
			break;
		}
	if (reste == NULL || (arret_force && now - arret_debut >= 3)) {
		char *argv[] = { "sh", "-c", NULL, NULL };

		argv[2] = arret_mode == 2 ? SYS_PRIVILEGE " redemarrer" : SYS_PRIVILEGE " arret";
		fprintf(stderr, "platine-wm: %s\n", argv[2]);
		lancer_argv(argv);
		arret_mode = 0;
		return;
	}
	if (arret_force) {
		arret_tuer(reste);
		return;
	}
	if (now - arret_debut >= (getenv("PLATINE_ARRET_DELAI") != NULL ?
	    atoi(getenv("PLATINE_ARRET_DELAI")) : ARRET_DELAI)) {
		char m[4], al[PATH_MAX];
		char *argv[] = { al, "arret-bloque", m, NULL, NULL };

		ptk_chemin_programme("System/Extensions/Alerte", "platine-alerte",
		    al, sizeof(al));
		snprintf(m, sizeof(m), "%d", arret_mode);
		argv[3] = (char *)client_app_name(reste);
		arret_mode = 0;
		lancer_argv(argv);
	}
}

/* ⌘⌥Échap : forcer l'application au premier plan à quitter */
static void
forcer_quitter(void)
{
	Client *c = focused;
	char w[32], al[PATH_MAX];
	char *argv[] = { al, "forcer", w, NULL, NULL };

	ptk_chemin_programme("System/Extensions/Alerte", "platine-alerte",
	    al, sizeof(al));
	if (c == NULL)
		for (c = clients; c && !client_finder(c); c = c->next)
			;
	if (c == NULL)
		return;
	snprintf(w, sizeof(w), "0x%lx", (unsigned long)c->win);
	argv[3] = (char *)client_app_name(c);
	lancer_argv(argv);
}

static void
grab_forcer(void)
{
	KeyCode k = XKeysymToKeycode(dpy, XK_Escape);
	unsigned int extra[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
	size_t i;

	if (k == 0)
		return;
	for (i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
		XGrabKey(dpy, k, Mod1Mask | ControlMask | extra[i], root, True,
		    GrabModeAsync, GrabModeAsync);
}

typedef struct {
	KeyCode code;
	char cmd[128];
} Shortcut;

static Shortcut shortcuts[64];
static int n_shortcuts;

/*
 * Raccourcis ⌘ des menus : le WM prend les combinaisons Mod1+lettre de la
 * fenêtre active et renvoie la commande correspondante au client, comme la
 * Toolbox de Mac OS 9.
 */
static void
update_shortcuts(Client *c)
{
	MenuModel m;
	int i, j;

	for (i = 0; i < n_shortcuts; i++)
		XUngrabKey(dpy, shortcuts[i].code, Mod1Mask, root);
	n_shortcuts = 0;
	if (c == NULL)
		return;
	parse_model(menu_source(c), &m);
	for (i = 0; i < m.count && n_shortcuts < 64; i++) {
		for (j = 0; j < m.n_items[i] && n_shortcuts < 64; j++) {
			char label[ITEM_LEN], sc[64], key[8];
			const char *p;
			KeySym ks;
			KeyCode kc;

			if (strcmp(m.items[i][j], "-") == 0 || m.items[i][j][0] == '~')
				continue;
			split_item(m.items[i][j], label, sizeof(label),
			    sc, sizeof(sc));
			if ((unsigned char)sc[0] != 0xE2 ||
			    (unsigned char)sc[1] != 0x8C ||
			    (unsigned char)sc[2] != 0x98)
				continue;
			p = sc + 3;
			if (*p == '\0')
				continue;
			key[0] = *p;
			key[1] = '\0';
			ks = XStringToKeysym(key);
			if (ks == NoSymbol)
				continue;
			kc = XKeysymToKeycode(dpy, ks);
			if (kc == 0)
				continue;
			/* owner_events=False : les raccourcis ⌘ reviennent
			 * toujours au WM, quel que soit le focus ; sinon la
			 * touche est livrée au client et le WM l'ignore selon
			 * l'état de ses grabs (comportement erratique). */
			XGrabKey(dpy, kc, Mod1Mask, root, False,
			    GrabModeAsync, GrabModeAsync);
			shortcuts[n_shortcuts].code = kc;
			snprintf(shortcuts[n_shortcuts].cmd,
			    sizeof(shortcuts[0].cmd), "%s", label);
			n_shortcuts++;
		}
	}
	XSync(dpy, False);
}

/* Une fenêtre qui se redessine sous le contour XOR (fréquent sur une machine lente) garde ses
 * pointillés une fois le contour effacé, et le XOR d'effacement en redessine là où elle les
 * avait déjà couverts : on fait tout redessiner dans la zone balayée. Une fenêtre transparente
 * mappée puis détruite par-dessus fait envoyer un Expose à tout ce qui est dessous — racine,
 * bureau et barre de menus, Control Strip (hors gestion) compris, pas seulement les clients. */
static void
outline_reparer(void)
{
	Client *c;

	if (ob_x1 > ob_x0 && ob_y1 > ob_y0) {
		XSetWindowAttributes a;
		Window cache;

		a.override_redirect = True;
		a.background_pixmap = None;	/* ne peint rien : seul l'Expose compte */
		cache = XCreateWindow(dpy, DefaultRootWindow(dpy), ob_x0 - 1, ob_y0 - 1,
		    (unsigned)(ob_x1 - ob_x0 + 2), (unsigned)(ob_y1 - ob_y0 + 2), 0,
		    CopyFromParent, InputOutput, CopyFromParent,
		    CWOverrideRedirect | CWBackPixmap, &a);
		XMapRaised(dpy, cache);
		XSync(dpy, False);
		XDestroyWindow(dpy, cache);
		XSync(dpy, False);
	}
	for (c = clients; c; c = c->next) {
		if (!c->mapped || c->x >= ob_x1 || c->y >= ob_y1 ||
		    c->x + c->w + 2 * c->bw + 1 <= ob_x0 ||
		    c->y + c->title_h + c->h + c->bw + 1 <= ob_y0)
			continue;
		XClearArea(dpy, c->win, 0, 0, 0, 0, True);
		draw_frame(c);
	}
	ob_x0 = ob_y0 = ob_x1 = ob_y1 = 0;
}

static void
focus_client(Client *c)
{
	XDeleteProperty(dpy, DefaultRootWindow(dpy), a_platine_bureau);
	if (focused != NULL && focused != c) {
		focused->active = 0;
		draw_frame(focused);
	}
	focused = c;
	update_shortcuts(c);
	if (c != NULL) {
		c->active = 1;
		if (c->mapped) {
			XRaiseWindow(dpy, c->frame);
			if (c->take_focus) {
				XEvent ev;

				memset(&ev, 0, sizeof(ev));
				ev.xclient.type = ClientMessage;
				ev.xclient.window = c->win;
				ev.xclient.message_type = wm_protocols;
				ev.xclient.format = 32;
				ev.xclient.data.l[0] = (long)wm_take_focus;
				ev.xclient.data.l[1] = CurrentTime;
				XSendEvent(dpy, c->win, False, NoEventMask, &ev);
			} else {
				XSetInputFocus(dpy, c->win, RevertToPointerRoot,
				    CurrentTime);
			}
		}
		draw_frame(c);
	}
	XRaiseWindow(dpy, menubar);
	raise_corners();
	if (popup_open) {
		XRaiseWindow(dpy, popup.win);
		raise_corners();
	}
	draw_menubar();
}

static void
focus_next(int dir)
{
	Client *c, *start;
	int n = 0, idx = 0, target;

	for (c = clients; c; c = c->next)
		if (c->mapped && !c->shaded)
			n++;
	if (n == 0)
		return;
	start = focused;
	if (start == NULL) {
		target = dir > 0 ? 0 : n - 1;
	} else {
		for (c = clients; c; c = c->next) {
			if (c == start)
				break;
			if (c->mapped && !c->shaded)
				idx++;
		}
		target = (idx + (dir > 0 ? 1 : n - 1)) % n;
	}
	idx = 0;
	for (c = clients; c; c = c->next) {
		if (!c->mapped || c->shaded)
			continue;
		if (idx == target) {
			focus_client(c);
			return;
		}
		idx++;
	}
}

static void
remove_client(Client *c)
{
	Client **pp;

	for (pp = &clients; *pp; pp = &(*pp)->next) {
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	}
	if (focused == c)
		focused = NULL;
	if (last_click_client == c)
		last_click_client = NULL;
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	XDestroyWindow(dpy, c->frame);
	free(c->raw_menu);
	free(c);
	if (focused == NULL)
		focus_next(1);
	draw_menubar();
}

static void
manage_window(Window w)
{
	XWindowAttributes attr;
	Client *c;
	XSetWindowAttributes fa;
	XSizeHints hints;
	long supplied;
	int fw, fh, has_pos = 0;
	static int cascade_x = 40, cascade_y = 60;

	lance_nom[0] = '\0';	/* la fenêtre attendue est là */

	if (w == menubar || (popup_open && w == popup.win))
		return;
	for (c = clients; c; c = c->next)
		if (c->win == w)
			return;

	if (!XGetWindowAttributes(dpy, w, &attr))
		return;
	if (attr.override_redirect) {
		XMapWindow(dpy, w);
		return;
	}

	c = calloc(1, sizeof(*c));
	if (c == NULL)
		return;
	c->win = w;
	c->x = attr.x;
	c->y = attr.y;
	c->w = attr.width > 0 ? attr.width : 400;
	c->h = attr.height > 0 ? attr.height : 300;
	c->bw = 6;
	c->title_h = ptk.titlebar_h;
	c->mapped = 1;
	c->take_focus = client_protocol(c, wm_take_focus);
	{
		char *st = get_prop(w, a_platine_style, NULL);

		if (st != NULL) {
			if (strcmp(st, "calculette") == 0) {
				c->style_calc = 1;
				c->bw = 1;
				c->title_h = 19;
			} else if (strncmp(st, "pense-bete", 10) == 0) {
				c->style_note = 1;
				c->bw = 1;
				c->title_h = 12;
				snprintf(c->note_couleur, sizeof(c->note_couleur), "%s",
				    st[10] == ':' ? st + 11 : "#ffff99");
			}
			free(st);
		}
	}

	memset(&hints, 0, sizeof(hints));
	if (XGetWMNormalHints(dpy, w, &hints, &supplied)) {
		if (hints.flags & (USPosition | PPosition))
			has_pos = 1;
		if (hints.flags & PMinSize) {
			c->min_w = hints.min_width;
			c->min_h = hints.min_height;
			if (c->w < hints.min_width)
				c->w = hints.min_width;
			if (c->h < hints.min_height)
				c->h = hints.min_height;
		}
		if (hints.flags & PMaxSize) {
			c->max_w = hints.max_width;
			c->max_h = hints.max_height;
			if (hints.max_width > 0 && c->w > hints.max_width)
				c->w = hints.max_width;
			if (hints.max_height > 0 && c->h > hints.max_height)
				c->h = hints.max_height;
		}
	}
	if (!has_pos && attr.x == 0 && attr.y == 0) {
		c->x = cascade_x;
		c->y = cascade_y;
		cascade_x += 24;
		cascade_y += 24;
		if (cascade_x > sw - 220)
			cascade_x = 40;
		if (cascade_y > sh - 180)
			cascade_y = 60;
	}
	if (c->y < ptk.menubar_h)
		c->y = ptk.menubar_h + 20;
	if (c->x + c->w > sw)
		c->x = sw - c->w - 10;
	if (c->x < 0)
		c->x = 0;
	if (c->y + c->h + c->title_h > sh)
		c->y = sh - c->h - c->title_h - c->bw;
	if (c->y < ptk.menubar_h)
		c->y = ptk.menubar_h;

	fw = c->w + 2 * c->bw;
	fh = c->h + c->title_h + c->bw;
	fa.override_redirect = True;
	fa.background_pixel = ptk.window.pixel;
	/* SubstructureRedirect : une fois le client dans son cadre, ses demandes de taille
	 * passent encore par le WM (sinon le client grandit seul dans un cadre figé) */
	fa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask | FocusChangeMask | SubstructureRedirectMask;
	c->frame = XCreateWindow(dpy, root, c->x, c->y,
	    (unsigned)(fw + 1), (unsigned)(fh + 1), 0, CopyFromParent,
	    InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &fa);
	XSetWindowBorderWidth(dpy, c->frame, 0);
	XAddToSaveSet(dpy, w);
	XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask |
	    FocusChangeMask);
	XSetWindowBorderWidth(dpy, w, 0);
	XReparentWindow(dpy, w, c->frame, c->bw, c->title_h);
	/*
	 * XSelectInput(ButtonPressMask) sur la fenêtre d'un autre client
	 * échoue sur certains serveurs (BadAccess). On utilise donc un grab
	 * passif + ReplayPointer, la méthode classique des WM X11.
	 * Le grab est installé après le reparentage : installé avant, le
	 * rejeu ne parvient pas toujours au client.
	 */
	XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask,
	    GrabModeSync, GrabModeAsync, None, None);

	c->next = clients;
	clients = c;

	update_client_names(c);
	set_wm_state(c, NormalState);
	publish_geometry(c);
	ptk_son(&ptk, "wopn");
	XMapWindow(dpy, c->frame);
	XMapWindow(dpy, w);
	if (!taille_fixe(c)) {
		XSetWindowAttributes ga;
		ga.override_redirect = True;
		ga.background_pixel = ptk.window.pixel;
		ga.event_mask = ExposureMask | ButtonPressMask |
		    ButtonReleaseMask | PointerMotionMask;
		c->grow = XCreateWindow(dpy, c->frame, fw - grow_taille(c) - 1, fh - grow_taille(c) - 1,
		    (unsigned)grow_taille(c), (unsigned)grow_taille(c), 0, CopyFromParent, InputOutput, CopyFromParent,
		    CWOverrideRedirect | CWBackPixel | CWEventMask, &ga);
		XMapRaised(dpy, c->grow);
	}
	XRaiseWindow(dpy, c->frame);
	XRaiseWindow(dpy, menubar);
	focus_client(c);
}

static void
unmanage_window(Window w)
{
	Client *c;

	for (c = clients; c; c = c->next) {
		if (c->win == w) {
			remove_client(c);
			return;
		}
	}
}

static void
split_item(const char *item, char *label, size_t lsz, char *sc, size_t ssz)
{
	/* « Libellé\x1fA\x1eB » : sous-menu, ignoré pour le libellé */
	char sans[ITEM_LEN];
	snprintf(sans, sizeof(sans), "%s", item);
	sans[strcspn(sans, "\x1f")] = '\0';
	item = sans;
	if (item[0] == '\x02' && strchr(item + 1, '\x02') != NULL)
		item = strchr(item + 1, '\x02') + 1;

	const char *tab = strchr(item, '\t');
	size_t n;

	sc[0] = '\0';
	if (tab == NULL) {
		snprintf(label, lsz, "%s", item);
		return;
	}
	n = (size_t)(tab - item);
	if (n >= lsz)
		n = lsz - 1;
	memcpy(label, item, n);
	label[n] = '\0';
	snprintf(sc, ssz, "%s", tab + 1);
}

static int
popup_is_sep(int i)
{
	return i >= 0 && i < popup.count &&
	    strcmp(popup.items[i], "-") == 0;
}

/* 18 px pour une ligne à icône (menu Pomme), 16 sinon, 6 pour un séparateur */
static int
popup_row_h(int i)
{
	if (popup_is_sep(i))
		return MENU_SEP_H;
	return popup.items[i][0] == '\x02' ? 18 : MENU_ITEM_H;
}

/* élément grisé (préfixe « ~ ») : visible, jamais sélectionnable */
static int
popup_is_off(int i)
{
	return i >= 0 && i < popup.count && popup.items[i][0] == '~';
}

static void
popup_geom(int idx, int *y, int *hh)
{
	int i, yy = popup.top;

	for (i = 0; i < popup.count; i++) {
		int ih = popup_row_h(i);

		if (i == idx) {
			*y = yy;
			*hh = ih;
			return;
		}
		yy += ih;
	}
	*y = yy;
	*hh = 0;
}

static int
popup_hit(int my)
{
	int i, yy = popup.top;

	for (i = 0; i < popup.count; i++) {
		int ih = popup_row_h(i);

		if (my >= yy && my < yy + ih)
			return popup_is_off(i) ? -1 : i;
		yy += ih;
	}
	return -1;
}

static void
popup_move(int delta)
{
	int i, guard = 0;

	if (popup.count == 0)
		return;
	i = popup.selected;
	if (i < 0)
		i = delta > 0 ? -1 : popup.count;
	do {
		i = (i + delta + popup.count) % popup.count;
		guard++;
	} while ((popup_is_sep(i) || popup_is_off(i)) && guard <= popup.count);
	if (popup_is_sep(i) || popup_is_off(i))
		return;
	popup.selected = i;
	popup_draw();
}

static int popup_has_sub(int i);

static void
popup_activate(int idx)
{
	char label[ITEM_LEN], sc[32];
	Client *c;

	if (popup_has_sub(idx)) {
		/* comme Mac OS 9 : cliquer sur un dossier du menu Pomme l'ouvre dans le Finder */
		if (popup.kind == 1) {
			char dos[ITEM_LEN], chem[PATH_MAX];
			const char *h = getenv("HOME");
			Atom a = XInternAtom(dpy, "_PLATINE_OUVRIR", False);

			split_item(popup.items[idx], dos, sizeof(dos), sc, sizeof(sc));
			/* le dossier tel qu'il est dans Éléments du menu Pomme, sinon
			 * (table de repli) dans l'arborescence du volume */
			(void)h;
			/* un alias ouvre son original, comme sous Mac OS 9 */
			if (pomme_chemin(dos) != NULL) {
				if (realpath(pomme_chemin(dos), chem) == NULL)
					snprintf(chem, sizeof(chem), "%s", pomme_chemin(dos));
			} else
				snprintf(chem, sizeof(chem), "%s/%s", ptk_volume(),
				    strcmp(dos, "Tableaux de bord") == 0 ? "System/Control Panels" : dos);
			XChangeProperty(dpy, DefaultRootWindow(dpy), a, XInternAtom(dpy, "UTF8_STRING", False), 8,
			    PropModeReplace, (unsigned char *)chem, (int)strlen(chem));
			close_popup();
		}
		return;
	}
	if (idx < 0 || idx >= popup.count || popup_is_sep(idx) || popup_is_off(idx)) {
		close_popup();
		return;
	}
	/* l'élément choisi clignote avant la fermeture du menu : 3 fois, 47 ms par état (relevé sur la VM) */

	ptk_son(&ptk, "mnus");
	menu_choisi = 1;
	{
		struct timespec ts = { 0, 47000000 };
		int k;

		popup.selected = idx;
		for (k = 0; k < 6; k++) {
			popup.flash = k % 2 == 0 ? idx : -1;
			popup_draw_row(idx);
			XFlush(dpy);
			nanosleep(&ts, NULL);
		}
	}
	popup.flash = -1;
	split_item(popup.items[idx], label, sizeof(label), sc, sizeof(sc));
	if (strncmp(label, "\xe2\x88\x9a ", 4) == 0)
		memmove(label, label + 4, strlen(label + 4) + 1);

	if (popup.kind == 2) {
		int n = 0;
		for (c = clients; c; c = c->next) {
			if (!c->mapped || c->shaded)
				continue;
			if (n == idx) {
				focus_client(c);
				break;
			}
			n++;
		}
	} else if (popup.kind == 3) {
		if (popup.client != NULL)
			send_command(popup.client, label);
		else if (popup.client_win != None)
			send_command_win(popup.client_win, label);
	} else if (popup.kind == 1 && !strstr(label, "propos") && pomme_chemin(label) != NULL) {
		/* élément du dossier Éléments du menu Pomme */
		snprintf(lance_nom, sizeof(lance_nom), "%s", label);
		lance_fin = time(NULL) + 4;
		pomme_ouvrir(pomme_chemin(label));
	} else if (popup.kind == 1) {
		char res[PATH_MAX];
		const char *cmd = strstr(label, "propos") ?
		    "@System/Extensions/À propos de cet ordinateur|platine-about" :
		    pomme_commande(label);

		if (cmd != NULL)
			cmd = ptk_resoudre(cmd, res, sizeof(res));

		if (cmd != NULL && !strstr(label, "propos")) {
			snprintf(lance_nom, sizeof(lance_nom), "%s", label);
			lance_fin = time(NULL) + 4;
		}
		if (cmd != NULL && fork() == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
			_exit(127);
		}
	} else if (popup.client != NULL && popup.client->raw_menu == NULL) {
		if (strcmp(label, "Fermer") == 0 ||
		    strcmp(label, "Quitter") == 0)
			close_client(popup.client);
	} else {
		send_command(popup.client, label);
	}
	close_popup();
}

static int
popup_has_sub(int i)
{
	return i >= 0 && i < popup.count && strchr(popup.items[i], '\x1f') != NULL;
}

/* Mesures Mac OS 9.2 : voir docs/08-menus.md. B = colonne du bord noir droit. */
static void
popup_draw_row(int i)
{
	int y, ih, baseline, B = popup.w - 2, last;
	char label[ITEM_LEN], sc[32];
	const char *l;
	int tx = 21, coche = 0, on;
	PtkColor txt;

	if (i < 0 || i >= popup.count)
		return;
	if (popup.buf != None && popup.win != popup.buf) {
		Window reel = popup.win;

		popup.win = popup.buf;
		popup_draw_row(i);
		popup.win = reel;
		popup_geom(i, &y, &ih);
		XCopyArea(dpy, popup.buf, reel, ptk.gc, 0, y, (unsigned)popup.w, (unsigned)ih, 0, y);
		XFlush(dpy);
		return;
	}
	popup_geom(i, &y, &ih);
	last = i == popup.count - 1;
	ptk_fill(&ptk, popup.win, 1, y, B - 1, ih, ptk.menu);
	ptk_fill(&ptk, popup.win, 1, y, 1, ih, ptk.white);
	ptk_fill(&ptk, popup.win, B - 1, y, 1, ih, ptk_color(&ptk, "#999999"));
	if (i == 0) {
		ptk_fill(&ptk, popup.win, 1, y, B - 2, 1, ptk.white);
		ptk_fill(&ptk, popup.win, B - 1, y, 1, 1, ptk.menu);
	}
	if (last) {
		ptk_fill(&ptk, popup.win, 1, y + ih - 1, B - 1, 1, ptk_color(&ptk, "#999999"));
		ptk_fill(&ptk, popup.win, 1, y + ih - 1, 1, 1, ptk.menu);
	}
	if (popup_is_sep(i)) {
		ptk_fill(&ptk, popup.win, 1, y + 2, B - 1, 1, ptk_color(&ptk, "#888888"));
		ptk_fill(&ptk, popup.win, 1, y + 3, B - 1, 1, ptk.white);
		XFlush(dpy);
		return;
	}
	split_item(popup.items[i], label, sizeof(label), sc, sizeof(sc));
	baseline = y + 12;
	on = i == popup.selected && i != popup.flash;
	if (on) {
		ptk_fill(&ptk, popup.win, 1, y, B - 1, ih, ptk.selection);
		ptk_fill(&ptk, popup.win, B - 1, y, 1, ih, ptk_color(&ptk, "#000088"));
		ptk_fill(&ptk, popup.win, 1, y, 1, ih, ptk_color(&ptk, "#6666cc"));
		if (i == 0)
			ptk_fill(&ptk, popup.win, 1, y, B - 2, 1, ptk_color(&ptk, "#6666cc"));
		if (last)
			ptk_fill(&ptk, popup.win, 1, y + ih - 1, B - 1, 1, ptk_color(&ptk, "#000088"));
	}
	if (popup.items[i][0] == '\x02') {
		char ic[64], fichier[96];
		const char *f = strchr(popup.items[i] + 1, '\x02');
		Pixmap ip = None, im = None;

		snprintf(ic, sizeof(ic), "%.*s", f ? (int)(f - popup.items[i] - 1) : 0,
		    popup.items[i] + 1);
		snprintf(fichier, sizeof(fichier), "menu/%s.xpm", ic);
		if (ptk_load_xpm(&ptk, fichier, &ip, &im) == 0 && ip != None) {
			ptk_draw_pixmap(&ptk, popup.win, ip, im, 19, y + 1, 16, 16);
			XFreePixmap(dpy, ip);
			if (im != None)
				XFreePixmap(dpy, im);
		}
		tx = 40;
		baseline = y + 13;
	}
	l = label;
	if (*l == '~')
		l++;
	if (strncmp(l, "\xe2\x88\x9a ", 4) == 0) {
		l += 4;
		coche = 1;
	}
	txt = on ? ptk.selection_text : popup_is_off(i) ? ptk_color(&ptk, "#777777") : ptk.text;
	if (coche)
		ptk_text(&ptk, popup.win, ptk.font_menu_bold, 8, baseline, "\xe2\x88\x9a", txt);
	ptk_text(&ptk, popup.win, ptk.font_menu_bold, tx, baseline, l, txt);
	if (sc[0] != '\0')
		/* même police que le menu, ⌘ en colonne à 40 px du bord noir */
		ptk_text(&ptk, popup.win, ptk.font_menu_bold, B - 40, baseline, sc, txt);
	if (popup_has_sub(i)) {
		/* flèche pleine 6×11, pointe à 7 px du bord noir */
		int r;
		for (r = 0; r < 11; r++)
			ptk_fill(&ptk, popup.win, B - 12, y + 2 + r, r <= 5 ? r + 1 : 11 - r, 1, txt);
	}
	XFlush(dpy);
}

static void
popup_draw(void)
{
	int i, B = popup.w - 2, bot = popup.h - 2, t = popup.top;
	XRectangle forme[3] = {
		{ 0, 0, (unsigned short)(popup.w - 1), (unsigned short)(popup.h - 1) },
		{ (short)(popup.w - 1), (short)(t + 1), 1, (unsigned short)(popup.h - t - 1) },
		{ 2, (short)(popup.h - 1), (unsigned short)(popup.w - 2), 1 },
	};

	Window reel = popup.win;

	XShapeCombineRectangles(dpy, reel, ShapeBounding, 0, 0, forme, 3,
	    ShapeSet, Unsorted);
	if (popup.buf == None)
		popup.buf = XCreatePixmap(dpy, reel, (unsigned)popup.w, (unsigned)popup.h,
		    (unsigned)DefaultDepth(dpy, screen));
	popup.win = popup.buf;
	ptk_fill(&ptk, popup.win, 0, 0, popup.w, popup.h, ptk.black);
	ptk_fill(&ptk, popup.win, B + 1, t + 1, 1, popup.h - t - 1, ptk_color(&ptk, "#222222"));
	ptk_fill(&ptk, popup.win, 2, bot + 1, popup.w - 2, 1, ptk_color(&ptk, "#222222"));
	for (i = 0; i < popup.count; i++)
		popup_draw_row(i);
	popup.win = reel;
	XCopyArea(dpy, popup.buf, reel, ptk.gc, 0, 0, (unsigned)popup.w, (unsigned)popup.h, 0, 0);
	XFlush(dpy);
}

/* largeur et hauteur d'après le contenu (fenêtre = menu + bord noir + ombre) */
static void
popup_measure(int *pw, int *ph)
{
	int i, big = 0, tmax = 0, h = popup.top + 2;

	for (i = 0; i < popup.count; i++) {
		char label[ITEM_LEN], sc[32];
		const char *l;
		int tw;

		h += popup_row_h(i);
		if (popup_is_sep(i))
			continue;
		split_item(popup.items[i], label, sizeof(label), sc, sizeof(sc));
		l = label;
		if (*l == '~')
			l++;
		if (strncmp(l, "\xe2\x88\x9a ", 4) == 0)
			l += 4;
		tw = ptk_text_width(&ptk, ptk.font_menu_bold, l) +
		    (popup.items[i][0] == '\x02' ? 40 : 21);
		if (tw > tmax)
			tmax = tw;
		if (sc[0] != '\0' || popup_has_sub(i))
			big = 1;
	}
	/* bord noir : 12 px après le texte ; 48 s'il y a un raccourci ou une flèche */
	tmax += big ? 48 : 12;
	if (tmax < 80)
		tmax = 80;
	if (tmax > sw - 4)
		tmax = sw - 4;
	*pw = tmax + 2;
	*ph = h;
}

/* --- sous-menus : le menu parent reste affiché, sa ligne reste en surbrillance --- */
static void
open_submenu(int idx)
{
	char buf[ITEM_LEN], *p, *q;
	int top, ih, w, h, x, y;
	XSetWindowAttributes a;

	if (have_parent || !popup_has_sub(idx))
		return;
	popup_geom(idx, &top, &ih);
	snprintf(buf, sizeof(buf), "%s", strchr(popup.items[idx], '\x1f') + 1);
	parent_popup = popup;
	have_parent = 1;
	popup.buf = None;
	popup.count = 0;
	popup.selected = -1;
	popup.flash = -1;
	popup.top = 1;
	popup.has_icons = 0;
	popup.ignore_release = 0;
	for (p = buf; p && *p && popup.count < MAX_ITEMS; p = q) {
		q = strchr(p, '\x1e');
		if (q)
			*q++ = '\0';
		snprintf(popup.items[popup.count++], ITEM_LEN, "%s", p);
	}
	popup_measure(&w, &h);
	x = parent_popup.x + parent_popup.w - 5;
	if (x + w > sw)
		x = parent_popup.x - w + 4;
	if (x < 0)
		x = 0;
	y = parent_popup.y + top - 1;
	if (y + h > sh)
		y = sh - h;
	popup.x = x;
	popup.y = y;
	popup.w = w;
	popup.h = h;
	a.override_redirect = True;
	a.background_pixel = ptk.menu.pixel;
	a.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	popup.win = XCreateWindow(dpy, root, x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent,
	    InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	XMapRaised(dpy, popup.win);
	raise_corners();
	XGrabPointer(dpy, popup.win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
	popup_draw();
}

static void
close_submenu(void)
{
	if (!have_parent)
		return;
	XDestroyWindow(dpy, popup.win);
	if (popup.buf != None)
		XFreePixmap(dpy, popup.buf);
	popup = parent_popup;
	have_parent = 0;
	XGrabPointer(dpy, popup.win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
}

static void open_popup(Client *c, int menu_index, int kind, int x);
static void barre_mesures(char *clockbuf, size_t n, int *sep_x, int *clock_x, int *app_x, int *app_tx);

/* titre de la barre sous x : genre (0 menu, 1 Pomme, 2 applications), numéro, position */
static int
titre_sous(int x, int *kind, int *n, int *mx)
{
	MenuModel m;
	char ck[32];
	int i, px = 37, app_x, app_tx, clock_x, sep_x;

	if (x >= 9 && x <= 36) {
		*kind = 1;
		*n = 0;
		*mx = 9;
		return 1;
	}
	parse_model(menu_source(focused), &m);
	for (i = 0; i < m.count; i++) {
		int w = ptk_text_width(&ptk, ptk.font_menu_bold, m.titles[i]) + 14;

		if (x >= px && x < px + w) {
			*kind = 0;
			*n = i;
			*mx = px;
			return 1;
		}
		px += w;
	}
	barre_mesures(ck, sizeof(ck), &sep_x, &clock_x, &app_x, &app_tx);
	if (x >= app_x) {
		*kind = 2;
		*n = 0;
		*mx = app_x;
		return 1;
	}
	return 0;
}

/* suivi du pointeur, en coordonnées d'écran, sur le menu et son sous-menu éventuel */
static void
popup_track(int rx, int ry)
{
	int idx = -1;

	/* pointeur sur la barre : le menu du titre survolé remplace le menu ouvert */
	if (popup.kind != 3 && ry < ptk.menubar_h) {
		int k, n, mx;

		if (have_parent)
			close_submenu();
		if (titre_sous(rx, &k, &n, &mx) && !(k == popup.kind && (k != 0 || n == popup.menu_index))) {
			menu_choisi = 1;	/* pas de son de fermeture en passant d'un menu à l'autre */
			open_popup(k == 0 ? focused : NULL, n, k, mx);
			return;
		}
		if (popup.selected >= 0) {
			int old = popup.selected;

			popup.selected = -1;
			popup_draw_row(old);
		}
		return;
	}

	if (have_parent && !(rx >= popup.x && rx < popup.x + popup.w &&
	    ry >= popup.y && ry < popup.y + popup.h)) {
		if (rx >= parent_popup.x && rx < parent_popup.x + parent_popup.w &&
		    ry >= parent_popup.y && ry < parent_popup.y + parent_popup.h) {
			Popup sub = popup;
			int pi;

			popup = parent_popup;
			pi = popup_hit(ry - popup.y);
			popup = sub;
			if (pi != parent_popup.selected)
				close_submenu();
			else
				return;
		} else {
			int old = popup.selected;
			popup.selected = -1;
			popup_draw_row(old);
			return;
		}
	}
	if (rx >= popup.x && rx < popup.x + popup.w - 1 && ry >= popup.y && ry < popup.y + popup.h - 1)
		idx = popup_hit(ry - popup.y);
	if (idx != popup.selected) {
		int old = popup.selected;

		popup.selected = idx;
		popup_draw_row(old);
		popup_draw_row(idx);
		if (idx >= 0) {
			popup.entre = 1;
			ptk_son(&ptk, "mnui");
		}
		if (popup_has_sub(idx))
			open_submenu(idx);
	}
}

static int
popup_just_opened(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (now.tv_sec - popup.open_tv.tv_sec) * 1000 +
	    (now.tv_usec - popup.open_tv.tv_usec) / 1000 < 150;
}

static void
close_popup(void)
{
	if (!popup_open)
		return;
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	if (popup.win != None)
		XDestroyWindow(dpy, popup.win);
	if (popup.buf != None)
		XFreePixmap(dpy, popup.buf);
	popup.buf = None;
	if (have_parent) {
		XDestroyWindow(dpy, parent_popup.win);
		if (parent_popup.buf != None)
			XFreePixmap(dpy, parent_popup.buf);
		have_parent = 0;
	}
	popup.win = None;
	popup_open = 0;
	if (!menu_choisi)
		ptk_son(&ptk, "mnuc");
	menu_choisi = 0;
	draw_menubar();
}

static void
open_popup(Client *c, int menu_index, int kind, int x)
{
	MenuModel m;
	int i, w = 120, h;
	XSetWindowAttributes a;

	close_popup();
	memset(&popup, 0, sizeof(popup));
	gettimeofday(&popup.open_tv, NULL);
	popup.selected = -1;
	popup.flash = -1;	/* sans quoi la 1re ligne ne se surligne jamais */
	popup.has_icons = (kind == 1);
	popup.kind = kind;
	popup.client = c;
	popup.menu_index = menu_index;
	{
		Window rr, cr;
		int wx, wy;
		unsigned mask;

		if (!XQueryPointer(dpy, root, &rr, &cr, &popup.open_rx, &popup.open_ry, &wx, &wy, &mask))
			popup.open_rx = popup.open_ry = -1000;
	}

	if (kind == 1) {
		apple_model(&m);
		for (i = 0; i < m.n_items[0]; i++)
			snprintf(popup.items[i], ITEM_LEN, "%s", m.items[0][i]);
		popup.count = m.n_items[0];
	} else if (kind == 2) {
		Client *cc;
		popup.count = 0;
		for (cc = clients; cc && popup.count < MAX_ITEMS; cc = cc->next) {
			if (!cc->mapped || cc->shaded)
				continue;
			snprintf(popup.items[popup.count], ITEM_LEN, "%.*s",
			    ITEM_LEN - 1, client_app_name(cc));
			popup.count++;
		}
		if (popup.count == 0)
			return;
	} else {
		parse_model(menu_source(c), &m);
		if (menu_index < 0 || menu_index >= m.count)
			return;
		for (i = 0; i < m.n_items[menu_index]; i++)
			snprintf(popup.items[i], ITEM_LEN, "%s",
			    m.items[menu_index][i]);
		popup.count = m.n_items[menu_index];
	}

	popup.top = 0;
	popup_measure(&w, &h);
	if (x + w > sw)
		x = sw - w;
	if (x < 0)
		x = 0;
	popup.x = x;
	popup.y = ptk.menubar_h;
	popup.w = w;
	popup.h = h;

	a.override_redirect = True;
	a.background_pixel = ptk.menu.pixel;
	a.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask;
	popup.win = XCreateWindow(dpy, root, popup.x, popup.y, (unsigned)w,
	    (unsigned)h, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	popup_open = 1;
	popup.ignore_release = 1;
	ptk_son(&ptk, "mnuo");
	XMapRaised(dpy, popup.win);
	raise_corners();
	XGrabPointer(dpy, popup.win, False,
	    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
	XGrabKeyboard(dpy, popup.win, False, GrabModeAsync, GrabModeAsync,
	    CurrentTime);
	popup_draw();
	draw_menubar();
}

static void
open_context(Client *c, int x, int y, const char *raw, int bw, int bh, int cour)
{
	char buf[4096];
	char *line, *save;
	int i, w = 120, h;

	close_popup();
	memset(&popup, 0, sizeof(popup));
	gettimeofday(&popup.open_tv, NULL);
	popup.selected = -1;
	popup.flash = -1;	/* sans quoi la 1re ligne s'affiche comme si elle clignotait */
	popup.ignore_release = 1;
	popup.kind = 3;
	popup.client = c;
	popup.has_icons = 0;

	snprintf(buf, sizeof(buf), "%s", raw);
	for (line = strtok_r(buf, "\n", &save); line != NULL;
	    line = strtok_r(NULL, "\n", &save)) {
		if (popup.count >= MAX_ITEMS)
			break;
		if (*line == '\0')
			continue;
		snprintf(popup.items[popup.count], ITEM_LEN, "%s", line);
		popup.count++;
	}
	if (popup.count == 0)
		return;
	popup.top = 1;
	popup_measure(&w, &h);
	if (bw > 0) {
		/* menu local OS 9 : bord gauche sur celui du bouton, au moins aussi large que lui,
		 * l'élément courant posé sur le bouton (sinon le menu s'ouvre sous le bouton) */
		int iy, ih;

		/* relevé OS 9.2.1 : le menu (bord noir compris) s'arrête au séparateur des flèches,
		 * qui restent visibles ; l'ombre d'1 px tombe sur elles. Ligne courante à y+1. */
		if (w < bw - 19)
			w = bw - 19;
		if (cour >= 0 && cour < popup.count) {
			popup_geom(cour, &iy, &ih);
			y = y + 1 - iy;
		} else {
			y += bh;
		}
	} else {
		/* menu contextuel : bascule à gauche/au-dessus du clic */
		if (x + w > sw)
			x = x - w;
		if (y + h > sh)
			y = y - h;
	}
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x + w > sw)
		x = sw - w;
	if (y + h > sh)
		y = sh - h;
	popup.x = x;
	popup.y = y;
	popup.w = w;
	popup.h = h;
	{
		Window rr, cr;
		int wx, wy, mx, my;
		unsigned mask;

		if (XQueryPointer(dpy, root, &rr, &cr, &mx, &my, &wx, &wy,
		    &mask)) {
			popup.open_rx = mx;
			popup.open_ry = my;
		} else {
			popup.open_rx = -1000;
			popup.open_ry = -1000;
		}
	}

	{
		XSetWindowAttributes a;
		a.override_redirect = True;
		a.background_pixel = ptk.menu.pixel;
		a.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
		    ButtonReleaseMask | PointerMotionMask;
		popup.win = XCreateWindow(dpy, root, popup.x, popup.y,
		    (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
		    CopyFromParent, CWOverrideRedirect | CWBackPixel |
		    CWEventMask, &a);
	}
	popup_open = 1;
	ptk_son(&ptk, "mnuo");
	XMapRaised(dpy, popup.win);
	raise_corners();
	XGrabPointer(dpy, popup.win, False,
	    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
	XGrabKeyboard(dpy, popup.win, False, GrabModeAsync, GrabModeAsync,
	    CurrentTime);
	popup_draw();
}

static void
menubar_click(int x)
{
	MenuModel m;
	int i, mx = 37;
	char ck[32];
	int app_x, app_tx, clock_x, sep_x;

	if (x >= 9 && x <= 36) {
		open_popup(NULL, 0, 1, 9);
		return;
	}

	parse_model(menu_source(focused), &m);

	for (i = 0; i < m.count; i++) {
		int w = ptk_text_width(&ptk, ptk.font_menu_bold,
		    m.titles[i]) + 14;
		if (x >= mx && x < mx + w) {
			open_popup(focused, i, 0, mx);
			return;
		}
		mx += w;
	}

	barre_mesures(ck, sizeof(ck), &sep_x, &clock_x, &app_x, &app_tx);
	if (x >= app_x) {
		open_popup(NULL, 0, 2, app_x);
		return;
	}
	if (x >= clock_x - 2 && x < sep_x) {
		/* comme Mac OS 9 : le clic montre la date, l'heure revient seule après 3,2 s (relevé sur la VM) */
		struct timeval n;

		gettimeofday(&n, NULL);
		horloge_date = 1;
		horloge_fin = n;
		horloge_fin.tv_sec += 3;
		horloge_fin.tv_usec += 200000;
		if (horloge_fin.tv_usec >= 1000000) {
			horloge_fin.tv_sec++;
			horloge_fin.tv_usec -= 1000000;
		}
		draw_menubar();
	}
}

/* cases de la barre de titre : 0 fermer, 1 zoom, 2 repli ; -1 ailleurs */
static int case_suivie = -1, case_dedans;
static Client *case_client;

static int
case_titre(int fw, int x, int y)
{
	if (y < 4 || y > 16)
		return -1;
	if (x >= 4 && x <= 16)
		return 0;
	if (x >= fw - 33 && x <= fw - 21)
		return 1;
	if (x >= fw - 17 && x <= fw - 5)
		return 2;
	return -1;
}

static void
dessiner_case(Client *c, int bx, int enfonce)
{
	int fw = c->w + 2 * c->bw;
	int x = bx == 0 ? 4 : bx == 1 ? fw - 33 : fw - 17;

	if (c->style_note) {
		int cx = bx == 0 ? 4 : fw - 12;

		ptk_fill(&ptk, c->frame, cx + 1, 3, 6, 6, enfonce ? ptk.black : note_teinte(c, 0));
		if (bx == 2 && !enfonce)
			ptk_line(&ptk, c->frame, cx + 1, 5, cx + 6, 5, ptk.black);
		XFlush(dpy);
		return;
	}
	if (c->style_calc) {
		ptk_fill(&ptk, c->frame, 9, 4, 11, 11, enfonce ? ptk.white : ptk.black);
		ptk_rect(&ptk, c->frame, 9, 4, 11, 11, ptk.white);
		XFlush(dpy);
		return;
	}
	ptk_title_button(&ptk, c->frame, x, 4, bx, enfonce);
	XFlush(dpy);
}

/* Icône du titre d'une fenêtre de dossier, à la Mac OS 9 : pressée elle fonce, glissée elle emporte
 * un fantôme du dossier ; au dépôt, le Finder déplace le dossier (_PLATINE_CHEMIN : il en est un). */
static Client *proxy_client;
static int proxy_x0, proxy_y0, proxy_bouge;

static int
icone_du_titre(Client *c, int fw, int x, int y)
{
	char t[256], *chemin;
	int tw, gx;

	if (y < 2 || y > 19 || (chemin = get_prop(c->win, a_platine_chemin, NULL)) == NULL)
		return 0;
	free(chemin);
	ptk_fit_text(&ptk, ptk.font_title, c->name[0] ? c->name : "Sans titre", t, sizeof(t), fw - 90);
	tw = ptk_text_width(&ptk, ptk.font_title, t);
	gx = (fw - (tw + 18)) / 2;
	return x >= gx && x < gx + 16;
}

static PtkFantome proxy_f;

static void
proxy_dessin(Ptk *p, Drawable d, void *user)
{
	static PtkArgb *icone;
	Client *c = user;
	int tw = ptk_text_width(p, p->font_small, c->name);

	/* relevé sur la VM : l'icône selon ses contours, le nom seul dans son cadre gris translucide */
	Pixmap pix, masque;

	client_icon(c, &pix, &masque);
	if (strcmp(c->app, "Finder") != 0 && pix != None) {
		XSetClipMask(dpy, p->gc, masque);
		XSetClipOrigin(dpy, p->gc, 1, 1);
		XCopyArea(dpy, pix, d, p->gc, 0, 0, 16, 16, 1, 1);
		XSetClipMask(dpy, p->gc, None);
	} else {
		if (icone == NULL)
			icone = ptk_load_argb(p, "dossier-16");
		ptk_draw_argb(p, d, icone, 0, 1, 3);
	}
	ptk_fill_alpha(p, d, 18, 2, tw + 6, 14, p->black, 0x6666);
	ptk_text(p, d, p->font_small, 21, 13, c->name, ptk_color(p, "#bbbbbb"));
}

static void
handle_frame_button(Client *c, XButtonEvent *e)
{
	int fw = c->w + 2 * c->bw;

	/* tout clic sur le cadre réactive la fenêtre : y compris celle mise en retrait par un clic sur le bureau */
	if (focused != c || !c->active) {
		focus_client(c);
	}
	if (e->y < c->title_h) {
		/* comme TrackGoAway et TrackBox : la case s'enfonce, suit le pointeur, n'agit
		 * qu'au relâchement dedans */
		{
			int bx = case_titre(fw, e->x, e->y);

			if (bx == 1 && taille_fixe(c))
				bx = -1;
			if (c->style_calc)	/* une seule case, à 9 px du bord */
				bx = e->x >= 9 && e->x < 20 && e->y >= 4 && e->y < 15 ? 0 : -1;
			if (c->style_note)	/* fermer à gauche, replier à droite */
				bx = !c->active || e->y < 2 || e->y > 10 ? -1 :
				    e->x >= 4 && e->x < 12 ? 0 : e->x >= fw - 12 && e->x < fw - 4 ? 2 : -1;
			if (bx >= 0) {
				case_suivie = bx;
				case_client = c;
				case_dedans = 1;
				ptk_son(&ptk, "wclp");
				dessiner_case(c, bx, 1);
				XGrabPointer(dpy, c->frame, False,
				    ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
				    GrabModeAsync, None, cursor, CurrentTime);
				return;
			}
		}
		if (icone_du_titre(c, fw, e->x, e->y)) {
			focus_client(c);
			proxy_client = c;
			proxy_x0 = e->x_root;
			proxy_y0 = e->y_root;
			proxy_bouge = 0;
			c->icone_pressee = 1;
			draw_frame(c);
			XGrabPointer(dpy, c->frame, False, ButtonReleaseMask | PointerMotionMask,
			    GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime);
			return;
		}
		if (e->time - last_click_time < 533 &&
		    last_click_client == c) {
			ptk_son(&ptk, c->shaded ? "wexp" : "wcol");
			toggle_shade(c);
			last_click_time = 0;
			return;
		}
		last_click_time = e->time;
		last_click_client = c;
		drag_mode = 1;
		drag_client = c;
		drag_dx = e->x_root;
		drag_dy = e->y_root;
		drag_x = c->x;
		drag_y = c->y;
		outline_show(c->x, c->y, c->w + 2 * c->bw + 1,
		    c->shaded ? c->title_h + 1 : c->title_h + c->h + c->bw + 1);
		XGrabPointer(dpy, c->frame, False,
		    ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
		    GrabModeAsync, None, cursor, CurrentTime);
		return;
	}
	focus_client(c);
}

static void
handle_event(XEvent *ev)
{
	Client *c;

	switch (ev->type) {
	case MapRequest:
		manage_window(ev->xmaprequest.window);
		break;

	case ConfigureRequest: {
		XConfigureRequestEvent *e = &ev->xconfigurerequest;
		for (c = clients; c; c = c->next)
			if (c->win == e->window)
				break;
		if (c == NULL) {
			XWindowChanges wc;
			wc.x = e->x;
			wc.y = e->y;
			wc.width = e->width;
			wc.height = e->height;
			wc.border_width = e->border_width;
			wc.sibling = e->above;
			wc.stack_mode = e->detail;
			XConfigureWindow(dpy, e->window, (unsigned)e->value_mask,
			    &wc);
			break;
		}
		if (c->shaded)
			break;
		/* demande venue du cadre : x et y y sont relatifs au cadre, seule la taille compte */
		if (e->parent == c->frame)
			e->value_mask &= ~(unsigned long)(CWX | CWY);
		configure_client(c,
		    (e->value_mask & CWX) ? e->x : c->x,
		    (e->value_mask & CWY) ? e->y : c->y,
		    (e->value_mask & CWWidth) ? e->width : c->w,
		    (e->value_mask & CWHeight) ? e->height : c->h);
		break;
	}

	case UnmapNotify:
		for (c = clients; c; c = c->next)
			if (c->win == ev->xunmap.window)
				break;
		if (c == NULL)
			break;
		if (c->ignore_unmap > 0) {
			c->ignore_unmap--;
			break;
		}
		c->mapped = 0;
		XUnmapWindow(dpy, c->frame);
		if (focused == c) {
			focused = NULL;
			focus_next(1);
		}
		draw_menubar();
		break;

	case DestroyNotify:
		unmanage_window(ev->xdestroywindow.window);
		break;

	case PropertyNotify:
		for (c = clients; c; c = c->next)
			if (c->win == ev->xproperty.window)
				break;
		if (c == NULL)
			break;
		if (ev->xproperty.atom == wm_protocols)
			c->take_focus = client_protocol(c, wm_take_focus);
		if (ev->xproperty.atom == a_platine_style && c->style_note) {
			char *st = get_prop(c->win, a_platine_style, NULL);

			if (st != NULL && strncmp(st, "pense-bete:", 11) == 0) {
				snprintf(c->note_couleur, sizeof(c->note_couleur), "%s", st + 11);
				draw_frame(c);
			}
			free(st);
		}
		if (ev->xproperty.atom == XA_WM_NAME ||
		    ev->xproperty.atom == a_net_wm_name ||
		    ev->xproperty.atom == a_platine_app ||
		    ev->xproperty.atom == a_platine_icon ||
		    ev->xproperty.atom == a_platine_menu) {
			update_client_names(c);
			if (c == focused)
				update_shortcuts(c);
			if (c == focused)
				draw_menubar();
			draw_frame(c);
		}
		break;

	case FocusIn:
		/* Ne jamais adopter un focus ici : le WM est le seul à poser
		 * le focus (pression, carte, menu). Adopter les FocusIn
		 * propagés provoquait une guerre S/D/S/D infinie entre
		 * fenêtres du même client (detail=NotifyAncestor). */
		break;

	case ButtonPress:
		if (!popup_open && ev->xbutton.window == menubar) {
			menubar_click(ev->xbutton.x);
			break;
		}
		if (popup_open) {
			/* menu ouvert par un clic : un nouvel appui choisit, change de menu ou ferme */
			int rx = ev->xbutton.x_root, ry = ev->xbutton.y_root;
			int k, n, mx;

			popup.ignore_release = 0;
			if (popup.kind != 3 && ry < ptk.menubar_h) {
				if (titre_sous(rx, &k, &n, &mx) &&
				    !(k == popup.kind && (k != 0 || n == popup.menu_index))) {
					menu_choisi = 1;
					open_popup(k == 0 ? focused : NULL, n, k, mx);
				} else
					close_popup();
				break;
			}
			if ((rx >= popup.x && rx < popup.x + popup.w && ry >= popup.y &&
			    ry < popup.y + popup.h) || (have_parent && rx >= parent_popup.x &&
			    rx < parent_popup.x + parent_popup.w && ry >= parent_popup.y &&
			    ry < parent_popup.y + parent_popup.h)) {
				popup_track(rx, ry);
				break;
			}
			close_popup();
			break;
		}
		for (c = clients; c; c = c->next) {
			if (c->grow == ev->xbutton.window) {
				drag_mode = 2;
				drag_client = c;
				drag_dx = ev->xbutton.x_root;
				drag_dy = ev->xbutton.y_root;
				drag_w = c->w;
				drag_h = c->h;
				drag_new_w = c->w;
				drag_new_h = c->h;
				outline_show(c->x, c->y, c->w + 2 * c->bw + 1,
				    c->title_h + c->h + c->bw + 1);
				XGrabPointer(dpy, c->grow, False,
				    ButtonReleaseMask | PointerMotionMask,
				    GrabModeAsync, GrabModeAsync, None,
				    cursor, CurrentTime);
				break;
			}
			if (c->win == ev->xbutton.window ||
			    c->win == ev->xbutton.subwindow) {
				/*
				 * Rejouer le clic avant le redessin de la barre :
				 * sinon le bouton est déjà relâché et le client
				 * ne reçoit plus le ButtonPress.
				 */
				XAllowEvents(dpy, ReplayPointer, CurrentTime);
				XSync(dpy, False);
				focus_client(c);
				break;
			}
			if (c->frame == ev->xbutton.window) {
				handle_frame_button(c, &ev->xbutton);
				break;
			}
		}
		break;

	case ButtonRelease:
		if (case_suivie >= 0) {
			Client *cs = case_client;
			int bx = case_suivie, dedans = case_dedans;

			case_suivie = -1;
			case_client = NULL;
			XUngrabPointer(dpy, CurrentTime);
			if (dedans) {
				dessiner_case(cs, bx, 0);
				ptk_son(&ptk, "wclr");
				if (bx == 0) {
					ptk_son(&ptk, "wcls");
					close_client(cs);
				} else if (bx == 1) {
					if (!taille_fixe(cs))
						ptk_son(&ptk, cs->maxed ? "wzmi" : "wzmo");
					toggle_zoom(cs);
				} else {
					ptk_son(&ptk, cs->shaded ? "wexp" : "wcol");
					toggle_shade(cs);
				}
			}
			break;
		}
		if (proxy_client != NULL) {
			Client *pc = proxy_client;

			proxy_client = NULL;
			XUngrabPointer(dpy, CurrentTime);
			pc->icone_pressee = 0;
			draw_frame(pc);
			ptk_fantome_fin(&ptk, &proxy_f);
			if (proxy_bouge) {
				char cmd[64];

				char tt[256];
				int fwp = pc->w + 2 * pc->bw, twp;

				/* position de l'icône du titre : le contour y reviendra si le dépôt est refusé */
				ptk_fit_text(&ptk, ptk.font_title, pc->name[0] ? pc->name : "Sans titre", tt, sizeof(tt), fwp - 90);
				twp = ptk_text_width(&ptk, ptk.font_title, tt);
				snprintf(cmd, sizeof(cmd), "__depot %d %d %d %d", ev->xbutton.x_root, ev->xbutton.y_root,
				    pc->x + (fwp - (twp + 18)) / 2, pc->y + 2);
				XSync(dpy, False);
				if (strcmp(pc->app, "Finder") == 0) {
					send_command_win(pc->win, cmd);
				} else {
					/* autre application : le Finder crée un alias du fichier de la fenêtre là où
					 * l'icône est lâchée (« __alias X Y IX IY\tCHEMIN ») */
					char *chemin = get_prop(pc->win, a_platine_chemin, NULL);
					Window bureau = fenetre_bureau();

					if (chemin != NULL && bureau != None) {
						char *m = malloc(strlen(chemin) + 80);

						if (m != NULL) {
							sprintf(m, "__alias %s\t%s", cmd + 8, chemin);
							send_command_win(bureau, m);
							free(m);
						}
					}
					free(chemin);
				}
			}
			break;
		}
		if (drag_mode != 0) {
			XUngrabPointer(dpy, CurrentTime);
			if (drag_mode == 2 && drag_client != NULL) {
				outline_destroy();
				configure_client(drag_client, drag_client->x, drag_client->y,
				    drag_new_w, drag_new_h);
			}
			if (drag_mode == 1 && drag_client != NULL) {
				outline_destroy();
				ptk_son_stop(son_depl_pid);
				son_depl_pid = 0;
				ptk_son(&ptk, "wmov-release");
			}
			if (drag_client != NULL) {
				if (drag_mode == 1)
					XMoveWindow(dpy,
					    drag_client->frame,
					    drag_client->x,
					    drag_client->y);
				publish_geometry(drag_client);
			}
			outline_reparer();
			drag_mode = 0;
			drag_client = NULL;
		}
		if (popup_open) {
			int rx = ev->xbutton.x_root, ry = ev->xbutton.y_root;
			int dedans = rx >= popup.x && rx < popup.x + popup.w &&
			    ry >= popup.y && ry < popup.y + popup.h;
			int immobile = abs(rx - popup.open_rx) + abs(ry - popup.open_ry) <= 3;
			int premier = popup.ignore_release;

			popup.ignore_release = 0;
			/* appui-relâché sans bouger, ou sur le titre : le menu reste ouvert (clic) */
			if (premier && (immobile || popup_just_opened()))
				break;
			if (popup.kind != 3 && ry < ptk.menubar_h && !popup.entre)
				break;
			if (dedans) {
				popup_activate(popup_hit(ry - popup.y));
				break;
			}
			if (have_parent && rx >= parent_popup.x && rx < parent_popup.x + parent_popup.w &&
			    ry >= parent_popup.y && ry < parent_popup.y + parent_popup.h) {
				/* menu Pomme : un clic sur la ligne d'un dossier l'ouvre, sur une autre la choisit */
				if (parent_popup.kind == 1) {
					close_submenu();
					popup_activate(popup_hit(ry - popup.y));
				}
				break;
			}
			close_popup();
		}
		break;

	case MotionNotify:
		if (proxy_client != NULL) {
			if (abs(ev->xmotion.x_root - proxy_x0) > 3 || abs(ev->xmotion.y_root - proxy_y0) > 3) {
				if (!proxy_bouge) {
					proxy_bouge = 1;
					ptk_fantome_debut(&ptk, &proxy_f, 24 + ptk_text_width(&ptk, ptk.font_small,
					    proxy_client->name), 18);
				}
			}
			if (proxy_bouge)
				ptk_fantome_poser(&ptk, &proxy_f, ev->xmotion.x_root - 9, ev->xmotion.y_root - 9,
				    proxy_dessin, proxy_client);
			break;
		}
		if (case_suivie >= 0) {
			Client *cs = case_client;
			int x = ev->xmotion.x_root - cs->x, y = ev->xmotion.y_root - cs->y;
			int dedans = cs->style_calc ? x >= 9 && x < 20 && y >= 4 && y < 15 :
			    cs->style_note ? y >= 2 && y <= 10 && (case_suivie == 0 ? x >= 4 && x < 12 :
			    x >= cs->w + 2 * cs->bw - 12 && x < cs->w + 2 * cs->bw - 4) :
			    case_titre(cs->w + 2 * cs->bw, x, y) == case_suivie;

			if (dedans != case_dedans) {
				case_dedans = dedans;
				ptk_son(&ptk, dedans ? "wcle" : "wclx");
				dessiner_case(cs, case_suivie, dedans);
			}
			break;
		}
		/* ne traiter que la dernière position : le 701 ne prend pas de retard */
		if (drag_mode != 0 || case_suivie >= 0)
			while (XCheckTypedEvent(dpy, MotionNotify, ev))
				;
		if (drag_mode == 1 && drag_client != NULL) {
			int nx = drag_x + ev->xmotion.x_root - drag_dx;
			int ny = drag_y + ev->xmotion.y_root - drag_dy;
			if (ny < ptk.menubar_h)
				ny = ptk.menubar_h;
			if (nx < 0)
				nx = 0;
			if (nx + drag_client->w + 2 > sw)
				nx = sw - drag_client->w - 2;
			outline_set(nx, ny, drag_client->w + 2 * drag_client->bw + 1,
			    drag_client->shaded ? drag_client->title_h + 1 :
			    drag_client->title_h + drag_client->h + drag_client->bw + 1);
			drag_client->x = nx;
			drag_client->y = ny;
			gettimeofday(&son_depl_t, NULL);
			/* relancé s'il s'est terminé alors que la fenêtre bouge encore */
			if (son_depl_pid <= 0 || kill(son_depl_pid, 0) != 0)
				son_depl_pid = ptk_son(&ptk, "wmov-moving");
		} else if (drag_mode == 2 && drag_client != NULL) {
			int nw = drag_w + ev->xmotion.x_root - drag_dx;
			int nh = drag_h + ev->xmotion.y_root - drag_dy;

			if (nw < 160)
				nw = 160;
			limiter_taille(drag_client, &nw, &nh);
			drag_new_w = nw;
			drag_new_h = nh;
			outline_set(drag_client->x, drag_client->y, nw + 2 * drag_client->bw + 1,
			    drag_client->title_h + nh + drag_client->bw + 1);
		} else if (popup_open) {
			popup_track(ev->xmotion.x_root, ev->xmotion.y_root);
		}
		break;

	case Expose:
		if (ev->xexpose.window == menubar) {
			if (ev->xexpose.count == 0)
				draw_menubar();
		} else if (popup_open && ev->xexpose.window == popup.win) {
			if (ev->xexpose.count == 0)
				popup_draw();
		} else if (popup_open && have_parent && ev->xexpose.window == parent_popup.win) {
			if (ev->xexpose.count == 0) {
				Popup sub = popup;

				popup = parent_popup;
				popup_draw();
				popup = sub;
			}
		} else {
			for (c = clients; c; c = c->next) {
				if (c->grow == ev->xexpose.window) {
					if (ev->xexpose.count == 0) {
						if (c->style_note)
							draw_grow_note(c);
						else
							draw_growbox(&ptk, c->grow, c->active);
					}
					break;
				}
				if (c->frame == ev->xexpose.window) {
					if (ev->xexpose.count == 0)
						draw_frame(c);
					break;
				}
			}
		}
		break;

	case KeyPress: {
		KeySym ks = XLookupKeysym(&ev->xkey, 0);

		if (ks == XK_Escape && (ev->xkey.state & Mod1Mask) && (ev->xkey.state & ControlMask)) {
			if (popup_open)
				close_popup();
			/* rendre le clavier tout de suite : l'alerte doit pouvoir le saisir */
			XUngrabKeyboard(dpy, ev->xkey.time);
			XFlush(dpy);
			forcer_quitter();
			break;
		}
		if (!popup_open) {
			int i;

			for (i = 0; i < n_shortcuts; i++)
				if (shortcuts[i].code ==
				    ev->xkey.keycode) {
					send_command(focused,
					    shortcuts[i].cmd);
					break;
				}
			break;
		}
		if (ks == XK_Escape) {
			if (have_parent)
				close_submenu();
			else
				close_popup();
		} else if (ks == XK_Right && popup_has_sub(popup.selected)) {
			open_submenu(popup.selected);
			popup_move(1);
		} else if (ks == XK_Left && have_parent) {
			close_submenu();
		} else if (ks == XK_Up) {
			popup_move(-1);
		} else if (ks == XK_Down) {
			popup_move(1);
		} else if (ks == XK_Home) {
			popup.selected = -1;
			popup_move(1);
		} else if (ks == XK_End) {
			popup.selected = popup.count;
			popup_move(-1);
		} else if (ks == XK_Return || ks == XK_KP_Enter) {
			if (popup.selected < 0)
				popup_move(1);
			if (popup_has_sub(popup.selected)) {
				open_submenu(popup.selected);
				popup_move(1);
			} else
				popup_activate(popup.selected);
		}
		break;
	}

	case ClientMessage:
		/* _NET_ACTIVE_WINDOW (EWMH) : une application déjà ouverte qu'on relance
		 * repasse au premier plan, comme sous Mac OS 9 */
		if (ev->xclient.message_type == XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False)) {
			for (c = clients; c; c = c->next)
				if (c->win == ev->xclient.window)
					break;
			if (c != NULL) {
				if (c->shaded)
					toggle_shade(c);
				focus_client(c);
				draw_menubar();
			}
			break;
		}
		if (ev->xclient.message_type == a_platine_arret) {
			long m = ev->xclient.data.l[0];

			arret_demarrer(m == 1 || m == 2 ? (int)m : 0, ev->xclient.data.l[1] != 0);
			break;
		}
		/* clic sur le bureau : comme Mac OS 9, plus aucune fenêtre n'est active. Le Finder
		 * lit _PLATINE_BUREAU pour viser le bureau ; un clic dans une fenêtre le retire. */
		if (ev->xclient.message_type == a_platine_lance) {
			char *n = get_prop(DefaultRootWindow(dpy), a_platine_lance, NULL);

			if (n != NULL) {
				snprintf(lance_nom, sizeof(lance_nom), "%s", n);
				lance_fin = time(NULL) + 4;
				free(n);
				draw_menubar();
			}
			break;
		}
		if (ev->xclient.message_type == a_platine_bureau && focused != NULL) {
			focused->active = 0;
			draw_frame(focused);
			XChangeProperty(dpy, DefaultRootWindow(dpy), a_platine_bureau, XA_CARDINAL, 8,
			    PropModeReplace, (unsigned char *)"1", 1);
			break;
		}
		if (ev->xclient.message_type == a_platine_context) {
			Client *cc;
			Window sender = ev->xclient.window;
			char *raw = NULL;

			for (cc = clients; cc; cc = cc->next) {
				if (cc->win == sender)
					break;
			}
			raw = get_prop(sender, a_platine_context, NULL);
			if (raw != NULL) {
				open_context(cc, (int)ev->xclient.data.l[0],
				    (int)ev->xclient.data.l[1], raw,
				    (int)ev->xclient.data.l[2], (int)ev->xclient.data.l[4],
				    (int)ev->xclient.data.l[3]);
				popup.client_win = sender;
				free(raw);
			}
		}
		break;

	default:
		break;
	}
}

static void
update_clock(void)
{
	time_t now = time(NULL);
	struct tm *tm;
	int minute;

	ptk_fuseau_suivre();
	tm = localtime(&now);
	minute = tm->tm_hour * 60 + tm->tm_min;	/* l'heure compte : un changement de fuseau garde souvent la minute */

	if (lance_nom[0] != '\0' && now >= lance_fin) {
		lance_nom[0] = '\0';	/* l'application n'a pas ouvert de fenêtre : on rend la barre */
		draw_menubar();
	}
	if (horloge_date) {
		struct timeval n;

		gettimeofday(&n, NULL);
		if (n.tv_sec > horloge_fin.tv_sec ||
		    (n.tv_sec == horloge_fin.tv_sec && n.tv_usec >= horloge_fin.tv_usec)) {
			horloge_date = 0;
			draw_menubar();
		}
	}
	if (now - last_clock >= 1) {
		int sec = reglage_int("horloge_secondes", 0);

		last_clock = now;
		/* Redessiner à la seconde si demandé, sinon quand la minute change. */
		static int last_minute = -1, last_sec = -1;

		if (sec) {
			if (tm->tm_sec != last_sec) {
				last_sec = tm->tm_sec;
				draw_menubar();
			}
		} else if (minute != last_minute) {
			last_minute = minute;
			draw_menubar();
		}
	}
}

int
main(void)
{
	XSetWindowAttributes a;
	XEvent ev;
	int fd;
	fd_set fds;
	struct timeval tv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "platine-wm: impossible d'ouvrir le display\n");
		return 1;
	}
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	XSetErrorHandler(xerror);
	XSetIOErrorHandler(xioerror);

	if (ptk_init(&ptk, dpy, screen) != 0) {
		fprintf(stderr, "platine-wm: échec de libptk\n");
		return 1;
	}
	gc = ptk.gc;
	cursor = XCreateFontCursor(dpy, XC_left_ptr);

	wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wm_state = XInternAtom(dpy, "WM_STATE", False);
	wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	a_platine_menu = XInternAtom(dpy, "_PLATINE_MENU", False);
	a_platine_arret = XInternAtom(dpy, "_PLATINE_ARRET", False);
	a_platine_app = XInternAtom(dpy, "_PLATINE_APP_NAME", False);
	a_platine_command = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_platine_icon = XInternAtom(dpy, "_PLATINE_ICON", False);
	a_platine_context = XInternAtom(dpy, "_PLATINE_CONTEXT", False);
	a_platine_bureau = XInternAtom(dpy, "_PLATINE_BUREAU", False);
	a_platine_chemin = XInternAtom(dpy, "_PLATINE_CHEMIN", False);
	a_platine_lance = XInternAtom(dpy, "_PLATINE_LANCE", False);
	a_platine_style = XInternAtom(dpy, "_PLATINE_STYLE", False);
	a_platine_geometry = XInternAtom(dpy, "_PLATINE_GEOMETRY", False);
	a_net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);

	XSelectInput(dpy, root, SubstructureRedirectMask |
	    SubstructureNotifyMask | KeyPressMask | ButtonPressMask);
	XDefineCursor(dpy, root, cursor);

	ptk_set_root_pattern(&ptk);
	ptk_load_xpm(&ptk, "tux-11x15.xpm", &pix_tux, &pix_tux_mask);
	ptk_load_xpm(&ptk, "app-finder-16.xpm", &pix_app16, &pix_app16_mask);
	{
		int i;
		char n[64];

		for (i = 0; i < NICONES_APP; i++) {
			snprintf(n, sizeof(n), "menu/%s.xpm", icones_app[i].nom);
			if (ptk_load_xpm(&ptk, n, &icones_app[i].pix,
			    &icones_app[i].mask) != 0)
				icones_app[i].pix = None;
		}
	}
	ptk_load_xpm(&ptk, "dossier-16.xpm", &pix_folder16,
	    &pix_folder16_mask);
	ptk_load_xpm(&ptk, "document-texte-16.xpm", &pix_doc16,
	    &pix_doc16_mask);
	ptk_load_xpm(&ptk, "disque-16.xpm", &pix_disk16, &pix_disk16_mask);
	if (ptk_load_xpm(&ptk, "pomme.xpm", &pix_pomme, &pix_pomme_mask) != 0)
		pix_pomme = None;
	ptk_load_xpm(&ptk, "corbeille-16.xpm", &pix_trash16,
	    &pix_trash16_mask);

	a.override_redirect = True;
	a.background_pixel = ptk.menu.pixel;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask;
	menubar = XCreateWindow(dpy, root, 0, 0, (unsigned)sw,
	    (unsigned)ptk.menubar_h, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask,
	    &a);
	XSetWindowBorderWidth(dpy, menubar, 0);
	XMapRaised(dpy, menubar);
	XDefineCursor(dpy, menubar, cursor);
	create_corners();

	grab_forcer();
	/* Pas de Alt+Tab : Mac OS 9 utilise le menu d'application. */
	XSync(dpy, False);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	draw_menubar();
	XFlush(dpy);

	ptk_ramasser_enfants();	/* pas SIG_IGN : hérité par les programmes lancés */
	fd = ConnectionNumber(dpy);
	while (running) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			handle_event(&ev);
		}
		if (son_depl_pid > 0) {
			/* plus de mouvement depuis 120 ms : le son de glissement s'arrête */
			struct timeval n;

			gettimeofday(&n, NULL);
			if ((n.tv_sec - son_depl_t.tv_sec) * 1000 +
			    (n.tv_usec - son_depl_t.tv_usec) / 1000 > 120) {
				ptk_son_stop(son_depl_pid);
				son_depl_pid = 0;
			}
		}
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		/* la date de l'horloge doit disparaître à l'heure dite, pas à la seconde suivante */
		tv.tv_sec = son_depl_pid > 0 || horloge_date || lance_nom[0] ? 0 : 1;
		tv.tv_usec = son_depl_pid > 0 ? 60000 : horloge_date || lance_nom[0] ? 100000 : 0;
		if (select(fd + 1, &fds, NULL, NULL, &tv) < 0 &&
		    errno != EINTR)
			break;
		update_clock();
		arret_suivre();
	}

	close_popup();
	for (Client *c = clients; c; c = c->next) {
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XReparentWindow(dpy, c->win, root, c->x, c->y);
		XMapWindow(dpy, c->win);
		XDestroyWindow(dpy, c->frame);
	}
	/* rien à délier : pas de raccourci global */
	XDestroyWindow(dpy, menubar);
	ptk_fini(&ptk);
	XCloseDisplay(dpy);
	return 0;
}
