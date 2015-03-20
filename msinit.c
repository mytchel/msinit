#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/types.h>

#include "msinit.h"

Service *services;

void stopservice(Service *s) {
	s->pid = 0;
	s->running = 0;
	pthread_exit(NULL);
}

void *runservice(void *arg) {
	int i, con;
	int stat_val;
	Service *s = (Service *) arg;
	char fullname[256];
	FILE *f;

	if (!services->running || s->running || (s->exits && s->started))
		stopservice(s);

	s->started = s->running = 1;
	s->ready = 0;

	for (i = 0; i < s->nneed; i++) {
		while (!s->need[i]->ready) {
			if (s->need[i]->pid > 1) 
				waitpid(s->need[i]->pid, NULL, 0);
			else
				usleep(1000);
		}
	}

	if (!s->exec[0]) {
		s->ready = 1;
		stopservice(s);
	}

	s->pid = fork();
	if (s->pid == 0) {
		printf("msinit: %s\n", s->name);
		setsid();
		execvp(s->exec[0], s->exec);
		fprintf(stderr, "msinit: execvp for has exited!\n", s->name);
		exit(0);
	}	

	if (!s->exits) {
		s->ready = 1;
	}

	while (s->running) {
		waitpid(s->pid, &stat_val, 0);
		
		if (WIFEXITED(stat_val)) {
			if (s->exits) 
				s->ready = 1;
			else
				s->ready = 0;

			s->running = 0;
			break;
		} else if (WIFSIGNALED(stat_val)) {
			s->ready = 0;
			s->running = 0;
			break;
		} else
			usleep(1000);
	}

	s->pid = 0;
	if (s->restart) {
		printf("msinit: restarting %s\n", s->name);
		if (!updateservice(s)) {
			sleep(1); /* Don't go into uninteractable loops of restarting. */
			runservice((void *) s);
		}
	}

	stopservice(s);
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
	s->next = NULL;
	s->running = s->ready = s->started = 0;
	s->pid = 0;
	return s;
}

int updateservice(Service *s) {
	FILE *file;
	char path[256];
	char buf[256], *line, *var, *val;
	int afters = 0;
	int i;
	char *c, *l, o;

	s->exec[0] = NULL;
	s->exits = 1;
	s->restart = 0;
	s->nneed = 0;

	sprintf(path, "%s/%s", SERVICEDIR, s->name);
	file = fopen(path, "r");
	if (!file)
		return 1;

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

		} else if (strcmp(var, "need") == 0) {
			if (s->nneed >= NEEDMAX) {
				fprintf(stderr, 
						"%s service file has exceded max number of needs.", 
						s->name);
				continue;
			}
			Service *n = findservice(val);
			if (n)
				s->need[s->nneed++] = n;
			else
				fprintf(stderr,
						"msinit: ERROR, %s has need %s that could not be found.\n", 
						s->name, val);
		} else if (strcmp(var, "exits") == 0) {
			s->exits = *val == 'y';
		} else if (strcmp(var, "restart") == 0) {
			s->restart = *val == 'y';
		} else {
			fprintf(stderr, "msinit: ERROR, unknown thing service file %s\n", 
					s->name);
		}
	}
	fclose(file);
}

void evaldir(char *dirname, Service *s) {
	DIR *d;
	struct dirent *dir;
	char name[256];

	sprintf(name, "%s/%s", SERVICEDIR, dirname);
	d = opendir(name);

	if (!d) {
		fprintf(stderr, "msinit: ERROR opening service dir %s\n", name);
		return;
	}

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			sprintf(name, "%s%s", dirname, dir->d_name);
			s->next = makeservice();
			s = s->next;
			strcpy(s->name, name);
		} else if (dir->d_type == DT_DIR && dir->d_name[0] != '.') {
			sprintf(name, "%s%s/", dirname, dir->d_name);
			evaldir(name, s);
			for (; s && s->next; s = s->next);
		}
	}

	closedir(d);
}

void evalfiles() {
	Service *s;

	if (!services) {
		services = makeservice();
		services->running = 1;
	}

	evaldir("", services);

	for (s = services->next; s; s = s->next) 
		updateservice(s);
}

int spawn(char *prog, ...) {
	char *argv[10];
	va_list ap;
	int i;
	pid_t pid;

	if ((pid = fork()) == 0) {
		argv[0] = prog;
		va_start(ap, prog);
		for (i = 1; i < EXECMAX && (argv[i] = va_arg(ap, char *)) != NULL; i++);
		argv[i] = NULL;
		va_end(ap);

		setsid();
		execvp(argv[0], argv);
		exit(1);

		return 0;
	} else {
		return pid;
	}
}

void shutdown() {
	if (services)
		services->running = 0;

	printf("\nCOMING DOWN.\n");

	sync();

	printf("sending all processes TERM signal...\n");
	kill(-1, SIGTERM);
	sleep(5);
	printf("sending all processes KILL signal...\n");
	kill(-1, SIGKILL);
	sleep(3);

	spawn("/sbin/hwclock", "--systohc", NULL);
	
	sync();

	printf("swapoff\n");
	spawn("/sbin/swapoff", "-a", NULL);
	sleep(1);
	printf("/bin/umount -a -r\n");
	spawn("/bin/umount", "-a", "-r", NULL);
	sleep(2);
	printf("/bin/mount -o remount,ro /\n");
	spawn("/bin/mount", "-o", "remount,ro", "/", NULL);

	sync();

	sleep(3);
}

void basicboot() {
	pid_t p;
	int stat_val;

	mount("none", "/proc", "proc", 0, "");
	mount("none", "/sys", "sysfs", 0, "");
	p = spawn("/bin/mount", "-o", "remount,rw", "/", NULL);
	do {
		waitpid(p, &stat_val, 0);
	} while (!WIFEXITED(stat_val));
}

void inthandler(int sig) {
	shutdown();
	reboot(RB_POWER_OFF);
	exit(0);
}

void quithandler(int sig) {
	shutdown();
	reboot(RB_AUTOBOOT);
	exit(0);
}

void chldhandler(int sig) {
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char **argv) {
	Service *s;
	int f;
	/* Ignore all signals */
	for (f = 1; f <= NSIG; f++)
		signal(f, SIG_IGN);

	signal(SIGINT, inthandler);
	signal(SIGQUIT, quithandler);
	signal(SIGCHLD, chldhandler);

	basicboot();

	if (fork() == 0) {
		evalfiles();
		for (s = services->next; s; s = s->next) 
			pthread_create(&s->thread, NULL, runservice, (void *) s);
	}

	while (1) 
		sleep(1000);

	return 0;
}
