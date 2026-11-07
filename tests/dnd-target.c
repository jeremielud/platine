/* Cible XDND de test : enregistre la sélection text/uri-list reçue. */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
message(Display *dpy, Atom type, Window target, Window source, long l1, long l2)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = target;
	ev.xclient.message_type = type;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)source;
	ev.xclient.data.l[1] = l1;
	ev.xclient.data.l[2] = l2;
	XSendEvent(dpy, target, False, 0, &ev);
	XFlush(dpy);
}

int
main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	Atom aware, enter, position, status, drop, selection, finished, copy, uri;
	Window win, source = None;
	XSizeHints hints;
	XEvent ev;
	int accepte = 0;

	if (dpy == NULL || argc != 2)
		return 2;
	aware = XInternAtom(dpy, "XdndAware", False);
	enter = XInternAtom(dpy, "XdndEnter", False);
	position = XInternAtom(dpy, "XdndPosition", False);
	status = XInternAtom(dpy, "XdndStatus", False);
	drop = XInternAtom(dpy, "XdndDrop", False);
	selection = XInternAtom(dpy, "XdndSelection", False);
	finished = XInternAtom(dpy, "XdndFinished", False);
	copy = XInternAtom(dpy, "XdndActionCopy", False);
	uri = XInternAtom(dpy, "text/uri-list", False);
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 500, 120, 240, 240, 0,
	    BlackPixel(dpy, DefaultScreen(dpy)), WhitePixel(dpy, DefaultScreen(dpy)));
	XStoreName(dpy, win, "Cible XDND");
	memset(&hints, 0, sizeof(hints));
	hints.flags = USPosition | USSize;
	hints.x = 500;
	hints.y = 120;
	hints.width = 240;
	hints.height = 240;
	XSetWMNormalHints(dpy, win, &hints);
	{
		long version = 5;

		XChangeProperty(dpy, win, aware, XA_ATOM, 32, PropModeReplace,
		    (unsigned char *)&version, 1);
	}
	XSelectInput(dpy, win, ExposureMask);
	XMapWindow(dpy, win);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type != ClientMessage && ev.type != SelectionNotify)
			continue;
		if (ev.type == SelectionNotify) {
			Atom type;
			int format;
			unsigned long n, after;
			unsigned char *data = NULL;
			FILE *f;

			if (ev.xselection.property == None)
				return 1;
			if (XGetWindowProperty(dpy, win, ev.xselection.property, 0, 65536,
			    True, AnyPropertyType, &type, &format, &n, &after, &data) != Success ||
			    data == NULL || format != 8)
				return 1;
			if ((f = fopen(argv[1], "w")) == NULL)
				return 1;
			fwrite(data, 1, n, f);
			fclose(f);
			XFree(data);
			message(dpy, finished, source, win, 1, copy);
			return 0;
		}
		if (ev.xclient.message_type == enter) {
			source = (Window)ev.xclient.data.l[0];
			accepte = (Atom)ev.xclient.data.l[2] == uri;
		} else if (ev.xclient.message_type == position && source != None) {
			message(dpy, status, source, win, accepte, copy);
		} else if (ev.xclient.message_type == drop && source != None && accepte) {
			XConvertSelection(dpy, selection, uri, uri, win,
			    (Time)ev.xclient.data.l[2]);
			XFlush(dpy);
		}
	}
}
