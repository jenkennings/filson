#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fnmatch.h>
#include <pwd.h>
#include "runtime_state.h"
#include "expansion.h"
#include "pipelines.h"

extern int filson_is_valid_varname(const char *name);

static int filson_brace_end(const char *s, int start);
static char *filson_expand_brace_expr(const char *inner, int inner_len);
static int filson_in_dquote_context = 0;
static char *
filson_make_quoted_result(char *r)
{
	char *q;

	if (r == NULL)
		return NULL;
	q = malloc(strlen(r) + 2);
	if (q == NULL)
		return r;
	q[0] = '\x02';
	strcpy(q + 1, r);
	free(r);
	return q;
}
static char *
filson_unescape_word(const char *s)
{
	char *out;
	int i;
	int j;

	out = malloc(strlen(s) + 1);
	if (out == NULL)
		return strdup(s);
	j = 0;
	for (i = 0; s[i] != '\0'; i++) {
		if (s[i] == '\\' && s[i + 1] != '\0') {
			i++;
		}
		out[j++] = s[i];
	}
	out[j] = '\0';
	return out;
}

static char *
filson_unescape_dquote_word(const char *s)
{
	char *out;
	int i;
	int j;
	char c;

	out = malloc(strlen(s) + 1);
	if (out == NULL)
		return strdup(s);
	j = 0;
	for (i = 0; s[i] != '\0'; i++) {
		if (s[i] == '\\' && s[i + 1] != '\0') {
			c = s[i + 1];
			if (c == '$' || c == '`' || c == '"' || c == '\\' ||
			    c == '}' || c == '\n') {
				i++;
				out[j++] = c;
				continue;
			}
		}
		out[j++] = s[i];
	}
	out[j] = '\0';
	return out;
}

static char *
filson_expand_pattern(const char *pat, int pat_len)
{
	char *out;
	int out_cap;
	int out_len;
	int i;
	int in_dq;
	char var_name[256];
	int vn_len;
	const char *vval;
	int vval_len;

	out_cap = pat_len * 3 + 16;
	out = malloc(out_cap);
	if (out == NULL)
		return NULL;
	out_len = 0;
	in_dq = 0;
	for (i = 0; i < pat_len; i++) {
		if ((unsigned char)pat[i] == 0x05) {
			in_dq = !in_dq;
			continue;
		}
		if (pat[i] == '$' && i + 1 < pat_len) {
			if (pat[i + 1] == '{') {
				int bend;
				char *brace_result;

				bend = filson_brace_end(pat, i + 2);
				if (bend > 0 && bend <= pat_len) {
					char *inner_copy;
					int inner_copy_len = bend - i - 2;
					inner_copy = malloc(inner_copy_len + 1);
					if (inner_copy == NULL) {
						free(out);
						return NULL;
					}
					memcpy(inner_copy, pat + i + 2, inner_copy_len);
					inner_copy[inner_copy_len] = '\0';
					brace_result = filson_expand_brace_expr(inner_copy, inner_copy_len);
					free(inner_copy);
					if (brace_result != NULL) {
						vval_len = strlen(brace_result);
						if (out_len + vval_len * 2 + 4 > out_cap) {
							out_cap = out_len + vval_len * 2 + 64;
							out = realloc(out, out_cap);
							if (out == NULL) {
								free(brace_result);
								return NULL;
							}
						}
						if (in_dq) {
							int k;
							for (k = 0; brace_result[k] != '\0'; k++) {
								if (brace_result[k] == '*' || brace_result[k] == '?' ||
								    brace_result[k] == '[' || brace_result[k] == '\\')
									out[out_len++] = '\\';
								out[out_len++] = brace_result[k];
							}
						} else {
							memcpy(out + out_len, brace_result, vval_len);
							out_len += vval_len;
						}
						free(brace_result);
						i = bend;
						continue;
					}
				}
			}
			vn_len = 0;
			if ((pat[i + 1] >= 'a' && pat[i + 1] <= 'z') ||
			    (pat[i + 1] >= 'A' && pat[i + 1] <= 'Z') ||
			    pat[i + 1] == '_') {
				while (i + 1 + vn_len < pat_len &&
				    ((pat[i + 1 + vn_len] >= 'a' && pat[i + 1 + vn_len] <= 'z') ||
				    (pat[i + 1 + vn_len] >= 'A' && pat[i + 1 + vn_len] <= 'Z') ||
				    (pat[i + 1 + vn_len] >= '0' && pat[i + 1 + vn_len] <= '9') ||
				    pat[i + 1 + vn_len] == '_'))
					vn_len++;
				if (vn_len > 0 && vn_len < (int)sizeof(var_name) - 1) {
					memcpy(var_name, pat + i + 1, vn_len);
					var_name[vn_len] = '\0';
					vval = getenv(var_name);
					if (vval == NULL) vval = "";
					vval_len = strlen(vval);
					if (out_len + vval_len * 2 + 4 > out_cap) {
						out_cap = out_len + vval_len * 2 + 64;
						out = realloc(out, out_cap);
						if (out == NULL) return NULL;
					}
					if (in_dq) {
						int k;
						for (k = 0; vval[k] != '\0'; k++) {
							if (vval[k] == '*' || vval[k] == '?' ||
							    vval[k] == '[' || vval[k] == '\\')
								out[out_len++] = '\\';
							out[out_len++] = vval[k];
						}
					} else {
						memcpy(out + out_len, vval, vval_len);
						out_len += vval_len;
					}
					i += vn_len;
					continue;
				}
			}
		}
		if (out_len + 2 > out_cap) {
			out_cap += 64;
			out = realloc(out, out_cap);
			if (out == NULL) return NULL;
		}
		out[out_len++] = pat[i];
	}
	out[out_len] = '\0';
	return out;
}

static int
filson_brace_end(const char *s, int start)
{
	int depth;
	int k;
	int in_single;
	int in_double;

	depth = 1;
	k = start;
	in_single = 0;
	in_double = 0;
	while (s[k] != '\0' && depth > 0) {
		if (!in_double && s[k] == '\'') {
			in_single = !in_single;
			k++;
			continue;
		}
		if (!in_single && s[k] == '"') {
			in_double = !in_double;
			k++;
			continue;
		}
		if (!in_single && !in_double && s[k] == '\\' && s[k + 1] != '\0') {
			k += 2;
			continue;
		}
		if (!in_single && !in_double) {
			if (s[k] == '$' && s[k + 1] == '{') {
				depth++;
				k += 2;
				continue;
			}
			if (s[k] == '}')
				depth--;
		}
		k++;
	}
	return depth == 0 ? k - 1 : -1;
}

static char *
filson_expand_brace_expr(const char *inner, int inner_len)
{
	char *var_name;
	const char *var_value;
	char op1;
	int colon;
	int op_pos;
	int i;
	int pat_len;
	char *pattern;
	char *result;

	int best_len;
	int test_len;
	char *trimmed;
	char *word;

	if (inner_len <= 0)
		return strdup("");

	if (inner[0] == '#') {
		char num_buf[32];
		const char *val;
		int slen;

		if (inner_len == 1) {
			snprintf(num_buf, sizeof(num_buf), "%d",
			    filson_get_pospar_count());
			return strdup(num_buf);
		}
		var_name = malloc(inner_len);
		if (var_name == NULL)
			return strdup("0");
		memcpy(var_name, inner + 1, inner_len - 1);
		var_name[inner_len - 1] = '\0';
		val = getenv(var_name);
		free(var_name);
		slen = val ? (int)strlen(val) : 0;
		snprintf(num_buf, sizeof(num_buf), "%d", slen);
		return strdup(num_buf);
	}

	op_pos = -1;
	colon = 0;
	op1 = 0;
	for (i = 0; i < inner_len; i++) {
		if ((inner[i] == 'a' || inner[i] == '_' ||
		    (inner[i] >= 'A' && inner[i] <= 'Z') ||
		    (inner[i] >= 'a' && inner[i] <= 'z') ||
		    (inner[i] >= '0' && inner[i] <= '9'))) {
			continue;
		}
		if (inner[i] == ':' && i > 0) {
			colon = 1;
			op_pos = i;
			op1 = inner[i + 1];
			break;
		}
		if ((inner[i] == '-' || inner[i] == '=' || inner[i] == '+' ||
		    inner[i] == '?' || inner[i] == '#' || inner[i] == '%') && i > 0) {
			colon = 0;
			op_pos = i;
			op1 = inner[i];
			break;
		}
		break;
	}

	if (op_pos <= 0) {
		var_name = malloc(inner_len + 1);
		if (var_name == NULL)
			return strdup("");
		memcpy(var_name, inner, inner_len);
		var_name[inner_len] = '\0';
		if (inner_len == 1 && inner[0] >= '1' && inner[0] <= '9') {
			char *pv = filson_get_pospar(inner[0] - '0');
			free(var_name);
			return strdup(pv ? pv : "");
		}
		var_value = getenv(var_name);
		result = strdup(var_value ? var_value : "");
		free(var_name);
		return result;
	}

	var_name = malloc(op_pos + 1);
	if (var_name == NULL)
		return strdup("");
	memcpy(var_name, inner, op_pos);
	var_name[op_pos] = '\0';

	if (op_pos == 1 && var_name[0] >= '1' && var_name[0] <= '9') {
		char *pv = filson_get_pospar(var_name[0] - '0');
		var_value = pv ? (((unsigned char)pv[0] == 0x01 || (unsigned char)pv[0] == 0x02) ? pv + 1 : pv) : NULL;
	} else {
		var_value = getenv(var_name);
	}

	if (inner[op_pos] == '#') {
		int greedy = (inner[op_pos + 1] == '#');
		int pfx_start = op_pos + (greedy ? 2 : 1);

		pat_len = inner_len - pfx_start;
		pattern = filson_expand_pattern(inner + pfx_start, pat_len);
		if (pattern == NULL) {
			free(var_name);
			return strdup("");
		}

		if (var_value == NULL) {
			free(var_name);
			free(pattern);
			return strdup("");
		}
		best_len = 0;
		if (pat_len == 0) {
			free(var_name);
			free(pattern);
			return strdup(var_value);
		}
		if (greedy) {
			for (test_len = strlen(var_value); test_len >= 0; test_len--) {
				char tmp_c = ((char *)var_value)[test_len];
				((char *)var_value)[test_len] = '\0';
				if (fnmatch(pattern, var_value, 0) == 0) {
					best_len = test_len;
					((char *)var_value)[test_len] = tmp_c;
					break;
				}
				((char *)var_value)[test_len] = tmp_c;
			}
		} else {
			for (test_len = 0; test_len <= (int)strlen(var_value); test_len++) {
				char tmp_c = ((char *)var_value)[test_len];
				((char *)var_value)[test_len] = '\0';
				if (fnmatch(pattern, var_value, 0) == 0) {
					best_len = test_len;
					((char *)var_value)[test_len] = tmp_c;
					break;
				}
				((char *)var_value)[test_len] = tmp_c;
			}
		}
		result = strdup(var_value + best_len);
		free(var_name);
		free(pattern);
		return result;
	}

	if (inner[op_pos] == '%') {
		int greedy = (inner[op_pos + 1] == '%');
		int sfx_start = op_pos + (greedy ? 2 : 1);
		int vlen;

		pat_len = inner_len - sfx_start;
		pattern = filson_expand_pattern(inner + sfx_start, pat_len);
		if (pattern == NULL) {
			free(var_name);
			return strdup("");
		}

		if (var_value == NULL) {
			free(var_name);
			free(pattern);
			return strdup("");
		}
		vlen = strlen(var_value);
		if (pat_len == 0) {
			free(var_name);
			free(pattern);
			return strdup(var_value);
		}
		best_len = vlen;
		if (greedy) {
			for (test_len = 0; test_len <= vlen; test_len++) {
				if (fnmatch(pattern, var_value + test_len, 0) == 0) {
					best_len = test_len;
					break;
				}
			}
		} else {
			for (test_len = vlen; test_len >= 0; test_len--) {
				if (fnmatch(pattern, var_value + test_len, 0) == 0) {
					best_len = test_len;
					break;
				}
			}
		}
		trimmed = malloc(best_len + 1);
		if (trimmed == NULL) {
			free(var_name);
			free(pattern);
			return strdup("");
		}
		memcpy(trimmed, var_value, best_len);
		trimmed[best_len] = '\0';
		free(var_name);
		free(pattern);
		return trimmed;
	}

	word = malloc(inner_len - (colon ? op_pos + 2 : op_pos + 1) + 1);
	if (word == NULL) {
		free(var_name);
		return strdup("");
	}
	{
		int word_start = colon ? op_pos + 2 : op_pos + 1;
		int word_len = inner_len - word_start;
		memcpy(word, inner + word_start, word_len);
		word[word_len] = '\0';
	}

	{
		int word_is_quoted = ((unsigned char)word[0] == 0x05);

	if (op1 == '-') {
		int unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		if (unset_or_empty) {
			char *tmp = filson_expand_string_variables(word);
			char *expanded = (tmp == word) ? strdup(word) : tmp;
			if (filson_in_dquote_context) {
				result = filson_unescape_dquote_word(expanded);
				free(expanded);
			} else {
				result = filson_unescape_word(expanded);
				if (result != expanded)
					free(expanded);
			}
			if (word_is_quoted && filson_in_dquote_context)
				result = filson_make_quoted_result(result);
		} else {
			result = strdup(var_value);
		}
		free(var_name);
		free(word);
		return result;
	}

	if (op1 == '=') {
		int unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		if (unset_or_empty) {
			char *tmp = filson_expand_string_variables(word);
			char *expanded = (tmp == word) ? strdup(word) : tmp;
			if (filson_in_dquote_context) {
				result = filson_unescape_dquote_word(expanded);
				free(expanded);
			} else {
				result = filson_unescape_word(expanded);
				if (result != expanded)
					free(expanded);
			}
			if (word_is_quoted && filson_in_dquote_context)
				result = filson_make_quoted_result(result);
			setenv(var_name, (unsigned char)result[0] == 0x02 ? result + 1 : result, 1);
		} else {
			result = strdup(var_value);
		}
		free(var_name);
		free(word);
		return result;
	}

	if (op1 == '+') {
		int set_and_nonempty = (var_value != NULL) && (!colon || var_value[0] != '\0');
		if (set_and_nonempty) {
			if (word_is_quoted &&
			    (unsigned char)word[0] == 0x05 && word[1] == '$' &&
			    word[2] == '@' && (unsigned char)word[3] == 0x05 &&
			    word[4] == '\0') {
				int count = filson_get_pospar_count();
				if (count == 0) {
					result = strdup("\x03");
				} else {
					int total;
					int kk;
					int rp;
					char *pv;

					total = 2;
					for (kk = 1; kk <= count; kk++) {
						pv = filson_get_pospar(kk);
						if (pv && ((unsigned char)pv[0] == 0x01 ||
						    (unsigned char)pv[0] == 0x02))
							pv++;
						total += (pv ? (int)strlen(pv) : 0) + 1;
					}
					result = malloc(total);
					if (result != NULL) {
						rp = 0;
						result[rp++] = '\x03';
						for (kk = 1; kk <= count; kk++) {
							pv = filson_get_pospar(kk);
							if (pv && ((unsigned char)pv[0] == 0x01 ||
							    (unsigned char)pv[0] == 0x02))
								pv++;
							if (kk > 1)
								result[rp++] = '\x1f';
							if (pv) {
								int plen = strlen(pv);
								memcpy(result + rp, pv, plen);
								rp += plen;
							}
						}
						result[rp] = '\0';
					} else {
						result = strdup("");
					}
				}
			} else {
				char *tmp = filson_expand_string_variables(word);
				char *expanded = (tmp == word) ? strdup(word) : tmp;
				if (filson_in_dquote_context) {
					result = filson_unescape_dquote_word(expanded);
					free(expanded);
				} else {
					result = filson_unescape_word(expanded);
					if (result != expanded)
						free(expanded);
				}
				if (word_is_quoted && filson_in_dquote_context)
					result = filson_make_quoted_result(result);
			}
		} else {
			result = strdup("");
		}
		free(var_name);
		free(word);
		return result;
	}

	if (op1 == '?') {
		int unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		if (unset_or_empty) {
			fprintf(stderr, "%s: %s\n", var_name, word[0] ? word : "parameter null or not set");
			free(var_name);
			free(word);
			return strdup("");
		}
		result = strdup(var_value);
		free(var_name);
		free(word);
		return result;
	}

	free(var_name);
	free(word);
	return strdup("");
	}
}

char *
filson_tilde_expand(const char *str)
{
	int ulen;
	const char *home;
	char *username;
	struct passwd *pw;
	char *result;
	size_t homelen;
	size_t restlen;

	if (str[0] != '~')
		return NULL;
	ulen = 0;
	while (str[1 + ulen] != '\0' && str[1 + ulen] != '/')
		ulen++;
	if (ulen == 0) {
		home = getenv("HOME");
		if (home == NULL)
			return NULL;
		homelen = strlen(home);
		restlen = strlen(str + 1);
		result = malloc(homelen + restlen + 1);
		if (result == NULL)
			return NULL;
		memcpy(result, home, homelen);
		memcpy(result + homelen, str + 1, restlen + 1);
		return result;
	}
	username = malloc(ulen + 1);
	if (username == NULL)
		return NULL;
	memcpy(username, str + 1, ulen);
	username[ulen] = '\0';
	pw = getpwnam(username);
	free(username);
	if (pw == NULL)
		return NULL;
	homelen = strlen(pw->pw_dir);
	restlen = strlen(str + 1 + ulen);
	result = malloc(homelen + restlen + 1);
	if (result == NULL)
		return NULL;
	memcpy(result, pw->pw_dir, homelen);
	memcpy(result + homelen, str + 1 + ulen, restlen + 1);
	return result;
}

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
	int expansion_found;

	if (str == NULL) {
		return strdup("");
	}
	if ((unsigned char)str[0] == 0x01)
		return strdup(str + 1);
	if ((unsigned char)str[0] == 0x02) {
		int save_dq;
		const char *inner;
		char *result_inner;

		save_dq = filson_in_dquote_context;
		filson_in_dquote_context = 1;
		inner = str + 1;
		result_inner = filson_expand_string_variables(inner);
		filson_in_dquote_context = save_dq;
		if (result_inner == inner)
			result_inner = strdup(inner);
		return result_inner;
	}
	expansion_found = 0;
	input_len = strlen(str);
	output = malloc(input_len * 2 + 1);
	if (output == NULL) {
		return (char *)str;
	}
	j = 0;
	for (i = 0; str[i] != '\0'; i++) {
		if ((unsigned char)str[i] == 0x05) {
			expansion_found = 1;
			continue;
		}
		if (str[i] == '$' && str[i + 1] != '\0') {
			if (str[i + 1] == '{') {
				int close_pos = filson_brace_end(str, i + 2);
				if (close_pos >= 0) {
					int inner_len = close_pos - (i + 2);
					char *expanded_val = filson_expand_brace_expr(str + i + 2, inner_len);
					if (expanded_val != NULL) {
						int val_len = strlen(expanded_val);
						if (j + val_len >= input_len * 2) {
							output = realloc(output, j + val_len + 256);
							if (output == NULL) {
								free(expanded_val);
								return (char *)str;
							}
						}
						strcpy(&output[j], expanded_val);
						j += val_len;
						free(expanded_val);
						expansion_found = 1;
					}
					i = close_pos;
					continue;
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
					}
					expansion_found = 1;
					i += var_len;
					free(var_name);
					continue;
				}
			} else if (str[i + 1] >= '0' && str[i + 1] <= '9') {
				char *pval;
				int val_len;

				pval = filson_get_pospar(str[i + 1] - '0');
				if (pval != NULL) {
					if ((unsigned char)pval[0] == 0x01 || (unsigned char)pval[0] == 0x02)
						pval++;
					val_len = strlen(pval);
					if (j + val_len > input_len * 2) {
						output = realloc(output, j + val_len + 256);
						if (output == NULL) {
							return (char *)str;
						}
					}
					strcpy(&output[j], pval);
					j += val_len;
				}
				expansion_found = 1;
				i++;
				continue;
			} else if (str[i + 1] == '?') {
				char num_buf[16];
				int val_len;
				extern int filson_last_exit_status;

				snprintf(num_buf, sizeof(num_buf), "%d", filson_last_exit_status);
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
			} else if (str[i + 1] == '$') {
				char num_buf[16];
				int val_len;

				snprintf(num_buf, sizeof(num_buf), "%d", (int)getpid());
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
			} else if (str[i + 1] == '@' || str[i + 1] == '*') {
				int k;
				int count;
				const char *ifs_sep;
				char sep_char;

				ifs_sep = getenv("IFS");
				sep_char = (ifs_sep != NULL && ifs_sep[0] != '\0') ? ifs_sep[0] : ' ';
				count = filson_get_pospar_count();
				for (k = 1; k <= count; k++) {
					char *pval;
					int val_len;

					pval = filson_get_pospar(k);
					if (pval == NULL) {
						continue;
					}
					if ((unsigned char)pval[0] == 0x01 || (unsigned char)pval[0] == 0x02)
						pval++;
					val_len = strlen(pval);
					if (j + val_len + 2 > input_len * 2) {
						output = realloc(output, j + val_len + 256);
						if (output == NULL) {
							return (char *)str;
						}
					}
					if (k > 1) {
						output[j++] = sep_char;
					}
					strcpy(&output[j], pval);
					j += val_len;
				}
				expansion_found = 1;
				i++;
				continue;
			} else if (str[i + 1] == '(' && str[i + 2] == '(') {
				int d;
				int close_p;

				d = 1;
				close_p = i + 3;
				while (str[close_p] != '\0' && d > 0) {
					if (str[close_p] == '(')
						d++;
					else if (str[close_p] == ')')
						d--;
					if (d > 0)
						close_p++;
				}
				if (d == 0 && str[close_p + 1] == ')') {
					int expr_len;
					char *arith_expr;

					expr_len = close_p - (i + 3);
					arith_expr = malloc(expr_len + 1);
					if (arith_expr != NULL) {
						char *arith_expanded;
						long arith_result;
						char result_buf[64];
						int rlen;

						memcpy(arith_expr, str + i + 3, expr_len);
						arith_expr[expr_len] = '\0';
						arith_expanded = filson_expand_string_variables(arith_expr);
						arith_result = filson_evaluate_arithmetic(
						    arith_expanded != arith_expr ? arith_expanded : arith_expr);
						if (arith_expanded != arith_expr)
							free(arith_expanded);
						free(arith_expr);
						snprintf(result_buf, sizeof(result_buf), "%ld", arith_result);
						rlen = strlen(result_buf);
						if (j + rlen >= input_len * 2) {
							output = realloc(output, j + rlen + 256);
							if (output == NULL)
								return (char *)str;
						}
						strcpy(&output[j], result_buf);
						j += rlen;
						expansion_found = 1;
					}
					i = close_p + 1;
					continue;
				}
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
