#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "history.h"

#define FILSON_HISTORY_SIZE 100

extern int filson_last_cmd_success;

static char *filson_history_entries[FILSON_HISTORY_SIZE];
static int filson_history_count = 0;

static const char *
filson_history_last_entry(void)
{
	if (filson_history_count == 0) {
		return NULL;
	}
	return filson_history_entries[filson_history_count - 1];
}

static char *
filson_history_last_arg(const char *line)
{
	int start, end, i;
	char *arg;

	if (line == NULL) {
		return NULL;
	}
	end = (int)strlen(line) - 1;
	while (end >= 0 && isspace((unsigned char)line[end])) {
		end--;
	}
	if (end < 0) {
		return strdup("");
	}
	start = end;
	while (start >= 0 && !isspace((unsigned char)line[start])) {
		start--;
	}
	start++;
	if (line[start] == '"' && line[end] == '"' && end > start) {
		start++;
		end--;
	} else if (line[start] == '\'' && line[end] == '\'' && end > start) {
		start++;
		end--;
	}
	arg = malloc((end - start + 2));
	if (arg == NULL) {
		return NULL;
	}
	for (i = start; i <= end; i++) {
		arg[i - start] = line[i];
	}
	arg[end - start + 1] = '\0';
	return arg;
}

static char *
filson_expand_last_arg_token(const char *line, const char *last_arg)
{
	int i, count, new_len, j;
	char *expanded;

	if (line == NULL || last_arg == NULL) {
		return NULL;
	}
	count = 0;
	for (i = 0; line[i] != '\0'; i++) {
		if (line[i] == '!' && line[i + 1] == '$') {
			count++;
			i++;
		}
	}
	if (count == 0) {
		return strdup(line);
	}
	new_len = (int)strlen(line) + count * ((int)strlen(last_arg) - 2);
	expanded = malloc(new_len + 1);
	if (expanded == NULL) {
		return NULL;
	}
	for (i = 0, j = 0; line[i] != '\0'; i++) {
		if (line[i] == '!' && line[i + 1] == '$') {
			memcpy(expanded + j, last_arg, strlen(last_arg));
			j += strlen(last_arg);
			i++;
		} else {
			expanded[j++] = line[i];
		}
	}
	expanded[j] = '\0';
	return expanded;
}

int
filson_history_count_entries(void)
{
	return filson_history_count;
}

const char *
filson_history_get(int idx)
{
	if (idx < 0 || idx >= filson_history_count) {
		return NULL;
	}
	return filson_history_entries[idx];
}

int
filson_history(char **args)
{
	int i, start, limit;
	char *endptr;
	long parsed;

	if (args[1] != NULL && strcmp(args[1], "-c") == 0) {
		filson_clear_history();
		filson_last_cmd_success = 1;
		return 1;
	}
	limit = filson_history_count;
	if (args[1] != NULL) {
		parsed = strtol(args[1], &endptr, 10);
		if (endptr == args[1] || *endptr != '\0' || parsed <= 0) {
			fprintf(stderr, "filson: history expects a positive number\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		if (parsed < limit) {
			limit = (int)parsed;
		}
	}
	start = filson_history_count - limit;
	for (i = start; i < filson_history_count; i++) {
		printf("%d %s\n", i + 1, filson_history_entries[i]);
	}
	filson_last_cmd_success = 1;
	return 1;
}

void
filson_add_history(const char *line)
{
	int i;

	if (line == NULL || line[0] == '\0') {
		return;
	}
	if (filson_history_count == FILSON_HISTORY_SIZE) {
		free(filson_history_entries[0]);
		for (i = 1; i < FILSON_HISTORY_SIZE; i++) {
			filson_history_entries[i - 1] = filson_history_entries[i];
		}
		filson_history_count--;
	}
	filson_history_entries[filson_history_count] = strdup(line);
	if (filson_history_entries[filson_history_count] != NULL) {
		filson_history_count++;
	}
}

void
filson_clear_history(void)
{
	int i;

	for (i = 0; i < filson_history_count; i++) {
		free(filson_history_entries[i]);
		filson_history_entries[i] = NULL;
	}
	filson_history_count = 0;
}

char *
filson_trim(char *s)
{
	char *end;

	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}
	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}
	return s;
}

char *
filson_resolve_history(char *line)
{
	char *trimmed;
	char *endptr;
	char *last_arg;
	char *expanded;
	const char *last_entry;
	long idx;

	trimmed = filson_trim(line);
	if (strcmp(trimmed, "!") == 0) {
		last_entry = filson_history_last_entry();
		if (last_entry == NULL) {
			fprintf(stderr, "filson: no commands in history\n");
			return NULL;
		}
		return strdup(last_entry);
	}
	if (strcmp(trimmed, "!!") == 0) {
		last_entry = filson_history_last_entry();
		if (last_entry == NULL) {
			fprintf(stderr, "filson: no commands in history\n");
			return NULL;
		}
		return strdup(last_entry);
	}
	if (trimmed[0] == '!' && trimmed[1] != '\0') {
		idx = strtol(trimmed + 1, &endptr, 10);
		if (*endptr != '\0' || idx <= 0 || idx > filson_history_count) {
			fprintf(stderr, "filson: invalid history reference: %s\n", trimmed);
			return NULL;
		}
		return strdup(filson_history_entries[idx - 1]);
	}
	if (strstr(trimmed, "!$") != NULL) {
		last_entry = filson_history_last_entry();
		if (last_entry == NULL) {
			fprintf(stderr, "filson: no commands in history\n");
			return NULL;
		}
		last_arg = filson_history_last_arg(last_entry);
		if (last_arg == NULL) {
			return NULL;
		}
		expanded = filson_expand_last_arg_token(trimmed, last_arg);
		free(last_arg);
		return expanded;
	}
	return strdup(trimmed);
}