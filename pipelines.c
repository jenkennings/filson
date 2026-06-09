#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <err.h>
#include "jobcontrol.h"
#include "pipelines.h"
#include "parameter_expansion.h"
#include "globbing.h"
#include "shell_session.h"
#include "expansion.h"

#define FILSON_EXPR_MAX_ITER 4096
#define FILSON_LOOP_MAX_ITER 65536

extern int filson_last_cmd_success;
extern int filson_last_exit_status;
extern int filson_execute(char **args, int argc, int background, char *segment);
extern char *filson_get_pospar(int idx);
int filson_execute_and_chain(char *line);

static char *
filson_read_command_output(FILE *fp)
{
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	assert(filson_last_cmd_success >= 0);
	char chunk[256];
	char *out;
	size_t cap, len, n;
	char *tmp;

	cap = 256;
	len = 0;
	out = malloc(cap);
	if (out == NULL) {
		return NULL;
	}
	out[0] = '\0';
	while (fgets(chunk, sizeof(chunk), fp) != NULL) {
		n = strlen(chunk);
		if (len + n + 1 > cap) {
			while (len + n + 1 > cap) {
				cap *= 2;
			}
			tmp = realloc(out, cap);
			if (tmp == NULL) {
				free(out);
				return NULL;
			}
			out = tmp;
		}
		memcpy(out + len, chunk, n);
		len += n;
		out[len] = '\0';
	}
	while (len > 0 && out[len - 1] == '\n') {
		len--;
	}
	out[len] = '\0';
	return out;
}

static char *
filson_run_subcommand(const char *cmd)
{
	assert(cmd != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int pipefd[2];
	pid_t pid;
	FILE *fp;
	char *out;
	char *cmd_copy;
	int status;

	cmd_copy = strdup(cmd);
	if (cmd_copy == NULL)
		return NULL;
	if (pipe(pipefd) == -1) {
		free(cmd_copy);
		return NULL;
	}
	pid = fork();
	if (pid == -1) {
		(void)close(pipefd[0]);
		(void)close(pipefd[1]);
		free(cmd_copy);
		return NULL;
	}
	if (pid == 0) {
		(void)close(pipefd[0]);
		(void)dup2(pipefd[1], STDOUT_FILENO);
		(void)close(pipefd[1]);
		filson_execute_and_chain(cmd_copy);
		free(cmd_copy);
		(void)fflush(stdout);
		_exit(filson_last_exit_status & 0xff);
	}
	(void)close(pipefd[1]);
	fp = fdopen(pipefd[0], "r");
	if (fp == NULL) {
		(void)close(pipefd[0]);
		waitpid(pid, &status, 0);
		free(cmd_copy);
		return NULL;
	}
	out = filson_read_command_output(fp);
	(void)fclose(fp);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		filson_last_exit_status = WEXITSTATUS(status);
		filson_last_cmd_success = (filson_last_exit_status == 0) ? 1 : 0;
	}
	free(cmd_copy);
	if (out != NULL) {
		for (int i = 0; out[i] != '\0'; i++) {
			if (out[i] == '\t')
				out[i] = '\x0e';
			else if (out[i] == '\n')
				out[i] = '\x0f';
		}
	}
	return out;
}

long filson_evaluate_arithmetic(const char *expr);
static long filson_parse_assign(const char *expr, int *pos);

static long
filson_pf_dollar_var(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	const char *var_value;
	char var_name[256];
	int var_len;
	long factor;

	(*pos)++;
	var_len = 0;
	while ((expr[*pos + var_len] >= 'a' && expr[*pos + var_len] <= 'z') ||
	       (expr[*pos + var_len] >= 'A' && expr[*pos + var_len] <= 'Z') ||
	       (expr[*pos + var_len] >= '0' && expr[*pos + var_len] <= '9') ||
	       expr[*pos + var_len] == '_')
		var_len++;
	if (var_len == 0 || var_len >= 256) return 0;
	memcpy(var_name, expr + *pos, var_len);
	var_name[var_len] = '\0';
	if (var_len == 1 && var_name[0] >= '0' && var_name[0] <= '9') {
		char *pval = filson_get_pospar(var_name[0] - '0');
		if (pval == NULL || strlen(pval) >= 256) factor = 0;
		else factor = filson_evaluate_arithmetic(pval);
	} else {
		var_value = getenv(var_name);
		if (var_value == NULL || strlen(var_value) >= 256) factor = 0;
		else factor = filson_evaluate_arithmetic(var_value);
	}
	*pos += var_len;
	return factor;
}

static long
filson_pf_paren(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	char var_name[256];
	int var_len, depth, i;

	(*pos)++;
	depth = 1;
	i = *pos;
	while (expr[i] != '\0' && depth > 0) {
		if (expr[i] == '(') depth++;
		else if (expr[i] == ')') depth--;
		i++;
	}
	if (depth != 0) return 0;
	var_len = i - *pos - 1;
	memcpy(var_name, expr + *pos, var_len);
	var_name[var_len] = '\0';
	*pos = i;
	return filson_evaluate_arithmetic(var_name);
}

static long
filson_pf_number(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long factor;

	if (expr[*pos] == '0' && (expr[*pos + 1] == 'x' || expr[*pos + 1] == 'X')) {
		*pos += 2;
		factor = 0;
		while ((expr[*pos] >= '0' && expr[*pos] <= '9') ||
		       (expr[*pos] >= 'a' && expr[*pos] <= 'f') ||
		       (expr[*pos] >= 'A' && expr[*pos] <= 'F')) {
			factor *= 16;
			if (expr[*pos] >= '0' && expr[*pos] <= '9') factor += expr[*pos] - '0';
			else if (expr[*pos] >= 'a' && expr[*pos] <= 'f') factor += expr[*pos] - 'a' + 10;
			else factor += expr[*pos] - 'A' + 10;
			(*pos)++;
		}
		return factor;
	}
	if (expr[*pos] == '0' && expr[*pos + 1] >= '0' && expr[*pos + 1] <= '7') {
		factor = 0;
		while (expr[*pos] >= '0' && expr[*pos] <= '7') {
			factor = factor * 8 + (expr[*pos] - '0');
			(*pos)++;
		}
		return factor;
	}
	factor = 0;
	while (expr[*pos] >= '0' && expr[*pos] <= '9') {
		factor = factor * 10 + (expr[*pos] - '0');
		(*pos)++;
	}
	return factor;
}

static long
filson_pf_bare_name(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	const char *var_value;
	char var_name[256];
	int var_len;

	var_len = 0;
	while ((expr[*pos + var_len] >= 'a' && expr[*pos + var_len] <= 'z') ||
	       (expr[*pos + var_len] >= 'A' && expr[*pos + var_len] <= 'Z') ||
	       (expr[*pos + var_len] >= '0' && expr[*pos + var_len] <= '9') ||
	       expr[*pos + var_len] == '_')
		var_len++;
	if (var_len == 0 || var_len >= 256) return 0;
	memcpy(var_name, expr + *pos, var_len);
	var_name[var_len] = '\0';
	*pos += var_len;
	var_value = getenv(var_name);
	if (var_value == NULL || strlen(var_value) >= 256) return 0;
	return filson_evaluate_arithmetic(var_value);
}

static long
filson_parse_factor(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	while (expr[*pos] == ' ' || expr[*pos] == '\t') (*pos)++;
	if (expr[*pos] == '!') { (*pos)++; return !filson_parse_factor(expr, pos); }
	if (expr[*pos] == '~') { (*pos)++; return ~filson_parse_factor(expr, pos); }
	if (expr[*pos] == '-') { (*pos)++; return -filson_parse_factor(expr, pos); }
	if (expr[*pos] == '+') { (*pos)++; return filson_parse_factor(expr, pos); }
	if (expr[*pos] == '$') return filson_pf_dollar_var(expr, pos);
	if (expr[*pos] == '(') return filson_pf_paren(expr, pos);
	if (expr[*pos] >= '0' && expr[*pos] <= '9') return filson_pf_number(expr, pos);
	if ((expr[*pos] >= 'a' && expr[*pos] <= 'z') ||
	    (expr[*pos] >= 'A' && expr[*pos] <= 'Z') ||
	    expr[*pos] == '_')
		return filson_pf_bare_name(expr, pos);
	return 0;
}

static long
filson_parse_term(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long term;
	long divisor;

	term = filson_parse_factor(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '*') {
			(*pos)++;
			term *= filson_parse_factor(expr, pos);
		} else if (expr[*pos] == '/') {
			(*pos)++;
			divisor = filson_parse_factor(expr, pos);
			if (divisor != 0) {
				term /= divisor;
			}
		} else if (expr[*pos] == '%') {
			(*pos)++;
			divisor = filson_parse_factor(expr, pos);
			if (divisor != 0) {
				term %= divisor;
			}
		} else {
			break;
		}
	} }
	return term;
}

static long
filson_parse_addexpr(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_term(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '+') {
			(*pos)++;
			result += filson_parse_term(expr, pos);
		} else if (expr[*pos] == '-') {
			(*pos)++;
			result -= filson_parse_term(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_shift(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_addexpr(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '<' && expr[*pos + 1] == '<') {
			*pos += 2;
			result <<= filson_parse_addexpr(expr, pos);
		} else if (expr[*pos] == '>' && expr[*pos + 1] == '>') {
			*pos += 2;
			result >>= filson_parse_addexpr(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_comparison(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_shift(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '<' && expr[*pos + 1] == '=') {
			*pos += 2;
			result = result <= filson_parse_shift(expr, pos);
		} else if (expr[*pos] == '>' && expr[*pos + 1] == '=') {
			*pos += 2;
			result = result >= filson_parse_shift(expr, pos);
		} else if (expr[*pos] == '<') {
			(*pos)++;
			result = result < filson_parse_shift(expr, pos);
		} else if (expr[*pos] == '>') {
			(*pos)++;
			result = result > filson_parse_shift(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_equality(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_comparison(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '=' && expr[*pos + 1] == '=') {
			*pos += 2;
			result = result == filson_parse_comparison(expr, pos);
		} else if (expr[*pos] == '!' && expr[*pos + 1] == '=') {
			*pos += 2;
			result = result != filson_parse_comparison(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_bitand(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_equality(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '&' && expr[*pos + 1] != '&') {
			(*pos)++;
			result &= filson_parse_equality(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_bitxor(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_bitand(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '^') {
			(*pos)++;
			result ^= filson_parse_bitand(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_bitor(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_bitxor(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '|' && expr[*pos + 1] != '|') {
			(*pos)++;
			result |= filson_parse_bitxor(expr, pos);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_logand(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_bitor(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '&' && expr[*pos + 1] == '&') {
			*pos += 2;
			result = (result != 0) & (filson_parse_bitor(expr, pos) != 0);
		} else {
			break;
		}
	} }
	return result;
}

static long
filson_parse_logor(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	long result;

	result = filson_parse_logand(expr, pos);
	{ int _iter = 0; while (_iter++ < FILSON_EXPR_MAX_ITER) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		if (expr[*pos] == '|' && expr[*pos + 1] == '|') {
			*pos += 2;
			result = (result != 0) | (filson_parse_logand(expr, pos) != 0);
		} else {
			break;
		}
	} }
	return result;
}

static int
filson_pa_scan_op(const char *expr, int after_var, char *assign_op_p, int *op_len_p)
{
	assert(expr != NULL);
	assert(assign_op_p != NULL);
	if (expr[after_var] == '=' && expr[after_var + 1] != '=') {
		*assign_op_p = '='; *op_len_p = 1; return 1;
	}
	if (expr[after_var] == '+' && expr[after_var+1] == '=') {
		*assign_op_p = '+'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '-' && expr[after_var+1] == '=') {
		*assign_op_p = '-'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '*' && expr[after_var+1] == '=') {
		*assign_op_p = '*'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '/' && expr[after_var+1] == '=') {
		*assign_op_p = '/'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '%' && expr[after_var+1] == '=') {
		*assign_op_p = '%'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '<' && expr[after_var+1] == '<' && expr[after_var+2] == '=') {
		*assign_op_p = '<'; *op_len_p = 3; return 1;
	}
	if (expr[after_var] == '>' && expr[after_var+1] == '>' && expr[after_var+2] == '=') {
		*assign_op_p = '>'; *op_len_p = 3; return 1;
	}
	if (expr[after_var] == '&' && expr[after_var+1] == '=') {
		*assign_op_p = '&'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '^' && expr[after_var+1] == '=') {
		*assign_op_p = '^'; *op_len_p = 2; return 1;
	}
	if (expr[after_var] == '|' && expr[after_var+1] == '=') {
		*assign_op_p = '|'; *op_len_p = 2; return 1;
	}
	return 0;
}

static long
filson_parse_assign(const char *expr, int *pos)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	char var_name[256];
	int var_len, after_var, is_assign, op_len;
	char assign_op;
	long val, existing;
	const char *existing_val;
	char val_str[64];
	int saved_pos;

	while (expr[*pos] == ' ' || expr[*pos] == '\t') (*pos)++;
	saved_pos = *pos;
	var_len = 0;
	if ((expr[*pos] >= 'a' && expr[*pos] <= 'z') ||
	    (expr[*pos] >= 'A' && expr[*pos] <= 'Z') ||
	    expr[*pos] == '_') {
		while ((expr[*pos + var_len] >= 'a' && expr[*pos + var_len] <= 'z') ||
		       (expr[*pos + var_len] >= 'A' && expr[*pos + var_len] <= 'Z') ||
		       (expr[*pos + var_len] >= '0' && expr[*pos + var_len] <= '9') ||
		       expr[*pos + var_len] == '_')
			var_len++;
	}
	is_assign = 0; assign_op = 0; op_len = 0;
	if (var_len > 0 && var_len < 256) {
		after_var = *pos + var_len;
		while (expr[after_var] == ' ' || expr[after_var] == '\t') after_var++;
		is_assign = filson_pa_scan_op(expr, after_var, &assign_op, &op_len);
	}
	if (is_assign) {
		memcpy(var_name, expr + *pos, var_len);
		var_name[var_len] = '\0';
		*pos = after_var + op_len;
		val = filson_parse_assign(expr, pos);
		if (assign_op != '=') {
			existing_val = getenv(var_name);
			existing = existing_val != NULL ? filson_evaluate_arithmetic(existing_val) : 0;
			switch (assign_op) {
			case '+': val = existing + val; break;
			case '-': val = existing - val; break;
			case '*': val = existing * val; break;
			case '/': val = val != 0 ? existing / val : 0; break;
			case '%': val = val != 0 ? existing % val : 0; break;
			case '<': val = existing << val; break;
			case '>': val = (long)((unsigned long)existing >> val); break;
			case '&': val = existing & val; break;
			case '^': val = existing ^ val; break;
			case '|': val = existing | val; break;
			}
		}
		snprintf(val_str, sizeof(val_str), "%ld", val);
		setenv(var_name, val_str, 1);
		return val;
	}
	*pos = saved_pos;
	return filson_parse_logor(expr, pos);
}

long
filson_evaluate_arithmetic(const char *expr)
{
	assert(expr != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int pos;

	if (expr == NULL || expr[0] == '\0') {
		return 0;
	}
	pos = 0;
	return filson_parse_assign(expr, &pos);
}

static int
filson_ecs_append_out(char **out_p, int *out_len_p, int *out_cap_p,
    const char *cmd_out)
{
	assert(out_p != NULL);
	assert(cmd_out != NULL);
	int tilde_esc, clen;
	char *tmp;

	clen = strlen(cmd_out);
	tilde_esc = (cmd_out[0] == '~') ? 1 : 0;
	if (*out_len_p + clen + 1 + tilde_esc > *out_cap_p) {
		while (*out_len_p + clen + 1 + tilde_esc > *out_cap_p)
			*out_cap_p *= 2;
		tmp = realloc(*out_p, *out_cap_p);
		if (tmp == NULL) return 0;
		*out_p = tmp;
	}
	if (tilde_esc)
		(*out_p)[(*out_len_p)++] = '\\';
	memcpy(*out_p + *out_len_p, cmd_out, clen);
	*out_len_p += clen;
	return 1;
}

static int
filson_ecs_backtick(const char *line, int i, char **out_p,
    int *out_len_p, int *out_cap_p, int *new_i_p)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int j, k;
	char *cmd, *cmd_out;

	j = i + 1;
	while (line[j] != '\0' && line[j] != '`') j++;
	if (line[j] != '`') return 0;
	cmd = malloc((j - (i + 1)) + 1);
	if (cmd == NULL) return -1;
	for (k = 0; k < j - (i + 1); k++) cmd[k] = line[i + 1 + k];
	cmd[k] = '\0';
	cmd_out = filson_run_subcommand(cmd);
	free(cmd);
	if (cmd_out != NULL) {
		if (!filson_ecs_append_out(out_p, out_len_p, out_cap_p, cmd_out)) {
			free(cmd_out);
			return -1;
		}
		free(cmd_out);
	}
	*new_i_p = j + 1;
	return 1;
}

static int
filson_ecs_dollar_arith(const char *line, int i, char **out_p,
    int *out_len_p, int *out_cap_p, int *new_i_p)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int j, depth, pass_len;
	char *tmp;

	j = i + 3;
	depth = 1;
	while (line[j] != '\0' && depth > 0) {
		if (line[j] == '(') depth++;
		else if (line[j] == ')') depth--;
		if (depth > 0) j++;
	}
	if (depth != 0 || line[j] != ')') return -1;
	pass_len = (j + 2) - i;
	if (*out_len_p + pass_len + 1 > *out_cap_p) {
		while (*out_len_p + pass_len + 1 > *out_cap_p) *out_cap_p *= 2;
		tmp = realloc(*out_p, *out_cap_p);
		if (tmp == NULL) return -1;
		*out_p = tmp;
	}
	memcpy(*out_p + *out_len_p, line + i, pass_len);
	*out_len_p += pass_len;
	*new_i_p = j + 2;
	return 1;
}

static int
filson_ecs_dollar_paren(const char *line, int i, char **out_p,
    int *out_len_p, int *out_cap_p, int *new_i_p)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int j, k, depth;
	char *cmd, *cmd_out;

	j = i + 2;
	depth = 1;
	while (line[j] != '\0' && depth > 0) {
		if (line[j] == '(') depth++;
		else if (line[j] == ')') depth--;
		if (depth > 0) j++;
	}
	if (depth != 0) return -1;
	cmd = malloc((j - (i + 2)) + 1);
	if (cmd == NULL) return -1;
	for (k = 0; k < j - (i + 2); k++) cmd[k] = line[i + 2 + k];
	cmd[k] = '\0';
	cmd_out = filson_run_subcommand(cmd);
	free(cmd);
	if (cmd_out == NULL) return -1;
	if (!filson_ecs_append_out(out_p, out_len_p, out_cap_p, cmd_out)) {
		free(cmd_out);
		return -1;
	}
	free(cmd_out);
	*new_i_p = j + 1;
	return 1;
}

static char *
filson_expand_command_substitutions(const char *line)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	char *out, *tmp;
	int i, out_cap, out_len, len, new_i, r;
	int in_single, in_double;

	if (line == NULL) return NULL;
	len = strlen(line);
	out_cap = (len * 2) + 1;
	out = malloc(out_cap);
	if (out == NULL) return NULL;
	out_len = 0;
	i = 0;
	in_single = 0;
	in_double = 0;
	while (line[i] != '\0') {
		if (!in_double && line[i] == '\'') in_single = !in_single;
		if (!in_single && line[i] == '"') in_double = !in_double;
		if (!in_single && line[i] == '`') {
			r = filson_ecs_backtick(line, i, &out, &out_len, &out_cap, &new_i);
			if (r < 0) { free(out); return NULL; }
			if (r > 0) { i = new_i; continue; }
		}
		if (!in_single && line[i] == '$' && line[i + 1] == '(') {
			if (line[i + 2] == '(') {
				r = filson_ecs_dollar_arith(line, i, &out, &out_len, &out_cap, &new_i);
				if (r < 0) { free(out); return NULL; }
				if (r > 0) { i = new_i; continue; }
			} else {
				r = filson_ecs_dollar_paren(line, i, &out, &out_len, &out_cap, &new_i);
				if (r < 0) { free(out); return NULL; }
				if (r > 0) { i = new_i; continue; }
			}
		}
		if (out_len + 2 > out_cap) {
			out_cap *= 2;
			tmp = realloc(out, out_cap);
			if (tmp == NULL) { free(out); return NULL; }
			out = tmp;
		}
		out[out_len++] = line[i++];
	}
	out[out_len] = '\0';
	return out;
}

static int
filson_nso_op(const char *line, int i, char *out, int *j_p)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int j = *j_p;

	if (line[i] == '&' && line[i + 1] == '&') {
		out[j++] = ' '; out[j++] = '&'; out[j++] = '&'; out[j++] = ' ';
		*j_p = j; return i + 2;
	}
	if (line[i] == '|' && line[i + 1] == '|') {
		out[j++] = ' '; out[j++] = '|'; out[j++] = '|'; out[j++] = ' ';
		*j_p = j; return i + 2;
	}
	if (line[i] == '2' && line[i+1] == '>' && line[i+2] == '&' && line[i+3] == '1') {
		out[j++] = ' '; out[j++] = '2'; out[j++] = '>'; out[j++] = '&'; out[j++] = '1'; out[j++] = ' ';
		*j_p = j; return i + 4;
	}
	if (line[i] == '2' && line[i+1] == '>' && line[i+2] == '>') {
		out[j++] = ' '; out[j++] = '2'; out[j++] = '>'; out[j++] = '>'; out[j++] = ' ';
		*j_p = j; return i + 3;
	}
	if (line[i] == '2' && line[i+1] == '>') {
		out[j++] = ' '; out[j++] = '2'; out[j++] = '>'; out[j++] = ' ';
		*j_p = j; return i + 2;
	}
	if (line[i] == '1' && line[i+1] == '>' && line[i+2] == '>') {
		out[j++] = ' '; out[j++] = '1'; out[j++] = '>'; out[j++] = '>'; out[j++] = ' ';
		*j_p = j; return i + 3;
	}
	if (line[i] == '1' && line[i+1] == '>') {
		out[j++] = ' '; out[j++] = '1'; out[j++] = '>'; out[j++] = ' ';
		*j_p = j; return i + 2;
	}
	if (line[i] == '>' && line[i+1] == '>') {
		out[j++] = ' '; out[j++] = '>'; out[j++] = '>'; out[j++] = ' ';
		*j_p = j; return i + 2;
	}
	if (line[i] == ';' || line[i] == '|' || line[i] == '<' ||
	    line[i] == '>' || line[i] == '&') {
		out[j++] = ' '; out[j++] = line[i]; out[j++] = ' ';
		*j_p = j; return i + 1;
	}
	return 0;
}

char *
filson_normalize_script_ops(const char *line)
{
	assert(line != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, j, len, in_single, in_double, brace_depth, new_i;
	char *out;

	if (line == NULL) return NULL;
	len = strlen(line);
	out = malloc((len * 4) + 1);
	if (out == NULL) return NULL;
	in_single = 0;
	in_double = 0;
	brace_depth = 0;
	i = 0;
	j = 0;
	while (line[i] != '\0') {
		if (!in_single && line[i] == '"') {
			in_double = !in_double;
			out[j++] = line[i++];
			continue;
		}
		if (!in_double && line[i] == '\'') {
			in_single = !in_single;
			out[j++] = line[i++];
			continue;
		}
		if (!in_single && !in_double) {
			if (line[i] == '{') brace_depth++;
			else if (line[i] == '}' && brace_depth > 0) brace_depth--;
			else if (line[i] == '#' && brace_depth == 0 &&
			    (i == 0 || line[i-1] == ' ' || line[i-1] == '\t' || line[i-1] == ';'))
				break;
			new_i = filson_nso_op(line, i, out, &j);
			if (new_i > 0) { i = new_i; continue; }
		}
		out[j++] = line[i++];
	}
	out[j] = '\0';
	return out;
}

static char *
filson_join_tokens(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int total, pos, len;
	char *out;

	total = 0;
	for (int i = start; i < end; i++) {
		total += strlen(tokens[i]) + 1;
	}
	out = malloc(total + 1);
	if (out == NULL) {
		return NULL;
	}
	pos = 0;
	for (int i = start; i < end; i++) {
		len = strlen(tokens[i]);
		memcpy(out + pos, tokens[i], len);
		pos += len;
		if (i + 1 < end) {
			out[pos++] = ' ';
		}
	}
	out[pos] = '\0';
	return out;
}

static void
filson_ep_child_setup(char ***argvv, int i, int stage_count, int pipe_count,
    int pipes[][2], char *infiles[], char *outfiles[], int out_append[],
    char *errfiles[], int err_append[], int err_to_out[])
{
	assert(argvv != NULL);
	assert(infiles != NULL);
	int fd;

	if (i > 0) (void)dup2(pipes[i - 1][0], 0);
	if (i < stage_count - 1) (void)dup2(pipes[i][1], 1);
	if (infiles[i] != NULL) {
		fd = open(infiles[i], O_RDONLY);
		if (fd < 0) { perror("filson"); _exit(EXIT_FAILURE); }
		(void)dup2(fd, 0); (void)close(fd);
	}
	if (outfiles[i] != NULL) {
		fd = open(outfiles[i], O_WRONLY | O_CREAT |
		    (out_append[i] ? O_APPEND : O_TRUNC), 0644);
		if (fd < 0) { perror("filson"); _exit(EXIT_FAILURE); }
		(void)dup2(fd, 1); (void)close(fd);
	}
	if (err_to_out[i]) {
		(void)dup2(1, 2);
	} else if (errfiles[i] != NULL) {
		fd = open(errfiles[i], O_WRONLY | O_CREAT |
		    (err_append[i] ? O_APPEND : O_TRUNC), 0644);
		if (fd < 0) { perror("filson"); _exit(EXIT_FAILURE); }
		(void)dup2(fd, 2); (void)close(fd);
	}
	{
		int j;
		for (j = 0; j < pipe_count; j++) {
			(void)close(pipes[j][0]);
			(void)close(pipes[j][1]);
		}
	}
	execvp(argvv[i][0], argvv[i]);
	warn("%s", argvv[i][0]);
	_exit(EXIT_FAILURE);
}

static int
filson_execute_pipeline(char ***argvv, char *infiles[], char *outfiles[], int out_append[], char *errfiles[], int err_append[], int err_to_out[], int stage_count, int background, const char *segment)
{
	assert(argvv != NULL);
	assert(infiles != NULL);
	int status, pipe_count;
	int pipes[64][2];
	pid_t pids[64];

	if (stage_count <= 0 || stage_count > 64) {
		filson_last_cmd_success = 0;
		return 1;
	}
	pipe_count = stage_count - 1;
	for (int i = 0; i < pipe_count; i++) {
		if (pipe(pipes[i]) < 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			return 1;
		}
	}
	for (int i = 0; i < stage_count; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			filson_ep_child_setup(argvv, i, stage_count, pipe_count, pipes,
			    infiles, outfiles, out_append, errfiles, err_append, err_to_out);
		} else if (pids[i] < 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			for (int j = 0; j < pipe_count; j++) { (void)close(pipes[j][0]); (void)close(pipes[j][1]); }
			return 1;
		}
	}
	for (int i = 0; i < pipe_count; i++) { (void)close(pipes[i][0]); (void)close(pipes[i][1]); }
	if (background) {
		int job_id = filson_add_job(pids[stage_count - 1], segment, 0);
		if (job_id < 0) {
			fprintf(stderr, "filson: too many background jobs\n");
			filson_last_cmd_success = 0;
		} else {
			printf("[%d] %d\n", job_id, pids[stage_count - 1]);
			filson_last_cmd_success = 1;
		}
		return 1;
	}
	status = 0;
	for (int i = 0; i < stage_count; i++) {
		waitpid(pids[i], &status, 0);
		if (i == stage_count - 1) {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				filson_last_cmd_success = 1;
			else
				filson_last_cmd_success = 0;
		}
	}
	return 1;
}

static int filson_execute_parsed_segment(char **tokens, int start, int end);

static int
filson_find_matching_done(char **tokens, int loop_pos, int *done_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, depth;

	depth = 1;
	i = loop_pos + 1;
	while (tokens[i] != NULL && depth > 0) {
		if (strcmp(tokens[i], "for") == 0 || strcmp(tokens[i], "while") == 0 || strcmp(tokens[i], "until") == 0 || strcmp(tokens[i], "until") == 0) {
			depth++;
		} else if (strcmp(tokens[i], "done") == 0) {
			depth--;
		}
		i++;
	}
	if (depth == 0) {
		*done_pos = i - 1;
		return 1;
	}
	return 0;
}

static int
filson_find_loop_do(char **tokens, int start, int end, int *do_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, depth;

	depth = 0;
	i = start + 1;
	while (i < end && tokens[i] != NULL) {
		if (strcmp(tokens[i], "for") == 0 || strcmp(tokens[i], "while") == 0 || strcmp(tokens[i], "until") == 0 || strcmp(tokens[i], "until") == 0) {
			depth++;
		} else if (strcmp(tokens[i], "done") == 0) {
			depth--;
		} else if (strcmp(tokens[i], "do") == 0 && depth == 0) {
			*do_pos = i;
			return 1;
		}
		i++;
	}
	return 0;
}

static char **
filson_efl_collect_items(char **tokens, int item_start, int do_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	char **items;
	int item_count;

	items = malloc(sizeof(char *) * 256);
	if (items == NULL) return NULL;
	item_count = 0;
	for (int i = item_start; i < do_pos; i++) {
		if (tokens[i] == NULL || strcmp(tokens[i], ";") == 0) continue;
		if (item_count >= 255) break;
		items[item_count] = strdup(tokens[i]);
		if (items[item_count] == NULL) {
			while (item_count > 0) free(items[--item_count]);
			free(items);
			return NULL;
		}
		item_count++;
	}
	items[item_count] = NULL;
	return items;
}

static int
filson_execute_for_loop(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int do_pos, body_start, body_end;
	int var_pos, in_pos, item_start;
	char *var_name;
	int status;
	char item_buf[256];
	char **items, **expanded_items;
	extern int filson_break_flag;
	extern int filson_continue_flag;

	if (!filson_find_loop_do(tokens, start, end, &do_pos)) {
		fprintf(stderr, "filson: syntax error: missing 'do' in for loop\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	var_pos = start + 1;
	if (var_pos >= do_pos || tokens[var_pos] == NULL) {
		fprintf(stderr, "filson: syntax error: missing variable in for loop\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	var_name = tokens[var_pos];
	in_pos = var_pos + 1;
	if (in_pos >= do_pos || tokens[in_pos] == NULL || strcmp(tokens[in_pos], "in") != 0) {
		fprintf(stderr, "filson: syntax error: missing 'in' in for loop\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	item_start = in_pos + 1;
	body_start = do_pos + 1;
	body_end = end - 1;
	while (body_end > body_start && strcmp(tokens[body_end - 1], ";") == 0) body_end--;
	items = filson_efl_collect_items(tokens, item_start, do_pos);
	if (items == NULL) { filson_last_cmd_success = 0; return 1; }
	expanded_items = filson_expand_globs(items);
	if (expanded_items != items) free(items);
	status = 1;
	for (int i = 0; expanded_items[i] != NULL; i++) {
		snprintf(item_buf, sizeof(item_buf), "%s", expanded_items[i]);
		setenv(var_name, item_buf, 1);
		filson_break_flag = 0;
		filson_continue_flag = 0;
		status = filson_execute_parsed_segment(tokens, body_start, body_end);
		if (filson_break_flag) { filson_break_flag = 0; break; }
		if (filson_continue_flag) { filson_continue_flag = 0; continue; }
		if (status == 0) break;
	}
	filson_free_expanded_args(expanded_items);
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_execute_while_loop(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int do_pos, cond_start, cond_end;
	int body_start, body_end;
	int status;
	extern int filson_break_flag;
	extern int filson_continue_flag;

	if (!filson_find_loop_do(tokens, start, end, &do_pos)) {
		fprintf(stderr, "filson: syntax error: missing 'do' in while loop\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	cond_start = start + 1;
	cond_end = do_pos;
	while (cond_end > cond_start && strcmp(tokens[cond_end - 1], ";") == 0) {
		cond_end--;
	}
	if (cond_start >= cond_end) {
		fprintf(stderr, "filson: syntax error: empty condition in while loop\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	body_start = do_pos + 1;
	body_end = end - 1;
	while (body_end > body_start && strcmp(tokens[body_end - 1], ";") == 0) {
		body_end--;
	}
	status = 1;
	{ int _iter = 0; while (_iter++ < FILSON_LOOP_MAX_ITER) {
		filson_execute_parsed_segment(tokens, cond_start, cond_end);
		if (!filson_last_cmd_success) {
			break;
		}
		filson_break_flag = 0;
		filson_continue_flag = 0;
		status = filson_execute_parsed_segment(tokens, body_start, body_end);
		if (filson_break_flag) {
			filson_break_flag = 0;
			break;
		}
		if (filson_continue_flag) {
			filson_continue_flag = 0;
			continue;
		}
		if (status == 0) {
			break;
		}
	} }
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_execute_until_loop(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int do_pos, cond_start, cond_end;
	int body_start, body_end;
	int status;
	extern int filson_break_flag;
	extern int filson_continue_flag;

	if (!filson_find_loop_do(tokens, start, end, &do_pos)) {
		fprintf(stderr, "filson: syntax error: missing 'do' in until loop\n");
		filson_last_cmd_success = 0;
		return 0;
	}
	cond_start = start + 1;
	cond_end = do_pos;
	body_start = do_pos + 1;
	body_end = end;

	{ int _iter = 0; while (_iter++ < FILSON_LOOP_MAX_ITER) {
		status = filson_execute_parsed_segment(tokens, cond_start, cond_end);
		if (filson_last_cmd_success) {
			break;
		}
		filson_break_flag = 0;
		filson_continue_flag = 0;
		status = filson_execute_parsed_segment(tokens, body_start, body_end);
		if (filson_break_flag) {
			filson_break_flag = 0;
			break;
		}
		if (filson_continue_flag) {
			filson_continue_flag = 0;
			continue;
		}
		if (status == 0) {
			break;
		}
	} }
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_string_matches_pattern(char *str, char *pattern)
{
	assert(str != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	return fnmatch(pattern, str, 0) == 0;
}

static int
filson_find_matching_esac(char **tokens, int case_pos, int *esac_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, depth;

	depth = 1;
	i = case_pos + 1;
	while (tokens[i] != NULL && depth > 0) {
		if (strcmp(tokens[i], "case") == 0) {
			depth++;
		} else if (strcmp(tokens[i], "esac") == 0) {
			depth--;
		}
		i++;
	}
	if (depth == 0) {
		*esac_pos = i - 1;
		return 1;
	}
	return 0;
}

static int
filson_execute_case_stmt(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, matched;
	char *case_var, *pattern;
	char *var_value;

	if (start + 1 >= end) {
		fprintf(stderr, "filson: syntax error: missing case expression\n");
		filson_last_cmd_success = 0;
		return 0;
	}
	case_var = tokens[start + 1];
	var_value = getenv(case_var);
	if (var_value == NULL) {
		var_value = "";
	}

	matched = 0;
	i = start + 2;
	while (i < end && tokens[i] != NULL) {
		if (strcmp(tokens[i], ";;") == 0) {
			i++;
			continue;
		}
		pattern = tokens[i];
		i++;
		while (i < end && tokens[i] != NULL && strcmp(tokens[i], ";;") != 0) {
			if (filson_string_matches_pattern(var_value, pattern)) {
				matched = 1;
			}
			if (matched) {
				filson_execute_parsed_segment(tokens, i, i + 1);
			}
			i++;
		}
		if (tokens[i] != NULL && strcmp(tokens[i], ";;") == 0) {
			matched = 0;
			i++;
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_find_matching_fi(char **tokens, int if_pos, int *fi_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int i, depth;

	depth = 1;
	i = if_pos + 1;
	while (tokens[i] != NULL && depth > 0) {
		if (strcmp(tokens[i], "if") == 0) {
			depth++;
		} else if (strcmp(tokens[i], "fi") == 0) {
			depth--;
		}
		i++;
	}
	if (depth == 0) {
		*fi_pos = i - 1;
		return 1;
	}
	return 0;
}

static int
filson_find_then_else(char **tokens, int start, int end, int *then_pos, int *else_pos, int *elif_pos)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	*then_pos = -1;
	*else_pos = -1;
	*elif_pos = -1;
	int depth = 1;
	for (int i = start + 1; i < end; i++) {
		if (strcmp(tokens[i], "if") == 0) {
			depth++;
		} else if (strcmp(tokens[i], "fi") == 0) {
			depth--;
		} else if (depth == 1 && strcmp(tokens[i], "then") == 0) {
			*then_pos = i;
		} else if (depth == 1 && strcmp(tokens[i], "else") == 0) {
			*else_pos = i;
		} else if (depth == 1 && strcmp(tokens[i], "elif") == 0) {
			*elif_pos = i;
		}
	}
	return *then_pos != -1;
}

static int
filson_execute_if_block(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int then_pos, else_pos, elif_pos;
	int cond_start, cond_end;
	int then_start, then_end;
	int else_start, else_end;

	if (!filson_find_then_else(tokens, start, end, &then_pos, &else_pos, &elif_pos)) {
		fprintf(stderr, "filson: syntax error: missing 'then' in if statement\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	cond_start = start + 1;
	cond_end = then_pos;
	while (cond_end > cond_start && strcmp(tokens[cond_end - 1], ";") == 0) {
		cond_end--;
	}
	then_start = then_pos + 1;
	if (elif_pos != -1) {
		then_end = elif_pos;
	} else if (else_pos != -1) {
		then_end = else_pos;
	} else {
		then_end = end - 1;
	}
	while (then_end > then_start && strcmp(tokens[then_end - 1], ";") == 0) {
		then_end--;
	}
	if (cond_start >= cond_end) {
		fprintf(stderr, "filson: syntax error: empty condition in if statement\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_execute_parsed_segment(tokens, cond_start, cond_end);
	if (filson_last_cmd_success) {
		return filson_execute_parsed_segment(tokens, then_start, then_end);
	} else if (elif_pos != -1) {
		return filson_execute_if_block(tokens, elif_pos, end);
	} else if (else_pos != -1) {
		else_start = else_pos + 1;
		else_end = end - 1;
		while (else_end > else_start && strcmp(tokens[else_end - 1], ";") == 0) {
			else_end--;
		}
		return filson_execute_parsed_segment(tokens, else_start, else_end);
	}
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_esr_file_redir(char **tokens, int i, int end, const char *op_name,
    char **files, int *append, int append_val)
{
	assert(tokens != NULL);
	assert(op_name != NULL);
	if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
		fprintf(stderr, "filson: syntax error near unexpected token `%s`\n", op_name);
		filson_last_cmd_success = 0;
		return -1;
	}
	files[0] = tokens[i + 1];
	append[0] = append_val;
	return i + 1;
}

static int
filson_eps_scan_redir(char **tokens, int start, int end, int *j_p,
    int pos[], char *infiles[], char *outfiles[], int out_append[],
    char *errfiles[], int err_append[], int err_to_out[],
    char *argvbuf[][256])
{
	assert(tokens != NULL);
	assert(infiles != NULL);
	*j_p = 0;
	for (int i = start; i < end; i++) {
		int ni;
		if (strcmp(tokens[i], "|") == 0) {
			if (*j_p + 1 >= 64) {
				fprintf(stderr, "filson: too many pipeline stages (max 64)\n");
				filson_last_cmd_success = 0;
				return 0;
			}
			(*j_p)++;
			continue;
		}
		if (strcmp(tokens[i], "<") == 0) {
			ni = filson_esr_file_redir(tokens, i, end, "<", &infiles[*j_p], &(int){0}, 0);
			if (ni < 0) return 0;
			i = ni; continue;
		}
		if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], "1>") == 0) {
			ni = filson_esr_file_redir(tokens, i, end, ">", &outfiles[*j_p], &out_append[*j_p], 0);
			if (ni < 0) return 0;
			i = ni; continue;
		}
		if (strcmp(tokens[i], ">>") == 0 || strcmp(tokens[i], "1>>") == 0) {
			ni = filson_esr_file_redir(tokens, i, end, ">>", &outfiles[*j_p], &out_append[*j_p], 1);
			if (ni < 0) return 0;
			i = ni; continue;
		}
		if (strcmp(tokens[i], "2>") == 0) {
			ni = filson_esr_file_redir(tokens, i, end, "2>", &errfiles[*j_p], &err_append[*j_p], 0);
			if (ni < 0) return 0;
			err_to_out[*j_p] = 0; i = ni; continue;
		}
		if (strcmp(tokens[i], "2>>") == 0) {
			ni = filson_esr_file_redir(tokens, i, end, "2>>", &errfiles[*j_p], &err_append[*j_p], 1);
			if (ni < 0) return 0;
			err_to_out[*j_p] = 0; i = ni; continue;
		}
		if (strcmp(tokens[i], "2>&1") == 0) {
			err_to_out[*j_p] = 1; errfiles[*j_p] = NULL; err_append[*j_p] = 0; continue;
		}
		if (pos[*j_p] >= 255) {
			fprintf(stderr, "filson: too many arguments\n");
			filson_last_cmd_success = 0;
			return 0;
		}
		argvbuf[*j_p][pos[*j_p]++] = tokens[i];
	}
	return 1;
}

static int
filson_eps_single_redir(char **argvv[], int pos[],
    char *infiles[], char *outfiles[], int out_append[],
    char *errfiles[], int err_append[], int err_to_out[],
    char *segment)
{
	assert(argvv != NULL);
	assert(infiles != NULL);
	int saved_in, saved_out, saved_err, k;
	int fd_in, fd_out, fd_err, redir_ok;

	saved_in = -1; saved_out = -1; saved_err = -1;
	fd_in = -1; fd_out = -1; fd_err = -1;
	redir_ok = 1;
	if (infiles[0] != NULL) {
		saved_in = dup(STDIN_FILENO);
		fd_in = open(infiles[0], O_RDONLY);
		if (fd_in < 0) { warn("%s", infiles[0]); redir_ok = 0; }
		else { (void)dup2(fd_in, STDIN_FILENO); (void)close(fd_in); }
	}
	if (redir_ok && outfiles[0] != NULL) {
		saved_out = dup(STDOUT_FILENO);
		fd_out = open(outfiles[0], O_WRONLY | O_CREAT |
		    (out_append[0] ? O_APPEND : O_TRUNC), 0644);
		if (fd_out < 0) { warn("%s", outfiles[0]); redir_ok = 0; }
		else { (void)dup2(fd_out, STDOUT_FILENO); (void)close(fd_out); }
	}
	if (redir_ok && err_to_out[0]) {
		saved_err = dup(STDERR_FILENO);
		(void)dup2(STDOUT_FILENO, STDERR_FILENO);
	} else if (redir_ok && errfiles[0] != NULL) {
		saved_err = dup(STDERR_FILENO);
		fd_err = open(errfiles[0], O_WRONLY | O_CREAT |
		    (err_append[0] ? O_APPEND : O_TRUNC), 0644);
		if (fd_err < 0) { warn("%s", errfiles[0]); redir_ok = 0; }
		else { (void)dup2(fd_err, STDERR_FILENO); (void)close(fd_err); }
	}
	if (redir_ok)
		k = filson_execute(argvv[0], pos[0], 0, segment);
	else { filson_last_cmd_success = 0; k = 1; }
	if (saved_in >= 0) { (void)dup2(saved_in, STDIN_FILENO); (void)close(saved_in); }
	if (saved_out >= 0) { (void)dup2(saved_out, STDOUT_FILENO); (void)close(saved_out); }
	if (saved_err >= 0) { (void)dup2(saved_err, STDERR_FILENO); (void)close(saved_err); }
	return k;
}

static int
filson_eps_count_stages(char **tokens, int start, int end,
    int *stage_count_p, int *has_redir_p)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	*stage_count_p = 1;
	*has_redir_p = 0;
	for (int i = start; i < end; i++) {
		if (strcmp(tokens[i], "&") == 0) {
			fprintf(stderr, "filson: syntax error near unexpected token `&'\n");
			filson_last_cmd_success = 0;
			return 0;
		}
		if (strcmp(tokens[i], "|") == 0) {
			(*stage_count_p)++;
		} else if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 ||
		    strcmp(tokens[i], "1>") == 0 || strcmp(tokens[i], ">>") == 0 ||
		    strcmp(tokens[i], "1>>") == 0 || strcmp(tokens[i], "2>") == 0 ||
		    strcmp(tokens[i], "2>>") == 0 || strcmp(tokens[i], "2>&1") == 0) {
			*has_redir_p = 1;
		}
	}
	if (*stage_count_p > 64) {
		fprintf(stderr, "filson: too many pipeline stages (max 64)\n");
		filson_last_cmd_success = 0;
		return 0;
	}
	return 1;
}

static int
filson_eps_fastpath(char **tokens, int start, int end, int background)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	char *segment;
	char *saved_end;
	int k, argc;

	segment = filson_join_tokens(tokens, start, end);
	if (segment == NULL) {
		fprintf(stderr, "filson: allocation error\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (start < end && (strcmp(tokens[start], "if") == 0 ||
	    strcmp(tokens[start], "for") == 0 ||
	    strcmp(tokens[start], "while") == 0)) {
		k = filson_execute_and_chain(segment);
		free(segment);
		return k;
	}
	for (int i = start; i < end; i++) {
		if (strcmp(tokens[i], ";") == 0) {
			k = filson_execute_and_chain(segment);
			free(segment);
			return k;
		}
	}
	saved_end = tokens[end];
	tokens[end] = NULL;
	argc = end - start;
	k = filson_execute(tokens + start, argc, background, segment);
	tokens[end] = saved_end;
	free(segment);
	return k;
}

static int
filson_execute_parsed_segment(char **tokens, int start, int end)
{
	assert(tokens != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	int j, k;
	int background, stage_count, has_redir;
	int pos[64];
	char *infiles[64], *outfiles[64], *errfiles[64];
	int out_append[64], err_append[64], err_to_out[64];
	char *argvbuf[64][256];
	char **argvv[64];
	char *segment;

	if (start >= end) { filson_last_cmd_success = 1; return 1; }
	background = 0;
	if (strcmp(tokens[end - 1], "&") == 0) {
		background = 1;
		end--;
		if (start >= end) {
			fprintf(stderr, "filson: syntax error near unexpected token `&'\n");
			filson_last_cmd_success = 0;
			return 1;
		}
	}
	if (!filson_eps_count_stages(tokens, start, end, &stage_count, &has_redir))
		return 1;
	if (stage_count == 1 && !has_redir)
		return filson_eps_fastpath(tokens, start, end, background);
	for (int i = 0; i < 64; i++) {
		pos[i] = 0; infiles[i] = NULL; outfiles[i] = NULL; errfiles[i] = NULL;
		out_append[i] = 0; err_append[i] = 0; err_to_out[i] = 0;
	}
	if (!filson_eps_scan_redir(tokens, start, end, &j,
	    pos, infiles, outfiles, out_append, errfiles, err_append, err_to_out,
	    argvbuf))
		return 1;
	for (int i = 0; i < stage_count; i++) {
		if (pos[i] == 0) {
			fprintf(stderr, "filson: syntax error near unexpected token `|'\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		argvbuf[i][pos[i]] = NULL;
		argvv[i] = argvbuf[i];
	}
	segment = filson_join_tokens(tokens, start, end);
	if (segment == NULL) {
		fprintf(stderr, "filson: allocation error\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (stage_count == 1 && !background) {
		k = filson_eps_single_redir(argvv, pos, infiles, outfiles, out_append,
		    errfiles, err_append, err_to_out, segment);
		free(segment);
		return k;
	}
	k = filson_execute_pipeline(argvv, infiles, outfiles, out_append, errfiles,
	    err_append, err_to_out, stage_count, background, segment);
	free(segment);
	return k;
}

static inline int
filson_iskw(const char *s, int i, const char *k, int n)
{
	assert(s != NULL);
	assert(k != NULL);
	return strncmp(s + i, k, n) == 0 &&
	    (s[i + n] == ' ' || s[i + n] == ';' || s[i + n] == '\0');
}

static int
prenorm_scan_kw(const char *s, int start, const char *kw)
{
	int i, in_s, in_d, level, kw_len;

	i = start;
	in_s = 0;
	in_d = 0;
	level = 1;
	kw_len = strlen(kw);
	while (s[i] != '\0') {
		if (!in_d && s[i] == '\'') { in_s ^= 1; i++; continue; }
		if (!in_s && s[i] == '"')  { in_d ^= 1; i++; continue; }
		if (in_s || in_d) { i++; continue; }
		if (s[i] == '\\' && s[i+1] != '\0') { i += 2; continue; }
		if (s[i] == '$' && s[i+1] == '(') {
			int d = 1; i += 2;
			while (s[i] != '\0' && d > 0) {
				if (s[i] == '(') d++;
				else if (s[i] == ')') d--;
				i++;
			}
			continue;
		}
		if (s[i] == '$' && s[i+1] == '{') {
			int d = 1; i += 2;
			while (s[i] != '\0' && d > 0) {
				if (s[i] == '{') d++;
				else if (s[i] == '}') d--;
				i++;
			}
			continue;
		}
		if (i > 0 && s[i-1] != ' ' && s[i-1] != ';' && s[i-1] != '\t') {
			i++;
			continue;
		}
		if (strncmp(s+i, kw, kw_len) == 0) {
			char a = s[i+kw_len];
			if ((a == ' ' || a == ';' || a == '\0') && level == 1)
				return i;
		}
		if (filson_iskw(s, i, "if", 2) || filson_iskw(s, i, "for", 3) ||
		    filson_iskw(s, i, "while", 5) || filson_iskw(s, i, "until", 5))
			level++;
		if (filson_iskw(s, i, "fi", 2) || filson_iskw(s, i, "done", 4)) {
			level--;
			if (level == 0)
				return -1;
		}
		i++;
	}
	return -1;
}

static char *
filson_eac_prenorm(const char *line)
{
	int plen, pi, i, in_single, in_double;
	char *prenorm;

	in_single = 0;
	in_double = 0;
	plen = strlen(line);
	prenorm = malloc(plen * 3 + 1);
	if (prenorm == NULL)
		return NULL;
	pi = 0;
	for (i = 0; line[i] != '\0'; i++) {
		if (!in_double && line[i] == '\'') {
			in_single = !in_single;
			prenorm[pi++] = line[i];
			continue;
		}
		if (!in_single && line[i] == '"') {
			in_double = !in_double;
			prenorm[pi++] = line[i];
			continue;
		}
		if (!in_single && !in_double && line[i] == '#' &&
		    (i == 0 || line[i-1] == '\n' || line[i-1] == ';' ||
		    line[i-1] == ' ' || line[i-1] == '\t')) {
			while (line[i] != '\0' && line[i] != '\n')
				i++;
			if (line[i] == '\0') break;
			i--;
			continue;
		}
		if (!in_single && !in_double && line[i] == '\n') {
			prenorm[pi++] = ' ';
			prenorm[pi++] = ';';
			prenorm[pi++] = ' ';
			continue;
		}
		prenorm[pi++] = line[i];
	}
	prenorm[pi] = '\0';
	return prenorm;
}

static void
filson_eac_advance(char **args, int *i_p, int *should_run_p)
{
	if (args[*i_p] == NULL)
		return;
	if (strcmp(args[*i_p], ";") == 0) {
		*should_run_p = 1;
		(*i_p)++;
	} else if (strcmp(args[*i_p], "&&") == 0) {
		*should_run_p = filson_last_cmd_success;
		(*i_p)++;
	} else if (strcmp(args[*i_p], "||") == 0) {
		*should_run_p = !filson_last_cmd_success;
		(*i_p)++;
	}
}

static int
filson_eac_funcdef(char **args, int i, int *new_i_p, int should_run)
{
	char func_name[256];
	int depth, brace_end, k, name_len, alen, body_len;
	char *body;

	alen = args[i] ? (int)strlen(args[i]) : 0;
	if (alen < 3 || args[i][alen-2] != '(' || args[i][alen-1] != ')')
		return 0;
	if (args[i+1] == NULL || strcmp(args[i+1], "{") != 0)
		return 0;
	name_len = alen - 2;
	if (name_len >= (int)sizeof(func_name))
		name_len = (int)sizeof(func_name) - 1;
	memcpy(func_name, args[i], name_len);
	func_name[name_len] = '\0';
	depth = 0;
	brace_end = -1;
	for (k = i + 1; args[k] != NULL; k++) {
		if (strcmp(args[k], "{") == 0) depth++;
		else if (strcmp(args[k], "}") == 0) {
			depth--;
			if (depth == 0) { brace_end = k; break; }
		}
	}
	if (brace_end <= 0)
		return 0;
	if (should_run) {
		body_len = 0;
		for (k = i + 2; k < brace_end; k++)
			body_len += strlen(args[k]) + 1;
		body = malloc(body_len + 1);
		if (body != NULL) {
			int bp = 0;
			for (k = i + 2; k < brace_end; k++) {
				int slen = strlen(args[k]);
				memcpy(body + bp, args[k], slen);
				bp += slen;
				body[bp++] = ' ';
			}
			if (bp > 0) body[bp-1] = '\0';
			else body[0] = '\0';
			filson_define_function(func_name, body);
			free(body);
		}
	}
	*new_i_p = brace_end + 1;
	if (args[*new_i_p] != NULL && strcmp(args[*new_i_p], ";") == 0)
		(*new_i_p)++;
	return 1;
}

static void
filson_eii_branch(char *prenorm, int te, int ep, int fp)
{
	char *tmp_s;
	int be, bs;
	extern int filson_function_return_requested;
	extern int filson_exit_called;

	if (filson_last_cmd_success) {
		be = (ep >= 0 && ep < fp) ? ep : fp;
		if (te < be) {
			tmp_s = strndup(prenorm + te, be - te);
			if (tmp_s != NULL) { filson_execute_and_chain(tmp_s); free(tmp_s); }
		}
	} else if (ep >= 0) {
		bs = ep + 4;
		while (prenorm[bs] == ' ' || prenorm[bs] == ';' || prenorm[bs] == '\t')
			bs++;
		if (bs < fp) {
			tmp_s = strndup(prenorm + bs, fp - bs);
			if (tmp_s != NULL) { filson_execute_and_chain(tmp_s); free(tmp_s); }
		}
	} else {
		filson_last_cmd_success = 1;
	}
	if (!filson_function_return_requested && !filson_exit_called) {
		int afi = fp + 2;
		while (prenorm[afi] == ' ' || prenorm[afi] == ';' || prenorm[afi] == '\t')
			afi++;
		if (prenorm[afi] != '\0')
			filson_execute_and_chain(prenorm + afi);
	}
}

static int
filson_eac_inline_if(char *prenorm)
{
	int ip, cs, tp, ep, fp, te;
	char *tmp_s;
	extern int filson_function_return_requested;
	extern int filson_exit_called;

	ip = 0;
	while (prenorm[ip] == ' ' || prenorm[ip] == '\t' || prenorm[ip] == ';')
		ip++;
	if (!(strncmp(prenorm + ip, "if", 2) == 0 &&
	    (prenorm[ip+2] == ' ' || prenorm[ip+2] == ';' || prenorm[ip+2] == '\0')))
		return 0;
	cs = ip + 2;
	while (prenorm[cs] == ' ' || prenorm[cs] == '\t' || prenorm[cs] == ';')
		cs++;
	tp = prenorm_scan_kw(prenorm, cs, "then");
	if (tp < 0) return 0;
	if (ip > 0) {
		tmp_s = strndup(prenorm, ip);
		if (tmp_s != NULL) { filson_execute_and_chain(tmp_s); free(tmp_s); }
		if (filson_function_return_requested || filson_exit_called) return 1;
	}
	tmp_s = strndup(prenorm + cs, tp - cs);
	if (tmp_s != NULL) { filson_execute_and_chain(tmp_s); free(tmp_s); }
	if (filson_function_return_requested || filson_exit_called) return 1;
	te = tp + 4;
	while (prenorm[te] == ' ' || prenorm[te] == ';' || prenorm[te] == '\t') te++;
	ep = prenorm_scan_kw(prenorm, te, "else");
	fp = prenorm_scan_kw(prenorm, te, "fi");
	if (fp < 0) {
		fprintf(stderr, "filson: syntax error: missing 'fi' for 'if'\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_eii_branch(prenorm, te, ep, fp);
	return 1;
}

static int
filson_eac_dispatch(char **args, int *i_p, int *should_run_p, int *status_p)
{
	int fi_pos, done_pos, esac_pos, new_i;
	const char *kw = args[*i_p];

	if (strcmp(kw, "if") == 0 && *should_run_p) {
		if (!filson_find_matching_fi(args, *i_p, &fi_pos)) {
			fprintf(stderr, "filson: syntax error: missing 'fi' for 'if'\n");
			filson_last_cmd_success = 0; *status_p = 1; return -1;
		}
		*status_p = filson_execute_if_block(args, *i_p, fi_pos + 1);
		if (*status_p == 0) return -1;
		*i_p = fi_pos + 1; *should_run_p = 1;
		filson_eac_advance(args, i_p, should_run_p);
		return 1;
	}
	if ((strcmp(kw, "for") == 0 || strcmp(kw, "while") == 0 ||
	    strcmp(kw, "until") == 0) && *should_run_p) {
		if (!filson_find_matching_done(args, *i_p, &done_pos)) {
			fprintf(stderr, "filson: syntax error: missing 'done' for '%s'\n", kw);
			filson_last_cmd_success = 0; *status_p = 1; return -1;
		}
		if (strcmp(kw, "for") == 0)
			*status_p = filson_execute_for_loop(args, *i_p, done_pos + 1);
		else if (strcmp(kw, "while") == 0)
			*status_p = filson_execute_while_loop(args, *i_p, done_pos + 1);
		else
			*status_p = filson_execute_until_loop(args, *i_p, done_pos + 1);
		if (*status_p == 0) return -1;
		*i_p = done_pos + 1; *should_run_p = 1;
		filson_eac_advance(args, i_p, should_run_p);
		return 1;
	}
	if (strcmp(kw, "case") == 0 && *should_run_p) {
		if (!filson_find_matching_esac(args, *i_p, &esac_pos)) {
			fprintf(stderr, "filson: syntax error: missing 'esac' for 'case'\n");
			filson_last_cmd_success = 0; *status_p = 1; return -1;
		}
		*status_p = filson_execute_case_stmt(args, *i_p, esac_pos + 1);
		if (*status_p == 0) return -1;
		*i_p = esac_pos + 1; *should_run_p = 1;
		filson_eac_advance(args, i_p, should_run_p);
		return 1;
	}
	new_i = *i_p;
	if (filson_eac_funcdef(args, *i_p, &new_i, *should_run_p)) {
		*i_p = new_i; *should_run_p = 1; return 1;
	}
	return 0;
}

static int
filson_eac_segment(char **args, int *i_p, int *should_run_p)
{
	int i = *i_p, j;

	j = i;
	while (args[j] != NULL && strcmp(args[j], ";") != 0 &&
	    strcmp(args[j], "&&") != 0 && strcmp(args[j], "||") != 0 &&
	    strcmp(args[j], "if") != 0 && strcmp(args[j], "for") != 0 &&
	    strcmp(args[j], "while") != 0 && strcmp(args[j], "until") != 0 &&
	    strcmp(args[j], "case") != 0)
		j++;
	if (j == i) {
		if (args[i] != NULL && strcmp(args[i], ";") == 0) {
			*should_run_p = 1;
			*i_p = i + 1;
			return 1;
		}
		fprintf(stderr, "filson: syntax error\n");
		filson_last_cmd_success = 0;
		return -1;
	}
	if (*should_run_p) {
		int st = filson_execute_parsed_segment(args, i, j);
		if (st == 0) return 0;
	}
	if (args[j] == NULL) { *i_p = j; return 0; }
	if (strcmp(args[j], ";") == 0) *should_run_p = 1;
	else if (strcmp(args[j], "&&") == 0) *should_run_p = filson_last_cmd_success;
	else if (strcmp(args[j], "||") == 0) *should_run_p = !filson_last_cmd_success;
	*i_p = j + 1;
	return 1;
}

int
filson_execute_and_chain(char *line)
{
	char *expanded, *normalized, *prenorm;
	char **args;
	int i, status, should_run;
	extern int filson_exit_called;

	prenorm = filson_eac_prenorm(line);
	if (prenorm == NULL) { filson_last_cmd_success = 0; return 1; }
	if (filson_eac_inline_if(prenorm)) {
		free(prenorm);
		return filson_exit_called ? 0 : 1;
	}
	expanded = filson_expand_command_substitutions(prenorm);
	free(prenorm);
	if (expanded == NULL) {
		fprintf(stderr, "filson: command substitution error\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	normalized = filson_normalize_script_ops(expanded);
	if (normalized == NULL) {
		fprintf(stderr, "filson: allocation error\n");
		free(expanded);
		filson_last_cmd_success = 0;
		return 1;
	}
	args = filson_split_line(normalized);
	{
		int ai; char *p;
		for (ai = 0; args[ai] != NULL; ai++)
			for (p = args[ai]; *p; p++) {
				if (*p == '\x0e') *p = '\t';
				else if (*p == '\x0f') *p = '\n';
			}
	}
	status = 1;
	should_run = 1;
	i = 0;
	while (args[i] != NULL) {
		int r = filson_eac_dispatch(args, &i, &should_run, &status);
		if (r < 0) break;
		if (r > 0) continue;
		r = filson_eac_segment(args, &i, &should_run);
		if (r < 0) { status = 1; break; }
		if (r == 0) break;
	}
	free(args);
	free(normalized);
	free(expanded);
	return status;
}