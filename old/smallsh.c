#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>

int main (int argc, const char* argv[]) {

  /* TODO: Input */
  
  // TODO: input for interactive mode
  //  TODO: see notes for interruption by signal
  //  TODO: see notes for CLEARERR(3)
  
  // input from script file
  FILE* fp;
  if (argc == 2) {
    fp = fopen(argv[1], "r");
    if (errno) {
      perror("fopen() failed");
    };
  };
  

  char *line = NULL;
  size_t len = 0;
  ssize_t nread;
  while ((nread = getline(&line, &len, fp)) != -1) {
    printf("Retrieved line of length %zd:\n", nread);
    printf("%s", line);
  }
  if (errno) {
    perror("getline failed or EOF");
  };
  free(line);




  // close input script file
  if (fp) {
    int close_fp = fclose(fp);
    if (close_fp) {
      perror("error closing");
    }
  };


  return EXIT_SUCCESS;
};

/* TODO: Word Splitting */
/* word_split () {

};
*/
// line of input split into words
// deliminate by whitespace characters (ISSPACE(3)) including <newline>
// backslashes are removed and include next character in current word even if whitespace
// '#' is a comment character and all following it will be removed



/* TODO: Expansion */

/* TODO: Parsing */

/* TODO: Execution */

/* TODO: Waiting */
