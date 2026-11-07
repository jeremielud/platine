/*
 * session-client : client de test pour l'arrêt et Forcer à quitter.
 *   session-client NOM obeit|tetu|sans   fenêtre « NOM » ; obeit quitte sur WM_DELETE_WINDOW,
 *                                        tetu l'ignore, sans ne le déclare pas
 *   session-client --arret MODE FORCE    envoie _PLATINE_ARRET à la racine
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	Window root, win;
	Atom proto, del;
	XEvent ev;
	long pid = (long)getpid();

	if (dpy == NULL || argc < 3)
		return 2;
	root = DefaultRootWindow(dpy);
	if (strcmp(argv[1], "--arret") == 0 && argc == 4) {
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.window = root;
		ev.xclient.message_type = XInternAtom(dpy, "_PLATINE_ARRET", False);
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = atol(argv[2]);
		ev.xclient.data.l[1] = atol(argv[3]);
		XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
		XCloseDisplay(dpy);
		return 0;
	}
	win = XCreateSimpleWindow(dpy, root, 60, 60, 240, 150, 0,
	    BlackPixel(dpy, DefaultScreen(dpy)), WhitePixel(dpy, DefaultScreen(dpy)));
	XStoreName(dpy, win, argv[1]);
	XChangeProperty(dpy, win, XInternAtom(dpy, "_PLATINE_APP_NAME", False),
	    XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
	    (unsigned char *)argv[1], (int)strlen(argv[1]));
	XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)&pid, 1);
	proto = XInternAtom(dpy, "WM_PROTOCOLS", False);
	del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	if (strcmp(argv[2], "sans") != 0)
		XSetWMProtocols(dpy, win, &del, 1);
	XMapWindow(dpy, win);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage && ev.xclient.message_type == proto &&
		    (Atom)ev.xclient.data.l[0] == del && strcmp(argv[2], "obeit") == 0)
			return 0;
	}
}
