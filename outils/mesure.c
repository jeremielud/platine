/*
 * mesure : délai entre une action et l'apparition d'une fenêtre, en millisecondes.
 *   mesure NOM commande...   attend la fenêtre (cliente) nommée NOM
 *   mesure -o commande...    attend la première fenêtre override-redirect (menu, alerte)
 *   mesure -a commande...    attend la première fenêtre cartographiée
 * Écoute MapNotify sur la racine (SubstructureNotify, sans gêner le WM). Délai max 10 s.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static double
ms(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
	return t.tv_sec * 1000.0 + t.tv_usec / 1000.0;
}

static int
nom_dans(Display *d, Window w, const char *nom, int prof)
{
	char *n = NULL;
	Window r, p, *enf = NULL;
	unsigned int ne = 0, i;
	int ok = 0;

	if (XFetchName(d, w, &n) && n != NULL) {
		ok = strcmp(n, nom) == 0;
		XFree(n);
	}
	if (ok || prof > 2 || !XQueryTree(d, w, &r, &p, &enf, &ne))
		return ok;
	for (i = 0; i < ne && !ok; i++)
		ok = nom_dans(d, enf[i], nom, prof + 1);
	if (enf)
		XFree(enf);
	return ok;
}

static int
ignorer(Display *d, XErrorEvent *e)
{
	(void)d; (void)e;
	return 0;
}

int
main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	int over, tout, fd;
	double t0;
	pid_t pid;

	if (d == NULL || argc < 3)
		return 2;
	XSetErrorHandler(ignorer);
	over = strcmp(argv[1], "-o") == 0;
	tout = strcmp(argv[1], "-a") == 0;
	XSelectInput(d, DefaultRootWindow(d), SubstructureNotifyMask);
	XSync(d, False);
	t0 = ms();
	if ((pid = fork()) == 0) {
		execvp(argv[2], argv + 2);
		_exit(127);
	}
	fd = ConnectionNumber(d);
	while (ms() - t0 < 10000) {
		while (XPending(d)) {
			XEvent e;

			XNextEvent(d, &e);
			if (e.type != MapNotify)
				continue;
			if (tout || (over ? e.xmap.override_redirect :
			    nom_dans(d, e.xmap.window, argv[1], 0))) {
				printf("%.0f\n", ms() - t0);
				waitpid(pid, NULL, 0);
				return 0;
			}
		}
		{
			fd_set f;
			struct timeval tv = { 0, 20000 };

			FD_ZERO(&f);
			FD_SET(fd, &f);
			select(fd + 1, &f, NULL, NULL, &tv);
		}
	}
	printf("délai dépassé\n");
	return 1;
}
