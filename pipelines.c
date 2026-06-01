#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "jobcontrol.h"
#include "pipelines.h"
#include "globbing.h"
#include "shell_session.h"

extern int filson_last_cmd_success;
extern int filson_execute(char **args, int background, char *segment);
extern char *filson_get_pospar(int idx);

static char *
filson_read_command_output(FILE *fp)
{
	char chunk[256];
	char *out;
	size_t cap, len, n, i;
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
	for (i = 0; i < len; i++) {
		if (out[i] == '\n') {
			out[i] = ' ';
		}
	}
	out[len] = '\0';
	return out;
}

static char *
filson_run_subcommand(const char *cmd)
{
	FILE *fp;
	char *out;

	fp = popen(cmd, "r");
	if (fp == NULL) {
		return NULL;
	}
	out = filson_read_command_output(fp);
	pclose(fp);
	return out;
}

static int filson_evaluate_arithmetic(const char *expr);

static long
filson_parse_factor(const char *expr, int *pos)
{
	long factor;
	const char *var_value;
	char var_name[256];
	int var_len, depth, i;

	while (expr[*pos] == ' ' || expr[*pos] == '\t') {
		(*pos)++;
	}
	if (expr[*pos] == '-') {
		(*pos)++;
		return -filson_parse_factor(expr, pos);
	}
	if (expr[*pos] == '+') {
		(*pos)++;
		return filson_parse_factor(expr, pos);
	}
	if (expr[*pos] == '$') {
		(*pos)++;
		var_len = 0;
		while ((expr[*pos + var_len] >= 'a' && expr[*pos + var_len] <= 'z') ||
		       (expr[*pos + var_len] >= 'A' && expr[*pos + var_len] <= 'Z') ||
		       (expr[*pos + var_len] >= '0' && expr[*pos + var_len] <= '9') ||
		       expr[*pos + var_len] == '_') {
			var_len++;
		}
		if (var_len == 0 || var_len >= 256) {
			return 0;
		}
		memcpy(var_name, expr + *pos, var_len);
		var_name[var_len] = '\0';
		if (var_len == 1 && var_name[0] >= '0' && var_name[0] <= '9') {
			char *pval = filson_get_pospar(var_name[0] - '0');
			factor = pval != NULL ? atol(pval) : 0;
		} else {
			var_value = getenv(var_name);
			factor = var_value != NULL ? atol(var_value) : 0;
		}
		*pos += var_len;
		return factor;
	}
	if (expr[*pos] == '(') {
		(*pos)++;
		depth = 1;
		i = *pos;
		while (expr[i] != '\0' && depth > 0) {
			if (expr[i] == '(') {
				depth++;
			} else if (expr[i] == ')') {
				depth--;
			}
			i++;
		}
		if (depth != 0) {
			return 0;
		}
		var_len = i - *pos - 1;
		memcpy(var_name, expr + *pos, var_len);
		var_name[var_len] = '\0';
		factor = filson_evaluate_arithmetic(var_name);
		*pos = i;
		return factor;
	}
	if (expr[*pos] >= '0' && expr[*pos] <= '9') {
		factor = 0;
		while (expr[*pos] >= '0' && expr[*pos] <= '9') {
			factor = factor * 10 + (expr[*pos] - '0');
			(*pos)++;
		}
		return factor;
	}
	return 0;
}

static long
filson_parse_term(const char *expr, int *pos)
{
	long term;
	char op;

	term = filson_parse_factor(expr, pos);
	while (1) {
		while (expr[*pos] == ' ' || expr[*pos] == '\t') {
			(*pos)++;
		}
		op = expr[*pos];
		if (op == '*') {
			(*pos)++;
			term *= filson_parse_factor(expr, pos);
		} else if (op == '/') {
			(*pos)++;
			long divisor = filson_parse_factor(expr, pos);
			if (divisor != 0) {
				term /= divisor;
			}
		} else if (op == '%') {
			(*pos)++;
			long divisor = filson_parse_factor(expr, pos);
			if (divisor != 0) {
				term %= divisor;
			}
		} else {
			break;
		}
	}
	return term;
}

static int
filson_evaluate_arithmetic(const char *expr)
{
	long result;
	int pos;
	char op;

	if (expr == NULL || expr[0] == '\0') {
		return 0;
	}
	pos = 0;
	result = filson_parse_term(expr, &pos);
	while (1) {
		while (expr[pos] == ' ' || expr[pos] == '\t') {
			pos++;
		}
		op = expr[pos];
		if (op == '+') {
			pos++;
			result += filson_parse_term(expr, &pos);
		} else if (op == '-' && (pos == 0 || expr[pos - 1] == ' ' || expr[pos - 1] == '\t' || expr[pos - 1] == '+' || expr[pos - 1] == '-' || expr[pos - 1] == '*' || expr[pos - 1] == '/' || expr[pos - 1] == '%' || expr[pos - 1] == '(')) {
			pos++;
			result -= filson_parse_term(expr, &pos);
		} else if (op == ')' || op == '\0') {
			break;
		} else {
			break;
		}
	}
	return (int)result;
}

static char *
filson_expand_command_substitutions(const char *line)
{
	char *out;
	int i, j, k, depth, in_single, in_double;
	int len, out_cap, out_len;
	char *cmd, *cmd_out;
	char *tmp;

	if (line == NULL) {
		return NULL;
	}
	len = strlen(line);
	out_cap = (len * 2) + 1;
	out = malloc(out_cap);
	if (out == NULL) {
		return NULL;
	}
	out_len = 0;
	i = 0;
	in_single = 0;
	in_double = 0;
	while (line[i] != '\0') {
		if (!in_double && line[i] == '\'') {
			in_single = !in_single;
		}
		if (!in_single && line[i] == '"') {
			in_double = !in_double;
		}
		if (!in_single && line[i] == '$' && line[i + 1] == '(') {
			if (line[i + 2] == '(') {
				j = i + 3;
				depth = 1;
				while (line[j] != '\0' && depth > 0) {
					if (line[j] == '(') {
						depth++;
					} else if (line[j] == ')') {
						depth--;
					}
					if (depth > 0) {
						j++;
					}
				}
				if (depth != 0 || line[j] != ')') {
					free(out);
					return NULL;
				}
				cmd = malloc((j - (i + 3)) + 1);
				if (cmd == NULL) {
					free(out);
					return NULL;
				}
				for (k = 0; k < j - (i + 3); k++) {
					cmd[k] = line[i + 3 + k];
				}
				cmd[k] = '\0';
				int arith_result = filson_evaluate_arithmetic(cmd);
				free(cmd);
				char result_buf[64];
				snprintf(result_buf, sizeof(result_buf), "%d", arith_result);
				if (out_len + (int)strlen(result_buf) + 1 > out_cap) {
					while (out_len + (int)strlen(result_buf) + 1 > out_cap) {
						out_cap *= 2;
					}
					tmp = realloc(out, out_cap);
					if (tmp == NULL) {
						free(out);
						return NULL;
					}
					out = tmp;
				}
				memcpy(out + out_len, result_buf, strlen(result_buf));
				out_len += strlen(result_buf);
				i = j + 2;
				continue;
			} else {
				j = i + 2;
				depth = 1;
				while (line[j] != '\0' && depth > 0) {
					if (line[j] == '(') {
						depth++;
					} else if (line[j] == ')') {
						depth--;
					}
					if (depth > 0) {
						j++;
					}
				}
				if (depth != 0) {
					free(out);
					return NULL;
				}
				cmd = malloc((j - (i + 2)) + 1);
				if (cmd == NULL) {
					free(out);
					return NULL;
				}
				for (k = 0; k < j - (i + 2); k++) {
					cmd[k] = line[i + 2 + k];
				}
				cmd[k] = '\0';
				cmd_out = filson_run_subcommand(cmd);
				free(cmd);
				if (cmd_out == NULL) {
					free(out);
					return NULL;
				}
				if (out_len + (int)strlen(cmd_out) + 1 > out_cap) {
					while (out_len + (int)strlen(cmd_out) + 1 > out_cap) {
						out_cap *= 2;
					}
					tmp = realloc(out, out_cap);
					if (tmp == NULL) {
						free(cmd_out);
						free(out);
						return NULL;
					}
					out = tmp;
				}
				memcpy(out + out_len, cmd_out, strlen(cmd_out));
				out_len += strlen(cmd_out);
				free(cmd_out);
				i = j + 1;
				continue;
			}
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
		out[out_len++] = line[i++];
	}
	out[out_len] = '\0';
	return out;
}

char *
filson_normalize_script_ops(const char *line)
{
	int i, j, len, in_single, in_double, brace_depth;
	char *out;

	if (line == NULL) {
		return NULL;
	}
	len = strlen(line);
	out = malloc((len * 4) + 1);
	if (out == NULL) {
		return NULL;
	}
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
			if (line[i] == '{') {
				brace_depth++;
			} else if (line[i] == '}') {
				if (brace_depth > 0) {
					brace_depth--;
				}
			} else if (line[i] == '#' && brace_depth == 0 &&
			    (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t' || line[i - 1] == ';')) {
				break;
			}
			if (line[i] == '&' && line[i + 1] == '&') {
				out[j++] = ' ';
				out[j++] = '&';
				out[j++] = '&';
				out[j++] = ' ';
				i += 2;
				continue;
			}
			if (line[i] == '|' && line[i + 1] == '|') {
				out[j++] = ' ';
				out[j++] = '|';
				out[j++] = '|';
				out[j++] = ' ';
				i += 2;
				continue;
			}
			if (line[i] == '2' && line[i + 1] == '>' && line[i + 2] == '&' && line[i + 3] == '1') {
				out[j++] = ' ';
				out[j++] = '2';
				out[j++] = '>';
				out[j++] = '&';
				out[j++] = '1';
				out[j++] = ' ';
				i += 4;
				continue;
			}
			if (line[i] == '2' && line[i + 1] == '>' && line[i + 2] == '>') {
				out[j++] = ' ';
				out[j++] = '2';
				out[j++] = '>';
				out[j++] = '>';
				out[j++] = ' ';
				i += 3;
				continue;
			}
			if (line[i] == '2' && line[i + 1] == '>') {
				out[j++] = ' ';
				out[j++] = '2';
				out[j++] = '>';
				out[j++] = ' ';
				i += 2;
				continue;
			}
			if (line[i] == '1' && line[i + 1] == '>' && line[i + 2] == '>') {
				out[j++] = ' ';
				out[j++] = '1';
				out[j++] = '>';
				out[j++] = '>';
				out[j++] = ' ';
				i += 3;
				continue;
			}
			if (line[i] == '1' && line[i + 1] == '>') {
				out[j++] = ' ';
				out[j++] = '1';
				out[j++] = '>';
				out[j++] = ' ';
				i += 2;
				continue;
			}
			if (line[i] == '>' && line[i + 1] == '>') {
				out[j++] = ' ';
				out[j++] = '>';
				out[j++] = '>';
				out[j++] = ' ';
				i += 2;
				continue;
			}
			if (line[i] == ';' || line[i] == '|' || line[i] == '<' || line[i] == '>' || line[i] == '&') {
				out[j++] = ' ';
				out[j++] = line[i++];
				out[j++] = ' ';
				continue;
			}
		}
		out[j++] = line[i++];
	}
	out[j] = '\0';
	return out;
}

static char *
filson_join_tokens(char **tokens, int start, int end)
{
	int i, total, pos, len;
	char *out;

	total = 0;
	for (i = start; i < end; i++) {
		total += strlen(tokens[i]) + 1;
	}
	out = malloc(total + 1);
	if (out == NULL) {
		return NULL;
	}
	pos = 0;
	for (i = start; i < end; i++) {
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

static int
filson_execute_pipeline(char ***argvv, char *infiles[], char *outfiles[], int out_append[], char *errfiles[], int err_append[], int err_to_out[], int stage_count, int background, const char *segment)
{
	int i, j, job_id;
	int status, pipe_count;
	int pipes[64][2];
	pid_t pids[64];

	if (stage_count <= 0 || stage_count > 64) {
		filson_last_cmd_success = 0;
		return 1;
	}
	pipe_count = stage_count - 1;
	for (i = 0; i < pipe_count; i++) {
		if (pipe(pipes[i]) < 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			return 1;
		}
	}
	for (i = 0; i < stage_count; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			if (i > 0) {
				dup2(pipes[i - 1][0], 0);
			}
			if (i < stage_count - 1) {
				dup2(pipes[i][1], 1);
			}
			if (infiles[i] != NULL) {
				int fd_in;

				fd_in = open(infiles[i], O_RDONLY);
				if (fd_in < 0) {
					perror("filson");
					exit(EXIT_FAILURE);
				}
				dup2(fd_in, 0);
				close(fd_in);
			}
			if (outfiles[i] != NULL) {
				int fd_out;

				fd_out = open(outfiles[i], O_WRONLY | O_CREAT | (out_append[i] ? O_APPEND : O_TRUNC), 0644);
				if (fd_out < 0) {
					perror("filson");
					exit(EXIT_FAILURE);
				}
				dup2(fd_out, 1);
				close(fd_out);
			}
			if (err_to_out[i]) {
				dup2(1, 2);
			} else if (errfiles[i] != NULL) {
				int fd_err;

				fd_err = open(errfiles[i], O_WRONLY | O_CREAT | (err_append[i] ? O_APPEND : O_TRUNC), 0644);
				if (fd_err < 0) {
					perror("filson");
					exit(EXIT_FAILURE);
				}
				dup2(fd_err, 2);
				close(fd_err);
			}
			for (j = 0; j < pipe_count; j++) {
				close(pipes[j][0]);
				close(pipes[j][1]);
			}
			execvp(argvv[i][0], argvv[i]);
			perror("filson");
			exit(EXIT_FAILURE);
		} else if (pids[i] < 0) {
			perror("filson");
			filson_last_cmd_success = 0;
			for (j = 0; j < pipe_count; j++) {
				close(pipes[j][0]);
				close(pipes[j][1]);
			}
			return 1;
		}
	}
	for (i = 0; i < pipe_count; i++) {
		close(pipes[i][0]);
		close(pipes[i][1]);
	}
	if (background) {
		job_id = filson_add_job(pids[stage_count - 1], segment, 0);
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
	for (i = 0; i < stage_count; i++) {
		waitpid(pids[i], &status, 0);
		if (i == stage_count - 1) {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
				filson_last_cmd_success = 1;
			} else {
				filson_last_cmd_success = 0;
			}
		}
	}
	return 1;
}

static int filson_execute_parsed_segment(char **tokens, int start, int end);

static int
filson_find_matching_done(char **tokens, int loop_pos, int *done_pos)
{
	int i, depth;

	depth = 1;
	i = loop_pos + 1;
	while (tokens[i] != NULL && depth > 0) {
		if (strcmp(tokens[i], "for") == 0 || strcmp(tokens[i], "while") == 0) {
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
	int i, depth;

	depth = 0;
	i = start + 1;
	while (i < end && tokens[i] != NULL) {
		if (strcmp(tokens[i], "for") == 0 || strcmp(tokens[i], "while") == 0) {
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

static int
filson_execute_for_loop(char **tokens, int start, int end)
{
	int do_pos, body_start, body_end;
	int var_pos, in_pos, item_start;
	char *var_name;
	int i, status, item_count;
	char item_buf[256];
	char **items, **expanded_items;

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
	while (body_end > body_start && strcmp(tokens[body_end - 1], ";") == 0) {
		body_end--;
	}
	item_count = 0;
	items = malloc(sizeof(char *) * 256);
	if (items == NULL) {
		fprintf(stderr, "filson: allocation error\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	for (i = item_start; i < do_pos; i++) {
		if (tokens[i] == NULL || strcmp(tokens[i], ";") == 0) {
			continue;
		}
		if (item_count >= 255) {
			break;
		}
		items[item_count] = strdup(tokens[i]);
		if (items[item_count] == NULL) {
			while (item_count > 0) {
				free(items[--item_count]);
			}
			free(items);
			filson_last_cmd_success = 0;
			return 1;
		}
		item_count++;
	}
	items[item_count] = NULL;
	expanded_items = filson_expand_globs(items);
	status = 1;
	for (i = 0; expanded_items[i] != NULL; i++) {
		snprintf(item_buf, sizeof(item_buf), "%s", expanded_items[i]);
		setenv(var_name, item_buf, 1);
		status = filson_execute_parsed_segment(tokens, body_start, body_end);
		if (status == 0) {
			break;
		}
	}
	filson_free_expanded_args(expanded_items);
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_execute_while_loop(char **tokens, int start, int end)
{
	int do_pos, cond_start, cond_end;
	int body_start, body_end;
	int status;

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
	while (1) {
		filson_execute_parsed_segment(tokens, cond_start, cond_end);
		if (!filson_last_cmd_success) {
			break;
		}
		status = filson_execute_parsed_segment(tokens, body_start, body_end);
		if (status == 0) {
			break;
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_find_matching_fi(char **tokens, int if_pos, int *fi_pos)
{
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
	int i, depth;

	*then_pos = -1;
	*else_pos = -1;
	*elif_pos = -1;
	depth = 1;
	for (i = start + 1; i < end; i++) {
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
	} else if (else_pos != -1) {
		else_start = else_pos + 1;
		if (elif_pos != -1) {
			else_end = elif_pos;
		} else {
			else_end = end - 1;
		}
		while (else_end > else_start && strcmp(tokens[else_end - 1], ";") == 0) {
			else_end--;
		}
		return filson_execute_parsed_segment(tokens, else_start, else_end);
	} else if (elif_pos != -1) {
		return filson_execute_if_block(tokens, elif_pos, end);
	}
	filson_last_cmd_success = 1;
	return 1;
}

static int
filson_execute_parsed_segment(char **tokens, int start, int end)
{
	int i, j, k;
	int background, stage_count;
	int pos[64], has_redir;
	char *infiles[64], *outfiles[64], *errfiles[64];
	int out_append[64], err_append[64], err_to_out[64];
	char *argvbuf[64][256];
	char **argvv[64];
	char *segment;
	char *saved_end;

	if (start >= end) {
		filson_last_cmd_success = 1;
		return 1;
	}
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
	for (i = start; i < end; i++) {
		if (strcmp(tokens[i], "&") == 0) {
			fprintf(stderr, "filson: syntax error near unexpected token `&'\n");
			filson_last_cmd_success = 0;
			return 1;
		}
	}
	stage_count = 1;
	has_redir = 0;
	for (i = start; i < end; i++) {
		if (strcmp(tokens[i], "|") == 0) {
			stage_count++;
		} else if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], "1>") == 0 || strcmp(tokens[i], ">>") == 0 || strcmp(tokens[i], "1>>") == 0 || strcmp(tokens[i], "2>") == 0 || strcmp(tokens[i], "2>>") == 0 || strcmp(tokens[i], "2>&1") == 0) {
			has_redir = 1;
		}
	}
	if (stage_count > 64) {
		fprintf(stderr, "filson: too many pipeline stages (max 64)\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (stage_count == 1 && !has_redir) {
		segment = filson_join_tokens(tokens, start, end);
		if (segment == NULL) {
			fprintf(stderr, "filson: allocation error\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		saved_end = tokens[end];
		tokens[end] = NULL;
		k = filson_execute(tokens + start, background, segment);
		tokens[end] = saved_end;
		free(segment);
		return k;
	}
	for (i = 0; i < 64; i++) {
		pos[i] = 0;
		infiles[i] = NULL;
		outfiles[i] = NULL;
		errfiles[i] = NULL;
		out_append[i] = 0;
		err_append[i] = 0;
		err_to_out[i] = 0;
	}
	j = 0;
	for (i = start; i < end; i++) {
		if (strcmp(tokens[i], "|") == 0) {
			if (j + 1 >= 64) {
				fprintf(stderr, "filson: too many pipeline stages (max 64)\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			j++;
			continue;
		}
		if (strcmp(tokens[i], "<") == 0) {
			if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
				fprintf(stderr, "filson: syntax error near unexpected token `<`\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			infiles[j] = tokens[++i];
			continue;
		}
		if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], "1>") == 0) {
			if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
				fprintf(stderr, "filson: syntax error near unexpected token `>`\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			outfiles[j] = tokens[++i];
			out_append[j] = 0;
			continue;
		}
		if (strcmp(tokens[i], ">>") == 0 || strcmp(tokens[i], "1>>") == 0) {
			if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
				fprintf(stderr, "filson: syntax error near unexpected token `>>`\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			outfiles[j] = tokens[++i];
			out_append[j] = 1;
			continue;
		}
		if (strcmp(tokens[i], "2>") == 0) {
			if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
				fprintf(stderr, "filson: syntax error near unexpected token `2>`\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			errfiles[j] = tokens[++i];
			err_append[j] = 0;
			err_to_out[j] = 0;
			continue;
		}
		if (strcmp(tokens[i], "2>>") == 0) {
			if (i + 1 >= end || strcmp(tokens[i + 1], "|") == 0) {
				fprintf(stderr, "filson: syntax error near unexpected token `2>>`\n");
				filson_last_cmd_success = 0;
				return 1;
			}
			errfiles[j] = tokens[++i];
			err_append[j] = 1;
			err_to_out[j] = 0;
			continue;
		}
		if (strcmp(tokens[i], "2>&1") == 0) {
			err_to_out[j] = 1;
			errfiles[j] = NULL;
			err_append[j] = 0;
			continue;
		}
		if (pos[j] >= 255) {
			fprintf(stderr, "filson: too many arguments\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		argvbuf[j][pos[j]++] = tokens[i];
	}
	for (i = 0; i < stage_count; i++) {
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
	k = filson_execute_pipeline(argvv, infiles, outfiles, out_append, errfiles, err_append, err_to_out, stage_count, background, segment);
	free(segment);
	return k;
}

int
filson_execute_and_chain(char *line)
{
	char *expanded;
	char *normalized;
	char **args;
	int i, j, fi_pos;
	int status, should_run;

	expanded = filson_expand_command_substitutions(line);
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
	status = 1;
	should_run = 1;
	i = 0;
	while (args[i] != NULL) {
		int done_pos;
		if (args[i] != NULL && strcmp(args[i], "if") == 0 && should_run) {
			if (!filson_find_matching_fi(args, i, &fi_pos)) {
				fprintf(stderr, "filson: syntax error: missing 'fi' for 'if'\n");
				filson_last_cmd_success = 0;
				status = 1;
				break;
			}
			status = filson_execute_if_block(args, i, fi_pos + 1);
			if (status == 0) {
				break;
			}
			i = fi_pos + 1;
			should_run = 1;
			if (args[i] != NULL) {
				if (strcmp(args[i], ";") == 0) {
					should_run = 1;
					i++;
				} else if (strcmp(args[i], "&&") == 0) {
					should_run = filson_last_cmd_success;
					i++;
				} else if (strcmp(args[i], "||") == 0) {
					should_run = !filson_last_cmd_success;
					i++;
				}
			}
			continue;
		}
		if (args[i] != NULL && strcmp(args[i], "for") == 0 && should_run) {
			if (!filson_find_matching_done(args, i, &done_pos)) {
				fprintf(stderr, "filson: syntax error: missing 'done' for 'for'\n");
				filson_last_cmd_success = 0;
				status = 1;
				break;
			}
			status = filson_execute_for_loop(args, i, done_pos + 1);
			if (status == 0) {
				break;
			}
			i = done_pos + 1;
			should_run = 1;
			if (args[i] != NULL) {
				if (strcmp(args[i], ";") == 0) {
					should_run = 1;
					i++;
				} else if (strcmp(args[i], "&&") == 0) {
					should_run = filson_last_cmd_success;
					i++;
				} else if (strcmp(args[i], "||") == 0) {
					should_run = !filson_last_cmd_success;
					i++;
				}
			}
			continue;
		}
		if (args[i] != NULL && strcmp(args[i], "while") == 0 && should_run) {
			if (!filson_find_matching_done(args, i, &done_pos)) {
				fprintf(stderr, "filson: syntax error: missing 'done' for 'while'\n");
				filson_last_cmd_success = 0;
				status = 1;
				break;
			}
			status = filson_execute_while_loop(args, i, done_pos + 1);
			if (status == 0) {
				break;
			}
			i = done_pos + 1;
			should_run = 1;
			if (args[i] != NULL) {
				if (strcmp(args[i], ";") == 0) {
					should_run = 1;
					i++;
				} else if (strcmp(args[i], "&&") == 0) {
					should_run = filson_last_cmd_success;
					i++;
				} else if (strcmp(args[i], "||") == 0) {
					should_run = !filson_last_cmd_success;
					i++;
				}
			}
			continue;
		}
		j = i;
		while (args[j] != NULL && strcmp(args[j], ";") != 0 && strcmp(args[j], "&&") != 0 && strcmp(args[j], "||") != 0 && strcmp(args[j], "if") != 0 && strcmp(args[j], "for") != 0 && strcmp(args[j], "while") != 0) {
			j++;
		}
		if (j == i) {
			fprintf(stderr, "filson: syntax error\n");
			filson_last_cmd_success = 0;
			status = 1;
			break;
		}
		if (should_run) {
			status = filson_execute_parsed_segment(args, i, j);
			if (status == 0) {
				break;
			}
		}
		if (args[j] == NULL) {
			break;
		}
		if (strcmp(args[j], ";") == 0) {
			should_run = 1;
		} else if (strcmp(args[j], "&&") == 0) {
			should_run = filson_last_cmd_success;
		} else if (strcmp(args[j], "||") == 0) {
			should_run = !filson_last_cmd_success;
		}
		i = j + 1;
	}
	free(args);
	free(normalized);
	free(expanded);
	return status;
}