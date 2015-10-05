/*
 * tsh - A tiny shell program with job control
 *
 * Vijay Parmar (Turnin login id : vparmar)
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

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"



/* Global variables */

char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
extern char **environ; /* defined in libc */



/* Function prototypes */
int kill(pid_t pid, int signal);
int sigprocmask(int sig, const sigset_t *set, sigset_t *set_last);


/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);



/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
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

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

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
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/




/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////



void eval(char *cmdline)
{

	int bg; // Save the return value from parseline()
	//
    // The 'argv' vector is filled in by the parseline
    // routine below. It provides the arguments needed
    // for the execve() routine, which you'll need to
    // use below to launch a process.
    //
	char *argv[MAXARGS]; // argument list
	pid_t pid; // Keep the pid when we fork
	sigset_t mask;


	//
    // The 'bg' variable is TRUE if the job should run
    // in background mode or FALSE if it should run in FG
    //
	// Parseline returns if we have bg(1) or fg(0)
	bg = parseline(cmdline, argv);


	// Some basecase, empty argument line
	if (argv[0] == NULL)
	{
		return;
	}

	// This if statement will run if our argument is not
	// a build-in command, like quit, jobs, fg and bg
	if (!builtin_cmd(argv))
	{
		// Now to prevent the sigchld handler to deletejob
		// before addjob we block it using sigprocmask.
		
		sigemptyset(&mask); // Initialize a empty set
		sigaddset(&mask, SIGCHLD); // Add SIGCHLD to the set

		// Adding SIGCHILD to the blocking list. By this we can
		// be sure our job won't be deleted from the job list
		// until it has ended, causing possible error.
		sigprocmask(SIG_BLOCK, &mask, NULL);


		// Create a new child process with fork()
		pid = fork();

		// fork() gives -1 if error occur
		if (pid < 0)
		{
			printf("Error forking child process\n");
			exit(0);
		}
		// Forked the process to run the program.
		else if (pid == 0)
		{
			// http://www.gnu.org/software/libc/manual/html_node/Launching-Jobs.html
			// Changing process group to its own process group
			setpgid(0, 0);

			// We can now allow the job to be deleted from the list
			// so we deleted SIGCHLD from the set.
			sigprocmask(SIG_UNBLOCK, &mask, NULL);

			// Executing a new program. Execve returns -1 if error otherwise
			// there is no return.
			if (execve(argv[0], argv, environ) < 0)
			{
				// The command doesn't exist, it displays
				printf("%s: Command not found\n", argv[0]);
				fflush(stdout);

				// Closing shell process since the forked process
				// isn't the right command
				exit(0);
			}

		}

		if (bg)
		{
			// Adding process to a job list.
			if (addjob(jobs, pid, BG, cmdline))
			{
				// Unblocking so deletejob() can be performed
				sigprocmask(SIG_UNBLOCK, &mask, NULL);

				// Printing the job id, process id and command line input
				printf("[%d] %d %s", pid2jid(pid), pid, cmdline);
				fflush(stdout);
			}
		}
		else
		{
			// Adding the process to a job list.
			if (addjob(jobs, pid, FG, cmdline))
			{
				// Unblocking so we can do deletejob()
				sigprocmask(SIG_UNBLOCK, &mask, NULL);

				// Go to waitfg where we run our waiting loop
				// for the process to finish
				waitfg(pid);
			}
		}
	}

	return;
}




/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////





/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//



int builtin_cmd(char **argv)
{
    if (strcmp(argv[0], "quit") == 0)
    {
        /* quit */
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(argv[0], "jobs") == 0)
    {
        /* print job list */
        listjobs(jobs);
        return 1;
    }
    else if (strcmp(argv[0], "&") == 0)
    {
        /*ignore singleton &*/
        return 1;
    }
    else if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0)
    {
        /* bg or fg command */
        // Resume stopped background job
        // Call running background job to foreground
        do_bgfg(argv);
        return 1;
    }
    else
    {
        // argv[0] is not a built-in command
        return 0;     /* not a builtin command */
    }
}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
  struct job_t *jobp=NULL;
  //int pid;

  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


int killpid = jobp->pid;
    kill(-killpid,SIGCONT);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGCHLD);

    if(!strcmp(argv[0],"fg"))
    {
        jobp->state = FG;
        sigprocmask(SIG_UNBLOCK,&mask,NULL); /* unblock signal after changing the state */
        waitfg(killpid);
    }
    else if(!strcmp(argv[0],"bg"))
    {
        jobp->state = BG;
        sigprocmask(SIG_UNBLOCK,&mask,NULL);/* unblock signal after changing the state */
        printf("[%d] (%d) %s", jobp->jid,jobp->pid,jobp->cmdline);
    }
    else
        printf("do_bgfg called incorrectly");
    return;
}






/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////





/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//



void waitfg(pid_t pid)
{
	// We use fgpid to get the current fg job in the job list
	// While the proccess id sent to waitfg is the same we loop
	// and sleep for 1 sec, when fgpid returns a different id or 0
	// the proccess has either terminated or stopped so we return.
	
	while (pid == fgpid(jobs))
	{
		sleep(1);   // wait one second
	}
	return;
}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////




/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//


void sigchld_handler(int sig)
{
	pid_t pid;
	int status;

        /* finding a child whose state is changed */

         while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0)
	  {
            
            struct job_t *job = getjobpid(jobs, pid); // Return job struct

            // If user hits ctrl+c or the process terminates suddenly
		   // we should print it out and delete the job


          //WIFSIGNALED(status): Returns true if the child process terminated 
          //because of a signal that was not caught. 
		
		  if (WIFSIGNALED(status))
		  {
            //WTERMSIG(status):Returns the number of the signal that caused the child
            //process to terminate. This status is only dened ifWIFSIGNALED(status) returned true.
			
			printf("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, WTERMSIG(status));
			fflush(stdout);
			deletejob(jobs, pid); // Remove job from the jobs list
		  }
		
		 // If user hits ctrl+z or the process gets SIGTSTP
		 // we but it in ST state and print out info.
         //WIFSTOPPED(status): Returns true if the child that caused the return is currently stopped.
		
		   else if (WIFSTOPPED(status))
		    {
			   job->state = ST; // Put it in ST (stop) state

               //WSTOPSIG(status): Returns the number of the signal that caused the child
              // to stop. This status is only dened if WIFSTOPPED(status) returned true.
			
			  printf("Job [%d] (%d) stopped by signal %d\n", job->jid, pid, WSTOPSIG(status));
			  fflush(stdout);
		    }

                 

            //WIFEXITED(status): Returns true if the child terminated normally, via a
	       //call to exit or a return.
                   
           else if ( WIFEXITED( status ) )  // if exited normaly
		    {
               deletejob( jobs, pid );  // Remove job from the jobs list
            }
    }

    return;
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////





/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//



void sigint_handler(int sig)
{
    pid_t pid;

    /* find foreground job; return if no fg job exists */
    pid = fgpid(jobs);
    if (pid == 0)
    {
        return;
	}

    /* forward signal */
    // Sending the kill command on the proccess group, which invokes
    // an action in sigchld handler who sends an error message as well
    // sending signal SIGINT to foreground process to terminate it
    
    if (kill(-pid, SIGINT) == -1)
    {
        unix_error("Error in killing!!");
	}
}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//


void sigtstp_handler(int sig)
{
    pid_t pid;

    /* find foreground job; return if no fg job exists */
    pid = fgpid(jobs);
    if (pid == 0)
        return;

    /* forward signal */
    // Sending the kill command on the proccess group, which invokes
   // an action in sigchld handler who sends an error message as well
   // sending signal SIGTSTP to foreground process to stop it
    if (kill(-pid, SIGTSTP) == -1)
        unix_error("kill");
}



/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////




/*********************
 * End signal handlers
 *********************/
