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
filson_has_glob_chars_impl(const char *str)
{
	if (str == NULL || str[0] == '\0') {
		return 0;
	}
	for (int i = 0; str[i] != '\0'; i++) {
		if (str[i] == '*' || str[i] == '?' || str[i] == '[') {
			return 1;
		}
		if (str[i] == '~' && i == 0) {
			return 1;
		}
	}
	return 0;
}

int
filson_has_glob_chars(const char *str)
{
	return filson_has_glob_chars_impl(str);
}

static int
filson_glob_expand_impl(const char *pattern, glob_t *pglob)
{
	int flags = GLOB_NOCHECK;

	return glob(pattern, flags, NULL, pglob);
}

int
filson_glob_expand(const char *pattern, glob_t *pglob)
{
	return filson_glob_expand_impl(pattern, pglob);
}

static char **
filson_eg_grow(char **expanded, int *bufsize_p)
{
	char **temp;
	*bufsize_p += FILSON_GLOB_BUFSIZE;
	temp = realloc(expanded, *bufsize_p * sizeof(char *));
	if (!temp) { fprintf(stderr, "filson: allocation error\n"); }
	return temp;
}

static int
filson_eg_add_glob(char **pglob_pathv, int pathc, char **expanded,
    int *position_p, int *bufsize_p, char ***expanded_p)
{
	for (int j = 0; j < pathc; j++) {
		if (*position_p >= *bufsize_p - 1) {
			char **t = filson_eg_grow(*expanded_p, bufsize_p);
			if (!t) return 0;
			*expanded_p = t;
			expanded = t;
		}
		expanded[*position_p] = malloc(strlen(pglob_pathv[j]) + 1);
		if (!expanded[*position_p]) { fprintf(stderr, "filson: allocation error\n"); return 0; }
		strcpy(expanded[*position_p], pglob_pathv[j]);
		(*position_p)++;
	}
	return 1;
}

char **
filson_expand_globs(char **args)
{
	int has_globs, bufsize, position;
	glob_t pglob;
	char **expanded;

	if (args == NULL) return NULL;
	has_globs = 0;
	for (int i = 0; args[i] != NULL; i++)
		if (filson_has_glob_chars_impl(args[i])) { has_globs = 1; break; }
	if (!has_globs) return args;
	bufsize = FILSON_GLOB_BUFSIZE;
	position = 0;
	expanded = malloc(bufsize * sizeof(char *));
	if (!expanded) { fprintf(stderr, "filson: allocation error\n"); return args; }
	for (int i = 0; args[i] != NULL; i++) {
		if (filson_has_glob_chars_impl(args[i])) {
			if (filson_glob_expand_impl(args[i], &pglob) == 0 && pglob.gl_pathc > 0) {
				if (!filson_eg_add_glob(pglob.gl_pathv, (int)pglob.gl_pathc,
				    expanded, &position, &bufsize, &expanded)) {
					globfree(&pglob); free(expanded); return args;
				}
				globfree(&pglob);
			}
		} else {
			if (position >= bufsize - 1) {
				char **t = filson_eg_grow(expanded, &bufsize);
				if (!t) { free(expanded); return args; }
				expanded = t;
			}
			expanded[position++] = args[i];
		}
	}
	expanded[position] = NULL;
	return expanded;
}

void
filson_free_expanded_args(char **args)
{
	if (args == NULL) {
		return;
	}
	for (int i = 0; args[i] != NULL; i++) {
		free(args[i]);
	}
	free(args);
}
