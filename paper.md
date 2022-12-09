# The Tiny Shell (tsh)

The Tiny Shell (`tsh`) is a simple shell program.

## Features

`tsh` supports the following features:

1. Command Evaluation - A command line interface that accepts user input and executes commands. Built-in commands include: 
    1. `quit` - exits the shell
    2. `logout` - logs the user out of the shell
    3. `adduser` - adds a new user to the system (requires root privileges)
    4. `history` - lists the last 10 commands executed
    5. `!N` - executes the Nth command from the history
    6. `jobs` - lists all background jobs
    7. `bg` - resumes a background job
    8. `fg` - resumes a background job in the foreground

The user may also execute any other command that is available on the system as a runnable script by spawning a child process.


2. Job Control - The shell supports running jobs in the background and foreground. The shell also supports suspending (`ctrl-z`), terminating (`ctrl-c`) and resuming jobs. The shell also supports the `jobs` command to list all background jobs and the `bg` and `fg` commands to resume a background job in the background or foreground respectively.

3. Signal Handling - The shell supports the following signals:
    1. `SIGINT` - terminates the foreground process
    2. `SIGTSTP` - suspends the foreground process
    3. `SIGCHLD` - handles the termination of a child process

4. Process File Management - The shell can run any command that is available on the system as a runnable script. In running such commands that are not built-in, the shell creates a folder in the `proc` directory for each process that is spawned, where the folder name is the process `pid` and contains a `status` file containing the following fields that are changed as the state of the process changes:
    1. `Name` - the command associated with the process
    2. `Pid` - the process id
    3. `PPid` - the parent process id
    4. `PGid` - the process group id
    5. `Sid` - the session id
    6. `STAT` - the process state
    7. `Username` - the username of the user that spawned the process

A struct `stat` is used to store this information when it is to be written to the `status` file. 

5. User Management - The shell supports multiple users along with the `root` user. The `root` user has the ability to add new users to the system. All built-in commands are restricted to a particular user. The `adduser` command can only be executed successfully by the `root` user.

## Functionality and Implementation - 

In this section, we will discuss the implementation of the various features of the shell. All code referenced can be found within `tsh.c`.

### Login

The shell starts by prompting the user for a username and password. The shell then checks if the user exists in the system and if the password entered is matches the password stored in the system for that particular user. If the user does not exist or the password does not match, the shell returns an error message stating that the user authentication has failed and the user must try again as shown below - 

```console
User Authentication failed. Please try again.
```

If the username and password are correct, the shell displays the prompt as shown below - 

```console
tsh>
```

Once a user is logged in to the shell, a history of the last 10 commands executed by the user is stored in the `<user directory>/.tsh_history` file. This file contains at most 10 entries (1 on each line) and the data is loaded in to a `history` array. Additionally, once the user logs in to the shell, the shell creates a folder in the `proc` directory for the shell process itself. This is done by creating a `stat` struct with the information described in the Process File Management section and creating the `status` file using the `create_proc_entry()` function. The shell `stat` struct is created using the `shell_stat()` function. The shell process is **not** added to the `jobs` array.

While entering the username, if the user enters the command `quit` the shell exits. The username and password data is stored in the `etc/passwd` file. The `etc/passwd` file is a text file that contains the following fields (separated by `:`) for each user -
1. username
2. password
3. user directory

New users can be added to the system using the `adduser` command. Given below is the usage for the `adduser` command - 

```console
tsh> adduser <user_name> <password>
```

When a new user is added to the system, the shell creates a new folder in the `home` directory with the name of the user and creates a `.tsh_history` file in the user directory to store a history of at most 10 most recently executed commands by that user (in the future). The user is then added to the `etc/passwd` file in the format described above.

**Only the root user can add new users to the system.** If a user who is not the root tries to add a user to the system, the shell displays the following error message - 

```console
root privileges required to run adduser.
```

If the root tries to add a user that already exists (i.e. the username is already present in the `etc/passwd` file), the shell displays the following error message - 

```console
User <user_name> may already exist.
```

where the `user_name` refers to the username entered by the user in the `adduser` command.

### Command Evaluation

The shell evaluates the commands entered by the user using the `eval()` function. This function first parses the text entered by the user in the command line using the `parseline()` function. This function determines whether the command should run in the background or foreground and creates the `argv` array that contains the command and its arguments. It then checks if the command to be executes is valid i.e. not an empty line. Following this, it writes the command to the `.tsh_history` file. After doing so, it checks if the command is a built-in command. If it is, the shell executes the built-in command **without spawning a new process** and in the **foreground**. Therefore, no `proc` entery needs to be created for built-in commands. If the command is not a built-in command, the shell starts by blocking the `SIGCHLD` signal to prevent the shell from handling the termination of the child process before it is spawned. The shell then forks a child process and the child process executes the command. Before the child process is told to execute the command, the `SIGCHLD` is unblocked, so that the child process can be terminated by the shell if it is terminated by the user. In addition to this, before the child process executes the command, the shell is placed in a new proces group to prevent it from being terminated if the child process is terminated by the user (i.e. `ctrl-c`) and a `proc` entry is created with the `pid` of the child process spawned. Before I explain the next step, it is important to mention the global variable `volatile sig_atomic_t fg_pid` that represents the foreground pid i.e. the pid of the process currently running in the foregound process group. If the command is to be executed in the foreground, the child process sets this to 0 inorder to make the shell wait for the foreground process to complete (which happens inside the `waitfg()` function). As this is done by the child, the parent blocks all signals, adds the job to the job queue (which is a global data structure that contains structs of jobs) and then unblocks all signals. This blocking and unblocking is done to prevent other processes from accessing the shared global data structure i.e. the job queue. After this, if the command is to be executed in the foreground, the shell waits for the foreground process to complete using the `waitfg()` function. If the command is to be executed in the background, the shell does not wait for the background process to complete and instead displays the `tsh>` prompt for the user to enter the next command. The `waitfg()` function used waits until the global variable `fg_pid` is set back to the `pid` of the child process spawned and until the calls `sigsuspend` instead of `sleep(1)` as this is wasteful of `CPU` resources.

### Built-in Commands

The built-in commands supported are the following - 

1. `quit` - This command exits the shell. While doing so, it determines whether the user is quitting while logging in or after logging in. This is done by using a contant `LOGIN_SUCCESS`. If the user is quitting while logging in, the shell removes all remianing entries in the `proc` folder if there are any and exits. If the user is quitting after logging in, the shell does the same, however, in addition, it rewrites the `.tsh_history` file to the 10 most recent commands entered by the user.

2. `logout` - This command enables the user to logout of the shell. The command first checks whether there are any remaining jobs running or suspended. It does this by checking for any entries in the `jobs` global array. If there are any, the shell displays the following error message - 

```console
There are suspended jobs.
```

The user must then either wait for the jobs to finish or terminate them. After which, the user can run the `logout` command again to logout of the shell. If there are no jobs running or suspended, the shell removes the entry in the `proc` folder for the shell process and exists by calling the `quit` command.

3. `adduser` - This command adds a new user to the system (requires root privileges). Please refer to the `Login` section for details on how this command works as it has been described in detail there.

4. `history` - This command lists the 10 most recent commands entered by the user with the most recent being the command with the higher listing number i.e. the 10th command shown in the output has been run more recently then the 7th command shown. As the commands are loaded in the global array `history` from `home/<user>/.tsh_history` at the time of initialization, this will hold entries (if required) from past logins by the same user. The commands are displayed by iterating over the global `history` array and printing the command and its listing number.

5. `!N` - This command executes the Nth command in the history. `N` can range from 1 to 10. If the number entered is not within this range, the shell displays an error stating that the number entered is not in the correct range. In addition, as per the specification, these commands are not added to the history i.e. !1 if entered will not show up in the history array or in the `home/<user>/.tsh_history` file. The command is run using the `run_nth_history()` function which checks if N is within the correct range, obtains the command by indexing the global `history` array with N and then executes the command using the `eval()` function.

6. `jobs` - This command lists all jobs that are currently running or suspended. The jobs are listed in the order in which they were added to the job queue. This is done by iterating over the global `jobs` array and printing the job details.

7. `bg` - This command resumes a suspended job in the background. The command takes the `<jid>` (job ID) or `<pid>` as an argument. This is done by calling the `do_bgfg()` function. In this function we first determine whether the number entered corresponds to the `jid` or `pid`. This is done by checking the return of `pid2jid()`. If this is 0 then we know that the number entered is the `pid` and if it is not 0, then we know that the number entered is the `jid`. We obtain the job corresponding to the number entered from the global jobs list using the `getjobpid()` or `getjobjid()` depending on the type of id entered. The allowed job state transitions are shown after the `fg` command below. We check if the transition requested is allowed and if not we display an error message to the user stating why it is not allowed. If the transition is allowed, we modify the job struct that we obtained by changing its state from `ST` to `BG` and then send a `SIGCONT` signal to the process corresponding to the job. We also modify the corresponding entry in the `proc` folder to reflect the change in state using the `edit_proc_entry()` function which reads the file into a buffer, modifies the buffer and then writes the buffer back to the file We pass in the appropriate new state to this fuction i.e. `R` since it is running in the foreground group..

8. `fg` - This command resumes a suspended job in the foreground. The command takes the `<jid>` (job ID) or `<pid>` as an argument. This is done by calling the `do_bgfg()` function. In the same way as described above, we determine whether the number entered corresponds to the `jid` or `pid`. We obtain the job corresponding to the number entered from the global jobs list using the `getjobpid()` or `getjobjid()` depending on the type of id entered. The allowed job state transitions are shown below. We check if the transition requested is allowed and if not we display an error message to the user stating why it is not allowed. If the transition is allowed we modify the job struct that we obtained by changing its state from `ST` or `BG` to `FG` and then send a `SIGCONT` signal to the process corresponding to the job. We also modify the corresponding entry in the `proc` folder to reflect the change in state using the `edit_proc_entry()` function which reads the file into a buffer, modifies the buffer and then writes the buffer back to the file. We pass in the appropriate new state to this fuction i.e. `R+` since it is running in the foreground group.

```
Jobs states: FG (foreground), BG (background), ST (stopped)
Job state transitions and enabling actions:
    FG -> ST  : ctrl-z
    ST -> FG  : fg command
    ST -> BG  : bg command
    BG -> FG  : fg command
```

### Proc

As mentioned above, the shell can run any command that is available on the system as a runnable script. In running such commands that are not built-in, the shell creates a folder in the `proc` directory for each process that is spawned, where the folder name is the process `pid` and contains a `status` file containing the following fields that are changed as the state of the process changes:
    1. `Name` - the command associated with the process
    2. `Pid` - the process id
    3. `PPid` - the parent process id
    4. `PGid` - the process group id
    5. `Sid` - the session id
    6. `STAT` - the process state
    7. `Username` - the username of the user that spawned the process

A struct `stat` is used to store this information when it is to be written to the `status` file. The struct is shown below - 

```c
struct stat_t {
    char name[MAXLINE];     /* name of the command */
    pid_t pid;              /* process id */
    pid_t ppid;             /* parent process id */
    pid_t pgid;             /* process group id */
    pid_t sid;              /* session id */
    char state[MAXLINE];    /* state of the process */
    char uname[MAXLINE];    /* user name */
};
```

The `stat` struct is created using the `get_stat()` function which takes in the process pid and the process state i.e. `FG`, `BG` or `ST`. A global variable is maitained for the shell's pid which is the session id stored in `volatile int session_id`. Additionally, the user that logs in is stored in a global variable to be used in the `get_stat()` function. We can then add, remove, and edit proc entries using the followinng functions - 

```c
void create_proc_entry(struct stat_t *stat);
void write_proc_entry(struct stat_t *stat);
void read_proc_entry(struct stat_t *stat, pid_t pid);
void edit_proc_entry(pid_t pid, char *new_state);
void remove_proc_entry(pid_t pid);
```

When processes are spawned, we create proc entries by passing the `stat` struct. Similarly we can edit the proc entry by passing the `pid` and the new state that we want to change the process state to. We edit the state of a proc entry in the `do_bgfg()` function when a job is resumed in the foreground or background. We also remove the proc entry when a job is terminated in the signal handlers.


### Job Control

As mentioned above, the `waitfg()` function used waits until the global variable `fg_pid` is set back to the `pid` of the child process spawned and until the calls `sigsuspend` instead of `sleep(1)` as this is wasteful of `CPU` resources. Therefore, when calling the `waitfg()` function, we pass the signal set that we want to suspend CPU execution till receieved and this loops until the global variable `fg_pid` mentioned previously is set back to the `pid` of the process spawned. The `fg_pid` is set to the `pid` of the process that was spawned inside the signal handlers. Whenever a process runs in the foreground (if it not a built-in command) or when a job is moved to the foreground using the `fg` command (which uses the `do_bgfg()` function), the `waitfg()` function is called to make the shell wait for the process to complete before prompting the user for another command.

The functioning of the `do_bgfg()` function has been described in detail in the `fg` and `bg` commands sections above. Please refer to those sections for more details.

The signals handlers that the shell implements are the following:

1. `SIGCHLD` - This is implemented in the `sigchld_handler()` function. When a `SIGCHLD` signal is received, the function checks if the signal received is the correct signal. If so, it blocks all signals to allow for this signal to be processed before handling any other signals that could be received. This is because only one signal of that type can be handled at a time and any signals of the same type are ignored if received when processing the current signal of that type. In the handler, we wait for the child process to complete and then check if the process terminated or was stopped. If the process was stopped, we get the appropriate job from the global jobs list and change the state to `ST`. In addition, we also change the state of the proc entry using the `edit_proc_entry()` function mentioned above to `T`. If the process was a foreground process, we set the global variable `fg_pid` to the process `pid` to indicate to `waitfg()` that the process has completed. Similarly, if the process terminated, we remove the job from the global jobs list and remove the proc entry using the `remove_proc_entry()` function mentioned above and change the `fg_pid` appropriately. Since we are blocking and unblocking all signals around this, we prevent the shared data structure `jobs` from being accessed by multiple processes at the same time.

2. `SIGTSTP` - This is implemented in the `sigtstp_handler()` function. When a `SIGSTP` signal is received, the function checks if the signal received is the correct signal. If so, it blocks all signals to allow for this signal to be processed before handling any other signals that could be received. This is because only one signal of that type can be handled at a time and any signals of the same type are ignored if received when processing the current signal of that type. We block all signals from being received as we are about to modify the global jobs list. We then obtain the foreground process id using `fgpid()` and check if the process id is valid. If so, we send a `SIGTSTP` signal to the process using `kill()`. We then set the global variable `fg_pid` to the pid that we obtained using `fgpid()` to indicated to `waitfg()`, edit the job entry from the `jobs` global array from `FG` to `ST` and then modify the `proc` entry similar to reflect the job state change. Finally we unblock all signals.

3. `SIGINT` - This is implemented in the `sigint_handler()` function. When a `SIGINT` signal is received, the function checks if the signal received is the correct signal. If so, it blocks all signals to allow for this signal to be processed before handling any other signals that could be received. This is because only one signal of that type can be handled at a time and any signals of the same type are ignored if received when processing the current signal of that type. We block all signals from being received as we are about to modify the global jobs list and the proc entries. We then obtain the foreground process id using `fgpid()` and check if the process id is valid. If so, we send a `SIGINT` signal to the process using `kill()`. We then set the global variable `fg_pid` to the pid that we obtained using `fgpid()` to indicated to `waitfg()` to exit, delete the job entry from the `jobs` global array and then remove the `proc` entry. Finally we unblock all signals. 


## Questions about the code

1. What was the most challenging aspect of the project for you?

The most challenging aspect of the project for me was the signal handling portion. This signal handlers were relatively easy to implement, however, in the entire code, determining when to block and unblock all signals was tricky. I found the `sigprocmask()` function to be very useful in this regard. I also found the `sigsuspend()` function to be very useful in the `waitfg()` function. I also found the parallel programming concepts to be extremely helpful for this part of the project as I was able to take concepts of concurrency and apply them to the shell. In addition, determining the process flow as the signal handlers was called was tricky. Tracing the function calls of the signal handler proved to be confusing as I spent a lot of time trying to determine where I need to remove/modify jobs from the jobs list and the proc entries. However, in the end, I found it extremely rewarding to have implemented the signal handlers and the job control portion of the shell.

2. Right now, the `tsh` shell is running within the filesystem of your default shell (i.e., most likely bash) and uses the filesystem structure associated with it. What if we required you to have the built-in commands of mount and unmount where the shell had to ensure that directories and files not physically mounted/unmounted could not be accessed or modified. Conceptually, what changes would you have to make to program to mount and umount work in the `tsh.c` file.

The way we could implement `mount` and `unmount` can be with `cp -r` and `rm -rf` respectively. For the `mount` command, we can either make a system call with `cp -r` as the command or write a `C` based function to recursively copy all files from the directory we are trying to mount into our project directory. We can also keep two global variables, the first will be the directory path that we are mounting and the second will be the project directory path. After that, for every function call, we can check if the command is trying to access a file from the directory we are mounting. If so we can replace the path of the mounting directory in the command with the path of our project directory and run the command with this new path. Therefore, we will be able to execute these commands without accessing the mounted directory. For the `unmount` command, we can either make a system call with `rm -rf` as the command or write a `C` based function to recursively delete all files from the directory we are trying to unmount or since we have this stored as a global variable, we can simply use this path.

3. What would need to change if you wanted to implement pipes (e.g., `sort longsort.txt | uniq | wc -l`) in `tsh`? Conceptually, what aspects of the shell would need to be modified and added to implement this functionality?

The way we can implement pipes is through just a few changes. The first change will be in the `parseline()` function. We would split on the `|` character and store the commands in an array. The next change is in the `eval()` function. In the `eval()` function, we will need to loop over the array of commands and for each command. We can execute a command, then read the output of the command either from `stdout` or by storing the return of all commands that can be executed (i.e. just changing the return types of our commands to return the actual output instead of printing to the shell). Inside the loop for executing the commands, we just add the output of the previous command at the end of the next command array i.e. (argv) as this is the data for the next command. We continue doing this until we complete all the commands. Finally, we can just print the output of the last command. This will allow us to implement pipes in the shell.

