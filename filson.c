#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int filson_cd(char **args);
int filson_help(char **args);
int filson_exit(char **args);
int filson_set(char **args);
int filson_echo(char **args);

char *builtin_str[] = {
  "cd",
  "help",
  "exit",
  "set",
  "echo"
};

int (*builtin_func[])(char **) = {
    &filson_cd,
    &filson_help,
    &filson_exit,
    &filson_set,
    &filson_echo
  };

  int filson_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
  }

  int filson_cd(char **args)
  {
    if (args[1] == NULL) {
      fprintf(stderr, "filson: expected argument to \"cd\?\n");
    } else {
      if (chdir(args[1]) != 0) {
	perror("lsh");
      }
    }
    return 1;
  }

  int filson_help(char ** args)
  {
    int i;
    printf("Bryan Copley's Filson\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");

    for (i = 0; i < filson_num_builtins(); i++) {
      printf(" %s\n", builtin_str[i]);
    }

    printf("Use the man command for information on other programs.\n");
    return 1;
  }

  int filson_exit(char **args)
  {
    return 0;
  }

  int filson_set(char **args)
  {
    if (args[1] == NULL || args[2] == NULL) {
      fprintf(stderr, "filson: expected arguments to \"set\" <var> <value>\n");
      return 1;
    }

    if (setenv(args[1], args[2], 1) != 0) {
      perror("filson");
    }
    return 1;
  }

  int filson_echo(char **args)
  {
    int i = 1;
    int first = 1;

    while (args[i] != NULL) {
      if (!first) {
        printf(" ");
      }
      first = 0;

      /* Check if argument starts with $ for variable expansion */
      if (args[i][0] == '$') {
        char *var_name = &args[i][1];
        char *var_value = getenv(var_name);
        if (var_value != NULL) {
          printf("%s", var_value);
        }
        /* If variable not found, print nothing (like bash) */
      } else {
        /* Print literal argument */
        printf("%s", args[i]);
      }
      i++;
    }
    printf("\n");
    return 1;
  }

  int filson_launch(char **args)
  {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
      if (execvp(args[0], args) == -1) {
	perror("filson");
      }
      exit(EXIT_FAILURE);
    } else if (pid < 0 ) {
      perror("filson");
    } else {
      do {
	wpid = waitpid(pid, &status, WUNTRACED);
      } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
  }

  int filson_execute(char **args)
  {
    int i;

    if (args[0] == NULL) {
      return 1;
    }

    for (i = 0; i < filson_num_builtins(); i++) {
      if (strcmp(args[0], builtin_str[i]) == 0) {
	return (*builtin_func[i])(args);
      }
    }

    return filson_launch(args);
  }

#define FILSON_RL_BUFSIZE 1024

  char *filson_read_line(void)
  {
    int bufsize = FILSON_RL_BUFSIZE;
    int position = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int c;

    if (!buffer) {
      fprintf(stderr, "filson: allocation error\n");
      exit(EXIT_FAILURE);
    }

    while (1) {
      c = getchar();

      if (c == EOF) {
	free(buffer);
	return NULL;
      } else if (c == '\n') {
	buffer[position] = '\0';
	return buffer;
      } else {
	buffer[position] = c;
      }
      position++;

      if (position >= bufsize) {
	bufsize += FILSON_RL_BUFSIZE;
	buffer = realloc(buffer, bufsize);
	if (!buffer) {
	  fprintf(stderr, "filson: allocation error\n");
	  exit(EXIT_FAILURE);
	}
      }
    }
  }

#define FILSON_TOK_BUFSIZE 64
#define FILSON_TOK_DELIM " \t\r\n\a"

  char **filson_split_line(char *line)
  {
    int bufsize = FILSON_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
      fprintf(stderr, "filson: allocation error\n");
      exit(EXIT_FAILURE);
    }

    token = strtok(line, FILSON_TOK_DELIM);
    while (token != NULL) {
      tokens[position] = token;
      position++;

      if (position >= bufsize) {
	bufsize += FILSON_TOK_BUFSIZE;
	tokens = realloc(tokens, bufsize * sizeof(char*));
	if (!tokens) {
	  fprintf(stderr, "filson: allocation error\n");
	  exit(EXIT_FAILURE);
	}
      }

      token = strtok(NULL, FILSON_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
  }

  void filson_loop(void)
  {
    char *line;
    char **args;
    int status;

    do {
      printf("filson>");
      line = filson_read_line();
      if (line == NULL) {
        break;
      }
      args = filson_split_line(line);
      status = filson_execute(args);

      free(line);
      free(args);
    } while (status);
  }

  int main(int argc, char **argv)
  {

    filson_loop();

    return EXIT_SUCCESS;
  }

