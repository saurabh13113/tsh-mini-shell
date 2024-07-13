/* 
 * tsh - A tiny shell program with job control
 * 
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
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* Per-job data */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, FG, BG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

volatile sig_atomic_t ready; /* Is the newest child in its own process group? */

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);
void sigusr1_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int freejid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != -1) {
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

    Signal(SIGUSR1, sigusr1_handler); /* Child is ready */

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
    } 

    exit(0); /* control never reaches here */
}

void execpipeline(char **commands, int num_cmds, sigset_t *masker, char *cmdline) { 
    int piper[2];
    int fdfla = -1;
    int c1 = 0;
    int c2 = -1;
    char *pipcmds[MAXARGS][MAXARGS];
    
    // Checker for foreground or backgroudn job
    int bgflag = 0;
    if (strcmp(commands[num_cmds - 1],"&") == 0){bgflag = 1;}

    // Make a secondary commands list[argv 2.0] for ease in pipeline
    // execution
    for (int j= 0; j < num_cmds; j++) {
        if (!strcmp(commands[j], "|") == 0) {c2 ++;
            pipcmds[c1][c2] = commands[j];} 
        else {c2 = -1; c1 ++;}
    } 
    c1 = c1 + 1;
    
    //Evaluating list of commands as per pipes
    int k = 0;
    while (k < c1) {
        if (pipe(piper) != 0) {printf("Piping Error"); 
            exit(1);}
            
        pid_t pid = fork();
        // Child progess
        if (pid == 0) {
            
            // To redirect the standard output on condition
            if (k + 1 < c1) {dup2(piper[1], STDOUT_FILENO);}
            // To redirect the standard input on condition
            if (fdfla != -1) {dup2 (fdfla, STDIN_FILENO);
                close (fdfla);}

            // Execute the command, raise error for improper execution
            if (execvp (pipcmds[k][0], pipcmds[k]) < 0) {
                fprintf(stderr, "Command failure: %s\n", pipcmds[k][0]);
                exit(1);
            }
            close(piper[0]);}

        // If forking not done right and properly
        else if (pid < 0) { perror("fork"); exit(1);} 

        // If parent instead of child
        else {
            if (fdfla != -1) {close (fdfla);}

            // Proper closing of the pipe and redirections
            fdfla = piper[0];
            close(piper[1]);
             
            // Work needed for a background or foreground process
            sigprocmask(SIG_SETMASK, masker, NULL);
            if (!bgflag) {// Foreground job
                addjob(jobs, pid, FG, cmdline);
                waitfg(fgpid(jobs));} 
            else {// Background job
                addjob(jobs, pid, BG, cmdline);
                printf("[%d] (%d) %s", pid2jid(pid), pid, getjobpid(jobs,pid) -> cmdline);
            }
        }
    k++;
    }
}
  
void ioredirection(char **argv){
    int input_f = -1; // Needed for input redirection
    int output_f = -1; // Needed for output redirection
    for (int i = 0; argv[i] != NULL; i++) {

        // When input redirection is needed
        if (strcmp(argv[i], "<") == 0) {
            input_f = open(argv[i + 1], O_RDONLY);
            if (input_f < 0) { 
                perror("Input redirection error");
                exit(1);
            }
            dup2(input_f, STDIN_FILENO);
            //Redirect stdint into the input file opened
            close(input_f);
            argv[i] = NULL; // Remove the redirection for later work
            }
        
        // When output redirection is needed
        else if (strcmp(argv[i], ">") == 0) {
            output_f = open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_f < 0) {
                perror("Output redirection error");
                exit(1);
            }
            dup2(output_f, STDOUT_FILENO); 
            //Redirect stdout into the output file opened
            close(output_f);
            argv[i] = NULL; // Remove the redirection for later work
        }
}}
  
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
void eval(char *cmdline) {
    char **argv = malloc(sizeof(char *) * MAXARGS); 
    int bgrt; 
    pid_t pid; 
    int bgflag = 0; // Background or foreground?
    int pipeflag = 0; // Flag for if pipes are there or not?
    //char *commandls[MAXLINE];
    sigset_t masker, masker2;
    
    bgrt = parseline(cmdline, argv);
    
    // Go through the list and check for a pipe, to raise pipeflag
    for (int i=0; argv[i] != NULL; i++){
        if (strcmp(argv[i], "|") == 0){pipeflag = 1;}}

    // Incase no arguments are returned[Commandline is empty]
    if (argv[0] == NULL){return;}
    
    // Work for pipes being done here, through an external command
    else if (pipeflag){
        sigemptyset(&masker);
        sigaddset(&masker, SIGCHLD);
        sigaddset(&masker, SIGINT);
        sigaddset(&masker, SIGTSTP);
        sigprocmask(SIG_BLOCK, &masker, &masker2);
        execpipeline(argv, bgrt, &masker2, cmdline);}

    // If a built in commdand is found, this is addressed immediately
    else if (builtin_cmd(argv)){
        return;
    }
    else {

        // Block the SIGCHLD,SIGTSTP,SIGINT signals as needed,
        // Note that these will be unblocked later as needed
        sigemptyset(&masker);
        sigaddset(&masker, SIGCHLD);
        sigaddset(&masker, SIGTSTP);
        sigaddset(&masker, SIGINT);
        sigprocmask(SIG_BLOCK, &masker, NULL);

        if (strcmp(argv[bgrt - 1],"&") == 0){bgflag = 1;}

        pid = fork();
        if (pid == 0) { // Child process
            sigprocmask(SIG_UNBLOCK, &masker, NULL);
            setpgid(0, 0);

            // Work done for input/output redirection if needed, 
            // is handled here
            ioredirection(argv);
            
            //In case a background process was raised, in order to
            //add delimiter/null terminator back
            if (bgflag){argv[bgrt-1] = '\0';}
            
            //Command execution
            if (execvp(argv[0], argv) < 0) { 
                printf("%s: Command not found.\n", argv[0]);
                exit(1);
            }
        }

        // Forking done incorrectly
        else if (pid < 0) {
            printf("Forking error.\n");
            return;}

        // Parent process
        // Signals are unblocked here as was said earlier
        setpgid(pid,pid);
        sigprocmask(SIG_UNBLOCK, &masker, NULL);
        Signal(SIGTSTP,sigtstp_handler);
        Signal(SIGINT,sigint_handler);
        
        if (!bgflag) {// Background job
            addjob(jobs, pid, FG, cmdline);
            waitfg(fgpid(jobs));
            } 
        else {// Foreground job
            addjob(jobs, pid, BG, cmdline);
            printf("[%d] (%d) %s", pid2jid(pid), pid, getjobpid(jobs,pid) -> cmdline);
            }
        }
    return;
    }
 
/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return number of arguments parsed.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to space or quote delimiters */
    int argc;                   /* number of args */

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
    
    return argc;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
    char* input = argv[0];

    if (strcmp(input, "quit") == 0){ //exit clause
        exit(0);
    }
    else if ((strcmp(input, "bg") == 0) || strcmp(input, "fg") == 0){ // fg or bg (as they both call the same function and the function differentiates)
        do_bgfg(argv);
        return 1;
    }
    // else if (strcmp(input, "fg") == 0){ // fg
    //     do_bgfg(argv);
    //     return 1;
    // }
    else if (strcmp(input, "jobs") == 0){ //list jobs
        listjobs(jobs);
        return 1;
    }

    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    // blank job_t variable
    struct job_t * curr = NULL;
    int job = 0;
    int jid = 0;
    int pid = 0;
    if (argv[1] == NULL){
        if (strcmp(argv[0], "bg") == 0){
            printf("bg: command requires PID or %%jid argument\n");
        }
        else if (strcmp(argv[0], "fg") == 0){
            printf("fg: command requires PID or %%jid argument\n");
        }
        return;
    }

    if (argv[1][0] == '%'){
        // makes a jid from the second argument
        jid = strtol(argv[1]+1, NULL, 10);
        curr = getjobjid(jobs, jid);
        if (curr == NULL){
            printf("%%%d: No such job\n", jid);
            return;
        }
        pid = curr->pid;
        job = 1;
    }
    else if (strtol(argv[1], NULL, 10)){
        // makes a pid from the second argument
        pid = strtol(argv[1], NULL, 10);
        curr = getjobpid(jobs, pid);
    }

    char* str = argv[1];
    for (; *str != '\0'; str++) {
        if (*str == '%') {
            continue;
        }
        if (*str < '0' || *str > '9') {
            if (strcmp(argv[0], "bg") == 0){
                printf("bg: argument must be a PID or %%jid\n");
                return;
            }
            else if (strcmp(argv[0], "fg") == 0){
                printf("fg: argument must be a PID or %%jid\n");
                return;
            }
            return;
        }
    }

    if (curr == NULL) {
        if (job){
            printf("%%%d: No such job\n", jid);
        }
        else{
            printf("(%d): No such process\n", pid);
        }
        return;
    }
    else {
        if (strcmp(argv[0], "bg") == 0){
            if (curr->state==ST){
                //printf("HERE BG2");
                curr->state=BG;
                kill(curr->pid, SIGCONT);
            }
            printf("[%d] (%d) %s", curr->jid, curr->pid, curr->cmdline);
            // kill(curr->pid, SIGCONT);
            return;
        }
        else {
            if (curr->state==ST){
                //waitfg(pid);
                curr->state=FG;
                kill(-(curr->pid), SIGCONT);
                waitfg(pid);
                }
            else if (curr->state==BG){
                // waitfg(pid);
                //waitfg(fgpid(jobs));
                curr->state=FG;
                kill(-(curr->pid), SIGCONT);
                waitfg(fgpid(jobs));
            }
            // printf("[%d] (%d) %s", curr->jid, curr->pid, curr->cmdline);
            // fflush(stdout);
            // kill(curr->pid, SIGCONT);
            // waitfg(pid);
            return;
        }
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask); // Block SIGCHLD
    // printf("506\n");
    // printf("\n\n %d   %d \n", pid, fgpid(jobs));
    // fflush(stdout);
    while (pid == fgpid(jobs)) {
        // printf("508\n");
        // printf("\n %d   %d  in the loop\n", pid, fgpid(jobs));
        // fflush(stdout);
        sigsuspend(&prev_mask); // Wait for SIGCHLD to be received

    }

    // printf("511\n");
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    //sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {
    int sta; // Status variable to keep track of the status of each child; whether terminated, signaled or stopped. 
    pid_t group_pid;  // pid_t variable (unsigned int) to keep track of the pid's returned of each child process in the loop.

    while ((group_pid = waitpid(-1, &sta, WNOHANG | WUNTRACED)) > 0){ //Adapted from textbook, WNOHANG option is used since 
            //we do not wait for currently running children to temrination.
        struct job_t* job_handle = getjobpid(jobs, group_pid); // Used to have refernce to job whose status is to change from fg -> bg.

        if(WIFSIGNALED(sta)){
            printf("Job [%d] (%d) terminated by signal 2\n",pid2jid(group_pid) ,group_pid); 
            deletejob(jobs, group_pid);
        }   
        else if(WIFEXITED(sta)){
            deletejob(jobs, group_pid); // Delete the job as it is now done and complete, no need for it to occupy space in the job list. 
        }
        else if(WIFSTOPPED(sta)){
            printf("Job [%d] (%d) stopped by signal 20\n", pid2jid(group_pid), group_pid);
            fflush(stdout);
            (*job_handle).state = ST; // Chnage state as job/process is now stopped and sent to the background processes.
        }
        // printf("572\n");

    }
    
    return;
    }

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    //kill(0, SIGINT);
    pid_t curr = fgpid(jobs);
    if (curr != 0){
        kill(-curr, SIGINT);
    }
    return;
    }

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
    // printf("575\n");
    pid_t curr = fgpid(jobs);
    if (curr != 0){ 
        getjobpid(jobs, curr)->state=ST;
        kill(-curr, SIGTSTP);
    }
    return;
}

/*
 * sigusr1_handler - child is ready
 */
void sigusr1_handler(int sig) {
    ready = 1;
}


/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* freejid - Returns smallest free job ID */
int freejid(struct job_t *jobs) {
    int i;
    int taken[MAXJOBS + 1] = {0};
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid != 0) 
        taken[jobs[i].jid] = 1;
    for (i = 1; i <= MAXJOBS; i++)
        if (!taken[i])
            return i;
    return 0;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    
    if (pid < 1)
        return 0;
    int free = freejid(jobs);
    if (!free) {
        printf("Tried to create too many jobs\n");
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = free;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    return 0; /*suppress compiler warning*/
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
    }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG: 
                    printf("Running ");
                    break;
                case FG: 
                    printf("Foreground ");
                    break;
                case ST: 
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", 
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message and terminate
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}