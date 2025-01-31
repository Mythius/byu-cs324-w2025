/* 
 * tsh - A tiny shell program with job control
 * 
 * Matthias Southwick, south38
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h> 

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define print		printf

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
	int c;
	char cmdline[MAXLINE];
	int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output
	 * on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) >= 0) {
		switch (c) {
			case 'h':             /* print help message */
				usage();
				break;
			case 'v':             /* emit additional diagnostic info */
				verbose = 1;
				break;
			case 'p':             /* don't print a prompt */
				emit_prompt = 0;  /* handy for automatic testing */
				break;
			default:
				usage();
		}
	}

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);
		fflush(stdout);
		fflush(stdout);
	} 

	exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit) then execute it
 * immediately. Otherwise, build a pipeline of commands and wait for all of
 * them to complete before returning.
*/
void eval(char *cmdline) 
{
	char* argv[MAXARGS];

	int cmds[MAXARGS];
	int stdin_redir[MAXARGS];
	int stdout_redir[MAXARGS];

	int send_to_background = parseline(cmdline,argv);

	int num_args = parseargs(argv,cmds,stdin_redir,stdout_redir);

	int prev_pipe[2] = {0,1}; // This values should never get used. Just using it to avoid warnings

	int group_id = 0;

	for(int i=0;i<num_args;i++){
		int code = builtin_cmd(&argv[cmds[i]]);
		if(code != 0) return;

		int mid = 1;
		if(send_to_background){
			mid = fork();
			if(mid > 0){
				return;
			}
		}


		int next_pipe[2];

		// Create Pipes
		if(i != num_args-1){ // no need to create a pipe for final program
			int worked = pipe(next_pipe);
			if(worked < 0){
				fprintf(stderr,"Failed to create pipe\n");
				exit(1);
			}
		}

		int fid = fork(); // Create pipe and then Create Child.
		if(i == 0 && fid > 0){
			group_id = fid;
		}

		if(fid > 0){ // TERMINAL CODE ONLY
			setpgid(fid,group_id);
			if(i > 0){ // If there is a previous pipe close it for terminal
				close(prev_pipe[0]);
				close(prev_pipe[1]); // Close Pipes from Terminal
				// fprintf(stderr,"I am Terminal, closing PREVIOUS PIPE fd:%d for myself and future processes\n",prev_pipe[0]);
				// fprintf(stderr,"I am Terminal, closing PREVIOUS PIPE fd:%d for myself and future processes\n",prev_pipe[1]);
			}
			if(i != num_args-1){ 
				// fprintf(stderr,"I am Terminal, keeping fd:%d for myself and future processes\n",next_pipe[0]);
				prev_pipe[0] = next_pipe[0];
				prev_pipe[1] = next_pipe[1];
			}
		}


		if(fid == 0){ // PROCESS CODE ONLY
			char *env_args[] = {NULL};

			if(i > 0){ // If not the first program, get input from previous pipe
				// fprintf(stderr,"I am process %d, will read from fd:%d\n",i,prev_pipe[0]);
				dup2(prev_pipe[0],0);
			}

			if(i != num_args-1){ // If not the last program setup to write to next pipe
				close(next_pipe[0]); // Close read end
				dup2(next_pipe[1],1); // Write to pipe
				// fprintf(stderr,"I am process %d, will write to fd:%d\n",i,next_pipe[1]);
				// fprintf(stderr,"I am process %d, will close to fd:%d\n",i,next_pipe[0]);
			}

			// Edit File Descriptors Here
			if(stdin_redir[i] > 0){ // INPUT REDIRECTION <
				int fd = open(argv[stdin_redir[i]],O_RDONLY);
				if(fd < 0){
					// fprintf(stderr,"File %s couldn't be read\n",argv[stdin_redir[i]]);
					exit(1);
				} 
				close(0);
				dup2(fd,0);
			}

			if(stdout_redir[i] > 0){ // OUTPUT REDIRECTION >
				int fd = open(argv[stdout_redir[i]],O_WRONLY | O_CREAT, 0600);
				if(fd < 0){
					exit(1);
					// fprintf(stderr,"File %s couldn't be created or opened\n",argv[stdin_redir[i]]);
				}
				close(1);
				dup2(fd,1);
			}

			// fprintf(stderr,"I am process %d, morphing into %s\n",i,argv[cmds[i]]);
			execve(argv[cmds[i]],&argv[cmds[i]],env_args);
			fprintf(stderr,"%s not found.\n",argv[cmds[i]]);
			exit(0); // If error
		}

		if(send_to_background && mid==0){ // FAKE TERMINAL CODE ONLY
			// fprintf(stderr,"Sending to background\n");
			exit(0);
		}

		
		if(i > 0){ // clean up from last pipe (if there was a pipe)
			close(prev_pipe[0]);
		}


	}

	for(int i=0;i<num_args;i++) wait(0); // great for single command.


	return;
}

/* 
 * parseargs - Parse the arguments to identify pipelined commands
 * 
 * Walk through each of the arguments to find each pipelined command.  If the
 * argument was | (pipe), then the next argument starts the new command on the
 * pipeline.  If the argument was < or >, then the next argument is the file
 * from/to which stdin or stdout should be redirected, respectively.  After it
 * runs, the arrays for cmds, stdin_redir, and stdout_redir all have the same
 * number of items---which is the number of commands in the pipeline.  The cmds
 * array is populated with the indexes of argv corresponding to the start of
 * each command sequence in the pipeline.  For each slot in cmds, there is a
 * corresponding slot in stdin_redir and stdout_redir.  If the slot has a -1,
 * then there is no redirection; if it is >= 0, then the value corresponds to
 * the index in argv that holds the filename associated with the redirection.
 * 
 */
int parseargs(char **argv, int *cmds, int *stdin_redir, int *stdout_redir) 
{
	int argindex = 0;    /* the index of the current argument in the current cmd */
	int cmdindex = 0;    /* the index of the current cmd */

	if (!argv[argindex]) {
		return 0;
	}

	cmds[cmdindex] = argindex;
	stdin_redir[cmdindex] = -1;
	stdout_redir[cmdindex] = -1;
	argindex++;
	while (argv[argindex]) {
		if (strcmp(argv[argindex], "<") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			stdin_redir[cmdindex] = argindex;
		} else if (strcmp(argv[argindex], ">") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			stdout_redir[cmdindex] = argindex;
		} else if (strcmp(argv[argindex], "|") == 0) {
			argv[argindex] = NULL;
			argindex++;
			if (!argv[argindex]) { /* if we have reached the end, then break */
				break;
			}
			cmdindex++;
			cmds[cmdindex] = argindex;
			stdin_redir[cmdindex] = -1;
			stdout_redir[cmdindex] = -1;
		}
		argindex++;
	}

	return cmdindex + 1;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	int bg;                     /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
	}
	return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(strcmp(argv[0],"quit")==0){
		exit(0);
	}


	return 0;     /* not a builtin command */
}

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage: shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information\n");
	printf("   -p   do not emit a command prompt\n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

