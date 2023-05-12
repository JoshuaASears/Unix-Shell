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

int split_words(char* lineptr, ssize_t char_count, char* buffer[MAX_WORDS]);
size_t replace_characters(char* input_string, char* output_string); 
void expand(char* buffer[MAX_WORDS], int word_index);

int main(int argc, const char* argv[]) {
  smallsh_pid = getpid();
  FILE* in_file;
  
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

  
  char* in_line = NULL;
  size_t n = 0;
  
  //repl
  for (int i = 1;;i++) {
    fg_exit_status = 0;
    bg_pid = 0;
    printf("REPL %d\n", i);
    
    // get line of input 
    ssize_t n_read = getline(&in_line, &n, in_file);
    if (n_read == -1){
      perror("main: rpl: end of file\n");
      goto EXIT;
    } else if (errno == EINVAL){
      err(EXIT_FAILURE, "main getline failed");
    };
    
    // word splitting
    char* words[MAX_WORDS] = {0};
    int total_words = split_words(in_line, n_read, words);
    // expansion
    for (int i = 0; i < total_words; ++i) {
      printf("Word %i: %s -> ", i+1, words[i]);
      expand(words, i);
      printf("%s\n", words[i]);
    };
    // parsing
    // if last word is & then entire line is background
    // > = write
    // < = read
    // >> = append
    // any word following >, <, >> is considered a path operator
    // execution
    // waiting
    for (int i = 0; i < total_words; ++i) {
      free(words[i]);
    };

  };

  // cleanup and exit
EXIT:;
  free(in_line);
  fclose(in_file);
  if (errno) {
    err(EXIT_FAILURE, "main fclose failed");
  };
  return EXIT_SUCCESS;
};




/* 
 * splits words based on white space
 * special character handling for comment (#) and escape(/) characters
 * returns number of words split from line
 * */
int split_words(char* lineptr, ssize_t char_count, char* buffer[MAX_WORDS]) {
  
  int word_count = 0;
  size_t word_length = 0;
  bool backslash = false;
  for (int i = 0; i < char_count; ++i) {
    int c = lineptr[i];
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
      void *temp = realloc(buffer[word_count], sizeof **buffer * word_length);
      if (errno == ENOMEM) {
        err(EXIT_FAILURE, "split_words realloc failed");
      };
      buffer[word_count] = temp;
      buffer[word_count][word_length - 2] = c;
      buffer[word_count][word_length - 1] = '\0';
      backslash = false;
    };
   };
  return word_count;
};



/* 
 * case handling for character expansion
 * returns size of output string
 * */
size_t replace_characters(char* input_string, char* output_string) {
  char return_string[100] = {'\0'};
  switch (input_string[1]) {
  // $$: replace with PID
    case '$':
      sprintf(return_string, "%d", smallsh_pid);
      break;
  // $?: replace with exit status of last foreground command or 0
    case '?':
      sprintf(return_string, "%d", fg_exit_status);
      break;
  // $!: replace with PID of most recent background process or empty str
    case '!':
      if (bg_pid) {
        sprintf(return_string, "%d", bg_pid);
      };
      break;
  // ${parameter}: replace with value of environment variable or empty str
    default:
      break;
  };
  if (errno == ENOMEM) {
    err(EXIT_FAILURE, "replace_characters strdup failed");
  };
  size_t return_length = 0;
  for (int count = 0; return_string[count] != '\0'; count++){
    output_string[count] = return_string[count];
    return_length++;
  };
  return return_length;
};



/* 
 * expands words for special $ character sequences
 * 
 * */
void expand (char* buffer[MAX_WORDS], int word_index) {
  
  // get word length
  size_t word_length = 0;
  for (int count = 0; buffer[word_index][count] != '\0'; count++) {
    word_length++;
  };

  int start = 0;
  int stop = 0;
  // iterate over word
  for (int i = 0; i < word_length; i++) {
    if (buffer[word_index][i] == '$' && i+1 < word_length) {
      start = i;
      i++;
      // find special character sequences
      if (buffer[word_index][i] == 0x7b) {
        while (i < word_length) {
          i++;
          if (buffer[word_index][i] == '}') {
            stop = i;
            break;
          };
        };
      } else if (
          buffer[word_index][i] == '$' ||
          buffer[word_index][i] == '!' ||
          buffer[word_index][i] == '?') {
          stop = i;
      };
      // if special character start and stop indexes have been found
      // splice out special characters
      if (stop) {
        size_t splice_len = stop - start + 1;
        char* splice_out = strndup(&buffer[word_index][start], splice_len);
        if (errno == ENOMEM) {
          err(EXIT_FAILURE, "expand strndup failed");
        };      
        size_t after_splice_len = word_length - stop - 1;
        char* after_splice = malloc(after_splice_len);
        if (errno == ENOMEM) {
          err(EXIT_FAILURE, "expand malloc failed");
        };
        strncpy(after_splice, &buffer[word_index][start + splice_len], after_splice_len);
        word_length = word_length - splice_len;
        char splice_in[100];
        // resize word and splice in new string
        splice_len = replace_characters(splice_out, splice_in);
        word_length = word_length + splice_len;
        void* tmp = realloc(buffer[word_index], sizeof **buffer * word_length + 1);
        if (errno == ENOMEM) {
          err(EXIT_FAILURE, "expand realloc failed");
        };
        buffer[word_index] = tmp;
        buffer[word_index][word_length] = '\0';
        strncpy(&buffer[word_index][start], splice_in, splice_len);
        strncpy(&buffer[word_index][start + splice_len], after_splice, after_splice_len);
        free(splice_out);
        free(after_splice);
        stop = 0;
      };
    };
  };
};



/* features */
// special parameters: $$, $?, $!, ${parameter}
// built-in commands: exit, cd
// non build=in commands using appropriate EXEC(3)
// redirection operators: <, >, >>
// use "&" to run commands in background
// custom behavior for signals: SIGINT, SIGTSTP
// all errors print value of $? to stderr
