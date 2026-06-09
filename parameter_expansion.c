#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "parameter_expansion.h"

#define EXPAND_BUFSIZE 256

static char *
filson_extract_var_name(const char *start, const char *end)
{
	size_t len;
	char *name;

	if (start >= end) return NULL;
	len = (size_t)(end - start);
	name = malloc(len + 1);
	if (name == NULL) return NULL;
	memcpy(name, start, len);
	name[len] = '\0';
	return name;
}

static char *
filson_epr_colon_minus(const char *var_name, const char *op_pos, const char *var_end)
{
	const char *var_value;
	char *default_val = filson_extract_var_name(op_pos + 2, var_end);
	if (default_val == NULL) return NULL;
	var_value = getenv(var_name);
	if (var_value == NULL || var_value[0] == '\0') return default_val;
	free(default_val);
	return strdup(var_value);
}

static char *
filson_epr_hash(const char *var_name, const char *op_pos, const char *var_end)
{
	const char *var_value;
	char *pattern = filson_extract_var_name(op_pos + 1, var_end);
	size_t plen;
	if (pattern == NULL) return NULL;
	var_value = getenv(var_name);
	if (var_value == NULL) { free(pattern); return strdup(""); }
	plen = strlen(pattern);
	char *result = (strlen(var_value) >= plen && strncmp(var_value, pattern, plen) == 0) ?
	    strdup(var_value + plen) : strdup(var_value);
	free(pattern);
	return result;
}

static char *
filson_epr_percent(const char *var_name, const char *op_pos, const char *var_end)
{
	const char *var_value;
	char *pattern = filson_extract_var_name(op_pos + 1, var_end);
	size_t plen, vlen;
	char *expanded, *result;
	if (pattern == NULL) return NULL;
	var_value = getenv(var_name);
	if (var_value == NULL) { free(pattern); return strdup(""); }
	plen = strlen(pattern); vlen = strlen(var_value);
	if (vlen >= plen && strcmp(var_value + vlen - plen, pattern) == 0) {
		expanded = malloc(vlen - plen + 1);
		if (expanded == NULL) { free(pattern); return NULL; }
		memcpy(expanded, var_value, vlen - plen);
		expanded[vlen - plen] = '\0';
		result = expanded;
	} else {
		result = strdup(var_value);
	}
	free(pattern);
	return result;
}

static char *
filson_expand_parameter_reference(const char *ref_start, const char *ref_end)
{
	const char *var_start, *var_end, *op_pos;
	char *var_name, *result;
	const char *var_value;

	if (ref_start >= ref_end || *ref_start != '$' || ref_start[1] != '{') return NULL;
	var_start = ref_start + 2;
	if (ref_end[-1] != '}') return NULL;
	var_end = ref_end - 1;
	op_pos = memchr(var_start, ':', var_end - var_start);
	if (op_pos == NULL) op_pos = memchr(var_start, '#', var_end - var_start);
	if (op_pos == NULL) op_pos = memchr(var_start, '%', var_end - var_start);
	if (op_pos == NULL) {
		var_name = filson_extract_var_name(var_start, var_end);
		if (var_name == NULL) return NULL;
		var_value = getenv(var_name);
		result = var_value ? strdup(var_value) : strdup("");
		free(var_name);
		return result;
	}
	var_name = filson_extract_var_name(var_start, op_pos);
	if (var_name == NULL) return NULL;
	if (op_pos[0] == ':' && op_pos[1] == '-')
		result = filson_epr_colon_minus(var_name, op_pos, var_end);
	else if (op_pos[0] == '#')
		result = filson_epr_hash(var_name, op_pos, var_end);
	else if (op_pos[0] == '%')
		result = filson_epr_percent(var_name, op_pos, var_end);
	else
		result = NULL;
	free(var_name);
	return result;
}

static int
filson_ep_expand_ref(const char *input, int i, char **out_p, int *out_len_p,
    int *out_cap_p, int *new_i_p)
{
	const char *ref_start = input + i;
	int j = i + 2, depth = 1;
	char *expanded_ref, *tmp;

	while (input[j] != '\0' && depth > 0) {
		if (input[j] == '{') depth++;
		else if (input[j] == '}') depth--;
		j++;
	}
	if (depth != 0) return -1;
	expanded_ref = filson_expand_parameter_reference(ref_start, input + j);
	if (expanded_ref == NULL) return -1;
	if (*out_len_p + (int)strlen(expanded_ref) + 1 > *out_cap_p) {
		while (*out_len_p + (int)strlen(expanded_ref) + 1 > *out_cap_p)
			(*out_cap_p) *= 2;
		tmp = realloc(*out_p, *out_cap_p);
		if (tmp == NULL) { free(expanded_ref); return -1; }
		*out_p = tmp;
	}
	memcpy(*out_p + *out_len_p, expanded_ref, strlen(expanded_ref));
	*out_len_p += strlen(expanded_ref);
	free(expanded_ref);
	*new_i_p = j;
	return 0;
}

char *
filson_expand_parameters(const char *input)
{
	int i, in_single, in_double, new_i;
	char *out, *tmp;
	int out_cap, out_len;

	if (input == NULL) return NULL;
	out_cap = (strlen(input) * 2) + 1;
	out = malloc(out_cap);
	if (out == NULL) return NULL;
	out_len = i = in_single = in_double = 0;
	while (input[i] != '\0') {
		if (!in_double && input[i] == '\'') { in_single = !in_single; out[out_len++] = input[i++]; continue; }
		if (!in_single && input[i] == '"') { in_double = !in_double; out[out_len++] = input[i++]; continue; }
		if (!in_single && input[i] == '$' && input[i + 1] == '{') {
			if (filson_ep_expand_ref(input, i, &out, &out_len, &out_cap, &new_i) < 0) {
				free(out); return NULL;
			}
			i = new_i; continue;
		}
		if (out_len + 2 > out_cap) {
			out_cap *= 2;
			tmp = realloc(out, out_cap);
			if (tmp == NULL) { free(out); return NULL; }
			out = tmp;
		}
		out[out_len++] = input[i++];
	}
	out[out_len] = '\0';
	return out;
}
