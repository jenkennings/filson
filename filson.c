#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"
#include "globbing.h"

int filson_cd(char **args);
int filson_help(char **args);
int filson_exit(char **args);
int filson_set(char **args);
int filson_echo(char **args);
int filson_pwd(char **args);
int filson_clear(char **args);
int filson_unset(char **args);
int filson_export(char **args);
int filson_type(char **args);
int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int background, char *segment);
char **filson_split_line(char *line);

static int filson_is_assignment_token(const char *token);
static int filson_parse_assignment_token(const char *token, char **name_out, const char **value_out);
static char *filson_expand_parameter(const char *arg);
static void filson_expand_arguments(char **args);
static int filson_run_command_only(char **args, int background, char *segment);
static int filson_run_with_temp_assignments(char **args, int assign_count, int background, char *segment);

int filson_last_cmd_success = 1;

char *builtin_str[] = {
	"cd",
	"help",
	"exit",
	"set",
	"echo",
	"pwd",
	"clear",
	"unset",
	"export",
	"type",
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
	&filson_pwd,
	&filson_clear,
	&filson_unset,
	&filson_export,
	&filson_type,
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
			fprintf(stderr, "filson: error - unsafe PATH: current directory entry found; refusing external execution\n");
			safe = 0;
			break;
		}
		token = strtok(NULL, ":");
	}
	free(path_copy);
	return safe;
}

static char *
filson_expand_parameter(const char *arg)
{
	const char *var_name, *default_value, *value, *pattern;
	const char *close_brace, *colon, *hash;
	char *result, *var_name_copy;
	size_t var_len, value_len, result_len, prefix_len, i;

	if (arg == NULL || arg[0] != '$') {
		return strdup(arg);
	}
	if (arg[1] != '{') {
		var_name = &arg[1];
		value = getenv(var_name);
		return strdup(value != NULL ? value : "");
	}
	close_brace = strchr(&arg[2], '}');
	if (close_brace == NULL) {
		return strdup(arg);
	}
	var_len = (size_t)(close_brace - &arg[2]);
	hash = memchr(&arg[2], '#', var_len);
	colon = memchr(&arg[2], ':', var_len);
	if (hash == NULL && colon == NULL) {
		var_name_copy = malloc(var_len + 1);
		if (var_name_copy == NULL) {
			return strdup(arg);
		}
		memcpy(var_name_copy, &arg[2], var_len);
		var_name_copy[var_len] = '\0';
		value = getenv(var_name_copy);
		result = strdup(value != NULL ? value : "");
		free(var_name_copy);
		return result;
	}
	if (hash != NULL && (colon == NULL || hash < colon)) {
		prefix_len = (size_t)(hash - &arg[2]);
		var_name_copy = malloc(prefix_len + 1);
		if (var_name_copy == NULL) {
			return strdup(arg);
		}
		memcpy(var_name_copy, &arg[2], prefix_len);
		var_name_copy[prefix_len] = '\0';
		value = getenv(var_name_copy);
		if (value == NULL) {
			free(var_name_copy);
			return strdup("");
		}
		pattern = hash + 1;
		value_len = (size_t)(close_brace - pattern);
		result_len = strlen(value);
		for (i = 0; i < value_len && i < result_len; i++) {
			if (value[i] != pattern[i]) {
				break;
			}
		}
		result = malloc(result_len - i + 1);
		if (result == NULL) {
			free(var_name_copy);
			return strdup(arg);
		}
		strcpy(result, &value[i]);
		free(var_name_copy);
		return result;
	}
	prefix_len = (size_t)(colon - &arg[2]);
	var_name_copy = malloc(prefix_len + 1);
	if (var_name_copy == NULL) {
		return strdup(arg);
	}
	memcpy(var_name_copy, &arg[2], prefix_len);
	var_name_copy[prefix_len] = '\0';
	value = getenv(var_name_copy);
	if (colon[1] == '-') {
		default_value = &colon[2];
		value_len = (size_t)(close_brace - default_value);
		if (value == NULL || value[0] == '\0') {
			result = malloc(value_len + 1);
			if (result == NULL) {
				free(var_name_copy);
				return strdup(arg);
			}
			memcpy(result, default_value, value_len);
			result[value_len] = '\0';
			free(var_name_copy);
			return result;
		}
		free(var_name_copy);
		return strdup(value);
	}
	free(var_name_copy);
	return strdup(arg);
}

static void
filson_expand_arguments(char **args)
{
	int i;
	char *expanded, *old;

	if (args == NULL) {
		return;
	}
	for (i = 0; args[i] != NULL; i++) {
		expanded = filson_expand_parameter(args[i]);
		if (expanded != args[i]) {
			old = args[i];
			args[i] = expanded;
			free(old);
		}
	}
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

	i = 1;
	first = 1;
	while (args[i] != NULL) {
		if (!first) {
			printf(" ");
		}
		first = 0;
		printf("%s", args[i]);
		i++;
	}
	printf("\n");
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_pwd(char **args)
{
	char buf[4096];

	(void)args;
	if (getcwd(buf, sizeof(buf)) == NULL) {
		perror("filson");
		filson_last_cmd_success = 0;
	} else {
		printf("%s\n", buf);
		filson_last_cmd_success = 1;
	}
	return 1;
}

int
filson_clear(char **args)
{
	(void)args;
	printf("\033[2J\033[H");
	fflush(stdout);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_unset(char **args)
{
	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected argument to \"unset\"\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	unsetenv(args[1]);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_export(char **args)
{
	const char *value;
	char *name;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected argument to \"export\"\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (!filson_parse_assignment_token(args[1], &name, &value)) {
		if (!filson_is_valid_varname(args[1])) {
			fprintf(stderr, "filson: invalid variable name: %s\n", args[1]);
			filson_last_cmd_success = 0;
			return 1;
		}
		value = getenv(args[1]);
		if (value == NULL) {
			fprintf(stderr, "filson: variable not set: %s\n", args[1]);
			filson_last_cmd_success = 0;
			return 1;
		}
		filson_last_cmd_success = 1;
		return 1;
	}
	if (setenv(name, value, 1) != 0) {
		perror("filson");
		free(name);
		filson_last_cmd_success = 0;
		return 1;
	}
	free(name);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_type(char **args)
{
	int i;
	char *path, *token, *path_copy, *full_path;
	size_t needed;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected argument to \"type\"\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (i = 0; i < filson_num_builtins(); i++) {
		if (strcmp(args[1], builtin_str[i]) == 0) {
			printf("%s is a shell builtin\n", args[1]);
			filson_last_cmd_success = 1;
			return 1;
		}
	}
	path = getenv("PATH");
	if (path == NULL) {
		printf("%s: not found\n", args[1]);
		filson_last_cmd_success = 0;
		return 1;
	}
	path_copy = malloc(strlen(path) + 1);
	if (path_copy == NULL) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	strcpy(path_copy, path);
	token = strtok(path_copy, ":");
	while (token != NULL) {
		needed = strlen(token) + strlen(args[1]) + 2;
		full_path = malloc(needed);
		if (full_path == NULL) {
			perror("filson");
			free(path_copy);
			filson_last_cmd_success = 0;
			return 1;
		}
		snprintf(full_path, needed, "%s/%s", token, args[1]);
		if (access(full_path, X_OK) == 0) {
			printf("%s\n", full_path);
			free(full_path);
			free(path_copy);
			filson_last_cmd_success = 1;
			return 1;
		}
		free(full_path);
		token = strtok(NULL, ":");
	}
	free(path_copy);
	printf("%s: not found\n", args[1]);
	filson_last_cmd_success = 0;
	return 1;
}

int
filson_launch(char **args, int background, char *segment)
{
	pid_t pid;
	int status;
	int job_id;
	char **expanded;

	pid = fork();
	if (pid == 0) {
		expanded = filson_expand_globs(args);
		if (execvp(expanded[0], expanded) == -1) {
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

static int
filson_is_assignment_token(const char *token)
{
	char *name;
	const char *value;
	int ok;

	ok = filson_parse_assignment_token(token, &name, &value);
	if (ok) {
		free(name);
	}
	return ok;
}

static int
filson_parse_assignment_token(const char *token, char **name_out, const char **value_out)
{
	const char *eq;
	char *name;
	size_t name_len;

	if (token == NULL) {
		return 0;
	}
	eq = strchr(token, '=');
	if (eq == NULL || eq == token) {
		return 0;
	}
	name_len = (size_t)(eq - token);
	name = malloc(name_len + 1);
	if (name == NULL) {
		return 0;
	}
	memcpy(name, token, name_len);
	name[name_len] = '\0';
	if (!filson_is_valid_varname(name)) {
		free(name);
		return 0;
	}
	*name_out = name;
	*value_out = eq + 1;
	return 1;
}

static int
filson_run_command_only(char **args, int background, char *segment)
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
	if (!filson_path_is_safe()) {
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_expand_arguments(args);
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

static int
filson_run_with_temp_assignments(char **args, int assign_count, int background, char *segment)
{
	char *names[128];
	char *old_values[128];
	int had_old[128];
	const char *value;
	const char *old_value;
	int i;
	int result;

	if (assign_count > 128) {
		fprintf(stderr, "filson: too many inline assignments\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (i = 0; i < assign_count; i++) {
		if (!filson_parse_assignment_token(args[i], &names[i], &value)) {
			filson_last_cmd_success = 0;
			while (--i >= 0) {
				free(names[i]);
				free(old_values[i]);
			}
			fprintf(stderr, "filson: invalid inline assignment\n");
			return 1;
		}
		old_value = getenv(names[i]);
		if (old_value != NULL) {
			had_old[i] = 1;
			old_values[i] = strdup(old_value);
			if (old_values[i] == NULL) {
				filson_last_cmd_success = 0;
				free(names[i]);
				while (--i >= 0) {
					free(names[i]);
					free(old_values[i]);
				}
				fprintf(stderr, "filson: allocation error\n");
				return 1;
			}
		} else {
			had_old[i] = 0;
			old_values[i] = NULL;
		}
		if (setenv(names[i], value, 1) != 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			free(names[i]);
			free(old_values[i]);
			while (--i >= 0) {
				unsetenv(names[i]);
				if (had_old[i]) {
					setenv(names[i], old_values[i], 1);
				}
				free(names[i]);
				free(old_values[i]);
			}
			return 1;
		}
	}
	result = filson_run_command_only(args + assign_count, background, segment);
	for (i = 0; i < assign_count; i++) {
		unsetenv(names[i]);
		if (had_old[i]) {
			setenv(names[i], old_values[i], 1);
		}
		free(names[i]);
		free(old_values[i]);
	}
	return result;
}

int
filson_execute(char **args, int background, char *segment)
{
	int i;
	int assign_count;
	char *name;
	const char *value;

	if (args[0] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	assign_count = 0;
	while (args[assign_count] != NULL && filson_is_assignment_token(args[assign_count])) {
		assign_count++;
	}
	if (assign_count > 0) {
		if (args[assign_count] == NULL) {
			for (i = 0; i < assign_count; i++) {
				if (!filson_parse_assignment_token(args[i], &name, &value)) {
					fprintf(stderr, "filson: invalid assignment\n");
					filson_last_cmd_success = 0;
					return 1;
				}
				if (setenv(name, value, 1) != 0) {
					perror("filson");
					free(name);
					filson_last_cmd_success = 0;
					return 1;
				}
				free(name);
			}
			filson_last_cmd_success = 1;
			return 1;
		}
		return filson_run_with_temp_assignments(args, assign_count, background, segment);
	}
	return filson_run_command_only(args, background, segment);
}

#define FILSON_RL_BUFSIZE 1024
#define FILSON_PROMPT "filson> "
#define FILSON_IDLE_TIMEOUT_SECS (45 * 60)

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
	fd_set rfds;
	struct timeval tv;
	int ready;

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
		if (interactive) {
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			tv.tv_sec = FILSON_IDLE_TIMEOUT_SECS;
			tv.tv_usec = 0;
			ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
			if (ready == 0) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				free(buffer);
				fprintf(stderr, "\nfilson: idle timeout (45 minutes) — session ended\n");
				return NULL;
			}
		}
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
	char **tokens, *token, *token_copy;

	bufsize = FILSON_TOK_BUFSIZE;
	position = 0;
	tokens = malloc(bufsize * sizeof(char *));
	if (!tokens) {
		fprintf(stderr, "filson: allocation error\n");
		exit(EXIT_FAILURE);
	}
	token = strtok(line, FILSON_TOK_DELIM);
	while (token != NULL) {
		token_copy = malloc(strlen(token) + 1);
		if (!token_copy) {
			fprintf(stderr, "filson: allocation error\n");
			exit(EXIT_FAILURE);
		}
		strcpy(token_copy, token);
		tokens[position] = token_copy;
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

