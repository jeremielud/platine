#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	Display *dpy;
	Window win;
	Atom protocols, take_focus;
	FILE *log;
	const char *name, *path;
	int x, y;

	if (argc != 4)
		return 2;
	name = argv[1];
	x = atoi(argv[2]);
	y = atoi(argv[3]);
	path = getenv("FOCUS_LOG");
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL || path == NULL)
		return 1;
	log = fopen(path, "a");
	if (log == NULL)
		return 1;
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), x, y, 240, 150, 0,
	    BlackPixel(dpy, DefaultScreen(dpy)), WhitePixel(dpy, DefaultScreen(dpy)));
	XStoreName(dpy, win, name);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | FocusChangeMask);
	protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	XSetWMProtocols(dpy, win, &take_focus, 1);
	XMapWindow(dpy, win);
	XFlush(dpy);
	for (;;) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage && ev.xclient.message_type == protocols &&
		    ev.xclient.data.l[0] == (long)take_focus) {
			fprintf(log, "%s WM_TAKE_FOCUS\n", name);
			fflush(log);
			XSetInputFocus(dpy, win, RevertToPointerRoot,
			    (Time)ev.xclient.data.l[1]);
			XFlush(dpy);
		} else if (ev.type == KeyPress) {
			fprintf(log, "%s KEY\n", name);
			fflush(log);
		}
	}
}
