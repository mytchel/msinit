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

#include "msinit.h"

Service *services;
int state;

void message(char *s, ...) {
	char *b = NULL;
	FILE *logout;
	va_list ap;

	logout = fopen(LOGFILE, "a+");
	if (logout) {
		fprintf(logout, "msinit: %s", s);
		va_start(ap, s);
		while ((b = va_arg(ap, char *)) != NULL) {
			fprintf(logout, "%s", b);
		}
		va_end(ap);
		fprintf(logout, "\n");
		fclose(logout);
	}

	fprintf(stdout, "msinit: %s", s);
	va_start(ap, s);
	while ((b = va_arg(ap, char *)) != NULL) {
		printf("%s", b);
	}
	va_end(ap);

	fprintf(stdout, "\n");
}

void *runservice(void *arg) {
	int i;
	int stat_val;
	Service *s = (Service *) arg;
	
	if (s->started) pthread_exit(NULL);

	for (i = 0; i < s->nneed; i++) {
		while (!s->need[i]->ready) {
			if (s->need[i]->pid > 1) 
				waitpid(s->need[i]->pid, NULL, 0);
			else
				usleep(10000);
		}
		if (s->started) pthread_exit(NULL);
	}

	if (state == SHUTDOWN || s->started) pthread_exit(NULL);
	else s->started = 1;

	s->pid = fork();
	if (s->pid == -1) {
		message("ERROR forking for ", s->name, NULL);
		pthread_exit(NULL);
	}

	if (s->pid == 0) {
		message("fork: ", s->name, NULL);
		execvp(s->exec[0], s->exec);
		message("ERROR execvp for ", s->name, " has exited!", NULL);
		exit(0);
	}	

	if (!s->exits) s->ready = 1;

	while (1) {
		waitpid(s->pid, &stat_val, 0);
		
		if (WIFEXITED(stat_val)) {
			s->started = 0;
			if (s->exits) {
				s->ready = 1;
			} else {
				s->ready = 0;
				message(s->name, "FAILED!", NULL);
			}

			if (s->restart)
				runservice((void *) s);

			break;
		}
		sleep(1);
	}
	
	pthread_exit(NULL);
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
	s->pid = -1;
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
	for (i = 1; i < EXECMAX && (argv[i] = va_arg(ap, char *)) != NULL; i++);
	argv[i] = NULL;
	va_end(ap);

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
			} while (*(c++) && i < EXECMAX - 1);

		} else if (strcmp(var, "need") == 0 && s->nneed < NEEDMAX) {
			Service *n = findservice(val);
			if (n)
				s->need[s->nneed++] = n;
			else
				message("ERROR, ", s->name, " has need ", val, 
						" that could not be found!", NULL);
		} else if (strcmp(var, "exits") == 0) {
			s->exits = *val == 'y';
		} else if (strcmp(var, "restart") == 0) {
			s->restart = *val == 'y';
		} else {
			message("ERROR, unknown thing in config line: ", buf, NULL);
		}
	}
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
		message("ERROR opening service dir ", name, NULL);
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
			message("ERROR opening file ", fullname, NULL);
			return 1;
		}
	}

	return 0;
}

void shutdown() {
	state = SHUTDOWN;
	message("\n\nCOMING DOWN.\n", NULL);

	wait(spawn("/sbin/hwclock", "--systohc", NULL));

	message("sending all processes the TERM signal...", NULL);
	kill(-1, SIGTERM);
	sleep(3);
	message("sending all processes the KILL signal...", NULL);
	kill(-1, SIGKILL);

	message("unmounting everything.", NULL);
	wait(spawn("/sbin/swapoff", "-a", NULL));
	wait(spawn("/bin/umount", "-a", "-d", "-r", "-t", 
				"nosysfs,noproc,nodevtmpfs", NULL));
	wait(spawn("/bin/umount", "-a", "-r", NULL));
	message("waiting for everything to finish.", NULL);

	wait(spawn("/bin/mount", "-o", "remount,rw", "/", NULL));

	reboot(RB_POWER_OFF);
	exit(0);
}

void basicboot() {
	if (mount("none", "/proc", "proc", 0, "") != 0) {
		message("ERROR mounting /proc", NULL);
		exit(1);
	}

	if (mount("none", "/sys", "sysfs", 0, "") != 0) {
		message("ERROR mounting /sys", NULL);
		exit(1);
	}

	wait(spawn("/sbin/hwclock", "--hctosys", NULL));

	message("remounting / rw", NULL);
	wait(spawn("/bin/mount", "-o", "remount,rw", "/", NULL));
}

void sigint(int num) {
	shutdown();
}

void fallback() {
	message("FALLING BACK TO GETTY", NULL);
	spawn("/sbin/agetty", "tty1", "linux", "--noclear", NULL);
}

int main(int argc, char **argv) {
	Service *s;

	message("starting...\n", NULL);

	signal(SIGINT, sigint);

	basicboot();

	// For debuging purposes.
	spawn("/sbin/agetty", "tty2", "linux", "--noclear", NULL);
	
	if (evalfiles() == 0) {
		message("starting services\n", NULL);
		for (s = services->next; s; s = s->next) {
			pthread_t pth;
			if (pthread_create(&pth, NULL, runservice, (void *) s))
				message("ERROR starting thread for service ", s->name, NULL);
		}
	} else {
		message("ERROR evaluating files in /etc/msinit.d", NULL);
		fallback();
	}

	message("basic boot complete and services should be starting.\n", NULL);
	while (1) {
		wait(-1);
		sleep(1);
	}

	return 0;
}
