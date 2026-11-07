/*
 * clipboard-client : client CLIPBOARD pour les tests du Finder et de la Toolbox.
 *   clipboard-client get             lit text/uri-list sur la sortie standard
 *   clipboard-client get-text        lit UTF8_STRING (repli STRING) sur la sortie standard
 *   clipboard-client own CHEMIN      publie ce chemin en text/uri-list et UTF8_STRING
 *   clipboard-client own-text CHEMIN publie le chemin comme texte UTF-8 seul
 *   clipboard-client own-contenu C   publie le contenu du fichier C comme texte UTF-8 seul
 *   clipboard-client get-cible CIBLE lit la cible (image/png…) sur la sortie standard
 *   clipboard-client cibles          liste les cibles annoncées (TARGETS)
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int texte_seul;
static int contenu;	/* own-contenu : la charge utile est le contenu du fichier */

/* demande CIBLE sur CLIPBOARD et recopie la réponse sur la sortie standard ;
 * retour 1 si le propriétaire ne sert pas cette cible */
static int
lire_selection(Display *dpy, Atom clip, Atom prop, Window win, Atom cible)
{
	XEvent ev;

	XConvertSelection(dpy, clip, cible, prop, win, CurrentTime);
	XFlush(dpy);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == SelectionNotify && ev.xselection.selection == clip) {
			Atom type;
			int format;
			unsigned long n, after;
			unsigned char *data = NULL;

			if (ev.xselection.property == None)
				return 1;
			if (XGetWindowProperty(dpy, win, ev.xselection.property, 0, 1 << 22,
			    True, AnyPropertyType, &type, &format, &n, &after, &data) == Success &&
			    data != NULL && format == 8) {
				fwrite(data, 1, n, stdout);
				XFree(data);
				return 0;
			}
			if (data != NULL && format == 32 && type == XA_ATOM) {
				unsigned long i;

				for (i = 0; i < n; i++) {
					char *nom = XGetAtomName(dpy, ((Atom *)data)[i]);

					printf("%s\n", nom != NULL ? nom : "?");
					XFree(nom);
				}
				XFree(data);
				return 0;
			}
			return 1;
		}
	}
}

static void
repondre(Display *dpy, XSelectionRequestEvent *q, const char *chemin)
{
	Atom uri = XInternAtom(dpy, "text/uri-list", False);
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom targets = XInternAtom(dpy, "TARGETS", False);
	Atom prop = q->property != None ? q->property : q->target;
	XEvent r;
	char texte[4096];

	memset(&r, 0, sizeof(r));
	r.xselection.type = SelectionNotify;
	r.xselection.requestor = q->requestor;
	r.xselection.selection = q->selection;
	r.xselection.target = q->target;
	r.xselection.time = q->time;
	r.xselection.property = None;
	if (q->target == targets) {
		Atom cibles[] = { targets, uri, utf8, XA_STRING };

		XChangeProperty(dpy, q->requestor, prop, XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)cibles, 4);
		r.xselection.property = prop;
	} else if ((q->target == uri && !texte_seul) || q->target == utf8 || q->target == XA_STRING) {
		if (contenu)
			snprintf(texte, sizeof(texte), "%s", chemin);
		else if (q->target == uri && strncmp(chemin, "file://", 7) != 0)
			snprintf(texte, sizeof(texte), "file://%s\r\n", chemin);
		else
			snprintf(texte, sizeof(texte), "%s\n", chemin);
		XChangeProperty(dpy, q->requestor, prop, q->target, 8,
		    PropModeReplace, (unsigned char *)texte, (int)strlen(texte));
		r.xselection.property = prop;
	}
	XSendEvent(dpy, q->requestor, False, 0, &r);
	XFlush(dpy);
}

int
main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	Atom clip, uri, prop;
	Window win;
	XEvent ev;

	if (dpy == NULL || argc < 2)
		return 2;
	clip = XInternAtom(dpy, "CLIPBOARD", False);
	uri = XInternAtom(dpy, "text/uri-list", False);
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	if ((strcmp(argv[1], "own") == 0 || strcmp(argv[1], "own-text") == 0 ||
	    strcmp(argv[1], "own-contenu") == 0) && argc == 3) {
		texte_seul = strcmp(argv[1], "own-text") == 0 ||
		    strcmp(argv[1], "own-contenu") == 0;
		contenu = strcmp(argv[1], "own-contenu") == 0;
		if (strcmp(argv[1], "own-contenu") == 0) {
			/* la charge utile devient le contenu du fichier */
			static char tampon[4096];
			FILE *f = fopen(argv[2], "r");
			size_t n;

			if (f == NULL)
				return 1;
			n = fread(tampon, 1, sizeof(tampon) - 1, f);
			if (ferror(f)) {
				fclose(f);
				return 1;
			}
			tampon[n] = '\0';
			fclose(f);
			argv[2] = tampon;
		}
		XSetSelectionOwner(dpy, clip, win, CurrentTime);
		if (XGetSelectionOwner(dpy, clip) != win)
			return 1;
		for (;;) {
			XNextEvent(dpy, &ev);
			if (ev.type == SelectionRequest)
				repondre(dpy, &ev.xselectionrequest, argv[2]);
		}
	}
	prop = XInternAtom(dpy, "_PLATINE_TEST_CLIPBOARD", False);
	if (strcmp(argv[1], "get") == 0)
		return lire_selection(dpy, clip, prop, win, uri);
	if (strcmp(argv[1], "get-cible") == 0 && argc == 3)
		return lire_selection(dpy, clip, prop, win, XInternAtom(dpy, argv[2], False));
	if (strcmp(argv[1], "cibles") == 0)
		return lire_selection(dpy, clip, prop, win, XInternAtom(dpy, "TARGETS", False));
	if (strcmp(argv[1], "get-text") == 0) {
		Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);

		if (lire_selection(dpy, clip, prop, win, utf8) == 0)
			return 0;
		return lire_selection(dpy, clip, prop, win, XA_STRING);
	}
	return 2;
}
