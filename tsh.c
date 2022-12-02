/* 
 * tsh - A tiny shell program with job control
 * 
 * <Dhruv Srikanth dhruvsrikanth>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>

/* Misc manifest constants */
#define MAXLINE    1024  /* max line size */
#define MAXARGS     128  /* max args on a command line */
#define MAXJOBS      16  /* max jobs at any point in time */
#define MAXJID    1<<16  /* max job ID */
#define MAXHISTORY  10   /* max history size */
#define MKDIR_MODE  0700 /* mkdir mode */
#define EXIT_SUCCESS 0   /* exit success */
#define EXIT_FAILURE 1   /* exit failure */
#define LOGIN_SUCCESS 0  /* login success */
#define LOGIN_FAILURE 1  /* login failure */

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
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
char *username;             /* The name of the user currently logged into the shell */
char *home;                 /* The home directory of the user currently logged into the shell */
struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

struct stat_t {
    char name[MAXLINE];     /* name of the command */
    pid_t pid;              /* process id */
    pid_t ppid;             /* parent process id */
    pid_t pgid;             /* process group id */
    pid_t sid;              /* session id */
    char state[MAXLINE];     /* state of the process */
    char uname[MAXLINE];    /* user name */
};

char history[MAXHISTORY][MAXLINE];  /* The history list */
pid_t session_id;                   /* The session id of the shell */
volatile sig_atomic_t pid_buf;     
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

char *login();
void usage(void);

void unix_error(char *msg);
void app_error(char *msg);
void reset_state_error(char *msg);
void user_error(char *msg);

typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/* Additional helper functions */
void authenticate(const char *username, const char *password, bool *authenticated);

void exec_builtin(char **argv);

bool are_open_jobs(struct job_t *jobs);

void quit(int sig);
void logout(int sig);
void add_user(char *user_name, char *pwd);
bool user_exists(char *user_name);

void init_history();
void show_history();
int history_length();
void add_to_history(char *cmd);
void write_to_history(char *cmd);
void run_nth_history(char *cmd);
void reset_history();

void shell_stat(struct stat_t *stat);
void make_stat(struct stat_t *stat, pid_t pid, char *cmd);

void create_proc_entry(struct stat_t *stat);
void write_proc_entry(struct stat_t *stat);
void read_proc_entry(struct stat_t *stat, pid_t pid);
void edit_proc_entry(pid_t pid, char *new_state);
void remove_proc_entry(pid_t pid);
void remove_proc_entries();

bool isnum(char *str);
void strrevr(char *str, const int length);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
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

    /* Have a user log into the shell */
    username = login();

    /* Initialize the history of commands used previously by the user */
    init_history();

    /* Create entry proc/PID/status for shell */
    struct stat_t stat;
    shell_stat(&stat);
    create_proc_entry(&stat);
    
    /* Execute the shell's read/eval loop */
    bool just_logged_in = true;
    while (1) {
        /* Read command line */
        if (emit_prompt) {
            if (just_logged_in) {
                just_logged_in = false;
            } else {
                printf("%s", prompt);
            }
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && (ferror(stdin))) {
            app_error("fgets error");
        }
        
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    } 

    free(username);
    free(home);
    remove_proc_entries();
    exit(EXIT_SUCCESS); /* control never reaches here */

}

/*
 * login - Performs user authentication for the shell
 *
 * See specificaiton for how this function should act
 *
 * This function returns a string of the username that is logged in
 */
char *login() {

    bool authenticated = false;
    while (1) {
        /* Get the user's details */ 
        printf("username: ");
        char *username = malloc(sizeof(char) * MAXLINE);
        scanf("%s", username);

        if (strcmp(username, "quit") == 0) {
            quit(LOGIN_FAILURE);
        }
        
        printf("password: ");
        char *password = malloc(sizeof(char) * MAXLINE);
        scanf("%s", password);

        /* Authenticate the validity of the user's entered details */
        authenticate(username, password, &authenticated);

        /* Free memory not used after this */
        free(password);

        /* Try again if information is incorrect otherwise return password */
        if (!authenticated) {
            user_error("User Authentication failed. Please try again.");
        } else {
            return username;
        }
    }

    return NULL;
}

/* Authentication function for verifying username and password in /etc/passwd */ 
void authenticate(const char *username, const char *password, bool *authenticated) {
    /* Open the file */
    FILE *fp;
    fp = fopen("etc/passwd", "r");
    if (fp == NULL) {
        reset_state_error("Could not open etc/passwd file.");
        fclose(fp);
        return;
    }

    /* Read the file one line at a time comparing username and password */
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, fp)) != -1) {
        char *token = strtok(line, ":");
        if (strcmp(token, username) == 0) {
            token = strtok(NULL, ":");
            if (strcmp(token, password) == 0) {
                *authenticated = true;

                token = strtok(NULL, ":");
                /* Get the home directory of the shell for the user logged in */
                home = malloc(sizeof(char) * (strlen(token) + 1));
                /* Remove newline character at the end */
                token[strlen(token) - 1] = '\0';
                strcpy(home, token);
                break;
            }
        }
    }

    /* Free memory not used after this */
    free(line);
    fclose(fp);
}

/* quit - Quit the shell */
void quit(int sig) {
    if (sig == LOGIN_SUCCESS) {
        /* Reset the history */
        reset_history();
    }

    /* Remove session proc entry */
    remove_proc_entry(session_id);

    /* Remove all proc entries */
    remove_proc_entries();

    /* Free memory not used after this */
    free(home);
    free(username);
    exit(EXIT_SUCCESS);
}

/* logout - Logout of the shell */
void logout(int sig) {
    /* Check if any jobs are remaining */
    if (are_open_jobs(jobs)) {
        user_error("There are suspended jobs.");
    } else {
        quit(sig);
    }
}

/* add_user - Add user to the system */
void add_user(char *user_name, char *pwd) {
    /* Check if username and password are valid */
    if (user_name == NULL || pwd == NULL || strlen(user_name) == 0 || strlen(pwd) == 0) {
        sprintf(sbuf, "Invalid username (%s) or password(%s) provided.", user_name, pwd);
        user_error(sbuf);
        return;
    }

    /* Only allow the root user to add new users */
    if (strcmp(username, "root") != 0) {
        user_error("root privileges required to run adduser.");
        return;
    }

    /* Check if user already exists */
    if (user_exists(user_name)) {
        sprintf(sbuf, "User %s may already exist.", user_name);
        user_error(sbuf);
        return;
    }


    /* Create new user directory */
    sprintf(sbuf, "home/%s", user_name);
    if (mkdir(sbuf, MKDIR_MODE) == -1) {
        reset_state_error("Could not create user directory.");
    }

    /* Create .tsh_history file */
    sprintf(sbuf, "home/%s/.tsh_history", user_name);
    FILE *fp;
    fp = fopen(sbuf, "w");
    if (fp == NULL) {
        reset_state_error("Could not create .tsh_history file.");
    }
    fclose(fp);

    
    /* Write to etc/passwd file */
    fp = fopen("etc/passwd", "a");
    if (fp == NULL) {
        reset_state_error("Could not open etc/passwd file.");
        fclose(fp);
        return;
    }

    sprintf(sbuf, "home/%s", user_name);
    const size_t written = fprintf(fp, "%s:%s:%s\n", user_name, pwd, sbuf);
    if (written != strlen(user_name) + strlen(pwd) + strlen(sbuf) + 3) {
        reset_state_error("Could not write to etc/passwd file.");
    }


    fclose(fp);
}

/* user_exitsts - check if a user exists in etc/passwd */
bool user_exists(char *user_name) {
    /* Open the file */
    FILE *fp;
    fp = fopen("etc/passwd", "r");
    if (fp == NULL) {
        reset_state_error("Could not open etc/passwd file.");
        fclose(fp);
        return false;
    }

    /* Read the file one line at a time comparing username */
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, fp)) != -1) {
        char *token = strtok(line, ":");
        if (strcmp(token, user_name) == 0) {
            free(line);
            fclose(fp);
            return true;
        }
    }

    /* Free memory not used after this */
    free(line);
    fclose(fp);
    return false;
}

/* init_history - Initialize the history array with the user's previous commands */
void init_history() {
    /* Get the file details */
    const size_t history_file_size = strlen(home) + 1 + 12; /* Does not include the null terminator */
    char *history_file = malloc(sizeof(char) * (history_file_size + 1));
    sprintf(history_file, "%s/.tsh_history", home);

    /* Open the file */
    FILE *fp;
    fp = fopen(history_file, "r");
    free(history_file);
    if (fp == NULL) {
        sprintf(sbuf, "Could not open %s/.tsh_history file.", home);
        reset_state_error(sbuf);
        fclose(fp);
        return;
    }

    /* Read the file in reverse */
    fseek(fp, 0, SEEK_END);
    long pos = ftell(fp);
    int i = 0;
    
    /* Store the commands */
    char commands[MAXHISTORY][MAXLINE];
    int command_count = 0;
    char command[MAXLINE];
    int command_length = 0;

    /* Add each line to the list of commands */
    while (i < pos && command_count < MAXHISTORY) {
        i++;
        fseek(fp, -i, SEEK_END);
        char c = fgetc(fp);
        if (c == '\n' && command_length > 0) {
            command[command_length] = '\0';
            strrevr(command, command_length);
            if (command_count == 0) {
                command[command_length - 1] = '\0';
            }
            strcpy(commands[command_count], command);
            command_count++;
            command_length = 0;
        } else {
            command[command_length] = c;
            command_length++;
        }
    }
    if (command_count >= MAXHISTORY) {
        command_count = MAXHISTORY;
    } else {
        command[command_length] = '\0';
        strrevr(command, command_length);
        strcpy(commands[command_count], command);
        command_count++;
    }

    /* Free memory not used after this */
    fclose(fp);

    /* Add the commands to the history array */
    for (int j = command_count - 1; j >= 0; j--) {
        add_to_history(commands[j]);
    }
}

/* show_history - Print the history of commands (Max number of commands printed is set to MAXHISTORY = 10)*/
void show_history() {
    /* Most recent command is last */
    printf("History (last 10 commands used from least to most recent):\n");
    for (int i = 0; i < MAXHISTORY; i++) {
        if (strlen(history[i]) != 0) {
            printf("%d. %s\n", i + 1, history[i]);
        }
    }
}

/* history_length - Number of commands present in the history */
int history_length() {
    int count = 0;
    for (int i = 0; i < MAXHISTORY; i++) {
        if (strlen(history[i]) == 0) {
            break;
        }
        count += 1;
    }
    return count;
}

/* write_to_history - Write command to history file */
void write_to_history(char *cmd) {
    /* Preprocess the command */
    if (cmd[strlen(cmd) - 1] == '\n') {
        cmd[strlen(cmd) - 1] = '\0';
    }
    
    /* Check for ! since it should not be written */
    if (cmd[0] == '!') {
        return;
    }

    /* Get the file details */
    const size_t history_file_size = strlen(home) + 1 + 12; /* Does not include the null terminator */
    char *history_file = malloc(sizeof(char) * (history_file_size + 1));
    sprintf(history_file, "%s/.tsh_history", home);

    /* Open the file */
    FILE *fp;
    fp = fopen(history_file, "a");

    /* Free up memory not used */
    free(history_file);

    if (fp == NULL) {
        sprintf(sbuf, "Could not open %s/.tsh_history file.", home);
        reset_state_error(sbuf);
        fclose(fp);
        return;
    }

    /* Write the command to the file as a new line */
    const size_t written = fprintf(fp, "%s\n", cmd);
    if (written != strlen(cmd) + 1) {
        reset_state_error("Could not write to history file.");
    }
    fclose(fp);
    
    /* Add to history */
    add_to_history(cmd);
}

/* run_nth_history - Run the Nth command in the history file */
void run_nth_history(char *cmd) {
    /* Get the number of the command to run (subtract 1 for 0 indexing) */
    int n;
    sscanf(cmd, "!%d", &n);
    const int h_length = history_length();
    if (n > h_length) {
        sprintf(sbuf, "Called command %d from history, however only %d commands present in history.", n, h_length);
        reset_state_error(sbuf);
        return;
    } else if (n < 1) {
        sprintf(sbuf, "Called command %d from history, however the number must be greater than 0.", n);
        reset_state_error(sbuf);
        return;
    }
    n -= 1;

    char command[MAXLINE];
    strcpy(command, history[n]);
    /* Add newline to simulate entry by user */
    strcat(command, "\n");
    eval(command);
    return;
}

/* add_to_history - Add command to history file */
void add_to_history(char *cmd) {
    /* Add to history */
    bool added = false;
    for (int i = 0; i < MAXHISTORY; i++) {
        if (strlen(history[i]) == 0) {
            strcpy(history[i], cmd);
            added = true;
            break;
        }
    }

    /* If the history is full, shift the history down and add the command */
    if (!added) {
        for (int i = 0; i < MAXHISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[MAXHISTORY - 1], cmd);
    }
}

/* reset_history - Reset the .tsh_history file */
void reset_history() {
    /* Get the file details */
    const size_t history_file_size = strlen(home) + 1 + 12; /* Does not include the null terminator */
    char *history_file = malloc(sizeof(char) * (history_file_size + 1));
    sprintf(history_file, "%s/.tsh_history", home);

    /* Open the file */
    FILE *fp;
    fp = fopen(history_file, "w");

    /* Free up memory not used */
    free(history_file);

    if (fp == NULL) {
        sprintf(sbuf, "Could not open %s/.tsh_history file.", home);
        reset_state_error(sbuf);
        fclose(fp);
        return;
    }

    /* Write all history commands to the file as a new line */
    size_t written;
    for (int i = 0; i < MAXHISTORY; i++) {
        if (strlen(history[i]) != 0) {
            written = fprintf(fp, "%s\n", history[i]);
            if (written != strlen(history[i]) + 1) {
                reset_state_error("Could not write to history file.");
            }
        }
    }
    fclose(fp);
}

/* shell_stat - Create shell stat struct */
void shell_stat(struct stat_t *stat) {
    strcpy(stat->name, "tsh");
    stat->pid = getpid();
    stat->ppid = getppid();
    stat->pgid = stat->pid;
    stat->sid = stat->pid;
    strcpy(stat->state, "Rs");
    strcpy(stat->uname, username);

    session_id = stat->sid;
}

/* get_stat - Create stat struct for process */
void get_stat(struct stat_t *stat, pid_t pid, char *cmd) {
    /* Get the process details */
    strcpy(stat->name, cmd);
    stat->pid = pid;
    stat->ppid = getppid();
    stat->pgid = getpgid(pid);
    stat->sid = session_id;
    strcpy(stat->state, "Ss");
    strcpy(stat->uname, username);
}

/* write_to_proc - Write to proc/PID/status */
void create_proc_entry(struct stat_t *stat) {
    /* Get the folder details */
    char proc_dir[MAXLINE];
    sprintf(proc_dir, "proc/%d", stat->pid);

    /* Create the folder */
    if (mkdir(proc_dir, MKDIR_MODE) == -1) {
        sprintf(sbuf, "Could not create folder %s.", proc_dir);
        reset_state_error(sbuf);
        return;
    }

    /* Write to the file */
    write_proc_entry(stat);
}

/* write_proc_entry - Write to proc/PID/status */
void write_proc_entry(struct stat_t *stat) {
    /* Get the file details */
    char proc_file[MAXLINE];
    sprintf(proc_file, "proc/%d/status", stat->pid);

    /* Open the file */
    FILE *fp;
    fp = fopen(proc_file, "w");
    if (fp == NULL) {
        sprintf(sbuf, "Could not open %s file.", proc_file);
        reset_state_error(sbuf);
        fclose(fp);
        return;
    }

    sprintf(sbuf, "Name: %s\nPid: %d\nPPid: %d\nPGid: %d\nSid: %d\nSTAT: %s\nUsername: %s\n", stat->name, stat->pid, stat->ppid, stat->pgid, stat->sid, stat->state, stat->uname);
    const size_t written = fprintf(fp, "%s", sbuf);
    if (written != strlen(sbuf)) {
        reset_state_error("Could not write to proc/PID/status file.");
    }
    fclose(fp);
}
    
/* read_proc_entry - Read proc entrt in proc/PID/status */
void read_proc_entry(struct stat_t *stat, pid_t pid) {
    /* Get the file details */
    char proc_file[MAXLINE];
    sprintf(proc_file, "proc/%d/status", pid);

    /* Open the file */
    FILE *fp;
    fp = fopen(proc_file, "r");
    if (fp == NULL) {
        sprintf(sbuf, "Could not open %s file.", proc_file);
        reset_state_error(sbuf);
        fclose(fp);
        return;
    }

    /* Read the file */
    fscanf(fp, "Name: %s\nPid: %d\nPPid: %d\nPGid: %d\nSid: %d\nSTAT: %s\nUsername: %s\n", stat->name, &stat->pid, &stat->ppid, &stat->pgid, &stat->sid, stat->state, stat->uname);
    fclose(fp);
}

/* edit_proc_entry - Edit proc entry in proc/PID/status */
void edit_proc_entry(pid_t pid, char *new_state) {
    /* Get the file details */
    char proc_file[MAXLINE];
    sprintf(proc_file, "proc/%d/status", pid);

    /* Read the current files contents into a stat struct */
    struct stat_t stat;
    read_proc_entry(&stat, pid);

    /* Edit the state */
    strcpy(stat.state, new_state);

    /* Write the new state to the file */
    write_proc_entry(&stat);
}

/* remove_proc_entry - Remove a specific proc entry in proc/PID/status */
void remove_proc_entry(pid_t pid) {
    /* Get the file details */
    char proc_file[MAXLINE];
    sprintf(proc_file, "proc/%d/status", pid);

    /* Remove the file */
    if (remove(proc_file) == -1) {
        sprintf(sbuf, "Could not remove %s file.", proc_file);
        reset_state_error(sbuf);
        return;
    }

    /* Remove the folder */
    char proc_dir[MAXLINE];
    sprintf(proc_dir, "proc/%d", pid);
    if (rmdir(proc_dir) == -1) {
        sprintf(sbuf, "Could not remove %s folder.", proc_dir);
        reset_state_error(sbuf);
        return;
    }
}

/* remove_proc_entries - Remove all proc entries in proc/PID/status */
void remove_proc_entries() {
    /* Get the folder details */
    char proc_dir[MAXLINE];
    sprintf(proc_dir, "proc");

    /* Get all pids in proc/ */
    struct dirent *de;
    DIR *dr = opendir(proc_dir);
    if (dr == NULL) {
        sprintf(sbuf, "Could not open %s folder.", proc_dir);
        reset_state_error(sbuf);
        return;
    }

    while ((de = readdir(dr)) != NULL) {
        /* Check if the file is a directory */
        if (de->d_type == DT_DIR) {
            /* Check if the directory is a pid */
            if (isnum(de->d_name)) {
                /* Remove the proc entry */
                remove_proc_entry(atoi(de->d_name));
            }
        }
    }

    closedir(dr);
}

/* isnum - Check whether a string is a number */
bool isnum(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

/* strrev - Reverse a string (in-place) */
void strrevr(char *str, const int length) {
    char c;
    for (int i = 0; i < length / 2; i++) {  
        c = str[i];  
        str[i] = str[length - i - 1];  
        str[length - i - 1] = c;  
    }  
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
void eval(char *cmdline) {
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXARGS];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */

    /* Parse the command line */
    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    if (argv[0] == NULL) {
        return;   /* Ignore empty lines */
    }

    /* Add command to history and .tsh_history */
    write_to_history(buf);

    /* Fork child process as job if the command is not a built-in command */
    if (!builtin_cmd(argv)) {
        if ((pid = fork()) == 0) {   /* Child runs user job */
            /* Set the process group ID */
            if (setpgid(0, 0) == -1) {
                reset_state_error("Could not set process group ID.");
            }

            /* Write to proc/PID/status */
            struct stat_t stat;
            get_stat(&stat, getpid(), argv[0]);
            create_proc_entry(&stat);

            /* Execute the command */
            if (execve(argv[0], argv, environ) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(EXIT_SUCCESS);
            }
        }

        /* Parent waits for foreground job to terminate */
        if (!bg) {
            int status;
            if (waitpid(pid, &status, 0) < 0) {
                unix_error("waitfg: waitpid error");
            }
        } else {
            printf("%d %s", pid, cmdline);
        }
    } else {
        /* If the command is a built-in command, execute it immediately in the foreground */
        exec_builtin(argv);
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) { /* ignore leading spaces */
        buf++;
    }

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
	    delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
	    while (*buf && (*buf == ' ')) { /* ignore spaces */
	       buf++;
        }

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        } else {
            delim = strchr(buf, ' ');
        }
    }
    
    argv[argc] = NULL;
    
    if (argc == 0) {  /* ignore blank line */
        return 1;
    }

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
int builtin_cmd(char **argv) {
    /* Built-in commands */
    const int n_builtins = 7;
    const char *builtins[n_builtins] = {"quit", "logout", "history", "bg", "fg", "jobs", "adduser"};
    for (int i = 0; i < n_builtins; i++) {
        if (strcmp(argv[0], builtins[i]) == 0) {
            return 1;
        }
    }
    
    /* Check for !N command */
    if (argv[0][0] == '!') {
        for (int i = 1; i < strlen(argv[0]); i++) {
            if (!isdigit(argv[0][i])) {
                return 0;
            }
        }
        return 1;
    }
    
    return 0;     /* not a builtin command */
}

/* 
 * exec_builtin - Execute the built-in command
 */
void exec_builtin(char **argv) {
    if (strcmp(argv[0], "quit") == 0) {
        quit(LOGIN_SUCCESS);
    } else if (strcmp(argv[0], "logout") == 0) {
        logout(LOGIN_SUCCESS);
    } else if (strcmp(argv[0], "history") == 0) {
        show_history();
    } else if (argv[0][0] == '!') {
        run_nth_history(argv[0]);
    } else if (strcmp(argv[0], "bg") == 0) {
        printf("bg\n");
    } else if (strcmp(argv[0], "fg") == 0) {
        printf("fg\n");
    } else if (strcmp(argv[0], "jobs") == 0) {
        listjobs(jobs);
    } else if (strcmp(argv[0], "adduser") == 0) {
        add_user(argv[1], argv[2]);
    }
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
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
    // if (sig == SIGCHLD) {
    //     int olderrno = errno;
    //     sigset_t mask_all, prev_all;
    //     pid_t pid;
    //     int status;
    //     sigfillset(&mask_all);
    //     while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) { /* Reap all children */
    //         /* Block all signals */
    //         sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

    //         /* Did the child process exit normally */
    //         if (WIFEXITED(status)) {
    //             deletejob(jobs, pid);
    //         } else if (WIFSIGNALED(status)) { /* Was the child process signalled to exit */
    //             deletejob(jobs, pid);
    //         } else if (WIFSTOPPED(status)) { /*Was the child process stopped and if so, maintain the job but update the state */
    //             getjobpid(jobs, pid)->state = ST;
    //         }

    //         /* Unblock all signals */
    //         sigprocmask(SIG_SETMASK, &prev_all, NULL);
    //     }

    //     if (errno != ECHILD) {
    //         unix_error("waitpid error");
    //     }
    //     errno = olderrno;
    // }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    if (sig == SIGINT) {
        int olderrno = errno;
        pid_t pid = fgpid(jobs);
        if (pid != 0) {
            kill(-pid, SIGINT);
        }
        errno = olderrno;
    }
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
    if (sig == SIGTSTP) {
        int olderrno = errno;
        pid_t pid = fgpid(jobs);
        if (pid != 0) {
            kill(-pid, SIGTSTP);
        }
        errno = olderrno;
    }
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

/* are_open_jobs - Check if any jobs are left to be completed */
bool are_open_jobs(struct job_t *jobs) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].state != UNDEF) {
            return true;
        }
    }
    return false;
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    
    if (pid < 1) {
        return 0;
    }

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS) {
                nextjid = 1;
            }
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }

    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
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
struct job_t *getjobjid(struct job_t *jobs, int jid) {
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
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(EXIT_SUCCESS);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
* reset_state_error - reset the state of the shell
*/
void reset_state_error(char *msg) {
    fprintf(stdout, "Error: %s\n", msg);
}

/* user_error - Raised when user makes error */
void user_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
        unix_error("Signal error");
    }
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(EXIT_FAILURE);
}

