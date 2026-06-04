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
int filson_alias(char **args);
int filson_test(char **args);
int filson_local(char **args);
int filson_return_stmt(char **args);
int filson_declare_func(char **args);
int filson_break(char **args);
int filson_continue(char **args);
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
	"continue"
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
	&filson_continue
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
	char buf[4096];
	int buf_len;

	(void)args;
	filson_last_cmd_success = 1;
	buf_len = 0;
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Bryan Copley's Filson\n");
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Type program names and arguments, and hit enter.\n");
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "The following are built in:\n");
	for (i = 0; i < filson_num_builtins(); i++) {
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "  %s\n", builtin_str[i]);
	}
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Use the man command for information on other programs.\n");
	if (buf_len > 0) {
		write(1, buf, buf_len);
	}
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
	char *expanded;
	char buf[8192];
	int buf_len;

	i = 1;
	first = 1;
	buf_len = 0;
	while (args[i] != NULL) {
		if (!first && buf_len < (int)sizeof(buf) - 1) {
			buf[buf_len++] = ' ';
		}
		first = 0;
		expanded = filson_expand_string_variables(args[i]);
		const char *str = expanded;
		while (*str && buf_len < (int)sizeof(buf) - 1) {
			buf[buf_len++] = *str++;
		}
		if (expanded != args[i]) {
			free(expanded);
		}
		i++;
	}
	if (buf_len < (int)sizeof(buf) - 1) {
		buf[buf_len++] = '\n';
	}
	write(1, buf, buf_len);
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
	if (strcmp(args[1], "-f") == 0) {
		if (args[2] == NULL) {
			fprintf(stderr, "filson: unset: -f requires a function name\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		if (!filson_unset_function(args[2])) {
			fprintf(stderr, "filson: unset: %s: not a function\n", args[2]);
			filson_last_cmd_success = 0;
			return 1;
		}
		filson_last_cmd_success = 1;
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
filson_alias(char **args)
{
	if (args[1] == NULL) {
		filson_print_aliases();
		filson_last_cmd_success = 1;
		return 1;
	}
	if (args[2] == NULL) {
		printf("filson: alias: expected <name> and <command>\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (!filson_is_valid_varname(args[1])) {
		printf("filson: alias: invalid alias name: %s\n", args[1]);
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_set_alias(args[1], args[2]);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_ssh(char **args)
{
	int argc, i;
	char **ssh_args;
	pid_t pid;
	int status;

	argc = 0;
	while (args[argc] != NULL) {
		argc++;
	}
	if (argc < 2) {
		fprintf(stderr, "filson: ssh: usage: ssh [options] [user@]hostname [command]\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	ssh_args = malloc((argc + 1) * sizeof(char *));
	if (ssh_args == NULL) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	ssh_args[0] = "ssh";
	for (i = 1; i < argc; i++) {
		ssh_args[i] = args[i];
	}
	ssh_args[argc] = NULL;
	pid = fork();
	if (pid == 0) {
		execvp("ssh", ssh_args);
		perror("filson");
		exit(EXIT_FAILURE);
	} else if (pid < 0) {
		perror("filson");
		free(ssh_args);
		filson_last_cmd_success = 0;
		return 1;
	}
	waitpid(pid, &status, 0);
	free(ssh_args);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		filson_last_cmd_success = 1;
	} else {
		filson_last_cmd_success = 0;
	}
	return 1;
}

int
filson_local(char **args)
{
	int i;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: local: usage: local var_name [var_name ...]\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (i = 1; args[i] != NULL; i++) {
		filson_declare_local(args[i]);
	}
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_return_stmt(char **args)
{
	int ret_value;

	if (!filson_has_active_function()) {
		fprintf(stderr, "filson: return: can only be used inside a function\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	ret_value = 0;
	if (args[1] != NULL) {
		ret_value = atoi(args[1]);
	}
	filson_return_from_function(ret_value);
	filson_last_cmd_success = (ret_value == 0);
	return 0;
}

int
filson_unset_func(char **args)
{
	int found;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: unset: usage: unset -f function_name\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (strcmp(args[1], "-f") == 0) {
		if (args[2] == NULL) {
			fprintf(stderr, "filson: unset: usage: unset -f function_name\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		found = filson_unset_function(args[2]);
		if (!found) {
			fprintf(stderr, "filson: unset: %s: not a function\n", args[2]);
			filson_last_cmd_success = 0;
			return 1;
		}
	} else {
		found = filson_unset_function(args[1]);
		if (found) {
			filson_last_cmd_success = 1;
			return 1;
		}
		if (strcmp(args[1], "-f") != 0) {
			unsetenv(args[1]);
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_declare_func(char **args)
{
	int show_func;

	show_func = (args[1] != NULL && strcmp(args[1], "-f") == 0);
	if (show_func) {
		if (args[2] != NULL) {
			if (filson_lookup_function(args[2]) != NULL) {
				filson_print_function_definition(args[2]);
			} else {
				fprintf(stderr, "filson: declare: %s: not a function\n", args[2]);
				filson_last_cmd_success = 0;
				return 1;
			}
		} else {
			filson_print_all_function_definitions();
		}
	} else if (args[1] != NULL && strcmp(args[1], "-l") == 0) {
		filson_print_all_function_names();
	} else {
		filson_print_all_function_declarations();
	}
	filson_last_cmd_success = 1;
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
filson_break(char **args)
{
	(void)args;
	filson_break_flag = 1;
	filson_last_cmd_success = 0;
	return 1;
}

int
filson_continue(char **args)
{
	(void)args;
	filson_continue_flag = 1;
	filson_last_cmd_success = 0;
	return 1;
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

