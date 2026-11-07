CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu99
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CPPFLAGS += -Itoolbox $(shell $(PKG_CONFIG) --cflags x11 xft xpm)
LDLIBS += $(shell $(PKG_CONFIG) --libs x11 xft xpm fontconfig) -lXext -lXrender

# Tout ce qui est produit va dans build/ ; la racine ne contient que les sources.
B = build

PTK     = $(B)/ptk.o $(B)/ptk_identite.o
PTK_APP = $(B)/ptk_app.o $(PTK)
H_PTK   = toolbox/ptk.h toolbox/ptk_sys.h toolbox/ptk_identite.h
H_APP   = toolbox/ptk_app.h $(H_PTK)

APPS = wm about strip test finder demo info tableau calculette demarrage alerte textedit theiere installateur instantane keycaps pensebetes profileur quicktime expander
# platine-osd utilise sys/audioio.h : NetBSD seulement (voir outils/construire-netbsd.sh)
ifeq ($(shell uname -s),NetBSD)
APPS += osd
endif
BINS = $(APPS:%=$(B)/platine-%)

all: $(BINS)

.SECONDEXPANSION:

$(B):
	mkdir -p $@

$(B)/ptk.o: toolbox/ptk.c $(H_PTK) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(B)/ptk_identite.o: toolbox/ptk_identite.c $(H_PTK) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(B)/ptk_app.o: toolbox/ptk_app.c $(H_APP) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(B)/fsops.o: src/apps/finder/fsops.c src/apps/finder/fsops.h | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Applications qui n'utilisent que libptk
$(B)/platine-wm: src/wm/wm.c src/wm/motifs.h $(PTK) $(H_PTK)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/wm/wm.c $(PTK) $(LDLIBS)

$(B)/platine-strip: src/apps/strip/strip.c src/apps/strip/motifs.h $(PTK) $(H_PTK)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/apps/strip/strip.c $(PTK) $(LDLIBS)

$(B)/platine-%: src/apps/%/$$*.c $(PTK) $(H_PTK)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(PTK) $(LDLIBS)

# Applications Toolbox (ptk_app)
$(B)/platine-demo $(B)/platine-info $(B)/platine-tableau $(B)/platine-alerte $(B)/platine-textedit $(B)/platine-installateur $(B)/platine-keycaps $(B)/platine-pensebetes $(B)/platine-profileur $(B)/platine-quicktime $(B)/platine-expander: $(B)/platine-%: src/apps/%/$$*.c $(PTK_APP) $(H_APP)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(PTK_APP) $(LDLIBS)

# Instantané : Toolbox, PNG par zlib, curseur par XFixes
$(B)/platine-instantane: src/apps/instantane/instantane.c $(PTK_APP) $(H_APP)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(PTK_APP) $(LDLIBS) $(shell $(PKG_CONFIG) --libs zlib xfixes)

$(B)/platine-finder: src/apps/finder/finder.c $(B)/fsops.o $(PTK) $(H_PTK)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/apps/finder/finder.c $(B)/fsops.o $(PTK) $(LDLIBS)

# Toolbox GL : programmes OpenGL/GLX (Théière, jeux) ; ni Xft ni briques de dessin
PTK_GL = $(B)/ptk_gl.o
H_GL   = toolbox/ptk_gl.h
LDGL   = -lGL -lX11 -lm

$(B)/ptk_gl.o: toolbox/ptk_gl.c $(H_GL) | $(B)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(B)/platine-theiere: src/apps/theiere/theiere.c $(PTK_GL) $(H_GL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(PTK_GL) $(LDGL)

TESTS = fsops fuseau xvfb focus outline arborescence finder finder-dnd finder-trash finder-bureau finder-scroll \
	finder-spring finder-liste finder-alias finder-coller finder-xdnd finder-types grille \
	reglages toolbox strip session textedit theiere identite installateur pomme \
	enregistrer instantane expander

test: all
	@set -e; for t in $(TESTS); do echo "== $$t"; ./tests/run-$$t.sh; done

clean:
	rm -rf $(B)

.PHONY: all clean test
