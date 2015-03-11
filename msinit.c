#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>

#include "msinit.h"

Service *services;

void runservice(Service *s) {
	int a;
	char *exec[10];
	pid_t pid;
	Service *sa;

	printf("msinit: possibly running service %s\n", s->name);

	if (isservicerunning(s)) {
		fprintf(stderr, "msinit: %s is already started, not running.\n", s->name);
		return;
	}

	for (a = 0; s->after[a][0]; a++) {
		sa = findservice(s->after[a]);
		if (sa)
			runservice(sa);
		else
			fprintf(stderr, "msinit: ERROR no service named '%s'\n", s->after[a]);

	}

	if ((pid = fork()) == 0) {
		printf("msinit: starting %s after sleep\n", s->name);
		sleep(2);
		printf("msinit: starting %s now\n", s->name);
//		sleep(20);
		execvp(s->exec[0], s->exec);
		fprintf(stderr, "msinit: ERROR occured while running service %s that"
				"caused it to terminate.\n", s->name);
		removepidfile(s);
	} else {
		printf("msinit: writing pid file\n");
		writepidfile(s, pid);
	}
}

void writepidfile(Service *s, pid_t pid) {
	FILE *f;
	char n[256];
	f = fopen(n, "w");
	sprintf(n, "/var/run/msinit/%s", s->name);
	if (f) {
		fprintf(f, "%i", pid);
		fclose(f);
	} else {
		fprintf(stderr, "msinit: ERROR opening pid file %s.\n", n);
	}
}

void removepidfile(Service *s) {
	char n[256];
	sprintf(n, "/var/run/msinit/%s", s->name);
	remove(n);
}

int isservicerunning(Service *s) {
	int f;
	char n[256];
	sprintf(n, "/var/run/msinit/%s", s->name);
	f = open(n, O_RDONLY);
	if (f < 0)
		return 0;
	close(f);
	return 1;
}

Service *findservice(char *name) {
	Service *s;
	for (s = services; s; s = s->next) 
		if (strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

int spawn(char *prog, ...) {
	char *argv[10];
	va_list ap;
	int i;
	pid_t pid;

	pid = fork();
	if (pid > 0) return pid;

	argv[0] = prog;
	va_start(ap, prog);
	for (i = 1; i < 9 && (argv[i] = va_arg(ap, char *)) != NULL; i++);
	argv[i] = NULL;

	execvp(argv[0], argv);
	exit(1);

	return 0;
}

void evalfile(char *name, FILE *file) {
	char *line, *linesave, *var, *val;
	int afters = 0;
	int i;

	Service *s;
	Service *new = malloc(sizeof(Service));
	new->name = name;
	new->after[0][0] = '\0';
	new->next = NULL;
	new->exec[0] = NULL;

	while (1) {
		line = linesave = malloc(sizeof(char) * 256);
		if (fgets(line, sizeof(char) * 256, file) == NULL) {
			free(line);
			break;
		}

		for (; *line == ' ' || *line == '\t'; line++);

		if (*line == '#' || *line == '\n') {
			free(linesave);
			continue;
		}

		printf("got line: %s", line);

		var = strsep(&line, "=");
		val = strsep(&line, "\n");

		if (strcmp(var, "exec") == 0) {
			printf("msinit: breaking into exec array\n");
			int c, l;
			i = c = l = 0;
			do {
				// TODO: Add handing of string args...
				
				if (val[c] == ' ' || !val[c]) {
					printf("adding chars from %i to %i\n", l, c);
					new->exec[i] = malloc(sizeof(char) * (c - l + 1));
					strncpy(new->exec[i], val + l, c - l);
					new->exec[i][c - l] = '\0';
					printf("added '%s'\n", new->exec[i]);
					new->exec[++i] = NULL;
					l = c + 1;
				}
			} while (val[c++] && i < 9);

		} else if (strcmp(var, "after") == 0 && afters < 9) {
			printf("msinit: adding '%s' to afters array\n", val);
			i = strlen(val);
			strncpy(new->after[afters], val, i);
			new->after[afters][i] = '\0';
			new->after[++afters][0] = '\0';
		}
		
		free(linesave);
	}

	printf("msinit: adding to service list\n");
	for (s = services; s && s->next; s = s->next);
	if (s) 
		s->next = new;
	else
		services = new;
}

int evalfiles() {
	DIR *d;
	struct dirent *dir;
	FILE *f;
	char *name, fullname[1024];

	d = opendir("/etc/msinit.d");
	if (!d) return 1;
	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			name = malloc(sizeof(char) * 256);
			strcpy(name, dir->d_name);
			sprintf(fullname, "/etc/msinit.d/%s", name);
			f = fopen(fullname, "r");
			if (f) {
				printf("msinit: evaluating file %s\n", dir->d_name);
				evalfile(name, f);
				fclose(f);
			} else {
				fprintf(stderr, "msinit: ERROR opening file %s\n", dir->d_name);
				return 1;
			}
		}
	}
	return 0;
}

void shutdown() {
	wait(spawn("/sbin/hwclock", "--systohc", NULL));

	printf("msinit: sending all processes the TERM signal...\n");
	kill(-1, SIGTERM);
	sleep(3);
	printf("msinit: sending all processes the KILL signal...\n");
	kill(-1, SIGKILL);

	printf("msinit: unmounting everything.\n");
	wait(spawn("/sbin/swapoff", "-a", NULL));
	wait(spawn("/bin/umount", "-a", NULL));
	printf("msinit: waiting for everything to finish.\n");

	wait(spawn("/bin/mount", "-o", "remount,rw", "/", NULL));

	reboot(RB_POWER_OFF);
	exit(0);
}

void basicboot() {
	if (mount("none", "/proc", "proc", 0, "") != 0) {
		printf("msinit: ERROR mounting /proc\n");
		exit(1);
	}

	if (mount("none", "/sys", "sysfs", 0, "") != 0) {
		printf("msinit: ERROR mounting /sys\n");
		exit(1);
	}

	wait(spawn("/sbin/hwclock", "--hctosys", NULL));

	printf("msinit: remount rw /\n");
	wait(spawn("/bin/mount", "-o", "remount,rw", "/", NULL));
}

void sigint(int num) {
	printf("msinit: got sigint.\n");
	shutdown();
}

void fallback() {
	fprintf(stderr, "msinit: Falling back to agetty.\n");
	spawn("/sbin/agetty", "tty1", "linux", "--noclear", NULL);
}

int main(int argc, char **argv) {
	Service *s;
	printf("msinit: starting...\n");

	signal(SIGINT, sigint);

	basicboot();

	// For debuging purposes.
	spawn("/sbin/agetty", "tty3", "linux", "--noclear", NULL);
	
	if (evalfiles() == 0) {
		wait(spawn("/bin/rm", "/var/run/msinit/*", NULL));
		for (s = services; s; s = s->next)
			runservice(s);
	} else {
		fprintf(stderr, "msinit: ERROR evaluating files in /etc/msinit.d\n");
		fallback();
	}

	while (1) {
		printf("msinit: waiting for nothing in particular.\n");
		wait(-1);
	}

	return 0;
}
