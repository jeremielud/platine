/* Source XDND de test : dépose un URI file:// dans une fenêtre cible. */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
message(Display *dpy, Atom type, Window target, Window source, long l1, long l2,
    long l4)
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
	ev.xclient.data.l[4] = l4;
	XSendEvent(dpy, target, False, 0, &ev);
	XFlush(dpy);
}

static char *
uri_for(const char *path)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p;
	char *uri, *s;
	size_t n = strlen(path);

	if ((uri = malloc(8 + n * 3)) == NULL)
		return NULL;
	strcpy(uri, "file://");
	s = uri + 7;
	for (p = (const unsigned char *)path; *p != '\0'; p++) {
		if (*p <= ' ' || *p == '%' || *p == '#' || *p >= 127) {
			*s++ = '%';
			*s++ = hex[*p >> 4];
			*s++ = hex[*p & 15];
		} else {
			*s++ = (char)*p;
		}
	}
	*s = '\0';
	return uri;
}

int
main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(NULL);
	Atom enter, position, status, drop, selection, finished, copy, uri, typelist;
	Window win, target;
	XEvent ev;
	char path[PATH_MAX], *texte;
	int x, y;
	unsigned int width, height, border, depth;
	Window root;

	if (dpy == NULL || argc != 3 || realpath(argv[2], path) == NULL)
		return 2;
	target = (Window)strtoul(argv[1], NULL, 0);
	enter = XInternAtom(dpy, "XdndEnter", False);
	position = XInternAtom(dpy, "XdndPosition", False);
	status = XInternAtom(dpy, "XdndStatus", False);
	drop = XInternAtom(dpy, "XdndDrop", False);
	selection = XInternAtom(dpy, "XdndSelection", False);
	finished = XInternAtom(dpy, "XdndFinished", False);
	copy = XInternAtom(dpy, "XdndActionCopy", False);
	uri = XInternAtom(dpy, "text/uri-list", False);
	typelist = XInternAtom(dpy, "XdndTypeList", False);
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, win, typelist, XA_ATOM, 32, PropModeReplace,
	    (unsigned char *)&uri, 1);
	texte = uri_for(path);
	if (texte == NULL || !XGetGeometry(dpy, target, &root, &x, &y, &width,
	    &height, &border, &depth) ||
	    !XTranslateCoordinates(dpy, target, root, 0, 0, &x, &y, &root))
		return 2;
	message(dpy, enter, target, win, (5L << 24) | 1, 0, 0);
	message(dpy, position, target, win, 0,
	    (long)((x + (int)width / 2) << 16) |
	    ((y + (int)height / 2) & 0xffff), copy);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage && ev.xclient.message_type == status) {
			if ((ev.xclient.data.l[1] & 1) == 0)
				return 1;
			XSetSelectionOwner(dpy, selection, win, CurrentTime);
			message(dpy, drop, target, win, 0, CurrentTime, 0);
		} else if (ev.type == SelectionRequest) {
			XSelectionRequestEvent *q = &ev.xselectionrequest;
			XEvent r;

			memset(&r, 0, sizeof(r));
			r.xselection.type = SelectionNotify;
			r.xselection.requestor = q->requestor;
			r.xselection.selection = q->selection;
			r.xselection.target = q->target;
			r.xselection.time = q->time;
			r.xselection.property = None;
			if (q->target == uri) {
				Atom prop = q->property != None ? q->property : q->target;

				XChangeProperty(dpy, q->requestor, prop, uri, 8,
				    PropModeReplace, (unsigned char *)texte, (int)strlen(texte));
				r.xselection.property = prop;
			}
			XSendEvent(dpy, q->requestor, False, 0, &r);
			XFlush(dpy);
		} else if (ev.type == ClientMessage && ev.xclient.message_type == finished) {
			int ok = (ev.xclient.data.l[1] & 1) != 0;

			free(texte);
			return ok ? 0 : 1;
		}
	}
}
