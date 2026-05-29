#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "globbing.h"

#define FILSON_GLOB_BUFSIZE 64

/**
 * Check if a string contains glob metacharacters.
 * Returns 1 if the string contains *, ?, [, or ~, 0 otherwise.
 */
static int
filson_has_glob_chars(const char *str)
{
	int i;

	if (str == NULL || str[0] == '\0') {
		return 0;
	}
	for (i = 0; str[i] != '\0'; i++) {
		if (str[i] == '*' || str[i] == '?' || str[i] == '[') {
			return 1;
		}
		if (str[i] == '~' && i == 0) {
			return 1;
		}
	}
	return 0;
}

/**
 * Expand a single glob pattern into matching files.
 * Returns an array of matched files, or NULL if no matches.
 * Caller must free the result with globfree().
 */
static int
filson_glob_expand(const char *pattern, glob_t *pglob)
{
	int flags = GLOB_NOCHECK;

	return glob(pattern, flags, NULL, pglob);
}

char **
filson_expand_globs(char **args)
{
	int i, j, has_globs;
	glob_t pglob;
	char **expanded, **temp;
	int bufsize, position;

	if (args == NULL) {
		return NULL;
	}
	has_globs = 0;
	for (i = 0; args[i] != NULL; i++) {
		if (filson_has_glob_chars(args[i])) {
			has_globs = 1;
			break;
		}
	}
	if (!has_globs) {
		return args;
	}
	bufsize = FILSON_GLOB_BUFSIZE;
	position = 0;
	expanded = malloc(bufsize * sizeof(char *));
	if (!expanded) {
		fprintf(stderr, "filson: allocation error\n");
		return args;
	}
	for (i = 0; args[i] != NULL; i++) {
		if (filson_has_glob_chars(args[i])) {
			if (filson_glob_expand(args[i], &pglob) == 0 && pglob.gl_pathc > 0) {
				for (j = 0; j < (int)pglob.gl_pathc; j++) {
					if (position >= bufsize - 1) {
						bufsize += FILSON_GLOB_BUFSIZE;
						temp = realloc(expanded, bufsize * sizeof(char *));
						if (!temp) {
							fprintf(stderr, "filson: allocation error\n");
							globfree(&pglob);
							free(expanded);
							return args;
						}
						expanded = temp;
					}
					expanded[position] = malloc(strlen(pglob.gl_pathv[j]) + 1);
					if (!expanded[position]) {
						fprintf(stderr, "filson: allocation error\n");
						globfree(&pglob);
						free(expanded);
						return args;
					}
					strcpy(expanded[position], pglob.gl_pathv[j]);
					position++;
				}
				globfree(&pglob);
			}
		} else {
			if (position >= bufsize - 1) {
				bufsize += FILSON_GLOB_BUFSIZE;
				temp = realloc(expanded, bufsize * sizeof(char *));
				if (!temp) {
					fprintf(stderr, "filson: allocation error\n");
					free(expanded);
					return args;
				}
				expanded = temp;
			}
			expanded[position] = args[i];
			position++;
		}
	}
	expanded[position] = NULL;
	free(args);
	return expanded;
}

void
filson_free_expanded_args(char **args)
{
	int i;

	if (args == NULL) {
		return;
	}
	for (i = 0; args[i] != NULL; i++) {
		free(args[i]);
	}
	free(args);
}
