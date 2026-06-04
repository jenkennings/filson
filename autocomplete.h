#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

void filson_handle_autocomplete(char **buffer, int *bufsize, int *position,
	const char **builtins, int builtin_count, void (*refresh_line)(const char *));

#endif