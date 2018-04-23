/*************************************************************************
 * Program Name: babysh.c
 * Author: Kevin Pardew
 * Description: This program is a shell to run command line instructions
 *   and return the results. This shell allows for the redirection of 
 *   standard input and output, and supports foreground and background 
 *   processes. The shell supports three built in commands: exit, cd, and
 *   status. The shell also supports comments, which are lines beginning
 *   with the # character.
 ************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINE_LENGTH 2048
#define MAX_ARGS 512
#define MAX_PROCESSES 100
#define IS_PROCESS_FINISHED 0 
#define IS_SHELL_RUNNING 1

// Function prototypes
void getInput(char input[]);
void parseInput(char input[], char *args[]);
void processArgs(char *args[], int *status, int *termination, pid_t bgOpen[]);
void cmdChangeDir(char *args[], int *status);
void cmdStatus(int status, int termination);
void cmdExit(pid_t bgOpen[]);
void cmdExecute(char *args[], int *status, int *termination, pid_t bgOpen[]);
void catchInterrupt(int signo);
int isInputRedirected(char *args[]);
int isOutputRedirected(char *args[]);
int isBackground(char *args[]);


int main(void) {
	char userInput[MAX_LINE_LENGTH + 1];
	char *inputArgs[MAX_ARGS];
	int i;
	int termination = 0;            // Termination value of process, if any
	int status = 0;                 // Returned status of process, if any
	pid_t bgOpen[MAX_PROCESSES];    // Array of open background processes
	int bgCounter = 0;              // Counter for background process array
	int bgStatus;                   // Status of completed background process

	// Initialize bgOpen to contain non-valid process ids
	for (i=0; i<MAX_PROCESSES; i++) {
		bgOpen[i] = -1;
	}

	// Set up a signal handler for main to ignore SIGINT. sigfillset() 
	// ensures that other signals will be blocked while this signal
	// is being processed
	struct sigaction act;
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigfillset(&(act.sa_mask));

	sigaction(SIGINT, &act, NULL);


	// Show the command prompt until user enters "exit"
	while (IS_SHELL_RUNNING) {
		// Check for background processes
		while (bgOpen[bgCounter] != -1) {
			pid_t curPid = bgOpen[bgCounter];
			pid_t wpid;

			if (curPid != IS_PROCESS_FINISHED) {
				// Current process has not been marked completed
				
				// Check for process completion
				wpid = waitpid(curPid, &bgStatus, WNOHANG);

				if (wpid == -1) {
					perror("wait failed\n");
					status = EXIT_FAILURE;
				}
				else if (wpid > 0) {
					// The process has completed
					printf("background pid %d is done: ", curPid);
					fflush(stdout);
				
					// Print exit value of process	
					if (WIFEXITED(bgStatus)) {
						printf("exit value %d\n", WEXITSTATUS(bgStatus));
						fflush(stdout);
					}

					// Print termination if foreground process is terminated.
					// Set termination flag for use in the status command.
					if (WIFSIGNALED(bgStatus)) {
						printf("terminated by signal %d\n", WTERMSIG(bgStatus));
						fflush(stdout);
					}

					// Mark current position in array as completed
					bgOpen[bgCounter] = IS_PROCESS_FINISHED;
				}	
			}
			bgCounter++;
		}	

		// Reset counter for background processes
		bgCounter = 0;

		// Show command prompt	
		printf(": ");
		fflush(stdout);

		// Get input from user
		getInput(userInput);

		// Parse user input into list of arguments
		parseInput(userInput, inputArgs); 
		
		// Process the arguments and attempt to execute any commands found
		processArgs(inputArgs, &status, &termination, bgOpen);
	}

	exit(EXIT_SUCCESS);
}



/*************************************************************************
 *
 * Function:    getInput() 
 * 
 * Description: This function gets a line of input from the user and 
 *              removes any newline character.
 * 
 * Parameters:  input - a char array
 * 
 * Returns:     None. input parameter is altered.
 *
 ************************************************************************/ 
void getInput(char input[]) {
	char *p;

	fgets(input, MAX_LINE_LENGTH, stdin);
	
	// Remove newline character if there is one
	if ((p = strchr(input, '\n')) != 0)
		*p = '\0';
}



/*************************************************************************
 *
 * Function:    parseInput()
 * 
 * Description:	This function evaluates a line of user input and parses
 *              it into an array of arguments.
 * 
 * Parameters:  input - a char array
 *              args - an array of char*
 * 
 * Returns:     None. args parameter will be altered.
 *
 ************************************************************************/ 
void parseInput(char input[], char *args[]) {
	int position = 0;
	char *token;

	token = strtok(input, " ");

	while (token != NULL) {
		// Add parsed value to array of arguments
		args[position] = token;
		token = strtok(NULL, " ");
		position++;
	}

	// Ensure that the final value in the array is set to null.
 	// This will help account for blank input, and provide for a 
 	// stopping point when evaluating the arguments.
	args[position] = NULL;

}



/*************************************************************************
 *
 * Function: 	processArgs()
 * 
 * Description: This function evaluates the first string in an array of
 *              arguments. An empty string (representing a blank line of 
 *              input) and a string beginning with '#' (the comment
 *              symbol) are ignored. All other strings are commands and 
 *              the function determines whether the command is built in
 *              or not. The command is sent to the appropriate function
 *              for execution.
 * 
 * Parameters:	args - an array of char*
 *              status - pointer to an int
 *              termination - pointer to an int
 *              bgOpen - an array of pid_t
 * 
 * Returns:     None.
 *
 ************************************************************************/ 
void processArgs(char *args[], int *status, int *termination, pid_t bgOpen[]) {
	int position = 0;


	if (args[0] == NULL || args[0][0] == '#') {
		// If the user input is a blank line or a commented line 
		// (beginning with #), do nothing and return to the shell prompt.
		return;
	}

	if (strcmp(args[0], "cd") == 0) {
		// Execute the change directory command
		cmdChangeDir(args, status);
	}
	else if (strcmp(args[0], "status") == 0) {
		// Execute the status command
		cmdStatus(*status, *termination);
	}
	else if (strcmp(args[0], "exit") == 0) {
		// Execute the exit command
		cmdExit(bgOpen);
	}	
	else {
		// Attempt to execute the given command
		cmdExecute(args, status, termination, bgOpen);
	}
}



/*************************************************************************
 *
 * Function:    cmdChangeDir() 
 * 
 * Description: This function changes the working directory to a path
 *              specified by the user. The function supports absolute
 *              and relative paths. If the path is not valid, an error
 *              message is printed.
 * 
 * Parameters:  args - an array of char*
 *              status - pointer to an int
 * 
 * Returns:     None. status parameter may be altered.
 *
 ************************************************************************/ 
void cmdChangeDir(char *args[], int *status) {
	// Find the users home directory
	char *homeDir = getenv("HOME");	

	// check for bad directory
	// set exit status
	// print cd: bad dir: No such file or directory

	if (args[1] == NULL) {
		// If no destination directory is specified change to home directory
		chdir(homeDir);
	}
	else {
		// Attempt to change to specified directory
		if (chdir(args[1]) == -1) {
			printf("cd: %s: No such file or directory\n", args[1]);
			fflush(stdout);
			*status = EXIT_FAILURE;
		}
	}
}



/*************************************************************************
 *
 * Function:    cmdStatus()
 * 
 * Description: This function executes the build in command "status". If
 *              the previous command exited normally, its exit status
 *              is displayed. Otherwise, if it was terminated by a signal,
 *              a message with the signal number is displayed.
 * 
 * Parameters:  status - int
 *              termination - int
 * 
 * Returns:     None.
 *
 ************************************************************************/ 
void cmdStatus(int status, int termination) {
	if (termination > 0) {
		// Previous process was terminated and the termination flag was 
		// set to a valid signal, so show termination signal
		printf("terminated by signal %d\n", termination);
		fflush(stdout);
	}
	else {
		// Previous process exited normally, so show exit status
		printf("exit value %d\n", status);
		fflush(stdout);
	}
}



/*************************************************************************
 *
 * Function:    cmdExit()
 * 
 * Description: This function executes the build in command "exit". Before
 *              exiting the program, all background processes are killed.
 * 
 * Parameters:  bgOpen - an array of pid_t
 * 
 * Returns:     None.
 *
 ************************************************************************/ 
void cmdExit(pid_t bgOpen[]) {
	int bgCounter = 0;
	pid_t curPid;

	// Kill running background processes	
	while (bgOpen[bgCounter] != -1) {
		curPid = bgOpen[bgCounter];
	
		// Attempt to kill the current process
		if (curPid != IS_PROCESS_FINISHED) {
			if (kill(curPid, SIGKILL) == -1) {
				perror("kill failed");
			}
		}
		bgCounter++;
	}

	exit(EXIT_SUCCESS);
}



/*************************************************************************
 *
 * Function:    cmdExecute()
 * 
 * Description: This function executes a command that is not built into 
 *              the shell. It determines if the process should be run in
 *              the background, and if the input or output need to be
 *              redirected. A child process is created to execute the 
 *              command in the foreground, or background if specified.
 *              The parent process evaluates if a foreground process
 *              exited normally or was terminated. 
 * 
 * Parameters:  args - an array of char*
 *              status - pointer to an int
 *              termination - pointer to an int
 *              bgOpen - an array of pid_t
 * 
 * Returns:     None. status, termination, and bgOpen parameters may 
 *              be altered.
 *
 ************************************************************************/ 
void cmdExecute(char *args[], int *status, int *termination, pid_t bgOpen[]) {
	
	pid_t cpid;         // pid of child process
	pid_t wpid;         // return value of waitpid() command
	int waitStatus;     // value altered in waitpid() command
	int inputFile;
	int outputFile;
	int redirectInput = isInputRedirected(args);
	int redirectOutput = isOutputRedirected(args);
	int runInBackground = isBackground(args);		
	int bgCounter = 0;

	
	// Set input and output files for process running in background 
	// without input or output redirection. The file location of
	// "/dev/null" suppresses any input or output to the process.
	if (runInBackground) {
		if (!redirectInput)
			inputFile = open("/dev/null", O_RDONLY);
		if (!redirectOutput)
			outputFile = open("/dev/null", O_WRONLY);

		if (inputFile == -1 || outputFile == -1) {
			printf("cannot open /dev/null\n");
			fflush(stdout);
			*status = EXIT_FAILURE;
			return;
		}

		// Ensure files are closed on exec
		fcntl(inputFile, F_SETFD, FD_CLOEXEC); 	
		fcntl(outputFile, F_SETFD, FD_CLOEXEC); 	
	}

	// For commands with redirection, the file to open will be the 
	// third element in args[].

	// Set input file for redirected input
	if (redirectInput) {
		inputFile = open(args[2], O_RDONLY);

		if (inputFile == -1) {
			printf("File Error: cannot open %s for input\n", args[2]);
			fflush(stdout);
			*status = EXIT_FAILURE;
			return;
		}
	
		// Ensure file is closed on exec
		fcntl(inputFile, F_SETFD, FD_CLOEXEC); 	
	}

	// Set output file for redirected output
	if (redirectOutput) {
		outputFile = open(args[2], O_WRONLY|O_CREAT|O_TRUNC, 0644);
		
		if (outputFile == -1) {
			printf("File Error: cannot open %s for ouput\n", args[2]);
			fflush(stdout);
			*status = EXIT_FAILURE;
			return;
		}
	
		// Ensure file is closed on exec
		fcntl(outputFile, F_SETFD, FD_CLOEXEC); 	
	}


	// Fork processes
	cpid = fork();

	if (cpid == -1) {
		perror("fork failed");
	}	
	else if (cpid == 0) {
		// Set up singnal handler to catch SIGINT in
		// the foreground processes. sigfillset() ensures
		// that other signals will be blocked while this
		// signal is being processed.
		struct sigaction act;
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigfillset(&(act.sa_mask));

		if (runInBackground) { 
			// Ensure a background process is not terminated by SIGINT
			act.sa_handler = SIG_IGN;
		}

		// Perform the default behavior of SIGINT for foreground processes
		sigaction(SIGINT, &act, NULL);


		if (runInBackground) {
			// Change input and output for processes running in background	

			if (dup2(inputFile, 0) == -1) {
				perror("dup2 failed");
				exit(EXIT_FAILURE);
			}

			if (dup2(outputFile, 1) == -1) {
				perror("dup2 failed");
				exit(EXIT_FAILURE);
			}
		}
		else if (redirectInput || redirectOutput) {
			// Change input or output if redirected in foreground
			
			int redirectFd;

			if (redirectInput)
				redirectFd = dup2(inputFile, 0);

			if (redirectOutput)
				redirectFd = dup2(outputFile, 1);

			if (redirectFd == -1) {
				perror("dup2");
				exit(EXIT_FAILURE);
			}
		}


		// Execute the process
		if (redirectInput || redirectOutput) {
			// Execute the program without arguments
			execlp(args[0], args[0], NULL);
		}
		else {
			// Execute the program with full list of arguments
			execvp(args[0], args);
		}

		// This is only reached if exec() fails
		printf("Execution Error: %s is not a valid command\n", args[0]);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	else if (cpid > 0) {
		
		if (runInBackground) {
			printf("background pid is %d\n", cpid);
			fflush(stdout);

			while (bgCounter < MAX_PROCESSES) {
				if (bgOpen[bgCounter] < 1) {
					// Add pid of background process to array.
					// If value is 0, insert at that position.
					// A value of -1 indicates there are no finished
					// processes in the list.
					bgOpen[bgCounter] = cpid;
					return;
				}
				bgCounter++;
			}
		}
		else {
			// Wait for the foreground child process to finish
			wpid = waitpid(cpid, &waitStatus, 0);
		}		

		if (wpid == -1) {
			perror("wait failed");
			*status = EXIT_FAILURE;
		}


		if (WIFEXITED(waitStatus)) {
			// The foreground process finished execution.
			// Save the exit status
			*status = WEXITSTATUS(waitStatus);
			
			// Ensure termination flag is set to non-valid signal value
			*termination = 0;
		}

		if (WIFSIGNALED(waitStatus)) {
			// The foreground process was terminated.
			// Set termination flag for use in the status command
			*termination = WTERMSIG(waitStatus);
			printf("terminated by signal %d\n", *termination);
			fflush(stdout);
		}
	}
}



/*************************************************************************
 *
 * Function:    isInputRedirected()
 * 
 * Description: This function searches for the "<" character to determine
 *              if a process should have its output redirected.
 * 
 * Parameters:  args - an array of char*
 * 
 * Returns:     1 if "<" is found, 0 if not.
 *
 ************************************************************************/ 
int isInputRedirected(char *args[]) {
	int position = 0;

	while (args[position] != NULL) {
		if (strcmp(args[position], "<") == 0) {
			return 1;
		}

		position++;
	}

	return 0;
}



/*************************************************************************
 *
 * Function:    isOutputRedirected()
 * 
 * Description: This function searches for the ">" character to determine
 *              if a process should have its output redirected.
 * 
 * Parameters:  args - an array of char*
 * 
 * Returns:     1 if ">" is found, 0 if not.
 *
 ************************************************************************/ 
int isOutputRedirected(char *args[]) {
	int position = 0;

	while (args[position] != NULL) {
		if (strcmp(args[position], ">") == 0) {
			return 1;
		}

		position++;
	}

	return 0;
}



/*************************************************************************
 *
 * Function:    isBackground() 
 * 
 * Description: This function searches for the "&" character to determine
 *              if a process should run in the background.
 * 
 * Parameters:  args - an array of char*
 * 
 * Returns:     1 if "&" is found, 0 if not.
 *
 ************************************************************************/ 
int isBackground(char *args[]) {
	int position = 0;

	while (args[position] != NULL) {
		if (strcmp(args[position], "&") == 0) {
			args[position] = NULL;
			return 1;
		}

		position++;
	}

	return 0;
}
