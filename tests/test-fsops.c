/* Tests sans X11 des opérations de fichiers du Finder. */
#define _POSIX_C_SOURCE 200809L

#include "../src/apps/finder/fsops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "ÉCHEC ligne %d: %s (errno=%d: %s)\n", \
		    __LINE__, #x, errno, strerror(errno)); \
		return 1; \
	} \
} while (0)

static int
write_file(const char *path, const char *text, mode_t mode)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
	size_t len = strlen(text), off = 0;

	if (fd < 0)
		return -1;
	while (off < len) {
		ssize_t n = write(fd, text + off, len - off);

		if (n <= 0) {
			close(fd);
			return -1;
		}
		off += (size_t)n;
	}
	return close(fd);
}

static int
read_file(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, size - 1);
	if (n < 0) {
		close(fd);
		return -1;
	}
	buf[n] = '\0';
	return close(fd);
}

static int
remove_tree(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *de;
	int rc = 0;

	if (lstat(path, &st) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((de = readdir(dir)) != NULL) {
		char child[PATH_MAX];

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, de->d_name) >=
		    (int)sizeof(child) || remove_tree(child) != 0)
			rc = -1;
	}
	closedir(dir);
	if (rmdir(path) != 0)
		rc = -1;
	return rc;
}

static int
prepare_tree(const char *root)
{
	char dir[PATH_MAX], file[PATH_MAX], link[PATH_MAX];

	if (snprintf(dir, sizeof(dir), "%s/sous-dossier", root) >=
	    (int)sizeof(dir))
		return -1;
	if (mkdir(dir, 0750) != 0)
		return -1;
	if (snprintf(file, sizeof(file), "%s/document.txt", dir) >=
	    (int)sizeof(file))
		return -1;
	if (write_file(file, "contenu de référence\n", 0640) != 0)
		return -1;
	if (snprintf(link, sizeof(link), "%s/lien", root) >= (int)sizeof(link))
		return -1;
	return symlink("sous-dossier/document.txt", link);
}

static int
check_tree(const char *root)
{
	char file[PATH_MAX], link[PATH_MAX], target[128], buf[128];
	struct stat st;
	ssize_t n;

	if (snprintf(file, sizeof(file), "%s/sous-dossier/document.txt", root) >=
	    (int)sizeof(file))
		return -1;
	if (read_file(file, buf, sizeof(buf)) != 0 ||
	    strcmp(buf, "contenu de référence\n") != 0)
		return -1;
	if (stat(file, &st) != 0 || (st.st_mode & 0777) != 0640)
		return -1;
	if (snprintf(link, sizeof(link), "%s/lien", root) >= (int)sizeof(link))
		return -1;
	if (lstat(link, &st) != 0 || !S_ISLNK(st.st_mode))
		return -1;
	n = readlink(link, target, sizeof(target) - 1);
	if (n < 0)
		return -1;
	target[n] = '\0';
	return strcmp(target, "sous-dossier/document.txt") == 0 ? 0 : -1;
}

int
main(void)
{
	char tmp[] = "/tmp/opencode/platine-fsops.XXXXXX";
	char src[PATH_MAX], copy[PATH_MAX], moved[PATH_MAX], file[PATH_MAX];
	char shm_template[] = "/dev/shm/platine-fsops.XXXXXX";
	char shm_dst[PATH_MAX], buf[64];
	struct stat a, b;
	int cross_device = 0;

	CHECK(mkdtemp(tmp) != NULL);
	CHECK(snprintf(src, sizeof(src), "%s/source", tmp) < (int)sizeof(src));
	CHECK(snprintf(copy, sizeof(copy), "%s/copie", tmp) < (int)sizeof(copy));
	CHECK(snprintf(moved, sizeof(moved), "%s/deplace", tmp) <
	    (int)sizeof(moved));
	CHECK(mkdir(src, 0755) == 0);
	CHECK(prepare_tree(src) == 0);

	/* Copie récursive et liens copiés comme liens. */
	CHECK(fsops_copy_tree(src, copy) == 0);
	CHECK(check_tree(copy) == 0);
	CHECK(check_tree(src) == 0);

	/* Aucun écrasement d'une destination existante. */
	CHECK(snprintf(file, sizeof(file), "%s/existant", tmp) <
	    (int)sizeof(file));
	CHECK(write_file(file, "sentinelle\n", 0600) == 0);
	errno = 0;
	CHECK(fsops_copy_tree(src, file) == -1 && errno == EEXIST);
	CHECK(read_file(file, buf, sizeof(buf)) == 0);
	CHECK(strcmp(buf, "sentinelle\n") == 0);

	/* Les fichiers spéciaux sont refusés sans laisser de destination. */
	CHECK(snprintf(file, sizeof(file), "%s/fifo", tmp) < (int)sizeof(file));
	CHECK(mkfifo(file, 0600) == 0);
	CHECK(snprintf(moved, sizeof(moved), "%s/fifo-copie", tmp) <
	    (int)sizeof(moved));
	errno = 0;
	CHECK(fsops_copy_tree(file, moved) == -1 && errno == EPERM);
	CHECK(lstat(moved, &a) == -1 && errno == ENOENT);
	CHECK(unlink(file) == 0);

	/* Déplacement ordinaire. */
	CHECK(snprintf(moved, sizeof(moved), "%s/deplace", tmp) <
	    (int)sizeof(moved));
	CHECK(fsops_move_tree(copy, moved) == 0);
	CHECK(lstat(copy, &a) == -1 && errno == ENOENT);
	CHECK(check_tree(moved) == 0);

	/* Repli EXDEV réel lorsque /dev/shm est un autre système de fichiers. */
	if (stat(tmp, &a) == 0 && stat("/dev/shm", &b) == 0 &&
	    a.st_dev != b.st_dev && mkdtemp(shm_template) != NULL) {
		cross_device = 1;
		CHECK(snprintf(shm_dst, sizeof(shm_dst), "%s/arbre", shm_template) <
		    (int)sizeof(shm_dst));
		CHECK(fsops_move_tree(moved, shm_dst) == 0);
		CHECK(lstat(moved, &a) == -1 && errno == ENOENT);
		CHECK(check_tree(shm_dst) == 0);
		CHECK(remove_tree(shm_template) == 0);
	} else {
		CHECK(remove_tree(moved) == 0);
	}

	CHECK(remove_tree(src) == 0);
	CHECK(remove_tree(tmp) == 0);
	printf("fsops : OK%s\n", cross_device ? " (EXDEV inclus)" :
	    " (EXDEV non disponible, ignoré)");
	return 0;
}
