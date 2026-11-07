/*
 * Copie et déplacement récursifs sans suivre les liens symboliques.
 */
#include "fsops.h"

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

static int
join_path(char *dst, size_t dstsz, const char *dir, const char *name)
{
	const char *slash;
	size_t len;
	int n;

	len = strlen(dir);
	slash = len > 0 && dir[len - 1] == '/' ? "" : "/";
	n = snprintf(dst, dstsz, "%s%s%s", dir, slash, name);
	if (n < 0 || (size_t)n >= dstsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

static int
copy_regular(const char *src, const char *dst, const struct stat *st,
    int *created)
{
	char buf[16384];
	struct stat current;
	struct timespec times[2];
	ssize_t nr;
	int in, out, flags;
	int saved;

	flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	in = open(src, flags);
	if (in < 0)
		return -1;
	out = -1;
	if (fstat(in, &current) != 0)
		goto fail;
	if (!S_ISREG(current.st_mode)) {
		errno = EPERM;
		goto fail;
	}
	out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (out < 0)
		goto fail;
	if (created != NULL)
		*created = 1;
	for (;;) {
		ssize_t off;

		nr = read(in, buf, sizeof(buf));
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			goto fail;
		}
		if (nr == 0)
			break;
		off = 0;
		while (off < nr) {
			ssize_t nw = write(out, buf + off, (size_t)(nr - off));

			if (nw < 0) {
				if (errno == EINTR)
					continue;
				goto fail;
			}
			if (nw == 0) {
				errno = EIO;
				goto fail;
			}
			off += nw;
		}
	}
	if (fchmod(out, st->st_mode & 07777) != 0)
		goto fail;
	times[0] = st->st_atim;
	times[1] = st->st_mtim;
	if (futimens(out, times) != 0)
		goto fail;
	if (close(out) != 0) {
		out = -1;
		goto fail;
	}
	out = -1;
	if (close(in) != 0) {
		in = -1;
		goto fail;
	}
	return 0;

fail:
	saved = errno;
	if (out >= 0)
		close(out);
	if (in >= 0)
		close(in);
	errno = saved;
	return -1;
}

static int
copy_symlink(const char *src, const char *dst, const struct stat *st,
    int *created)
{
	struct timespec times[2];
	size_t size;
	char *target;
	ssize_t n;
	int saved;

	size = 256;
	for (;;) {
		target = malloc(size);
		if (target == NULL)
			return -1;
		n = readlink(src, target, size);
		if (n < 0) {
			saved = errno;
			free(target);
			errno = saved;
			return -1;
		}
		if ((size_t)n < size)
			break;
		free(target);
		if (size > (size_t)-1 / 2) {
			errno = ENAMETOOLONG;
			return -1;
		}
		size *= 2;
	}
	target[n] = '\0';
	if (symlink(target, dst) != 0) {
		saved = errno;
		free(target);
		errno = saved;
		return -1;
	}
	free(target);
	if (created != NULL)
		*created = 1;
	times[0] = st->st_atim;
	times[1] = st->st_mtim;
	return utimensat(AT_FDCWD, dst, times, AT_SYMLINK_NOFOLLOW);
}

static int copy_node(const char *src, const char *dst, int *created);

static int
copy_directory(const char *src, const char *dst, const struct stat *st,
    int *created)
{
	struct dirent *de;
	struct timespec times[2];
	DIR *dir;
	int saved;

	if (mkdir(dst, 0700) != 0)
		return -1;
	if (created != NULL)
		*created = 1;
	dir = opendir(src);
	if (dir == NULL)
		return -1;
	for (;;) {
		char child_src[PATH_MAX], child_dst[PATH_MAX];

		errno = 0;
		de = readdir(dir);
		if (de == NULL) {
			if (errno != 0)
				goto fail;
			break;
		}
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (join_path(child_src, sizeof(child_src), src, de->d_name) != 0 ||
		    join_path(child_dst, sizeof(child_dst), dst, de->d_name) != 0 ||
		    copy_node(child_src, child_dst, NULL) != 0)
			goto fail;
	}
	if (closedir(dir) != 0)
		return -1;
	if (chmod(dst, st->st_mode & 07777) != 0)
		return -1;
	times[0] = st->st_atim;
	times[1] = st->st_mtim;
	return utimensat(AT_FDCWD, dst, times, 0);

fail:
	saved = errno;
	closedir(dir);
	errno = saved;
	return -1;
}

static int
copy_node(const char *src, const char *dst, int *created)
{
	struct stat st;

	if (lstat(src, &st) != 0)
		return -1;
	if (S_ISREG(st.st_mode))
		return copy_regular(src, dst, &st, created);
	if (S_ISDIR(st.st_mode))
		return copy_directory(src, dst, &st, created);
	if (S_ISLNK(st.st_mode))
		return copy_symlink(src, dst, &st, created);
	errno = EPERM;
	return -1;
}

static int
remove_tree(const char *path)
{
	struct stat st;
	struct dirent *de;
	DIR *dir;
	int saved;

	if (lstat(path, &st) != 0)
		return -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	/* Les modes finaux peuvent interdire le nettoyage ou la suppression. */
	if ((st.st_mode & S_IRWXU) != S_IRWXU &&
	    chmod(path, (st.st_mode & 07777) | S_IRWXU) != 0)
		return -1;
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	for (;;) {
		char child[PATH_MAX];

		errno = 0;
		de = readdir(dir);
		if (de == NULL) {
			if (errno != 0)
				goto fail;
			break;
		}
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (join_path(child, sizeof(child), path, de->d_name) != 0 ||
		    remove_tree(child) != 0)
			goto fail;
	}
	if (closedir(dir) != 0)
		return -1;
	return rmdir(path);

fail:
	saved = errno;
	closedir(dir);
	errno = saved;
	return -1;
}

int
fsops_copy_tree(const char *src, const char *dst)
{
	int created;
	int saved;

	if (src == NULL || dst == NULL || src[0] == '\0' || dst[0] == '\0') {
		errno = EINVAL;
		return -1;
	}
	created = 0;
	if (copy_node(src, dst, &created) == 0)
		return 0;
	saved = errno;
	if (created)
		remove_tree(dst);
	errno = saved;
	return -1;
}

int
fsops_move_tree(const char *src, const char *dst)
{
	struct stat st;

	if (src == NULL || dst == NULL || src[0] == '\0' || dst[0] == '\0') {
		errno = EINVAL;
		return -1;
	}
	/* rename(2) peut écraser : refuser une destination déjà présente. */
	if (lstat(dst, &st) == 0) {
		errno = EEXIST;
		return -1;
	}
	if (errno != ENOENT)
		return -1;
	if (rename(src, dst) == 0)
		return 0;
	if (errno != EXDEV)
		return -1;
	if (fsops_copy_tree(src, dst) != 0)
		return -1;
	return remove_tree(src);
}
