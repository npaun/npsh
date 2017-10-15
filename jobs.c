#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "jobs.h"


typedef struct {
	job* store[JOBS_MAX];	// Array of job pointers
	int fg;			// Currently active job
} jobs_table;

jobs_table jt;

job* jobs_add(void) {
	job* j = malloc(sizeof(job));
	j->argc = 0;
	j->fg = true;
	j->redirect= NULL;
	j->buf = NULL;
	j->line = NULL;
	j->signal = 0;

	int i;
	for (i = 0; (i < JOBS_MAX && jt.store[i] != NULL); i++) // Find a free slot, linearly
		; // NOOP

	if (i == JOBS_MAX) {
		fprintf(stderr,"Error: too many jobs.\n");
		abort();
	}

	j->jid = i;

	jt.store[j->jid] = j;

	return j;
}

void jobs_remove(job* j) {

	if (jt.fg == j->jid)  // Was the current job in the foreground? 
		jt.fg = -1; // There is no more foreground job


	int jid = j->jid;

	free(j->buf); // Backing store of argv
	free(j->line); // Backing of message printing
	free(j);
	jt.store[jid] = NULL;
}

void jobs_fg_set(job* j) {
	jt.fg = j->jid;
}

job* jobs_fg_get(void) {
	if (jt.fg != -1) {
		return jt.store[jt.fg];
	}
	else
		return NULL;
}

job* jobs_pgid(pid_t pgid) {
	for (int i = 0; i < JOBS_MAX; i++) {
		if (jt.store[i] && jt.store[i]->pgid == pgid) 
			return jt.store[i];
	}

	return NULL;
}

job* jobs_jid(int jid) {
	if (jid < JOBS_MAX) 
		return jt.store[jid];
	else
		return NULL;
}
