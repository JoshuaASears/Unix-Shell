#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>

#define MAX_WORDS 512


pid_t smallsh_pid;
int fg_exit_status;
pid_t bg_pid;
bool exiting = false;

FILE* in_file;
char* read_line = NULL;
size_t n = 0;

void exit_proc(int total_words, char* word_list[MAX_WORDS]);
void cd_proc(int total_words, char* word_list[MAX_WORDS]);

void cleanup(void);

int split_words(ssize_t char_count, char* word_list[MAX_WORDS]);
void expand(int word_index, char* word_list[MAX_WORDS]);

int main(int argc, const char* argv[]) {
  smallsh_pid = getpid();

  // register exit handler
  int handler_1 = atexit(cleanup);
  if (handler_1) {
    err(EXIT_FAILURE, "Exit handler not set");
  };
  
  // too many arguments 
  if (argc > 2) {
    errno = E2BIG;
    err(EXIT_FAILURE, "Invoke with one file name or no arguments");
  
  // interactive mode (stdin) 
  } else if (argc == 1) {
    printf("No file, opening interactive mode.\n");
    in_file = stdin;
    errno = ENOSYS;
    err(EXIT_FAILURE, "Interactive mode");

  // file mode
  } else {
    printf("File detected, opening file...\n");
    in_file = fopen(argv[1], "r");
    if (errno) {
      err(EXIT_FAILURE, "main fopen failed");
    } else {
      printf("Open successful.\n");
      int fd = fileno(in_file);
      int flags = fcntl(fd, F_GETFD);
      flags |= O_CLOEXEC;
      fcntl(fd, F_SETFD, flags);
    };
  };
  
  //repl
  for (int i = 1;;i++) {
    
    // reset tracking variables
    fg_exit_status = 0;
    bg_pid = 0;
    printf("REPL %d\n", i);
    
    // get line of input 
    ssize_t char_read = getline(&read_line, &n, in_file);
    if (char_read == -1){
      exit(fg_exit_status); 
    } else if (errno == EINVAL){
      err(EXIT_FAILURE, "main getline failed");
    };
    
    // word splitting
    char* word_list[MAX_WORDS] = {0};
    int total_words = split_words(char_read, word_list);
    printf("total_words = %d\n", total_words);

    // expansion
    for (int i = 0; i < total_words; ++i) {
      printf("Word %i: %s -> ", i+1, word_list[i]);
      expand(i, word_list);
      printf("%s\n", word_list[i]);
    };
     
    if (total_words > 0) {
          
      // built-ins: exit cd
      int exit_called = strcmp(word_list[0], "exit");
      int cd_called = strcmp(word_list[0], "cd");

      if (exit_called == 0) {
        exit_proc(total_words, word_list);
        goto CLEAN_WORD_LIST;  
      } else if (cd_called == 0) {
        cd_proc(total_words, word_list);
        goto CLEAN_WORD_LIST;
      };
    
      // parsing (in child)
        // if last word is & then entire line is background
        // > = write
        // < = read
        // >> = append
        // any word following >, <, >> is considered a path operator
    
      // execution

      // waiting

CLEAN_WORD_LIST:;
      for (int i = 0; i < total_words; ++i) {
        free(word_list[i]);
      };
      if (exiting) {
        exit(fg_exit_status); 
      };
    };
  };
};


/* exit handlers */
void cleanup(void){
  free(read_line);
  fclose(in_file);
  if (errno) {
    err(EXIT_FAILURE, "main fclose failed");
  };
};

/*** build in commands: exit, cd ***/

/* 
 * checks for valid arguments for exit call
 * prints to std error for invalid arguments
 * */
void exit_proc(int total_words, char* word_list[MAX_WORDS]){
  
  // too many arguments
  if (total_words > 2) {
    errno = E2BIG;
    perror("exit");
    errno = 0;
    return;

  // valid number of arguments
  } else if (total_words == 2) {
    int user_exit_status = atoi(word_list[1]);
    
    // invalid argument type
    if (!user_exit_status) {
      errno = EINVAL;
      perror("exit");
      errno = 0;
      return;
    
  // valid exit calls
    } else {  
      fg_exit_status = user_exit_status;
    };
   };
  exiting = true;
}; 

/*
 * checks for valid arguments for exit call
 * prints to std error for invalid arguments
 * */
void cd_proc(int total_words, char* word_list[MAX_WORDS]){
  // too many artuments
  if (total_words > 2) {
    errno = E2BIG;
    perror("cd");
    errno = 0;

  // valid number of arguments
  } else if (total_words == 2) {
    
    // user defined path 
    chdir(word_list[1]);
    if (errno) {
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
int split_words(ssize_t char_count, char* word_list[MAX_WORDS]) {
  
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
void expand (int word_index, char* word_list[MAX_WORDS]) {
  
  // get word length
  size_t word_length = 0;
  for (int count = 0; word_list[word_index][count] != '\0'; count++) {
    word_length++;
  };

  int start = 0;
  int stop = 0;

  // iterate over word
  for (int i = 0; i < word_length; i++) {
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
        
        // copy after splice string
        int s3_len = word_length - stop;
        char* after_splice = strndup(&word_list[word_index][stop+1], s3_len);
        
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
          char replacement[255] = {'\0'};
          if (splice[1] == '$') {

          // $$: replace with PID
            sprintf(replacement, "%d", smallsh_pid);

          // $?: replace with exit status of last foreground command or 0
          } else if (splice[1] == '?') {
            sprintf(replacement, "%d", fg_exit_status);

          // $!: replace with PID of most recent background process or empty str
          } else if (splice[1] == '!') {
              if (bg_pid) {
                sprintf(replacement, "%d", bg_pid);
              };

          // ${parameter}: replace with value of environment variable or empty str
          } else {
            strcpy(replacement, getenv(splice)); 
          };
        
          // word_list[word_index] realloc to the three items above
          int s2_len = strlen(replacement); 
          word_list[word_index] = realloc(word_list[word_index], sizeof s1_len + s2_len + s3_len + 1);
          strcpy(word_list[word_index], before_splice);
          strcpy(&word_list[word_index][s1_len], replacement);
          strcpy(&word_list[word_index][s1_len+s2_len], after_splice);
        
          // cleanup
          free(splice);
        };
        free(before_splice);
        free(after_splice);
        stop = 0;
      };
    };
  };
};

