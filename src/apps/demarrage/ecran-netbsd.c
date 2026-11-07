/* platine-ecran-netbsd : écran de démarrage façon Mac OS 9 pour NetBSD.
 * Dessine sur /dev/ttyE0 en mode framebuffer (WSDISPLAYIO_MODE_DUMBFB) pendant les
 * scripts rc, avant X, à la suite du Happy Mac que le noyau affiche (SPLASHSCREEN).
 * Progression : les lignes « [running /etc/rc.d/NOM] » que /etc/rc écrit lui-même dans
 * /var/run/rc.log (aucune modification de /etc/rc) ; le total et les scripts passés avant
 * son lancement sont dans /var/run/platine-etapes. Une icône d'extension par script.
 * Sécurité : sans X au bout de 60 s, la console redevient visible (fin du splash). Laisse /var/run/platine-demarrage.etat pour platine-demarrage :
 *   total N
 *   ext-<icône>\n...
 * Usage : platine-ecran-netbsd   (lancé par /etc/rc.d/platine_ecran)
 */
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dev/wscons/wsconsio.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WSDISPDEV "/dev/ttyE0"
#define ASSETS_DIR "/opt/platine/assets"
#define ETAT_PATH "/var/run/platine-demarrage.etat"
#define IMAGE_PATH "/var/run/platine-demarrage.ppm"
#define RCLOG_PATH "/var/run/rc.log"
#define X_SOCKET "/tmp/.X11-unix/X0"

#define FW 422
#define FH 323
#define MAX_EXT 64

struct fb {
	int fd;
	void *base;
	unsigned int *ptr;
	uint64_t len;
	uint64_t offset;
	uint64_t maplen;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t bpp;
	uint32_t roff, goff, boff;
	unsigned int omode;
	unsigned char *reel;	/* l'écran ; ptr est un tampon en mémoire, copié d'un bloc */
};

static struct fb fb;
static int sw, sh, fx0, fy0;
static unsigned int *happy, *logo, *fond, *fen_vide, *fen_barre;
static int happy_w, happy_h, logo_w, logo_h, fond_w, fond_h, fv_w, fv_h, fb_w, fb_h;
static unsigned int *ext[MAX_EXT];
static int ext_w[MAX_EXT], ext_h[MAX_EXT];
static int n_ext, total = 12;
static char etat[4096] = "";

static long ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void pause_ms(int ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static unsigned int rgb(unsigned int r, unsigned int g, unsigned int b)
{
	return (r << fb.roff) | (g << fb.goff) | (b << fb.boff);
}

static void point(int x, int y, unsigned int c)
{
	if (x < 0 || y < 0 || x >= sw || y >= sh)
		return;
	unsigned char *p = (unsigned char *)fb.ptr + (uint64_t)y * fb.stride + (uint64_t)x * (fb.bpp / 8);
	memcpy(p, &c, fb.bpp / 8);
}

static unsigned int lire(int x, int y)
{
	if (x < 0 || y < 0 || x >= sw || y >= sh)
		return 0;
	unsigned char *p = (unsigned char *)fb.ptr + (uint64_t)y * fb.stride + (uint64_t)x * (fb.bpp / 8);
	unsigned int c = 0;
	memcpy(&c, p, fb.bpp / 8);
	return c;
}

static void rect(int x0, int y0, int x1, int y1, unsigned int c)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			point(x, y, c);
}

static void cadre(int x0, int y0, int x1, int y1, unsigned int c)
{
	for (int x = x0; x < x1; x++) { point(x, y0, c); point(x, y1 - 1, c); }
	for (int y = y0; y < y1; y++) { point(x0, y, c); point(x1 - 1, y, c); }
}

static void ligne_h(int x0, int y, int x1, unsigned int c)
{
	for (int x = x0; x < x1; x++) point(x, y, c);
}

static void ligne_v(int x, int y0, int y1, unsigned int c)
{
	for (int y = y0; y < y1; y++) point(x, y, c);
}

/* image ARGB prémultiplié BGRA (format Platine) */
static unsigned int *load_argb(const char *name, int *w, int *h)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/icones/%s.argb", ASSETS_DIR, name);
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	unsigned char hdr[8];
	if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "ARGB", 4) != 0) {
		fclose(f); return NULL;
	}
	int W = (hdr[4] << 8) | hdr[5];
	int H = (hdr[6] << 8) | hdr[7];
	unsigned char *src = malloc(W * H * 4);
	if (!src) { fclose(f); return NULL; }
	if ((int)fread(src, 4, W * H, f) != W * H) {
		free(src); fclose(f); return NULL;
	}
	fclose(f);
	unsigned int *dst = malloc(W * H * sizeof(unsigned int));
	if (!dst) { free(src); return NULL; }
	for (int i = 0; i < W * H; i++) {
		unsigned int b = src[i * 4 + 0];
		unsigned int g = src[i * 4 + 1];
		unsigned int r = src[i * 4 + 2];
		unsigned int a = src[i * 4 + 3];
		if (a == 255) {
			dst[i] = rgb(r, g, b) | 0xff000000u;	/* opaque : sinon image() le saute */
		} else if (a == 0) {
			dst[i] = 0; /* placeholder */
		} else {
			/* dé-prémultiplié approximatif */
			r = (r * 255) / a;
			g = (g * 255) / a;
			b = (b * 255) / a;
			dst[i] = rgb(r, g, b);
			dst[i] |= a << 24;
		}
	}
	free(src);
	*w = W; *h = H;
	return dst;
}

static unsigned int *load_ppm(const char *name, int *w, int *h)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", ASSETS_DIR, name);
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	int W, H, M;
	if (fscanf(f, "P6 %d %d %d", &W, &H, &M) != 3 || M != 255) {
		fclose(f); return NULL;
	}
	fgetc(f); /* espace */
	unsigned char *src = malloc(W * H * 3);
	if (!src || (int)fread(src, 3, W * H, f) != W * H) {
		free(src); fclose(f); return NULL;
	}
	fclose(f);
	unsigned int *dst = malloc(W * H * sizeof(unsigned int));
	if (!dst) { free(src); return NULL; }
	for (int i = 0; i < W * H; i++) {
		unsigned int r = src[i * 3 + 0];
		unsigned int g = src[i * 3 + 1];
		unsigned int b = src[i * 3 + 2];
		dst[i] = rgb(r, g, b);
	}
	free(src);
	*w = W; *h = H;
	return dst;
}

static void image(const unsigned int *px, int W, int H, int x0, int y0, int has_alpha)
{
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			unsigned int c = px[y * W + x];
			unsigned int a = has_alpha ? (c >> 24) & 255 : 255;
			if (a == 255) {
				point(x0 + x, y0 + y, c & 0xffffff);
			} else if (a > 0) {
				unsigned int f = lire(x0 + x, y0 + y);
				unsigned int r = (((c >> 16) & 255) * a + ((f >> fb.roff) & 255) * (255 - a)) / 255;
				unsigned int g = (((c >> 8) & 255) * a + ((f >> fb.goff) & 255) * (255 - a)) / 255;
				unsigned int b = (((c >> 0) & 255) * a + ((f >> fb.boff) & 255) * (255 - a)) / 255;
				point(x0 + x, y0 + y, rgb(r, g, b));
			}
		}
	}
}

static void motif(void)
{
	if (!fond) {
		rect(0, 0, sw, sh, rgb(99, 99, 156));
		return;
	}
	for (int y = 0; y < sh; y += fond_h)
		for (int x = 0; x < sw; x += fond_w)
			image(fond, fond_w, fond_h, x, y, 0);
}

static void fenetre(const unsigned int *txt, int tl, int th)
{
	unsigned int gris = rgb(221, 221, 221);
	unsigned int blanc = rgb(255, 255, 255);
	unsigned int noir = rgb(0, 0, 0);

	rect(fx0, fy0, fx0 + FW, fy0 + FH, gris);
	cadre(fx0, fy0, fx0 + FW, fy0 + FH, noir);
	ligne_h(fx0 + 1, fy0 + 1, fx0 + FW - 1, blanc);
	ligne_v(fx0 + 1, fy0 + 1, fy0 + FH - 1, blanc);
	rect(fx0 + 36, fy0 + 27, fx0 + 385, fy0 + 249, blanc);
	if (logo)
		image(logo, logo_w, logo_h, fx0 + 37 + (347 - logo_w) / 2, fy0 + 28 + (220 - logo_h) / 2, 0);
	cadre(fx0 + 36, fy0 + 27, fx0 + 385, fy0 + 249, noir);
	rect(fx0 + 2, fy0 + 262, fx0 + FW - 2, fy0 + 284, gris);
	if (txt)
		image(txt, tl, th, fx0 + FW / 2 - tl / 2, fy0 + 266, 1);
}

/* remplissage de la barre, copie de ptk_progress (le creux vide est dans l'image de X) */
static void progression(int v, int max)
{
	static const unsigned char grad[10][3] = {
		{ 0x33, 0x33, 0x99 }, { 0x66, 0x66, 0xcc }, { 0x99, 0x99, 0xff }, { 0xcc, 0xcc, 0xff },
		{ 0xee, 0xee, 0xee }, { 0xcc, 0xcc, 0xff }, { 0x99, 0x99, 0xff }, { 0x66, 0x66, 0xcc },
		{ 0x33, 0x33, 0x99 }, { 0x00, 0x00, 0x55 } };
	int x = fx0 + 131, y = fy0 + 291, w = 160, h = 12, inner = h - 4, fill;

	if (max <= 0) max = 1;
	if (v > max) v = max;
	if (v < 0) v = 0;
	fill = (w - 4) * v / max;
	if (fill <= 0)
		return;
	for (int i = 0; i < inner; i++) {
		int k = i * 9 / (inner - 1);
		ligne_h(x + 2, y + 2 + i, x + 2 + fill, rgb(grad[k][0], grad[k][1], grad[k][2]));
	}
	ligne_v(x + 1, y + 2, y + h - 2, rgb(0x66, 0x66, 0xcc));
}

static int ix = 8, iy, n_icones;

static void icone(const char *service)
{
	/* Recherche une icône d'extension ARGB correspondant au service */
	static const struct { const char *nom; const char *icone; } assoc[] = {
		{ "fsck_root", "ext-startup-disk" },
		{ "modules", "ext-shared-library-manager" },
		{ "root", "ext-startup-disk" },
		{ "sysctl", "ext-general-controls" },
		{ "swap1", "ext-memory" },
		{ "swap2", "ext-memory" },
		{ "mountcritlocal", "ext-foreign-file-access" },
		{ "clearcritlocal", "ext-folder-actions" },
		{ "random_seed", "ext-keychain-access" },
		{ "entropy", "ext-keychain-access" },
		{ "bootconf", "ext-extensions-manager" },
		{ "network", "ext-open-transport" },
		{ "wpa_supplicant", "ext-internet" },
		{ "dhcpcd", "ext-internet-config-extension" },
		{ "sysdb", "ext-text-encoding-converter" },
		{ "wscons", "ext-monitors" },
		{ "syslogd", "ext-system-monitor-plugins" },
		{ "mountall", "ext-foreign-file-access" },
		{ "cleartmp", "ext-folder-actions" },
		{ "dmesg", "ext-apple-guide" },
		{ "ldconfig", "ext-shared-library-manager-ppc" },
		{ "securelevel", "ext-multiple-users" },
		{ "local", "ext-location-manager" },
		{ "powerd", "ext-energy-saver" },
		{ "sshd", "ext-file-sharing" },
		{ NULL, NULL }
	};
	const unsigned int *px = NULL;
	const char *nom_icone = "ext-extensions-manager";
	int W = 32, H = 32;
	if (n_ext < MAX_EXT) {
		for (int i = 0; assoc[i].nom; i++) {
			if (strcmp(service, assoc[i].nom) == 0 ||
			    strncmp(service, assoc[i].nom, strlen(assoc[i].nom)) == 0) {
				unsigned int *img = load_argb(assoc[i].icone, &W, &H);
				nom_icone = assoc[i].icone;
				if (img) {
					px = img;
					ext[n_ext] = img;
					ext_w[n_ext] = W;
					ext_h[n_ext] = H;
					n_ext++;
				}
				break;
			}
		}
	}
	if (!px && n_ext < MAX_EXT) {
		unsigned int *img = load_argb("ext-extensions-manager", &W, &H);
		if (img) {
			px = img;
			ext[n_ext] = img;
			ext_w[n_ext] = W;
			ext_h[n_ext] = H;
			n_ext++;
		}
	}
	if (px) {
		image(px, W, H, ix, iy, 1);
		ix += 40;
		if (ix > sw - 40) { ix = 8; iy -= 40; }
	} else {
		/* repli : carré gris avec bord */
		rect(ix + 4, iy + 4, ix + 28, iy + 28, rgb(187, 187, 187));
		cadre(ix + 4, iy + 4, ix + 28, iy + 28, rgb(0, 0, 0));
		ix += 40;
		if (ix > sw - 40) { ix = 8; iy -= 40; }
	}
	n_icones++;
	char e[128];
	snprintf(e, sizeof e, "%s\n", px ? nom_icone : "ext-extensions-manager");
	if (strlen(etat) + strlen(e) < sizeof etat) strcat(etat, e);
}

static int fb_init(void)
{
	fb.fd = open(WSDISPDEV, O_RDWR | O_NONBLOCK);
	if (fb.fd < 0) {
		perror("platine-ecran-netbsd: open " WSDISPDEV);
		return -1;
	}
	struct wsdisplayio_fbinfo fbi;
	if (ioctl(fb.fd, WSDISPLAYIO_GET_FBINFO, &fbi) < 0) {
		perror("platine-ecran-netbsd: WSDISPLAYIO_GET_FBINFO");
		close(fb.fd);
		return -1;
	}
	if (fbi.fbi_bitsperpixel != 32 && fbi.fbi_bitsperpixel != 24 && fbi.fbi_bitsperpixel != 16) {
		fprintf(stderr, "platine-ecran-netbsd: profondeur %d non supportée\n", fbi.fbi_bitsperpixel);
		close(fb.fd);
		return -1;
	}
	fb.width = fbi.fbi_width;
	fb.height = fbi.fbi_height;
	fb.stride = fbi.fbi_stride;
	fb.bpp = fbi.fbi_bitsperpixel;
	fb.len = fbi.fbi_fbsize;
	fb.offset = fbi.fbi_fboffset;
	if (fbi.fbi_pixeltype == WSFB_RGB) {
		fb.roff = fbi.fbi_subtype.fbi_rgbmasks.red_offset;
		fb.goff = fbi.fbi_subtype.fbi_rgbmasks.green_offset;
		fb.boff = fbi.fbi_subtype.fbi_rgbmasks.blue_offset;
	} else {
		/* fallback RGB888 */
		fb.roff = 16; fb.goff = 8; fb.boff = 0;
	}
	if (ioctl(fb.fd, WSDISPLAYIO_GMODE, &fb.omode) < 0) {
		perror("platine-ecran-netbsd: WSDISPLAYIO_GMODE");
		close(fb.fd);
		return -1;
	}
	unsigned int mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(fb.fd, WSDISPLAYIO_SMODE, &mode) < 0) {
		perror("platine-ecran-netbsd: WSDISPLAYIO_SMODE");
		close(fb.fd);
		return -1;
	}
	/* mmap exige un offset aligné sur une page ; on mappe depuis 0 et on
	 * applique fbi_fboffset sur le pointeur. */
	fb.maplen = fb.len + fb.offset;
	fb.base = mmap(0, fb.maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
	if (fb.base == MAP_FAILED) {
		perror("platine-ecran-netbsd: mmap");
		close(fb.fd);
		return -1;
	}
	fb.reel = (unsigned char *)fb.base + fb.offset;
	/* double tampon : on dessine en mémoire et on copie l'image entière (plus d'artefacts) ;
	 * il part de l'écran actuel, donc du Happy Mac du noyau */
	fb.ptr = malloc((size_t)fb.stride * fb.height);
	if (fb.ptr == NULL)
		fb.ptr = (unsigned int *)fb.reel;
	else
		memcpy(fb.ptr, fb.reel, (size_t)fb.stride * fb.height);
	sw = fb.width; sh = fb.height;
	fx0 = (sw - FW) / 2;
	fy0 = sh <= 500 ? (sh - FH) / 2 - 20 : (sh - FH) / 2 - 55;
	if (fy0 < 4) fy0 = 4;
	iy = sh - 40;
	return 0;
}

static void montrer(void)
{
	if ((unsigned char *)fb.ptr != fb.reel)
		memcpy(fb.reel, fb.ptr, (size_t)fb.stride * fb.height);
}

/* /etc/platine-capture présent : copie de l'écran réel en PPM (mise au point du démarrage) */
static void capture_de(int n, const unsigned char *src)
{
	char chemin[64];
	FILE *f;

	if (access("/etc/platine-capture", F_OK) != 0)
		return;
	snprintf(chemin, sizeof chemin, "/var/run/platine-capture-%02d.ppm", n);
	if ((f = fopen(chemin, "wb")) == NULL)
		return;
	fprintf(f, "P6 %d %d 255\n", sw, sh);
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			unsigned int c = 0;

			memcpy(&c, src + (size_t)y * fb.stride + (size_t)x * (fb.bpp / 8), fb.bpp / 8);
			fputc((c >> fb.roff) & 255, f);
			fputc((c >> fb.goff) & 255, f);
			fputc((c >> fb.boff) & 255, f);
		}
	fclose(f);
}

/* dernière image (RGB) : platine-demarrage la pose en fond dès la connexion à X */
static void capture_image(void)
{
	FILE *f = fopen(IMAGE_PATH ".neuf", "wb");
	unsigned char *ligne = malloc((size_t)sw * 3);

	if (f == NULL || ligne == NULL) {
		if (f) fclose(f);
		free(ligne);
		return;
	}
	fprintf(f, "P6 %d %d 255\n", sw, sh);
	for (int y = 0; y < sh; y++) {
		for (int x = 0; x < sw; x++) {
			unsigned int c = lire(x, y);

			ligne[x * 3] = (c >> fb.roff) & 255;
			ligne[x * 3 + 1] = (c >> fb.goff) & 255;
			ligne[x * 3 + 2] = (c >> fb.boff) & 255;
		}
		fwrite(ligne, 3, (size_t)sw, f);
	}
	fclose(f);
	free(ligne);
	chmod(IMAGE_PATH ".neuf", 0644);
	rename(IMAGE_PATH ".neuf", IMAGE_PATH);
}

static void capture(int n)
{
	capture_de(n, fb.reel);
}

static void fb_fini(void)
{
	if (fb.base && fb.base != MAP_FAILED) munmap(fb.base, fb.maplen);
	if (fb.fd >= 0) {
		ioctl(fb.fd, WSDISPLAYIO_SMODE, &fb.omode);
		close(fb.fd);
	}
}

static void sauver_etat(void)
{
	FILE *f = fopen(ETAT_PATH, "w");
	if (f) {
		fprintf(f, "total %d\n%s", total, etat);
		fclose(f);
		chmod(ETAT_PATH, 0644);
	}
}

static void scene_demarrage(int fait)
{
	motif();
	if (fen_barre)
		image(fen_barre, fb_w, fb_h, fx0, fy0, 0);
	else
		fenetre(NULL, 0, 0);
	progression(fait, total);
	for (int i = 0; i < n_icones; i++)
		image(ext[i], ext_w[i], ext_h[i], 8 + (i % ((sw - 8) / 40)) * 40,
		    sh - 40 - (i / ((sw - 8) / 40)) * 40, 1);
}

/* rc.log peut contenir des scripts déjà listés dans platine-etapes (avant notre lancement) */
static char vus[2048];
static int deja_vu(const char *nom)
{
	char cle[80];

	snprintf(cle, sizeof cle, " %s ", nom);
	if (strstr(vus, cle) != NULL)
		return 1;
	if (strlen(vus) + strlen(cle) < sizeof vus)
		strcat(vus, cle + (vus[0] ? 1 : 0));
	return 0;
}

int main(int argc, char **argv)
{
	if (fb_init() < 0)
		return 1;

	happy = load_ppm("happymac.ppm", &happy_w, &happy_h);
	logo = load_ppm("demarrage.ppm", &logo_w, &logo_h);
	fond = load_ppm("fond.ppm", &fond_w, &fond_h);
	fen_vide = load_ppm("demarrage-fenetre-vide.ppm", &fv_w, &fv_h);
	fen_barre = load_ppm("demarrage-fenetre.ppm", &fb_w, &fb_h);

	/* le splash du noyau, gardé en mémoire : /var n'est pas encore monté */
	unsigned char *splash = NULL;
	if (access("/etc/platine-capture", F_OK) == 0 &&
	    (splash = malloc((size_t)fb.stride * fb.height)) != NULL)
		memcpy(splash, fb.reel, (size_t)fb.stride * fb.height);
	/* platine-ecran-netbsd TOTAL [scripts déjà passés…] (voir /etc/rc.d/platine_ecran) */
	int recus = 0;
	if (argc > 1 && atoi(argv[1]) > 0)
		total = atoi(argv[1]);
	for (int i = 2; i < argc; i++)
		if (!deja_vu(argv[i])) {
			icone(argv[i]);
			recus++;
		}
	/* 1. gris et Happy Mac, à l'endroit exact du splash du noyau (centré, fond #888) */
	rect(0, 0, sw, sh, rgb(136, 136, 136));
	if (happy)
		image(happy, happy_w, happy_h, (sw - happy_w) / 2, (sh - happy_h) / 2, 0);
	montrer();
	capture(1);
	int n_capture = 2, montres = -1, image_ecrite = 0;

	long t0 = ms(), x_vu = 0;
	time_t debut = time(NULL);
	off_t lu = 0;
	char ligne[256];
	size_t n_ligne = 0;
	long prochaine_etape = t0 + 700;
	int etape = 0;

	for (;;) {
		long t = ms();
		FILE *f;

		f = fopen(RCLOG_PATH, "r");
		if (f != NULL) {
			int c;

			fseeko(f, lu, SEEK_SET);
			while ((c = fgetc(f)) != EOF) {
				lu++;
				if (c != '\n') {
					if (n_ligne < sizeof(ligne) - 1)
						ligne[n_ligne++] = (char)c;
					continue;
				}
				ligne[n_ligne] = '\0';
				n_ligne = 0;
				/* « [running /etc/rc.d/sshd] » ; rc.log est réécrit à chaque démarrage */
				if (strncmp(ligne, "[running /etc/rc.d/", 19) == 0) {
					char *nom = ligne + 19, *fin = strchr(nom, ']');

					if (fin) *fin = '\0';
					if (strcmp(nom, "platine_ecran") != 0 && !deja_vu(nom)) {
						icone(nom);
						recus++;
					}
				}
			}
			fclose(f);
		}

		if (etape == 0 && t >= prochaine_etape) {
			rect(0, 0, sw, sh, rgb(136, 136, 136));
			montrer();
			prochaine_etape = t + 350;
			etape = 1;
		} else if (etape == 1 && t >= prochaine_etape) {
			if (fen_vide)
				image(fen_vide, fv_w, fv_h, fx0, fy0, 0);
			else
				fenetre(NULL, 0, 0);
			montrer();
			capture(n_capture++);
			prochaine_etape = t + 800;
			etape = 2;
		} else if (etape == 2 && t >= prochaine_etape) {
			etape = 3;
		}
		/* une image seulement quand une icône arrive (au plus toutes les 120 ms) */
		if (etape == 3 && recus != montres && t >= prochaine_etape) {
			scene_demarrage(recus);
			montrer();
			if (montres < 0 || recus == total)
				capture(n_capture++);
			montres = recus;
			/* image finale prête avant X : platine-demarrage la pose en fond aussitôt */
			if (recus >= total && !image_ecrite) {
				capture_image();
				image_ecrite = 1;
			}
			prochaine_etape = t + 120;
		}

		/* X démarre : l'état part pour platine-demarrage. On garde ttyE0 ouvert quelques
		 * secondes, le temps que X prenne l'écran : fermer plus tôt ramènerait la console. */
		struct stat st;
		/* un socket plus ancien que nous vient d'une session précédente */
		if (stat(X_SOCKET, &st) == 0 && st.st_mtime >= debut) {
			if (x_vu == 0) {
				x_vu = t;
				scene_demarrage(recus);
				montrer();
				capture(n_capture++);
				if (splash) {
					capture_de(0, splash);
					free(splash);
					splash = NULL;
				}
				if (!image_ecrite)
					capture_image();
				sauver_etat();
			} else if (t - x_vu > 4000)
				break;
		}
		if (x_vu == 0 && t - t0 > 60000)	/* X n'est jamais venu : rendre la console */
			break;
		pause_ms(50);
	}
	if (x_vu == 0) {
		int non = 0;

		sauver_etat();
		/* le splash du noyau masque la console : la rendre visible (mode mono-utilisateur, erreurs) */
		ioctl(fb.fd, WSDISPLAYIO_SSPLASH, &non);
		fb_fini();
	} else if (fb.base && fb.base != MAP_FAILED)
		munmap(fb.base, fb.maplen);
	return 0;
}
