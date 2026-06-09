#include <sys/wait.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
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
	char prev_pwd[PATH_MAX];
	char new_pwd[PATH_MAX];
	const char *cur_pwd;
	char *target;

	cur_pwd = getenv("PWD");
	if (cur_pwd != NULL) {
		strncpy(prev_pwd, cur_pwd, sizeof(prev_pwd) - 1);
		prev_pwd[sizeof(prev_pwd) - 1] = '\0';
	} else {
		if (getcwd(prev_pwd, sizeof(prev_pwd)) == NULL) {
			prev_pwd[0] = '\0';
		}
	}
	if (args[1] == NULL) {
		target = getenv("HOME");
		if (target == NULL) {
			warnx("cd: HOME not set");
			filson_last_cmd_success = 0;
			return 1;
		}
	} else if (strcmp(args[1], "-") == 0) {
		target = getenv("OLDPWD");
		if (target == NULL) {
			warnx("cd: OLDPWD not set");
			filson_last_cmd_success = 0;
			return 1;
		}
	} else {
		target = args[1];
	}
	if (chdir(target) != 0) {
		warn("cd: %s", target);
		filson_last_cmd_success = 0;
		return 1;
	}
	setenv("OLDPWD", prev_pwd, 1);
	if (target[0] == '/') {
		strncpy(new_pwd, target, sizeof(new_pwd) - 1);
		new_pwd[sizeof(new_pwd) - 1] = '\0';
	} else if (prev_pwd[0] != '\0') {
		int n = snprintf(new_pwd, sizeof(new_pwd), "%s/%s", prev_pwd, target);
		if (n < 0 || n >= (int)sizeof(new_pwd)) {
			new_pwd[sizeof(new_pwd) - 1] = '\0';
		}
	} else {
		if (getcwd(new_pwd, sizeof(new_pwd)) == NULL) {
			new_pwd[0] = '\0';
		}
	}
	setenv("PWD", new_pwd, 1);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_help(char **args)
{
	char buf[4096];
	int buf_len;

	(void)args;
	filson_last_cmd_success = 1;
	buf_len = 0;
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Bryan Copley's Filson\n");
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Type program names and arguments, and hit enter.\n");
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "The following are built in:\n");
	for (int i = 0; i < filson_num_builtins(); i++) {
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "  %s\n", builtin_str[i]);
	}
	buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "Use the man command for information on other programs.\n");
	if (buf_len > 0) {
		(void)write(1, buf, buf_len);
	}
	return 1;
}

int
filson_exit(char **args)
{
	extern int filson_exit_code;
	extern int filson_exit_called;
	int code;

	code = 0;
	if (args[1] != NULL) {
		code = atoi(args[1]);
	}
	filson_exit_code = code;
	filson_exit_called = 1;
	filson_last_cmd_success = (code == 0) ? 1 : 0;
	return 0;
}

int
filson_set(char **args)
{
	extern int filson_noglob;

	if (args[1] != NULL && args[1][0] == '-' && args[1][1] != '\0' && args[1][1] != '-') {
		for (int i = 1; args[i] != NULL && args[i][0] == '-' && args[i][1] != '\0'; i++) {
			const char *flags = args[i] + 1;
			while (*flags) {
				if (*flags == 'f') filson_noglob = 1;
				flags++;
			}
		}
		filson_last_cmd_success = 1;
		return 1;
	}
	if (args[1] != NULL && args[1][0] == '+' && args[1][1] != '\0') {
		for (int i = 1; args[i] != NULL && args[i][0] == '+'; i++) {
			const char *flags = args[i] + 1;
			while (*flags) {
				if (*flags == 'f') filson_noglob = 0;
				flags++;
			}
		}
		filson_last_cmd_success = 1;
		return 1;
	}
	if (args[1] != NULL && strcmp(args[1], "--") == 0) {
		int count = 0;
		for (int i = 2; args[i] != NULL; i++) {
			count++;
		}
		filson_set_posparams(&args[2], count);
		filson_last_cmd_success = 1;
		return 1;
	}
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

static int
filson_echo_escape(const char **str_p, char *buf, int buf_len, int buf_max)
{
	const char *str = *str_p;
	str++;
	switch (*str) {
	case 'a': buf[buf_len++] = '\a'; break;
	case 'b': buf[buf_len++] = '\b'; break;
	case 'c': *str_p = str; return -1;
	case 'e': buf[buf_len++] = '\033'; break;
	case 'f': buf[buf_len++] = '\f'; break;
	case 'n': buf[buf_len++] = '\n'; break;
	case 'r': buf[buf_len++] = '\r'; break;
	case 't': buf[buf_len++] = '\t'; break;
	case 'v': buf[buf_len++] = '\v'; break;
	case '\\': buf[buf_len++] = '\\'; break;
	case '0': {
		int octal_val = 0, octal_len = 0;
		str++;
		while (octal_len < 3 && *str >= '0' && *str <= '7') {
			octal_val = octal_val * 8 + (*str - '0');
			str++; octal_len++;
		}
		if (buf_len < buf_max) buf[buf_len++] = (char)octal_val;
		str--;
		break;
	}
	default:
		if (buf_len + 1 < buf_max) { buf[buf_len++] = '\\'; buf[buf_len++] = *str; }
		break;
	}
	*str_p = str + 1;
	return buf_len;
}

int
filson_echo(char **args)
{
	int i, eflag, nflag, first, buf_len;
	char buf[8192];
	const char *str;

	eflag = nflag = 0;
	i = 1;
	while (args[i] != NULL && args[i][0] == '-') {
		if (strcmp(args[i], "-e") == 0) { eflag = 1; i++; }
		else if (strcmp(args[i], "-n") == 0) { nflag = 1; i++; }
		else break;
	}
	first = 1;
	buf_len = 0;
	while (args[i] != NULL) {
		if (!first && buf_len < (int)sizeof(buf) - 1) buf[buf_len++] = ' ';
		first = 0;
		str = args[i];
		if (eflag) {
			while (*str && buf_len < (int)sizeof(buf) - 2) {
				if (*str == '\\' && *(str + 1) != '\0') {
					int res = filson_echo_escape(&str, buf, buf_len, (int)sizeof(buf) - 1);
					if (res < 0) {
						(void)write(1, buf, buf_len);
						filson_last_cmd_success = 1;
						return 1;
					}
					buf_len = res;
				} else {
					buf[buf_len++] = *str++;
				}
			}
		} else {
			while (*str && buf_len < (int)sizeof(buf) - 1) buf[buf_len++] = *str++;
		}
		i++;
	}
	if (!nflag && buf_len < (int)sizeof(buf) - 1) buf[buf_len++] = '\n';
	(void)write(1, buf, buf_len);
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_pwd(char **args)
{
	char buf[4096];
	const char *pwd;

	if (args[1] != NULL && strcmp(args[1], "-P") == 0) {
		if (getcwd(buf, sizeof(buf)) == NULL) {
			warn("getcwd");
			filson_last_cmd_success = 0;
			return 1;
		}
		printf("%s\n", buf);
		filson_last_cmd_success = 1;
		return 1;
	}
	pwd = getenv("PWD");
	if (pwd != NULL && pwd[0] != '\0') {
		printf("%s\n", pwd);
		filson_last_cmd_success = 1;
		return 1;
	}
	if (getcwd(buf, sizeof(buf)) == NULL) {
		warn("getcwd");
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
	(void)fflush(stdout);
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
	char *path, *token, *path_copy, *full_path;
	size_t needed;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected argument to \"type\"\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (int i = 0; i < filson_num_builtins(); i++) {
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
	char *eq;
	char *name;
	char *value;

	if (args[1] == NULL) {
		filson_print_aliases();
		filson_last_cmd_success = 1;
		return 1;
	}
	for (int i = 1; args[i] != NULL; i++) {
		eq = strchr(args[i], '=');
		if (eq != NULL) {
			name = malloc(eq - args[i] + 1);
			if (name == NULL) {
				filson_last_cmd_success = 0;
				return 1;
			}
			memcpy(name, args[i], eq - args[i]);
			name[eq - args[i]] = '\0';
			value = eq + 1;
			if (!filson_is_valid_varname(name)) {
				warnx("alias: invalid alias name: %s", name);
				free(name);
				filson_last_cmd_success = 0;
				return 1;
			}
			filson_set_alias(name, value);
			free(name);
		} else {
			if (filson_lookup_alias(args[i]) != NULL) {
				filson_print_one_alias(args[i]);
			} else {
				fprintf(stderr, "alias: %s: not found\n", args[i]);
				filson_last_cmd_success = 0;
				return 1;
			}
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_unalias(char **args)
{
	int found;

	if (args[1] == NULL) {
		warnx("unalias: usage: unalias [-a] name ...");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (strcmp(args[1], "-a") == 0) {
		filson_remove_all_aliases();
		filson_last_cmd_success = 1;
		return 1;
	}
	for (int i = 1; args[i] != NULL; i++) {
		found = filson_remove_alias(args[i]);
		if (!found) {
			warnx("unalias: %s: not found", args[i]);
			filson_last_cmd_success = 0;
			return 1;
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_trap(char **args)
{
	(void)args;
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_ssh(char **args)
{
	int argc;
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
	for (int i = 1; i < argc; i++) {
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
	if (args[1] == NULL) {
		fprintf(stderr, "filson: local: usage: local var_name [var_name ...]\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (int i = 1; args[i] != NULL; i++) {
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

static int
filson_read_parse_args(char **args, char **varname_p, char **prompt_p,
    int *use_prompt_p)
{
	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected variable name for read\n");
		filson_last_cmd_success = 0; return 0;
	}
	if (args[1][0] == '-' && args[1][1] == 'p') {
		if (args[2] == NULL) {
			fprintf(stderr, "filson: -p requires prompt argument\n");
			filson_last_cmd_success = 0; return 0;
		}
		*prompt_p = args[2]; *use_prompt_p = 1;
		if (args[3] == NULL) {
			fprintf(stderr, "filson: expected variable name for read\n");
			filson_last_cmd_success = 0; return 0;
		}
		*varname_p = args[3];
	} else {
		*varname_p = args[1];
	}
	if (!filson_is_valid_varname(*varname_p)) {
		fprintf(stderr, "filson: invalid variable name: %s\n", *varname_p);
		filson_last_cmd_success = 0; return 0;
	}
	return 1;
}

int
filson_read(char **args)
{
	char *varname = NULL, *prompt = NULL;
	char line[4096];
	char *result;
	int use_prompt = 0, len;

	if (!filson_read_parse_args(args, &varname, &prompt, &use_prompt)) return 1;
	if (use_prompt) { (void)fputs(prompt, stdout); fflush(stdout); }
	result = fgets(line, sizeof(line), stdin);
	if (result == NULL) { filson_last_cmd_success = 0; return 1; }
	len = strlen(line);
	if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
	if (setenv(varname, line, 1) != 0) {
		perror("filson"); filson_last_cmd_success = 0; return 1;
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

static char *
filson_source_accum_line(FILE *fp, const char *first_line)
{
	char line[4096];
	char *accum, *tmp;
	int accum_len, accum_cap;
	size_t len;

	accum_cap = strlen(first_line) + 4096;
	accum = malloc(accum_cap);
	if (accum == NULL) return NULL;
	accum_len = strlen(first_line);
	memcpy(accum, first_line, accum_len + 1);
	while (filson_needs_continuation(accum)) {
		if (fgets(line, sizeof(line), fp) == NULL) break;
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
		if (accum_len + (int)strlen(line) + 4 > accum_cap) {
			accum_cap = accum_len + strlen(line) + 4096;
			tmp = realloc(accum, accum_cap);
			if (tmp == NULL) break;
			accum = tmp;
		}
		accum[accum_len++] = '\n';
		memcpy(accum + accum_len, line, strlen(line));
		accum_len += strlen(line);
		accum[accum_len] = '\0';
	}
	return accum;
}

int
filson_source(char **args)
{
	char *filename;
	FILE *fp;
	char line[4096];
	int status, len, rv;
	char *accum;

	if (args[1] == NULL) {
		fprintf(stderr, "filson: expected filename for source\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	filename = args[1];
	fp = fopen(filename, "r");
	if (fp == NULL) { perror("filson"); filson_last_cmd_success = 0; return 1; }
	filson_push_source_frame();
	status = 1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
		if (line[0] == '\0' || line[0] == '#') continue;
		accum = filson_source_accum_line(fp, line);
		if (accum == NULL) break;
		status = filson_execute_and_chain(accum);
		free(accum);
		rv = filson_consume_return_value();
		if (rv >= 0) {
			(void)fclose(fp);
			filson_pop_source_frame();
			{
				extern int filson_last_exit_status;
				filson_last_exit_status = rv;
				filson_last_cmd_success = (rv == 0) ? 1 : 0;
			}
			return 1;
		}
		if (status == 0) break;
	}
	filson_pop_source_frame();
	(void)fclose(fp);
	return 1;
}

int
filson_dot(char **args)
{
	return filson_source(args);
}

int
filson_eval(char **args)
{
	char buf[4096];
	char *arg;
	int pos;
	int len;

	if (args[1] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	pos = 0;
	for (int i = 1; args[i] != NULL && pos < (int)sizeof(buf) - 2; i++) {
		arg = args[i];
		len = strlen(arg);
		if (len >= 2 && arg[0] == '"' && arg[len - 1] == '"') {
			arg = arg + 1;
			len -= 2;
		} else if (len >= 2 && arg[0] == '\'' && arg[len - 1] == '\'') {
			arg = arg + 1;
			len -= 2;
		}
		if (pos + len >= (int)sizeof(buf) - 2)
			len = (int)sizeof(buf) - 2 - pos;
		memcpy(buf + pos, arg, len);
		pos += len;
		if (args[i + 1] != NULL && pos < (int)sizeof(buf) - 2)
			buf[pos++] = ' ';
	}
	buf[pos] = '\0';
	return filson_execute_and_chain(buf);
}