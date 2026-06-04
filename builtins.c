#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "expansion.h"
#include "runtime_state.h"
#include "shell_session.h"
#include "pipelines.h"
#include "builtins.h"

extern int filson_last_cmd_success;
extern int filson_break_flag;
extern int filson_continue_flag;
extern const char *builtin_str[];

int filson_num_builtins(void);
int filson_is_valid_varname(const char *name);

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
filson_read(char **args)
{
	char *varname;
	char *prompt;
	char line[4096];
	char *result;
	int use_prompt;
	int len;

	use_prompt = 0;
	prompt = NULL;
	varname = NULL;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected variable name for read\n");
		filson_last_cmd_success = 0;
		return 1;
	}

	if (args[1][0] == '-' && args[1][1] == 'p') {
		if (args[2] == NULL) {
			fprintf(stderr, "filson: -p requires prompt argument\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		prompt = args[2];
		use_prompt = 1;
		if (args[3] == NULL) {
			fprintf(stderr, "filson: expected variable name for read\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		varname = args[3];
	} else {
		varname = args[1];
	}

	if (!filson_is_valid_varname(varname)) {
		fprintf(stderr, "filson: invalid variable name: %s\n", varname);
		filson_last_cmd_success = 0;
		return 1;
	}

	if (use_prompt) {
		fputs(prompt, stdout);
		fflush(stdout);
	}

	result = fgets(line, sizeof(line), stdin);
	if (result == NULL) {
		filson_last_cmd_success = 0;
		return 1;
	}

	len = strlen(line);
	if (len > 0 && line[len - 1] == '\n') {
		line[len - 1] = '\0';
	}

	if (setenv(varname, line, 1) != 0) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}

	filson_last_cmd_success = 1;
	return 1;
}

int
filson_shift(char **args)
{
	int n;
	char *endptr;

	n = 1;
	if (args[1] != NULL) {
		n = strtol(args[1], &endptr, 10);
		if (*endptr != '\0' || n < 0) {
			fprintf(stderr, "filson: shift: invalid argument\n");
			filson_last_cmd_success = 0;
			return 1;
		}
	}

	if (filson_shift_posparams(n) < 0) {
		fprintf(stderr, "filson: shift: too many arguments\n");
		filson_last_cmd_success = 0;
		return 1;
	}

	filson_last_cmd_success = 1;
	return 1;
}

int
filson_source(char **args)
{
	char *filename;
	FILE *fp;
	char line[4096];
	int status;
	int len;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected filename for source\n");
		filson_last_cmd_success = 0;
		return 1;
	}

	filename = args[1];
	fp = fopen(filename, "r");
	if (fp == NULL) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		}
		if (line[0] == '\0' || line[0] == '#') {
			continue;
		}
		status = filson_execute_and_chain(line);
		if (status == 0) {
			break;
		}
	}

	fclose(fp);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_dot(char **args)
{
	return filson_source(args);
}