#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "autocomplete.h"

static void
filson_print_safe(const char *s)
{
	while (*s != '\0') {
		unsigned char c;

		c = (unsigned char)*s;
		if (c < 0x20 || c == 0x7f) {
			putchar('?');
		} else {
			putchar(c);
		}
		s++;
	}
}

#define FILSON_RL_BUFSIZE 1024

struct filson_match_list {
	char **items;
	int count;
	int cap;
};

static int
filson_starts_with(const char *s, const char *prefix)
{
	while (*prefix != '\0') {
		if (*s == '\0' || *s != *prefix) {
			return 0;
		}
		s++;
		prefix++;
	}
	return 1;
}

static int
filson_is_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static int
filson_is_executable(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		return 0;
	}
	if (!S_ISREG(st.st_mode)) {
		return 0;
	}
	return access(path, X_OK) == 0;
}

static int
filson_match_exists(struct filson_match_list *matches, const char *value)
{
	int i;

	for (i = 0; i < matches->count; i++) {
		if (strcmp(matches->items[i], value) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
filson_add_match(struct filson_match_list *matches, const char *value)
{
	char **tmp;

	if (filson_match_exists(matches, value)) {
		return;
	}
	if (matches->count == matches->cap) {
		matches->cap = matches->cap == 0 ? 16 : matches->cap * 2;
		tmp = realloc(matches->items, (size_t)matches->cap * sizeof(char *));
		if (tmp == NULL) {
			return;
		}
		matches->items = tmp;
	}
	matches->items[matches->count] = strdup(value);
	if (matches->items[matches->count] != NULL) {
		matches->count++;
	}
}

static void
filson_free_matches(struct filson_match_list *matches)
{
	int i;

	for (i = 0; i < matches->count; i++) {
		free(matches->items[i]);
	}
	free(matches->items);
	matches->items = NULL;
	matches->count = 0;
	matches->cap = 0;
}

static int
filson_match_cmp(const void *a, const void *b)
{
	const char *sa;
	const char *sb;

	sa = *(const char *const *)a;
	sb = *(const char *const *)b;
	return strcmp(sa, sb);
}

static void
filson_sort_matches(struct filson_match_list *matches)
{
	if (matches->count > 1) {
		qsort(matches->items, (size_t)matches->count, sizeof(char *), filson_match_cmp);
	}
}

static int
filson_common_prefix_len(struct filson_match_list *matches)
{
	int i;
	int n;

	if (matches->count == 0) {
		return 0;
	}
	n = (int)strlen(matches->items[0]);
	for (i = 1; i < matches->count; i++) {
		int j;
		int m;

		m = (int)strlen(matches->items[i]);
		if (m < n) {
			n = m;
		}
		for (j = 0; j < n; j++) {
			if (matches->items[0][j] != matches->items[i][j]) {
				n = j;
				break;
			}
		}
	}
	return n;
}

static void
filson_replace_span(char **buffer, int *bufsize, int *position, int start, int end,
	const char *replacement)
{
	int replacement_len;
	int tail_len;
	int needed;
	char *tmp;

	replacement_len = (int)strlen(replacement);
	tail_len = *position - end;
	needed = start + replacement_len + tail_len + 1;
	if (needed > *bufsize) {
		while (*bufsize < needed) {
			*bufsize += FILSON_RL_BUFSIZE;
		}
		tmp = realloc(*buffer, (size_t)*bufsize);
		if (tmp == NULL) {
			return;
		}
		*buffer = tmp;
	}
	memmove(*buffer + start + replacement_len, *buffer + end, (size_t)tail_len + 1);
	memcpy(*buffer + start, replacement, (size_t)replacement_len);
	*position = start + replacement_len + tail_len;
}

static void
filson_collect_dir_matches(const char *dir_for_open, const char *display_prefix,
	const char *name_prefix, struct filson_match_list *matches)
{
	DIR *dir;
	struct dirent *entry;
	char candidate[4096];
	char fullpath[4096];
	int show_hidden;

	show_hidden = name_prefix[0] == '.';
	dir = opendir(dir_for_open);
	if (dir == NULL) {
		return;
	}
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!show_hidden && entry->d_name[0] == '.') {
			continue;
		}
		if (!filson_starts_with(entry->d_name, name_prefix)) {
			continue;
		}
		snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_for_open, entry->d_name);
		if (filson_is_dir(fullpath)) {
			snprintf(candidate, sizeof(candidate), "%s%s/", display_prefix, entry->d_name);
		} else {
			snprintf(candidate, sizeof(candidate), "%s%s", display_prefix, entry->d_name);
		}
		filson_add_match(matches, candidate);
	}
	closedir(dir);
}

static void
filson_collect_path_exec_matches(const char *prefix, struct filson_match_list *matches)
{
	char *path;
	char *path_copy;
	char *dir;
	DIR *dp;
	struct dirent *entry;
	char fullpath[4096];

	path = getenv("PATH");
	if (path == NULL || path[0] == '\0') {
		return;
	}
	path_copy = strdup(path);
	if (path_copy == NULL) {
		return;
	}
	dir = strtok(path_copy, ":");
	while (dir != NULL) {
		dp = opendir(dir[0] == '\0' ? "." : dir);
		if (dp != NULL) {
			while ((entry = readdir(dp)) != NULL) {
				if (!filson_starts_with(entry->d_name, prefix)) {
					continue;
				}
				snprintf(fullpath, sizeof(fullpath), "%s/%s",
					dir[0] == '\0' ? "." : dir, entry->d_name);
				if (filson_is_executable(fullpath)) {
					filson_add_match(matches, entry->d_name);
				}
			}
			closedir(dp);
		}
		dir = strtok(NULL, ":");
	}
	free(path_copy);
}

static void
filson_collect_builtin_matches(const char *prefix, const char **builtins, int builtin_count,
	struct filson_match_list *matches)
{
	int i;

	for (i = 0; i < builtin_count; i++) {
		if (filson_starts_with(builtins[i], prefix)) {
			filson_add_match(matches, builtins[i]);
		}
	}
}

static void
filson_collect_completions(const char *token, int command_pos,
	const char **builtins, int builtin_count, struct filson_match_list *matches)
{
	const char *slash;
	char dir_open[4096];
	char display_prefix[4096];
	char name_prefix[4096];
	int prefix_len;

	slash = strrchr(token, '/');
	if (slash != NULL) {
		prefix_len = (int)(slash - token + 1);
		if (prefix_len >= (int)sizeof(display_prefix)) {
			return;
		}
		memcpy(display_prefix, token, (size_t)prefix_len);
		display_prefix[prefix_len] = '\0';
		if (prefix_len == 1 && token[0] == '/') {
			strcpy(dir_open, "/");
		} else {
			memcpy(dir_open, token, (size_t)(prefix_len - 1));
			dir_open[prefix_len - 1] = '\0';
			if (dir_open[0] == '\0') {
				strcpy(dir_open, ".");
			}
		}
		strncpy(name_prefix, slash + 1, sizeof(name_prefix) - 1);
		name_prefix[sizeof(name_prefix) - 1] = '\0';
		filson_collect_dir_matches(dir_open, display_prefix, name_prefix, matches);
		return;
	}
	filson_collect_dir_matches(".", "", token, matches);
	if (command_pos) {
		filson_collect_builtin_matches(token, builtins, builtin_count, matches);
		filson_collect_path_exec_matches(token, matches);
	}
}

static void
filson_hac_display_matches(struct filson_match_list *matches, const char *buf,
    void (*refresh_line)(const char *))
{
	int i;
	printf("\n");
	for (i = 0; i < matches->count; i++) {
		filson_print_safe(matches->items[i]);
		if (i + 1 < matches->count) printf("  ");
	}
	printf("\n");
	refresh_line(buf);
	filson_free_matches(matches);
}

void
filson_handle_autocomplete(char **buffer, int *bufsize, int *position,
	const char **builtins, int builtin_count, void (*refresh_line)(const char *))
{
	int token_start, token_end, command_pos, common_len;
	char token[4096], partial[4096];
	struct filson_match_list matches;

	matches.items = NULL; matches.count = matches.cap = 0;
	token_end = token_start = *position;
	while (token_start > 0 && (*buffer)[token_start - 1] != ' ' &&
		(*buffer)[token_start - 1] != '\t')
		token_start--;
	if (token_end - token_start >= (int)sizeof(token)) return;
	memcpy(token, *buffer + token_start, (size_t)(token_end - token_start));
	token[token_end - token_start] = '\0';
	command_pos = token_start == 0;
	filson_collect_completions(token, command_pos, builtins, builtin_count, &matches);
	filson_sort_matches(&matches);
	if (matches.count == 0) {
		printf("\a"); fflush(stdout); filson_free_matches(&matches); return;
	}
	if (matches.count == 1) {
		filson_replace_span(buffer, bufsize, position, token_start,
			token_end, matches.items[0]);
		refresh_line(*buffer); filson_free_matches(&matches); return;
	}
	common_len = filson_common_prefix_len(&matches);
	if (common_len > token_end - token_start && common_len < (int)sizeof(partial)) {
		memcpy(partial, matches.items[0], (size_t)common_len);
		partial[common_len] = '\0';
		filson_replace_span(buffer, bufsize, position, token_start,
			token_end, partial);
		refresh_line(*buffer); filson_free_matches(&matches); return;
	}
	filson_hac_display_matches(&matches, *buffer, refresh_line);
}