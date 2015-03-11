typedef struct Service Service;
struct Service {
	char *name;
	char *exec[10];
	char after[10][256];
	Service *next;
};

void runservice(Service *s);
void writepidfile(Service *s, pid_t pid);
void removepidfile(Service *s);
int isservicerunning(Service *s);
Service *findservice(char *name);

int spawn(char *prog, ...);

void evalfile(char *name, FILE *file);
int evalfiles();

void shutdown();
void basicboot();

void sigint(int num);
void fallback();
