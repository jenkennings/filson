#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "runtime_state.h"
#include "expansion.h"

extern int filson_is_valid_varname(const char *name);

char *
filson_expand_string_variables(const char *str)
{
	int i;
	int j;
	int input_len;
	char *output;
	char *var_name;
	char *var_value;
	int var_len;
	const char *bracket_end;
	int expansion_found;

	if (str == NULL) {
		return strdup("");
	}
	expansion_found = 0;
	input_len = strlen(str);
	output = malloc(input_len * 2 + 1);
	if (output == NULL) {
		return (char *)str;
	}
	j = 0;
	for (i = 0; str[i] != '\0'; i++) {
		if (str[i] == '$' && str[i + 1] != '\0') {
			if (str[i + 1] == '{') {
				bracket_end = strchr(&str[i + 2], '}');
				if (bracket_end != NULL) {
					var_len = bracket_end - &str[i + 2];
					var_name = malloc(var_len + 1);
					if (var_name != NULL) {
						memcpy(var_name, &str[i + 2], var_len);
						var_name[var_len] = '\0';
						var_value = getenv(var_name);
						if (var_value != NULL) {
							int val_len;

							val_len = strlen(var_value);
							if (j + val_len > input_len * 2) {
								output = realloc(output, j + val_len + 256);
								if (output == NULL) {
									free(var_name);
									return (char *)str;
								}
							}
							strcpy(&output[j], var_value);
							j += val_len;
							expansion_found = 1;
						}
						free(var_name);
						i += var_len + 2;
						continue;
					}
				}
			} else if ((str[i + 1] >= 'a' && str[i + 1] <= 'z') ||
			    (str[i + 1] >= 'A' && str[i + 1] <= 'Z') ||
			    str[i + 1] == '_') {
				var_len = 0;
				while (str[i + 1 + var_len] != '\0' &&
				       ((str[i + 1 + var_len] >= 'a' && str[i + 1 + var_len] <= 'z') ||
				       (str[i + 1 + var_len] >= 'A' && str[i + 1 + var_len] <= 'Z') ||
				       (str[i + 1 + var_len] >= '0' && str[i + 1 + var_len] <= '9') ||
				       str[i + 1 + var_len] == '_')) {
					var_len++;
				}
				var_name = malloc(var_len + 1);
				if (var_name != NULL) {
					memcpy(var_name, &str[i + 1], var_len);
					var_name[var_len] = '\0';
					var_value = getenv(var_name);
					if (var_value != NULL) {
						int val_len;

						val_len = strlen(var_value);
						if (j + val_len > input_len * 2) {
							output = realloc(output, j + val_len + 256);
							if (output == NULL) {
								free(var_name);
								return (char *)str;
							}
						}
						strcpy(&output[j], var_value);
						j += val_len;
						expansion_found = 1;
					}
					i += var_len;
					free(var_name);
					continue;
				}
			} else if (str[i + 1] >= '0' && str[i + 1] <= '9') {
				char *pval;
				int val_len;

				pval = filson_get_pospar(str[i + 1] - '0');
				if (pval != NULL) {
					val_len = strlen(pval);
					if (j + val_len > input_len * 2) {
						output = realloc(output, j + val_len + 256);
						if (output == NULL) {
							return (char *)str;
						}
					}
					strcpy(&output[j], pval);
					j += val_len;
					expansion_found = 1;
				}
				i++;
				continue;
			} else if (str[i + 1] == '#') {
				char num_buf[16];
				int pcount;
				int val_len;

				pcount = filson_get_pospar_count();
				snprintf(num_buf, sizeof(num_buf), "%d", pcount);
				val_len = strlen(num_buf);
				if (j + val_len > input_len * 2) {
					output = realloc(output, j + val_len + 256);
					if (output == NULL) {
						return (char *)str;
					}
				}
				strcpy(&output[j], num_buf);
				j += val_len;
				expansion_found = 1;
				i++;
				continue;
			} else if (str[i + 1] == '@') {
				int k;
				int count;

				count = filson_get_pospar_count();
				for (k = 1; k <= count; k++) {
					char *pval;
					int val_len;

					pval = filson_get_pospar(k);
					if (pval == NULL) {
						continue;
					}
					val_len = strlen(pval);
					if (j + val_len + 2 > input_len * 2) {
						output = realloc(output, j + val_len + 256);
						if (output == NULL) {
							return (char *)str;
						}
					}
					if (k > 1) {
						output[j++] = ' ';
					}
					strcpy(&output[j], pval);
					j += val_len;
				}
				expansion_found = 1;
				i++;
				continue;
			}
		}
		if (j >= input_len * 2) {
			output = realloc(output, j + 256);
			if (output == NULL) {
				return (char *)str;
			}
		}
		output[j++] = str[i];
	}
	output[j] = '\0';
	if (!expansion_found) {
		free(output);
		return (char *)str;
	}
	return output;
}

int
filson_parse_assignment_token(const char *token, char **name_out, const char **value_out)
{
	const char *eq;
	char *name;
	size_t name_len;

	if (token == NULL) {
		return 0;
	}
	eq = strchr(token, '=');
	if (eq == NULL || eq == token) {
		return 0;
	}
	name_len = (size_t)(eq - token);
	name = malloc(name_len + 1);
	if (name == NULL) {
		return 0;
	}
	memcpy(name, token, name_len);
	name[name_len] = '\0';
	if (!filson_is_valid_varname(name)) {
		free(name);
		return 0;
	}
	*name_out = name;
	*value_out = eq + 1;
	return 1;
}

int
filson_is_assignment_token(const char *token)
{
	char *name;
	const char *value;
	int ok;

	ok = filson_parse_assignment_token(token, &name, &value);
	if (ok) {
		free(name);
	}
	return ok;
}
