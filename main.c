/******************************************************************************
 *
 *  File Name........: main.c
 *
 *  Description......: Simple driver program for ush's parser
 *
 *  Author...........: Vincent W. Freeh
 *
 *****************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "parse.h"

// Global Variables which hold hostname, user's directory and current directory
char *hostname;
char *homedir;
char *current_dir;

// Environment Variables Available
extern char **environ;

char *built_in_commands[] = {"echo", "cd", "pwd", "logout", "setenv", "unsetenv", "where", 0};

int is_built_in_command(const char *command_name) {
  int i = 0;
  char *current = built_in_commands[i];

  while(current != NULL) {
    if(!strcmp(command_name, current))
      return 1;
    i++;
    current = built_in_commands[i];
  }
  return 0;
}

/*
 * initializes the shell
 * Mainly setsup the hostname and user's home directory which is also the current directory
*/
void init() {
  struct passwd *pw;


  // Get the hostname
  hostname = (char *)malloc(1024 * sizeof(char));
  if(hostname == NULL) {
    exit(1);
  }
  gethostname(hostname, 1024);
  hostname = realloc(hostname, strlen(hostname) + 1);

  // Get the user's home directory
  pw = getpwuid(getuid());
  homedir = (char *)malloc((strlen(pw->pw_dir) + 1) * sizeof(char));
  homedir = strcpy(homedir, pw -> pw_dir);

  // Get the current directory
  current_dir = (char *)malloc((4 * 1024) * sizeof(char));
  if(current_dir == NULL)
    exit(-1);
  getcwd(current_dir, 4 * 1024);
  current_dir = realloc(current_dir, strlen(current_dir) + 1);
}

/*
 * Changes the current directory
 *
 * If path == NULL changes to home directory
 * Otherwise it conbstructs a new path (absolute or relative)
 * Checks if we are trying to change to a directory and not file
*/
void change_current_directory(char *path) {
  char *effective_dest;
  struct stat sb;
  // If the path is null, we revert back to home directory
  if((path == NULL) || (strlen(path) == 0)) {
    strcpy(current_dir, homedir);
    setenv("PWD", current_dir, 1);
    chdir(current_dir);
    return;
  }

  // Calculate the effective destination directory
  if(path[0] == '/') {
    effective_dest = (char *)malloc((strlen(path) + 1) * sizeof(char));
    strcpy(effective_dest, path);
  } else {
    // if the current_directory ends in / character we append
    if(current_dir[strlen(current_dir) - 1] == '/') {
      effective_dest = (char *)malloc((strlen(current_dir) + strlen(path) + 1) * sizeof(char));
      strcpy(effective_dest, current_dir);
      strcat(effective_dest, path);
    } else {
      effective_dest = (char *)malloc((strlen(current_dir) + strlen(path) + 2) * sizeof(char));
      strcpy(effective_dest, current_dir);
      strcat(effective_dest, "/");
      strcat(effective_dest, path);
    }
  }

  // Check if the effective path exists
  if(access(effective_dest, F_OK) == -1) {
    fprintf(stderr, "No such file or directory [%s]\n", effective_dest);
    return;
  }

  // We can only change to a directory and not to a file
  // get the statistic for this file / directory
  stat(effective_dest, &sb);
  if(!S_ISDIR(sb.st_mode)) {
    fprintf(stderr, "Not a directory [%s]\n", effective_dest);
    return;
  }

  // The directory exists and it is actually a directory
  // Change the current_dir
  strcpy(current_dir, effective_dest);

  // Update the PWD environment Variable
  setenv("PWD", current_dir, 1);

  // Call chdir just in case
  chdir(effective_dest);
}

/*
 * Checks if the file exists in the directory
 * Directory path must be absolute path
 * File must not have any
 *
 * Returns 0 if file exists, otherwise returns -1
*/
int file_exists(char *dir, char *file) {
  char *absolute_path;
  int access_allowed;

  // If the dir ends with a slash,
  if(dir[strlen(dir) - 1] == '/') {
    absolute_path = (char *)malloc((strlen(dir) + strlen(file) + 1) * sizeof(char));
    strcpy(absolute_path, dir);
    strcat(absolute_path, file);
  } else {
    absolute_path = (char *)malloc((strlen(dir) + strlen(file) + 2) * sizeof(char));
    strcpy(absolute_path, dir);
    strcat(absolute_path, "/");
    strcat(absolute_path, file);
  }

  access_allowed = access(absolute_path, F_OK);
  free(absolute_path);
  return access_allowed;
}


// Returns the number of commands in the pipe
int getCommandCount(Pipe p) {
  int count = 0;
  Cmd c;

  if(p == NULL)
    return 0;

  for(c = p -> head; c != NULL; c = c -> next)
    count++;

  return count;
}

void exitIfEmptyCommand(Pipe p) {
  if(p == NULL)
    return;
  Cmd c = p -> head;
  if(!strcmp(c -> args[0], "end")) {
    freePipe(p);
    exit(0);
  }
}

// Built in echo command
void echo(Cmd command) {
  int i;
  // If there are no arguments return
  if(command -> nargs == 1) {
    printf("\n");
    return;
  }
  for(i = 1; i < command -> nargs; i++) {
    printf("%s ", command -> args[i]);
  }
  printf("\n");
}

// Build in cd command (which changes the current directory)
void change_dir(Cmd command) {
  char *dest;
  // if there are no arguments change to home directory
  if(command -> nargs == 1)
    change_current_directory(NULL);
  else
    change_current_directory(command -> args[1]);
}

// Built in command to print the current directory
void pwd() {
  // Print the current directory
  printf("%s\n", current_dir);
}

// Built in command to logout / exit
void logout() {
  // Exit the shell
  exit(0);
}

void print_all_env_variables() {
  char *current = *environ;
  int offset = 0;
  while(current) {
    printf("%s\n", current);
    current = *(environ + offset++);
  }
}

void set_environment_variable(char *name, char *value) {
  // We always overide the value of the environment Variables
  setenv(name, value, 1);
}

void set_environment(Cmd command) {
  if(command -> nargs == 1) {
    print_all_env_variables();
    return;
  } else if(command -> nargs == 2){
    // Set the environment variable to an empty string
    set_environment_variable(command -> args[1], "");
  } else if(command -> nargs >= 3) {
    // Set the environment variable to a value
    set_environment_variable(command -> args[1], command -> args[2]);
  }
}

void unset_environment(Cmd command) {
  // If no arguments provided do nothing and silently return
  if(command -> nargs < 2)
    return;
  unsetenv(command -> args[1]);
}

// The function returns whether a file is found in the one of the PATH variable
// directory or not
// If the file is found, absolute path to the file is returned
// Otherwise NULL is returned, which denotes that we finished searching everywhere
// but could not find the file
char *locate_in_path(char *file_name) {
  // Get the value of the PATH variable
  char *path_variable_value;
  char *directory_in_path;
  char *absolute_path;

  // Copy the path variable value in a string
  path_variable_value = strdup(getenv("PATH"));

  if(path_variable_value == NULL) {
    // This means that PATH variable was not found
    // Someone must have deleted this variable
    return NULL;
  }

  directory_in_path = strtok(path_variable_value, ":");
  while(directory_in_path != NULL) {
    if(file_exists(directory_in_path, file_name) == 0) {
      // We found the file which matches the criteria
      // Return it
      absolute_path = (char *)malloc(sizeof(char) *
        (strlen(directory_in_path) + strlen(file_name) + 2));
      strcpy(absolute_path, directory_in_path);
      strcat(absolute_path, "/");
      strcat(absolute_path, file_name);
      free(path_variable_value);
      return absolute_path;
    }
    directory_in_path = strtok(NULL, ":");
  }

  free(path_variable_value);
  return NULL;
}

void find_where(Cmd command) {
  char *search_term;
  char *path_value;
  int path_value_len;
  char *path_token;

  // If where was called with no arguments return
  if(command -> nargs == 1)
    return;

  search_term = command -> args[1];

  // If there is nothing to be searched, return
  if(search_term == NULL || strlen(search_term) == 0)
    return;

  // Is it a built in command
  if(strcmp(search_term, "echo") == 0     ||
     strcmp(search_term, "cd") == 0       ||
     strcmp(search_term, "pwd") == 0      ||
     strcmp(search_term, "logout") == 0   ||
     strcmp(search_term, "setenv") == 0   ||
     strcmp(search_term, "unsetenv") == 0 ||
     strcmp(search_term, "where") == 0) {
       printf("[built-in] %s\n", search_term);
  }

  // Get the path environment variable, it may not be there so be careful
  if(getenv("PATH") == NULL)
    return;

  // However if path is not empty or NULL, copy it into a buffer
  // we will be breaking it into tokens
  path_value_len = strlen(getenv("PATH"));
  path_value = (char *)malloc((path_value_len + 1) * sizeof(char));
  strcpy(path_value, getenv("PATH"));

  path_token = strtok(path_value, ":");
  while(path_token != NULL) {
    if(file_exists(path_token, search_term) == 0) {
      if(path_token[strlen(path_token) - 1] == '/')
        printf("%s%s\n", path_token, search_term);
      else
        printf("%s/%s\n", path_token, search_term);
    }
    path_token = strtok(NULL, ":");
  }

}

void execute_non_built_in_command(Cmd command) {
  // This is the executable file name and it is always absolute
  // Our goal is to locate this file
  char *executable_file_name = NULL;
  char *command_name = command -> args[0];

  // If the command starts with /, it refers to an executable file
  // using the absolute path
  if(command_name[0] == '/') {
    // Treat the entire command as an absolute path
    // Locate the file
    if(access(command_name, X_OK) == 0) {
      executable_file_name = malloc((strlen(command_name) + 1) * sizeof(char));
      strcpy(executable_file_name, command_name);
    }
  } else if (strchr(command_name, '/') != NULL) {
    // This should be treated as relative
    if(file_exists(current_dir, command_name) == 0) {
      executable_file_name = (char *)malloc((strlen(current_dir) + strlen(command_name) + 1) * sizeof(char));
      strcpy(executable_file_name, current_dir);
      strcat(executable_file_name, "/");
      strcat(executable_file_name, command_name);
    }
  }

  // We did not find this command either as built-in command
  // nor as an absolute file
  // nor as a relative file
  // I think it is time to locate this in known locations
  if(executable_file_name == NULL)
    executable_file_name = locate_in_path(command_name);

  if(executable_file_name == NULL) {
    fprintf(stderr, "command not found\n");
    return;
  }

  // Execute this command
  // Lets assume that absolute path is provided at the moment
  int pid;
  pid = fork();
  if(pid == 0) {
      // Child process
      execve(executable_file_name, command -> args, environ);
      exit(0);
  } else {
    // Parent (this shell) will wait for the Child
    waitpid(pid, NULL, 0);
    free(executable_file_name);
  }
}


void be_nice(Cmd command) {
  int priority, parsed_priority;
  char *absolute_path = NULL;
  int pid;
  char **command_args = NULL;

  // If there are no arguments, we set the nicety of the shell to be 4
  if(command -> nargs == 1) {
    // Only set the default priority of 4
    setpriority(PRIO_PROCESS, 0, 4);
    return;
  }

  // Check if the second argument is priority or not
  priority = atoi(command -> args[1]);

  // Brinf the priority to expected levels
  if(priority < -20)
    priority = -20;
  if(priority > 19)
    priority = 19;

  // Set the priority
  if(priority == 0)   /*Problem witgh parse, set it to 0*/
    setpriority(PRIO_PROCESS, 0, 4);
  else
    setpriority(PRIO_PROCESS, 0, priority);

  if(priority == 0) {
    // Priority was not provided
    // Means that second argument forward is the command is the
    absolute_path = locate_in_path(command -> args[1]);
    command_args = command -> args + 1;

  } else {
    absolute_path = locate_in_path(command -> args[2]);
    command_args = command -> args + 2;
  }

  // Fork
  pid = fork();
  if(pid == 0) {
    execve(absolute_path, command_args, environ);
    exit(0);
  } else {
    waitpid(pid, NULL, 0);
  }
}

// Execute a single command
void execute_command(Cmd command) {
  // _old stores the stdin, stdout and stderr before starting execution
  int stdout_old, stderr_old, stdin_old;

  // If the command reads from the file or writes to a file
  // the folllowing are the fids for those
  int outfile = -1, infile = -1;

  // Dup the current stdout
  // stdout_old and stderr_old will keep track of stdout and stderr which can be restored later
  stdout_old = dup(fileno(stdout));
  stderr_old = dup(fileno(stderr));
  stdin_old = dup(fileno(stdin));

  if(command -> in == Tin) {
    // Open a file for reading
    infile = open(command -> infile, O_RDONLY);
    // standard in should read from the file now
    dup2(infile, STDIN_FILENO);
  }

  // Handle output redirection
  switch(command -> out) {
    case Tout:
      outfile = open(command -> outfile, O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      // file descriptor # 1 should now point to the above file
      dup2(outfile, STDOUT_FILENO);
      break;
    case Tapp:
      outfile = open(command -> outfile, O_WRONLY | O_APPEND | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      dup2(outfile, STDOUT_FILENO);
      break;
    case ToutErr:
      outfile = open(command -> outfile, O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      // Both standard error and standard out should point to the file
      dup2(outfile, STDOUT_FILENO);
      dup2(outfile, STDERR_FILENO);
      break;
    case TappErr:
      outfile = open(command -> outfile, O_WRONLY | O_APPEND | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      dup2(outfile, STDOUT_FILENO);
      dup2(outfile, STDERR_FILENO);
      break;
  }


  // Get the command name, it is the first argument
  char *command_name = command -> args[0];
  if(!strcmp(command_name, "echo")) {
    echo(command);
  } else if(!strcmp(command_name, "cd")) {
    change_dir(command);
  } else if(!strcmp(command_name, "pwd")) {
    pwd();
  } else if(!strcmp(command_name, "logout")) {
    logout();
  } else if(!strcmp(command_name, "setenv")) {
    set_environment(command);
  } else if(!strcmp(command_name, "unsetenv")) {
    unset_environment(command);
  } else if(!strcmp(command_name, "where")) {
    find_where(command);
  } else if(!strcmp(command_name, "nice")){
    be_nice(command);
  } else {
    // This is not a built in command
    // Execute non-built in command
    execute_non_built_in_command(command);
  }

  // We are done with the command
  // If any infile or outfiles were open close them
  if(outfile != -1) {
    close(outfile);
  }
  if(infile != -1)
    close(infile);

  if(command -> in == Tin) {
    // Restore the system in and return the descriptor
    dup2(stdin_old, STDIN_FILENO);
    close(stdin_old);
  }

  // Restore the stdout stdin and return the descriptor to the pool
  dup2(stdout_old, STDOUT_FILENO);
  dup2(stderr_old, STDERR_FILENO);
  dup2(stdin_old, STDIN_FILENO);
  close(stdout_old);
  close(stderr_old);
  close(stdin_old);
}

void execute_pipe_command(int in, int out, Cmd command) {
  char *absolute_path = NULL;
  int pid;
  char *command_name = command -> args[0];
  int stdout_old, stderr_old;
  int outfile = 0;
  char **command_args;
  int priority;

  if(out == 1) {
    // Should we print it to out or somewhere else
    if(command -> out == Tapp) {
      outfile = open(command -> outfile, O_WRONLY | O_APPEND | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    } else if (command -> out == Tout) {
      outfile = open(command -> outfile, O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    }
  }

  // If this is a built in command, we need to handle the built in command
  if(is_built_in_command(command_name)) {

    // back up the current file descriptors for out and err
    stdout_old = dup(fileno(stdout));
    stderr_old = dup(fileno(stderr));
    // Create two new descriptors
    if(outfile == 0) {
      dup2(out, STDOUT_FILENO);
      dup2(out, STDERR_FILENO);
    } else {
      dup2(outfile, STDOUT_FILENO);
      dup2(outfile, STDERR_FILENO);
    }
    if(!strcmp(command_name, "echo")) {
      echo(command);
    } else if(!strcmp(command_name, "cd")) {
      change_dir(command);
    } else if(!strcmp(command_name, "pwd")) {
      pwd();
    } else if(!strcmp(command_name, "logout")) {
      logout();
    } else if(!strcmp(command_name, "setenv")) {
      set_environment(command);
    } else if(!strcmp(command_name, "unsetenv")) {
      unset_environment(command);
    } else if(!strcmp(command_name, "where")) {
      find_where(command);
    }
    dup2(stdout_old, STDOUT_FILENO);
    dup2(stderr_old, STDERR_FILENO);
    close(stdout_old);
    close(stderr_old);
    return;
  }

  // Special habndling of nice command
  if(!strcmp(command_name, "nice")) {
    if(command -> nargs == 1) {
      setpriority(PRIO_PROCESS, 0, 4);
      return;
    }
    else {
      priority = atoi(command -> args[1]);
      if(priority < -20) priority = -20;
      if(priority > 19) priority = 19;
      if(priority == 0) {
        setpriority(PRIO_PROCESS, 0, 4);
        // command name is the second argument
        command_name = command -> args[1];
        command_args = command -> args + 1;
      } else {
        setpriority(PRIO_PROCESS, 0, priority);
        command_name = command -> args[2];
        command_args = command -> args + 2;
      }
    }
  } else {
    command_args = command -> args;
  }


  // This is not a built in pipe command
  // Figure out the absolute path of this command
  if(command_name[0] == '/') {
    // This is a path to an executable file
    if(access(command_name, X_OK) == 0) {
      absolute_path = command_name;
    }
  } else if(strchr(command_name, '/') != NULL) {
    if(file_exists(current_dir, command_name) == 0) {
      absolute_path = (char *)malloc((strlen(current_dir) + strlen(command_name) + 1) * sizeof(char));
      strcpy(absolute_path, current_dir);
      strcat(absolute_path, "/");
      strcat(absolute_path, command_name);
    }
  }

  // This is neither the absolute or relative file path to an executable
  if(absolute_path == NULL)
    absolute_path = locate_in_path(command_name);

  if(absolute_path == NULL) {
    fprintf(stderr, "command not found\n");
    return;
  }



  // We found an executable that we can execute
  // Fork a process
  pid = fork();

  if(pid == 0) {
    if(in != 0) {
      dup2(in, STDIN_FILENO);
      close(in);
    }

    if(out != 1) {
      dup2(out, STDOUT_FILENO);
      // Also connect error if needed
      if(command -> out == TpipeErr)
        dup2(out, STDERR_FILENO);
      close(out);
    } else {
      // This is the last command
      if(outfile != 0) {
        dup2(outfile, STDOUT_FILENO);
        close(outfile);
      }
    }

    // Child will do this
    execve(absolute_path, command_args, environ);
    exit(0);
  } else {
    // Parent just waits for the child
    waitpid(pid, NULL, 0);
  }
}

void setup_pipeline(Pipe p) {
  // Copy the command pointers in an array
  Cmd *cmd_array = NULL;
  int num_commands = 0;
  Cmd current;
  int i;
  int in = 0;
  int fd[2];
  char *absolute_path;
  int pid;
  // Last command outputs to std out
  int last_command_out = 1;


  num_commands = getCommandCount(p);
  cmd_array = (Cmd *)malloc(num_commands * sizeof(Cmd));
  for(i = 0, current = p -> head; i < num_commands && current != NULL; i++, current = current -> next) {
    cmd_array[i] = current;
  }

  // First command may read from a file
  // We need to check that
  if(cmd_array[0] -> in == Tin) {
    // open the file in read mode
    in = open(cmd_array[0] -> infile, O_RDONLY);
  }


  // print the commands
  for(i = 0; i < num_commands - 1; i++) {

    // Create a pipe
    pipe(fd);

    execute_pipe_command(in, fd[1], cmd_array[i]);

    // Closing the write end of the pipe
    close(fd[1]);

    // First command may read from a file
    in = fd[0];
  }

  execute_pipe_command(in, 1, cmd_array[i]);
}

// Executes the pipe
void executePipe(Pipe p) {
  int num_command = 0;
  if(p == NULL)
    return;
  // Count the number of commands in the pipe
  // If there is just one command, we only need to run that
  // otherwise we will need to setup pipeline
  num_command = getCommandCount(p);
  if(num_command == 0)
    return;

  if(num_command == 1) {
    execute_command(p -> head);
  } else {
    setup_pipeline(p);
  }

  executePipe(p -> next);
}

int is_empty_or_end(Pipe p) {
  if(p == NULL)
    return 1;
  Cmd c = p -> head;
  if(!strcmp(c -> args[0], "end")) {
    freePipe(p);
    return 1;
  }
  return 0;
}


void handle_ushrc() {
  char *ushrc_path;
  int ushrc_fid;
  int stdin_old = dup(fileno(stdin));
  Pipe p;

  ushrc_path = (char *)malloc((strlen(homedir) + strlen("/.ushrc") + 1) * sizeof(char));
  strcpy(ushrc_path, homedir);
  strcat(ushrc_path, "/.ushrc");

  if(access(ushrc_path, R_OK) != 0)
    return;

  // File exists .. open it
  ushrc_fid = open(ushrc_path, O_RDONLY);

  // Redirect input to this file
  dup2(ushrc_fid, STDIN_FILENO);

  // handle ushrc
  while(1) {
    // Parse the pipe
    p = parse();
    if(is_empty_or_end(p))
      break;
    executePipe(p);
  }

  // ushrc handling done ... now move everything back
  dup2(stdin_old, STDIN_FILENO);

  // Close the file descriptors we used in this function
  close(ushrc_fid);
  close(stdin_old);


}

int main(int argc, char *argv[])
{
  Pipe p;

  // initialize the shell
  init();

  handle_ushrc();

  while ( 1 ) {
    // Show the prompt if terminal is attached to the std in
    if(isatty(STDIN_FILENO))
      printf("%s%% ", hostname);
    fflush(NULL);
    // Parse the pipe which can be made of multiple commands
    p = parse();
    if(p == NULL)
      continue;
    if(is_empty_or_end(p))
      break;
    executePipe(p);
    freePipe(p);
  }
}

/*........................ end of main.c ....................................*/
