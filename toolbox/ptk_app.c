#include "ptk_app.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static void
set_utf8(PtkApp *a, Window w, Atom prop, const char *value)
{
	XChangeProperty(a->dpy, w, prop, XA_STRING, 8, PropModeReplace,
	    (const unsigned char *)value, (int)strlen(value));
}

static char *
get_prop(PtkApp *a, Window w, Atom prop)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	char *out = NULL;

	if (XGetWindowProperty(a->dpy, w, prop, 0, 8192, False,
	    AnyPropertyType, &type, &format, &n, &after, &data) == Success &&
	    data != NULL) {
		out = malloc(n + 1);
		if (out) {
			memcpy(out, data, n);
			out[n] = '\0';
		}
		XFree(data);
	}
	return out;
}

int
ptk_app_init(PtkApp *app, const char *name, const char *icon)
{
	memset(app, 0, sizeof(*app));
	app->dpy = XOpenDisplay(NULL);
	if (app->dpy == NULL)
		return -1;
	app->screen = DefaultScreen(app->dpy);
	app->root = RootWindow(app->dpy, app->screen);
	if (ptk_init(&app->ptk, app->dpy, app->screen) != 0)
		return -1;
	app->a_menu = XInternAtom(app->dpy, "_PLATINE_MENU", False);
	app->a_app = XInternAtom(app->dpy, "_PLATINE_APP_NAME", False);
	app->a_icon = XInternAtom(app->dpy, "_PLATINE_ICON", False);
	if (getenv("PLATINE_DOCUMENT") != NULL) {
		const char *e = getenv("PLATINE_DOCUMENT");
		struct stat st;

		/* le Finder passe le fichier double-cliqué : pour un programme, c'est le
		 * programme lui-même (ou son alias, suivi par stat) — l'icône de titre, pas un
		 * document à ouvrir ; sinon, c'est un document */
		if (stat(e, &st) == 0 && S_ISREG(st.st_mode) && access(e, X_OK) == 0)
			snprintf(app->lanceur, sizeof(app->lanceur), "%s", e);
		else
			snprintf(app->document, sizeof(app->document), "%s", e);
		unsetenv("PLATINE_DOCUMENT");	/* pas transmis aux programmes lancés ensuite */
	}
	app->a_cmd = XInternAtom(app->dpy, "_PLATINE_COMMAND", False);
	app->a_context = XInternAtom(app->dpy, "_PLATINE_CONTEXT", False);
	app->a_protocols = XInternAtom(app->dpy, "WM_PROTOCOLS", False);
	app->a_delete = XInternAtom(app->dpy, "WM_DELETE_WINDOW", False);
	app->a_clip = XInternAtom(app->dpy, "CLIPBOARD", False);
	app->a_utf8 = XInternAtom(app->dpy, "UTF8_STRING", False);
	app->a_targets = XInternAtom(app->dpy, "TARGETS", False);
	app->name = name;
	app->icon = icon;
	app->running = 1;
	app->quit_on_last_close = 1;
	app->fd_ext = -1;
	{
		char path[PATH_MAX], file[128];

		snprintf(file, sizeof(file), "%s-32.xpm",
		    icon ? icon : "app");
		if (snprintf(path, sizeof(path), "%s/icones/%s",
		    app->ptk.assets_dir, file) < (int)sizeof(path) &&
		    access(path, R_OK) == 0 &&
		    ptk_load_xpm(&app->ptk, file, &app->icon_pix,
		    &app->icon_mask) == 0) {
			app->icon_w = 32;
			app->icon_h = 32;
		}
	}
	return 0;
}

static int
text_wrap_lines(Ptk *p, XftFont *font, const char *text, int maxw)
{
	char buf[1024], *word, *save;
	int lines = 1, x = 0;

	if (text == NULL || *text == '\0')
		return 0;
	snprintf(buf, sizeof(buf), "%s", text);
	for (word = strtok_r(buf, " ", &save); word != NULL;
	    word = strtok_r(NULL, " ", &save)) {
		int ww = ptk_text_width(p, font, word);
		if (x > 0 && x + 4 + ww > maxw) {
			lines++;
			x = ww;
		} else {
			x += (x > 0 ? 4 : 0) + ww;
		}
	}
	return lines;
}

static int
draw_wrapped(Ptk *p, Drawable d, XftFont *font, int x, int y, int maxw,
    const char *text, int lineh, PtkColor color)
{
	char buf[1024], *word, *save;
	char line[1024];
	int lw = 0;

	if (text == NULL || *text == '\0')
		return y;
	snprintf(buf, sizeof(buf), "%s", text);
	line[0] = '\0';
	for (word = strtok_r(buf, " ", &save); word != NULL;
	    word = strtok_r(NULL, " ", &save)) {
		int ww = ptk_text_width(p, font, word);
		if (lw > 0 && lw + 4 + ww > maxw) {
			ptk_text(p, d, font, x, y, line, color);
			y += lineh;
			snprintf(line, sizeof(line), "%s", word);
			lw = ww;
		} else {
			if (lw > 0) {
				strncat(line, " ", sizeof(line) -
				    strlen(line) - 1);
				lw += 4;
			}
			strncat(line, word, sizeof(line) - strlen(line) - 1);
			lw += ww;
		}
	}
	if (line[0] != '\0')
		ptk_text(p, d, font, x, y, line, color);
	return y + lineh;
}

static void
free_controls(PtkControl *c)
{
	while (c) {
		PtkControl *n = c->next;
		free(c);
		c = n;
	}
}

static void
window_remove(PtkWindow *w)
{
	PtkWindow **pp;

	for (pp = &w->app->windows; *pp; pp = &(*pp)->next) {
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	}
	if (w->app->active == w)
		w->app->active = w->app->windows;
}

static void
commit_text_focus(PtkWindow *w)
{
	if (w->focus != NULL &&
	    (w->focus->kind == PTK_TEXTFIELD ||
	    w->focus->kind == PTK_TEXTAREA) &&
	    w->focus->action != NULL)
		w->focus->action(w, w->focus, w->focus->user);
}

static void window_draw(PtkWindow *w);
static void scrollbar_pas(PtkWindow *w, PtkControl *c, const PtkScrollbar *s, int zone);
static void repetition_armer(PtkWindow *w, PtkControl *c, int x, int y);
static void repetition_arreter(PtkApp *app);

static void
focus_control(PtkWindow *w, PtkControl *c)
{
	if (w->focus == c)
		return;
	commit_text_focus(w);
	if (w->focus != NULL)
		w->focus->focused = 0;
	w->focus = c;
	if (c != NULL) {
		c->focused = 1;
		c->caret_on = 1;
		if (c->kind == PTK_TEXTFIELD)
			c->tout_selectionne = 1;
	}
}

static void
focus_suivant(PtkWindow *w, int sens)
{
	PtkControl *c = w->focus, *premier = NULL, *candidat = NULL;
	int vu = c == NULL;

	for (PtkControl *o = w->controls; o != NULL; o = o->next) {
		if (!o->visible || !o->enabled || (o->kind != PTK_TEXTFIELD &&
		    o->kind != PTK_TEXTAREA && o->kind != PTK_LISTE))
			continue;
		if (premier == NULL)
			premier = o;
		if (vu && candidat == NULL)
			candidat = o;
		if (o == c)
			vu = 1;
	}
	if (sens < 0) {
		candidat = NULL;
		for (PtkControl *o = w->controls; o != NULL; o = o->next) {
			if (!o->visible || !o->enabled || (o->kind != PTK_TEXTFIELD &&
			    o->kind != PTK_TEXTAREA && o->kind != PTK_LISTE))
				continue;
			if (o == c)
				break;
			candidat = o;
		}
		if (candidat == NULL)
			for (PtkControl *o = w->controls; o != NULL; o = o->next)
				if (o->visible && o->enabled && (o->kind == PTK_TEXTFIELD ||
				    o->kind == PTK_TEXTAREA || o->kind == PTK_LISTE))
					candidat = o;
	}
	if (candidat == NULL)
		candidat = premier;
	focus_control(w, candidat);
	window_draw(w);
}

void
ptk_window_close(PtkWindow *w)
{
	Display *dpy;
	PtkApp *app;

	if (w == NULL || w->disposed)
		return;
	app = w->app;
	dpy = app->dpy;
	commit_text_focus(w);
	w->disposed = 1;
	window_remove(w);
	if (w->back != None)
		XFreePixmap(dpy, w->back);
	XDestroyWindow(dpy, w->win);
	free_controls(w->controls);
	free(w);
	XFlush(dpy);
	if (app->windows == NULL && app->quit_on_last_close)
		app->running = 0;
}

PtkWindow *
ptk_window_new(PtkApp *app, const char *title, int x, int y, int w, int h)
{
	PtkWindow *win;
	XSetWindowAttributes a;
	XSizeHints hints;

	win = calloc(1, sizeof(*win));
	if (win == NULL)
		return NULL;
	win->app = app;
	win->p = &app->ptk;
	win->x = x;
	win->y = y;
	win->w = w;
	win->h = h;
	snprintf(win->title, sizeof(win->title), "%s", title);
	snprintf(win->icon, sizeof(win->icon), "%s",
	    app->icon ? app->icon : "app");
	snprintf(win->menu, sizeof(win->menu),
	    "Fichier|Fermer\t⌘W\n"
	    "Édition|Annuler|-|Couper|Copier|Coller\n"
	    "Aide|À propos");

	a.background_pixel = app->ptk.window.pixel;
	a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
	    ButtonReleaseMask | PointerMotionMask | KeyPressMask | KeyReleaseMask |
	    FocusChangeMask | PropertyChangeMask;
	win->win = XCreateWindow(app->dpy, app->root, x, y, (unsigned)w,
	    (unsigned)h, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWBackPixel | CWEventMask, &a);
	XStoreName(app->dpy, win->win, title);
	XSetWMProtocols(app->dpy, win->win, &app->a_delete, 1);
	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | USPosition | PPosition;
	hints.min_width = 200;
	hints.min_height = 120;
	hints.x = x;
	hints.y = y;
	XSetWMNormalHints(app->dpy, win->win, &hints);
	set_utf8(app, win->win, app->a_app,
	    app->name ? app->name : "Application");
	set_utf8(app, win->win, app->a_icon, win->icon);
	set_utf8(app, win->win, app->a_menu, win->menu);
	{
		/* icône de titre saisissable : voir ptk_window_set_document */
		char doc[1100];
		/* dossiers de l'arborescence Macintosh du volume */
		static const char *dossiers[] = { "System/Control Panels",
		    "Applications", "System/Apple Menu Items",
		    "System/Extensions" };
		size_t i;

		doc[0] = '\0';
		if (app->windows == NULL && (app->document[0] != '\0' || app->lanceur[0] != '\0'))
			snprintf(doc, sizeof(doc), "%s", app->document[0] != '\0' ? app->document :
			    app->lanceur);
		for (i = 0; doc[0] == '\0' && i < sizeof(dossiers) / sizeof(dossiers[0]); i++) {
			snprintf(doc, sizeof(doc), "%s/%s/%s", ptk_volume(), dossiers[i], title);
			if (strchr(title, '/') != NULL || title[0] == '\0' || access(doc, F_OK) != 0)
				doc[0] = '\0';
		}
		if (doc[0] != '\0')
			ptk_window_set_document(win, doc);
	}
	win->next = app->windows;
	app->windows = win;
	return win;
}

void
ptk_window_set_document(PtkWindow *w, const char *path)
{
	Atom a = XInternAtom(w->app->dpy, "_PLATINE_CHEMIN", False);

	if (path == NULL || path[0] == '\0')
		XDeleteProperty(w->app->dpy, w->win, a);
	else
		set_utf8(w->app, w->win, a, path);
}

void
ptk_window_set_draw(PtkWindow *w, void (*draw)(PtkWindow *, void *),
    void *user)
{
	w->draw = draw;
	w->user = user;
}

void
ptk_window_set_event(PtkWindow *w,
    void (*event)(PtkWindow *, PtkEvent *, void *), void *user)
{
	w->event = event;
	w->user = user;
}

void
ptk_window_set_command(PtkWindow *w,
    void (*command)(PtkWindow *, const char *, void *), void *user)
{
	w->command = command;
	w->user = user;
}

void
ptk_window_set_menu(PtkWindow *w, const char *menu)
{
	snprintf(w->menu, sizeof(w->menu), "%s", menu);
	set_utf8(w->app, w->win, w->app->a_menu, w->menu);
}

void
ptk_window_set_icon(PtkWindow *w, const char *icon)
{
	snprintf(w->icon, sizeof(w->icon), "%s", icon);
	set_utf8(w->app, w->win, w->app->a_icon, w->icon);
}

void
ptk_window_set_title(PtkWindow *w, const char *title)
{
	snprintf(w->title, sizeof(w->title), "%s", title);
	XStoreName(w->app->dpy, w->win, w->title);
	XFlush(w->app->dpy);
}

void
ptk_window_set_close(PtkWindow *w, int (*close)(PtkWindow *, void *), void *user)
{
	w->close = close;
	w->close_user = user;
}

/* Temps de la Toolbox, en secondes. Mac OS 9 : curseur texte à 32 ticks
 * (GetCaretTime), répétition des contrôles après un court délai puis à
 * chaque passage de TrackControl, bouton clignoté 8 ticks au clavier. */
#define CLIGNOTEMENT 0.533
#define REPETITION_DELAI 0.30
#define REPETITION_PAS 0.05
/* pôle de barbier et flèches de recherche : un cran de trame toutes les
 * 90 ms ; quand rien n'anime, on reconsulte toutes les 500 ms (un instituteur
 * qui apparaît plus tard doit animer) */
#define BARBIER_PAS 0.09
#define BARBIER_CREUX 0.5
#define FLASH_US 133000

static double
horloge(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

/* --- Contrôles de la Toolbox relevés sur Mac OS 9 : groupe, curseur, onglets, liste --- */
static void
grave_trait(Ptk *p, Drawable d, int x0, int y0, int x1, int y1)
{
	/* inactif : #bbb puis #ddd, comme le séparateur */
	ptk_line(p, d, x0, y0, x1, y1, ptk_color(p, p->inactif ? "#bbbbbb" : "#888888"));
	ptk_line(p, d, x0 + 1, y0 + 1, x1 + 1, y1 + 1, p->inactif ? p->face : p->white);
}

/* encre des libellés : #777 pour une fenêtre inactive ou un contrôle désactivé */
static PtkColor
encre(Ptk *p)
{
	return p->inactif ? p->text_inactif : p->text;
}

static void
dessiner_groupe(PtkWindow *w, PtkControl *c)
{
	Ptk *p = w->p;
	XftFont *f = c->police != NULL ? c->police : p->font_menu;
	int tw = c->label[0] ? ptk_text_width(p, f, c->label) : 0;

	if (tw > 0) {
		grave_trait(p, w->surface, c->x, c->y, c->x + 6, c->y);
		grave_trait(p, w->surface, c->x + tw + 12, c->y, c->x + c->w - 1, c->y);
		ptk_text(p, w->surface, f, c->x + 9, c->y + 4, c->label, encre(p));
	} else {
		grave_trait(p, w->surface, c->x, c->y, c->x + c->w - 1, c->y);
	}
	grave_trait(p, w->surface, c->x, c->y + c->h - 1, c->x + c->w - 1, c->y + c->h - 1);
	grave_trait(p, w->surface, c->x, c->y, c->x, c->y + c->h - 1);
	grave_trait(p, w->surface, c->x + c->w - 1, c->y, c->x + c->w - 1, c->y + c->h - 1);
}

static int
curseur_x(const PtkControl *c)
{
	int n = c->max - c->min;

	return c->x + 5 + (n > 0 ? (c->value - c->min) * (c->w - 11) / n : 0);
}

static void
dessiner_curseur(PtkWindow *w, PtkControl *c)
{
	Ptk *p = w->p;
	Drawable d = w->surface;
	int y = c->y + 4, x = curseur_x(c), i, n = c->max - c->min;
	PtkColor c1 = ptk_color(p, "#ccccff"), c2 = ptk_color(p, "#9999ff"), c3 = ptk_color(p, "#6666cc");

	/* rail creusé */
	ptk_fill(p, d, c->x + 1, y, c->w - 2, 5, p->black);
	ptk_fill(p, d, c->x, y + 1, c->w, 3, p->black);
	ptk_fill(p, d, c->x + 1, y + 1, c->w - 2, 3, ptk_color(p, "#aaaaaa"));
	ptk_line(p, d, c->x + 2, y + 5, c->x + c->w - 2, y + 5, p->white);
	if (!c->checked)
		for (i = 0; i <= n && n > 0; i++) {
			int tx = c->x + 5 + i * (c->w - 11) / n;

			ptk_fill(p, d, tx, c->y + 15, 2, 3, p->black);
		}
	/* pouce : 11 px de large, en pointe vers le bas, plat pour un volume */
	{
		int h = c->checked ? 13 : 9;

		ptk_fill(p, d, x - 5, c->y, 11, h, p->black);
		ptk_fill(p, d, x - 4, c->y + 1, 9, h - 2, c2);
		ptk_line(p, d, x - 4, c->y + 1, x + 4, c->y + 1, c1);
		ptk_line(p, d, x - 4, c->y + 1, x - 4, c->y + h - 2, c1);
		ptk_line(p, d, x + 4, c->y + 2, x + 4, c->y + h - 2, c3);
		if (!c->checked)
			for (i = 0; i < 5; i++) {
				ptk_line(p, d, x - 4 + i, c->y + h - 1 + i, x + 4 - i, c->y + h - 1 + i, i == 4 ? p->black : c2);
				ptk_line(p, d, x - 5 + i, c->y + h - 1 + i, x - 5 + i, c->y + h - 1 + i, p->black);
				ptk_line(p, d, x + 5 - i, c->y + h - 1 + i, x + 5 - i, c->y + h - 1 + i, p->black);
			}
		for (i = -2; i <= 2; i += 2)
			ptk_line(p, d, x + i, c->y + 3, x + i, c->y + 3 + (c->checked ? 6 : 4), ptk_color(p, "#333399"));
	}
}

/*
 * Onglets relevés pixel par pixel sur Mac OS 9 (audit/macos9-ref/tableaux/a-*.png, son.png).
 * b = pied gauche de l'onglet, e = dernier pixel noir du sommet = b + largeur du texte + 12,
 * onglet suivant en e + 11. Le trait noir du panneau est 21 px sous le sommet de l'onglet.
 * Avec c->x = -1, les bords noirs du panneau tombent sur le cadre noir de la fenêtre, comme sous OS 9.
 */
#define ONGLET_H 21

static void
onglet_px(Ptk *p, Drawable d, int x, int y, int n, const char *coul)
{
	if (n > 0)
		ptk_fill(p, d, x, y, n, 1, ptk_color(p, coul));
}

static void
onglet_rang(Ptk *p, Drawable d, int b, int e, int y, int o, int sel)
{
	static const signed char dep[ONGLET_H] = { 10, 8, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0 };
	const char *fond = sel ? "#eeeeee" : "#cccccc", *lis = sel ? "#ffffff" : "#dddddd";
	const char *ang = sel ? "#222222" : "#333333", *bis = sel ? "#bbbbbb" : "#aaaaaa";
	int x = b + dep[o], k = (o - 5) / 3, r = (o - 5) % 3;

	/* bord gauche, puis remplissage jusqu'au bord droit */
	switch (o) {
	case 0:
		onglet_px(p, d, x, y, 1, "#222222");
		onglet_px(p, d, x + 1, y, e - x, "#000000");
		return;
	case 1:
		onglet_px(p, d, x, y, 1, "#222222");
		onglet_px(p, d, x + 1, y, 1, "#000000");
		onglet_px(p, d, x + 2, y, 1, bis);
		onglet_px(p, d, x + 3, y, e - x - 3, "#cccccc");
		if (sel)
			onglet_px(p, d, e, y, 1, "#bbbbbb");
		else {
			onglet_px(p, d, e - 1, y, 1, "#bbbbbb");
			onglet_px(p, d, e, y, 1, "#aaaaaa");
		}
		onglet_px(p, d, e + 1, y, 2, "#000000");
		return;
	case 2:
		onglet_px(p, d, x, y, 1, "#222222");
		onglet_px(p, d, x + 1, y, 1, bis);
		onglet_px(p, d, x + 2, y, 1, "#cccccc");
		onglet_px(p, d, x + 3, y, e - x - 2, lis);
		onglet_px(p, d, e + 1, y, 1, "#dddddd");
		onglet_px(p, d, e + 2, y, 1, "#888888");
		onglet_px(p, d, e + 3, y, 1, "#000000");
		return;
	case 3:
		onglet_px(p, d, x, y, 1, "#222222");
		onglet_px(p, d, x + 1, y, 1, bis);
		onglet_px(p, d, x + 2, y, 1, sel ? "#ffffff" : "#cccccc");
		onglet_px(p, d, x + 3, y, 1, lis);
		onglet_px(p, d, x + 4, y, e + 2 - x - 4, fond);
		onglet_px(p, d, e + 2, y, 1, "#bbbbbb");
		onglet_px(p, d, e + 3, y, 1, "#444444");
		onglet_px(p, d, e + 4, y, 1, ang);
		return;
	case 4:
		onglet_px(p, d, x, y, 1, "#000000");
		onglet_px(p, d, x + 1, y, 1, "#cccccc");
		onglet_px(p, d, x + 2, y, 1, lis);
		onglet_px(p, d, x + 3, y, e + 3 - x - 3, fond);
		if (sel)
			onglet_px(p, d, e + 2, y, 1, "#dddddd");
		onglet_px(p, d, e + 3, y, 1, sel ? "#888888" : "#aaaaaa");
		onglet_px(p, d, e + 4, y, 1, "#000000");
		return;
	}
	/* flancs : motif de 3 rangs, décalé d'un pixel à chaque période */
	if (r == 0) {
		int dernier = o == ONGLET_H - 1;

		onglet_px(p, d, x, y, 1, dernier && !sel ? "#000000" : "#222222");
		onglet_px(p, d, x + 1, y, 1, bis);
		onglet_px(p, d, x + 2, y, 1, lis);
		onglet_px(p, d, x + 3, y, e + 3 + k - x - 3, fond);
		onglet_px(p, d, e + 3 + k, y, 1, "#bbbbbb");
		onglet_px(p, d, e + 4 + k, y, 1, "#444444");
		onglet_px(p, d, e + 5 + k, y, 1, dernier ? "#000000" : ang);
	} else {
		onglet_px(p, d, x, y, 1, "#000000");
		onglet_px(p, d, x + 1, y, 1, "#cccccc");
		onglet_px(p, d, x + 2, y, 1, lis);
		onglet_px(p, d, x + 3, y, e + 4 + k - x - 3, fond);
		if (sel) {
			onglet_px(p, d, e + 3 + k, y, 1, "#dddddd");
			onglet_px(p, d, e + 4 + k, y, 1, "#888888");
		} else
			onglet_px(p, d, e + 4 + k, y, 1, r == 1 ? "#888888" : "#aaaaaa");
		onglet_px(p, d, e + 5 + k, y, 1, "#000000");
	}
}

static void
dessiner_onglets(PtkWindow *w, PtkControl *c)
{
	Ptk *p = w->p;
	Drawable d = w->surface;
	char l[sizeof(c->items)], *it, *sv;
	int X = c->x, Y = c->y + ONGLET_H, R = c->x + c->w - 1, B = c->y + c->h - 1;
	int b = c->x + 5, i = 0, bs = -1, es = 0, o;

	/* fenêtre : biseau du fond, au-dessus d'un panneau posé sur le cadre */
	if (c->x < 0) {
		onglet_px(p, d, 0, 0, w->w - 1, "#ffffff");
		ptk_fill(p, d, 0, 0, 1, Y, p->white);
		ptk_fill(p, d, w->w - 1, 1, 1, Y - 1, ptk_color(p, "#aaaaaa"));
	}
	/* panneau */
	ptk_fill(p, d, X + 3, Y + 3, R - X - 5, B - Y - 5, p->window);
	ptk_fill(p, d, X, Y, R - X + 1, 1, p->black);
	ptk_fill(p, d, X, B, R - X + 1, 1, p->black);
	ptk_fill(p, d, X, Y, 1, B - Y + 1, p->black);
	ptk_fill(p, d, R, Y, 1, B - Y + 1, p->black);
	onglet_px(p, d, X + 1, Y + 1, R - X - 2, "#cccccc");
	onglet_px(p, d, R - 1, Y + 1, 1, "#dddddd");
	onglet_px(p, d, X + 2, Y + 2, R - X - 4, "#ffffff");
	onglet_px(p, d, R - 2, Y + 2, 1, "#eeeeee");
	ptk_fill(p, d, X + 1, Y + 2, 1, B - Y - 3, ptk_color(p, "#cccccc"));
	ptk_fill(p, d, X + 2, Y + 3, 1, B - Y - 5, p->white);
	ptk_fill(p, d, R - 1, Y + 2, 1, B - Y - 3, ptk_color(p, "#999999"));
	ptk_fill(p, d, R - 2, Y + 3, 1, B - Y - 5, ptk_color(p, "#bbbbbb"));
	onglet_px(p, d, X + 2, B - 2, R - X - 3, "#bbbbbb");
	onglet_px(p, d, X + 2, B - 2, 1, "#eeeeee");
	onglet_px(p, d, X + 2, B - 1, R - X - 2, "#999999");
	onglet_px(p, d, X + 1, B - 1, 1, "#dddddd");
	snprintf(l, sizeof(l), "%s", c->items);
	for (it = strtok_r(l, "|", &sv); it != NULL; it = strtok_r(NULL, "|", &sv), i++) {
		int tw = ptk_text_width(p, p->font_menu, it), e = b + tw + 12, sel = i == c->value;

		for (o = 0; o < ONGLET_H; o++)
			onglet_rang(p, d, b, e, c->y + o, o, sel);
		ptk_text(p, d, p->font_menu, b + 12, c->y + 15, it, p->text);
		if (sel) {
			bs = b;
			es = e;
		}
		b = e + 11;
	}
	/* l'onglet ouvert se fond dans le panneau */
	if (bs >= 0) {
		onglet_px(p, d, bs + 1, Y, 1, "#cccccc");
		onglet_px(p, d, bs + 2, Y, 1, "#ffffff");
		onglet_px(p, d, bs + 3, Y, es + 8 - bs - 3, "#eeeeee");
		onglet_px(p, d, es + 8, Y, 1, "#dddddd");
		onglet_px(p, d, es + 9, Y, 1, "#bbbbbb");
		onglet_px(p, d, bs + 2, Y + 1, es + 9 - bs - 2, "#eeeeee");
		onglet_px(p, d, bs + 1, Y + 2, es + 10 - bs - 1, "#eeeeee");
	}
}

#define LISTE_RANG 17

/* élément n d'une liste, découpé : icône (« \x02icône\x02 »), inactif (« ~ »),
 * texte ; 0 si l'élément n'existe pas */
static int
liste_element(const PtkControl *c, int n, char *icone, size_t ni, int *inactif,
    char *texte, size_t nt)
{
	const char *p = c->items;
	int i;

	for (i = 0; i < n && p != NULL; i++) {
		p = strchr(p, '|');
		if (p != NULL)
			p++;
	}
	if (p == NULL || (*p == '\0' && n > 0))
		return 0;
	icone[0] = '\0';
	*inactif = 0;
	if (*p == '~') {
		*inactif = 1;
		p++;
	}
	if (*p == '\x02') {
		const char *f = strchr(p + 1, '\x02');
		size_t l = f != NULL ? (size_t)(f - p - 1) : 0;

		if (f != NULL && l < ni) {
			memcpy(icone, p + 1, l);
			icone[l] = '\0';
			p = f + 1;
		}
	}
	{
		size_t l = strcspn(p, "|");

		if (l >= nt)
			l = nt - 1;
		memcpy(texte, p, l);
		texte[l] = '\0';
	}
	return 1;
}

static int
liste_inactif(const PtkControl *c, int n)
{
	char ic[64], t[8];
	int in = 0;

	return liste_element(c, n, ic, sizeof(ic), &in, t, sizeof(t)) && in;
}

static int
liste_nombre(const PtkControl *c)
{
	int n = 0;
	const char *q;

	if (c->items[0] == '\0')
		return 0;
	for (q = c->items; *q != '\0'; q++)
		if (*q == '|')
			n++;
	return n + 1;
}

static void
dessiner_liste(PtkWindow *w, PtkControl *c)
{
	Ptk *p = w->p;
	Drawable d = w->surface;
	XftFont *f = c->police != NULL ? c->police : p->font_small;
	char l[sizeof(c->items)], *it, *sv;
	int ent = c->label[0] ? 19 : 0, n = liste_nombre(c), i = 0, y = c->y + 2 + ent;
	int vis = (c->h - 4 - ent) / LISTE_RANG, xb = c->x + c->w - 17, cx = c->x + c->w - (c->col2 > 0 ? c->col2 : 72);

	/* inactif : pas d'anneau de focus, cadre #888 comme le menu local */
	PtkColor cadre = p->inactif ? ptk_color(p, "#888888") :
	    c->focused ? p->focus : p->black;

	ptk_rect(p, d, c->x, c->y, c->w, c->h, cadre);
	ptk_rect(p, d, c->x + 1, c->y + 1, c->w - 2, c->h - 2,
	    p->inactif || !c->focused ? (p->inactif ? p->face : p->black) : p->focus);
	ptk_fill(p, d, c->x + 2, c->y + 2, c->w - 4, c->h - 4, p->white);
	if (ent) {
		char h1[sizeof(c->label)], *t;

		ptk_fill(p, d, c->x + 2, c->y + 2, cx - c->x - 2, ent - 1, ptk_color(p, "#888888"));
		ptk_fill(p, d, cx, c->y + 2, xb - cx, ent - 1, ptk_color(p, "#cccccc"));
		ptk_line(p, d, c->x + 2, c->y + ent + 1, xb, c->y + ent + 1, p->black);
		snprintf(h1, sizeof(h1), "%s", c->label);
		if ((t = strchr(h1, '\t')) != NULL) {
			*t++ = '\0';
			ptk_text(p, d, p->font_small, cx + 8, c->y + 15, t, p->text);
		}
		ptk_text(p, d, p->font_small, c->x + 30, c->y + 15, h1, p->text);
		ptk_fill(p, d, cx, c->y + ent + 2, xb - cx, c->h - ent - 4, p->window);
		ptk_fill(p, d, c->x + 2, c->y + ent + 2, cx - c->x - 2, c->h - ent - 4, p->face);
	}
	snprintf(l, sizeof(l), "%s", c->items);
	for (it = strtok_r(l, "|", &sv); it != NULL; it = strtok_r(NULL, "|", &sv), i++) {
		char *t2, icone[64] = "";
		int inactif = 0, tx = c->x + (ent ? 30 : 6);

		if (i < c->defil || i >= c->defil + vis)
			continue;
		/* « ~ » : grisé ; « \x02icône\x02 » : icône 16×16 des menus */
		if (*it == '~') {
			inactif = 1;
			it++;
		}
		if (*it == '\x02' && strchr(it + 1, '\x02') != NULL) {
			char *f = strchr(it + 1, '\x02');

			snprintf(icone, sizeof(icone), "%.*s", (int)(f - it - 1), it + 1);
			it = f + 1;
		}
		if ((t2 = strchr(it, '\t')) != NULL)
			*t2++ = '\0';
		if (i == c->value && !inactif) {
			int tw = ptk_text_width(p, f, it);

			if (ent)
				ptk_fill(p, d, c->x + 28, y + 1, tw + 4, LISTE_RANG - 3, p->field_select);
			else
				ptk_fill(p, d, c->x + 2, y, xb - c->x - 2, LISTE_RANG, p->field_select);
		}
		/* l'icône après le fond de sélection, sinon il la recouvre */
		if (icone[0] != '\0') {
			char fic[96];
			Pixmap ip = None, im = None;

			snprintf(fic, sizeof(fic), "menu/%s.xpm", icone);
			if (ptk_load_xpm(p, fic, &ip, &im) == 0 && ip != None) {
				ptk_draw_pixmap(p, d, ip, im, tx - 2, y, 16, 16);
				XFreePixmap(p->dpy, ip);
				if (im != None)
					XFreePixmap(p->dpy, im);
			}
			tx += 18;
		}
		ptk_text(p, d, f, tx, y + 12, it, inactif || p->inactif ? p->text_inactif : p->text);
		if (t2 != NULL) {
			ptk_clip(p, cx, y, xb - cx - 2, LISTE_RANG);
			ptk_text(p, d, f, cx + 8, y + 12, t2, inactif || p->inactif ? p->text_inactif : p->text);
			ptk_unclip(p);
		}
		if (ent)
			ptk_line(p, d, c->x + 2, y + LISTE_RANG - 1, xb - 1, y + LISTE_RANG - 1, p->white);
		y += LISTE_RANG;
	}
	{
		PtkScrollbar s = { 0 };

		s.x = xb;
		s.y = c->y + 2;
		s.w = 16;
		s.h = c->h - 4;
		s.value = c->defil;
		s.max = n - vis;
		s.page = vis;
		s.active = c->enabled && !p->inactif;
		s.pressed = c->pressed ? c->dedans : 0;
		ptk_scrollbar_draw(p, d, &s);
	}
}

/* clic : renvoie 1 si la valeur a changé */
static void
curseur_suivre(PtkWindow *w, PtkControl *c, int x)
{
	int n = c->max - c->min, v;

	if (n <= 0)
		return;
	v = c->min + ((x - c->x - 5) * n + (c->w - 11) / 2) / (c->w - 11);
	if (v < c->min)
		v = c->min;
	if (v > c->max)
		v = c->max;
	if (v != c->value) {
		c->value = v;
		ptk_window_invalidate(w);
		if (c->action != NULL)
			c->action(w, c, c->user);
	}
}

static void
scrollbar_controle(const PtkControl *c, PtkScrollbar *s)
{
	s->x = c->x;
	s->y = c->y;
	s->w = c->w;
	s->h = c->h;
	s->horizontal = c->checked;
	s->value = c->value;
	s->min = c->min;
	s->max = c->max;
	s->page = c->defil;
	s->active = c->enabled;
	s->pressed = c->pressed ? c->dedans : 0;
}

static void
scrollbar_definir(PtkWindow *w, PtkControl *c, int value)
{
	int ancien = c->kind == PTK_LISTE ? c->defil : c->value;

	if (c->kind == PTK_LISTE) {
		int n = liste_nombre(c), ent = c->label[0] ? 19 : 0;
		int vis = (c->h - 4 - ent) / LISTE_RANG;
		int max = n > vis ? n - vis : 0;

		if (value < 0)
			value = 0;
		if (value > max)
			value = max;
		c->defil = value;
	} else {
		if (value < c->min)
			value = c->min;
		if (value > c->max)
			value = c->max;
		c->value = value;
	}
	ptk_window_invalidate(w);
	if (ancien != value && c->kind != PTK_LISTE && c->action != NULL)
		c->action(w, c, c->user);
}

static int
scrollbar_press(PtkWindow *w, PtkControl *c, const PtkScrollbar *s,
    int x, int y)
{
	int zone = ptk_scrollbar_zone(s, x, y);

	if (zone == PTK_SB_RIEN)
		return 0;
	c->pressed = 1;
	c->dedans = zone;
	c->caret = zone;
	if (zone == PTK_SB_POUCE) {
		int p, l;

		ptk_son(w->p, "sbtp");
		ptk_scrollbar_thumb(s, &p, &l);
		c->group = (s->horizontal ? x : y) - p;
	} else {
		if (zone == PTK_SB_RECUL || zone == PTK_SB_AVANCE)
			ptk_son(w->p, "sbap");
		scrollbar_pas(w, c, s, zone);
		repetition_armer(w, c, x, y);
	}
	ptk_window_invalidate(w);
	return 1;
}

/* ascenseur d'un PTK_SCROLLBAR, ou ascenseur vertical d'une PTK_LISTE */
static void
scrollbar_geometrie(const PtkControl *c, PtkScrollbar *s)
{
	memset(s, 0, sizeof(*s));
	if (c->kind == PTK_SCROLLBAR) {
		scrollbar_controle(c, s);
	} else {
		int ent = c->label[0] ? 19 : 0, n = liste_nombre(c);
		int vis = (c->h - 4 - ent) / LISTE_RANG;

		s->x = c->x + c->w - 17;
		s->y = c->y + 2;
		s->w = 16;
		s->h = c->h - 4;
		s->value = c->defil;
		s->max = n - vis;
		s->page = vis;
		s->active = c->enabled;
	}
}

/* un pas d'une zone de l'ascenseur : flèche (1) ou rail (une page) */
static void
scrollbar_pas(PtkWindow *w, PtkControl *c, const PtkScrollbar *s, int zone)
{
	int page = s->page > 0 ? s->page : 1;
	int delta = zone == PTK_SB_RECUL ? -1 : zone == PTK_SB_AVANCE ? 1 :
	    zone == PTK_SB_PAGE_RECUL ? -page : zone == PTK_SB_PAGE_AVANCE ?
	    page : 0;

	if (delta != 0)
		scrollbar_definir(w, c, s->value + delta);
}

/* maintien : le contrôle répète tant que le bouton reste enfoncé */
static void
repetition_armer(PtkWindow *w, PtkControl *c, int x, int y)
{
	w->app->rep_w = w;
	w->app->rep_c = c;
	w->app->rep_t = horloge() + REPETITION_DELAI;
	w->app->rep_x = x;
	w->app->rep_y = y;
}

static void
repetition_arreter(PtkApp *app)
{
	app->rep_c = NULL;
	app->rep_w = NULL;
}

static void
scrollbar_motion(PtkWindow *w, PtkControl *c, int x, int y)
{
	PtkScrollbar s;

	scrollbar_geometrie(c, &s);
	w->app->rep_x = x;
	w->app->rep_y = y;
	if (c->caret == PTK_SB_POUCE) {
		scrollbar_definir(w, c, ptk_scrollbar_value(&s, x, y, c->group));
	} else {
		c->dedans = ptk_scrollbar_zone(&s, x, y) == c->caret ? c->caret : 0;
		ptk_window_invalidate(w);
	}
}

static void
scrollbar_release(PtkWindow *w, PtkControl *c)
{
	if (c->caret == PTK_SB_RECUL || c->caret == PTK_SB_AVANCE)
		ptk_son(w->p, "sbar");
	repetition_arreter(w->app);
	c->pressed = 0;
	c->dedans = 0;
	c->caret = 0;
	c->group = 0;
	ptk_window_invalidate(w);
}

/* petites flèches : moitié haute (1) ou basse (2) sous le point */
static int
fleches_moitie(const PtkControl *c, int x, int y)
{
	if (x < c->x || x >= c->x + PTK_PETITES_FLECHES_L || y < c->y ||
	    y >= c->y + PTK_PETITES_FLECHES_H)
		return 0;
	return y < c->y + PTK_PETITES_FLECHES_H / 2 ? 1 : 2;
}

static void
fleches_pas(PtkWindow *w, PtkControl *c)
{
	int v = c->value + (c->caret == 1 ? 1 : -1);

	if (v < c->min || v > c->max)
		return;
	c->value = v;
	ptk_window_invalidate(w);
	if (c->action != NULL)
		c->action(w, c, c->user);
}

/* une échéance de répétition : flèches et rail d'ascenseur (tant que la
 * zone pressée est sous le pointeur ; le rail s'arrête quand le pouce
 * arrive au pointeur), petites flèches (tant que la moitié pressée l'est) */
static void
repetition(PtkApp *app)
{
	PtkWindow *w = app->rep_w;
	PtkControl *c = app->rep_c;

	if (c == NULL || !c->pressed) {
		repetition_arreter(app);
		return;
	}
	if (c->kind == PTK_PETITES_FLECHES) {
		if (c->dedans)
			fleches_pas(w, c);
	} else {
		PtkScrollbar s;
		int zone;

		scrollbar_geometrie(c, &s);
		zone = ptk_scrollbar_zone(&s, app->rep_x, app->rep_y);
		if (zone == c->caret && zone != PTK_SB_POUCE)
			scrollbar_pas(w, c, &s, zone);
	}
	if (w->app->rep_c != NULL)
		window_draw(w);
}

static void
liste_clic(PtkWindow *w, PtkControl *c, int x, int y, Time t)
{
	int ent = c->label[0] ? 19 : 0, n = liste_nombre(c);
	PtkScrollbar s;
	static PtkControl *dernier;
	static Time t_dernier;
	static int r_dernier = -1;

	scrollbar_geometrie(c, &s);
	if (x >= s.x) {
		scrollbar_press(w, c, &s, x, y);
		return;
	}
	if (y >= c->y + 2 + ent) {
		int r = c->defil + (y - c->y - 2 - ent) / LISTE_RANG;

		if (r >= 0 && r < n && !liste_inactif(c, r)) {
			/* double-clic : même rangée, dans le délai du tableau Souris */
			const char *dc = getenv("PLATINE_DOUBLE_CLIC");
			Time delai = dc != NULL ? (Time)atoi(dc) : 533;

			c->double_clic = dernier == c && r_dernier == r && t - t_dernier < delai;
			dernier = c;
			r_dernier = c->double_clic ? -1 : r;
			t_dernier = t;
			c->value = r;
			if (c->action != NULL)
				c->action(w, c, c->user);
			c->double_clic = 0;
		}
	}
	ptk_window_invalidate(w);
}

static void
liste_clavier(PtkWindow *w, PtkControl *c, KeySym ks, const char *texte)
{
	int n = liste_nombre(c), ent = c->label[0] != 0;
	int vis = (c->h - 4 - ent * 19) / LISTE_RANG, ancien = c->value;
	static char frappe[32];
	static struct timeval t_frappe;

	if (n == 0)
		return;
	/* sélection par frappe, comme les listes de Mac OS 9 : les lettres tapées
	 * à moins d'une seconde d'intervalle forment le début du nom cherché */
	if (texte != NULL && (unsigned char)texte[0] >= 32 && ks < 0xff00) {
		struct timeval now;
		size_t l;
		int i;

		gettimeofday(&now, NULL);
		if ((now.tv_sec - t_frappe.tv_sec) * 1000 + (now.tv_usec - t_frappe.tv_usec) / 1000 > 1000)
			frappe[0] = '\0';
		t_frappe = now;
		l = strlen(frappe);
		snprintf(frappe + l, sizeof(frappe) - l, "%s", texte);
		for (i = 0; i < n; i++) {
			char ic[64], t[256];
			int in;

			if (liste_element(c, i, ic, sizeof(ic), &in, t, sizeof(t)) && !in &&
			    strncasecmp(t, frappe, strlen(frappe)) >= 0) {
				c->value = i;
				break;
			}
		}
		ks = XK_VoidSymbol;
	} else if (ks == XK_Up)
		c->value--;
	else if (ks == XK_Down)
		c->value++;
	else if (ks == XK_Home)
		c->value = 0;
	else if (ks == XK_End)
		c->value = n - 1;
	else if (ks == XK_Page_Up)
		c->value -= vis > 0 ? vis : 1;
	else if (ks == XK_Page_Down)
		c->value += vis > 0 ? vis : 1;
	else if (ks != XK_VoidSymbol)
		return;
	if (c->value < 0)
		c->value = 0;
	if (c->value >= n)
		c->value = n - 1;
	/* une rangée inactive ne se sélectionne pas : on continue dans le sens */
	{
		int sens = ks == XK_Up || ks == XK_Page_Up || ks == XK_End ? -1 : 1, k;

		for (k = 0; k < n && liste_inactif(c, c->value); k++) {
			c->value += sens;
			if (c->value < 0 || c->value >= n) {
				sens = -sens;
				c->value = ancien >= 0 && ancien < n ? ancien : 0;
			}
		}
	}
	if (c->value < c->defil)
		c->defil = c->value;
	if (c->value >= c->defil + vis)
		c->defil = c->value - vis + 1;
	if (c->defil < 0)
		c->defil = 0;
	if (c->defil > n - vis)
		c->defil = n > vis ? n - vis : 0;
	ptk_window_invalidate(w);
	if (ancien != c->value && c->action != NULL)
		c->action(w, c, c->user);
}

static void
texte_ligne_deplacer(PtkControl *c, int delta)
{
	int debut = c->caret, col = 0, cible, fin;

	while (debut > 0 && c->text[debut - 1] != '\n')
		debut--;
	col = c->caret - debut;
	if (delta < 0) {
		if (debut == 0)
			return;
		fin = debut - 1;
		cible = fin;
		while (cible > 0 && c->text[cible - 1] != '\n')
			cible--;
		debut = cible;
		c->caret = cible + (col < fin - cible ? col : fin - cible);
	} else {
		fin = c->caret;
		while (c->text[fin] != '\0' && c->text[fin] != '\n')
			fin++;
		if (c->text[fin] == '\0')
			return;
		debut = fin + 1;
		fin = debut;
		while (c->text[fin] != '\0' && c->text[fin] != '\n')
			fin++;
		c->caret = debut + (col < fin - debut ? col : fin - debut);
	}
}

void
ptk_control_set_page(PtkControl *c, int page)
{
	c->page = page;
}

void
ptk_control_set_group(PtkControl *c, int group)
{
	c->group = group;
}

void
ptk_onglets_afficher(PtkWindow *w, PtkControl *onglets)
{
	PtkControl *c;

	for (c = w->controls; c != NULL; c = c->next)
		if (c != onglets && c->page >= 0)
			c->visible = c->page == onglets->value;
	ptk_window_invalidate(w);
}

static void
control_draw_corps(PtkWindow *w, PtkControl *c)
{
	Ptk *p = w->p;
	int baseline;

	switch (c->kind) {
	case PTK_SEPARATEUR:
		ptk_separateur(p, w->surface, c->x, c->y, c->w, c->h);
		break;
	case PTK_PETITES_FLECHES:
		ptk_petites_fleches(p, w->surface, c->x, c->y,
		    c->pressed ? c->dedans : 0, c->max > c->min);
		break;
	case PTK_BISEAU: {
		/* bouton biseauté Platinum : cadre noir aux coins arrondis, lumière
		 * blanche en haut à gauche, ombre #aaa puis #777 en bas à droite, face
		 * #ddd ; enfoncé : teintes inversées, face #999 ; inactif : cadre
		 * #888, face unie */
		int x = c->x, y = c->y, W = c->w, H = c->h;
		int presse = c->pressed && c->dedans && !p->inactif;
		PtkColor bord = p->inactif ? ptk_color(p, "#888888") : p->black;

		ptk_fill(p, w->surface, x + 1, y + 1, W - 2, H - 2,
		    presse ? ptk_color(p, "#999999") : p->face);
		ptk_line(p, w->surface, x + 2, y, x + W - 3, y, bord);
		ptk_line(p, w->surface, x + 2, y + H - 1, x + W - 3, y + H - 1, bord);
		ptk_line(p, w->surface, x, y + 2, x, y + H - 3, bord);
		ptk_line(p, w->surface, x + W - 1, y + 2, x + W - 1, y + H - 3, bord);
		ptk_line(p, w->surface, x + 1, y + 1, x + 1, y + 1, bord);
		ptk_line(p, w->surface, x + W - 2, y + 1, x + W - 2, y + 1, bord);
		ptk_line(p, w->surface, x + 1, y + H - 2, x + 1, y + H - 2, bord);
		ptk_line(p, w->surface, x + W - 2, y + H - 2, x + W - 2, y + H - 2, bord);
		if (!p->inactif) {
			PtkColor l = presse ? ptk_color(p, "#555555") : p->white;
			PtkColor o1 = presse ? ptk_color(p, "#bbbbbb") : ptk_color(p, "#aaaaaa");
			PtkColor o2 = presse ? ptk_color(p, "#dddddd") : ptk_color(p, "#777777");

			ptk_line(p, w->surface, x + 2, y + 1, x + W - 3, y + 1, l);
			ptk_line(p, w->surface, x + 1, y + 2, x + 1, y + H - 3, l);
			ptk_line(p, w->surface, x + 2, y + 2, x + W - 4, y + 2, l);
			ptk_line(p, w->surface, x + 2, y + 2, x + 2, y + H - 4, l);
			ptk_line(p, w->surface, x + 3, y + H - 3, x + W - 3, y + H - 3, o1);
			ptk_line(p, w->surface, x + W - 3, y + 3, x + W - 3, y + H - 3, o1);
			ptk_line(p, w->surface, x + 2, y + H - 2, x + W - 2, y + H - 2, o2);
			ptk_line(p, w->surface, x + W - 2, y + 2, x + W - 2, y + H - 2, o2);
		}
		if (c->contenu != NULL)
			c->contenu(w, c, x + 3 + presse, y + 3 + presse, W - 6, H - 6, presse);
		break;
	}
	case PTK_TRIANGLE:
		ptk_triangle(p, w->surface, c->x, c->y + (c->h - 12) / 2,
		    c->checked, c->pressed && c->dedans);
		if (c->label[0]) {
			baseline = c->y + (c->h + p->font_menu->ascent -
			    p->font_menu->descent) / 2;
			ptk_text(p, w->surface, p->font_menu, c->x + 18,
			    baseline, c->label, encre(p));
		}
		break;
	case PTK_GROUPE:
		dessiner_groupe(w, c);
		break;
	case PTK_CURSEUR:
		dessiner_curseur(w, c);
		break;
	case PTK_ONGLETS:
		dessiner_onglets(w, c);
		break;
	case PTK_LISTE:
		dessiner_liste(w, c);
		break;
	case PTK_SCROLLBAR: {
		PtkScrollbar s = { 0 };

		scrollbar_controle(c, &s);
		s.active = s.active && !p->inactif;	/* fenêtre inactive : ascenseur vide */
		ptk_scrollbar_draw(p, w->surface, &s);
		break;
	}
	case PTK_BUTTON:
	case PTK_DEFAULT_BUTTON:
		ptk_button(p, w->surface, c->x, c->y, c->w, c->h,
		    c->label, c->pressed && c->dedans, c->kind == PTK_DEFAULT_BUTTON,
		    c->enabled ? p->text : p->text_dim);
		break;
	case PTK_CHECKBOX:
		ptk_checkbox(p, w->surface, c->x, c->y, c->checked);
		baseline = c->y + (12 + p->font_menu->ascent -
		    p->font_menu->descent) / 2;
		ptk_text(p, w->surface, p->font_menu, c->x + 20, baseline,
		    c->label, encre(p));
		break;
	case PTK_RADIO:
		ptk_radio(p, w->surface, c->x, c->y, c->checked);
		baseline = c->y + (12 + p->font_menu->ascent -
		    p->font_menu->descent) / 2;
		ptk_text(p, w->surface, p->font_menu, c->x + 20, baseline,
		    c->label, encre(p));
		break;
	case PTK_TEXTFIELD: {
		int caret = c->caret;
		size_t len = strlen(c->text);
		if (caret < 0)
			caret = 0;
		if ((size_t)caret > len)
			caret = (int)len;
		ptk_field(p, w->surface, c->x, c->y, c->w, c->h,
		    c->focused && !c->tout_selectionne);
		if (c->tout_selectionne)
			ptk_fill(p, w->surface, c->x + 3, c->y + 3, c->w - 6, c->h - 6, p->field_select);
		ptk_text(p, w->surface, p->font_menu, c->x + 5,
		    c->y + (c->h + p->font_menu->ascent -
		    p->font_menu->descent) / 2, c->text, p->text);
		if (c->focused && c->caret_on && !c->tout_selectionne && w->active) {
			char tmp[PTK_TEXT_MAX];
			memcpy(tmp, c->text, (size_t)caret);
			tmp[caret] = '\0';
			{
				int cx = c->x + 5 + ptk_text_width(p,
				    p->font_menu, tmp);
				int yy = c->y + 4;
				ptk_line(p, w->surface, cx, yy, cx,
				    c->y + c->h - 5, p->black);
			}
		}
		break;
	}
	case PTK_STATIC:
		ptk_text(p, w->surface, p->font_menu, c->x, c->y + 12,
		    c->text[0] ? c->text : c->label, encre(p));
		break;
	case PTK_PROGRESS:
		ptk_progress(p, w->surface, c->x, c->y, c->w, c->h,
		    c->value, c->max);
		break;
	case PTK_BARBIER:
		ptk_barbier(p, w->surface, c->x, c->y, c->w, c->h, c->defil);
		break;
	case PTK_RECHERCHE:
		ptk_recherche(p, w->surface, c->x, c->y, c->w, c->h,
		    c->defil, c->enabled);
		break;
	case PTK_POPUP:
		ptk_popup_police(p, w->surface, c->x, c->y, c->w, c->h,
		    c->text[0] ? c->text : c->label, c->pressed, c->police);
		break;
	case PTK_TEXTAREA: {
		char *copy, *line, *save;
		int ly = c->y + 4 + p->font_menu->ascent;
		int cx = c->x + 5, cy = c->y + 4;
		int caret = c->caret;
		size_t len = strlen(c->text);

		ptk_field(p, w->surface, c->x, c->y, c->w, c->h,
		    c->focused);
		ptk_fill(p, w->surface, c->x + 3, c->y + 3, c->w - 6,
		    c->h - 6, p->white);
		/* une ligne par « \n », lignes vides comprises (strtok les
		 * sautait : paragraphes collés, curseur décalé), découpé au cadre */
		copy = strdup(c->text);
		ptk_clip(p, c->x + 3, c->y + 3, c->w - 6, c->h - 6);
		for (line = copy; line != NULL && ly - p->font_menu->ascent < c->y + c->h;
		    line = save) {
			save = strchr(line, '\n');
			if (save != NULL)
				*save++ = '\0';
			ptk_text(p, w->surface, p->font_menu, c->x + 5, ly,
			    line, p->text);
			ly += p->font_menu->ascent + p->font_menu->descent;
		}
		ptk_unclip(p);
		free(copy);
		if (c->focused && c->caret_on && w->active && caret >= 0 &&
		    (size_t)caret <= len) {
			/* position du curseur : ligne puis colonne */
			int i, colx = cx;
			int col = 0, row = 0;

			for (i = 0; i < caret; i++) {
				if (c->text[i] == '\n') {
					row++;
					col = 0;
				} else {
					col++;
				}
			}
			{
				char pre[PTK_TEXT_MAX];
				snprintf(pre, sizeof(pre), "%.*s", col,
				    c->text + (caret - col));
				colx += ptk_text_width(p, p->font_menu,
				    pre);
			}
			cy += row * (p->font_menu->ascent +
			    p->font_menu->descent);
			ptk_line(p, w->surface, colx, cy + 2, colx,
			    cy + p->font_menu->ascent +
			    p->font_menu->descent, p->black);
		}
		break;
	}
	}
}

/* Mac OS 9 désactive les contrôles d'une fenêtre passée en arrière-plan
 * (DeactivateControl) et ceux qu'on a désactivés : même apparence */
static void
control_draw(PtkWindow *w, PtkControl *c)
{
	if (!c->visible)
		return;
	w->p->inactif = !c->enabled || !w->active;
	control_draw_corps(w, c);
	w->p->inactif = 0;
}

static void
window_draw(PtkWindow *w)
{
	Ptk *p = w->p;
	PtkControl *c;

	if (w->disposed || !w->mapped)
		return;
	/* le tampon hors écran n'est recréé que si la fenêtre change de taille */
	if (w->back != None && (w->back_w != w->w || w->back_h != w->h)) {
		XFreePixmap(p->dpy, w->back);
		w->back = None;
	}
	if (w->back == None) {
		w->back = XCreatePixmap(p->dpy, w->win, (unsigned)w->w,
		    (unsigned)w->h, (unsigned)DefaultDepth(p->dpy, p->screen));
		w->back_w = w->w;
		w->back_h = w->h;
	}
	w->surface = w->back;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	if (w->draw)
		w->draw(w, w->user);
	for (c = w->controls; c; c = c->next)
		control_draw(w, c);
	if (w->draw_apres != NULL)
		w->draw_apres(w);
	XCopyArea(p->dpy, w->back, w->win, p->gc, 0, 0,
	    (unsigned)w->w, (unsigned)w->h, 0, 0);
	XFlush(p->dpy);
}

void
ptk_window_invalidate(PtkWindow *w)
{
	window_draw(w);
}

void
ptk_window_show(PtkWindow *w)
{
	w->mapped = 1;
	set_utf8(w->app, w->win, w->app->a_menu, w->menu);
	set_utf8(w->app, w->win, w->app->a_app,
	    w->app->name ? w->app->name : "Application");
	set_utf8(w->app, w->win, w->app->a_icon, w->icon);
	XMapRaised(w->app->dpy, w->win);
	window_draw(w);
	w->app->active = w;
}

PtkControl *
ptk_control_add(PtkWindow *w, int kind, int x, int y, int ww, int hh,
    const char *label)
{
	PtkControl *c = calloc(1, sizeof(*c));
	PtkControl **pp;

	if (c == NULL)
		return NULL;
	c->kind = kind;
	c->x = x;
	c->y = y;
	c->w = ww;
	c->h = hh;
	c->enabled = 1;
	c->visible = 1;
	c->page = -1;
	c->min = 0;
	c->max = 100;
	if (kind == PTK_SCROLLBAR)
		c->defil = 10;
	if (kind == PTK_PETITES_FLECHES) {
		c->w = PTK_PETITES_FLECHES_L;
		c->h = PTK_PETITES_FLECHES_H;
	}
	snprintf(c->label, sizeof(c->label), "%s", label ? label : "");
	for (pp = &w->controls; *pp; pp = &(*pp)->next)
		;
	*pp = c;
	return c;
}

void
ptk_control_set_action(PtkControl *c,
    void (*action)(PtkWindow *, PtkControl *, void *), void *user)
{
	c->action = action;
	c->user = user;
}

void
ptk_control_set_text(PtkControl *c, const char *text)
{
	snprintf(c->text, sizeof(c->text), "%s", text ? text : "");
	c->caret = (int)strlen(c->text);
	/* un contrôle qui dessine son libellé (bouton, case, radio, triangle, groupe) le
	 * change aussi : « Démarrer » qui devient « Arrêter » */
	if (c->kind == PTK_BUTTON || c->kind == PTK_DEFAULT_BUTTON ||
	    c->kind == PTK_CHECKBOX || c->kind == PTK_RADIO ||
	    c->kind == PTK_TRIANGLE || c->kind == PTK_GROUPE)
		snprintf(c->label, sizeof(c->label), "%s", c->text);
}

void
ptk_control_set_items(PtkControl *c, const char *items)
{
	snprintf(c->items, sizeof(c->items), "%s", items ? items : "");
}

void
ptk_control_set_value(PtkControl *c, int value)
{
	if (value < c->min)
		value = c->min;
	if (value > c->max)
		value = c->max;
	c->value = value;
}

int
ptk_control_value(PtkControl *c)
{
	return c->value;
}

static PtkControl *
control_at(PtkWindow *w, int x, int y)
{
	PtkControl *c, *dessus = NULL;

	/* le dernier ajouté est dessiné par-dessus : un bouton dans un cadre ou un panneau d'onglets */
	for (c = w->controls; c; c = c->next) {
		if (!c->visible)
			continue;
		if (x >= c->x && x < c->x + c->w &&
		    y >= c->y && y < c->y + c->h)
			dessus = c;
	}
	return dessus;
}

/* menu local : l[2] largeur et l[4] hauteur du bouton, l[3] élément courant (-1 : aucun) */
static void
send_context_popup(PtkWindow *w, int x, int y, int bw, int bh, int idx, const char *menu)
{
	XEvent ev;

	/* rend le pointeur saisi par l'appui : le WM le prend, on glisse et on relâche sur l'élément */
	XUngrabPointer(w->app->dpy, CurrentTime);
	set_utf8(w->app, w->win, w->app->a_context, menu);
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w->win;
	ev.xclient.message_type = w->app->a_context;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = x;
	ev.xclient.data.l[1] = y;
	ev.xclient.data.l[2] = bw;
	ev.xclient.data.l[3] = idx;
	ev.xclient.data.l[4] = bh;
	XSendEvent(w->app->dpy, w->app->root, False,
	    SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(w->app->dpy);
}

static void
send_event(PtkWindow *w, PtkEvent *ev)
{
	if (w->event)
		w->event(w, ev, w->user);
}

static void
controle_son(PtkWindow *w, PtkControl *c, const char *etat)
{
	const char *base = c->kind == PTK_CHECKBOX ? "chk" :
	    c->kind == PTK_RADIO ? "rad" : c->kind == PTK_TRIANGLE ? "dsc" :
	    c->kind == PTK_BISEAU ? "bev" : "btn";
	char code[8];

	snprintf(code, sizeof(code), "%s%s", base, etat);
	ptk_son(w->p, code);
}

/* Bouton par défaut et bouton Annuler de la fenêtre (visibles et actifs) */
static PtkControl *
bouton_defaut(PtkWindow *w)
{
	PtkControl *c;

	for (c = w->controls; c != NULL; c = c->next)
		if (c->kind == PTK_DEFAULT_BUTTON && c->visible && c->enabled)
			return c;
	return NULL;
}

static PtkControl *
bouton_annuler(PtkWindow *w)
{
	PtkControl *c;

	for (c = w->controls; c != NULL; c = c->next)
		if ((c->kind == PTK_BUTTON || c->kind == PTK_DEFAULT_BUTTON) &&
		    c->visible && c->enabled &&
		    (strcmp(c->label, "Annuler") == 0 ||
		    strcmp(c->label, "Cancel") == 0))
			return c;
	return NULL;
}

/* Activation au clavier, comme le Dialog Manager : le bouton s'enfonce
 * 8 ticks (HiliteControl + Delay) puis agit ; pas de son, les sons
 * Platinum accompagnent le suivi à la souris (TrackControl) */
static void
bouton_flash(PtkWindow *w, PtkControl *b)
{
	b->pressed = 1;
	b->dedans = 1;
	window_draw(w);
	XSync(w->app->dpy, False);
	usleep(FLASH_US);
	b->pressed = 0;
	b->dedans = 0;
	window_draw(w);
	XSync(w->app->dpy, False);
	if (b->action != NULL)
		b->action(w, b, b->user);
}

/* --- Presse-papiers texte : Copier/Couper/Coller des contrôles texte --- */

static int
controle_texte(PtkControl *c)
{
	return c != NULL && (c->kind == PTK_TEXTFIELD ||
	    c->kind == PTK_TEXTAREA);
}

/* le contrôle appartient-il encore à une fenêtre ouverte ? (un collage peut
 * arriver après la fermeture de sa fenêtre) */
static PtkWindow *
fenetre_du_controle(PtkApp *app, PtkControl *c)
{
	PtkWindow *w;
	PtkControl *o;

	for (w = app->windows; w != NULL; w = w->next)
		for (o = w->controls; o != NULL; o = o->next)
			if (o == c)
				return w;
	return NULL;
}

/* un texte collé remplace la pseudo-sélection, sinon il s'insère au caret ;
 * un champ monoligne ignore les sauts de ligne */
static void
coller_texte(PtkControl *c, const char *texte)
{
	char buf[PTK_TEXT_MAX];
	size_t n = 0, l, caret, garde;
	const char *p;

	for (p = texte; *p != '\0' && n < sizeof(buf) - 1; p++)
		if (*p != '\r' && (c->kind != PTK_TEXTFIELD || *p != '\n'))
			buf[n++] = *p;
	buf[n] = '\0';
	l = strlen(c->text);
	if (c->tout_selectionne) {
		c->tout_selectionne = 0;
		c->text[0] = '\0';
		l = 0;
		c->caret = 0;
	}
	caret = c->caret >= 0 ? (size_t)c->caret : 0;
	if (caret > l)
		caret = l;
	garde = l - caret;
	if (l + n >= sizeof(c->text))	/* limite du contrôle : tronquer la fin */
		n = sizeof(c->text) - 1 - l;
	memcpy(c->text + caret + n, c->text + caret, garde + 1);
	memcpy(c->text + caret, buf, n);
	c->text[caret + n + garde] = '\0';
	c->caret = (int)(caret + n);
	c->caret_on = 1;
}

/* la fenêtre qui copie devient propriétaire de CLIPBOARD */
static void
copier_texte(PtkApp *app, PtkWindow *w, PtkControl *c, int couper)
{
	if (c->text[0] == '\0')
		return;
	snprintf(app->clip_text, sizeof(app->clip_text), "%s", c->text);
	app->clip_win = w->win;
	XSetSelectionOwner(app->dpy, app->a_clip, w->win, CurrentTime);
	if (couper) {
		c->text[0] = '\0';
		c->caret = 0;
		c->tout_selectionne = 0;
		window_draw(w);
	}
}

/* requête d'un client : TARGETS, ou le texte en UTF8_STRING et STRING
 * (mêmes octets UTF-8 dans les deux, comme fait le Finder) */
static void
clip_requete(PtkApp *app, XSelectionRequestEvent *q)
{
	XEvent r;
	Atom prop = q->property != None ? q->property : q->target;

	memset(&r, 0, sizeof(r));
	r.xselection.type = SelectionNotify;
	r.xselection.requestor = q->requestor;
	r.xselection.selection = q->selection;
	r.xselection.target = q->target;
	r.xselection.time = q->time;
	r.xselection.property = None;
	if (q->selection == app->a_clip && app->clip_text[0] != '\0') {
		if (q->target == app->a_targets) {
			Atom cibles[] = { app->a_targets, app->a_utf8,
			    XA_STRING };

			XChangeProperty(app->dpy, q->requestor, prop,
			    XA_ATOM, 32, PropModeReplace,
			    (unsigned char *)cibles, 3);
			r.xselection.property = prop;
		} else if (q->target == app->a_utf8 ||
		    q->target == XA_STRING) {
			XChangeProperty(app->dpy, q->requestor, prop,
			    q->target, 8, PropModeReplace,
			    (unsigned char *)app->clip_text,
			    (int)strlen(app->clip_text));
			r.xselection.property = prop;
		}
	}
	XSendEvent(app->dpy, q->requestor, False, 0, &r);
	XFlush(app->dpy);
}

/* réponse du collage : le texte reçu va au contrôle qui l'a demandé */
static void
clip_recu(PtkApp *app, XEvent *e)
{
	PtkControl *c = app->coller_cible;
	PtkWindow *w;
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;

	app->coller_cible = NULL;
	if (app->coller_rappel != NULL) {
		void (*rappel)(PtkWindow *, const char *, void *) = app->coller_rappel;
		PtkWindow *cw = app->coller_fenetre, *o;

		app->coller_rappel = NULL;
		for (o = app->windows; o != NULL && o != cw; o = o->next)
			;
		if (o == NULL || e->xselection.property == None)
			return;
		if (XGetWindowProperty(app->dpy, e->xselection.requestor,
		    e->xselection.property, 0, PTK_TEXT_MAX / 4 + 1, True,
		    AnyPropertyType, &type, &format, &n, &after, &data) != Success ||
		    data == NULL)
			return;
		if (format == 8)
			rappel(cw, (char *)data, app->coller_user);
		XFree(data);
		return;
	}
	if (c == NULL || e->xselection.property == None)
		return;
	w = fenetre_du_controle(app, c);
	if (w == NULL)
		return;
	if (XGetWindowProperty(app->dpy, e->xselection.requestor,
	    e->xselection.property, 0, PTK_TEXT_MAX / 4 + 1, True,
	    AnyPropertyType, &type, &format, &n, &after, &data) != Success ||
	    data == NULL)
		return;
	if (format == 8)
		coller_texte(c, (char *)data);
	XFree(data);
	window_draw(w);
}

void
ptk_copier(PtkWindow *w, const char *texte)
{
	PtkApp *app = w->app;

	if (texte == NULL || texte[0] == '\0')
		return;
	snprintf(app->clip_text, sizeof(app->clip_text), "%s", texte);
	app->clip_win = w->win;
	XSetSelectionOwner(app->dpy, app->a_clip, w->win, CurrentTime);
	XFlush(app->dpy);
}

void
ptk_coller(PtkWindow *w, void (*recu)(PtkWindow *, const char *, void *), void *user)
{
	PtkApp *app = w->app;

	if (XGetSelectionOwner(app->dpy, app->a_clip) == None) {
		ptk_beep(app);
		return;
	}
	app->coller_cible = NULL;
	app->coller_rappel = recu;
	app->coller_fenetre = w;
	app->coller_user = user;
	XConvertSelection(app->dpy, app->a_clip, app->a_utf8, app->a_utf8, w->win, CurrentTime);
	XFlush(app->dpy);
}

static void
dispatch(PtkApp *app, XEvent *e)
{
	PtkWindow *w;

	for (w = app->windows; w; w = w->next)
		if (w->win == e->xany.window)
			break;
	if (w == NULL && app->hors != NULL && app->hors(app, e, app->hors_user))
		return;
	if (w == NULL && e->type != ClientMessage)
		return;

	switch (e->type) {
	case Expose:
		if (e->xexpose.count == 0)
			window_draw(w);
		break;
	case ConfigureNotify:
		if (w->w != e->xconfigure.width || w->h != e->xconfigure.height) {
			PtkEvent ev = { 0 };

			w->w = e->xconfigure.width;
			w->h = e->xconfigure.height;
			ev.type = PTK_EV_RESIZE;
			ev.width = w->w;
			ev.height = w->h;
			send_event(w, &ev);
			window_draw(w);
		}
		break;
	case ButtonPress: {
		PtkControl *c = control_at(w, e->xbutton.x, e->xbutton.y);
		PtkEvent ev = { 0 };
		if (c != NULL && (c->kind == PTK_GROUPE ||
		    c->kind == PTK_SEPARATEUR))
			c = NULL;	/* un cadre ou un trait ne prend pas les clics */
		if (c != NULL && !c->enabled)
			break;
		if (c != NULL && c->kind == PTK_PETITES_FLECHES) {
			int m = fleches_moitie(c, e->xbutton.x, e->xbutton.y);

			if (m == 0 || c->max <= c->min)
				break;
			c->pressed = 1;
			c->caret = m;
			c->dedans = m;
			ptk_son(w->p, m == 1 ? "laup" : "ladp");
			fleches_pas(w, c);
			repetition_armer(w, c, e->xbutton.x, e->xbutton.y);
			window_draw(w);
			break;
		}
		if (c != NULL && c->kind == PTK_CURSEUR) {
			c->pressed = 1;
			ptk_son(w->p, "sltp");
			curseur_suivre(w, c, e->xbutton.x);
			break;
		}
		if (c != NULL && c->kind == PTK_LISTE) {
			focus_control(w, c);
			liste_clic(w, c, e->xbutton.x, e->xbutton.y, e->xbutton.time);
			break;
		}
		if (c != NULL && c->kind == PTK_SCROLLBAR) {
			PtkScrollbar s;

			focus_control(w, NULL);
			scrollbar_geometrie(c, &s);
			scrollbar_press(w, c, &s, e->xbutton.x, e->xbutton.y);
			break;
		}
		if (c != NULL && c->kind == PTK_ONGLETS) {
			if (e->xbutton.y < c->y + ONGLET_H) {
				char l[sizeof(c->items)], *it, *sv;
				int x = c->x + 5, i = 0;

				snprintf(l, sizeof(l), "%s", c->items);
				for (it = strtok_r(l, "|", &sv); it != NULL; it = strtok_r(NULL, "|", &sv), i++) {
					int lg = ptk_text_width(w->p, w->p->font_menu, it) + 23;

					if (e->xbutton.x >= x && e->xbutton.x < x + lg && i != c->value) {
						c->value = i;
						ptk_son(w->p, "tabp");
						ptk_onglets_afficher(w, c);
						if (c->action != NULL)
							c->action(w, c, c->user);
						break;
					}
					x += lg;
				}
				break;
			}
			c = NULL;	/* le panneau laisse passer aux contrôles et à l'application */
		}
		if (c != NULL) {
			if (c->kind == PTK_TEXTFIELD ||
			    c->kind == PTK_TEXTAREA) {
				int cx = c->x + 5;
				size_t i;

				if (w->focus != NULL && w->focus != c)
					commit_text_focus(w);
				if (w->focus != NULL)
					w->focus->focused = 0;
				w->focus = c;
				c->focused = 1;
				c->caret_on = 1;
				c->caret = (int)strlen(c->text);
				if (c->kind == PTK_TEXTFIELD) {
					for (i = 0; i < strlen(c->text);
					    i++) {
						char tmp[PTK_TEXT_MAX];
						memcpy(tmp, c->text, i);
						tmp[i] = '\0';
						if (cx + ptk_text_width(w->p,
						    w->p->font_menu, tmp) >
						    e->xbutton.x) {
							c->caret = (int)i;
							break;
						}
					}
				}
				window_draw(w);
				break;
			}
			if (c->kind == PTK_POPUP) {
				Window child;
				int rx, ry;

				if (w->popup != NULL)
					w->popup->pressed = 0;
				w->popup = c;
				c->pressed = 1;
				ptk_son(w->p, "popp");
				char list[sizeof(c->items) + 512], src[sizeof(c->items)], *it, *sv;
				const char *cour = c->text[0] ? c->text : c->label;
				int idx = -1, n = 0;

				/* menu local OS 9 : l'élément choisi est coché et vient se poser sur le bouton */
				snprintf(src, sizeof(src), "%s",
				    c->items[0] ? c->items : c->label);
				list[0] = '\0';
				for (it = strtok_r(src, "|", &sv); it != NULL;
				    it = strtok_r(NULL, "|", &sv), n++) {
					size_t l = strlen(list);
					int coche = idx < 0 && strcmp(it, cour) == 0;

					if (coche)
						idx = n;
					snprintf(list + l, sizeof(list) - l, "%s%s\n",
					    coche ? "\xe2\x88\x9a " : "", it);
				}
				XTranslateCoordinates(w->app->dpy, w->win,
				    w->app->root, c->x, c->y,
				    &rx, &ry, &child);
				send_context_popup(w, rx, ry, c->w, c->h, idx, list);
				window_draw(w);
				break;
			}
			if (c->kind == PTK_BUTTON ||
			    c->kind == PTK_DEFAULT_BUTTON ||
			    c->kind == PTK_CHECKBOX ||
			    c->kind == PTK_RADIO || c->kind == PTK_TRIANGLE ||
			    c->kind == PTK_BISEAU) {
				if (w->focus != NULL) {
					commit_text_focus(w);
					w->focus->focused = 0;
					w->focus = NULL;
				}
				c->pressed = 1;
				c->dedans = 1;
				controle_son(w, c, "p");
				window_draw(w);
				break;
			}
		} else if (w->focus != NULL) {
			commit_text_focus(w);
			w->focus->focused = 0;
			w->focus = NULL;
			window_draw(w);
		}
		ev.type = PTK_EV_MOUSE_DOWN;
		ev.x = e->xbutton.x;
		ev.y = e->xbutton.y;
		ev.button = e->xbutton.button;
		send_event(w, &ev);
		break;
	}
	case ButtonRelease: {
		PtkControl *c;
		for (c = w->controls; c; c = c->next) {
			int inside;
			if (!c->pressed)
				continue;
			if (c->kind == PTK_POPUP)
				continue;
			if (c->kind == PTK_SCROLLBAR || c->kind == PTK_LISTE) {
				scrollbar_release(w, c);
				return;
			}
			if (c->kind == PTK_PETITES_FLECHES) {
				ptk_son(w->p, c->caret == 1 ? "laur" : "ladr");
				repetition_arreter(w->app);
				c->pressed = 0;
				c->dedans = 0;
				window_draw(w);
				return;
			}
			if (c->kind == PTK_CURSEUR) {
				c->pressed = 0;
				ptk_son(w->p, "slte");
				return;
			}
			c->pressed = 0;
			c->dedans = 0;
			inside = e->xbutton.x >= c->x &&
			    e->xbutton.x < c->x + c->w &&
			    e->xbutton.y >= c->y &&
			    e->xbutton.y < c->y + c->h;
			if (!inside) {
				window_draw(w);
				return;
			}
			controle_son(w, c, "r");
			if (c->kind == PTK_TRIANGLE)
				c->checked = !c->checked;
			if (c->kind == PTK_CHECKBOX ||
			    c->kind == PTK_RADIO) {
				if (c->kind == PTK_RADIO) {
					PtkControl *o;
					for (o = w->controls; o;
					    o = o->next)
						if (o != c && o->kind == PTK_RADIO &&
						    o->group == c->group)
							o->checked = 0;
					c->checked = 1;
				} else {
					c->checked = !c->checked;
				}
			}
			window_draw(w);
			if (c->action)
				c->action(w, c, c->user);
			return;
		}
		{
			PtkEvent ev = { 0 };
			ev.type = PTK_EV_MOUSE_UP;
			ev.x = e->xbutton.x;
			ev.y = e->xbutton.y;
			ev.button = e->xbutton.button;
			send_event(w, &ev);
		}
		break;
	}
	case MotionNotify: {
		PtkEvent ev = { 0 };
		PtkControl *cc;

		for (cc = w->controls; cc != NULL; cc = cc->next) {
			if (cc->pressed && (cc->kind == PTK_SCROLLBAR ||
			    cc->kind == PTK_LISTE)) {
				scrollbar_motion(w, cc, e->xmotion.x, e->xmotion.y);
				return;
			}
			if (cc->pressed && cc->kind == PTK_PETITES_FLECHES) {
				int m = fleches_moitie(cc, e->xmotion.x, e->xmotion.y);
				int d = m == cc->caret ? m : 0;

				if (d != cc->dedans) {
					cc->dedans = d;
					window_draw(w);
				}
				return;
			}
			if (cc->pressed && (cc->kind == PTK_BUTTON ||
			    cc->kind == PTK_DEFAULT_BUTTON ||
			    cc->kind == PTK_CHECKBOX || cc->kind == PTK_RADIO ||
			    cc->kind == PTK_TRIANGLE || cc->kind == PTK_BISEAU)) {
				int dedans = e->xmotion.x >= cc->x &&
				    e->xmotion.x < cc->x + cc->w &&
				    e->xmotion.y >= cc->y &&
				    e->xmotion.y < cc->y + cc->h;

				if (dedans != cc->dedans) {
					cc->dedans = dedans;
					controle_son(w, cc, dedans ? "e" : "x");
					window_draw(w);
				}
				return;
			}
			if (cc->kind == PTK_CURSEUR && cc->pressed) {
				curseur_suivre(w, cc, e->xmotion.x);
				return;
			}
		}
		ev.type = PTK_EV_MOUSE_MOVE;
		ev.x = e->xmotion.x;
		ev.y = e->xmotion.y;
		send_event(w, &ev);
		break;
	}
	case KeyPress: {
		PtkControl *c = w->focus;
		PtkEvent ev = { 0 };
		KeySym touche = XLookupKeysym(&e->xkey, 0);
		if (touche == XK_Tab || touche == XK_ISO_Left_Tab) {
			focus_suivant(w, touche == XK_ISO_Left_Tab || (e->xkey.state & ShiftMask) ? -1 : 1);
			break;
		}
		/* Dialog Manager : Retour et Entrée valent le bouton par défaut
		 * (sauf dans une zone de texte, ou un champ qui a son action),
		 * Échap et ⌘. le bouton Annuler ; le bouton clignote puis agit */
		if ((touche == XK_Return || touche == XK_KP_Enter) &&
		    !(c != NULL && (c->kind == PTK_TEXTAREA ||
		    (c->kind == PTK_TEXTFIELD && c->action != NULL)))) {
			PtkControl *b = bouton_defaut(w);

			if (b != NULL) {
				commit_text_focus(w);
				bouton_flash(w, b);
				break;
			}
		}
		if (touche == XK_Escape || (touche == XK_period &&
		    (e->xkey.state & Mod1Mask))) {
			PtkControl *b = bouton_annuler(w);

			if (b != NULL) {
				bouton_flash(w, b);
				break;
			}
		}
		if (c != NULL && c->kind == PTK_LISTE) {
			char tx[8];
			int nt = XLookupString(&e->xkey, tx, sizeof(tx) - 1, NULL, NULL);

			tx[nt > 0 ? nt : 0] = '\0';
			liste_clavier(w, c, touche, nt > 0 ? tx : NULL);
			break;
		}
		if (c != NULL && (c->kind == PTK_TEXTFIELD ||
		    c->kind == PTK_TEXTAREA)) {
			char buf[32];
			KeySym ks;
			int n = XLookupString(&e->xkey, buf, sizeof(buf),
			    &ks, NULL);
			size_t l = strlen(c->text);
			int caret = c->caret;

			/* une touche de modification seule (⌘ maintenue avant
			 * un raccourci) ne touche ni la sélection ni le texte,
			 * comme sous Mac OS 9 */
			if (IsModifierKey(ks))
				break;
			if (caret < 0)
				caret = 0;
			if ((size_t)caret > l)
				caret = (int)l;
			c->caret_on = 1;
			if (c->tout_selectionne) {
				/* première frappe : le nom sélectionné est remplacé, les flèches gardent le texte */
				c->tout_selectionne = 0;
				if (ks == XK_BackSpace || (ks >= 0x20 && ks < 0xff00)) {
					c->text[0] = '\0';
					caret = 0;
					c->caret = 0;
					if (ks == XK_BackSpace) {
						ptk_window_invalidate(w);
						break;
					}
				}
			}
			if (ks == XK_BackSpace) {
				if (caret > 0) {
					memmove(c->text + caret - 1,
					    c->text + caret,
					    l - (size_t)caret + 1);
					c->caret = caret - 1;
				}
				window_draw(w);
				break;
			}
			if (ks == XK_Delete) {
				if ((size_t)caret < l)
					memmove(c->text + caret,
					    c->text + caret + 1,
					    l - (size_t)caret);
				window_draw(w);
				break;
			}
			if (ks == XK_Left) {
				if (caret > 0)
					c->caret = caret - 1;
				window_draw(w);
				break;
			}
			if (ks == XK_Right) {
				if ((size_t)caret < l)
					c->caret = caret + 1;
				window_draw(w);
				break;
			}
			if (c->kind == PTK_TEXTAREA && (ks == XK_Up || ks == XK_Down)) {
				texte_ligne_deplacer(c, ks == XK_Up ? -1 : 1);
				window_draw(w);
				break;
			}
			if (ks == XK_Home) {
				c->caret = 0;
				window_draw(w);
				break;
			}
			if (ks == XK_End) {
				c->caret = (int)l;
				window_draw(w);
				break;
			}
			if (ks == XK_Return || ks == XK_KP_Enter) {
				if (c->kind == PTK_TEXTAREA) {
					if (l < sizeof(c->text) - 2) {
						memmove(c->text + caret + 1,
						    c->text + caret,
						    l - (size_t)caret + 1);
						c->text[caret] = '\n';
						c->caret = caret + 1;
						window_draw(w);
					}
					break;
				}
				if (c->action)
					c->action(w, c, c->user);
				break;
			}
			if (n > 0 && (unsigned char)buf[0] >= 32 &&
			    l < sizeof(c->text) - 1) {
				memmove(c->text + caret + 1, c->text + caret,
				    l - (size_t)caret + 1);
				c->text[caret] = buf[0];
				c->caret = caret + 1;
				window_draw(w);
			}
			break;
		}
		ev.type = PTK_EV_KEY;
		ev.state = e->xkey.state;
		ev.keycode = (int)e->xkey.keycode;
		ev.keysym = XLookupKeysym(&e->xkey, 0);
		{
			int n = XLookupString(&e->xkey, ev.text,
			    sizeof(ev.text), NULL, NULL);
			ev.text[n > 0 ? n : 0] = '\0';
		}
		send_event(w, &ev);
		break;
	}
	case KeyRelease: {
		PtkEvent ev = { 0 };

		ev.type = PTK_EV_KEY_UP;
		ev.state = e->xkey.state;
		ev.keycode = (int)e->xkey.keycode;
		ev.keysym = XLookupKeysym(&e->xkey, 0);
		send_event(w, &ev);
		break;
	}
	case FocusIn:
		if (e->xfocus.detail == NotifyPointer)
			break;
		w->active = 1;
		ptk_window_invalidate(w);
		{
			PtkEvent ev = { 0 };
			ev.type = PTK_EV_ACTIVATE;
			send_event(w, &ev);
		}
		break;
	case FocusOut:
		if (e->xfocus.detail == NotifyPointer)
			break;
		w->active = 0;
		/* plus de maintien en cours dans une fenêtre qui n'est plus devant */
		if (app->rep_w == w)
			repetition_arreter(app);
		ptk_window_invalidate(w);
		break;
	case SelectionClear:
		if (e->xselectionclear.selection == app->a_clip) {
			app->clip_win = None;
			app->clip_text[0] = '\0';
		}
		break;
	case SelectionRequest:
		if (e->xselectionrequest.selection == app->a_clip)
			clip_requete(app, &e->xselectionrequest);
		break;
	case SelectionNotify:
		if (e->xselection.selection == app->a_clip &&
		    e->xselection.target == app->a_utf8)
			clip_recu(app, e);
		else if (app->xdnd != NULL)
			app->xdnd(app, e, app->xdnd_user);
		break;
	case ClientMessage:
		if (e->xclient.message_type == app->a_protocols &&
		    (Atom)e->xclient.data.l[0] == app->a_delete) {
			for (w = app->windows; w; w = w->next)
				if (w->win == e->xclient.window)
					break;
			if (w != NULL &&
			    (w->close == NULL || w->close(w, w->close_user)))
				ptk_window_close(w);
			break;
		}
		if (e->xclient.message_type == app->a_cmd) {
			char *cmd = get_prop(app, e->xclient.window, app->a_cmd);
			if (cmd != NULL && w != NULL) {
				if (w->popup != NULL) {
					PtkControl *pc = w->popup;
					char list[sizeof(pc->items)], *item, *save;
					int found = 0;

					snprintf(list, sizeof(list), "%s",
					    pc->items);
					for (item = strtok_r(list, "|", &save);
					    item != NULL;
					    item = strtok_r(NULL, "|", &save)) {
						if (strcmp(item, cmd) == 0) {
							found = 1;
							break;
						}
					}
					if (found) {
						pc->pressed = 0;
						w->popup = NULL;
						ptk_son(w->p, "popr");
						snprintf(pc->text,
						    sizeof(pc->text), "%s",
						    cmd);
						window_draw(w);
						if (pc->action)
							pc->action(w, pc,
							    pc->user);
						free(cmd);
						break;
					}
				}
				if (strcmp(cmd, "Fermer") == 0) {
					if (w->close == NULL ||
					    w->close(w, w->close_user))
						ptk_window_close(w);
					free(cmd);
					break;
				}
				if (strcmp(cmd, "Quitter") == 0) {
					ptk_app_quit(app);
					free(cmd);
					break;
				}
				/* Édition des contrôles texte, gérée par la
				 * Toolbox comme Mac OS 9 la gère dans le
				 * système ; l'application voit le reste */
				if (controle_texte(w->focus)) {
					if (strcmp(cmd, "Tout sélectionner") == 0) {
						w->focus->tout_selectionne = 1;
						window_draw(w);
						free(cmd);
						break;
					}
					if (strcmp(cmd, "Copier") == 0 ||
					    strcmp(cmd, "Couper") == 0) {
						copier_texte(app, w, w->focus,
						    strcmp(cmd, "Couper") == 0);
						free(cmd);
						break;
					}
					if (strcmp(cmd, "Coller") == 0) {
						if (XGetSelectionOwner(app->dpy,
						    app->a_clip) == None)
							ptk_beep(app);
						else {
							app->coller_cible =
							    w->focus;
							XConvertSelection(
							    app->dpy,
							    app->a_clip,
							    app->a_utf8,
							    app->a_utf8,
							    w->win,
							    CurrentTime);
							XFlush(app->dpy);
						}
						free(cmd);
						break;
					}
				}
				if (w->command)
					w->command(w, cmd, w->user);
				free(cmd);
			}
		}
		/* messages hors Toolbox (XDND) : les applications qui les
		 * attendent (Expander) les traitent, sinon ignorés */
		if (app->xdnd != NULL)
			app->xdnd(app, e, app->xdnd_user);
		break;
	default:
		break;
	}
}

static void boucle(PtkApp *app, const int *fini);

void
ptk_app_run(PtkApp *app)
{
	boucle(app, NULL);
}

/* les contrôles indéterminés (PTK_BARBIER, PTK_RECHERCHE) avancent d'un cran
 * de trame dans chaque fenêtre active visible ; retour : combien ont animé */
static int
barbier_avance(PtkApp *app)
{
	PtkWindow *w;
	int n = 0;

	for (w = app->windows; w != NULL; w = w->next) {
		PtkControl *c;

		if (!w->mapped || !w->active || w->disposed)
			continue;	/* inactive ou repliée : figée, comme Mac OS 9 */
		for (c = w->controls; c != NULL; c = c->next)
			if (c->visible && c->enabled &&
			    (c->kind == PTK_BARBIER || c->kind == PTK_RECHERCHE))
				break;
		if (c == NULL)
			continue;
		n++;
		/* seuls les contrôles animés sont repeints dans le tampon et recopiés :
		 * le reste de la fenêtre ne bouge pas (sur le 701, une image de
		 * barbier ne coûte plus la fenêtre entière) */
		if (w->back == None || w->back_w != w->w || w->back_h != w->h) {
			window_draw(w);
			continue;
		}
		w->surface = w->back;
		for (c = w->controls; c != NULL; c = c->next) {
			if (c->kind != PTK_BARBIER && c->kind != PTK_RECHERCHE)
				continue;
			c->defil = (c->defil + 1) % (c->kind == PTK_BARBIER ? 6 : 45);
			if (!c->visible)
				continue;
			control_draw(w, c);
			XCopyArea(w->p->dpy, w->back, w->win, w->p->gc, c->x, c->y,
			    (unsigned)c->w, (unsigned)c->h + 1, c->x, c->y);
		}
		XFlush(w->p->dpy);
	}
	return n;
}

/* boucle d'une fenêtre modale : jusqu'à ce que *fini passe à 1 (ou que
 * l'application quitte), avec les mêmes minuteries que ptk_app_run */
void
ptk_app_run_modal(PtkApp *app, const int *fini)
{
	boucle(app, fini);
}

static void
boucle(PtkApp *app, const int *fini)
{
	XEvent e;
	int fd = ConnectionNumber(app->dpy);

	app->clign_t = horloge() + CLIGNOTEMENT;
	while (app->running && (fini == NULL || !*fini)) {
		double t, echeance;

		while (XPending(app->dpy) > 0) {
			XNextEvent(app->dpy, &e);
			dispatch(app, &e);
		}
		if (!app->running || (fini != NULL && *fini))
			break;
		/* échéances : elles ne glissent plus quand des événements
		 * arrivent (le curseur clignote même pendant que la souris
		 * bouge) */
		t = horloge();
		if (t >= app->clign_t) {
			PtkWindow *w;
			PtkControl *c;

			for (w = app->windows; w; w = w->next) {
				c = w->focus;
				if (c != NULL && w->active &&
				    (c->kind == PTK_TEXTFIELD ||
				    c->kind == PTK_TEXTAREA)) {
					c->caret_on = !c->caret_on;
					window_draw(w);
				}
			}
			app->clign_t = t + CLIGNOTEMENT;
		}
		if (app->rep_c != NULL && t >= app->rep_t) {
			repetition(app);
			app->rep_t = t + REPETITION_PAS;
		}
		if (app->tic != NULL && app->tic_periode > 0 && t >= app->tic_t) {
			app->tic_t = t + app->tic_periode;
			app->tic(app, app->tic_user);
		}
		if (t >= app->barbier_t) {
			/* pôle de barbier et flèches : rien à animer → on
			 * reconsulte moins souvent, pour reprendre dès qu'une
			 * fenêtre en porte un */
			app->barbier_t = barbier_avance(app) ?
			    t + BARBIER_PAS : t + BARBIER_CREUX;
		}
		if (XPending(app->dpy) > 0)
			continue;
		echeance = app->clign_t;
		if (app->rep_c != NULL && app->rep_t < echeance)
			echeance = app->rep_t;
		if (app->tic != NULL && app->tic_periode > 0 && app->tic_t < echeance)
			echeance = app->tic_t;
		if (app->barbier_t < echeance)
			echeance = app->barbier_t;
		{
			fd_set fds;
			struct timeval tv;
			double r = echeance - horloge();
			int ext = app->fd_ext, max = fd;

			if (r < 0)
				r = 0;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			if (ext >= 0 && app->fd_pret != NULL) {
				FD_SET(ext, &fds);
				if (ext > max)
					max = ext;
			}
			tv.tv_sec = (long)r;
			tv.tv_usec = (long)((r - (long)r) * 1e6);
			if (select(max + 1, &fds, NULL, NULL, &tv) > 0 && ext >= 0 &&
			    app->fd_pret != NULL && FD_ISSET(ext, &fds))
				app->fd_pret(app, app->fd_user);
		}
	}
}

void
ptk_app_traiter(PtkApp *app)
{
	XEvent e;

	/* les pôles de barbier avancent aussi pendant un long travail (la
	 * boucle proprement dite ne tourne pas) */
	if (horloge() >= app->barbier_t)
		app->barbier_t = barbier_avance(app) ?
		    horloge() + BARBIER_PAS : horloge() + BARBIER_CREUX;
	while (XPending(app->dpy) > 0) {
		XNextEvent(app->dpy, &e);
		dispatch(app, &e);
	}
	XFlush(app->dpy);
}

void
ptk_app_quit(PtkApp *app)
{
	PtkWindow *w;

	/* comme la fermeture d'une fenêtre : un document modifié gardé ouvert
	 * (Annuler dans l'alerte) interrompt la sortie */
	app->en_sortie = 1;
	for (w = app->windows; w != NULL; w = w->next)
		if (w->close != NULL && !w->close(w, w->close_user)) {
			app->en_sortie = 0;
			return;
		}
	app->running = 0;
}

void
ptk_beep(PtkApp *app)
{
	ptk_son(&app->ptk, "lerr");
}

/* bouton de l'alerte sous le point, -1 sinon */
static int
alerte_bouton_sous(const int *bx, const int *bw, int n, int by, int x, int y)
{
	int i;

	for (i = 0; i < n; i++)
		if (x >= bx[i] && x < bx[i] + bw[i] && y >= by && y < by + 20)
			return i;
	return -1;
}

static int
alert_buttons(const char *buttons, char out[4][64])
{
	int n = 0;
	char buf[256];
	char *p, *save;

	snprintf(buf, sizeof(buf), "%s", buttons ? buttons : "OK");
	for (p = strtok_r(buf, "|", &save); p && n < 4;
	    p = strtok_r(NULL, "|", &save))
		snprintf(out[n++], 64, "%s", p);
	if (n == 0)
		snprintf(out[n++], 64, "OK");
	return n;
}

int
ptk_alert(PtkWindow *parent, const char *message, const char *detail,
    const char *buttons)
{
	PtkApp *app = parent->app;
	Ptk *p = &app->ptk;
	char labels[4][64];
	int n, i, w, h, x, y, by;
	Window win;
	XSetWindowAttributes a;
	int chosen = -1;
	int bx[4], bw[4], total_b = 0;
	int presse = -1;	/* bouton maintenu à la souris */
	int msg_lines, det_lines;
	int sw = DisplayWidth(app->dpy, app->screen);
	int sh = DisplayHeight(app->dpy, app->screen);

	ptk_beep(app);
	n = alert_buttons(buttons, labels);
	w = 300;
	for (i = 0; i < n; i++) {
		/* 82 px comme Mac OS 9, élargi pour les libellés longs */
		int tw = ptk_text_width(p, p->font_menu_bold, labels[i]) + 24;

		bw[i] = tw > 82 ? tw : 82;
		total_b += bw[i] + (i ? 12 : 0);
	}
	if (w < 320)
		w = 320;
	if (w < total_b + 40)	/* jusqu'à quatre boutons sur une rangée */
		w = total_b + 40;
	if (w > sw - 40)
		w = sw - 40;
	msg_lines = text_wrap_lines(p, p->font_title, message, w - 84);
	if (msg_lines < 1)
		msg_lines = 1;
	det_lines = text_wrap_lines(p, p->font_menu, detail, w - 84);
	h = 56 + msg_lines * 16 + (det_lines ? 6 + det_lines * 16 : 0) +
	    24 + 44;
	if (h < 156)
		h = 156;
	by = h - 38;
	x = (sw - w) / 2;
	y = (sh - h) / 2;
	a.override_redirect = True;
	a.background_pixel = p->face.pixel;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask | KeyPressMask;
	win = XCreateWindow(app->dpy, app->root, x, y, (unsigned)w,
	    (unsigned)h, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	XMapRaised(app->dpy, win);
	XGrabPointer(app->dpy, win, False, ButtonPressMask |
	    ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
	    None, None, CurrentTime);
	/* le clavier peut encore être pris (raccourci du WM qui a lancé l'alerte) : réessayer */
	for (i = 0; i < 50 && XGrabKeyboard(app->dpy, win, False, GrabModeAsync,
	    GrabModeAsync, CurrentTime) != GrabSuccess; i++)
		usleep(20000);
	XSync(app->dpy, False);
	XRaiseWindow(app->dpy, win);
	XSync(app->dpy, False);

	for (;;) {
		XEvent e;
		XNextEvent(app->dpy, &e);
		if (e.type == Expose && e.xexpose.count == 0) {
			ptk_fill(p, win, 0, 0, w, h, p->face);
			ptk_rect(p, win, 0, 0, w, h, p->black);
			ptk_line(p, win, 1, 1, w - 2, 1, p->white);
			ptk_line(p, win, 1, 1, 1, h - 2, p->white);
			if (app->icon_pix != None)
				ptk_draw_pixmap(p, win, app->icon_pix,
				    app->icon_mask, 20, 24, app->icon_w,
				    app->icon_h);
			else
				ptk_icon(p, win, 20, 24, 32, "app");
			{
				int ty = draw_wrapped(p, win, p->font_title,
				    64, 40, w - 84, message, 16, p->text);
				if (detail)
					draw_wrapped(p, win, p->font_menu,
					    64, ty + 6, w - 84, detail, 16,
					    p->text);
			}
			{
				int total = 0;
				int xx;
				for (i = 0; i < n; i++)
					total += bw[i] + (i ? 12 : 0);
				xx = w - 20 - total;
				for (i = 0; i < n; i++) {
					bx[i] = xx;
					ptk_button(p, win, xx, by, bw[i],
					    20, labels[i], 0,
					    i == n - 1, p->text);
					xx += bw[i] + 12;
				}
			}
			XFlush(app->dpy);
		} else if (e.type == ButtonPress) {
			/* comme TrackControl : le bouton s'enfonce, suit le
			 * pointeur, et n'agit que relâché dessus */
			presse = alerte_bouton_sous(bx, bw, n, by, e.xbutton.x,
			    e.xbutton.y);
			if (presse >= 0) {
				ptk_son(p, "btnp");
				ptk_button(p, win, bx[presse], by, bw[presse], 20,
				    labels[presse], 1, presse == n - 1, p->text);
				XFlush(app->dpy);
			}
		} else if (e.type == MotionNotify && presse >= 0) {
			int sous = alerte_bouton_sous(bx, bw, n, by,
			    e.xmotion.x, e.xmotion.y) == presse;

			ptk_button(p, win, bx[presse], by, bw[presse], 20,
			    labels[presse], sous, presse == n - 1, p->text);
			XFlush(app->dpy);
		} else if (e.type == ButtonRelease) {
			i = alerte_bouton_sous(bx, bw, n, by, e.xbutton.x,
			    e.xbutton.y);
			if (presse >= 0 && i == presse) {
				ptk_son(p, "btnr");
				chosen = i;
				break;
			}
			if (presse >= 0) {
				ptk_button(p, win, bx[presse], by, bw[presse], 20,
				    labels[presse], 0, presse == n - 1, p->text);
				XFlush(app->dpy);
			}
			presse = -1;
		} else if (e.type == KeyPress) {
			KeySym ks = XLookupKeysym(&e.xkey, 0);
			if (ks == XK_Return || ks == XK_KP_Enter)
				chosen = n - 1;
			else if (ks == XK_Escape || (ks == XK_period &&
			    (e.xkey.state & Mod1Mask))) {
				/* Échap vaut « Annuler » où qu'il soit (alerte de la touche d'alimentation :
				 * Redémarrer | Suspendre | Annuler | Éteindre), sinon le premier bouton */
				chosen = 0;
				for (i = 0; i < n; i++)
					if (strcmp(labels[i], "Annuler") == 0)
						chosen = i;
			}
			if (chosen >= 0) {
				/* le bouton choisi au clavier clignote 8 ticks,
				 * sans son : les sons Platinum suivent la souris */
				ptk_button(p, win, bx[chosen], by, bw[chosen], 20,
				    labels[chosen], 1, chosen == n - 1, p->text);
				XSync(app->dpy, False);
				usleep(FLASH_US);
				ptk_button(p, win, bx[chosen], by, bw[chosen], 20,
				    labels[chosen], 0, chosen == n - 1, p->text);
				XSync(app->dpy, False);
				break;
			}
		}
	}
	XUngrabKeyboard(app->dpy, CurrentTime);
	XUngrabPointer(app->dpy, CurrentTime);
	XDestroyWindow(app->dpy, win);
	XFlush(app->dpy);
	return chosen;
}

/* --- Zone de dialogue d'enregistrement (Standard File, Mac OS 9) ---
 *
 * Relevée sur la VM (SimpleText, ⌘S ; audit/macos9-ref/enregistrer-standard-file.png) :
 * boîte de 356×250 sans barre de titre, trait noir, liseré #bbb en haut et à gauche,
 * #999 puis #555 en bas et à droite, ombre noire de 1 px ; fond blanc ; menu local du
 * dossier en haut, disque à droite ; liste 218×146 ; Éjecter, Bureau, Nouveau, trait,
 * Annuler, Enregistrer (par défaut) dans une colonne de 80 px ; invite en gras et champ
 * du nom en bas. Une rangée de dossier sélectionnée dans la liste change Enregistrer en
 * Ouvrir ; un double-clic l'ouvre ; les fichiers sont grisés.
 */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <X11/extensions/shape.h>

#define DLG_L 356
#define DLG_H 250

typedef struct {
	PtkWindow *w;
	PtkControl *chemin, *liste, *nom, *ejecter, *bureau, *nouveau, *annuler, *ok;
	char dossier[PATH_MAX];
	char entrees[256][256];
	int dossier_de[256];
	int n;
	int fini, reponse;
	char resultat[PATH_MAX];
	const char *invite;
	int ouvrir;		/* StandardGetFile : choisir un document existant */
	const char *types;	/* extensions acceptées, séparées par des espaces (NULL : toutes) */
	int choisir_dossier;	/* choisir un dossier : seuls les dossiers sont montrés, Choisir renvoie le dossier choisi */
} Dialogue;

/* le document a-t-il une extension que l'application ouvre ? */
static int
dlg_type_ok(const Dialogue *d, const char *nom)
{
	const char *e = strrchr(nom, '.'), *t;
	size_t l;

	if (d->types == NULL || d->types[0] == '\0')
		return 1;
	if (e == NULL || e == nom)
		return 0;
	e++;
	l = strlen(e);
	for (t = d->types; *t; ) {
		size_t n = strcspn(t, " ");

		if (n == l && strncasecmp(t, e, l) == 0)
			return 1;
		t += n;
		while (*t == ' ')
			t++;
	}
	return 0;
}

static int
dlg_cmp(const void *a, const void *b)
{
	return strcoll((const char *)a, (const char *)b);
}

/* cadre d'une boîte de dialogue modale Platinum, relevé sur la VM */
static void
dlg_cadre(PtkWindow *w)
{
	Ptk *p = w->p;
	int R = DLG_L - 1, B = DLG_H - 1;

	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->white);
	ptk_rect(p, w->surface, 0, 0, DLG_L, DLG_H, p->black);
	ptk_line(p, w->surface, 1, 1, R - 1, 1, ptk_color(p, "#bbbbbb"));
	ptk_line(p, w->surface, 1, 1, 1, B - 1, ptk_color(p, "#bbbbbb"));
	ptk_line(p, w->surface, 2, B - 2, R - 2, B - 2, ptk_color(p, "#999999"));
	ptk_line(p, w->surface, R - 2, 2, R - 2, B - 2, ptk_color(p, "#999999"));
	ptk_line(p, w->surface, 2, B - 1, R - 1, B - 1, ptk_color(p, "#555555"));
	ptk_line(p, w->surface, R - 1, 2, R - 1, B - 1, ptk_color(p, "#555555"));
	/* ombre portée */
	ptk_line(p, w->surface, DLG_L, 1, DLG_L, DLG_H, p->black);
	ptk_line(p, w->surface, 2, DLG_H, DLG_L, DLG_H, p->black);
}

/* fenêtre modale hors gestion : pas de cadre du WM, coins de l'ombre découpés */
static void
dlg_rendre_modale(PtkWindow *w)
{
	XSetWindowAttributes a;
	XRectangle r[3] = {
		{ 0, 0, DLG_L, DLG_H }, { DLG_L, 1, 1, DLG_H }, { 2, DLG_H, DLG_L - 1, 1 },
	};

	a.override_redirect = True;
	XChangeWindowAttributes(w->app->dpy, w->win, CWOverrideRedirect, &a);
	XShapeCombineRectangles(w->app->dpy, w->win, ShapeBounding, 0, 0, r, 3, ShapeSet,
	    Unsorted);
	w->active = 1;
}

static void
dlg_montrer(PtkWindow *w)
{
	int i;

	ptk_window_show(w);
	XRaiseWindow(w->app->dpy, w->win);
	XSync(w->app->dpy, False);
	for (i = 0; i < 50 && XGrabKeyboard(w->app->dpy, w->win, False, GrabModeAsync,
	    GrabModeAsync, CurrentTime) != GrabSuccess; i++)
		usleep(20000);
	w->active = 1;
	ptk_window_invalidate(w);
}

static void
dlg_fermer(PtkWindow *w)
{
	XUngrabKeyboard(w->app->dpy, CurrentTime);
	ptk_window_close(w);
}

static void
dlg_dessin(PtkWindow *w, void *user)
{
	Dialogue *d = user;
	Ptk *p = w->p;
	Pixmap ip = None, im = None;
	char t[300];

	dlg_cadre(w);
	/* le disque, à droite du menu local */
	if (ptk_load_xpm(p, "menu/disque.xpm", &ip, &im) == 0 && ip != None) {
		ptk_draw_pixmap(p, w->surface, ip, im, 256, 14, 16, 16);
		XFreePixmap(p->dpy, ip);
		if (im != None)
			XFreePixmap(p->dpy, im);
	}
	ptk_text(p, w->surface, p->font_menu_bold, 276, 26, "Eee PC 701", p->text);
	ptk_line(p, w->surface, 258, 124, 337, 124, ptk_color(p, "#888888"));
	ptk_line(p, w->surface, 259, 125, 338, 125, p->white);
	if (!d->ouvrir) {
		snprintf(t, sizeof(t), "%s", d->invite);
		ptk_text(p, w->surface, p->font_menu_bold, 18, 207, t, p->text);
	}
}

/* le menu local : du dossier courant jusqu'au volume « Eee PC 701 » */
static void
dlg_chemin(Dialogue *d)
{
	char items[sizeof(d->chemin->items)] = "", c[PATH_MAX];
	const char *vol = ptk_volume();
	size_t lv = strlen(vol);

	char premier[256] = "";

	snprintf(c, sizeof(c), "%s", d->dossier);
	for (;;) {
		char parent[PATH_MAX];
		const char *nom, *s0 = strrchr(c, '/');
		size_t l = strlen(items);
		char *s;

		/* nom montré : le disque, ou le nom traduit d'un dossier du Macintosh */
		snprintf(parent, sizeof(parent), "%.*s", s0 == NULL || s0 == c ? 1 : (int)(s0 - c), c);
		nom = strcmp(c, vol) == 0 ? "Eee PC 701" : ptk_nom_affiche(parent, s0 != NULL ? s0 + 1 : c);
		if (premier[0] == '\0')
			snprintf(premier, sizeof(premier), "%s", nom);
		snprintf(items + l, sizeof(items) - l, "%s%s", l ? "|" : "", nom);
		if (strcmp(c, vol) == 0 || strlen(c) <= lv || (s = strrchr(c, '/')) == NULL)
			break;
		if (s == c) {
			/* le volume est la racine du disque : « / » ferme le menu */
			if (strcmp(vol, "/") != 0)
				break;
			snprintf(c, sizeof(c), "/");
			continue;
		}
		*s = '\0';
	}
	ptk_control_set_items(d->chemin, items);
	ptk_control_set_text(d->chemin, premier);
}

static void
dlg_remplir(Dialogue *d)
{
	DIR *r = opendir(d->dossier);
	struct dirent *e;
	char items[sizeof(d->liste->items)] = "";
	int i;

	d->n = 0;
	/* comme le Finder : noms en point et dossiers BSD de la racine masqués */
	while (r != NULL && d->n < 256 && (e = readdir(r)) != NULL)
		if (!ptk_masque(d->dossier, e->d_name))
			snprintf(d->entrees[d->n++], sizeof(d->entrees[0]), "%s", e->d_name);
	if (r != NULL)
		closedir(r);
	qsort(d->entrees, (size_t)d->n, sizeof(d->entrees[0]), dlg_cmp);
	for (i = 0; i < d->n; i++) {
		char ch[PATH_MAX + 256];
		struct stat st;
		size_t l = strlen(items);

		snprintf(ch, sizeof(ch), "%s/%s", d->dossier, d->entrees[i]);
		d->dossier_de[i] = stat(ch, &st) == 0 && S_ISDIR(st.st_mode);
		if (d->choisir_dossier) {
			/* « Autre… » de l'Installateur : seuls les dossiers sont montrés */
			if (!d->dossier_de[i]) {
				memmove(d->entrees + i, d->entrees + i + 1,
				    (size_t)(d->n - i - 1) * sizeof(d->entrees[0]));
				d->n--;
				i--;
				continue;
			}
			snprintf(items + l, sizeof(items) - l, "%s\x02%s\x02%s",
			    l ? "|" : "", "dossier", ptk_nom_affiche(d->dossier, d->entrees[i]));
			continue;
		}
		if (d->ouvrir) {
			/* Ouvrir : seuls les dossiers et les documents que l'application
			 * sait ouvrir sont montrés, comme le filtre de StandardGetFile */
			if (!d->dossier_de[i] && !dlg_type_ok(d, d->entrees[i])) {
				memmove(d->entrees + i, d->entrees + i + 1,
				    (size_t)(d->n - i - 1) * sizeof(d->entrees[0]));
				d->n--;
				i--;
				continue;
			}
			snprintf(items + l, sizeof(items) - l, "%s\x02%s\x02%s", l ? "|" : "",
			    d->dossier_de[i] ? "dossier" : "document",
			    d->dossier_de[i] ? ptk_nom_affiche(d->dossier, d->entrees[i]) : d->entrees[i]);
			continue;
		}
		/* dossiers ouvrables ; documents et programmes grisés, comme Standard File */
		snprintf(items + l, sizeof(items) - l, "%s%s\x02%s\x02%s", l ? "|" : "",
		    d->dossier_de[i] ? "" : "~",
		    d->dossier_de[i] ? "dossier" : access(ch, X_OK) == 0 ? "application" :
		    "document", d->dossier_de[i] ? ptk_nom_affiche(d->dossier, d->entrees[i]) :
		    d->entrees[i]);
	}
	ptk_control_set_items(d->liste, items);
	d->liste->value = -1;
	d->liste->defil = 0;
	dlg_chemin(d);
	snprintf(d->ok->label, sizeof(d->ok->label), "%s",
	    d->ouvrir ? "Ouvrir" : d->choisir_dossier ? "Choisir" : "Enregistrer");
	d->ok->enabled = !d->ouvrir;	/* Ouvrir : actif quand une rangée est choisie */
	ptk_window_invalidate(d->w);
}

static void dlg_ok(PtkWindow *w, PtkControl *c, void *user);

static void
dlg_aller(Dialogue *d, const char *dossier)
{
	/* chemin trop long : on reste où l'on est */
	if ((size_t)snprintf(d->dossier, sizeof(d->dossier), "%s", dossier) >= sizeof(d->dossier))
		snprintf(d->dossier, sizeof(d->dossier), "%s", ptk_volume());
	dlg_remplir(d);
	/* comme Standard File : après un changement de dossier, le clavier revient
	 * au nom du document, sélectionné ; Tab l'envoie à la liste */
	if (d->nom != NULL && d->w != NULL) {
		focus_control(d->w, d->nom);
		d->nom->tout_selectionne = 1;
	}
}

static void
dlg_liste_action(PtkWindow *w, PtkControl *c, void *user)
{
	Dialogue *d = user;
	int i = c->value;

	if (i < 0 || i >= d->n)
		return;
	if (d->ouvrir) {
		d->ok->enabled = 1;
		if (c->double_clic)
			dlg_ok(w, d->ok, d);
		else
			ptk_window_invalidate(w);
		return;
	}
	if (d->choisir_dossier) {
		/* un dossier se choisit d'un clic, s'ouvre d'un double-clic */
		if (c->double_clic) {
			char ch[PATH_MAX];

			if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier,
			    d->entrees[i]) < sizeof(ch))
				dlg_aller(d, ch);
			return;
		}
		ptk_window_invalidate(w);
		return;
	}
	/* une rangée de dossier : Enregistrer devient Ouvrir ; double-clic l'ouvre */
	if (d->dossier_de[i] && c->double_clic) {
		char ch[PATH_MAX];

		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier, d->entrees[i]) < sizeof(ch))
			dlg_aller(d, ch);
		return;
	}
	snprintf(d->ok->label, sizeof(d->ok->label), "%s",
	    d->dossier_de[i] ? "Ouvrir" : "Enregistrer");
	ptk_window_invalidate(w);
}

static void
dlg_chemin_action(PtkWindow *w, PtkControl *c, void *user)
{
	Dialogue *d = user;
	char items[sizeof(c->items)], *it, *sv, ch[PATH_MAX];
	int k = 0;

	(void)w;
	/* combien de crans remonter : rang de l'élément choisi dans le menu */
	snprintf(items, sizeof(items), "%s", c->items);
	for (it = strtok_r(items, "|", &sv); it != NULL; it = strtok_r(NULL, "|", &sv), k++)
		if (strcmp(it, c->text) == 0)
			break;
	snprintf(ch, sizeof(ch), "%s", d->dossier);
	while (k-- > 0) {
		char *s = strrchr(ch, '/');

		if (s == NULL)
			break;
		if (s == ch) {	/* jusqu'à la racine du disque */
			snprintf(ch, sizeof(ch), "/");
			break;
		}
		*s = '\0';
	}
	dlg_aller(d, ch);
}

static void
dlg_bureau(PtkWindow *w, PtkControl *c, void *user)
{
	char b[PATH_MAX];

	(void)w;
	(void)c;
	/* Bureau : le dossier du bureau, comme le bouton de Standard File */
	dlg_aller(user, ptk_bureau(b, sizeof(b)) ? b : ptk_volume());
}

static void
dlg_annuler(PtkWindow *w, PtkControl *c, void *user)
{
	Dialogue *d = user;

	(void)w;
	(void)c;
	d->reponse = 0;
	d->fini = 1;
}

/* nom d'un nouveau dossier : petite boîte modale, comme Standard File */
static int nd_fini, nd_ok;

static void
nd_annuler(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	(void)user;
	nd_ok = 0;
	nd_fini = 1;
}

static void
nd_creer(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	(void)user;
	nd_ok = 1;
	nd_fini = 1;
}

static void
nd_dessin(PtkWindow *w, void *user)
{
	Ptk *p = w->p;

	(void)user;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	ptk_rect(p, w->surface, 0, 0, w->w, w->h, p->black);
	ptk_line(p, w->surface, 1, 1, w->w - 2, 1, p->white);
	ptk_line(p, w->surface, 1, 1, 1, w->h - 2, p->white);
	ptk_text(p, w->surface, p->font_menu_bold, 16, 24, "Nom du nouveau dossier :", p->text);
}

static void
dlg_nouveau(PtkWindow *w, PtkControl *c, void *user)
{
	Dialogue *d = user;
	PtkWindow *n;
	PtkControl *champ, *b;
	char nom[256] = "", ch[PATH_MAX + 256];

	(void)c;
	n = ptk_window_new(w->app, "Nouveau dossier", w->x + 38, w->y + 60, 280, 110);
	{
		XSetWindowAttributes a;

		a.override_redirect = True;
		XChangeWindowAttributes(w->app->dpy, n->win, CWOverrideRedirect, &a);
	}
	ptk_window_set_draw(n, nd_dessin, NULL);
	champ = ptk_control_add(n, PTK_TEXTFIELD, 16, 34, 248, 22, "");
	ptk_control_set_text(champ, "dossier sans titre");
	champ->tout_selectionne = 1;
	b = ptk_control_add(n, PTK_BUTTON, 86, 76, 82, 20, "Annuler");
	ptk_control_set_action(b, nd_annuler, NULL);
	b = ptk_control_add(n, PTK_DEFAULT_BUTTON, 182, 76, 82, 20, "Créer");
	ptk_control_set_action(b, nd_creer, NULL);
	nd_fini = nd_ok = 0;
	dlg_montrer(n);
	focus_control(n, champ);
	ptk_app_run_modal(w->app, &nd_fini);
	commit_text_focus(n);
	if (nd_ok && (size_t)snprintf(nom, sizeof(nom), "%s", champ->text) >= sizeof(nom))
		nom[0] = '\0';
	dlg_fermer(n);
	XGrabKeyboard(w->app->dpy, w->win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	if (nom[0] == '\0' || strchr(nom, '/') != NULL)
		return;
	snprintf(ch, sizeof(ch), "%s/%s", d->dossier, nom);
	if (mkdir(ch, 0755) != 0) {
		ptk_alert(w, "Le dossier ne peut pas être créé.", strerror(errno), "OK");
		return;
	}
	dlg_aller(d, ch);
}

static void
dlg_ok(PtkWindow *w, PtkControl *c, void *user)
{
	Dialogue *d = user;
	char nom[256], ch[PATH_MAX + 256], m[400];
	struct stat st;
	size_t l;

	(void)c;
	commit_text_focus(w);
	if (d->choisir_dossier) {
		/* un dossier choisi dans la liste, sinon le dossier affiché */
		int i = d->liste->value;

		if (i < 0 || i >= d->n) {
			snprintf(d->resultat, sizeof(d->resultat), "%s", d->dossier);
			d->reponse = 1;
			d->fini = 1;
			return;
		}
		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier,
		    d->entrees[i]) >= sizeof(d->resultat)) {
			ptk_beep(w->app);
			return;
		}
		memcpy(d->resultat, ch, strlen(ch) + 1);
		d->reponse = 1;
		d->fini = 1;
		return;
	}
	if (d->ouvrir) {
		int i = d->liste->value;

		if (i < 0 || i >= d->n) {
			ptk_beep(w->app);
			return;
		}
		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier, d->entrees[i]) >=
		    sizeof(d->resultat))
			return;
		if (d->dossier_de[i]) {
			dlg_aller(d, ch);
			return;
		}
		memcpy(d->resultat, ch, strlen(ch) + 1);
		d->reponse = 1;
		d->fini = 1;
		return;
	}
	/* Ouvrir : la rangée de dossier sélectionnée */
	if (strcmp(d->ok->label, "Ouvrir") == 0 && d->liste->value >= 0 &&
	    d->liste->value < d->n && d->dossier_de[d->liste->value]) {
		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier,
		    d->entrees[d->liste->value]) < sizeof(d->dossier))
			dlg_aller(d, ch);
		return;
	}
	if ((size_t)snprintf(nom, sizeof(nom), "%s", d->nom->text) >= sizeof(nom)) {
		ptk_beep(w->app);
		return;
	}
	for (l = strlen(nom); l > 0 && nom[l - 1] == ' '; l--)
		nom[l - 1] = '\0';
	if (nom[0] == '\0' || strchr(nom, '/') != NULL || nom[0] == '.') {
		ptk_beep(w->app);
		return;
	}
	if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", d->dossier, nom) >= sizeof(d->resultat)) {
		ptk_beep(w->app);
		return;
	}
	if (stat(ch, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			snprintf(m, sizeof(m), "« %s » est un dossier : choisissez un autre nom.", nom);
			ptk_alert(w, m, NULL, "OK");
			XGrabKeyboard(w->app->dpy, w->win, False, GrabModeAsync, GrabModeAsync,
			    CurrentTime);
			return;
		}
		snprintf(m, sizeof(m), "Remplacer « %s » existant ?", nom);
		if (ptk_alert(w, m, NULL, "Annuler|Remplacer") != 1) {
			XGrabKeyboard(w->app->dpy, w->win, False, GrabModeAsync, GrabModeAsync,
			    CurrentTime);
			return;
		}
	}
	memcpy(d->resultat, ch, strlen(ch) + 1);	/* longueur vérifiée plus haut */
	d->reponse = 1;
	d->fini = 1;
}

int
ptk_enregistrer(PtkApp *app, const char *invite, const char *nom, const char *dossier,
    char *chemin, size_t n)
{
	Dialogue d;
	int sw = DisplayWidth(app->dpy, app->screen), sh = DisplayHeight(app->dpy, app->screen);
	int garde = app->quit_on_last_close;
	struct stat st;

	memset(&d, 0, sizeof(d));
	d.invite = invite != NULL ? invite : "Enregistrer le document sous :";
	if (dossier != NULL && stat(dossier, &st) == 0 && S_ISDIR(st.st_mode))
		snprintf(d.dossier, sizeof(d.dossier), "%s", dossier);
	else if (ptk_perso("Documents", d.dossier, sizeof(d.dossier)) &&
	    (stat(d.dossier, &st) != 0 || !S_ISDIR(st.st_mode)))
		snprintf(d.dossier, sizeof(d.dossier), "%s", ptk_volume());
	app->quit_on_last_close = 0;
	d.w = ptk_window_new(app, "Enregistrer", (sw - DLG_L) / 2, (sh - DLG_H) / 3,
	    DLG_L + 1, DLG_H + 1);
	if (d.w == NULL) {
		app->quit_on_last_close = garde;
		return 0;
	}
	dlg_rendre_modale(d.w);
	ptk_window_set_draw(d.w, dlg_dessin, &d);
	d.chemin = ptk_control_add(d.w, PTK_POPUP, 26, 12, 202, 19, "");
	ptk_control_set_action(d.chemin, dlg_chemin_action, &d);
	d.liste = ptk_control_add(d.w, PTK_LISTE, 18, 35, 218, 146, "");
	d.liste->police = app->ptk.font_menu;	/* Charcoal 12, comme Standard File */
	ptk_control_set_action(d.liste, dlg_liste_action, &d);
	d.ejecter = ptk_control_add(d.w, PTK_BUTTON, 258, 38, 80, 20, "Éjecter");
	d.ejecter->enabled = 0;
	d.bureau = ptk_control_add(d.w, PTK_BUTTON, 258, 66, 80, 20, "Bureau");
	ptk_control_set_action(d.bureau, dlg_bureau, &d);
	d.nouveau = ptk_control_add(d.w, PTK_BUTTON, 258, 93, 80, 20, "Nouveau…");
	ptk_control_set_action(d.nouveau, dlg_nouveau, &d);
	d.annuler = ptk_control_add(d.w, PTK_BUTTON, 258, 136, 80, 20, "Annuler");
	ptk_control_set_action(d.annuler, dlg_annuler, &d);
	d.ok = ptk_control_add(d.w, PTK_DEFAULT_BUTTON, 258, 166, 80, 20, "Enregistrer");
	ptk_control_set_action(d.ok, dlg_ok, &d);
	d.nom = ptk_control_add(d.w, PTK_TEXTFIELD, 18, 212, 204, 22, "");
	ptk_control_set_text(d.nom, nom != NULL ? nom : "sans titre");
	d.nom->tout_selectionne = 1;
	dlg_remplir(&d);
	dlg_montrer(d.w);
	focus_control(d.w, d.nom);
	ptk_app_run_modal(app, &d.fini);
	dlg_fermer(d.w);
	app->quit_on_last_close = garde;
	if (d.reponse)
		snprintf(chemin, n, "%s", d.resultat);
	return d.reponse;
}

/* Zone de dialogue d'ouverture, à l'image de StandardGetFile sous Mac OS 9 : menu local du
 * dossier, disque, liste des dossiers et des documents acceptés (types : extensions
 * séparées par des espaces, NULL : tout), Éjecter, Bureau, Annuler, Ouvrir. Modale.
 * 1 et le chemin du document choisi, 0 si Annuler. */
int
ptk_ouvrir(PtkApp *app, const char *types, const char *dossier, char *chemin, size_t n)
{
	Dialogue d;
	int sw = DisplayWidth(app->dpy, app->screen), sh = DisplayHeight(app->dpy, app->screen);
	int garde = app->quit_on_last_close;
	struct stat st;

	memset(&d, 0, sizeof(d));
	d.ouvrir = 1;
	d.types = types;
	d.invite = "";
	if (dossier != NULL && stat(dossier, &st) == 0 && S_ISDIR(st.st_mode))
		snprintf(d.dossier, sizeof(d.dossier), "%s", dossier);
	else if (ptk_perso("Documents", d.dossier, sizeof(d.dossier)) &&
	    (stat(d.dossier, &st) != 0 || !S_ISDIR(st.st_mode)))
		snprintf(d.dossier, sizeof(d.dossier), "%s", ptk_volume());
	app->quit_on_last_close = 0;
	d.w = ptk_window_new(app, "Ouvrir", (sw - DLG_L) / 2, (sh - DLG_H) / 3,
	    DLG_L + 1, DLG_H + 1);
	if (d.w == NULL) {
		app->quit_on_last_close = garde;
		return 0;
	}
	dlg_rendre_modale(d.w);
	ptk_window_set_draw(d.w, dlg_dessin, &d);
	d.chemin = ptk_control_add(d.w, PTK_POPUP, 26, 12, 202, 19, "");
	ptk_control_set_action(d.chemin, dlg_chemin_action, &d);
	d.liste = ptk_control_add(d.w, PTK_LISTE, 18, 35, 218, DLG_H - 50, "");
	d.liste->police = app->ptk.font_menu;
	ptk_control_set_action(d.liste, dlg_liste_action, &d);
	d.ejecter = ptk_control_add(d.w, PTK_BUTTON, 258, 38, 80, 20, "Éjecter");
	d.ejecter->enabled = 0;
	d.bureau = ptk_control_add(d.w, PTK_BUTTON, 258, 66, 80, 20, "Bureau");
	ptk_control_set_action(d.bureau, dlg_bureau, &d);
	d.annuler = ptk_control_add(d.w, PTK_BUTTON, 258, DLG_H - 74, 80, 20, "Annuler");
	ptk_control_set_action(d.annuler, dlg_annuler, &d);
	d.ok = ptk_control_add(d.w, PTK_DEFAULT_BUTTON, 258, DLG_H - 44, 80, 20, "Ouvrir");
	ptk_control_set_action(d.ok, dlg_ok, &d);
	dlg_remplir(&d);
	dlg_montrer(d.w);
	focus_control(d.w, d.liste);
	ptk_app_run_modal(app, &d.fini);
	dlg_fermer(d.w);
	app->quit_on_last_close = garde;
	if (d.reponse)
		snprintf(chemin, n, "%s", d.resultat);
	return d.reponse;
}

int
ptk_choisir_dossier(PtkApp *app, const char *invite, const char *dossier, char *chemin, size_t n)
{
	Dialogue d;
	int sw = DisplayWidth(app->dpy, app->screen), sh = DisplayHeight(app->dpy, app->screen);
	int garde = app->quit_on_last_close;
	struct stat st;

	memset(&d, 0, sizeof(d));
	d.choisir_dossier = 1;
	d.invite = invite != NULL ? invite : "Choisissez un dossier :";
	if (dossier != NULL && stat(dossier, &st) == 0 && S_ISDIR(st.st_mode))
		snprintf(d.dossier, sizeof(d.dossier), "%s", dossier);
	else if (ptk_perso("Documents", d.dossier, sizeof(d.dossier)) &&
	    (stat(d.dossier, &st) != 0 || !S_ISDIR(st.st_mode)))
		snprintf(d.dossier, sizeof(d.dossier), "%s", ptk_volume());
	app->quit_on_last_close = 0;
	d.w = ptk_window_new(app, "Choisir un dossier", (sw - DLG_L) / 2, (sh - DLG_H) / 3,
	    DLG_L + 1, DLG_H + 1);
	if (d.w == NULL) {
		app->quit_on_last_close = garde;
		return 0;
	}
	dlg_rendre_modale(d.w);
	ptk_window_set_draw(d.w, dlg_dessin, &d);
	d.chemin = ptk_control_add(d.w, PTK_POPUP, 26, 12, 202, 19, "");
	ptk_control_set_action(d.chemin, dlg_chemin_action, &d);
	d.liste = ptk_control_add(d.w, PTK_LISTE, 18, 35, 218, DLG_H - 50, "");
	d.liste->police = app->ptk.font_menu;
	ptk_control_set_action(d.liste, dlg_liste_action, &d);
	d.ejecter = ptk_control_add(d.w, PTK_BUTTON, 258, 38, 80, 20, "Éjecter");
	d.ejecter->enabled = 0;
	d.bureau = ptk_control_add(d.w, PTK_BUTTON, 258, 66, 80, 20, "Bureau");
	ptk_control_set_action(d.bureau, dlg_bureau, &d);
	d.annuler = ptk_control_add(d.w, PTK_BUTTON, 258, DLG_H - 74, 80, 20, "Annuler");
	ptk_control_set_action(d.annuler, dlg_annuler, &d);
	d.ok = ptk_control_add(d.w, PTK_DEFAULT_BUTTON, 258, DLG_H - 44, 80, 20, "Choisir");
	ptk_control_set_action(d.ok, dlg_ok, &d);
	dlg_remplir(&d);
	dlg_montrer(d.w);
	focus_control(d.w, d.liste);
	ptk_app_run_modal(app, &d.fini);
	dlg_fermer(d.w);
	app->quit_on_last_close = garde;
	if (d.reponse)
		snprintf(chemin, n, "%s", d.resultat);
	return d.reponse;
}

/* Une seule copie d'un programme (Mac OS 9) : voir ptk_app.h. */
int
ptk_instance_unique(PtkApp *app, PtkWindow *w, const char *signature, const char *document)
{
	char nom[64];
	Atom sel;
	Window autre;

	snprintf(nom, sizeof(nom), "_PLATINE_INSTANCE_%s", signature);
	sel = XInternAtom(app->dpy, nom, False);
	XGrabServer(app->dpy);
	autre = XGetSelectionOwner(app->dpy, sel);
	if (autre == None) {
		XSetSelectionOwner(app->dpy, sel, w->win, CurrentTime);
		XUngrabServer(app->dpy);
		XFlush(app->dpy);
		return 0;
	}
	XUngrabServer(app->dpy);
	if (document != NULL && document[0] != '\0') {
		char cmd[PATH_MAX + 32];
		XEvent e;

		snprintf(cmd, sizeof(cmd), "Ouvrir le document\t%s", document);
		set_utf8(app, autre, app->a_cmd, cmd);
		memset(&e, 0, sizeof(e));
		e.xclient.type = ClientMessage;
		e.xclient.window = autre;
		e.xclient.message_type = app->a_cmd;
		e.xclient.format = 32;
		XSendEvent(app->dpy, autre, False, NoEventMask, &e);
	}
	{
		XEvent e;

		memset(&e, 0, sizeof(e));
		e.xclient.type = ClientMessage;
		e.xclient.window = autre;
		e.xclient.message_type = XInternAtom(app->dpy, "_NET_ACTIVE_WINDOW", False);
		e.xclient.format = 32;
		e.xclient.data.l[0] = 2;	/* demande d'un programme */
		XSendEvent(app->dpy, app->root, False,
		    SubstructureRedirectMask | SubstructureNotifyMask, &e);
	}
	XSync(app->dpy, False);
	return 1;
}
