/*
 * ptk_gl.c — Toolbox GL de Platine (voir ptk_gl.h et docs/11-graphisme-gl.md).
 */
#include "ptk_gl.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#define PERIODE (1.0 / 60.0)	/* attente maximale sans synchro verticale */

double
ptk_gl_horloge(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void
texte(Display *d, Window w, const char *prop, const char *v)
{
	XChangeProperty(d, w, XInternAtom(d, prop, False), XA_STRING, 8,
	    PropModeReplace, (const unsigned char *)v, (int)strlen(v));
}

static int
extension(const char *liste, const char *nom)
{
	size_t n = strlen(nom);
	const char *p = liste;

	while (p != NULL && (p = strstr(p, nom)) != NULL) {
		if ((p == liste || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0'))
			return 1;
		p += n;
	}
	return 0;
}

/* intervalle d'échange 1 : glXSwapBuffers attend le retour de trame */
static int
synchro_verticale(Display *d)
{
	const char *ext = glXQueryExtensionsString(d, DefaultScreen(d));
	int (*mesa)(unsigned int);
	int (*sgi)(int);

	if (ext == NULL)
		return 0;
	if (extension(ext, "GLX_MESA_swap_control")) {
		mesa = (int (*)(unsigned int))glXGetProcAddressARB(
		    (const GLubyte *)"glXSwapIntervalMESA");
		if (mesa != NULL && mesa(1) == 0)
			return 1;
	}
	if (extension(ext, "GLX_SGI_swap_control")) {
		sgi = (int (*)(int))glXGetProcAddressARB(
		    (const GLubyte *)"glXSwapIntervalSGI");
		if (sgi != NULL && sgi(1) == 0)
			return 1;
	}
	return 0;
}

/* tampons de sommets, par l'extension ARB : le pilote i915 n'annonce
 * qu'OpenGL 1.4 mais fournit GL_ARB_vertex_buffer_object */
typedef void (*fn_gen_buffers)(GLsizei, GLuint *);
typedef void (*fn_bind_buffer)(GLenum, GLuint);
typedef void (*fn_buffer_data)(GLenum, ptrdiff_t, const void *, GLenum);
static fn_gen_buffers gen_buffers;
static fn_bind_buffer bind_buffer;
static fn_buffer_data buffer_data;

static int
tampons_de_sommets(void)
{
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);

	if (ext == NULL || !extension(ext, "GL_ARB_vertex_buffer_object"))
		return 0;
	gen_buffers = (fn_gen_buffers)glXGetProcAddressARB(
	    (const GLubyte *)"glGenBuffersARB");
	bind_buffer = (fn_bind_buffer)glXGetProcAddressARB(
	    (const GLubyte *)"glBindBufferARB");
	buffer_data = (fn_buffer_data)glXGetProcAddressARB(
	    (const GLubyte *)"glBufferDataARB");
	return gen_buffers != NULL && bind_buffer != NULL && buffer_data != NULL;
}

int
ptk_gl_ouvrir(PtkGl *g, const char *nom, const char *icone, const char *menu,
    int largeur, int hauteur)
{
	int attribs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16,
	    GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1, None };
	XVisualInfo *vi;
	XSetWindowAttributes a;
	XSizeHints hints;
	const char *r;

	memset(g, 0, sizeof(*g));
	snprintf(g->nom, sizeof(g->nom), "%s", nom);
	g->dpy = XOpenDisplay(NULL);
	if (g->dpy == NULL) {
		fprintf(stderr, "%s : pas de serveur X\n", nom);
		return -1;
	}
	vi = glXChooseVisual(g->dpy, DefaultScreen(g->dpy), attribs);
	if (vi == NULL) {
		fprintf(stderr, "%s : aucun visuel GLX (double tampon + profondeur)\n",
		    nom);
		return -1;
	}
	g->ctx = glXCreateContext(g->dpy, vi, NULL, True);
	a.colormap = XCreateColormap(g->dpy, RootWindow(g->dpy, vi->screen),
	    vi->visual, AllocNone);
	a.event_mask = ExposureMask | StructureNotifyMask | VisibilityChangeMask |
	    KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask;
	g->win = XCreateWindow(g->dpy, RootWindow(g->dpy, vi->screen), 60, 60,
	    largeur, hauteur, 0, vi->depth, InputOutput, vi->visual,
	    CWColormap | CWEventMask, &a);
	XFree(vi);
	XStoreName(g->dpy, g->win, nom);
	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | PSize;
	hints.min_width = 160;
	hints.min_height = 120;
	hints.width = largeur;
	hints.height = hauteur;
	XSetWMNormalHints(g->dpy, g->win, &hints);
	g->wm_protocols = XInternAtom(g->dpy, "WM_PROTOCOLS", False);
	g->wm_delete = XInternAtom(g->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(g->dpy, g->win, &g->wm_delete, 1);
	/* citoyenne du bureau Platine : nom, icône et menus dans la barre */
	texte(g->dpy, g->win, "_PLATINE_APP_NAME", nom);
	texte(g->dpy, g->win, "_PLATINE_ICON", icone != NULL ? icone : "app");
	if (menu != NULL)
		texte(g->dpy, g->win, "_PLATINE_MENU", menu);
	g->a_cmd = XInternAtom(g->dpy, "_PLATINE_COMMAND", False);
	XMapWindow(g->dpy, g->win);
	glXMakeCurrent(g->dpy, g->win, g->ctx);

	g->largeur = largeur;
	g->hauteur = hauteur;
	g->vivant = 1;
	g->visible = 1;
	g->rendu = r = (const char *)glGetString(GL_RENDERER);
	if (strstr(r, "llvmpipe") != NULL || strstr(r, "softpipe") != NULL ||
	    strstr(r, "swrast") != NULL)
		g->nature = "logiciel";
	else if (glXIsDirect(g->dpy, g->ctx))
		g->nature = "matériel (rendu direct)";
	else
		g->nature = "indirect";
	g->vsync = synchro_verticale(g->dpy);
	g->vbo = tampons_de_sommets();
	printf("%s : moteur %s, %s, %s, %s\n", nom, r, g->nature,
	    g->vsync ? "synchro verticale" : "cadence par select",
	    g->vbo ? "tampons de sommets" : "tableaux de sommets");
	g->t = g->t_titre = ptk_gl_horloge();
	return 0;
}

static void
commande(PtkGl *g)
{
	Atom type;
	int form;
	unsigned long n, ap;
	unsigned char *don = NULL;

	if (XGetWindowProperty(g->dpy, g->win, g->a_cmd, 0, 256, True,
	    XA_STRING, &type, &form, &n, &ap, &don) != Success || don == NULL)
		return;
	if (g->commande != NULL)
		g->commande(g, (const char *)don, g->user);
	else if (strcmp((char *)don, "Fermer") == 0 ||
	    strcmp((char *)don, "Quitter") == 0)
		g->vivant = 0;
	XFree(don);
}

static void
traiter(PtkGl *g, XEvent *ev)
{
	switch (ev->type) {
	case MapNotify:
		g->montree = 1;
		break;
	case UnmapNotify:
		g->montree = 0;
		break;
	case VisibilityNotify:
		g->visible = ev->xvisibility.state != VisibilityFullyObscured;
		break;
	case ConfigureNotify:
		g->largeur = ev->xconfigure.width;
		g->hauteur = ev->xconfigure.height;
		break;
	case ClientMessage:
		if (ev->xclient.message_type == g->wm_protocols &&
		    (Atom)ev->xclient.data.l[0] == g->wm_delete)
			g->vivant = 0;
		else if (ev->xclient.message_type == g->a_cmd)
			commande(g);
		return;
	}
	if (g->evenement != NULL)
		g->evenement(g, ev, g->user);
}

int
ptk_gl_image(PtkGl *g)
{
	double t;
	int pause = 0;

	for (;;) {
		XEvent ev;

		while (g->vivant && XPending(g->dpy) > 0) {
			XNextEvent(g->dpy, &ev);
			traiter(g, &ev);
		}
		if (!g->vivant)
			return 0;
		/* iconifiée ou entièrement recouverte : rien à dessiner, on
		 * dort sur la connexion X jusqu'au prochain événement */
		if (g->montree && g->visible)
			break;
		XPeekEvent(g->dpy, &ev);
		pause = 1;
	}
	/* avec la synchro, glXSwapBuffers donne seul le rythme ; sans elle,
	 * attente des événements au plus PERIODE (arrondie au tick noyau) */
	if (!g->vsync) {
		int fd = ConnectionNumber(g->dpy);
		fd_set fds;
		struct timeval tv = { 0, (long)(PERIODE * 1e6) };

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		select(fd + 1, &fds, NULL, NULL, &tv);
	}
	t = ptk_gl_horloge();
	if (pause) {
		/* reprise : pas de saut dans l'animation ni dans les i/s */
		g->dt = 0.0;
		g->images = 0;
		g->t_titre = t;
	} else
		g->dt = t - g->t;
	g->t = t;
	glViewport(0, 0, g->largeur, g->hauteur);
	return 1;
}

void
ptk_gl_echanger(PtkGl *g)
{
	glXSwapBuffers(g->dpy, g->win);
	g->images++;
	if (g->t - g->t_titre >= 1.0) {
		g->fps = (int)(g->images / (g->t - g->t_titre) + 0.5);
		g->images = 0;
		g->t_titre = g->t;
		/* intervalle accepté mais ignoré par le pilote : on repasse à
		 * l'attente pour ne pas brûler le processeur */
		if (g->vsync && g->fps > 90) {
			g->vsync = 0;
			printf("%s : synchro verticale inopérante, attente par "
			    "select\n", g->nom);
		}
		if (g->titre_fps) {
			char titre[192];

			snprintf(titre, sizeof(titre), "%s — %s — %d i/s",
			    g->nom, g->rendu, g->fps);
			XStoreName(g->dpy, g->win, titre);
		}
	}
}

void
ptk_gl_fermer(PtkGl *g)
{
	glXMakeCurrent(g->dpy, None, NULL);
	glXDestroyContext(g->dpy, g->ctx);
	XDestroyWindow(g->dpy, g->win);
	XCloseDisplay(g->dpy);
}

void
ptk_gl_maillage(PtkGl *g, PtkGlMaillage *m, const GLfloat *sommets,
    int nb_sommets, const GLushort *indices, int nb_indices)
{
	memset(m, 0, sizeof(*m));
	m->nb_indices = nb_indices;
	m->vbo = g->vbo;
	if (m->vbo) {
		gen_buffers(2, m->tampons);
		bind_buffer(GL_ARRAY_BUFFER_ARB, m->tampons[0]);
		buffer_data(GL_ARRAY_BUFFER_ARB,
		    (ptrdiff_t)nb_sommets * 6 * sizeof(GLfloat), sommets,
		    GL_STATIC_DRAW_ARB);
		bind_buffer(GL_ELEMENT_ARRAY_BUFFER_ARB, m->tampons[1]);
		buffer_data(GL_ELEMENT_ARRAY_BUFFER_ARB,
		    (ptrdiff_t)nb_indices * sizeof(GLushort), indices,
		    GL_STATIC_DRAW_ARB);
		bind_buffer(GL_ARRAY_BUFFER_ARB, 0);
		bind_buffer(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	} else {
		m->sommets = sommets;
		m->indices = indices;
	}
}

void
ptk_gl_dessiner(const PtkGlMaillage *m)
{
	const char *base = NULL;
	const void *idx = m->indices;

	if (m->vbo) {
		bind_buffer(GL_ARRAY_BUFFER_ARB, m->tampons[0]);
		bind_buffer(GL_ELEMENT_ARRAY_BUFFER_ARB, m->tampons[1]);
		idx = NULL;
	} else
		base = (const char *)m->sommets;
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glVertexPointer(3, GL_FLOAT, 6 * sizeof(GLfloat), base);
	glNormalPointer(GL_FLOAT, 6 * sizeof(GLfloat),
	    base + 3 * sizeof(GLfloat));
	glDrawElements(GL_TRIANGLES, m->nb_indices, GL_UNSIGNED_SHORT, idx);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	if (m->vbo) {
		bind_buffer(GL_ARRAY_BUFFER_ARB, 0);
		bind_buffer(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	}
}

void
ptk_gl_perspective(double fovy, double aspect, double zn, double zf)
{
	double f = 1.0 / tan(fovy * M_PI / 360.0);
	double m[16] = { 0 };

	m[0] = f / aspect;
	m[5] = f;
	m[10] = (zf + zn) / (zn - zf);
	m[11] = -1.0;
	m[14] = 2.0 * zf * zn / (zn - zf);
	glMultMatrixd(m);
}
