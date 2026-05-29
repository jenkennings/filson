#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "jobcontrol.h"
#include "pipelines.h"

extern int filson_last_cmd_success;
extern int filson_execute(char **args, int background, char *segment);
extern char **filson_split_line(char *line);

char *
filson_normalize_script_ops(const char *line)
{
	int i, j, len, in_single, in_double;
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
			if (line[i] == '#') {
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
			if (line[i] == '1' && line[i + 1] == '>') {
				out[j++] = ' ';
				out[j++] = '1';
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
filson_execute_pipeline(char ***argvv, char *infiles[], char *outfiles[], int stage_count, int background, const char *segment)
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

				fd_out = open(outfiles[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd_out < 0) {
					perror("filson");
					exit(EXIT_FAILURE);
				}
				dup2(fd_out, 1);
				close(fd_out);
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

static int
filson_execute_parsed_segment(char **tokens, int start, int end)
{
	int i, j, k;
	int background, stage_count;
	int pos[64], has_redir;
	char *infiles[64], *outfiles[64];
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
		} else if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], "1>") == 0) {
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
	k = filson_execute_pipeline(argvv, infiles, outfiles, stage_count, background, segment);
	free(segment);
	return k;
}

int
filson_execute_and_chain(char *line)
{
	char *normalized;
	char **args;
	int i, j;
	int status, should_run;

	normalized = filson_normalize_script_ops(line);
	if (normalized == NULL) {
		fprintf(stderr, "filson: allocation error\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	args = filson_split_line(normalized);
	status = 1;
	should_run = 1;
	i = 0;
	while (args[i] != NULL) {
		j = i;
		while (args[j] != NULL && strcmp(args[j], ";") != 0 && strcmp(args[j], "&&") != 0 && strcmp(args[j], "||") != 0) {
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
		} else {
			should_run = !filson_last_cmd_success;
		}
		i = j + 1;
	}
	free(args);
	free(normalized);
	return status;
}