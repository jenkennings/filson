#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"
#include "globbing.h"
#include "shell_session.h"
#include "runtime_state.h"
#include "expansion.h"
#include "test.h"
#include "builtins.h"

int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int background, char *segment);

static int filson_run_command_only(char **args, int background, char *segment);
static int filson_run_with_temp_assignments(char **args, int assign_count, int background, char *segment);

int filson_last_cmd_success = 1;
int filson_break_flag = 0;
int filson_continue_flag = 0;

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
	"bg",
	"wait",
	"alias",
	"test",
	"local",
	"return",
	"declare",
	"break",
	"continue",
	"read",
	"shift",
	"source",
	"."
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
	&filson_bg,
	&filson_wait,
	&filson_alias,
	&filson_test,
	&filson_local,
	&filson_return_stmt,
	&filson_declare_func,
	&filson_break,
	&filson_continue,
	&filson_read,
	&filson_shift,
	&filson_source,
	&filson_dot
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
filson_run_command_only(char **args, int background, char *segment)
{
	int i;
	char *alias_value, *new_command;

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
	alias_value = filson_lookup_alias(args[0]);
	if (alias_value != NULL) {
		new_command = malloc(strlen(alias_value) + 1);
		if (new_command == NULL) {
			perror("filson");
			filson_last_cmd_success = 0;
			return 1;
		}
		strcpy(new_command, alias_value);
		args[0] = new_command;
	}
	if (filson_lookup_function(args[0]) != NULL) {
		return filson_call_function(args[0], args);
	}
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

