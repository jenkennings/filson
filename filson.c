#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"

int filson_cd(char **args);
int filson_help(char **args);
int filson_exit(char **args);
int filson_set(char **args);
int filson_echo(char **args);
int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int background, char *segment);
char **filson_split_line(char *line);

int filson_last_cmd_success = 1;

char *builtin_str[] = {
	"cd",
	"help",
	"exit",
	"set",
	"echo",
	"history",
	"jobs",
	"fg",
	"bg"
};

int (*builtin_func[])(char **) = {
	&filson_cd,
	&filson_help,
	&filson_exit,
	&filson_set,
	&filson_echo,
	&filson_history,
	&filson_jobs,
	&filson_fg,
	&filson_bg
};

int
filson_num_builtins(void)
{
	return sizeof(builtin_str) / sizeof(char *);
}

int
filson_is_valid_varname(const char *name)
{
	int i;

	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	if (!((name[0] >= 'a' && name[0] <= 'z') || 
	      (name[0] >= 'A' && name[0] <= 'Z') || 
	      name[0] == '_')) {
		return 0;
	}
	for (i = 1; name[i] != '\0'; i++) {
		if (!((name[i] >= 'a' && name[i] <= 'z') || 
		      (name[i] >= 'A' && name[i] <= 'Z') || 
		      (name[i] >= '0' && name[i] <= '9') || 
		      name[i] == '_')) {
			return 0;
		}
	}
	return 1;
}

int
filson_arg_count(char **args)
{
	int count;

	count = 0;
	while (args[count] != NULL) {
		count++;
	}
	return count;
}

int
filson_path_is_safe(void)
{
	char *path_str;
	char *path_copy, *token;
	int safe;

	path_str = getenv("PATH");
	if (path_str == NULL) {
		return 1;
	}
	path_copy = malloc(strlen(path_str) + 1);
	if (path_copy == NULL) {
		return 1;
	}
	strcpy(path_copy, path_str);
	safe = 1;
	token = strtok(path_copy, ":");
	while (token != NULL) {
		if (strcmp(token, ".") == 0 || strcmp(token, "") == 0) {
			fprintf(stderr, "filson: warning - current directory in PATH\n");
			safe = 0;
			break;
		}
		token = strtok(NULL, ":");
	}
	free(path_copy);
	return safe;
}

int
filson_cd(char **args)
{
	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected argument to \"cd\"\n");
		filson_last_cmd_success = 0;
	} else {
		if (chdir(args[1]) != 0) {
			perror("filson");
			filson_last_cmd_success = 0;
		} else {
			filson_last_cmd_success = 1;
		}
	}
	return 1;
}

int
filson_help(char **args)
{
	int i;

	(void)args;
	filson_last_cmd_success = 1;
	printf("Bryan Copley's Filson\n");
	printf("Type program names and arguments, and hit enter.\n");
	printf("The following are built in:\n");
	for (i = 0; i < filson_num_builtins(); i++) {
		printf("  %s\n", builtin_str[i]);
	}
	printf("Use the man command for information on other programs.\n");
	return 1;
}

int
filson_exit(char **args)
{
	(void)args;
	filson_last_cmd_success = 1;
	return 0;
}

int
filson_set(char **args)
{
	if (args[1] == NULL || args[2] == NULL) {
		fprintf(stderr, "filson: expected arguments to \"set\" <var> <value>\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (!filson_is_valid_varname(args[1])) {
		fprintf(stderr, "filson: invalid variable name: %s\n", args[1]);
		filson_last_cmd_success = 0;
		return 1;
	}
	if (setenv(args[1], args[2], 1) != 0) {
		perror("filson");
		filson_last_cmd_success = 0;
	} else {
		filson_last_cmd_success = 1;
	}
	return 1;
}

int
filson_echo(char **args)
{
	int i, first;
	char *var_name, *var_value;

	i = 1;
	first = 1;
	while (args[i] != NULL) {
		if (!first) {
			printf(" ");
		}
		first = 0;
		if (args[i][0] == '$') {
			var_name = &args[i][1];
			var_value = getenv(var_name);
			if (var_value != NULL) {
				printf("%s", var_value);
			}
		} else {
			printf("%s", args[i]);
		}
		i++;
	}
	printf("\n");
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_launch(char **args, int background, char *segment)
{
	pid_t pid;
	int status;
	int job_id;

	pid = fork();
	if (pid == 0) {
		if (execvp(args[0], args) == -1) {
			perror("filson");
		}
		exit(EXIT_FAILURE);
	} else if (pid < 0) {
		perror("filson");
		filson_last_cmd_success = 0;
	} else {
		if (background) {
			job_id = filson_add_job(pid, segment, 0);
			if (job_id < 0) {
				fprintf(stderr, "filson: too many background jobs\n");
				filson_last_cmd_success = 0;
			} else {
				printf("[%d] %d\n", job_id, pid);
				filson_last_cmd_success = 1;
			}
		} else {
			do {
				waitpid(pid, &status, WUNTRACED);
			} while (!WIFEXITED(status) && !WIFSIGNALED(status) && !WIFSTOPPED(status));
			if (WIFSTOPPED(status)) {
				job_id = filson_add_job(pid, segment, 1);
				if (job_id >= 0) {
					printf("[%d] Stopped %s\n", job_id, segment);
				}
				filson_last_cmd_success = 0;
			} else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
				filson_last_cmd_success = 1;
			} else {
				filson_last_cmd_success = 0;
			}
		}
	}
	return 1;
}

int
filson_execute(char **args, int background, char *segment)
{
	int i;

	if (args[0] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	if (filson_arg_count(args) > 1024) {
		fprintf(stderr, "filson: too many arguments\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_path_is_safe();
	for (i = 0; i < filson_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			if (background) {
				fprintf(stderr, "filson: cannot run built-in in background\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			return (*builtin_func[i])(args);
		}
	}
	return filson_launch(args, background, segment);
}

#define FILSON_RL_BUFSIZE 1024
#define FILSON_PROMPT "filson> "

static void
filson_refresh_line(const char *buffer)
{
	printf("\r%s%s\033[K", FILSON_PROMPT, buffer);
	fflush(stdout);
}

char *
filson_read_line(void)
{
	int bufsize, position, c;
	int interactive, history_cursor, history_count;
	char *buffer;
	const char *history_entry;
	struct termios oldt, newt;

	bufsize = FILSON_RL_BUFSIZE;
	position = 0;
	buffer = malloc(sizeof(char) * bufsize);
	if (!buffer) {
		fprintf(stderr, "filson: allocation error\n");
		exit(EXIT_FAILURE);
	}
	buffer[0] = '\0';
	interactive = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
	history_count = filson_history_count_entries();
	history_cursor = history_count;
	if (interactive) {
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		newt.c_cc[VMIN] = 1;
		newt.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	while (1) {
		c = getchar();
		if (interactive && c == 27) {
			int next1, next2;

			next1 = getchar();
			next2 = getchar();
			if (next1 == '[' && (next2 == 'A' || next2 == 'B')) {
				if (next2 == 'A' && history_cursor > 0) {
					history_cursor--;
				} else if (next2 == 'B' && history_cursor < history_count) {
					history_cursor++;
				}
				if (history_cursor >= 0 && history_cursor < history_count) {
					history_entry = filson_history_get(history_cursor);
					if (history_entry == NULL) {
						history_entry = "";
					}
					while ((int)strlen(history_entry) >= bufsize) {
						bufsize += FILSON_RL_BUFSIZE;
						buffer = realloc(buffer, bufsize);
						if (!buffer) {
							fprintf(stderr, "filson: allocation error\n");
							exit(EXIT_FAILURE);
						}
					}
					strcpy(buffer, history_entry);
					position = strlen(buffer);
				} else {
					position = 0;
					buffer[0] = '\0';
				}
				filson_refresh_line(buffer);
			}
			continue;
		}
		if (interactive && c == '\t') {
			filson_handle_autocomplete(&buffer, &bufsize, &position,
				builtin_str, filson_num_builtins(), filson_refresh_line);
			continue;
		}
		if (c == EOF || (interactive && c == 4 && position == 0)) {
			if (interactive) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			}
			free(buffer);
			return NULL;
		} else if (c == '\n') {
			if (interactive) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				printf("\n");
			}
			buffer[position] = '\0';
			return buffer;
		} else if (interactive && (c == 127 || c == '\b')) {
			if (position > 0) {
				position--;
				buffer[position] = '\0';
				history_cursor = history_count;
				filson_refresh_line(buffer);
			}
		} else if (interactive && c >= 32 && c <= 126) {
			buffer[position] = (char)c;
			position++;
			buffer[position] = '\0';
			if (position >= bufsize - 1) {
				bufsize += FILSON_RL_BUFSIZE;
				buffer = realloc(buffer, bufsize);
				if (!buffer) {
					fprintf(stderr, "filson: allocation error\n");
					exit(EXIT_FAILURE);
				}
			}
			history_cursor = history_count;
			filson_refresh_line(buffer);
		} else {
			if (!interactive) {
				buffer[position] = c;
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
	}
}

#define FILSON_TOK_BUFSIZE 64
#define FILSON_TOK_DELIM " \t\r\n\a"

char **
filson_split_line(char *line)
{
	int bufsize, position;
	char **tokens, *token;

	bufsize = FILSON_TOK_BUFSIZE;
	position = 0;
	tokens = malloc(bufsize * sizeof(char *));
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
			tokens = realloc(tokens, bufsize * sizeof(char *));
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

void
filson_loop(void)
{
	char *line, *resolved;
	int status;

	status = 1;
	do {
		filson_reap_background_jobs();
		printf(FILSON_PROMPT);
		fflush(stdout);
		line = filson_read_line();
		if (line == NULL) {
			printf("\n");
			break;
		}
		resolved = filson_resolve_history(line);
		if (resolved == NULL) {
			free(line);
			continue;
		}
		filson_add_history(resolved);
		status = filson_execute_and_chain(resolved);
		free(resolved);
		free(line);
	} while (status);
	filson_clear_history();
}

