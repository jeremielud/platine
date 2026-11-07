#!/bin/sh
# construire-netbsd.sh : compile Platine sous NetBSD (pas de pkg-config ni de GNU make en base).
set -eu
cd "$(dirname "$0")/.."
X=/usr/X11R7
F="-O2 -std=gnu99 -D_NETBSD_SOURCE -Wall -I$X/include -I$X/include/freetype2"
L="-L$X/lib -Wl,-R$X/lib -lXft -lXpm -lfontconfig -lfreetype -lXrender -lXext -lX11 -lm"
cc $F -c -o ptk.o toolbox/ptk.c
cc $F -c -o ptk_identite.o toolbox/ptk_identite.c
cc $F -c -o ptk_app.o toolbox/ptk_app.c
cc $F -c -o fsops.o src/apps/finder/fsops.c
cc $F -o platine-wm src/wm/wm.c ptk.o ptk_identite.o $L
cc $F -o platine-about src/apps/about/about.c ptk.o ptk_identite.o $L
cc $F -o platine-strip src/apps/strip/strip.c ptk.o ptk_identite.o $L
cc $F -o platine-finder src/apps/finder/finder.c fsops.o ptk.o ptk_identite.o $L
cc $F -o platine-info src/apps/info/info.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-demo src/apps/demo/demo.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-osd src/apps/osd/osd.c ptk.o ptk_identite.o $L
cc $F -o platine-demarrage src/apps/demarrage/demarrage.c ptk.o ptk_identite.o $L
cc $F -o platine-calculette src/apps/calculette/calculette.c ptk.o ptk_identite.o $L
cc $F -o platine-tableau src/apps/tableau/tableau.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-alerte src/apps/alerte/alerte.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-textedit src/apps/textedit/textedit.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-expander src/apps/expander/expander.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-installateur src/apps/installateur/installateur.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-instantane src/apps/instantane/instantane.c ptk_app.o ptk.o ptk_identite.o $L -lXfixes -lz
cc $F -o platine-keycaps src/apps/keycaps/keycaps.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-pensebetes src/apps/pensebetes/pensebetes.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-profileur src/apps/profileur/profileur.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -o platine-quicktime src/apps/quicktime/quicktime.c ptk_app.o ptk.o ptk_identite.o $L
cc $F -Itoolbox -c -o ptk_gl.o toolbox/ptk_gl.c
cc $F -Itoolbox -o platine-theiere src/apps/theiere/theiere.c ptk_gl.o -L$X/lib -Wl,-R$X/lib -lGL -lX11 -lm
cc -O2 -std=gnu99 -D_NETBSD_SOURCE -Wall -o platine-ecran-netbsd src/apps/demarrage/ecran-netbsd.c
cc -O2 -I$X/include -o xclic outils/xclic.c -L$X/lib -Wl,-R$X/lib -lXtst -lXext -lX11
strip platine-wm platine-about platine-strip platine-finder platine-info platine-demo platine-osd platine-demarrage platine-calculette platine-tableau platine-alerte platine-textedit platine-expander platine-theiere platine-installateur platine-instantane platine-keycaps platine-pensebetes platine-profileur platine-quicktime platine-ecran-netbsd xclic
ls -la platine-wm platine-about platine-strip platine-finder platine-info platine-demo platine-osd platine-demarrage platine-calculette platine-tableau platine-alerte platine-textedit platine-expander platine-theiere platine-installateur platine-instantane platine-keycaps platine-pensebetes platine-profileur platine-quicktime platine-ecran-netbsd
