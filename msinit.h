typedef struct Service Service;
struct Service {
	Service *next; /* For main linked list. */
	char *name;
	char *exec[10];
	
	int exits, restart;
	int started, ready;
	int nneed;
	Service **need; /* Wont start until all in here are ready. */
};

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
