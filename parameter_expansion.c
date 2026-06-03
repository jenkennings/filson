#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "parameter_expansion.h"

#define EXPAND_BUFSIZE 256

/**
 * Extract variable name from ${...} syntax.
 * Returns pointer to variable name (caller must free), or NULL if invalid.
 */
static char *
filson_extract_var_name(const char *start, const char *end)
{
	size_t len;
	char *name;

	if (start >= end) {
		return NULL;
	}
	len = (size_t)(end - start);
	name = malloc(len + 1);
	if (name == NULL) {
		return NULL;
	}
	memcpy(name, start, len);
	name[len] = '\0';
	return name;
}

/**
 * Expand a single ${VAR} reference.
 * Handles: ${VAR}, ${VAR:-default}, ${VAR#pattern}, ${VAR%pattern}
 */
static char *
filson_expand_parameter_reference(const char *ref_start, const char *ref_end)
{
	const char *var_start, *var_end, *op_pos;
	char *var_name, *default_val, *pattern, *result;
	const char *var_value;
	size_t len;
	char *expanded;
	int cap, pos;

	if (ref_start >= ref_end || *ref_start != '$' || ref_start[1] != '{') {
		return NULL;
	}
	var_start = ref_start + 2;
	if (ref_end[-1] != '}') {
		return NULL;
	}
	var_end = ref_end - 1;
	op_pos = memchr(var_start, ':', var_end - var_start);
	if (op_pos == NULL) {
		op_pos = memchr(var_start, '#', var_end - var_start);
	}
	if (op_pos == NULL) {
		op_pos = memchr(var_start, '%', var_end - var_start);
	}
	if (op_pos == NULL) {
		var_name = filson_extract_var_name(var_start, var_end);
		if (var_name == NULL) {
			return NULL;
		}
		var_value = getenv(var_name);
		if (var_value == NULL) {
			free(var_name);
			return strdup("");
		}
		result = strdup(var_value);
		free(var_name);
		return result;
	}
	var_name = filson_extract_var_name(var_start, op_pos);
	if (var_name == NULL) {
		return NULL;
	}
	if (op_pos[0] == ':' && op_pos[1] == '-') {
		default_val = filson_extract_var_name(op_pos + 2, var_end);
		if (default_val == NULL) {
			free(var_name);
			return NULL;
		}
		var_value = getenv(var_name);
		if (var_value == NULL || var_value[0] == '\0') {
			result = default_val;
		} else {
			free(default_val);
			result = strdup(var_value);
		}
		free(var_name);
		return result;
	}
	if (op_pos[0] == '#') {
		pattern = filson_extract_var_name(op_pos + 1, var_end);
		if (pattern == NULL) {
			free(var_name);
			return NULL;
		}
		var_value = getenv(var_name);
		if (var_value == NULL) {
			free(var_name);
			free(pattern);
			return strdup("");
		}
		len = strlen(var_value);
		if (len >= strlen(pattern) && strncmp(var_value, pattern, strlen(pattern)) == 0) {
			result = strdup(var_value + strlen(pattern));
		} else {
			result = strdup(var_value);
		}
		free(var_name);
		free(pattern);
		return result;
	}
	if (op_pos[0] == '%') {
		pattern = filson_extract_var_name(op_pos + 1, var_end);
		if (pattern == NULL) {
			free(var_name);
			return NULL;
		}
		var_value = getenv(var_name);
		if (var_value == NULL) {
			free(var_name);
			free(pattern);
			return strdup("");
		}
		len = strlen(var_value);
		if (len >= strlen(pattern) && strcmp(var_value + len - strlen(pattern), pattern) == 0) {
			expanded = malloc(len - strlen(pattern) + 1);
			if (expanded == NULL) {
				free(var_name);
				free(pattern);
				return NULL;
			}
			memcpy(expanded, var_value, len - strlen(pattern));
			expanded[len - strlen(pattern)] = '\0';
			result = expanded;
		} else {
			result = strdup(var_value);
		}
		free(var_name);
		free(pattern);
		return result;
	}
	free(var_name);
	return NULL;
}

char *
filson_expand_parameters(const char *input)
{
	int i, j, depth, in_single, in_double;
	const char *ref_start;
	char *expanded_ref;
	char *out;
	int out_cap, out_len;
	char *tmp;

	if (input == NULL) {
		return NULL;
	}
	out_cap = (strlen(input) * 2) + 1;
	out = malloc(out_cap);
	if (out == NULL) {
		return NULL;
	}
	out_len = 0;
	i = 0;
	in_single = 0;
	in_double = 0;
	while (input[i] != '\0') {
		if (!in_double && input[i] == '\'') {
			in_single = !in_single;
			out[out_len++] = input[i++];
			continue;
		}
		if (!in_single && input[i] == '"') {
			in_double = !in_double;
			out[out_len++] = input[i++];
			continue;
		}
		if (!in_single && input[i] == '$' && input[i + 1] == '{') {
			ref_start = input + i;
			j = i + 2;
			depth = 1;
			while (input[j] != '\0' && depth > 0) {
				if (input[j] == '{') {
					depth++;
				} else if (input[j] == '}') {
					depth--;
				}
				j++;
			}
			if (depth != 0) {
				free(out);
				return NULL;
			}
			expanded_ref = filson_expand_parameter_reference(ref_start, input + j);
			if (expanded_ref == NULL) {
				free(out);
				return NULL;
			}
			if (out_len + (int)strlen(expanded_ref) + 1 > out_cap) {
				while (out_len + (int)strlen(expanded_ref) + 1 > out_cap) {
					out_cap *= 2;
				}
				tmp = realloc(out, out_cap);
				if (tmp == NULL) {
					free(expanded_ref);
					free(out);
					return NULL;
				}
				out = tmp;
			}
			memcpy(out + out_len, expanded_ref, strlen(expanded_ref));
			out_len += strlen(expanded_ref);
			free(expanded_ref);
			i = j;
			continue;
		}
		if (out_len + 2 > out_cap) {
			out_cap *= 2;
			tmp = realloc(out, out_cap);
			if (tmp == NULL) {
				free(out);
				return NULL;
			}
			out = tmp;
		}
		out[out_len++] = input[i++];
	}
	out[out_len] = '\0';
	return out;
}
