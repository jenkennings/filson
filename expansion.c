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
	int j;

	out = malloc(strlen(s) + 1);
	if (out == NULL)
		return strdup(s);
	j = 0;
	for (int i = 0; s[i] != '\0'; i++) {
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
	int j;

	out = malloc(strlen(s) + 1);
	if (out == NULL)
		return strdup(s);
	j = 0;
	for (int i = 0; s[i] != '\0'; i++) {
		if (s[i] == '\\' && s[i + 1] != '\0') {
			char c = s[i + 1];
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

static void
filson_ep_append(char **out, int *out_len, int *out_cap,
    const char *val, int val_len, int escape_glob)
{
	if (*out_len + val_len * 2 + 4 > *out_cap) {
		*out_cap = *out_len + val_len * 2 + 64;
		*out = realloc(*out, *out_cap);
		if (*out == NULL)
			return;
	}
	if (escape_glob) {
		for (int k = 0; k < val_len; k++) {
			if (val[k] == '*' || val[k] == '?' ||
			    val[k] == '[' || val[k] == '\\')
				(*out)[(*out_len)++] = '\\';
			(*out)[(*out_len)++] = val[k];
		}
	} else {
		memcpy(*out + *out_len, val, val_len);
		*out_len += val_len;
	}
}

static int
filson_ep_try_brace(const char *pat, int pat_len, int i,
    char **out, int *out_len, int *out_cap, int in_dq)
{
	int bend, inner_copy_len, vval_len;
	char *brace_result, *inner_copy;

	bend = filson_brace_end(pat, i + 2);
	if (bend <= 0 || bend > pat_len)
		return 0;
	inner_copy_len = bend - i - 2;
	inner_copy = malloc(inner_copy_len + 1);
	if (inner_copy == NULL) { free(*out); *out = NULL; return -1; }
	memcpy(inner_copy, pat + i + 2, inner_copy_len);
	inner_copy[inner_copy_len] = '\0';
	brace_result = filson_expand_brace_expr(inner_copy, inner_copy_len);
	free(inner_copy);
	if (brace_result == NULL)
		return 0;
	vval_len = strlen(brace_result);
	filson_ep_append(out, out_len, out_cap, brace_result, vval_len, in_dq);
	free(brace_result);
	if (*out == NULL)
		return -1;
	return bend;
}

static int
filson_ep_scan_name(const char *pat, int pat_len, int i)
{
	int vn_len = 0;
	if (i + 1 >= pat_len)
		return 0;
	if (!((pat[i + 1] >= 'a' && pat[i + 1] <= 'z') ||
	    (pat[i + 1] >= 'A' && pat[i + 1] <= 'Z') ||
	    pat[i + 1] == '_'))
		return 0;
	while (i + 1 + vn_len < pat_len &&
	    ((pat[i + 1 + vn_len] >= 'a' && pat[i + 1 + vn_len] <= 'z') ||
	    (pat[i + 1 + vn_len] >= 'A' && pat[i + 1 + vn_len] <= 'Z') ||
	    (pat[i + 1 + vn_len] >= '0' && pat[i + 1 + vn_len] <= '9') ||
	    pat[i + 1 + vn_len] == '_'))
		vn_len++;
	return vn_len;
}

static char *
filson_expand_pattern(const char *pat, int pat_len)
{
	char *out;
	int out_cap;
	int out_len;
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
	for (int i = 0; i < pat_len; i++) {
		if ((unsigned char)pat[i] == 0x05) {
			in_dq = !in_dq;
			continue;
		}
		if (pat[i] == '$' && i + 1 < pat_len) {
			if (pat[i + 1] == '{') {
				int bend = filson_ep_try_brace(pat, pat_len, i,
				    &out, &out_len, &out_cap, in_dq);
				if (out == NULL) return NULL;
				if (bend > 0) { i = bend; continue; }
				if (bend < 0) return NULL;
			}
			vn_len = filson_ep_scan_name(pat, pat_len, i);
			if (vn_len > 0 && vn_len < (int)sizeof(var_name) - 1) {
				memcpy(var_name, pat + i + 1, vn_len);
				var_name[vn_len] = '\0';
				vval = getenv(var_name);
				if (vval == NULL) vval = "";
				vval_len = strlen(vval);
				filson_ep_append(&out, &out_len, &out_cap, vval, vval_len, in_dq);
				if (out == NULL) return NULL;
				i += vn_len;
				continue;
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
filson_ebe_hash_len(const char *inner, int inner_len)
{
	char num_buf[32];
	char *var_name;
	const char *val;
	int slen;

	if (inner_len == 1) {
		snprintf(num_buf, sizeof(num_buf), "%d", filson_get_pospar_count());
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

static char *
filson_ebe_prefix_trim(const char *inner, int inner_len, int op_pos,
    const char *var_value, char *var_name)
{
	int greedy, pfx_start, pat_len, best_len, test_len;
	char *pattern, *result;

	greedy = (inner[op_pos + 1] == '#');
	pfx_start = op_pos + (greedy ? 2 : 1);
	pat_len = inner_len - pfx_start;
	pattern = filson_expand_pattern(inner + pfx_start, pat_len);
	if (pattern == NULL) { free(var_name); return strdup(""); }
	if (var_value == NULL) { free(var_name); free(pattern); return strdup(""); }
	if (pat_len == 0) { free(var_name); free(pattern); return strdup(var_value); }
	best_len = 0;
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

static char *
filson_ebe_suffix_trim(const char *inner, int inner_len, int op_pos,
    const char *var_value, char *var_name)
{
	int greedy, sfx_start, pat_len, vlen, best_len, test_len;
	char *pattern, *trimmed;

	greedy = (inner[op_pos + 1] == '%');
	sfx_start = op_pos + (greedy ? 2 : 1);
	pat_len = inner_len - sfx_start;
	pattern = filson_expand_pattern(inner + sfx_start, pat_len);
	if (pattern == NULL) { free(var_name); return strdup(""); }
	if (var_value == NULL) { free(var_name); free(pattern); return strdup(""); }
	if (pat_len == 0) { free(var_name); free(pattern); return strdup(var_value); }
	vlen = strlen(var_value);
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
	if (trimmed == NULL) { free(var_name); free(pattern); return strdup(""); }
	memcpy(trimmed, var_value, best_len);
	trimmed[best_len] = '\0';
	free(var_name);
	free(pattern);
	return trimmed;
}

static char *
filson_ebe_expand_word(char *word, int word_is_quoted)
{
	char *tmp, *expanded, *result;

	tmp = filson_expand_string_variables(word);
	expanded = (tmp == word) ? strdup(word) : tmp;
	if (filson_in_dquote_context) {
		result = filson_unescape_dquote_word(expanded);
		free(expanded);
	} else {
		result = filson_unescape_word(expanded);
		if (result != expanded)
			free(expanded);
	}
	if (word_is_quoted)
		result = filson_make_quoted_result(result);
	return result;
}

static char *
filson_ebe_plus_at(void)
{
	int count, total, rp;
	char *pv, *result;

	count = filson_get_pospar_count();
	if (count == 0)
		return strdup("\x03");
	total = 2;
	for (int kk = 1; kk <= count; kk++) {
		pv = filson_get_pospar(kk);
		if (pv && ((unsigned char)pv[0] == 0x01 || (unsigned char)pv[0] == 0x02))
			pv++;
		total += (pv ? (int)strlen(pv) : 0) + 1;
	}
	result = malloc(total);
	if (result == NULL)
		return strdup("");
	rp = 0;
	result[rp++] = '\x03';
	for (int kk = 1; kk <= count; kk++) {
		pv = filson_get_pospar(kk);
		if (pv && ((unsigned char)pv[0] == 0x01 || (unsigned char)pv[0] == 0x02))
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
	return result;
}

static char *
filson_ebe_word_op(char op1, int colon, const char *var_value,
    char *var_name, char *word)
{
	int word_is_quoted = ((unsigned char)word[0] == 0x05);
	int unset_or_empty;
	char *result;

	if (op1 == '-') {
		unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		result = unset_or_empty ? filson_ebe_expand_word(word, word_is_quoted) : strdup(var_value);
		free(var_name); free(word);
		return result;
	}
	if (op1 == '=') {
		unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		if (unset_or_empty) {
			result = filson_ebe_expand_word(word, 0);
			if (word_is_quoted && filson_in_dquote_context)
				result = filson_make_quoted_result(result);
			setenv(var_name, (unsigned char)result[0] == 0x02 ? result + 1 : result, 1);
		} else {
			result = strdup(var_value);
		}
		free(var_name); free(word);
		return result;
	}
	if (op1 == '+') {
		int set_and_nonempty = (var_value != NULL) && (!colon || var_value[0] != '\0');
		if (set_and_nonempty) {
			if (word_is_quoted && (unsigned char)word[0] == 0x05 &&
			    word[1] == '$' && word[2] == '@' &&
			    (unsigned char)word[3] == 0x05 && word[4] == '\0') {
				result = filson_ebe_plus_at();
			} else {
				result = filson_ebe_expand_word(word, word_is_quoted);
			}
		} else {
			result = strdup("");
		}
		free(var_name); free(word);
		return result;
	}
	if (op1 == '?') {
		unset_or_empty = (var_value == NULL) || (colon && var_value[0] == '\0');
		if (unset_or_empty) {
			fprintf(stderr, "%s: %s\n", var_name,
			    word[0] ? word : "parameter null or not set");
			free(var_name); free(word);
			return strdup("");
		}
		result = strdup(var_value);
		free(var_name); free(word);
		return result;
	}
	free(var_name); free(word);
	return strdup("");
}

static int
filson_ebe_scan_op(const char *inner, int inner_len, char *op1_out, int *colon_out)
{
	for (int i = 0; i < inner_len; i++) {
		if ((inner[i] >= 'A' && inner[i] <= 'Z') ||
		    (inner[i] >= 'a' && inner[i] <= 'z') ||
		    (inner[i] >= '0' && inner[i] <= '9') ||
		    inner[i] == '_')
			continue;
		if (inner[i] == ':' && i > 0) {
			*colon_out = 1;
			*op1_out = inner[i + 1];
			return i;
		}
		if ((inner[i] == '-' || inner[i] == '=' || inner[i] == '+' ||
		    inner[i] == '?' || inner[i] == '#' || inner[i] == '%') && i > 0) {
			*colon_out = 0;
			*op1_out = inner[i];
			return i;
		}
		return -1;
	}
	return -1;
}

static char *
filson_ebe_simple_var(const char *inner, int inner_len)
{
	char *var_name;
	const char *var_value;
	char *result;

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

static char *
filson_expand_brace_expr(const char *inner, int inner_len)
{
	char *var_name;
	const char *var_value;
	char op1;
	int colon;
	int op_pos;
	char *word;
	int word_start, word_len;

	if (inner_len <= 0)
		return strdup("");
	if (inner[0] == '#')
		return filson_ebe_hash_len(inner, inner_len);

	op1 = 0;
	colon = 0;
	op_pos = filson_ebe_scan_op(inner, inner_len, &op1, &colon);
	if (op_pos <= 0)
		return filson_ebe_simple_var(inner, inner_len);

	var_name = malloc(op_pos + 1);
	if (var_name == NULL)
		return strdup("");
	memcpy(var_name, inner, op_pos);
	var_name[op_pos] = '\0';

	if (op_pos == 1 && var_name[0] >= '1' && var_name[0] <= '9') {
		char *pv = filson_get_pospar(var_name[0] - '0');
		var_value = pv ? (((unsigned char)pv[0] == 0x01 ||
		    (unsigned char)pv[0] == 0x02) ? pv + 1 : pv) : NULL;
	} else {
		var_value = getenv(var_name);
	}

	if (inner[op_pos] == '#')
		return filson_ebe_prefix_trim(inner, inner_len, op_pos, var_value, var_name);
	if (inner[op_pos] == '%')
		return filson_ebe_suffix_trim(inner, inner_len, op_pos, var_value, var_name);

	word_start = colon ? op_pos + 2 : op_pos + 1;
	word_len = inner_len - word_start;
	word = malloc(word_len + 1);
	if (word == NULL) { free(var_name); return strdup(""); }
	memcpy(word, inner + word_start, word_len);
	word[word_len] = '\0';
	return filson_ebe_word_op(op1, colon, var_value, var_name, word);
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

static int
filson_esv_realloc(char **out, int j, int needed, int input_len)
{
	char *tmp;
	if (j + needed <= input_len * 2)
		return 1;
	tmp = realloc(*out, j + needed + 256);
	if (tmp == NULL) return 0;
	*out = tmp;
	return 1;
}

static int
filson_esv_dollar_brace(const char *str, int i, char **out, int *j_p,
    int input_len)
{
	int close_pos = filson_brace_end(str, i + 2);
	char *expanded_val;
	int val_len;

	if (close_pos < 0)
		return 0;
	expanded_val = filson_expand_brace_expr(str + i + 2, close_pos - (i + 2));
	if (expanded_val == NULL)
		return close_pos;
	val_len = strlen(expanded_val);
	if (!filson_esv_realloc(out, *j_p, val_len, input_len)) {
		free(expanded_val);
		return -1;
	}
	strcpy(*out + *j_p, expanded_val);
	*j_p += val_len;
	free(expanded_val);
	return close_pos;
}

static int
filson_esv_dollar_name(const char *str, int i, char **out, int *j_p,
    int input_len)
{
	int var_len = 0;
	char *var_name, *var_value;

	while (str[i + 1 + var_len] != '\0' &&
	    ((str[i + 1 + var_len] >= 'a' && str[i + 1 + var_len] <= 'z') ||
	    (str[i + 1 + var_len] >= 'A' && str[i + 1 + var_len] <= 'Z') ||
	    (str[i + 1 + var_len] >= '0' && str[i + 1 + var_len] <= '9') ||
	    str[i + 1 + var_len] == '_'))
		var_len++;
	var_name = malloc(var_len + 1);
	if (var_name == NULL)
		return 0;
	memcpy(var_name, &str[i + 1], var_len);
	var_name[var_len] = '\0';
	var_value = getenv(var_name);
	free(var_name);
	if (var_value != NULL) {
		int val_len = strlen(var_value);
		if (!filson_esv_realloc(out, *j_p, val_len, input_len))
			return -1;
		strcpy(*out + *j_p, var_value);
		*j_p += val_len;
	}
	return i + var_len;
}

static int
filson_esv_dollar_num(const char *str, int i, char **out, int *j_p,
    int input_len)
{
	char *pval = filson_get_pospar(str[i + 1] - '0');
	int val_len;

	if (pval != NULL) {
		if ((unsigned char)pval[0] == 0x01 || (unsigned char)pval[0] == 0x02)
			pval++;
		val_len = strlen(pval);
		if (!filson_esv_realloc(out, *j_p, val_len, input_len))
			return -1;
		strcpy(*out + *j_p, pval);
		*j_p += val_len;
	}
	return i + 1;
}

static int
filson_esv_dollar_special(const char *str, int i, char **out, int *j_p,
    int input_len)
{
	char num_buf[32];
	int val_len;
	extern int filson_last_exit_status;

	if (str[i + 1] == '?')
		snprintf(num_buf, sizeof(num_buf), "%d", filson_last_exit_status);
	else if (str[i + 1] == '$')
		snprintf(num_buf, sizeof(num_buf), "%d", (int)getpid());
	else
		snprintf(num_buf, sizeof(num_buf), "%d", filson_get_pospar_count());
	val_len = strlen(num_buf);
	if (!filson_esv_realloc(out, *j_p, val_len, input_len))
		return -1;
	strcpy(*out + *j_p, num_buf);
	*j_p += val_len;
	return i + 1;
}

static int
filson_esv_dollar_at(int i, char **out, int *j_p, int input_len)
{
	int count, val_len;
	const char *ifs_sep;
	char sep_char;
	char *pval;

	ifs_sep = getenv("IFS");
	sep_char = (ifs_sep && ifs_sep[0]) ? ifs_sep[0] : ' ';
	count = filson_get_pospar_count();
	for (int k = 1; k <= count; k++) {
		pval = filson_get_pospar(k);
		if (pval == NULL) continue;
		if ((unsigned char)pval[0] == 0x01 || (unsigned char)pval[0] == 0x02)
			pval++;
		val_len = strlen(pval);
		if (!filson_esv_realloc(out, *j_p, val_len + 2, input_len))
			return -1;
		if (k > 1)
			(*out)[(*j_p)++] = sep_char;
		strcpy(*out + *j_p, pval);
		*j_p += val_len;
	}
	return i + 1;
}

static int
filson_esv_dollar_arith(const char *str, int i, char **out, int *j_p,
    int input_len)
{
	int d, close_p, expr_len, rlen;
	char *arith_expr, *arith_expanded, result_buf[64];
	long arith_result;

	d = 1;
	close_p = i + 3;
	while (str[close_p] != '\0' && d > 0) {
		if (str[close_p] == '(') d++;
		else if (str[close_p] == ')') d--;
		if (d > 0) close_p++;
	}
	if (d != 0 || str[close_p + 1] != ')')
		return 0;
	expr_len = close_p - (i + 3);
	arith_expr = malloc(expr_len + 1);
	if (arith_expr == NULL)
		return 0;
	memcpy(arith_expr, str + i + 3, expr_len);
	arith_expr[expr_len] = '\0';
	arith_expanded = filson_expand_string_variables(arith_expr);
	arith_result = filson_evaluate_arithmetic(
	    arith_expanded != arith_expr ? arith_expanded : arith_expr);
	if (arith_expanded != arith_expr) free(arith_expanded);
	free(arith_expr);
	snprintf(result_buf, sizeof(result_buf), "%ld", arith_result);
	rlen = strlen(result_buf);
	if (!filson_esv_realloc(out, *j_p, rlen, input_len))
		return -1;
	strcpy(*out + *j_p, result_buf);
	*j_p += rlen;
	return close_p + 1;
}

char *
filson_expand_string_variables(const char *str)
{
	int j, input_len, expansion_found, local_in_dq, new_i;
	char *output, *result_inner;

	if (str == NULL)
		return strdup("");
	if ((unsigned char)str[0] == 0x01)
		return strdup(str + 1);
	if ((unsigned char)str[0] == 0x02) {
		int save_dq = filson_in_dquote_context;
		filson_in_dquote_context = 1;
		result_inner = filson_expand_string_variables(str + 1);
		filson_in_dquote_context = save_dq;
		return (result_inner == str + 1) ? strdup(str + 1) : result_inner;
	}
	expansion_found = 0;
	input_len = strlen(str);
	output = malloc(input_len * 2 + 1);
	if (output == NULL)
		return (char *)str;
	j = 0; local_in_dq = 0;
	for (int i = 0; str[i] != '\0'; i++) {
		if ((unsigned char)str[i] == 0x05) {
			expansion_found = 1;
			if (!filson_in_dquote_context) {
				local_in_dq = !local_in_dq;
				if (local_in_dq && j > 0)
					output[j++] = '\x02';
			}
			continue;
		}
		if (str[i] == '$' && str[i + 1] != '\0') {
			new_i = 0;
			if (str[i + 1] == '{')
				new_i = filson_esv_dollar_brace(str, i, &output, &j, input_len);
			else if ((str[i + 1] >= 'a' && str[i + 1] <= 'z') ||
			    (str[i + 1] >= 'A' && str[i + 1] <= 'Z') || str[i + 1] == '_')
				new_i = filson_esv_dollar_name(str, i, &output, &j, input_len);
			else if (str[i + 1] >= '0' && str[i + 1] <= '9')
				new_i = filson_esv_dollar_num(str, i, &output, &j, input_len);
			else if (str[i + 1] == '?' || str[i + 1] == '$' || str[i + 1] == '#')
				new_i = filson_esv_dollar_special(str, i, &output, &j, input_len);
			else if (str[i + 1] == '@' || str[i + 1] == '*')
				new_i = filson_esv_dollar_at(i, &output, &j, input_len);
			else if (str[i + 1] == '(' && str[i + 2] == '(')
				new_i = filson_esv_dollar_arith(str, i, &output, &j, input_len);
			if (new_i < 0) { free(output); return (char *)str; }
			if (new_i > 0) { expansion_found = 1; i = new_i; continue; }
		}
		if (j >= input_len * 2) {
			output = realloc(output, j + 256);
			if (output == NULL) return (char *)str;
		}
		output[j++] = str[i];
	}
	output[j] = '\0';
	if (!expansion_found) { free(output); return (char *)str; }
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
