#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>


#include "jobs.h"
#include "run.h"

#ifdef DELAY
#include <time.h>
#endif

#ifdef TC
#include <termios.h>
struct termios sane_termios;
#endif

typedef enum {INIT,PARSE,REDIRECT,EOL} ParseState;


bool parse(job* cmd, char* line) {
	char* tok0 = line; // Start of current token
	char* toki = line; // Current position in the string
	ParseState state = INIT;
	

	while ((toki = strpbrk(toki,">& \t\n")) != NULL) { // Get pointer to first delimiter
		char delim = *toki;
		*toki = 0; // Make the delimiter represent end of argument (NULL)
		if (strlen(tok0))  { // Non-null string
			switch (state) {
				case INIT:
					if (cmd->argc == ARG_MAX - 1) {
						fprintf(stderr,"warning:\t Too many arguments: current argument will be silently ignored.\n");
						break;
					}
					cmd->argv[cmd->argc++] = tok0; // Store into argv
					state = PARSE;
				break;

				case PARSE:
					if (cmd->argc == ARG_MAX - 1) {
						fprintf(stderr,"warning:\t Too many arguments: current argument will be silently ignored.\n");
						break;
					}

					cmd->argv[cmd->argc++] = tok0; // Store into argv
				break;

				case REDIRECT:
					cmd->redirect = tok0; // Store into the redirect destination
					state = PARSE;
					/* N.B: >file& is valid syntax for backgrounded truncation */
				break;

				case EOL:
					fprintf(stderr,"warning: \tMultiple statements on one line: not implemented.\n");
					cmd->argv[cmd->argc] = 0; // So our dear buddy execvp knows to stop
					return true;
				break;
			}
		}
					
		toki++; // Move past delimiter
		tok0 = toki; // Next token starts after this delimiter

		switch (delim) {
			case '&':
				/* In POSIX shell syntax, the & is a statement separator.
				 * We will permit this use but ignore the RHS of the statement.
				 * A leading & will be rejected, just as in a real shell. */

				if (state == PARSE) {
					cmd->fg = false;
					state = EOL;
				}
				else {
					fprintf(stderr,"*** Syntax error: Unexpected `&': a command was expected.\n");
					return false;
				}
			break;

			case '>':
				if (state == PARSE || state == INIT) {
					if (cmd->redirect) {
						/* POSIX shell syntax allows for multiple redirections, which we haven't implemented.
						 * Let the user know that the last one wins. */
						fprintf(stderr,"warning: Multiple redirections: last one wins.\n");
					}
						state = REDIRECT;
				}
			break;
				
		}

	}

	if (state == REDIRECT) {
		/* Everything must redirect somewhere. 
		 * Apparently our dear user forgot. */
		fprintf(stderr,"*** Syntax error: Unexpected end of line: a filename was expected.\n");
		return false;
	}
	else if (state == PARSE || state == EOL) {
		cmd->argv[cmd->argc]  = 0; // So execvp knows to stop
		return true; // Yee haw!
	}
	else
		return false; // Avoid unnecessary processing for an empty line
}



bool repl(void) {
	ssize_t len;
	size_t cap;

	job* j;

	for(;;) {
		char *line = NULL;
		run_bg_supervise();

		#ifdef TC
		tcsetattr(STDOUT_FILENO,TCSADRAIN,&sane_termios); // Put back sane terminal settings
		#endif

		printf("\033[32mnp\033[35msh\033[1m$\033[0m ");
		len = getline(&line,&cap,stdin);
		//run_bg_supervise(); // This was useful, but bash doesn't do this
		
		j = jobs_add();

		if (len < 1) {
			printf("\n"); // So the user won't cry at what has become of his terminal
			exit(EXIT); // User probably whacked a control D
		}


		j->line = line;
		j->buf = realloc(j->buf,(len+1)*sizeof(char));
		memcpy(j->buf,line,len + 1);


		if (!parse(j,j->buf)) {
			jobs_remove(j);
			continue;
		}

		run(j);
	}

		
}

void interrupt(int nil) {
	job* j;

	write(STDOUT_FILENO,"#",2); // Acknowledge the user's request to interrupt.
	if ((j = jobs_fg_get()) != NULL) {
		kill(-j->pgid,SIGINT); // Signal for you...!
	}
}

void init(void) {
	signal(SIGTSTP,SIG_IGN); // Assignment requirement: Ignore Ctrl+Z
	signal(SIGTTOU,SIG_IGN); // We need to be able to control the terminal
	signal(SIGTTIN,SIG_IGN); //"
	signal(SIGINT,interrupt);

	setpgrp(); // Great a new process group
	#ifdef TC
	tcsetpgrp(STDOUT_FILENO,getpgrp()); // Take control of the tty
	tcgetattr(STDOUT_FILENO,&sane_termios); // Save terminal settings
	#endif

	#ifdef DELAY
	srand(time(NULL));
	#endif
}

int main(int argc, char** argv) {
	init();
	repl();
	return 0;
}
