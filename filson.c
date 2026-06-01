#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
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
int filson_alias(char **args);
int filson_ssh(char **args);
int filson_local(char **args);
int filson_return_stmt(char **args);
int filson_declare_func(char **args);
int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int background, char *segment);
char **filson_split_line(char *line);

static int filson_is_assignment_token(const char *token);
static int filson_parse_assignment_token(const char *token, char **name_out, const char **value_out);
static char *filson_expand_string_variables(const char *str);
static char *filson_lookup_alias(const char *name);
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
	"bg",
	"wait",
	"alias",
	"ssh",
	"local",
	"return",
	"declare"
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
	&filson_ssh,
	&filson_local,
	&filson_return_stmt,
	&filson_declare_func
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

#define FILSON_MAX_ALIASES 64

struct filson_alias_entry {
	int used;
	char *name;
	char *value;
};

static struct filson_alias_entry filson_aliases[FILSON_MAX_ALIASES];

static char *
filson_lookup_alias(const char *name)
{
	int i;

	if (name == NULL) {
		return NULL;
	}
	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used && strcmp(filson_aliases[i].name, name) == 0) {
			return filson_aliases[i].value;
		}
	}
	return NULL;
}

static void
filson_set_alias(const char *name, const char *value)
{
	int i, empty_slot;

	if (name == NULL || value == NULL) {
		return;
	}
	empty_slot = -1;
	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used && strcmp(filson_aliases[i].name, name) == 0) {
			free(filson_aliases[i].value);
			filson_aliases[i].value = malloc(strlen(value) + 1);
			if (filson_aliases[i].value == NULL) {
				return;
			}
			strcpy(filson_aliases[i].value, value);
			return;
		}
		if (!filson_aliases[i].used && empty_slot == -1) {
			empty_slot = i;
		}
	}
	if (empty_slot == -1) {
		return;
	}
	filson_aliases[empty_slot].used = 1;
	filson_aliases[empty_slot].name = malloc(strlen(name) + 1);
	filson_aliases[empty_slot].value = malloc(strlen(value) + 1);
	if (filson_aliases[empty_slot].name == NULL || filson_aliases[empty_slot].value == NULL) {
		free(filson_aliases[empty_slot].name);
		free(filson_aliases[empty_slot].value);
		filson_aliases[empty_slot].used = 0;
		return;
	}
	strcpy(filson_aliases[empty_slot].name, name);
	strcpy(filson_aliases[empty_slot].value, value);
}

#define FILSON_MAX_FUNCTIONS 64
#define FILSON_MAX_POSPARAMS 32
#define FILSON_FUNC_CALL_DEPTH 8
#define FILSON_MAX_LOCAL_VARS 16

struct filson_func_entry {
	int used;
	char *name;
	char *body;
};

static struct filson_func_entry filson_functions[FILSON_MAX_FUNCTIONS];

struct filson_local_var {
	char *name;
	char *value;
	char *saved_value;
};

struct filson_param_frame {
	char *params[FILSON_MAX_POSPARAMS];
	int count;
	int return_value;
	struct filson_local_var local_vars[FILSON_MAX_LOCAL_VARS];
	int local_count;
};

static struct filson_param_frame filson_call_stack[FILSON_FUNC_CALL_DEPTH];
static int filson_call_depth = 0;
static int filson_function_return_requested = 0;

char *
filson_get_pospar(int idx)
{
	struct filson_param_frame *fr;

	if (filson_call_depth == 0) {
		return NULL;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	if (idx < 0 || idx >= fr->count) {
		return NULL;
	}
	return fr->params[idx];
}

static void
filson_define_function(const char *name, const char *body)
{
	int i, empty_slot;

	if (name == NULL || body == NULL) {
		return;
	}
	empty_slot = -1;
	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used && strcmp(filson_functions[i].name, name) == 0) {
			free(filson_functions[i].body);
			filson_functions[i].body = malloc(strlen(body) + 1);
			if (filson_functions[i].body != NULL) {
				strcpy(filson_functions[i].body, body);
			}
			return;
		}
		if (!filson_functions[i].used && empty_slot == -1) {
			empty_slot = i;
		}
	}
	if (empty_slot == -1) {
		return;
	}
	filson_functions[empty_slot].used = 1;
	filson_functions[empty_slot].name = malloc(strlen(name) + 1);
	filson_functions[empty_slot].body = malloc(strlen(body) + 1);
	if (filson_functions[empty_slot].name == NULL || filson_functions[empty_slot].body == NULL) {
		free(filson_functions[empty_slot].name);
		free(filson_functions[empty_slot].body);
		filson_functions[empty_slot].used = 0;
		return;
	}
	strcpy(filson_functions[empty_slot].name, name);
	strcpy(filson_functions[empty_slot].body, body);
}

static char *
filson_lookup_function(const char *name)
{
	int i;

	if (name == NULL) {
		return NULL;
	}
	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used && strcmp(filson_functions[i].name, name) == 0) {
			return filson_functions[i].body;
		}
	}
	return NULL;
}

static int
filson_function_exists(const char *name)
{
	return filson_lookup_function(name) != NULL;
}

static void
filson_declare_local(const char *name)
{
	struct filson_param_frame *fr;
	int i;

	if (filson_call_depth == 0) {
		return;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	for (i = 0; i < fr->local_count; i++) {
		if (strcmp(fr->local_vars[i].name, name) == 0) {
			return;
		}
	}
	if (fr->local_count >= FILSON_MAX_LOCAL_VARS) {
		return;
	}
	fr->local_vars[fr->local_count].name = malloc(strlen(name) + 1);
	if (fr->local_vars[fr->local_count].name != NULL) {
		strcpy(fr->local_vars[fr->local_count].name, name);
		fr->local_vars[fr->local_count].value = NULL;
		fr->local_vars[fr->local_count].saved_value = getenv(name) ? strdup(getenv(name)) : NULL;
		fr->local_count++;
	}
}

static int
filson_is_local_var(const char *name)
{
	struct filson_param_frame *fr;
	int i;

	if (filson_call_depth == 0) {
		return 0;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	for (i = 0; i < fr->local_count; i++) {
		if (strcmp(fr->local_vars[i].name, name) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
filson_restore_locals(void)
{
	struct filson_param_frame *fr;
	int i;

	if (filson_call_depth == 0) {
		return;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	for (i = 0; i < fr->local_count; i++) {
		if (fr->local_vars[i].saved_value != NULL) {
			setenv(fr->local_vars[i].name, fr->local_vars[i].saved_value, 1);
			free(fr->local_vars[i].saved_value);
		} else {
			unsetenv(fr->local_vars[i].name);
		}
		free(fr->local_vars[i].name);
	}
	fr->local_count = 0;
}

static int filson_call_function(const char *name, char **args);

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
filson_expand_string_variables(const char *str)
{
	int i, j, input_len;
	char *output, *var_name, *var_value;
	int var_len;
	const char *bracket_end;
	int expansion_found;

	if (str == NULL) {
		return strdup("");
	}
	expansion_found = 0;
	input_len = strlen(str);
	output = malloc(input_len * 2 + 1);
	if (output == NULL) {
		return (char *)str;
	}
	j = 0;
	for (i = 0; str[i] != '\0'; i++) {
		if (str[i] == '$' && str[i + 1] != '\0') {
			if (str[i + 1] == '{') {
				bracket_end = strchr(&str[i + 2], '}');
				if (bracket_end != NULL) {
					var_len = bracket_end - &str[i + 2];
					var_name = malloc(var_len + 1);
					if (var_name != NULL) {
						memcpy(var_name, &str[i + 2], var_len);
						var_name[var_len] = '\0';
						var_value = getenv(var_name);
						if (var_value != NULL) {
							int val_len = strlen(var_value);
							if (j + val_len > input_len * 2) {
								output = realloc(output, j + val_len + 256);
								if (output == NULL) {
									free(var_name);
									return (char *)str;
								}
							}
							strcpy(&output[j], var_value);
							j += val_len;
							expansion_found = 1;
						}
						free(var_name);
						i += var_len + 2;
						continue;
					}
				}
			} else if ((str[i + 1] >= 'a' && str[i + 1] <= 'z') ||
				   (str[i + 1] >= 'A' && str[i + 1] <= 'Z') ||
				   str[i + 1] == '_') {
				var_len = 0;
				while (str[i + 1 + var_len] != '\0' &&
				       ((str[i + 1 + var_len] >= 'a' && str[i + 1 + var_len] <= 'z') ||
					(str[i + 1 + var_len] >= 'A' && str[i + 1 + var_len] <= 'Z') ||
					(str[i + 1 + var_len] >= '0' && str[i + 1 + var_len] <= '9') ||
					str[i + 1 + var_len] == '_')) {
					var_len++;
				}
				var_name = malloc(var_len + 1);
				if (var_name != NULL) {
					memcpy(var_name, &str[i + 1], var_len);
					var_name[var_len] = '\0';
					var_value = getenv(var_name);
					if (var_value != NULL) {
						int val_len = strlen(var_value);
						if (j + val_len > input_len * 2) {
							output = realloc(output, j + val_len + 256);
							if (output == NULL) {
								free(var_name);
								return (char *)str;
							}
						}
						strcpy(&output[j], var_value);
						j += val_len;
						expansion_found = 1;
					}
					i += var_len;
					free(var_name);
					continue;
				}
			} else if (str[i + 1] >= '0' && str[i + 1] <= '9') {
				char *pval;
				int val_len;

				pval = filson_get_pospar(str[i + 1] - '0');
				if (pval != NULL) {
					val_len = strlen(pval);
					if (j + val_len > input_len * 2) {
						output = realloc(output, j + val_len + 256);
						if (output == NULL) {
							return (char *)str;
						}
					}
					strcpy(&output[j], pval);
					j += val_len;
					expansion_found = 1;
				}
				i++;
				continue;
			} else if (str[i + 1] == '#') {
				char num_buf[16];
				int pcount, val_len;

				pcount = (filson_call_depth > 0) ? filson_call_stack[filson_call_depth - 1].count - 1 : 0;
				if (pcount < 0) {
					pcount = 0;
				}
				snprintf(num_buf, sizeof(num_buf), "%d", pcount);
				val_len = strlen(num_buf);
				if (j + val_len > input_len * 2) {
					output = realloc(output, j + val_len + 256);
					if (output == NULL) {
						return (char *)str;
					}
				}
				strcpy(&output[j], num_buf);
				j += val_len;
				expansion_found = 1;
				i++;
				continue;
			} else if (str[i + 1] == '@') {
				struct filson_param_frame *fr;
				int k, val_len;

				if (filson_call_depth > 0) {
					fr = &filson_call_stack[filson_call_depth - 1];
					for (k = 1; k < fr->count; k++) {
						if (fr->params[k] == NULL) {
							continue;
						}
						val_len = strlen(fr->params[k]);
						if (j + val_len + 2 > input_len * 2) {
							output = realloc(output, j + val_len + 256);
							if (output == NULL) {
								return (char *)str;
							}
						}
						if (k > 1) {
							output[j++] = ' ';
						}
						strcpy(&output[j], fr->params[k]);
						j += val_len;
					}
					expansion_found = 1;
				}
				i++;
				continue;
			}
		}
		if (j >= input_len * 2) {
			output = realloc(output, j + 256);
			if (output == NULL) {
				return (char *)str;
			}
		}
		output[j++] = str[i];
	}
	output[j] = '\0';
	if (!expansion_found) {
		free(output);
		return (char *)str;
	}
	return output;
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
	int i;

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
		for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
			if (filson_functions[i].used && strcmp(filson_functions[i].name, args[2]) == 0) {
				free(filson_functions[i].name);
				free(filson_functions[i].body);
				filson_functions[i].used = 0;
				filson_last_cmd_success = 1;
				return 1;
			}
		}
		fprintf(stderr, "filson: unset: %s: not a function\n", args[2]);
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
filson_alias(char **args)
{
	int i;

	if (args[1] == NULL) {
		for (i = 0; i < FILSON_MAX_ALIASES; i++) {
			if (filson_aliases[i].used) {
				printf("alias %s='%s'\n", filson_aliases[i].name,
				    filson_aliases[i].value);
			}
		}
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

	if (filson_call_depth == 0) {
		fprintf(stderr, "filson: return: can only be used inside a function\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	ret_value = 0;
	if (args[1] != NULL) {
		ret_value = atoi(args[1]);
	}
	filson_call_stack[filson_call_depth - 1].return_value = ret_value;
	filson_function_return_requested = 1;
	filson_last_cmd_success = (ret_value == 0);
	return 0;
}

int
filson_unset_func(char **args)
{
	int i, found;

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
		found = 0;
		for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
			if (filson_functions[i].used && strcmp(filson_functions[i].name, args[2]) == 0) {
				free(filson_functions[i].name);
				free(filson_functions[i].body);
				filson_functions[i].used = 0;
				found = 1;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, "filson: unset: %s: not a function\n", args[2]);
			filson_last_cmd_success = 0;
			return 1;
		}
	} else {
		found = 0;
		for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
			if (filson_functions[i].used && strcmp(filson_functions[i].name, args[1]) == 0) {
				free(filson_functions[i].name);
				free(filson_functions[i].body);
				filson_functions[i].used = 0;
				found = 1;
				break;
			}
		}
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
	int i, show_func;

	show_func = (args[1] != NULL && strcmp(args[1], "-f") == 0);
	if (show_func) {
		if (args[2] != NULL) {
			char *body = filson_lookup_function(args[2]);
			if (body != NULL) {
				printf("%s() {\n", args[2]);
				printf("\t%s\n", body);
				printf("}\n");
			} else {
				fprintf(stderr, "filson: declare: %s: not a function\n", args[2]);
				filson_last_cmd_success = 0;
				return 1;
			}
		} else {
			for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
				if (filson_functions[i].used) {
					printf("%s() {\n", filson_functions[i].name);
					printf("\t%s\n", filson_functions[i].body);
					printf("}\n");
				}
			}
		}
	} else if (args[1] != NULL && strcmp(args[1], "-l") == 0) {
		for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
			if (filson_functions[i].used) {
				printf("%s\n", filson_functions[i].name);
			}
		}
	} else {
		for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
			if (filson_functions[i].used) {
				printf("declare -f %s\n", filson_functions[i].name);
			}
		}
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

static int
filson_call_function(const char *name, char **args)
{
	char *body;
	struct filson_param_frame *frame;
	int i, argc;

	body = filson_lookup_function(name);
	if (body == NULL) {
		fprintf(stderr, "filson: %s: function not found\n", name);
		filson_last_cmd_success = 0;
		return 1;
	}
	if (filson_call_depth >= FILSON_FUNC_CALL_DEPTH) {
		fprintf(stderr, "filson: function call stack overflow\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	frame = &filson_call_stack[filson_call_depth];
	for (i = 0; i < FILSON_MAX_POSPARAMS; i++) {
		frame->params[i] = NULL;
	}
	frame->return_value = 0;
	frame->local_count = 0;
	argc = 0;
	frame->params[argc++] = (char *)name;
	for (i = 1; args[i] != NULL && argc < FILSON_MAX_POSPARAMS; i++) {
		frame->params[argc++] = args[i];
	}
	frame->count = argc;
	filson_call_depth++;
	filson_function_return_requested = 0;
	filson_execute_and_chain(body);
	filson_restore_locals();
	filson_call_depth--;
	filson_last_cmd_success = 1;
	return 1;
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

static void
filson_refresh_line_cursor(const char *buffer, int cursor)
{
	int len, move_left;

	len = strlen(buffer);
	printf("\r%s%s\033[K", FILSON_PROMPT, buffer);
	move_left = len - cursor;
	if (move_left > 0) {
		printf("\033[%dD", move_left);
	}
	fflush(stdout);
}

static void
filson_set_kill_buffer(char **kill_buffer, const char *src, int len)
{
	char *next;

	if (kill_buffer == NULL) {
		return;
	}
	if (len < 0) {
		len = 0;
	}
	next = malloc(len + 1);
	if (next == NULL) {
		return;
	}
	if (len > 0) {
		memcpy(next, src, len);
	}
	next[len] = '\0';
	free(*kill_buffer);
	*kill_buffer = next;
}

char *
filson_read_line(void)
{
	int bufsize, position, cursor, c;
	int interactive, history_cursor, history_count;
	char *buffer;
	char *kill_buffer;
	const char *history_entry;
	struct termios oldt, newt;
	fd_set rfds;
	struct timeval tv;
	int ready;

	bufsize = FILSON_RL_BUFSIZE;
	position = 0;
	cursor = 0;
	kill_buffer = NULL;
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
				free(kill_buffer);
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
			if (next1 == '[' && (next2 == 'A' || next2 == 'B' || next2 == 'C' || next2 == 'D')) {
				if (next2 == 'A' && history_cursor > 0) {
					history_cursor--;
				} else if (next2 == 'B' && history_cursor < history_count) {
					history_cursor++;
				} else if (next2 == 'C' && cursor < position) {
					cursor++;
					filson_refresh_line_cursor(buffer, cursor);
					continue;
				} else if (next2 == 'D' && cursor > 0) {
					cursor--;
					filson_refresh_line_cursor(buffer, cursor);
					continue;
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
					cursor = position;
				} else {
					position = 0;
					cursor = 0;
					buffer[0] = '\0';
				}
				filson_refresh_line_cursor(buffer, cursor);
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
			free(kill_buffer);
			free(buffer);
			return NULL;
		} else if (c == '\n') {
			if (interactive) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				printf("\n");
			}
			buffer[position] = '\0';
			free(kill_buffer);
			return buffer;
		} else if (interactive && c == 1) {
			cursor = 0;
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && c == 5) {
			cursor = position;
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && c == 2) {
			if (cursor > 0) {
				cursor--;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 6) {
			if (cursor < position) {
				cursor++;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 11) {
			if (cursor < position) {
				filson_set_kill_buffer(&kill_buffer, buffer + cursor, position - cursor);
				position = cursor;
				buffer[position] = '\0';
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 21) {
			if (cursor > 0) {
				filson_set_kill_buffer(&kill_buffer, buffer, cursor);
				memmove(buffer, buffer + cursor, position - cursor + 1);
				position -= cursor;
				cursor = 0;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 23) {
			int start;

			start = cursor;
			while (start > 0 && isspace((unsigned char)buffer[start - 1])) {
				start--;
			}
			while (start > 0 && !isspace((unsigned char)buffer[start - 1])) {
				start--;
			}
			if (start < cursor) {
				filson_set_kill_buffer(&kill_buffer, buffer + start, cursor - start);
				memmove(buffer + start, buffer + cursor, position - cursor + 1);
				position -= (cursor - start);
				cursor = start;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 25) {
			int ylen;

			if (kill_buffer != NULL) {
				ylen = strlen(kill_buffer);
				while (position + ylen >= bufsize - 1) {
					bufsize += FILSON_RL_BUFSIZE;
					buffer = realloc(buffer, bufsize);
					if (!buffer) {
						fprintf(stderr, "filson: allocation error\n");
						exit(EXIT_FAILURE);
					}
				}
				memmove(buffer + cursor + ylen, buffer + cursor, position - cursor + 1);
				memcpy(buffer + cursor, kill_buffer, ylen);
				cursor += ylen;
				position += ylen;
				filson_refresh_line_cursor(buffer, cursor);
				history_cursor = history_count;
			}
		} else if (interactive && c == 12) {
			printf("\033[2J\033[H");
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && c == 16) {
			if (history_cursor > 0) {
				history_cursor--;
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
					cursor = position;
					filson_refresh_line_cursor(buffer, cursor);
				}
			}
		} else if (interactive && c == 14) {
			if (history_cursor < history_count) {
				history_cursor++;
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
					cursor = position;
				} else {
					position = 0;
					cursor = 0;
					buffer[0] = '\0';
				}
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c == 4) {
			if (cursor < position) {
				memmove(buffer + cursor, buffer + cursor + 1, position - cursor);
				position--;
				buffer[position] = '\0';
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && (c == 127 || c == '\b' || c == 8)) {
			if (cursor > 0) {
				memmove(buffer + cursor - 1, buffer + cursor, position - cursor + 1);
				cursor--;
				position--;
				history_cursor = history_count;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && c >= 32 && c <= 126) {
			if (position >= bufsize - 2) {
				bufsize += FILSON_RL_BUFSIZE;
				buffer = realloc(buffer, bufsize);
				if (!buffer) {
					fprintf(stderr, "filson: allocation error\n");
					exit(EXIT_FAILURE);
				}
			}
			memmove(buffer + cursor + 1, buffer + cursor, position - cursor + 1);
			buffer[cursor] = (char)c;
			position++;
			cursor++;
			history_cursor = history_count;
			filson_refresh_line_cursor(buffer, cursor);
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

static char filson_heredoc_tmppath[64] = "";

static int
filson_heredoc_find(const char *line, char *delim_out, int *start_pos, int *end_pos)
{
	int i, in_single, in_double, dstart, dend;

	in_single = 0;
	in_double = 0;
	for (i = 0; line[i] != '\0'; i++) {
		if (!in_double && line[i] == '\'') {
			in_single = !in_single;
			continue;
		}
		if (!in_single && line[i] == '"') {
			in_double = !in_double;
			continue;
		}
		if (!in_single && !in_double &&
		    line[i] == '<' && line[i + 1] == '<' && line[i + 2] != '<') {
			*start_pos = i;
			i += 2;
			while (line[i] == ' ' || line[i] == '\t') {
				i++;
			}
			dstart = i;
			while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' &&
			       line[i] != '\n' && line[i] != ';') {
				i++;
			}
			dend = i;
			if (dend <= dstart || dend - dstart >= 256) {
				return 0;
			}
			memcpy(delim_out, line + dstart, dend - dstart);
			delim_out[dend - dstart] = '\0';
			*end_pos = dend;
			return 1;
		}
	}
	return 0;
}

static char *
filson_prepare_heredoc(const char *line)
{
	char delim[256];
	int hd_start, hd_end;
	char *body_line, *new_line;
	int fd, n, interactive;

	if (!filson_heredoc_find(line, delim, &hd_start, &hd_end)) {
		return NULL;
	}
	strcpy(filson_heredoc_tmppath, "/tmp/filson_hdoc_XXXXXX");
	fd = mkstemp(filson_heredoc_tmppath);
	if (fd < 0) {
		return NULL;
	}
	interactive = isatty(STDIN_FILENO);
	while (1) {
		if (interactive) {
			write(STDOUT_FILENO, "heredoc> ", 9);
		}
		body_line = filson_read_line();
		if (body_line == NULL) {
			break;
		}
		if (strcmp(body_line, delim) == 0) {
			free(body_line);
			break;
		}
		{
			char *expanded_line = filson_expand_string_variables(body_line);
			n = strlen(expanded_line);
			write(fd, expanded_line, n);
			write(fd, "\n", 1);
			if (expanded_line != body_line) {
				free(expanded_line);
			}
		}
		free(body_line);
	}
	close(fd);
	new_line = malloc(hd_start + strlen(filson_heredoc_tmppath) + (strlen(line) - hd_end) + 4);
	if (new_line == NULL) {
		unlink(filson_heredoc_tmppath);
		filson_heredoc_tmppath[0] = '\0';
		return NULL;
	}
	snprintf(new_line, hd_start + strlen(filson_heredoc_tmppath) + (strlen(line) - hd_end) + 4,
	    "%.*s< %s%s", hd_start, line, filson_heredoc_tmppath, line + hd_end);
	return new_line;
}

static int
filson_funcdef_parse(const char *line, char *name_out, int name_max,
    char **body_out, int *needs_more)
{
	const char *p, *after_name, *q, *body_start, *body_end, *brace_close;
	int name_len, depth, body_len;

	p = line;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) {
		return 0;
	}
	name_len = 0;
	while ((p[name_len] >= 'a' && p[name_len] <= 'z') ||
	       (p[name_len] >= 'A' && p[name_len] <= 'Z') ||
	       (p[name_len] >= '0' && p[name_len] <= '9') ||
	       p[name_len] == '_') {
		name_len++;
	}
	if (name_len == 0 || name_len >= name_max) {
		return 0;
	}
	after_name = p + name_len;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != '(') {
		return 0;
	}
	after_name++;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != ')') {
		return 0;
	}
	after_name++;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != '{') {
		return 0;
	}
	memcpy(name_out, p, name_len);
	name_out[name_len] = '\0';
	q = after_name;
	depth = 0;
	brace_close = NULL;
	while (*q != '\0') {
		if (*q == '{') {
			depth++;
		} else if (*q == '}') {
			depth--;
			if (depth == 0) {
				brace_close = q;
				break;
			}
		}
		q++;
	}
	if (brace_close == NULL) {
		*needs_more = 1;
		*body_out = NULL;
		return 1;
	}
	body_start = after_name + 1;
	body_end = brace_close;
	while (body_start < body_end &&
	       (*body_start == ' ' || *body_start == '\t' || *body_start == '\n' || *body_start == ';')) {
		body_start++;
	}
	while (body_end > body_start &&
	       (*(body_end - 1) == ' ' || *(body_end - 1) == '\t' ||
	        *(body_end - 1) == ';' || *(body_end - 1) == '\n')) {
		body_end--;
	}
	body_len = body_end - body_start;
	*body_out = malloc(body_len + 1);
	if (*body_out == NULL) {
		return 0;
	}
	memcpy(*body_out, body_start, body_len);
	(*body_out)[body_len] = '\0';
	*needs_more = 0;
	return 1;
}

void
filson_loop(void)
{
	char *line, *resolved, *hd_line, *func_body;
	char *trimmed;
	char func_name[256];
	int func_needs_more;
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
		trimmed = filson_trim(line);
		if (strcmp(trimmed, "!") == 0) {
			const char *last_entry;

			if (filson_history_count_entries() == 0) {
				fprintf(stderr, "filson: no commands in history\n");
				free(line);
				continue;
			}
			last_entry = filson_history_get(filson_history_count_entries() - 1);
			if (last_entry != NULL) {
				printf("%s\n", last_entry);
			}
			free(line);
			continue;
		}
		resolved = filson_resolve_history(line);
		if (resolved == NULL) {
			free(line);
			continue;
		}
		filson_add_history(resolved);
		if (filson_funcdef_parse(resolved, func_name, sizeof(func_name),
		    &func_body, &func_needs_more)) {
			if (func_needs_more) {
				const char *pp;
				int depth, accum_len, bufsize;
				char *accum, *more, *tmp;

				bufsize = strlen(resolved) + 4096;
				accum = malloc(bufsize);
				if (accum != NULL) {
					accum_len = strlen(resolved);
					strcpy(accum, resolved);
					depth = 0;
					pp = resolved;
					while (*pp != '\0') {
						if (*pp == '{') {
							depth++;
						} else if (*pp == '}') {
							depth--;
						}
						pp++;
					}
					while (depth > 0) {
						if (isatty(STDIN_FILENO)) {
							write(STDOUT_FILENO, "> ", 2);
						}
						more = filson_read_line();
						if (more == NULL) {
							break;
						}
						if (accum_len + (int)strlen(more) + 4 > bufsize) {
							bufsize = accum_len + strlen(more) + 4096;
							tmp = realloc(accum, bufsize);
							if (tmp == NULL) {
								free(more);
								break;
							}
							accum = tmp;
						}
						accum[accum_len++] = ';';
						memcpy(accum + accum_len, more, strlen(more));
						accum_len += strlen(more);
						accum[accum_len] = '\0';
						pp = more;
						while (*pp != '\0') {
							if (*pp == '{') {
								depth++;
							} else if (*pp == '}') {
								depth--;
							}
							pp++;
						}
						free(more);
					}
					filson_funcdef_parse(accum, func_name, sizeof(func_name),
					    &func_body, &func_needs_more);
					free(accum);
				}
			}
			if (func_body != NULL) {
				filson_define_function(func_name, func_body);
				free(func_body);
			}
			free(resolved);
			free(line);
			continue;
		}
		hd_line = filson_prepare_heredoc(resolved);
		if (hd_line != NULL) {
			status = filson_execute_and_chain(hd_line);
			free(hd_line);
			if (filson_heredoc_tmppath[0] != '\0') {
				unlink(filson_heredoc_tmppath);
				filson_heredoc_tmppath[0] = '\0';
			}
		} else {
			status = filson_execute_and_chain(resolved);
		}
		free(resolved);
		free(line);
	} while (status);
	filson_clear_history();
}

