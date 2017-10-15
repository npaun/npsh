#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>

#include "builtin.h"
#include "jobs.h"
#include "run.h"

#define MATCH(builtin,addr) if (strcmp(name,builtin) == 0) return &addr;
#define BLOCKSZ 4096



/********** BUILTIN_INLINE **********/
void builtin_cd(int argc, char** argv) {
	char* where;

	if (argc == 2)	
		where = argv[1];
	else
		where = getenv("HOME");

	char* path = NULL;
	if ((path = realpath(where,NULL)) == NULL) {
		perror("cd");
		EXIT = EXIT_FAILURE;
		goto term;
	}
	else if (chdir(path) == -1) {
		perror("cd");
		EXIT = EXIT_FAILURE;
		goto term;
	}
	else 
		EXIT = EXIT_SUCCESS;

term:
	free(path);
}

void builtin_fg(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr,"fg: expected a job id\n");
		EXIT = EXIT_FAILURE; 	
	}

	int jid;
	// Be very liberal in the jid formats we accept -- the usual %n and just n, and other weird ideas
	if (argv[1][0] == '%')
		jid = atoi(argv[1] + 1);
	else
		jid = atoi(argv[1]);

	if (jid == 0) {
		fprintf(stderr,"%d: invalid job id\n",jid);
		EXIT = EXIT_FAILURE; 
	}

	job* j;
	if ((j = jobs_jid(jid - 1 )) == NULL) { // User-facing jids start at 1 by convention
		fprintf(stderr,"%d: no such job\n",jid);
		EXIT = EXIT_FAILURE;
	}
	run_fg_set(j);
	EXIT = EXIT_SUCCESS;
}


void builtin_exit(int argc, char** argv) {
	if (argc == 2) _exit(atoi(argv[1]));
	_exit(EXIT_SUCCESS);
}

void builtin_jobs(int argc, char** argv) {
	if (argc != 1) {
		fprintf(stderr,"jobs: no arguments expected\n");
		EXIT = EXIT_FAILURE;
		return;
	}

	int i,status;
	job* j;

	for (i = 0; i < JOBS_MAX; i++) {
		if ((j = jobs_jid(i)) != NULL) {
			if (j->fg) continue; // Do not display foreground jobs

			int reporting = waitpid(-j->pgid,&status,WNOHANG | WUNTRACED);

			if (reporting == -1)  // Child has detached itself somehow
				jobs_remove(j);

			run_bg_update(j,reporting,status);
		
		}
	}
	
	EXIT = EXIT_SUCCESS;
}

/******** BUILTIN_FORK ***********/

void builtin_cat(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr,"cat: expected a filename\n");
		_exit(EXIT_FAILURE);
	}
	int fd;
	if ((fd = open(argv[1],O_RDONLY)) == -1) {
			perror(argv[1]);
			_exit(EXIT_FAILURE);
	}

	ssize_t bytes;
	char buf[BLOCKSZ];
	while ((bytes = read(fd,buf,BLOCKSZ)) > 0) {
		if (write(STDOUT_FILENO,buf,bytes) == -1) {
			perror("cat (stdout)");
			_exit(EXIT_FAILURE);
		}
	}
	if (bytes == -1) { // Read error
		perror(argv[1]);
		_exit(EXIT_FAILURE);
	}
	//Otherwise we got an EOF -- all is good
	
	if (close(fd) == -1) {
		perror(argv[1]);
		_exit(EXIT_FAILURE);
	}

	_exit(EXIT_SUCCESS);

}

void builtin_cp(int argc, char** argv) {
	if (argc != 3) {
		fprintf(stderr,"usage: cp filea fileb\n");
		_exit(EXIT_FAILURE);
	}


	int fa, fb;
	if ((fa = open(argv[1],O_RDONLY)) == -1) {
		perror(argv[1]);
		_exit(EXIT_FAILURE);
	}

	if ((fb = open(argv[2],O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) == -1) {
		perror(argv[2]);
		_exit(EXIT_FAILURE);
	}

	ssize_t bytes;
	char buf[BLOCKSZ];
	while ((bytes = read(fa,buf,BLOCKSZ)) > 0) {
		if (write(fb,buf,bytes) == -1) {
			perror(argv[2]);
			_exit(EXIT_FAILURE);
		}
	}
	if (bytes == -1) { // Read error
		perror(argv[1]);
		_exit(EXIT_FAILURE);
	}
	// On EOF, close everything

	if (close(fa) == -1) {
		perror(argv[1]);
		_exit(EXIT_FAILURE);
	}

	if(close(fb) == -1) {
		perror(argv[2]);
		_exit(EXIT_FAILURE);
	}

	_exit(EXIT_SUCCESS);
}

void builtin_pwd(int argc, char** argv) {
	char cwd[BLOCKSZ];

	if (argc != 1) {
		fprintf(stderr,"pwd: no arguments expected\n");
		_exit(EXIT_FAILURE);
	}


	if (getcwd(cwd,BLOCKSZ - 1) == NULL){
		perror("pwd");
		_exit(EXIT_FAILURE);
	}

	// Shove in a trailing newline
	size_t len = strlen(cwd);
	cwd[len++] = '\n';
	cwd[len] = '\0';

	if (write(STDOUT_FILENO,cwd,len) == -1) {
		perror("pwd");
		_exit(EXIT_FAILURE);
	}

	_exit(EXIT_SUCCESS);
}

void builtin_ls(int argc, char** argv) {
	if (argc != 1) {
		fprintf(stderr,"ls: no arguments expected\n");
		_exit(EXIT_FAILURE);
	}

	DIR* dir;
	struct dirent* entry;
	if ((dir = opendir(".")) == NULL) {
		perror("ls (./)");
		_exit(EXIT_FAILURE);
	}

	char buf[BLOCKSZ];

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') continue; // Shhh... hidden files

		snprintf(buf,BLOCKSZ,"%s\n",entry->d_name);
		if (write(STDOUT_FILENO,buf,strlen(buf)) == -1) {
			perror("ls (stdout)");
			_exit(EXIT_FAILURE);
		}

	}

	_exit(EXIT_SUCCESS);
}


/*********** The lookup table **********/

builtin_t* builtin_lookup(int type, char* name) {
	if (type == BUILTIN_INLINE) {
		MATCH("cd",builtin_cd);
		MATCH("exit",builtin_exit);
		MATCH("fg",builtin_fg);
		MATCH("jobs",builtin_jobs);
	}
	else if (type == BUILTIN_FORK) {
		MATCH("cat",builtin_cat);
		MATCH("cp",builtin_cp);
		MATCH("pwd",builtin_pwd);
		MATCH("ls",builtin_ls);

	}

	return NULL;			
}
