#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pipelines.h"
#include "runtime_state.h"

extern int filson_last_cmd_success;

#define FILSON_MAX_ALIASES 64
#define FILSON_MAX_FUNCTIONS 64
#define FILSON_MAX_POSPARAMS 32
#define FILSON_FUNC_CALL_DEPTH 8
#define FILSON_MAX_LOCAL_VARS 16

struct filson_alias_entry {
	int used;
	char *name;
	char *value;
};

struct filson_func_entry {
	int used;
	char *name;
	char *body;
};

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

static struct filson_alias_entry filson_aliases[FILSON_MAX_ALIASES];
static struct filson_func_entry filson_functions[FILSON_MAX_FUNCTIONS];
static struct filson_param_frame filson_call_stack[FILSON_FUNC_CALL_DEPTH];
static int filson_call_depth = 0;
static int filson_dot_depth = 0;
int filson_function_return_requested = 0;

static char *filson_global_posparams[FILSON_MAX_POSPARAMS];
static int filson_global_pospar_count = 0;

char *
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

void
filson_set_alias(const char *name, const char *value)
{
	int i;
	int empty_slot;

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

void
filson_print_aliases(void)
{
	int i;
	struct filson_alias_entry sorted[FILSON_MAX_ALIASES];
	int count;
	int j;
	int k;

	count = 0;
	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used) {
			sorted[count++] = filson_aliases[i];
		}
	}
	for (i = 0; i < count - 1; i++) {
		for (j = i + 1; j < count; j++) {
			if (strcmp(sorted[i].name, sorted[j].name) > 0) {
				struct filson_alias_entry tmp = sorted[i];
				sorted[i] = sorted[j];
				sorted[j] = tmp;
			}
		}
	}
	(void)k;
	for (i = 0; i < count; i++) {
		int needs_quote = 0;
		const char *v = sorted[i].value;

		if (v[0] == '\0') {
			needs_quote = 1;
		} else {
			for (j = 0; v[j] != '\0'; j++) {
				if (v[j] == ' ' || v[j] == '\t' || v[j] == '\'' ||
				    v[j] == '"' || v[j] == '\\' || v[j] == '$' ||
				    v[j] == '!' || v[j] == '&' || v[j] == '|' ||
				    v[j] == ';' || v[j] == '<' || v[j] == '>' ||
				    v[j] == '(' || v[j] == ')' || v[j] == '`' ||
				    v[j] == '*' || v[j] == '?') {
					needs_quote = 1;
					break;
				}
			}
		}
		if (needs_quote) {
			printf("%s='%s'\n", sorted[i].name, v);
		} else {
			printf("%s=%s\n", sorted[i].name, v);
		}
	}
}
void
filson_print_one_alias(const char *name)
{
	int i;
	int j;
	int needs_quote;
	const char *v;

	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used && strcmp(filson_aliases[i].name, name) == 0) {
			v = filson_aliases[i].value;
			needs_quote = 0;
			if (v[0] == '\0') {
				needs_quote = 1;
			} else {
				for (j = 0; v[j] != '\0'; j++) {
					if (v[j] == ' ' || v[j] == '\t' || v[j] == '\'' ||
					    v[j] == '"' || v[j] == '\\' || v[j] == '$' ||
					    v[j] == '!' || v[j] == '&' || v[j] == '|' ||
					    v[j] == ';' || v[j] == '<' || v[j] == '>' ||
					    v[j] == '(' || v[j] == ')' || v[j] == '`' ||
					    v[j] == '*' || v[j] == '?') {
						needs_quote = 1;
						break;
					}
				}
			}
			if (needs_quote) {
				printf("%s='%s'\n", name, v);
			} else {
				printf("%s=%s\n", name, v);
			}
			return;
		}
	}
}

int
filson_remove_alias(const char *name)
{
	int i;

	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used && strcmp(filson_aliases[i].name, name) == 0) {
			filson_aliases[i].used = 0;
			free(filson_aliases[i].name);
			free(filson_aliases[i].value);
			filson_aliases[i].name = NULL;
			filson_aliases[i].value = NULL;
			return 1;
		}
	}
	return 0;
}

void
filson_remove_all_aliases(void)
{
	int i;

	for (i = 0; i < FILSON_MAX_ALIASES; i++) {
		if (filson_aliases[i].used) {
			filson_aliases[i].used = 0;
			free(filson_aliases[i].name);
			free(filson_aliases[i].value);
			filson_aliases[i].name = NULL;
			filson_aliases[i].value = NULL;
		}
	}
}
void
filson_define_function(const char *name, const char *body)
{
	int i;
	int empty_slot;

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

char *
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

int
filson_unset_function(const char *name)
{
	int i;

	if (name == NULL) {
		return 0;
	}
	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used && strcmp(filson_functions[i].name, name) == 0) {
			free(filson_functions[i].name);
			free(filson_functions[i].body);
			filson_functions[i].used = 0;
			return 1;
		}
	}
	return 0;
}

void
filson_print_function_definition(const char *name)
{
	char *body;

	body = filson_lookup_function(name);
	if (body != NULL) {
		printf("%s() {\n", name);
		printf("\t%s\n", body);
		printf("}\n");
	}
}

void
filson_print_all_function_definitions(void)
{
	int i;

	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used) {
			printf("%s() {\n", filson_functions[i].name);
			printf("\t%s\n", filson_functions[i].body);
			printf("}\n");
		}
	}
}

void
filson_print_all_function_names(void)
{
	int i;

	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used) {
			printf("%s\n", filson_functions[i].name);
		}
	}
}

void
filson_print_all_function_declarations(void)
{
	int i;

	for (i = 0; i < FILSON_MAX_FUNCTIONS; i++) {
		if (filson_functions[i].used) {
			printf("declare -f %s\n", filson_functions[i].name);
		}
	}
}

char *
filson_get_pospar(int idx)
{
	struct filson_param_frame *fr;

	if (filson_call_depth == 0) {
		if (idx == 0) {
			return getenv("FILSON_SCRIPT_PATH");
		}
		if (idx < 1 || idx > filson_global_pospar_count) {
			return NULL;
		}
		return filson_global_posparams[idx - 1];
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	if (idx < 0 || idx >= fr->count) {
		return NULL;
	}
	return fr->params[idx];
}

int
filson_get_pospar_count(void)
{
	struct filson_param_frame *fr;

	if (filson_call_depth == 0) {
		return filson_global_pospar_count;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	if (fr->count <= 1) {
		return 0;
	}
	return fr->count - 1;
}

int
filson_shift_posparams(int n)
{
	struct filson_param_frame *fr;
	int i;

	if (!filson_has_active_function()) {
		return -1;
	}
	fr = &filson_call_stack[filson_call_depth - 1];
	if (n > fr->count - 1) {
		return -1;
	}
	if (n <= 0) {
		return 0;
	}
	for (i = 1; i < fr->count - n; i++) {
		if (fr->params[i] != NULL) {
			free(fr->params[i]);
		}
		fr->params[i] = fr->params[i + n];
	}
	for (i = fr->count - n; i < fr->count; i++) {
		fr->params[i] = NULL;
	}
	fr->count -= n;
	return 0;
}

int
filson_has_active_function(void)
{
	return filson_call_depth > 0 || filson_dot_depth > 0;
}

void
filson_set_posparams(char **args, int count)
{
	int i;
	struct filson_param_frame *fr;

	if (filson_call_depth > 0) {
		fr = &filson_call_stack[filson_call_depth - 1];
		for (i = 1; i < fr->count; i++) {
			free(fr->params[i]);
			fr->params[i] = NULL;
		}
		fr->count = 1;
		for (i = 0; i < count && (i + 1) < FILSON_MAX_POSPARAMS; i++) {
			fr->params[i + 1] = args[i] ? strdup(args[i]) : NULL;
			fr->count++;
		}
		return;
	}
	for (i = 0; i < filson_global_pospar_count; i++) {
		free(filson_global_posparams[i]);
		filson_global_posparams[i] = NULL;
	}
	filson_global_pospar_count = 0;
	for (i = 0; i < count && i < FILSON_MAX_POSPARAMS; i++) {
		filson_global_posparams[i] = args[i] ? strdup(args[i]) : NULL;
		filson_global_pospar_count++;
	}
}

void
filson_declare_local(const char *name)
{
	struct filson_param_frame *fr;
	int i;

	if (filson_call_depth == 0 || name == NULL) {
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

int
filson_return_from_function(int ret_value)
{
	if (filson_call_depth == 0 && filson_dot_depth == 0) {
		return 0;
	}
	if (filson_call_depth > 0)
		filson_call_stack[filson_call_depth - 1].return_value = ret_value;
	else
		filson_call_stack[0].return_value = ret_value;
	filson_function_return_requested = 1;
	return 1;
}

int
filson_consume_return_value(void)
{
	int rv;

	if (!filson_function_return_requested) {
		return -1;
	}
	if (filson_call_depth == 0 && filson_dot_depth == 0) {
		filson_function_return_requested = 0;
		return -1;
	}
	if (filson_call_depth > 0)
		rv = filson_call_stack[filson_call_depth - 1].return_value;
	else
		rv = filson_call_stack[0].return_value;
	filson_function_return_requested = 0;
	return rv;
}

void
filson_push_source_frame(void)
{
	filson_dot_depth++;
	filson_function_return_requested = 0;
}

void
filson_pop_source_frame(void)
{
	if (filson_dot_depth > 0)
		filson_dot_depth--;
}

int
filson_call_function(const char *name, char **args)
{
	char *body;
	struct filson_param_frame *frame;
	int i;
	int argc;

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
	frame->params[argc++] = strdup(name);
	for (i = 1; args[i] != NULL && argc < FILSON_MAX_POSPARAMS; i++) {
		frame->params[argc++] = strdup(args[i]);
	}
	frame->count = argc;
	filson_call_depth++;
	filson_function_return_requested = 0;
	filson_execute_and_chain(body);
	filson_restore_locals();
	{
		int rv;
		extern int filson_last_exit_status;
		rv = frame->return_value;
		filson_call_depth--;
		for (i = 0; i < frame->count; i++) {
			free(frame->params[i]);
			frame->params[i] = NULL;
		}
		frame->count = 0;
		filson_last_exit_status = rv;
		filson_last_cmd_success = (rv == 0) ? 1 : 0;
	}
	return 1;
}
