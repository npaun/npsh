#include <stdbool.h>
#include <sys/types.h>

#define ARG_MAX 16
#define JOBS_MAX 16

typedef struct {
	/* (1) The user's command */
	char*	line;		// Unmodified command line for use in messages
	char* 	buf; 		// We keep the actual text of the command line arguments here
	char* 	argv[ARG_MAX];  // Argument vector
	int	argc;		// Number of arguments
	bool	fg;		// Is this process in the foreground?
	char*	redirect;	// (optional) Redirection destination

	/* (2) Control information */
	pid_t	pgid;		// Process Group ID
	int 	jid;
	int	signal; 	// The suspending signal
} job;

job* jobs_add(void);
void jobs_remove(job*);
void jobs_fg_set(job*);
job* jobs_fg_get(void);
job* jobs_pgid(pid_t);
job* jobs_jid(int);
