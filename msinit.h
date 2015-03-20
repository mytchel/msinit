#define FALLBACK spawn("/sbin/sulogin", "-p", NULL)
#define SERVICEDIR "/etc/msinit.d/"

#define EXECMAX 15
#define NEEDMAX 15

typedef struct Service Service;
struct Service {
	Service *next; /* For main linked list. */
	char *name;
	char *exec[EXECMAX];
	pid_t pid;
	int exits, restart;
	int running, ready, started;
	int nneed;
	pthread_t thread;
	Service *need[NEEDMAX]; /* Wont start until all in here are ready. */
};

void stopservice(Service *s);
void *runservice(void *a);
Service *findservice(char *name);
Service *makeservice(); /* Makes and empty service struct. */

int spawn(char *prog, ...);

int updateservice(Service *s);
void evaldir(char *name, Service *s);
void evalfiles();

void shutdown();
void basicboot();

void inthandler(int num);
void quithandler(int num);
void chldhandler(int num);
