#include "systemcalls.h"
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the commands in ... with arguments @param arguments were executed 
 *   successfully using the system() call, false if an error occurred, 
 *   either in invocation of the system() command, or if a non-zero return 
 *   value was returned by the command issued in @param.
*/
bool do_system(const char *cmd)
{
    if (!cmd) return false;
	return (system(cmd) == 0);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the 
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    va_end(args);

/*
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
*/
	pid_t pid = fork();
	if (pid == -1) { // failure
		perror("fork");
		return false;
	} else if (pid == 0) { // child
		execv(command[0], &command[0]);
	
		// unreachable except in case of error
		perror("execv");
		abort();
	} else { // parent
		int wstat = -1;
		pid_t wpid;
		do {
			wpid = wait(&wstat);
		} while (wpid == pid && (WIFEXITED(wstat) || WIFSIGNALED(wstat)));

		if (WIFEXITED(wstat))
			return (WEXITSTATUS(wstat) == 0);
		else
			return false;
	}
}

/**
* @param outputfile - The full path to the file to write with command output.  
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];
    va_end(args);

/*
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
*/
	pid_t pid = fork();
	if (pid == -1) { // failure
		perror("fork");
		return false;
	} else if (pid == 0) { // child
		int fd = open(outputfile, O_WRONLY);
		if (fd < 0) {
			perror("open");
			abort();
		}

		int nfd = dup2(fd, fileno(stdout));
		if (nfd == -1) {
			perror("dup2");
			abort();
		}
		close(fd);
		
		execv(command[0], &command[0]);
		
		// unreachable except in case of error
		perror("execv");
		abort();
	} else { // parent
		int wstat = 0;
		pid_t wpid;
		do {
			wpid = wait(&wstat);
		} while (wpid == pid && (WIFEXITED(wstat) || WIFSIGNALED(wstat)));
		
		if (WIFEXITED(wstat))
			return (WEXITSTATUS(wstat) == 0);
		else
			return false;
	}
}
