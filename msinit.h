#define SERVICEDIR "/etc/msinit.d"
#define LOGFILE "/var/log/msinit"

#define RUNNING 0
#define SHUTDOWN 1

#define EXECMAX 15
#define NEEDMAX 5

typedef struct Service Service;
struct Service {
	Service *next; /* For main linked list. */
	char *name;
	char *exec[EXECMAX];
	pid_t pid;	
	int exits, restart;
	int started, ready;
	int nneed;
	Service *need[NEEDMAX]; /* Wont start until all in here are ready. */
};

void message(char *s, ...);

Service *makeservice(); /* Makes and empty service struct. */

void *runservice(void *a);
Service *findservice(char *name);

int spawn(char *prog, ...);

void fleshservice(Service *s, FILE *file);
void evaldir(char *name, Service *s);
int evalfiles();

void shutdown();
void basicboot();

void sigint(int num);
void fallback();
