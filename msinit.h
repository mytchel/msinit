#ifndef SERVICEDIR
#define SERVICEDIR "/etc/msinit.d/"
#endif

#define MAXLEN 15

typedef struct Service Service;
struct Service {
	Service *next; /* For main linked list. */
	char *name;
	char *exec[MAXLEN];
	char *env[MAXLEN];
	pid_t pid;
	int exits, restart;
	int running, ready, started;
	int nneed;
	pthread_t thread;
	Service *need[MAXLEN]; /* Wont start until all in here are ready. */
};

void stopservice(Service *s);
void *runservice(void *a);

Service *findservice(char *name);
Service *makeservice(); /* Makes an empty service struct. */

/* splits strings for env and exec into the array. */
void splittoarray(char *array[MAXLEN], char *str); 

int updateservice(Service *s);
void evaldir(char *name, Service *s);
void evalfiles();

void setdefaultenv();

int spawn(char *prog, ...);
void shutdown();

void inthandler(int num);
void quithandler(int num);
void chldhandler(int num);

