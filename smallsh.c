#define _POSIX_C_SOURCE 200809L
#define MAX_INPUT 512

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

// Struct to store env variables
struct env_vars {
  char *ifs;
  char *ps1;
  char *home_path;
  char *smallsh_process_id;
  char *last_fg_exec_return_val;
  char *last_bg_exec_return_val;
};

// Struct to store semantic tokens of the input string
struct parsed_tokens {
  char *cmd;
  char *cmd_args[MAX_INPUT];
  char *input_for_execvp[MAX_INPUT + 1];
  bool will_run_in_bg; // if true, run the process in the background
  char *input_redirection_path;
  char *output_redirection_path;
};


int parse_input(char **words, unsigned int num_words, struct parsed_tokens *pt);
void init_parsed_tokens_struct(struct parsed_tokens *pt);
void free_parsed_tokens_struct(struct parsed_tokens *pt);
void print_parsed_tokens_struct(struct parsed_tokens *pt);
void print_env_struct(struct env_vars *env);
void execute_cd_command(struct env_vars *env, struct parsed_tokens *pt);
void execute_exit_command(struct env_vars *env, struct parsed_tokens *pt);
void execute_other_command(struct env_vars *env, struct parsed_tokens *pt);

char *str_gsub(char **haystack, char const *needle, char const *sub);
void print_prompt(struct env_vars *env);
void init_env_vars(struct env_vars *env);
void free_env_vars_struct(struct env_vars *env);
void expand_variables(char **split_words, unsigned int num_words, struct env_vars *env);
void update_env_vars_bg_return_vals(struct env_vars *env);

/* Our signal handler for SIGINT */
void handle_SIGINT(int signo) {
  // Should ignore SIGINT unless we are reading a line of input
}

/* Our signal handler for SIGTSTP */
void handle_SIGTSTP(int signo) {
  // Ignores SIGTSTP
}

// Global Variables
struct env_vars env;
struct parsed_tokens pt;


int main(void) {

  // CTRL-C on terminal
  struct sigaction SIGINT_old_action;
  struct sigaction SIGINT_action = {0};
  SIGINT_action.sa_handler = handle_SIGINT;
  SIGINT_action.sa_flags = 0;
  sigfillset(&SIGINT_action.sa_mask);
  if (sigaction(SIGINT, &SIGINT_action, &SIGINT_old_action) < 0) {
    perror("Could not set SIGINT handler");
    exit(EXIT_FAILURE);
  }

  // CTRL-Z on terminal
  struct sigaction SIGTSTP_old_action;
  struct sigaction SIGTSTP_action = {0};
  SIGTSTP_action.sa_handler = handle_SIGTSTP;
  sigfillset(&SIGTSTP_action.sa_mask);
  SIGTSTP_action.sa_flags = 0;
  if (sigaction(SIGTSTP, &SIGTSTP_action, &SIGTSTP_old_action) < 0 ) {
    perror("Couldn't set SIGTSTP handler");
    exit(EXIT_FAILURE);
  }
  


  char *split_words[MAX_INPUT];
  char *line = NULL;
  size_t n = 0;

  init_env_vars(&env);


start:
  while (true) {

    int status = -1;
    pid_t bgChildPid = -5;

    // Check if any background child processes have finished or signalled
    while((bgChildPid = waitpid(0, &status, WNOHANG | WUNTRACED)) > 0) {

      if (WIFEXITED(status)) {
			  fprintf(stderr, "Child process %jd done. Exit status %d\n", (intmax_t)bgChildPid, WEXITSTATUS(status));
      }
      else if (WIFSIGNALED(status)) {
	      fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)bgChildPid, WTERMSIG(status));
      }
      else {
        if (WIFSTOPPED(status)) {
          // check if it is stopped, send a SIGCONT, the child will signal a SIGCHLD if it is stopped
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)bgChildPid);
            kill(bgChildPid, SIGCONT);
            // Update $!
            free(env.last_bg_exec_return_val);
            env.last_bg_exec_return_val = NULL;

            char *tmp_str = (char *)malloc(sizeof(char) * 8);
            sprintf(tmp_str, "%d", getpid());

            env.last_bg_exec_return_val = strdup(tmp_str);
            free(tmp_str);
        }
      }
    }
      
    // Initialize the parsed tokens struct    
    init_parsed_tokens_struct(&pt);

    // Prompt user for input
    print_prompt(&env);
   
    // Make SIG_INT run here only
	  SIGINT_action.sa_handler = handle_SIGINT;
	  sigaction(SIGINT, &SIGINT_action, &SIGINT_old_action);
    
    // Get the line of input from a user
    ssize_t line_length;
    if ((line_length = getline(&line, &n, stdin)) == -1) {
      // Reset errno if an interrupt signal came through
      if (errno == EINTR) {
        clearerr(stdin);
        fprintf(stderr, "\n");
        errno = 0;
        free_parsed_tokens_struct(&pt);
        goto start;
      }
      // Check for EOF, exit if found
      else if (feof(stdin)) {
        execute_exit_command(&env, &pt);
      }
      // Log any error to the user
      else {
        perror("getline()");
        exit(EXIT_FAILURE);
      }
    }
	  
    // Reset SIGINT handler to normal
    sigaction(SIGINT, &SIGINT_old_action, NULL); 

    // Split user input into an array for variable expansion and parsing
    char *token = NULL;
    unsigned int index = 0;
    // use strtok to split the line
    if (env.ifs == NULL) {
      token = strtok(line, " \t\n");
      while (token != NULL) {
        split_words[index] = strdup(token);
        index++;
        token = strtok(NULL, " \t\n");
      }
    }
    else {
      token = strtok(line, env.ifs);
      while (token != NULL) {
        split_words[index] = strdup(token);
        index++;
        token = strtok(NULL, env.ifs);
      }
    }

    // Expand any variables in the user input 
    expand_variables(split_words, index, &env);
    
    // Parse the user input into the pt struct
    if (parse_input(split_words, index, &pt) < 0) {
      perror("Error with parss_input()");
      for (int i = 0; i < index; i++)
        free(split_words[i]);
      goto exit;
    }

    // Restart the loop if user input is empty
    if (pt.cmd == NULL)
      goto start;
   
    // Check for exit or cd built in commands 
    if (strcmp(pt.cmd, "exit") == 0) {
      execute_exit_command(&env, &pt);
      goto exit;
    }
    if (strcmp(pt.cmd, "cd") == 0) {
      execute_cd_command(&env, &pt);
      goto start;
    }

    // Fork a child process to execvp the non built in command
	  pid_t spawnpid = -5;
	  int childStatus = -5;
	  pid_t childPid = -5;
    int targetFD = -5;
    int sourceFD = -5;

    spawnpid = fork(); // If fork is successful, the value of spawnpid will be 0 in the child, the child's pid in the parent
	  switch (spawnpid){
	  case -1:
		  perror("fork()");
		  exit(EXIT_FAILURE);
	  case 0:
      // This is the child process
      if (pt.will_run_in_bg) {
        // Find and remove '&' from the command args
        for (int i = 0; i < MAX_INPUT; i++) {
          if (*pt.input_for_execvp[i] == '&' && !pt.input_for_execvp[i + 1]) {
            pt.input_for_execvp[i] = NULL;
            break;
          }
        }
      }

      // Handle output redirection
      if (pt.output_redirection_path) {
        close(1);

        // Open the target FD, will get 1 since we just closed 1 (stdout)
        targetFD = -1;
        if ((targetFD = open(pt.output_redirection_path, O_WRONLY | O_CREAT | O_APPEND, 0777) == -1)) {
          perror("Output open()");
          exit(EXIT_SUCCESS);
        }
      }

      // Handle input redirection
      if (pt.input_redirection_path) {
	      close(0);

        // Open source file, will get 0 as the FD since we just closed 0 (stdout)
	      sourceFD = open(pt.input_redirection_path, O_RDONLY);
	      if (sourceFD == -1) { 
		      perror("Source open()"); 
	  	    exit(EXIT_FAILURE); 
        }
      }

      if (execvp(pt.input_for_execvp[0], pt.input_for_execvp) == -1) {
        perror("Error executing that command.");
        exit(EXIT_FAILURE);
      }
      
      // Close the targetFD and sourceFD
      if (targetFD >= 0) {
        if (close(targetFD) == -1) {
          perror("close(targetFD)");
          exit(EXIT_FAILURE);
        }
      }
      if (sourceFD >= 0) {
        if (close(sourceFD) == -1) {
          perror("close(sourceFD)");
          exit(EXIT_FAILURE);
        }
      }
      goto exit;
      break;

	default:
    // This is the parent process

    if (pt.will_run_in_bg) {

      // Find and remove the '&' from the args input for execvp()
      for (int i = 0; i < MAX_INPUT; i++) {
        if (pt.input_for_execvp[i] == NULL)
          break;

        if (strcmp(pt.input_for_execvp[i], "&") == 0) {
          pt.input_for_execvp[i] = NULL;
          break;
        }
      }

      // Update $! to be the PID of the background process
      free(env.last_bg_exec_return_val);
      env.last_bg_exec_return_val = NULL;

      char *tmp_str = (char *)malloc(sizeof(char) * 8);
      sprintf(tmp_str, "%d", spawnpid);
      
      env.last_bg_exec_return_val = strdup(tmp_str);
      free(tmp_str);
      
      childPid = waitpid(spawnpid, &childStatus, WNOHANG); // non-blocking wait
    }
    else {
      childPid = waitpid(spawnpid, &childStatus, 0); // blocking wait
      
      // Check whether the child process exited or was signaled
      if (WIFEXITED(childStatus)) {
        // Update $?
        free(env.last_fg_exec_return_val);
        env.last_fg_exec_return_val = NULL;

        char *tmp_str = (char *)malloc(sizeof(char) * 8);
        sprintf(tmp_str, "%d", WEXITSTATUS(childStatus));
        
        env.last_fg_exec_return_val = strdup(tmp_str);
        free(tmp_str);
      }
      else {
        // Update $? to 128 + [n], where [n] is the number of the signal that caused the child to terminate
        free(env.last_fg_exec_return_val);
        env.last_fg_exec_return_val = NULL;

        char *tmp_str = (char *)malloc(sizeof(char) * 8);
        sprintf(tmp_str, "%d", (WTERMSIG(childStatus) + 128));

        env.last_fg_exec_return_val = strdup(tmp_str);
        free(tmp_str);
      }
    }
    break;
	}
    // Free strings
    for (unsigned int i = 0; i < index; i++)
      free(split_words[i]);
  }
 
exit:
  if (atoi(env.smallsh_process_id) == getpid())
    free_env_vars_struct(&env);
  free_parsed_tokens_struct(&pt);

  return 0;
}


void execute_cd_command(struct env_vars *env, struct parsed_tokens *pt) {

  if (pt->cmd_args[0] == NULL) {
    if (chdir(env->home_path) != 0) {
      perror("Error, unable to CD to the home directory!");
    }
  }
  else {
    if (chdir(pt->cmd_args[0]) < 0) {
      perror("Error, unable to CD to that directory!\n");
    }
  }
}


void execute_exit_command(struct env_vars *env, struct parsed_tokens *pt) {
 

  if (pt->cmd_args[0] != NULL) { // an argument was provided

    // if the argument is not an integer, it is an error, or if more than 1 arguments are supplied, it is an error
    if (pt->cmd_args[1] != NULL) {
      perror("Error, too many arguments for exit command provided");
      exit(2);
    }
    if (isdigit(*pt->cmd_args[0]) == 0) {
      perror("Error, argument for exit command is not an integer");
      exit(2);
    }

    // send SIGINT through kill() to all child processes
    if (kill(0, SIGINT) < 0) {
      perror("Error with kill()");
      exit(2);
    }
    
    
    // free stuff first
    int tmp = atoi(pt->cmd_args[0]);
    free(env->last_fg_exec_return_val);
    char *tmp_str = (char *)malloc(sizeof(char) * 6);
    sprintf(tmp_str, "%d", tmp);
    env->last_fg_exec_return_val = tmp_str;
    
    fprintf(stderr, "\nexit\n");
    exit(tmp);
  }
  else {
    // send SIGINT through kill() to all child processes
    if (kill(0, SIGINT) < 0) {
      perror("Error with kill()\n");
      exit(2);
    }

    int tmp = atoi(env->last_fg_exec_return_val);
    fprintf(stderr, "\nexit\n");
    exit(tmp);
  }
};


int parse_input(char **words, unsigned int num_words, struct parsed_tokens *pt) {

  unsigned int num_words_before_comments = num_words;
  
  for (int i = 0; i < num_words; i++) {
    if (strcmp(words[i], "#") == 0) {
      num_words_before_comments = i;
      break;
    }
  }
 
  if (num_words_before_comments < num_words)
    num_words = num_words_before_comments;

  // Check if the last word is an "&" and set run in background to True
  if (num_words > 0) {
    if (strcmp(words[num_words - 1], "&") == 0) {
      pt->will_run_in_bg = true;
    }
  }

  // Set cmd and cmd_args accordingly
  switch (num_words) {    
    case 0:
      return 0;
    case 1:
      // Only store the command
      pt->cmd = strdup(words[0]); // Free this!
      break;
    default:
      pt->cmd = strdup(words[0]); // Free this!
      
      // Check that the second input is actually flags
      int j = 1;
      int num_args = 0;
      while (j < num_words) {
        if ((strcmp(words[j], "<") == 0) || (strcmp(words[j], ">") == 0)) {
          // store num_args and break the loop by setting j to num_words
          j = num_words;
          
        }
        else {
          num_args += 1;
          j++;
        }
      }
      for (int i = 0; i < num_args; i++) {
        pt->cmd_args[i] = strdup(words[i + 1]); // Free this!
        pt->input_for_execvp[i+1] = strdup(pt->cmd_args[i]);
      }
      break;
  }

  // Check for redirection of input/output
  for (int i = 1; i < num_words; i++) {

    if (strcmp(words[i], "<") == 0) {
      if (pt->input_redirection_path != NULL) {
        fprintf(stderr, "Error, multiple input redirection operands provided.\n");
        return -1;
      }
      // set the word after "<" to be the input redirection path 
      if ((i + 1) >= num_words) {
        fprintf(stderr, "Error, input redirection operand < provided but no following word provided.\n");
        return -1;
      }
      pt->input_redirection_path = strdup(words[i + 1]); // free this!
    }
    
    if (strcmp(words[i], ">") == 0) {
      // printf("Current word is: %s\n", words[i]);
      // printf("Next word is: %s\n", words[i + 1]);

      if (pt->output_redirection_path != NULL) {
        fprintf(stderr, "Error, multiple output redirection operands provided.\n");
        return -1;
      }
      // set the word after ">" to be the output redirection path
      if ((i + 1) >= num_words) {
        fprintf(stderr, "Error, output redirection operand > provided but no following word provided.\n");
        return -1;
      }
      pt->output_redirection_path = strdup(words[i + 1]); // free this!
    }

  }
  pt->input_for_execvp[0] = strdup(pt->cmd);

  return 0;
}


void init_parsed_tokens_struct(struct parsed_tokens *pt) {
  pt->cmd = NULL;
  for (int i = 0; i < MAX_INPUT; i++)
    pt->cmd_args[i] = NULL;
  for (int i = 0; i < MAX_INPUT + 1; i++) {
    pt->input_for_execvp[i] = NULL;
  }
  pt->input_redirection_path = NULL;
  pt->output_redirection_path = NULL;
  pt->will_run_in_bg = 0;
}


void free_parsed_tokens_struct(struct parsed_tokens *pt) {
  if (pt->cmd)
    free(pt->cmd);
  for (int i = 0; i < MAX_INPUT; i++) {
    if (pt->cmd_args[i])
      free(pt->cmd_args[i]);
    else
      break;
  }
  for (int i = 0; i < MAX_INPUT + 1; i++) {
    if (pt->input_for_execvp[i])
      free(pt->input_for_execvp[i]);
    else
      break;
  }
  if (pt->input_redirection_path)
    free(pt->input_redirection_path);
  if (pt->output_redirection_path)
    free(pt->output_redirection_path);
}


void print_env_struct(struct env_vars *env) {
  fprintf(stderr, "Homepath is: %s\n", env->home_path);
  fprintf(stderr, "IFS is: %s\n", env->ifs);
  fprintf(stderr, "PS1 is: %s\n", env->ps1);
  fprintf(stderr, "Last bg exec return val is: %s\n", env->last_bg_exec_return_val);
  fprintf(stderr, "Last fg exec return val is: %s\n", env->last_fg_exec_return_val);
  fprintf(stderr, "SMALLSH proecess is: %s\n", env->smallsh_process_id);
}


void print_parsed_tokens_struct(struct parsed_tokens *pt) {
  fprintf(stderr, "pt->cmd: %s\n", pt->cmd);
  for (int i = 0; i < MAX_INPUT; i++) {
    if (pt->cmd_args[i])
      fprintf(stderr, "pt->cmd_args[%d]: %s\n", i, pt->cmd_args[i]);
    else
      break;
  }
  
  for (int i = 0; i < MAX_INPUT + 1; i++) {
    if (pt->input_for_execvp[i])
      fprintf(stderr, "pt->input_for_execvp[%d]: %s\n", i, pt->input_for_execvp[i]);
    else
      break;
  }
  
  fprintf(stderr, "pt->input_redirection_path: %s\n", pt->input_redirection_path);
  fprintf(stderr, "pt->output_redirection_path: %s\n", pt->output_redirection_path);
  fprintf(stderr, "pt->will_run_in_bg: %d\n", pt->will_run_in_bg);

}


char *str_gsub(char **haystack, char const *needle, char const *sub) {
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle);
  size_t const sub_len = strlen(sub);

  for (; (str = strstr(str, needle));) {
    ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }

    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}


void print_prompt(struct env_vars *env) {
  if (env->ps1 != NULL)
    fprintf(stderr, "%s ", env->ps1);
  else
     fprintf(stderr, "$ ");
}


void init_env_vars(struct env_vars *env) {

    const char *PS1 = "PS1";
    const char *IFS = "IFS";

    char *ps1 = getenv(PS1);
    char *ifs = getenv(IFS);

    char *home_path = getenv("HOME");

    if (home_path == NULL)
      home_path = strdup(""); // free this!
    else {
      // Append an "/" to the end of the home variable string
      // char *tmp = (char *)malloc(sizeof(char) * strlen(home_path) + 1);
      char *tmp = (char *)malloc(sizeof(char) * strlen(home_path) + 1);
      if (!tmp) {
        fprintf(stderr, "Error mallocing tmp string for the home_path variable!\n");
      }
      strcpy(tmp, home_path);
      tmp[strlen(home_path)] = '/';
      tmp[strlen(home_path) + 1] = '\0';
      
      strcpy(home_path, tmp);
      // home_path = tmp;
    }

    // This should be the PID of smallsh, so will likely need to put this in a struct so we do not overwrite with a child process
    pid_t process_id = getpid();
    // Convert to a string
    char *process_id_str = (char *)malloc(sizeof(char) * 6);
    //char process_id_str[6];
    if (sprintf(process_id_str, "%d", process_id) < 0) {
      fprintf(stderr, "Error converting process_id from pid_t to char */!\n");
      exit(1);
    }


    char *last_fg_exec_return_val = getenv("$?");
    if (last_fg_exec_return_val == NULL) {
      // will default to "0"
      if (!(last_fg_exec_return_val = (char *)malloc(sizeof(char) * 2))) {
          fprintf(stderr, "Error mallocing for last_fg_exec_return_val!\n");
          // exit();
      }
      last_fg_exec_return_val[0] = '0';
      last_fg_exec_return_val[1] = '\0';
    }

    char *last_bg_exec_return_val = getenv("$!");
    if (last_bg_exec_return_val == NULL) {
      if (!(last_bg_exec_return_val = (char *)malloc(sizeof(char) * 1))) {
        fprintf(stderr, "Error mallocing for last_bg_exec_return_val!\n");
        // exit(1);
      }
      last_bg_exec_return_val[0] = '\0';
    }

    env->ifs = ifs;
    env->ps1 = ps1;
    env->home_path = home_path;
    env->smallsh_process_id = process_id_str;
    env->last_fg_exec_return_val = last_fg_exec_return_val;
    env->last_bg_exec_return_val = last_bg_exec_return_val;
}

void update_env_vars_bg_return_vals(struct env_vars *env) {
  
  // Free old values first
  free(env->last_bg_exec_return_val);

  char *last_bg_exec_return_val = getenv("$!");
  if (last_bg_exec_return_val == NULL) {
    if (!(last_bg_exec_return_val = (char *)malloc(sizeof(char) * 1))) {
      fprintf(stderr, "Error mallocing for last_bg_exec_return_val!\n");
      // exit(1);
    }
    last_bg_exec_return_val[0] = '\0';
  }

  env->last_bg_exec_return_val = last_bg_exec_return_val;
}


void free_env_vars_struct(struct env_vars *env) {
    free(env->home_path);
    free(env->smallsh_process_id);
    free(env->last_fg_exec_return_val);
    free(env->last_bg_exec_return_val);
}


void expand_variables(char **split_words, unsigned int num_words, struct env_vars *env) {
 
  for (unsigned int i = 0; i < num_words; i++) {
    str_gsub(&split_words[i], "~/", env->home_path);
    str_gsub(&split_words[i], "$$", env->smallsh_process_id);
    str_gsub(&split_words[i], "$?", env->last_fg_exec_return_val);
    str_gsub(&split_words[i], "$!", env->last_bg_exec_return_val);
  }
}
