/* xclic : tests sur le 701 sans xdotool. xclic bouge X Y | presse | relache | clic | clicrapide | double | glisse X1 Y1 X2 Y2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
static Display *d;
static void b(int p) { XTestFakeButtonEvent(d, 1, p, 0); XFlush(d); usleep(60000); }
static void m(int x, int y) { XTestFakeMotionEvent(d, -1, x, y, 0); XFlush(d); usleep(40000); }
static void k(const char *nom, int p) { KeyCode c = XKeysymToKeycode(d, XStringToKeysym(nom)); XTestFakeKeyEvent(d, c, p, 0); XFlush(d); usleep(40000); }
int main(int c, char **v)
{
	if (c < 2 || !(d = XOpenDisplay(NULL))) return 1;
	if (!strcmp(v[1], "touche")) {
		int i;
		for (i = 3; i < c; i++) k(v[i], 1);
		k(v[2], 1); k(v[2], 0);
		for (i = c - 1; i >= 3; i--) k(v[i], 0);
	}
	else if (!strcmp(v[1], "bouge")) m(atoi(v[2]), atoi(v[3]));
	else if (!strcmp(v[1], "presse")) b(1);
	else if (!strcmp(v[1], "relache")) b(0);
	else if (!strcmp(v[1], "clic")) { m(atoi(v[2]), atoi(v[3])); b(1); b(0); }
	else if (!strcmp(v[1], "clicrapide")) {
		XTestFakeMotionEvent(d, -1, atoi(v[2]), atoi(v[3]), 0);
		XFlush(d);
		XTestFakeButtonEvent(d, 1, 1, 0);
		XFlush(d);
		XTestFakeButtonEvent(d, 1, 0, 0);
	}
	else if (!strcmp(v[1], "doublemain")) {	/* rythme humain : doublemain X Y ÉCART_MS */
		int g = atoi(v[4]) * 1000;
		m(atoi(v[2]), atoi(v[3]));
		XTestFakeButtonEvent(d, 1, 1, 0); XFlush(d); usleep(80000);
		XTestFakeButtonEvent(d, 1, 0, 0); XFlush(d); usleep((useconds_t)g);
		XTestFakeButtonEvent(d, 1, 1, 0); XFlush(d); usleep(80000);
		XTestFakeButtonEvent(d, 1, 0, 0); XFlush(d);
	}
	else if (!strcmp(v[1], "double")) { m(atoi(v[2]), atoi(v[3])); b(1); b(0); b(1); b(0); }
	else if (!strcmp(v[1], "glisse")) {
		int x1 = atoi(v[2]), y1 = atoi(v[3]), x2 = atoi(v[4]), y2 = atoi(v[5]), i;
		m(x1, y1); b(1);
		for (i = 1; i <= 8; i++) m(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8);
		b(0);
	}
	XSync(d, False);
	return 0;
}
