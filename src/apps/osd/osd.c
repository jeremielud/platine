/*
 * platine-osd (NetBSD) : boîte de réglage au milieu de l'écran, comme sur les PowerBook, quand
 * le volume change (touches Fn : le noyau agit seul et ne prévient personne). Surveille la table
 * de mixage. Ne s'affiche pas pendant que la Control Strip règle le volume (_PLATINE_VOLUME).
 */
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include "../../../toolbox/ptk.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Affichage à l'écran", "posd", "application", PTK_VERSION, "");


#define L 168
#define H 160
#define ZOOM 4

static Ptk ptk;
static Display *dpy;
static Window win;
static Pixmap tampon, grand, grand_masque;
static int gr_w, gr_h;
static int mixeur = -1, maitre = -1;
static Atom a_gel;

static int
trouver_maitre(void)
{
	mixer_devinfo_t d, c;

	for (d.index = 0; ioctl(mixeur, AUDIO_MIXER_DEVINFO, &d) == 0; d.index++) {
		if (d.type != AUDIO_MIXER_VALUE || strcmp(d.label.name, AudioNmaster) != 0)
			continue;
		c.index = d.mixer_class;
		if (ioctl(mixeur, AUDIO_MIXER_DEVINFO, &c) == 0 &&
		    strcmp(c.label.name, AudioCoutputs) == 0)
			return d.index;
	}
	return -1;
}

static int
lire_volume(void)
{
	mixer_ctrl_t m;

	memset(&m, 0, sizeof(m));
	m.dev = maitre;
	m.type = AUDIO_MIXER_VALUE;
	m.un.value.num_channels = 2;
	if (ioctl(mixeur, AUDIO_MIXER_READ, &m) != 0)
		return -1;
	return m.un.value.level[0];
}

/* l'icône du haut-parleur de la Control Strip, agrandie sans lissage */
static void
preparer_icone(void)
{
	Pixmap ip = None, im = None;
	XImage *ii, *mi = NULL;
	Window r;
	int x, y;
	unsigned w, h, bw, dp;
	GC g1;

	if (ptk_load_xpm(&ptk, "strip-son.xpm", &ip, &im) != 0 || ip == None)
		return;
	XGetGeometry(dpy, ip, &r, &x, &y, &w, &h, &bw, &dp);
	gr_w = (int)w * ZOOM;
	gr_h = (int)h * ZOOM;
	grand = XCreatePixmap(dpy, win, (unsigned)gr_w, (unsigned)gr_h, dp);
	ii = XGetImage(dpy, ip, 0, 0, w, h, AllPlanes, ZPixmap);
	if (im != None) {
		grand_masque = XCreatePixmap(dpy, win, (unsigned)gr_w, (unsigned)gr_h, 1);
		mi = XGetImage(dpy, im, 0, 0, w, h, 1, XYPixmap);
	}
	g1 = grand_masque != None ? XCreateGC(dpy, grand_masque, 0, NULL) : NULL;
	for (y = 0; y < (int)h; y++)
		for (x = 0; x < (int)w; x++) {
			XSetForeground(dpy, ptk.gc, XGetPixel(ii, x, y));
			XFillRectangle(dpy, grand, ptk.gc, x * ZOOM, y * ZOOM, ZOOM, ZOOM);
			if (mi != NULL) {
				XSetForeground(dpy, g1, XGetPixel(mi, x, y));
				XFillRectangle(dpy, grand_masque, g1, x * ZOOM, y * ZOOM, ZOOM, ZOOM);
			}
		}
	XDestroyImage(ii);
	if (mi != NULL)
		XDestroyImage(mi);
	if (g1 != NULL)
		XFreeGC(dpy, g1);
}

static void
boite(Drawable d, GC g, int x, int y, int w, int h, int r)
{
	XFillRectangle(dpy, d, g, x + r, y, (unsigned)(w - 2 * r), (unsigned)h);
	XFillRectangle(dpy, d, g, x, y + r, (unsigned)w, (unsigned)(h - 2 * r));
	XFillArc(dpy, d, g, x, y, 2 * r, 2 * r, 90 * 64, 90 * 64);
	XFillArc(dpy, d, g, x + w - 2 * r - 1, y, 2 * r, 2 * r, 0, 90 * 64);
	XFillArc(dpy, d, g, x, y + h - 2 * r - 1, 2 * r, 2 * r, 180 * 64, 90 * 64);
	XFillArc(dpy, d, g, x + w - 2 * r - 1, y + h - 2 * r - 1, 2 * r, 2 * r, 270 * 64, 90 * 64);
}

static void
dessiner(int pc)
{
	PtkColor rouge = ptk_color(&ptk, "#cc0000");
	int n = (pc * 16 + 50) / 100, i;

	XSetForeground(dpy, ptk.gc, ptk.black.pixel);
	boite(tampon, ptk.gc, 0, 0, L, H, 18);
	XSetForeground(dpy, ptk.gc, ptk.white.pixel);
	boite(tampon, ptk.gc, 1, 1, L - 2, H - 2, 17);
	XSetForeground(dpy, ptk.gc, ptk_color(&ptk, "#dddddd").pixel);
	boite(tampon, ptk.gc, 2, 2, L - 4, H - 4, 16);
	if (grand != None)
		ptk_draw_pixmap(&ptk, tampon, grand, grand_masque, (L - gr_w) / 2, 22, gr_w, gr_h);
	if (pc == 0) {
		XSetLineAttributes(dpy, ptk.gc, 6, LineSolid, CapRound, JoinRound);
		XSetForeground(dpy, ptk.gc, rouge.pixel);
		XDrawLine(dpy, tampon, ptk.gc, 50, 28, 118, 88);
		XSetLineAttributes(dpy, ptk.gc, 0, LineSolid, CapButt, JoinMiter);
	}
	for (i = 0; i < 16; i++) {
		int x = 20 + i * 8;

		ptk_fill(&ptk, tampon, x, 118, 7, 17, i < n ? ptk.selection : ptk.white);
		ptk_rect(&ptk, tampon, x, 118, 7, 17, ptk.black);
	}
	XCopyArea(dpy, tampon, win, ptk.gc, 0, 0, L, H, 0, 0);
}

static int
strip_regle(void)
{
	Atom t;
	int f;
	unsigned long n, r;
	unsigned char *v = NULL;
	int gel = 0;

	if (XGetWindowProperty(dpy, ptk.root, a_gel, 0, 1, False, AnyPropertyType, &t, &f, &n, &r, &v) == Success && v != NULL) {
		gel = n > 0;
		XFree(v);
	}
	return gel;
}

int
main(void)
{
	XSetWindowAttributes a;
	Pixmap forme;
	GC g1;
	struct pollfd pf;
	int dernier, visible = 0;
	struct timespec fin = { 0, 0 };

	if ((mixeur = open("/dev/mixer", O_RDONLY)) < 0 || (maitre = trouver_maitre()) < 0)
		return 1;
	if ((dpy = XOpenDisplay(NULL)) == NULL || ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;
	a_gel = XInternAtom(dpy, "_PLATINE_VOLUME", False);
	a.override_redirect = True;
	a.background_pixmap = None;
	a.save_under = True;
	win = XCreateWindow(dpy, ptk.root, (DisplayWidth(dpy, ptk.screen) - L) / 2,
	    (DisplayHeight(dpy, ptk.screen) - H) / 2 + 40, L, H, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWSaveUnder, &a);
	XSelectInput(dpy, win, ExposureMask);
	tampon = XCreatePixmap(dpy, win, L, H, (unsigned)DefaultDepth(dpy, ptk.screen));
	forme = XCreatePixmap(dpy, win, L, H, 1);
	g1 = XCreateGC(dpy, forme, 0, NULL);
	XSetForeground(dpy, g1, 0);
	XFillRectangle(dpy, forme, g1, 0, 0, L, H);
	XSetForeground(dpy, g1, 1);
	boite(forme, g1, 0, 0, L, H, 18);
	XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, forme, ShapeSet);
	XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);	/* les clics traversent */
	preparer_icone();
	dernier = lire_volume();
	pf.fd = ConnectionNumber(dpy);
	pf.events = POLLIN;
	for (;;) {
		XEvent e;
		struct timespec t;
		int v;

		XFlush(dpy);
		poll(&pf, 1, 150);
		while (XPending(dpy)) {
			XNextEvent(dpy, &e);
			if (e.type == Expose && visible)
				XCopyArea(dpy, tampon, win, ptk.gc, 0, 0, L, H, 0, 0);
		}
		v = lire_volume();
		clock_gettime(CLOCK_MONOTONIC, &t);
		if (v >= 0 && v != dernier) {
			dernier = v;
			if (!strip_regle()) {
				XMapRaised(dpy, win);
				visible = 1;
				dessiner((v * 100 + 127) / 255);
				fin = t;
				fin.tv_sec += 1;
				fin.tv_nsec += 500000000;
			}
		}
		if (visible && (t.tv_sec > fin.tv_sec || (t.tv_sec == fin.tv_sec && t.tv_nsec >= fin.tv_nsec % 1000000000))) {
			if (t.tv_sec > fin.tv_sec + (fin.tv_nsec >= 1000000000)) {
				XUnmapWindow(dpy, win);
				visible = 0;
			}
		}
	}
}
