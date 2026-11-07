/* Encoche de la case d'agrandissement, fenêtre active (20×20), relevée sur Mac OS 9.2 ;
 * chaque pixel = un gris en hexadécimal. Dessin procédural. */
static const char *const map_grow[20] = {
	"00000000000000000000000000000000ffcccc99",
	"00ffffffffffffffffffffffffffffffffcccc99",
	"00ffcccccccccccccccccccccccccccccccccc99",
	"00ffcccccccccccccccccccccccccccccccccc99",
	"00ffccccccccccccccffffcccccccccccccccc99",
	"00ffccccccccccccffcc77cccccccccccccccc99",
	"00ffccccccccccffcc77ccffffcccccccccccc99",
	"00ffccccccccffcc77ccffcc77cccccccccccc99",
	"00ffccccccffcc77ccffcc77ccffffcccccccc99",
	"00ffccccffcc77ccffcc77ccffcc77cccccccc99",
	"00ffccccaa77ccffcc77ccffcc77cccccccccc99",
	"00ffccccccccffcc77ccffcc77cccccccccccc99",
	"00ffccccccccaa77ccffcc77cccccccccccccc99",
	"00ffccccccccccccffcc77cccccccccccccccc99",
	"00ffccccccccccccaa77cccccccccccccccccc99",
	"00ffcccccccccccccccccccccccccccccccccc99",
	"ffffcccccccccccccccccccccccccccccccccc99",
	"cccccccccccccccccccccccccccccccccccccc99",
	"cccccccccccccccccccccccccccccccccccccc99",
	"9999999999999999999999999999999999999999",
};
/* Rainure devant le menu d'application (6 colonnes × 20 rangées), Mac OS 9.2. */
static const char *const map_rainure[20] = {
	"ffffffffffdd",
	"ffdddddddd99",
	"ffdddddddd99",
	"ffdddddddd99",
	"ffdd88aadd99",
	"ffddbbffdd99",
	"ffdddddddd99",
	"ffdd88aadd99",
	"ffddbbffdd99",
	"ffdddddddd99",
	"ffdd88aadd99",
	"ffddbbffdd99",
	"ffdddddddd99",
	"ffdd88aadd99",
	"ffddbbffdd99",
	"ffdddddddd99",
	"ffdddddddd99",
	"ffdddddddd99",
	"999999999999",
	"000000000000",
};
/* coins arrondis de l'écran, sur la barre de menus : gris, ou -- pour le noir du bord */
static const char *const map_coin_g[6] = {
	"000000000055aadd",
	"00000055aaffffff",
	"000055ddffdddddd",
	"0055ddffdddddddd",
	"00aaffdddddddddd",
	"55ffdddddddddddd",
};
static const char *const map_coin_d[6] = {
	"ddaa552200000000",
	"ddccbb8855000000",
	"ddddddbb88550000",
	"ddddddddbb885500",
	"ddddddddddbb8822",
	"ddddddddddddaa55",
};
