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
#include <syslog.h>

#include "msinit.h"

Service *services;

void *runservice(void *arg) {
	int i;
	int stat_val;
	Service *s = (Service *) arg;

	if (!services->running || s->running || (s->exits && s->started))
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
		s->ready = 1;
		pthread_exit(NULL);
	}

	s->pid = fork();
	if (s->pid == 0) {
		syslog(LOG_NOTICE, "starting %s\n", s->name);
		execvp(s->exec[0], s->exec);
		syslog(LOG_ALERT, "ERROR execvp for has exited!\n", s->name);
		exit(0);
	}	

	if (!s->exits) {
		s->ready = 1;
		syslog(LOG_NOTICE, "%s ready.\n", s->name);
	}

	while (1) {
		waitpid(s->pid, &stat_val, 0);
		
		if (WIFEXITED(stat_val)) {
			if (s->exits) {
				s->ready = 1;
				syslog(LOG_NOTICE, "%s ready\n", s->name);
			} else {
				s->ready = 0;
				syslog(LOG_WARNING, "ERROR: %s EXITED!\n", s->name);
			}
			break;
		} else if (WIFSIGNALED(stat_val)) {
			s->ready = 0;
			syslog(LOG_NOTICE, "%s WAS KILLED BY A SIGNAL!\n", s->name);
			break;
		} else /* Dont go into infite loops listening to nothing & wasting cpu */
			sleep(1);
	}

	s->pid = 0;
	s->running = 0;
	if (s->restart) {
		syslog(LOG_NOTICE, "RESTARTING %s\n", s->name);
		runservice((void *) s);
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
	s->next = NULL;
	s->running = s->ready = s->started = 0;
	s->pid = 0;
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

		} else if (strcmp(var, "need") == 0) {
			if (s->nneed >= NEEDMAX) {
				syslog(LOG_ALERT, 
						"%s service file has exceded max number of needs.", 
						s->name);
				continue;
			}
			Service *n = findservice(val);
			if (n)
				s->need[s->nneed++] = n;
			else
				syslog(LOG_NOTICE, 
						"ERROR, %s has need %s that could not be found.\n", 
						s->name, val);
		} else if (strcmp(var, "exits") == 0) {
			s->exits = *val == 'y';
		} else if (strcmp(var, "restart") == 0) {
			s->restart = *val == 'y';
		} else {
			syslog(LOG_ALERT, "ERROR, unknown thing service file %s\n", 
					s->name);
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
		syslog(LOG_NOTICE, "ERROR opening service dir %s\n", name);
		return;
	}

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			if (name)
				sprintf(fullname, "%s/%s", name, dir->d_name);
			else
				sprintf(fullname, "%s", dir->d_name);

			/* Create a new service and add it to the list. */
			if (findservice(fullname))
				continue;

			s->next = makeservice();
			s = s->next;
			strcpy(s->name, fullname);
		} else if (dir->d_type == DT_DIR && dir->d_name[0] != '.') {
			if (name)
				sprintf(fullname, "%s/%s", name, dir->d_name);
			else
				sprintf(fullname, "%s", dir->d_name);
			evaldir(fullname, s);
			for (; s && s->next; s = s->next);
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
		services->running = 1;
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
			syslog(LOG_NOTICE, "ERROR opening file %s\n", fullname);
			return 1;
		}
	}

	return 0;
}

void shutdown() {
	if (services)
		services->running = 0;

	openlog("msinit", LOG_PID|LOG_PERROR, LOG_USER);
	
	printf("\nCOMING DOWN.\n");
	syslog(LOG_CRIT, "COMING DOWN\n");

	printf("sending all processes TERM signal...\n");
	kill(-1, SIGTERM);
	sleep(5);
	printf("sending all processes KILL signal...\n");
	kill(-1, SIGKILL);
	sleep(1);

	spawn("/sbin/hwclock", "--systohc", NULL);
	
	sync();

	syslog(LOG_NOTICE, "turning off swap.\n");
	spawn("/sbin/swapoff", "-a", NULL);
	syslog(LOG_NOTICE, "unmounting all file systems.\n");
	spawn("/bin/umount", "-a", "-d", "-r", 
			"-t", "nosysfs,noproc,nodevtmpfs", NULL);
	sleep(1);
	spawn("/bin/umount", "-a", "-r", NULL);
	sleep(1);
	spawn("/bin/mount", "-o", "remount,ro", "/", NULL);
	sleep(1);
	
	closelog();
}
void basicboot() {
	pid_t p;
	int stat_val;

	if (mount("none", "/proc", "proc", 0, "") != 0) {
		printf("ERROR mounting /proc\n");
		exit(1);
	}

	if (mount("none", "/sys", "sysfs", 0, "") != 0) {
		printf("ERROR mounting /sys\n");
		exit(1);
	}
	
	// For debuging purposes.
	spawn("/sbin/agetty", "tty2", "linux", "--noclear", NULL);

	p = spawn("/bin/mount", "-o", "remount,rw", "/", NULL);
	do {
		waitpid(p, &stat_val, 0);
	} while (!WIFEXITED(stat_val));
}

void sigint(int sig) {
	shutdown();
	reboot(RB_POWER_OFF);
	exit(0);
}

void sigquit(int sig) {
	shutdown();
	reboot(RB_AUTOBOOT);
	exit(0);
}

int main(int argc, char **argv) {
	signal(SIGINT, sigint);
	signal(SIGQUIT, sigquit);

	basicboot();

	if (fork() == 0) {
		openlog("msinit", LOG_PID|LOG_PERROR, LOG_USER);
		if (evalfiles()) {
			syslog(LOG_EMERG, "ERROR EVALUATING SERVICE FILES!\n");
			spawn("/sbin/agetty", "tty1", "linux", "--noclear", NULL);
		} else {
			syslog(LOG_NOTICE, "...evaluated. starting all services...\n");
			Service *s;
			for (s = services->next; s; s = s->next) {
				pthread_t pth;
				if (pthread_create(&pth, NULL, runservice, (void *) s))
					syslog(LOG_ALERT, 
							"ERROR starting thread for service %s\n", s->name);
			}
		}
		closelog();
	} else {
		signal(SIGCHLD, SIG_IGN);
	}

	printf("going into infinite sleep\n");
	while (1) 
		sleep(1000);

	return 0;
}
