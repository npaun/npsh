#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "builtin.h"
#include "jobs.h"
#include "run.h"

int tcfd = STDIN_FILENO; // File descriptor we can use to control the terminal
int OUTFD = STDOUT_FILENO; // Some builtin utilities will just write to OUTFD instead of munging STDOUT
int EXIT = 0; // Last exit status -- only relevant if user EOFs after a fg job

#define BUFSZ 4096

#define REDIR_REPLACE 0
#define REDIR_ADD 1

void run_delay(void) { // As per the TA
	#ifdef DELAY
	int rem = rand() % 10;

	//handles interruption by signal

	do {
		rem = sleep(rem);
	} while(rem);	
	#endif
}

void run_shell_control(void) {
	// Debug code can be inserted here
	#ifdef TC
	tcsetpgrp(tcfd,getpgrp());
	#endif
}

void run_fg_supervise(job* j) {
	int ret, status;
	jobs_fg_set(j); // Mark this as the foreground job

	ret = waitpid(-j->pgid,&status,WUNTRACED); // Don't lock up if our job was stopped
	run_shell_control(); // The process is now done -- back to the shell

	if (ret == -1) { // Not our child anymore
		jobs_remove(j);
		EXIT = 200;
	}
	else if (WIFEXITED(status)) {
		EXIT = WEXITSTATUS(status);
		jobs_remove(j); // Job is terminated
	}
	else if (WIFSIGNALED(status)) {
		EXIT = 128 + WTERMSIG(status); // This is conventional for unix shells
		jobs_remove(j);
	}
	else if (WIFSTOPPED(status)) {
		printf("[%d] suspended (%d) from foreground\n",j->jid + 1,WSTOPSIG(status));
		
		j->signal = WSTOPSIG(status);
		j->fg = false; // Not in the foreground anymore

		EXIT = 200 + WSTOPSIG(status); // Just made this one up on the spot
	}
	else // Wut?
		EXIT = 200; 

}



void run_bg_update(job* j, int reporting, int status) {
	char buf[BUFSZ];

	if (reporting == 0) { // Nothing has changed for the processes in question
		if (j->signal) // If we know it was already suspended
			snprintf(buf,BUFSZ,"[%d] suspended by signal %d\t\t%s",j->jid + 1,j->signal,j->line);
		else // Otherwise, assume it's running
			snprintf(buf,BUFSZ,"[%d] running\t\t\t\t%s",j->jid + 1,j->line);

	}
	else if (WIFEXITED(status)) {
		snprintf(buf,BUFSZ,"[%d] exited with status %d\t\t%s",j->jid + 1,WEXITSTATUS(status),j->line);
		jobs_remove(j); // It's over!
	}
	else if (WIFSIGNALED(status)) {
		snprintf(buf,BUFSZ,"[%d] terminated by signal %d\t\t%s",j->jid + 1,WTERMSIG(status),j->line);
		jobs_remove(j);
	}
	else if (WIFSTOPPED(status)) {
		snprintf(buf,BUFSZ,"[%d] suspended by signal %d\t\t%s",j->jid + 1,WSTOPSIG(status),j->line);
		j->signal = WSTOPSIG(status); // Mark the job as having been suspended so jobs(1) can report it later
	}

	write(OUTFD,buf,strlen(buf)); // Do the write by syscall, just in case this is mandatory
}





void run_bg_supervise(void) {
	int reporting, status;
	job* j;
	while ((reporting = waitpid(-1,&status,WNOHANG | WUNTRACED)) >= 1) {
	       	// While at least one child wants to report
		if (!(j = jobs_pgid(reporting))) // Can't find this job -- not sure how it became our kid
			continue;
		
		run_bg_update(j,reporting,status);
	}
	
}


void run_fg_set(job* j) {
	jobs_fg_set(j); // Mark this as the foreground job
	printf("[%d] running\t\t%s",j->jid + 1,j->line);

	#ifdef TC
	tcsetpgrp(tcfd,j->pgid); // Give that job control over the terminal
	#endif

	kill(-j->pgid,SIGCONT); // Tell the job to start working again if it wasn't
	run_fg_supervise(j); // Supervise it until it quits
}



int run_redirect(char* file, int mode){ // Inline this function to avoid messing things up in the fork
	if (file == NULL) return STDOUT_FILENO;
	int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH); // These options are used for > in POSIX shells
	if (fd != -1) {
		if (mode == REDIR_REPLACE) {
			if (dup2(fd,STDOUT_FILENO) == -1) {
				perror("dup2"); // stdout is now *redirected* to the file
				return -1;
			}
			if (close(fd) == -1) {
				perror("close"); // Close the file descriptor we used to set up redirection
				return -1;
			}
		}
	}
	else {
		perror(file); // Ask the system to explain why we couldn't open
		return -1;
	}

	return fd;
}



 void run_child_exec(job* j) { // Avoid messing things up after a fork
	builtin_t* addr;
	if ((addr = builtin_lookup(BUILTIN_FORK,j->argv[0]))) { // Builtin?
		(*addr)(j->argc,j->argv); // Execute it by function pointer
	}
	else {	// Command?
		if (execvp(j->argv[0],j->argv) == -1) { // Failed to execute?
			perror(j->argv[0]); // Let system explain...
			_exit(127); // This is the only safe way to escape from this
		}
		// The command is now executing. No further shell code will run in the child
	}
}


void run_reset(void) {
	#ifdef TC
	signal(SIGTTOU,SIG_DFL);
	signal(SIGTTIN,SIG_DFL);
	#endif

	signal(SIGTSTP,SIG_DFL);
	signal(SIGINT,SIG_DFL);
}

void run_real(job* j) {
	pid_t pid = fork(); 	
	pid_t pgid = 0;

	if (pid == -1)  // Couldn't fork?
		perror("fork"); // Let system explain...
	else if (pid > 0) { // We are the parent
		j->pgid = pid;
		setpgid(pid,j->pgid); // Give our child process a PGID
		if (j->fg) 
			run_fg_supervise(j); // Wait for the child process to finish
		else
			printf("[%d] %d\n",j->jid + 1,pid); // Report PID of the background job	


		return;

	}
	else  {// We are the child
		pid_t chld = getpid();
		if (pgid == 0) pgid = chld;
		setpgid(pid,pgid); // Give ourselves a PGID

		if (j->fg) {
			#ifdef TC
			tcsetpgrp(tcfd,pgid); // Foreground processes control the terminal
			#endif
		}
	

		if (run_redirect(j->redirect,REDIR_REPLACE) == -1) // Set up redirection
			_exit(255); // We had an issue setting up redirects

		run_reset(); // Fix signal handlers
		run_delay(); // Introduce an artificial delay
		run_child_exec(j);
	}
		

	abort(); //AAAAAAAAAAAAAAAAH
}

void run(job* j) {
	builtin_t* addr;
	if (j->fg && (addr = builtin_lookup(BUILTIN_INLINE,j->argv[0]))) { // Is our job actually a request to modify shell state?
		// You can't coherently background cd, etc. so we'll ignore that case
		if ((OUTFD = run_redirect(j->redirect,REDIR_ADD)) == -1)
			return; // Don't blindly continue
		
		(*addr)(j->argc,j->argv); // Call by function pointer
		if (j->redirect) close(OUTFD);
		jobs_remove(j); // Job done
	}
	else 
		return run_real(j); // We need to actually fork and monitor what is going on
}
