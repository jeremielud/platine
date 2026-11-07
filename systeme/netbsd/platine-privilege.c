/*
 * platine-privilege : donne root à un seul script, /opt/platine/libexec/privilege, pour le groupe
 * wheel (setuid root, mode 4750 root:wheel). Environnement vidé. NetBSD n'a ni doas ni sudo en base.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	char tz[160];
	char *env[3];
	const char *t = getenv("TZ");
	int n = 0;

	env[n++] = "PATH=/sbin:/usr/sbin:/bin:/usr/bin";
	if (t != NULL && *t != '\0') {
		snprintf(tz, sizeof(tz), "TZ=%s", t);
		env[n++] = tz;
	}
	env[n] = NULL;
	(void)argc;
	if (setgid(0) != 0 || setuid(0) != 0) {
		perror("platine-privilege");
		return 1;
	}
	argv[0] = "/opt/platine/libexec/privilege";
	execve("/bin/sh", (char *[]){ "sh", argv[0], argv[1], argc > 2 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL, NULL }, env);
	perror("platine-privilege");
	return 1;
}
