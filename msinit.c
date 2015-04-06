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
#include <syslog.h>

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

	/* wait on all needs */
	for (i = 0; i < s->nneed; i++) {
		while (!s->need[i]->ready) {
			if (s->need[i]->pid > 1) 
				waitpid(s->need[i]->pid, NULL, 0);
			else
				usleep(1000);
		}
	}

	/* if no exec then stop here */
	if (!s->exec[0]) {
		s->ready = 1;
		stopservice(s);
	}

	s->pid = fork();
	if (s->pid == 0) {
		syslog(LOG_NOTICE, "%s", s->name);
		setsid();
		
		if (s->env) 
			execvpe(s->exec[0], s->exec, s->env);
		else
			execvp(s->exec[0], s->exec);

		syslog(LOG_ALERT, "execvp for has exited!", s->name);
		exit(0);
	} else {
		if (!s->exits) s->ready = 1;

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
				s->ready = s->running = 0;
				break;
			} else
				usleep(1000);
		}

		s->pid = 0;
		if (s->restart) {
			syslog(LOG_ALERT, "restarting %s", s->name);
			if (!updateservice(s)) {
				sleep(1); /* Don't go into uninteractable loops of restarting. */
				runservice((void *) s);
			}
		} else {
			stopservice(s);
		}
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
	int i;
	Service *s = malloc(sizeof(Service));
	s->name = malloc(sizeof(char) * 256);
	s->next = NULL;
	s->running = s->ready = s->started = 0;
	s->pid = 0;
	for (i = 0; i < MAXLEN; i++) s->exec[i] = s->env[i] = NULL;
	return s;
}

void splittoarray(char *array[MAXLEN], char *str) {
	char *c, *l, o;
	int i = 0;
	c = l = str;
	do {
		if (*c == '"' || *c == '\'') {
			o = *c;
			for (c++; c && *c != o; c++);
		}

		if (*c == '\\') c += 2;

		if (*c == ' ' || *c == '\t' || !*c) {
			if (array[i]) free(array[i]);
			array[i] = calloc(c - l + 1, sizeof(char));
			strncpy(array[i], l, c - l);
			array[++i] = NULL;

			for (l = c; *l == ' ' || *l == '\t' || !l; l++);
			c = l;
		}
	} while (*(c++) && i < MAXLEN - 1);
}

int updateservice(Service *s) {
	FILE *file;
	char path[256];
	char buf[256], *line, *var, *val;
	int afters = 0;
	int i, linen;

	s->exec[0] = NULL;
	s->exits = 1;
	s->restart = 0;
	s->nneed = 0;

	sprintf(path, "%s/%s", SERVICEDIR, s->name);
	file = fopen(path, "r");
	if (!file)
		return 1;

	linen = 0;
	while (fgets(buf, sizeof(buf), file) != NULL) {
		for (line = &buf[0]; *line == ' ' || *line == '\t'; line++);

		if (*line == '#' || *line == '\n')
			continue;

		var = strsep(&line, "=");
		val = strsep(&line, "\n");

		if (strcmp(var, "exec") == 0) {
			splittoarray(s->exec, val);

		} else if (strcmp(var, "env") == 0) {
			splittoarray(s->env, val);

		} else if (strcmp(var, "need") == 0) {
			if (s->nneed >= MAXLEN) {
				syslog(LOG_CRIT,
						"%s service file has exceded max number of needs.", 
						s->name);
				continue;
			}
			Service *n = findservice(val);
			if (n)
				s->need[s->nneed++] = n;
			else
				syslog(LOG_CRIT, "%s has need %s that could not be found.", 
						s->name, val);

		} else if (strcmp(var, "exits") == 0) {
			s->exits = *val == 'y';

		} else if (strcmp(var, "restart") == 0) {
			s->restart = *val == 'y';

		} else {
			syslog(LOG_CRIT, "unknown thing service file %s:%i", 
					s->name, linen);
		}

		linen++;
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
		syslog(LOG_ALERT, "opening service dir %s", name);
		return;
	}

	while ((dir = readdir(d)) != NULL) {
		if (dir->d_name[0] == '.') continue;
		if (dir->d_type == DT_REG || dir->d_type == DT_LNK) {
			sprintf(name, "%s%s", dirname, dir->d_name);
			s->next = makeservice();
			s = s->next;
			strcpy(s->name, name);
		} else if (dir->d_type == DT_DIR) {
			sprintf(name, "%s%s/", dirname, dir->d_name);
			evaldir(name, s);
			for (; s && s->next; s = s->next);
		}
	}

	closedir(d);
}

void evalfiles() {
	Service *s;

	services = makeservice();
	services->running = 1;

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
		for (i = 1; i < MAXLEN && (argv[i] = va_arg(ap, char *)) != NULL; i++);
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

	syslog(LOG_WARNING, "COMING DOWN.");

	sync();

	syslog(LOG_WARNING, "sending all processes TERM signal...");
	kill(-1, SIGTERM);
	sleep(5);
	syslog(LOG_WARNING, "sending all processes KILL signal...");
	kill(-1, SIGKILL);
	sleep(3);

	spawn("/sbin/hwclock", "--systohc", NULL);

	sync();

	syslog(LOG_WARNING, "swapoff");
	spawn("/sbin/swapoff", "-a", NULL);
	sleep(1);
	syslog(LOG_WARNING, "/bin/umount -a -r");
	spawn("/bin/umount", "-a", "-r", NULL);
	sleep(2);
	syslog(LOG_WARNING, "/bin/mount -o remount,ro /");
	spawn("/bin/mount", "-o", "remount,ro", "/", NULL);

	sync();

	sleep(3);
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

	setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);

	if (fork() == 0) {
		openlog("msinit", LOG_NDELAY|LOG_PERROR, LOG_DAEMON);
		syslog(LOG_NOTICE, "evaluating servies...");
		evalfiles();
		syslog(LOG_NOTICE, "starting services...");
		for (s = services->next; s; s = s->next) 
			pthread_create(&s->thread, NULL, runservice, (void *) s);
	}

	while (1) 
		sleep(1000);

	return 0;
}
