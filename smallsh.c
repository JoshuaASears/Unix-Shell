#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WORDS 512

pid_t smallsh_pid;
int fg_exit_status;
pid_t bg_pid;

FILE* in_file;
int fd;
char* read_line = 0;
size_t n = 0;
char* word_list[MAX_WORDS] = {0};

void cleanup(void);
void sigint_handler(int sig) {};

void exit_proc(int total_words);
void cd_proc(int total_words);

int split_words(ssize_t char_count);
void expand(int word_index);

int main(int argc, const char* argv[]) {
  
  bool interactive_mode = false;
  // too many arguments 
  if (argc > 2) {
    errno = E2BIG;
    err(EXIT_FAILURE, "Invoke with one file name or no arguments");
  
  // interactive mode (stdin) 
  } else if (argc == 1) {
    in_file = stdin;
    interactive_mode = true;

  // file mode
  } else {
    in_file = fopen(argv[1], "r");
    if (errno) {
      err(EXIT_FAILURE, "main fopen failed");
    } else {
      fd = fileno(in_file);
      int flags = fcntl(fd, F_GETFD);
      flags |= FD_CLOEXEC;
      fcntl(fd, F_SETFD, flags);
    };
  };

  // initialize shell variables ($$, $?, $!)
  smallsh_pid = getpid();
  fg_exit_status = 0;
  bg_pid = 0;

  // initialize shell signals
  // SIGTSTP (remove CTRL-Z functionality)
  struct sigaction SIGTSTP_action = {0};
  struct sigaction SIGTSTP_restore;
  SIGTSTP_action.sa_handler = SIG_IGN;
  if (sigaction(SIGTSTP, &SIGTSTP_action, &SIGTSTP_restore)) {
    err(EXIT_FAILURE, "main sigaction SIGTSTSP failed");
  };
  // SIGINT (limit CTRL-C functionality)
  struct sigaction SIGINT_action = {0};
  struct sigaction SIGINT_restore;
  SIGINT_action.sa_handler = SIG_IGN;
  if (sigaction(SIGINT, &SIGINT_action, &SIGINT_restore)) {
    err(EXIT_FAILURE, "main sigaction SIGINT failed");
  };
  
  // register exit handler
  if (atexit(cleanup)) {
    err(EXIT_FAILURE, "main atexit failed");
  };
  
  //repl
  char* prompt = getenv("PS1");
  for (;;) {

    // print prompt 
    if (interactive_mode) {
      fprintf(stderr, "%s", prompt);
    };

    // get line of input
    // sig handler for input line
    SIGINT_action.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &SIGINT_action, 0)) {
      err(EXIT_FAILURE, "main sigaction SIGINT failed");
    };
    ssize_t char_read = getline(&read_line, &n, in_file);
    // reset disposition
    SIGINT_action.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &SIGINT_action, 0)) {
      err(EXIT_FAILURE, "main sigaction SIGINT failed");
    };
    
    if (errno == EINTR) {
      clearerr(in_file);
      errno = 0;
      fprintf(stderr,"%c",'\n');
      continue;
    } else if (char_read == -1){
      exit(fg_exit_status); 
    };
    SIGINT_action.sa_handler = SIG_IGN;
    
    pid_t child_pid;
    int child_status;
    int bg_child_status;
    // manage background process
    if (bg_pid == waitpid(0, &bg_child_status, WNOHANG)) {
      if (WIFEXITED(bg_child_status)) {
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bg_pid, WEXITSTATUS(bg_child_status));
      } else if (WIFSIGNALED(bg_child_status)) {
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bg_pid, WTERMSIG(bg_child_status));
      } else if (WIFSTOPPED(bg_child_status)) {
        fprintf(stderr, "Child process %d stopped. Continuing.\n", bg_pid);
        if (kill(bg_pid, SIGCONT)) {
          err(EXIT_FAILURE, "main waiting kill SIGCONT failed");
        };
      };
    };

    errno = 0;
    // word splitting 
    int total_words = split_words(char_read);

    // expansion
    for (int i = 0; i < total_words; ++i) {
      expand(i);
    };
    int exec_i = 0;
    if (total_words > 0) {
          
      // built-ins: exit cd
      if (!(strcmp(word_list[0], "exit"))) {
        exit_proc(total_words);
      } else if (!(strcmp(word_list[0], "cd"))) {
        cd_proc(total_words);
     

      // non built-in: fork to exec and parsing
      } else {
      child_pid = fork();
      switch(child_pid) {
        case -1:
          err(EXIT_FAILURE, "main fork failed");
        
        // child
        case 0:

          fg_exit_status = 0;
          
          // reset signals in child
          if (sigaction(SIGTSTP, &SIGTSTP_restore, NULL)) {
            err(EXIT_FAILURE, "main sigaction SIGTSTSP restore failed");
          };
          if (sigaction(SIGINT, &SIGINT_restore, NULL)) {
            err(EXIT_FAILURE, "main sigaction SIGINT restore failed");
          };

          // parsing
          for (int i = 0; i < total_words; i++) {
            // < = read token
            if (!(strcmp(word_list[i], "<"))) {
              i++;
              if (i < total_words){
                int redirect = open(word_list[i], O_RDONLY | O_CLOEXEC);
                if (redirect == -1) {
                  perror("redirect: open");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
                int redirect_fd = dup2(redirect, 0);
                if (redirect_fd == -1) {
                  perror("dup2");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
              };

            // > = write token
            } else if (!(strcmp(word_list[i], ">"))) {
              i++;
              if (i < total_words){
                int redirect = open(word_list[i], O_RDWR | O_TRUNC | O_CREAT, 0777);
                if (redirect == -1) {
                  perror("redirect: open");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
                int redirect_fd = dup2(redirect, 1);
                if (redirect_fd == -1) {
                  perror("dup2");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
              };

            // >> = append token
            } else if (!(strcmp(word_list[i], ">>"))) {
              i++;
              if (i < total_words){
                int redirect = open(word_list[i], O_RDWR | O_APPEND | O_CREAT, 0777);
                if (redirect == -1) {
                  perror("redirect: open");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
                int redirect_fd = dup2(redirect, 1);
                if (redirect_fd == -1) {
                  perror("dup2");
                  fg_exit_status = errno;
                  goto LEAVE_FORK;
                };
              };

            // & = background token
            } else if (!(strcmp(word_list[i], "&"))) {
              continue;

            // add args to cmd
            } else {
              if (exec_i < i) {
                free(word_list[exec_i]);
              };
              word_list[exec_i] = word_list[i];
              exec_i++;
              
            };
          };
            // execution
            if (exec_i > 0) {
              word_list[exec_i] = realloc(word_list[exec_i], **word_list * 1);
              word_list[exec_i] = 0;
              execvp(word_list[0], word_list);
              perror("exec");
              fg_exit_status = errno;
              goto LEAVE_FORK;
            };
        LEAVE_FORK:;
          exit(fg_exit_status);
        
        // parent
        default:

          // waiting
          
          // wait for foreground
          if ((strcmp(word_list[total_words - 1], "&"))) {
            child_pid = waitpid(child_pid, &child_status, WUNTRACED);
            
            // set $? to exit stataus of foreground
            if (WIFEXITED(child_status)) {
              fg_exit_status = WEXITSTATUS(child_status);
            
            // foreground exited by a signal
            } else if (WIFSTOPPED(child_status)) {
              fprintf(stderr, "Child process %d stopped. Continuing.\n", child_pid);
              if (kill(child_pid, SIGCONT)) {
                err(EXIT_FAILURE, "main waiting kill SIGCONT failed");
              };
              bg_pid = child_pid;
              
            // if terminated by signal set $? to 128 + number of signal
            } else {
              fg_exit_status = 128 + WTERMSIG(child_status);
            };

          // default behavior: background indicated
          } else {
          // set $! to bg pid
          bg_pid = child_pid;
          };
    
      };
      };
    };
  };
};

/*** handlers ***/

/* exit handlers */
void cleanup(void){
  if (smallsh_pid == getpid()) {
    free(read_line);
    for (int x = 0; x < MAX_WORDS; x++) {
      free(word_list[x]);
    };
    if (in_file){
      fclose(in_file);
      if (errno) {
        err(EXIT_FAILURE, "cleanup fclose failed");
      };
    };
  };
};


/*** build in commands: exit, cd ***/

/* 
 * checks for valid arguments for exit call
 * prints to std error for invalid arguments
 * */
void exit_proc(int total_words){
  
  // too many arguments
  if (total_words > 2) {
    errno = E2BIG;
    fg_exit_status = errno;
    perror("exit");
    errno = 0;
    return;

  // valid number of arguments
  } else if (total_words == 2) {
    int user_exit_status = atoi(word_list[1]);
    
    // invalid argument type
    if (!user_exit_status) {
      errno = EINVAL;
      fg_exit_status = errno;
      perror("exit");
      errno = 0;
      return;
    
  // valid exit calls
    } else {  
      fg_exit_status = user_exit_status;
    };
   };
  exit(fg_exit_status);
}; 

/*
 * checks for valid arguments for exit call
 * prints to std error for invalid arguments
 * */
void cd_proc(int total_words){
  // too many artuments
  if (total_words > 2) {
    errno = E2BIG;
    fg_exit_status = errno;
    perror("cd");
    errno = 0;

  // valid number of arguments
  } else if (total_words == 2) {
    
    // user defined path 
    chdir(word_list[1]);
    if (errno) {
      fg_exit_status = errno;
      perror("cd");
      errno = 0;
    };
  
  // no args = root directory
  } else {
    chdir(getenv("HOME"));
    if (errno) {
      perror("cd");
      errno = 0;
    };
  };
};


/*** string augmentation functions for stdio/file input ***/

/* 
 * splits words based on white space
 * special character handling for comment (#) and escape(/) characters
 * returns number of words split from line
 * */
int split_words(ssize_t char_count) {
  int word_count = 0;
  size_t word_length = 0;
  bool backslash = false;

  for (int i = 0; i < char_count; ++i) {
    int c = read_line[i];

    // backslash: escape character
    if (c == '\\' && !backslash) {
      backslash = true;
      continue;

    // hashtag: end of line comment
    } else if (c == '#' && !backslash) {
      break;

    // whitespace: increase word_count
    } else if (isspace(c) && !backslash) {
      if (word_length != 0) {
        if (word_count == MAX_WORDS) break;
        word_count++;
        word_length = 0;
      };
      backslash = false;
      continue;

    // adding characters to word string
    } else {
      if (word_length == 0) {
        word_length = word_length + 2;
      } else {
        word_length++;
      };
      void *temp = realloc(word_list[word_count], sizeof **word_list * word_length);
      if (errno == ENOMEM) {
        err(EXIT_FAILURE, "split_words realloc failed");
      };
      word_list[word_count] = temp;
      word_list[word_count][word_length - 2] = c;
      word_list[word_count][word_length - 1] = '\0';
      backslash = false;
    };
   };
  return word_count;
};

/* 
 * expands words for special character ($) variables
 * */
void expand (int word_index) {
  
  size_t word_length = strlen(word_list[word_index]);
  int start = 0;
  int stop = 0;
  // iterate over word
  int i = 0;
  while (i < word_length) {
    if (word_list[word_index][i] == '$' && i+1 < word_length) {
      start = i;
      i++;

      // find special character sequences
      if (word_list[word_index][i] == 0x7b) {
        while (i < word_length) {
          i++;
          if (word_list[word_index][i] == '}') {
            stop = i;
            break;
          };
        };
      } else if (
          word_list[word_index][i] == '$' ||
          word_list[word_index][i] == '!' ||
          word_list[word_index][i] == '?') {
          stop = i;
      };

      // if special character start and stop indexes have been found
      // splice out special characters
      if (stop) {
        // copy before splice string
        int s1_len = start;
        char* before_splice = strndup(word_list[word_index], s1_len);
        if (errno) {
          err(EXIT_FAILURE, "expand: strndup before_splice");
        }; 
        
        // copy after splice string
        int s3_len = word_length - stop;
        char* after_splice = strndup(&word_list[word_index][stop+1], s3_len);
        if (errno) {
          err(EXIT_FAILURE, "expand: strndup after_splice");
        }; 
        // splice_string
        int splice_size = 0;
        int splice_start = 0;
        if (word_list[word_index][start+1] != 0x7b) {
          splice_size = stop - start + 1;
          splice_start = start;
        } else {
          splice_size = stop - start - 2;
          splice_start = start + 2;
        };
        if (splice_size > 0) { 
          // character case handling
          char* splice = strndup(&word_list[word_index][splice_start], splice_size);
          if (errno) {
            err(EXIT_FAILURE, "expand: strndup splice");
          }; 
          char* replacement = malloc(7);
          if (splice[1] == '$') {

          // $$: replace with PID
            sprintf(replacement, "%d", smallsh_pid);
            if (errno) {
              err(EXIT_FAILURE, "expand: sprintf $$ replace");
            }; 

          // $?: replace with exit status of last foreground command or 0
          } else if (splice[1] == '?') {
            sprintf(replacement, "%d", fg_exit_status);
            if (errno) {
              err(EXIT_FAILURE, "expand: sprintf $? replace");
            }; 

          // $!: replace with PID of most recent background process or empty str
          } else if (splice[1] == '!') {
              if (bg_pid) {
                sprintf(replacement, "%d", bg_pid);
                if (errno) {
                  err(EXIT_FAILURE, "expand: sprintf $! replace");
                }; 
              };

          // ${parameter}: replace with value of environment variable or empty str
          } else {
            char* get_temp = getenv(splice);
            if (get_temp) {
              void* temp_p1 = realloc(replacement, sizeof (char*) * (strlen(get_temp)+ 1));
              if (errno == ENOMEM) {
                err(EXIT_FAILURE, "expand ${parameter} realloc 1 failed");
              };
              replacement = temp_p1;
              strcpy(replacement, get_temp);
            } else {
              void* temp_p2 = realloc(replacement, 1);
              if (errno == ENOMEM) {
                err(EXIT_FAILURE, "expand ${parameter} realloc 2 failed");
              };
              replacement = temp_p2;
            };
          };
          // word_list[word_index] realloc to the three items above
          
          int s2_len = strlen(replacement);
          word_length = s1_len + s2_len + s3_len;
          i = s1_len;
          void* temp_p3 = realloc(word_list[word_index], word_length);
          if (errno == ENOMEM) {
            err(EXIT_FAILURE, "expand realloc 3 failed");
          };
          word_list[word_index] = temp_p3; 
          if (s1_len > 0) {
            strcpy(&word_list[word_index][0], before_splice);
          };
          if (s2_len > 0) {
            strcpy(&word_list[word_index][s1_len], replacement);
          };
          if (s3_len > 0) {
            strcpy(&word_list[word_index][s1_len+s2_len], after_splice);
          };
          //word_list[word_index][word_length] = '\0';
          free(replacement); 
          free(before_splice);
          free(after_splice);
          free(splice);
        };
        stop = 0;
        continue;
      };
    };
    i++;
  };
};

