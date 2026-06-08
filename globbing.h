#ifndef FILSON_GLOBBING_H
#define FILSON_GLOBBING_H

#include <glob.h>

char **filson_expand_globs(char **args);
void filson_free_expanded_args(char **args);
int filson_has_glob_chars(const char *str);
int filson_glob_expand(const char *pattern, glob_t *pglob);

#endif
