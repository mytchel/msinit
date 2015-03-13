#define SERVICEDIR "/etc/msinit.d"
#define LOGFILE "/var/log/msinit"
#define BACKLOGFILE "/var/log/msinit.old"

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
	int running, ready, started;
	int nneed;
	Service *need[NEEDMAX]; /* Wont start until all in here are ready. */
};

void message(char *s, ...);
void mvlogfile();

void *runservice(void *a);
Service *findservice(char *name);
Service *makeservice(); /* Makes and empty service struct. */

int spawn(char *prog, ...);

void fulloutservice(Service *s, FILE *file);
void evaldir(char *name, Service *s);
int evalfiles();

void shutdown(int r);
void basicboot();

void sigint(int num);
void sigquit(int num);
