#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "history.h"

#define FILSON_HISTORY_SIZE 100

extern int filson_last_cmd_success;

static char *filson_history_entries[FILSON_HISTORY_SIZE];
static int filson_history_count = 0;

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
	long idx;

	trimmed = filson_trim(line);
	if (strcmp(trimmed, "!!") == 0) {
		if (filson_history_count == 0) {
			fprintf(stderr, "filson: no commands in history\n");
			return NULL;
		}
		return strdup(filson_history_entries[filson_history_count - 1]);
	}
	if (trimmed[0] == '!' && trimmed[1] != '\0') {
		idx = strtol(trimmed + 1, &endptr, 10);
		if (*endptr != '\0' || idx <= 0 || idx > filson_history_count) {
			fprintf(stderr, "filson: invalid history reference: %s\n", trimmed);
			return NULL;
		}
		return strdup(filson_history_entries[idx - 1]);
	}
	return strdup(trimmed);
}