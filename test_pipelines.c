#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "pipelines.h"

#define TEST_PASS(name) printf("[PASS] %s\n", name); tests_passed++
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); tests_failed++

int tests_passed = 0;
int tests_failed = 0;

void
test_normalize_operators(void)
{
	char *out;

	out = filson_normalize_script_ops("echo a;echo b|tr a-z A-Z && echo ok");
	if (out != NULL && strstr(out, ";") != NULL && strstr(out, "|") != NULL && strstr(out, "&&") != NULL) {
		TEST_PASS("normalize_operators");
	} else {
		TEST_FAIL("normalize_operators", "operator normalization failed");
	}
	free(out);
}

void
test_normalize_comment_strip(void)
{
	char *out;

	out = filson_normalize_script_ops("echo hello # comment");
	if (out != NULL && strstr(out, "comment") == NULL) {
		TEST_PASS("normalize_comment_strip");
	} else {
		TEST_FAIL("normalize_comment_strip", "comment not stripped");
	}
	free(out);
}

int
run_capture(const char *command, const char *outfile)
{
	int stdout_backup, fd_out, result;
	char *line;

	stdout_backup = dup(1);
	fd_out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(fd_out, 1);
	line = strdup(command);
	if (line == NULL) {
		dup2(stdout_backup, 1);
		close(fd_out);
		close(stdout_backup);
		return 1;
	}
	result = filson_execute_and_chain(line);
	fflush(stdout);
	dup2(stdout_backup, 1);
	close(fd_out);
	close(stdout_backup);
	free(line);
	return result;
}

void
test_semicolon_execution(void)
{
	FILE *f;
	char buf[256];

	run_capture("echo A; echo B", "/tmp/filson_pipe_test_out.txt");
	f = fopen("/tmp/filson_pipe_test_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("semicolon_execution", "output file missing");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);
	if (strstr(buf, "A") != NULL && strstr(buf, "B") != NULL) {
		TEST_PASS("semicolon_execution");
	} else {
		TEST_FAIL("semicolon_execution", "missing semicolon output");
	}
	unlink("/tmp/filson_pipe_test_out.txt");
}

void
test_or_execution(void)
{
	FILE *f;
	char buf[256];

	run_capture("false || echo fallback", "/tmp/filson_pipe_test_out.txt");
	f = fopen("/tmp/filson_pipe_test_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("or_execution", "output file missing");
		return;
	}
	fgets(buf, sizeof(buf), f);
	fclose(f);
	if (strstr(buf, "fallback") != NULL) {
		TEST_PASS("or_execution");
	} else {
		TEST_FAIL("or_execution", "|| logic failed");
	}
	unlink("/tmp/filson_pipe_test_out.txt");
}

void
test_pipeline_execution(void)
{
	FILE *f;
	char buf[256];

	run_capture("echo left | tr a-z A-Z", "/tmp/filson_pipe_test_out.txt");
	f = fopen("/tmp/filson_pipe_test_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("pipeline_execution", "output file missing");
		return;
	}
	fgets(buf, sizeof(buf), f);
	fclose(f);
	if (strstr(buf, "LEFT") != NULL) {
		TEST_PASS("pipeline_execution");
	} else {
		TEST_FAIL("pipeline_execution", "pipe output incorrect");
	}
	unlink("/tmp/filson_pipe_test_out.txt");
}

void
test_output_redirection(void)
{
	FILE *f;
	char buf[256];
	char *line;

	line = strdup("echo redir > /tmp/filson_pipe_redir.txt");
	if (line == NULL) {
		TEST_FAIL("output_redirection", "allocation failed");
		return;
	}
	filson_execute_and_chain(line);
	free(line);
	f = fopen("/tmp/filson_pipe_redir.txt", "r");
	if (f == NULL) {
		TEST_FAIL("output_redirection", "redirect target missing");
		return;
	}
	fgets(buf, sizeof(buf), f);
	fclose(f);
	if (strstr(buf, "redir") != NULL) {
		TEST_PASS("output_redirection");
	} else {
		TEST_FAIL("output_redirection", "redirected content wrong");
	}
	unlink("/tmp/filson_pipe_redir.txt");
}

void
test_input_redirection(void)
{
	FILE *f;
	char buf[256];

	f = fopen("/tmp/filson_pipe_in.txt", "w");
	if (f == NULL) {
		TEST_FAIL("input_redirection", "setup file create failed");
		return;
	}
	fprintf(f, "inputline\n");
	fclose(f);
	run_capture("cat < /tmp/filson_pipe_in.txt", "/tmp/filson_pipe_test_out.txt");
	f = fopen("/tmp/filson_pipe_test_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("input_redirection", "output file missing");
		unlink("/tmp/filson_pipe_in.txt");
		return;
	}
	fgets(buf, sizeof(buf), f);
	fclose(f);
	if (strstr(buf, "inputline") != NULL) {
		TEST_PASS("input_redirection");
	} else {
		TEST_FAIL("input_redirection", "input redirect failed");
	}
	unlink("/tmp/filson_pipe_test_out.txt");
	unlink("/tmp/filson_pipe_in.txt");
}

void
test_stderr_redirection(void)
{
	FILE *f;
	char buf[256];
	char *line;

	line = strdup("ls /tmp/filson_missing_stderr_target 2> /tmp/filson_pipe_err.txt");
	if (line == NULL) {
		TEST_FAIL("stderr_redirection", "allocation failed");
		return;
	}
	filson_execute_and_chain(line);
	free(line);
	f = fopen("/tmp/filson_pipe_err.txt", "r");
	if (f == NULL) {
		TEST_FAIL("stderr_redirection", "stderr redirect target missing");
		return;
	}
	if (fgets(buf, sizeof(buf), f) == NULL) {
		fclose(f);
		TEST_FAIL("stderr_redirection", "stderr redirect file empty");
		unlink("/tmp/filson_pipe_err.txt");
		return;
	}
	fclose(f);
	if (strstr(buf, "No such file") != NULL || strstr(buf, "cannot access") != NULL) {
		TEST_PASS("stderr_redirection");
	} else {
		TEST_FAIL("stderr_redirection", "unexpected stderr content");
	}
	unlink("/tmp/filson_pipe_err.txt");
}

void
test_stderr_to_stdout_redirection(void)
{
	FILE *f;
	char buf[512];

	run_capture("ls /tmp/filson_missing_merge_target 2>&1", "/tmp/filson_pipe_test_out.txt");
	f = fopen("/tmp/filson_pipe_test_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("stderr_to_stdout_redirection", "capture output missing");
		return;
	}
	if (fgets(buf, sizeof(buf), f) == NULL) {
		fclose(f);
		TEST_FAIL("stderr_to_stdout_redirection", "captured output empty");
		unlink("/tmp/filson_pipe_test_out.txt");
		return;
	}
	fclose(f);
	if (strstr(buf, "No such file") != NULL || strstr(buf, "cannot access") != NULL) {
		TEST_PASS("stderr_to_stdout_redirection");
	} else {
		TEST_FAIL("stderr_to_stdout_redirection", "stderr was not merged into stdout");
	}
	unlink("/tmp/filson_pipe_test_out.txt");
}

void
test_output_append_redirection(void)
{
	FILE *f;
	char buf[256];
	char *line;

	line = strdup("echo first > /tmp/filson_pipe_append.txt; echo second >> /tmp/filson_pipe_append.txt");
	if (line == NULL) {
		TEST_FAIL("output_append_redirection", "allocation failed");
		return;
	}
	filson_execute_and_chain(line);
	free(line);
	f = fopen("/tmp/filson_pipe_append.txt", "r");
	if (f == NULL) {
		TEST_FAIL("output_append_redirection", "append target missing");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);
	if (strstr(buf, "first") != NULL && strstr(buf, "second") != NULL) {
		TEST_PASS("output_append_redirection");
	} else {
		TEST_FAIL("output_append_redirection", "append content incorrect");
	}
	unlink("/tmp/filson_pipe_append.txt");
}

void
test_stderr_append_redirection(void)
{
	FILE *f;
	char buf[512];
	char *line;

	line = strdup("ls /tmp/filson_missing_append_a 2> /tmp/filson_pipe_err_append.txt; ls /tmp/filson_missing_append_b 2>> /tmp/filson_pipe_err_append.txt");
	if (line == NULL) {
		TEST_FAIL("stderr_append_redirection", "allocation failed");
		return;
	}
	filson_execute_and_chain(line);
	free(line);
	f = fopen("/tmp/filson_pipe_err_append.txt", "r");
	if (f == NULL) {
		TEST_FAIL("stderr_append_redirection", "stderr append target missing");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);
	if (strstr(buf, "filson_missing_append_a") != NULL && strstr(buf, "filson_missing_append_b") != NULL) {
		TEST_PASS("stderr_append_redirection");
	} else {
		TEST_FAIL("stderr_append_redirection", "stderr append content incorrect");
	}
	unlink("/tmp/filson_pipe_err_append.txt");
}

int
main(void)
{
	printf("\n=== PIPELINES TESTS ===\n");
	test_normalize_operators();
	test_normalize_comment_strip();
	test_semicolon_execution();
	test_or_execution();
	test_pipeline_execution();
	test_output_redirection();
	test_input_redirection();
	test_stderr_redirection();
	test_stderr_to_stdout_redirection();
	test_output_append_redirection();
	test_stderr_append_redirection();
	printf("\nPassed: %d\n", tests_passed);
	printf("Failed: %d\n", tests_failed);
	return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}