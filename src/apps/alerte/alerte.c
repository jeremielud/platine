/*
 * platine-alerte : alertes de session lancées par le WM ou par powerd, qui exécutent
 * elles-mêmes le choix (le WM ne lit pas le code de sortie de ses enfants).
 *
 *   platine-alerte forcer FENÊTRE NOM     « Forcer NOM à quitter ? » (⌘⌥Échap)
 *   platine-alerte arret-bloque MODE NOM  une application a refusé de se fermer
 *   platine-alerte alimentation           touche d'alimentation (Mac OS 9)
 *
 * MODE : 1 = éteindre, 2 = redémarrer. Le WM reçoit _PLATINE_ARRET sur la racine :
 * data.l[0] = MODE (0 : annuler), data.l[1] = 1 pour ne plus attendre les applications.
 */
#include <X11/Xatom.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_sys.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Alerte", "palr", "application", PTK_VERSION, "");


static PtkApp app;

static void
envoyer_arret(long mode, long force)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = app.root;
	ev.xclient.message_type = XInternAtom(app.dpy, "_PLATINE_ARRET", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = mode;
	ev.xclient.data.l[1] = force;
	XSendEvent(app.dpy, app.root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(app.dpy);
}

static long
pid_fenetre(Window w)
{
	Atom type;
	int fmt;
	unsigned long n, reste;
	unsigned char *d = NULL;
	long pid = 0;

	if (XGetWindowProperty(app.dpy, w, XInternAtom(app.dpy, "_NET_WM_PID", False), 0, 1, False,
	    XA_CARDINAL, &type, &fmt, &n, &reste, &d) == Success && d != NULL) {
		if (n == 1 && fmt == 32)
			pid = *(long *)d;
		XFree(d);
	}
	return pid;
}

static int
alerte(const char *icone, const char *msg, const char *detail, const char *boutons)
{
	PtkWindow parent;

	if (icone != NULL) {
		char f[64];

		snprintf(f, sizeof(f), "%s-32.xpm", icone);
		if (ptk_load_xpm(&app.ptk, f, &app.icon_pix, &app.icon_mask) == 0)
			app.icon_w = app.icon_h = 32;
	}
	memset(&parent, 0, sizeof(parent));
	parent.app = &app;
	parent.p = &app.ptk;
	return ptk_alert(&parent, msg, detail, boutons);
}

static int
ignorer(Display *d, XErrorEvent *e)
{
	(void)d;
	(void)e;
	return 0;
}

int
main(int argc, char **argv)
{
	char m[256], d[512];

	setlocale(LC_ALL, "");
	if (argc < 2 || ptk_app_init(&app, "Alerte", "app") != 0)
		return 2;
	XSetErrorHandler(ignorer);	/* la fenêtre visée peut disparaître pendant l'alerte */

	if (strcmp(argv[1], "forcer") == 0 && argc >= 4) {
		Window w = (Window)strtoul(argv[2], NULL, 0);
		long pid;

		snprintf(m, sizeof(m), "Forcer « %s » à quitter ?", argv[3]);
		if (alerte("attention", m, "Les modifications non enregistrées seront perdues.",
		    "Annuler|Forcer à quitter") != 1)
			return 1;
		pid = pid_fenetre(w);
		if (pid > 1 && pid != (long)getpid())
			kill((pid_t)pid, SIGKILL);
		XKillClient(app.dpy, w);
		XSync(app.dpy, False);
		if (strcmp(argv[3], "Finder") == 0 && fork() == 0) {	/* comme sous Mac OS 9 */
			setsid();
			char fi[PATH_MAX];

			sleep(1);
			ptk_chemin_programme("System/Finder", "platine-finder", fi, sizeof(fi));
			execlp(fi, fi, (char *)NULL);
			_exit(127);
		}
		return 0;
	}
	if (strcmp(argv[1], "arret-bloque") == 0 && argc >= 4) {
		long mode = atol(argv[2]) == 2 ? 2 : 1;

		snprintf(m, sizeof(m), mode == 2 ? "Le redémarrage a été interrompu." :
		    "L'extinction a été interrompue.");
		snprintf(d, sizeof(d), "« %s » ne s'est pas fermé. Enregistrez votre travail puis "
		    "quittez-le, ou forcez-le à quitter (⌘⌥Échap).", argv[3]);
		if (alerte("attention", m, d, mode == 2 ? "Annuler|Redémarrer quand même" :
		    "Annuler|Éteindre quand même") == 1)
			envoyer_arret(mode, 1);
		return 0;
	}
	if (strcmp(argv[1], "alimentation") == 0) {
		switch (alerte("attention", "Voulez-vous vraiment éteindre votre ordinateur maintenant ?",
		    NULL, "Redémarrer|Suspendre|Annuler|Éteindre")) {
		case 0:	envoyer_arret(2, 0); break;
		case 1:	if (system(SYS_PRIVILEGE " veille") != 0) ptk_beep(&app); break;
		case 3:	envoyer_arret(1, 0); break;
		}
		return 0;
	}
	fprintf(stderr, "usage : platine-alerte forcer FENÊTRE NOM | arret-bloque MODE NOM | alimentation\n");
	return 2;
}
