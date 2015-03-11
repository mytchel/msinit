#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>

#define SERVICEDIR "/etc/msinit.d"
#define PIDDIR "/var/run/msinit"

#include "msinit.h"

Service *services;

void *runservice(void *arg) {
	int i;
	pid_t pid;
	int stat_val;
	Service *s = (Service *) arg;

	if (s->started) return;

	printf("msinit: trying to start %s\n", s->name);

	for (i = 0; i < s->nneed; i++) {
		while (!s->need[i]->ready) {
			fprintf(stderr, "%s will have to wait for %s\n", s->name, 
					s->need[i]->name);
			sleep(1);
		}
		if (s->started) return;
	}

	printf("msinit: %s is ready to start!!\n", s->name);

	if (s->started) return;
	else s->started = 1;

	pid = fork();
	if (pid == 0) {
		printf("fork %s: ", s->name);
		for (i = 0; s->exec[i]; i++)
			printf("%s ", s->exec[i]);
		printf("\n");
		execvp(s->exec[0], s->exec);
		fprintf(stderr, "msinit: ERROR execvp for %s has exited!\n", s->name);
		exit(0);
	}

	if (!s->exits) s->ready = 1;

	while (1) {
		waitpid(pid, &stat_val, 0);
		
		if (WIFEXITED(stat_val)) {
			s->started = 0;
			if (s->exits) {
				s->ready = 1;
			} else {
				s->ready = 0;
				fprintf(stderr, "%s: FAILED!\n", s->name);
			}

			if (s->restart) {
				runservice(s);
			}

			break;
		}
		
		sleep(1);
	}
}

Service *findservice(char *name) {
	Service *s;
	for (s = services; s; s = s->next) 
		if (strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

Service *makeservice() {
	Service *s = malloc(sizeof(Service));
	s->name = malloc(sizeof(char) * 256);
	s->exec[0] = NULL;
	s->exits = 1;
	s->restart = 0;
	s->started = s->ready = 0;
	s->nneed = 0;
	s->next = NULL;
	return s;
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

void fleshservice(Service *s, FILE *file) {
	char buf[256], *line, *var, *val;
	int afters = 0;
	int i;
	char *c, *l, o;
	
	while (fgets(buf, sizeof(buf), file) != NULL) {
		for (line = &buf[0]; *line == ' ' || *line == '\t'; line++);

		if (*line == '#' || *line == '\n')
			continue;

		var = strsep(&line, "=");
		val = strsep(&line, "\n");

		if (strcmp(var, "exec") == 0) {
			i = 0;
			c = l = val;
			do {
				if (*c == '"' || *c == '\'') {
					o = *c;
					for (c++; c && *c != o; c++);
				}

				if (*c == '\\') c += 2;
				
				if (*c == ' ' || *c == '\t' || !*c) {
					s->exec[i] = malloc(sizeof(char) * (c - l + 1));
					strncpy(s->exec[i], l, c - l);
					s->exec[i][c - l] = '\0';
					s->exec[++i] = NULL;

					for (l = c; *l == ' ' || *l == '\t' || !l; l++);
					c = l;
				}
			} while (*(c++) && i < 9);

		} else if (strcmp(var, "need") == 0) {
			s->need = realloc(s->need, sizeof(Service*) * (s->nneed + 1));
			s->need[s->nneed++] = findservice(val);
		} else if (strcmp(var, "exits") == 0) {
			s->exits = *val == 'y';
		} else if (strcmp(var, "restart") == 0) {
			s->restart = *val == 'y';
		} else {
			fprintf(stderr, "msinit: ERROR unknown thing in config line: %s", 
					buf);
		}
	}

	printf("msinit: service %s should be good to go\n", s->name);
}

void evaldir(char *name, Service *s) {
	DIR *d;
	struct dirent *dir;
	char fullname[256];

	if (name)
		sprintf(fullname, "%s/%s", SERVICEDIR, name);
	else
		sprintf(fullname, SERVICEDIR);
	
	d = opendir(fullname);

	if (!d) {
		fprintf(stderr, "msinit: ERROR opening service dir %s\n", name);
		return;
	}

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			/* Create a new service and add it to the list. */
			s->next = makeservice();
			s = s->next;
			if (name)
				sprintf(s->name, "%s/%s", name, dir->d_name);
			else
				sprintf(s->name, "%s", dir->d_name);
		} else if (dir->d_type == DT_DIR && dir->d_name[0] != '.') {
			char subname[1024];
			if (name)
				sprintf(subname, "%s/%s", name, dir->d_name);
			else
				sprintf(subname, "%s", dir->d_name);
			evaldir(subname, s);
		}
	}

	closedir(d);
}

int evalfiles() {
	FILE *f;
	Service *s;
	char fullname[256];

	services = makeservice();
	sprintf(services->name, "basic-boot");
	services->ready = 1;

	evaldir(NULL, services);

	/* Flesh out the services. */
	for (s = services->next; s; s = s->next) {	
		sprintf(fullname, "%s/%s", SERVICEDIR, s->name);
		f = fopen(fullname, "r");
		if (f) {
			fleshservice(s, f);
			fclose(f);
		} else {
			fprintf(stderr, "msinit: ERROR opening file %s\n", fullname);
			return 1;
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
		for (s = services->next; s; s = s->next) {
			pthread_t pth;
			pthread_create(&pth, NULL, runservice, (void *) s);
		}
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
