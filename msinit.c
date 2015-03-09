#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>

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

void shutdown() {
	printf("msinit: running stop script.\n");
	pid_t p = spawn("/etc/msinit.stop", NULL);
	wait(p);

	sleep(1);

	printf("msinit: sending all processes the TERM signal...\n");
	kill(-1, SIGTERM);
	sleep(3);
	printf("msinit: sending all processes the KILL signal...\n");
	kill(-1, SIGKILL);

	printf("msinit: unmounting everything.\n");
	p = spawn("/bin/mount", "-o", "remount,rw", "/", NULL);
	wait(p);
	spawn("/sbin/swapoff", "-a", NULL);
	p = spawn("/bin/umount", "-a", NULL);
	wait(p);
	printf("msinit: waiting for everything to finish.\n");
	sleep(5);
	
	reboot(RB_POWER_OFF);
	exit(0);
}

void mounttmps() {
	if (mount("none", "/proc", "proc", 0, "") != 0) {
		printf("msinit: ERROR mounting /proc\n");
		exit(1);
	}

	if (mount("none", "/sys", "sysfs", 0, "") != 0) {
		printf("msinit: ERROR mounting /sys\n");
		exit(1);
	}
}

void startudev() {
	printf("msinit: starting udev.\n");
	spawn("/sbin/start_udev", NULL);
}

void mountmain() {
	pid_t p;
	
	printf("msinit: remount rw /\n");
	p = spawn("/bin/mount", "-o", "remount,rw", "/", NULL);
	wait(p);

	printf("msinit: mount others from fstab.\n");
	p = spawn("/bin/mount", "-a", NULL);
	wait(p);

	spawn("/sbin/swapon", "-a", NULL);
}

void loadsettings() {
	spawn("/bin/hostname", "-F", "/etc/hostname", NULL);
	spawn("/sbin/hwclock", "--hctosys", NULL);
}

void sigint(int num) {
	shutdown();
}

int main(int argc, char **argv) {
	int s;

	if (geteuid() != 0) {
		fprintf(stderr, "msinit: must be superuser.\n");
		exit(1);
	}
	
	printf("msinit: starting...\n");

	signal(SIGINT, sigint);

	mounttmps();
	startudev();
	mountmain();
	loadsettings();

	spawn("/etc/msinit.start", NULL);

	printf("msinit: give it a little time.\n");
	sleep(1);
	spawn("/sbin/agetty", "tty1", "linux", "--noclear", "-a", "nilp", NULL);

	while (1) {
		printf("msinit: waiting for something.\n");
		wait(-1);
	}

	return 0;
}
