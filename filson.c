#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"
#include "globbing.h"
#include "shell_session.h"
#include "runtime_state.h"
#include "expansion.h"
#include "builtins.h"

int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int background, char *segment);

static int filson_run_command_only(char **args, int background, char *segment);
static int filson_run_with_temp_assignments(char **args, int assign_count, int background, char *segment);
static void filson_free_assignment_buffers(int count, char **names, char **old_values);
static void filson_restore_assignment_environment(int count, char **names, char **old_values, int *had_old);

#define FILSON_MAX_ARGS 1024
#define FILSON_MAX_INLINE_ASSIGNMENTS 128

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

	if (args == NULL) {
		return 0;
	}
	count = 0;
	while (count <= FILSON_MAX_ARGS && args[count] != NULL) {
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
	pid_t waited;
	int status;
	int job_id;
	char **expanded;

	assert(args != NULL);
	assert(args[0] != NULL);

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
				waited = waitpid(pid, &status, WUNTRACED);
				if (waited < 0) {
					perror("filson");
					filson_last_cmd_success = 0;
					return 1;
				}
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
	int result;
	char *alias_value;
	char *alias_expanded;

	assert(args != NULL);

	if (args[0] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	if (filson_arg_count(args) > FILSON_MAX_ARGS) {
		fprintf(stderr, "filson: too many arguments\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	alias_expanded = NULL;
	alias_value = filson_lookup_alias(args[0]);
	if (alias_value != NULL) {
		alias_expanded = malloc(strlen(alias_value) + 1);
		if (alias_expanded == NULL) {
			perror("filson");
			filson_last_cmd_success = 0;
			return 1;
		}
		strcpy(alias_expanded, alias_value);
		args[0] = alias_expanded;
	}
	if (filson_lookup_function(args[0]) != NULL) {
		result = filson_call_function(args[0], args);
		free(alias_expanded);
		return result;
	}
	for (i = 0; i < filson_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			if (background) {
				fprintf(stderr, "filson: cannot run built-in in background\n");
				filson_last_cmd_success = 0;
				free(alias_expanded);
				return 1;
			}
			result = (*builtin_func[i])(args);
			free(alias_expanded);
			return result;
		}
	}
	if (!filson_path_is_safe()) {
		filson_last_cmd_success = 0;
		free(alias_expanded);
		return 1;
	}
	result = filson_launch(args, background, segment);
	free(alias_expanded);
	return result;
}

static int
filson_run_with_temp_assignments(char **args, int assign_count, int background, char *segment)
{
	char *names[FILSON_MAX_INLINE_ASSIGNMENTS];
	char *old_values[FILSON_MAX_INLINE_ASSIGNMENTS];
	int had_old[FILSON_MAX_INLINE_ASSIGNMENTS];
	const char *value;
	const char *old_value;
	int i;
	int result;

	assert(args != NULL);
	if (assign_count > FILSON_MAX_INLINE_ASSIGNMENTS) {
		fprintf(stderr, "filson: too many inline assignments\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (i = 0; i < assign_count; i++) {
		names[i] = NULL;
		old_values[i] = NULL;
		had_old[i] = 0;
	}
	for (i = 0; i < assign_count; i++) {
		if (!filson_parse_assignment_token(args[i], &names[i], &value)) {
			filson_last_cmd_success = 0;
			filson_free_assignment_buffers(i, names, old_values);
			fprintf(stderr, "filson: invalid inline assignment\n");
			return 1;
		}
		old_value = getenv(names[i]);
		if (old_value != NULL) {
			had_old[i] = 1;
			old_values[i] = strdup(old_value);
			if (old_values[i] == NULL) {
				filson_last_cmd_success = 0;
				filson_free_assignment_buffers(i + 1, names, old_values);
				fprintf(stderr, "filson: allocation error\n");
				return 1;
			}
		}
		if (setenv(names[i], value, 1) != 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			filson_restore_assignment_environment(i, names, old_values, had_old);
			filson_free_assignment_buffers(i + 1, names, old_values);
			return 1;
		}
	}
	result = filson_run_command_only(args + assign_count, background, segment);
	filson_restore_assignment_environment(assign_count, names, old_values, had_old);
	filson_free_assignment_buffers(assign_count, names, old_values);
	return result;
}

static void
filson_free_assignment_buffers(int count, char **names, char **old_values)
{
	int i;

	for (i = 0; i < count; i++) {
		free(names[i]);
		free(old_values[i]);
	}
}

static void
filson_restore_assignment_environment(int count, char **names, char **old_values, int *had_old)
{
	int i;

	for (i = 0; i < count; i++) {
		unsetenv(names[i]);
		if (had_old[i]) {
			setenv(names[i], old_values[i], 1);
		}
	}
}

int
filson_execute(char **args, int background, char *segment)
{
	int i;
	int assign_count;
	char *name;
	const char *value;

	if (args == NULL || args[0] == NULL) {
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

