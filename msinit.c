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

void mvlogfile() {
	int f = open(LOGFILE, O_RDONLY);
	if (!f) return;
	close(f);
	rename(LOGFILE, BACKLOGFILE);
}

void *runservice(void *arg) {
	int i;
	int stat_val;
	Service *s = (Service *) arg;

	message(s->name, " has a new thread!", NULL);

	/* If it is running no point in runnig it again. Else if it has been
	 * started and exits then no point running it again. And not much
	 * point starting it if I'm about to shut down. */
	if (state == SHUTDOWN || s->running || (s->exits && s->started))
			pthread_exit(NULL);

	s->started = s->running = 1;
	s->ready = 0;

	for (i = 0; i < s->nneed; i++) {
		while (!s->need[i]->ready) {
			if (s->need[i]->pid > 1) 
				waitpid(s->need[i]->pid, NULL, 0);
			else
				usleep(10000);
		}
	}

	if (!s->exec[0]) {
		message(s->name, " has no exec feild! setting ready.", NULL);
		s->ready = 1;
		pthread_exit(NULL);
	}

	s->pid = fork();
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
			if (s->exits) {
				s->ready = 1;
			} else {
				s->ready = 0;
				message("ERROR: ", s->name, " EXITED!", NULL);
			}
			break;
		} else if (WIFSIGNALED(stat_val)) {
			s->ready = 0;
			message(s->name, " WAS KILLED BY A SIGNAL!", NULL);
			break;
		} else /* Dont go into infite loops listening to nothing & wasting cpu */
			sleep(1);
	}

	s->running = 0;
	if (s->restart)
		runservice((void *) s);

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
	s->next = NULL;
	s->running = s->ready = s->started = 0;
	s->pid = -1;
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

void fulloutservice(Service *s, FILE *file) {
	char buf[256], *line, *var, *val;
	int afters = 0;
	int i;
	char *c, *l, o;

	s->exec[0] = NULL;
	s->exits = 1;
	s->restart = 0;
	s->nneed = 0;
	
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

	for (; s && s->next; s = s->next);

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			/* Create a new service and add it to the list. */
			if (findservice(dir->d_name)) {
				message("already have a service called ", dir->d_name, 
						" not adding another.", NULL);
				continue;
			}

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

	if (!services) {
		services = makeservice();
		sprintf(services->name, "basic-boot");
		services->ready = 1;
	}

	evaldir(NULL, services);

	/* Flesh out the services. */
	for (s = services->next; s; s = s->next) {	
		sprintf(fullname, "%s/%s", SERVICEDIR, s->name);
		f = fopen(fullname, "r");
		if (f) {
			fulloutservice(s, f);
			fclose(f);
		} else {
			message("ERROR opening file ", fullname, NULL);
			return 1;
		}
	}

	return 0;
}

void shutdown(int r) {
	state = SHUTDOWN;
	message("\n\nCOMING DOWN.\n", NULL);

	spawn("/sbin/hwclock", "--systohc", NULL);

	message("sending all processes the TERM signal...", NULL);
	kill(-1, SIGTERM);
	sleep(5);
	message("sending all processes the KILL signal...", NULL);
	kill(-1, SIGKILL);
	sleep(3);

	spawn("/sbin/swapoff", "-a", NULL);
	message("unmounting everything.", NULL);
	spawn("/bin/mount", "-o", "remount,rw", "/", NULL);
	sleep(1);
	spawn("/bin/umount", "-a", "-r", NULL);
	
	message("waiting for everything to finish.", NULL);
	sleep(3);

	if (r) 
		reboot(RB_AUTOBOOT);
	else
		reboot(RB_POWER_OFF);

	exit(0);
}

void basicboot() {
	pid_t p;
	int stat_val;

	if (mount("none", "/proc", "proc", 0, "") != 0) {
		message("ERROR mounting /proc", NULL);
		exit(1);
	}

	if (mount("none", "/sys", "sysfs", 0, "") != 0) {
		message("ERROR mounting /sys", NULL);
		exit(1);
	}

	spawn("/sbin/hwclock", "--hctosys", NULL);

	// For debuging purposes.
	spawn("/sbin/agetty", "tty2", "linux", "--noclear", NULL);

	message("remounting / rw", NULL);
	p = spawn("/bin/mount", "-o", "remount,rw", "/", NULL);

	while (1) {
		waitpid(p, &stat_val, 0);
		if (WIFEXITED(stat_val)) break;
	}
}

void sigint(int num) {
	shutdown(0);
}

void sigquit(int num) {
	shutdown(1);
}

int main(int argc, char **argv) {
	Service *s;

	message("starting...\n", NULL);

	signal(SIGINT, sigint);
	signal(SIGQUIT, sigquit);

	basicboot();
	mvlogfile();

	if (fork() == 0) {
		message("going to eval service files...", NULL);
		if (evalfiles()) {
			message("\n\nERROR EVALUATING SERVICE FILES!\n", NULL);
			spawn("/sbin/agetty", "tty1", "linux", "--noclear", NULL);
		} else {
			message("...evaluated. starting all services...", NULL);

			for (s = services->next; s; s = s->next) {
				pthread_t pth;
				if (pthread_create(&pth, NULL, runservice, (void *) s))
					message("ERROR starting thread for service ", s->name, NULL);
			}
		}
	}
	
	while (1) 
		sleep(1000);

	return 0;
}
