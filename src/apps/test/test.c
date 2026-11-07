/*
 * platine-test — client de démonstration pour platine-wm.
 *
 * Publie un modèle de menus via _PLATINE_MENU, reçoit les commandes via
 * _PLATINE_COMMAND et dessine quelques contrôles Platine avec libptk.
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../toolbox/ptk.h"

#define WIN_W 420
#define WIN_H 300

static Display *dpy;
static Window win;
static Ptk ptk;
static Atom a_utf8, a_menu, a_app, a_cmd, a_protocols, a_delete, a_icon;
static int counter;
static char last_cmd[128] = "aucune";

static void
draw(void)
{
	XWindowAttributes attr;
	int w, h, baseline;
	char buf[160];

	XGetWindowAttributes(dpy, win, &attr);
	w = attr.width;
	h = attr.height;
	ptk_fill(&ptk, win, 0, 0, w, h, ptk.window);

	ptk_text(&ptk, win, ptk.font_title, 18, 30,
	    "Contenu Platine", ptk.text);

	ptk_button(&ptk, win, 18, 48, 120, 26, "Bouton", 0, 0, ptk.text);
	ptk_button(&ptk, win, 152, 48, 120, 26, "OK", 0, 1, ptk.text);

	ptk_bevel(&ptk, win, 18, 92, 254, 26, 1);
	ptk_text(&ptk, win, ptk.font_menu, 26, 110, "Champ de texte",
	    ptk.text);

	ptk_bevel(&ptk, win, 18, 132, 13, 13, 1);
	ptk_text(&ptk, win, ptk.font_menu, 38, 143, "Case à cocher",
	    ptk.text);

	ptk_bevel(&ptk, win, 18, 160, 254, 12, 1);
	ptk_fill(&ptk, win, 20, 162, 120, 8, ptk.selection);

	ptk_line(&ptk, win, 18, 188, 272, 188, ptk.shadow);
	ptk_line(&ptk, win, 18, 189, 272, 189, ptk.light);

	snprintf(buf, sizeof(buf), "Dernière commande : %s", last_cmd);
	baseline = 210;
	ptk_text(&ptk, win, ptk.font_menu, 18, baseline, buf, ptk.text);
	snprintf(buf, sizeof(buf), "Compteur : %d", counter);
	ptk_text(&ptk, win, ptk.font_menu, 18, baseline + 20, buf, ptk.text);
	ptk_text(&ptk, win, ptk.font_small, 18, h - 16,
	    "Fenêtre gérée par platine-wm — menu d'application à droite", ptk.text_dim);

	XFlush(dpy);
}

static void
set_prop(Atom prop, const char *value)
{
	XChangeProperty(dpy, win, prop, a_utf8, 8, PropModeReplace,
	    (const unsigned char *)value, (int)strlen(value));
}

int
main(void)
{
	XSetWindowAttributes a;
	XSizeHints hints;
	XEvent ev;
	char *cmd;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "platine-test: pas de display\n");
		return 1;
	}
	if (ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;

	a.background_pixel = ptk.window.pixel;
	a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask;
	win = XCreateWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)), 50, 70,
	    WIN_W, WIN_H, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWBackPixel | CWEventMask, &a);
	XStoreName(dpy, win, "Test Platine");

	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize;
	hints.min_width = 240;
	hints.min_height = 180;
	XSetWMNormalHints(dpy, win, &hints);

	a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	a_menu = XInternAtom(dpy, "_PLATINE_MENU", False);
	a_app = XInternAtom(dpy, "_PLATINE_APP_NAME", False);
	a_cmd = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	a_icon = XInternAtom(dpy, "_PLATINE_ICON", False);

	XSetWMProtocols(dpy, win, &a_delete, 1);
	set_prop(a_app, "Test Platine");
	set_prop(a_icon, "app");
	set_prop(a_menu,
	    "Fichier|Nouveau\t⌘N|Ouvrir\t⌘O|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V\n"
	    "Présentation|Icônes|Liste|Boutons\n"
	    "Aide|À propos de Test");

	XMapWindow(dpy, win);
	draw();

	for (;;) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw();
			break;
		case ConfigureNotify:
			draw();
			break;
		case ClientMessage:
			if (ev.xclient.message_type == a_protocols &&
			    (Atom)ev.xclient.data.l[0] == a_delete)
				goto done;
			if (ev.xclient.message_type == a_cmd) {
				cmd = NULL;
				{
					Atom type;
					int format;
					unsigned long n, after;
					unsigned char *data = NULL;
					if (XGetWindowProperty(dpy, win, a_cmd,
					    0, 1024, False, AnyPropertyType,
					    &type, &format, &n, &after,
					    &data) == Success && data) {
						cmd = malloc(n + 1);
						memcpy(cmd, data, n);
						cmd[n] = '\0';
						XFree(data);
					}
				}
				if (cmd != NULL) {
					snprintf(last_cmd, sizeof(last_cmd), "%s",
					    cmd);
					fprintf(stderr, "platine-test: commande %s\n",
					    cmd);
					if (strcmp(cmd, "Quitter") == 0 ||
					    strcmp(cmd, "Fermer") == 0) {
						free(cmd);
						goto done;
					}
					if (strcmp(cmd, "Nouveau") == 0)
						counter++;
					free(cmd);
					draw();
				}
			}
			break;
		default:
			break;
		}
	}

done:
	XDestroyWindow(dpy, win);
	ptk_fini(&ptk);
	XCloseDisplay(dpy);
	return 0;
}
