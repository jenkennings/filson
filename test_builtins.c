#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define TEST_PASS(name) printf("[PASS] %s\n", name); tests_passed++
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); tests_failed++
#define TEST_SECTION(name) printf("\n=== %s ===\n", name)

int tests_passed = 0;
int tests_failed = 0;

int filson_cd(char **args);
int filson_help(char **args);
int filson_exit(char **args);
int filson_set(char **args);
int filson_echo(char **args);
int filson_num_builtins(void);
void filson_add_history(const char *line);
void filson_clear_history(void);
char *filson_resolve_history(char *line);

void
test_cd_no_args(void)
{
	char *args[] = {"cd", NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_cd(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1) {
		TEST_PASS("cd_no_args");
	} else {
		TEST_FAIL("cd_no_args", "Expected return 1");
	}
}

void
test_cd_valid_directory(void)
{
	char *args[] = {"cd", "/tmp", NULL};
	char cwd[256];
	int result;

	result = filson_cd(args);
	getcwd(cwd, sizeof(cwd));
	if (result == 1 && strcmp(cwd, "/tmp") == 0) {
		TEST_PASS("cd_valid_directory");
		chdir("/home/bc");
	} else {
		TEST_FAIL("cd_valid_directory", "Failed to change directory");
	}
}

void
test_cd_invalid_directory(void)
{
	char *args[] = {"cd", "/nonexistent_directory_xyz", NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_cd(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1) {
		TEST_PASS("cd_invalid_directory");
	} else {
		TEST_FAIL("cd_invalid_directory", "Expected return 1 on error");
	}
}

void
test_help_lists_builtins(void)
{
	char *args[] = {"help", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[1024];
	int found_header, found_cd;

	stdout_backup = dup(1);
	temp_file = open("/tmp/help_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_help(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/help_output.txt", "r");
	found_header = 0;
	found_cd = 0;
	while (fgets(buffer, sizeof(buffer), f)) {
		if (strstr(buffer, "Filson") != NULL)
			found_header = 1;
		if (strstr(buffer, "cd") != NULL)
			found_cd = 1;
	}
	fclose(f);
	if (result == 1 && found_header && found_cd) {
		TEST_PASS("help_lists_builtins");
	} else {
		TEST_FAIL("help_lists_builtins", "Help output missing expected content");
	}
	unlink("/tmp/help_output.txt");
}

void
test_help_return_value(void)
{
	char *args[] = {"help", NULL};
	int stderr_backup, devnull, stdout_backup, temp_file, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	stdout_backup = dup(1);
	temp_file = open("/tmp/help_output.txt", O_WRONLY | O_CREAT, 0644);
	dup2(temp_file, 1);
	result = filson_help(args);
	dup2(stdout_backup, 1);
	dup2(stderr_backup, 2);
	close(temp_file);
	close(devnull);
	close(stdout_backup);
	close(stderr_backup);
	if (result == 1) {
		TEST_PASS("help_return_value");
	} else {
		TEST_FAIL("help_return_value", "Expected return 1");
	}
	unlink("/tmp/help_output.txt");
}

void
test_exit_return_value(void)
{
	char *args[] = {"exit", NULL};
	int result;

	result = filson_exit(args);
	if (result == 0) {
		TEST_PASS("exit_return_value");
	} else {
		TEST_FAIL("exit_return_value", "Expected return 0");
	}
}

void
test_set_simple_variable(void)
{
	char *args[] = {"set", "TEST_VAR", "test_value", NULL};
	char *value;
	int result;

	result = filson_set(args);
	value = getenv("TEST_VAR");
	if (result == 1 && value != NULL && strcmp(value, "test_value") == 0) {
		TEST_PASS("set_simple_variable");
	} else {
		TEST_FAIL("set_simple_variable", "Failed to set variable");
	}
}

void
test_set_overwrite_variable(void)
{
	char *args1[] = {"set", "OVERWRITE_TEST", "value1", NULL};
	char *args2[] = {"set", "OVERWRITE_TEST", "value2", NULL};
	char *value;

	filson_set(args1);
	value = getenv("OVERWRITE_TEST");
	if (strcmp(value, "value1") != 0) {
		TEST_FAIL("overwrite_variable", "First set failed");
		return;
	}
	filson_set(args2);
	value = getenv("OVERWRITE_TEST");
	if (strcmp(value, "value2") == 0) {
		TEST_PASS("overwrite_variable");
	} else {
		TEST_FAIL("overwrite_variable", "Failed to overwrite variable");
	}
}

void
test_set_no_variable_name(void)
{
	char *args[] = {"set", NULL, NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_set(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1) {
		TEST_PASS("set_no_variable_name");
	} else {
		TEST_FAIL("set_no_variable_name", "Expected return 1 on error");
	}
}

void
test_set_no_value(void)
{
	char *args[] = {"set", "VAR_NAME", NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_set(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1) {
		TEST_PASS("set_no_value");
	} else {
		TEST_FAIL("set_no_value", "Expected return 1 on error");
	}
}

void
test_set_empty_value(void)
{
	char *args[] = {"set", "EMPTY_VAR", "", NULL};
	char *value;
	int result;

	result = filson_set(args);
	value = getenv("EMPTY_VAR");
	if (result == 1 && value != NULL && strlen(value) == 0) {
		TEST_PASS("set_empty_value");
	} else {
		TEST_FAIL("set_empty_value", "Failed to set empty value");
	}
}

void
test_set_special_chars(void)
{
	char *args[] = {"set", "SPECIAL_VAR", "!@#$%^&*()", NULL};
	char *value;
	int result;

	result = filson_set(args);
	value = getenv("SPECIAL_VAR");
	if (result == 1 && value != NULL && strcmp(value, "!@#$%^&*()") == 0) {
		TEST_PASS("set_special_chars");
	} else {
		TEST_FAIL("set_special_chars", "Special characters not preserved");
	}
}

void
test_echo_literal_text(void)
{
	char *args[] = {"echo", "hello", "world", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && strstr(buffer, "hello world") != NULL) {
		TEST_PASS("echo_literal_text");
	} else {
		TEST_FAIL("echo_literal_text", "Echo output incorrect");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_variable_expansion(void)
{
	char *args[] = {"echo", "$ECHO_TEST", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	setenv("ECHO_TEST", "expanded_value", 1);
	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && strstr(buffer, "expanded_value") != NULL) {
		TEST_PASS("echo_variable_expansion");
	} else {
		TEST_FAIL("echo_variable_expansion", "Variable not expanded");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_mixed_text_and_vars(void)
{
	char *args[] = {"echo", "Hello", "$USER_NAME", "welcome", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	setenv("USER_NAME", "testuser", 1);
	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && strstr(buffer, "Hello") && strstr(buffer, "testuser") && 
	    strstr(buffer, "welcome")) {
		TEST_PASS("echo_mixed_text_and_vars");
	} else {
		TEST_FAIL("echo_mixed_text_and_vars", "Mixed output incorrect");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_undefined_variable(void)
{
	char *args[] = {"echo", "text", "$UNDEFINED_VAR_XYZ", "more", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	unsetenv("UNDEFINED_VAR_XYZ");
	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && strstr(buffer, "text") && strstr(buffer, "more")) {
		TEST_PASS("echo_undefined_variable");
	} else {
		TEST_FAIL("echo_undefined_variable", "Undefined variable handling incorrect");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_no_args(void)
{
	char *args[] = {"echo", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && (buffer[0] == '\n' || buffer[0] == '\0')) {
		TEST_PASS("echo_no_args");
	} else {
		TEST_FAIL("echo_no_args", "Echo with no args should print newline");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_multiple_variables(void)
{
	char *args[] = {"echo", "$VAR1", "$VAR2", "$VAR3", NULL};
	int stdout_backup, temp_file, result;
	FILE *f;
	char buffer[256];

	setenv("VAR1", "first", 1);
	setenv("VAR2", "second", 1);
	setenv("VAR3", "third", 1);
	stdout_backup = dup(1);
	temp_file = open("/tmp/echo_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(temp_file, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(temp_file);
	close(stdout_backup);
	f = fopen("/tmp/echo_output.txt", "r");
	fgets(buffer, sizeof(buffer), f);
	fclose(f);
	if (result == 1 && strstr(buffer, "first") && strstr(buffer, "second") && 
	    strstr(buffer, "third")) {
		TEST_PASS("echo_multiple_variables");
	} else {
		TEST_FAIL("echo_multiple_variables", "Multiple variables not expanded correctly");
	}
	unlink("/tmp/echo_output.txt");
}

void
test_echo_return_value(void)
{
	char *args[] = {"echo", "test", NULL};
	int stdout_backup, devnull, result;

	stdout_backup = dup(1);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 1);
	result = filson_echo(args);
	dup2(stdout_backup, 1);
	close(devnull);
	close(stdout_backup);
	if (result == 1) {
		TEST_PASS("echo_return_value");
	} else {
		TEST_FAIL("echo_return_value", "Expected return 1");
	}
}

void
test_history_expand_bangbang(void)
{
	char *line, *resolved;

	filson_clear_history();
	filson_add_history("echo hello");
	line = strdup("!!");
	resolved = filson_resolve_history(line);
	if (resolved != NULL && strcmp(resolved, "echo hello") == 0) {
		TEST_PASS("history_expand_bangbang");
	} else {
		TEST_FAIL("history_expand_bangbang", "!! did not resolve to last command");
	}
	free(line);
	free(resolved);
	filson_clear_history();
}

void
test_history_expand_last_arg(void)
{
	char *line, *resolved;

	filson_clear_history();
	filson_add_history("echo alpha beta");
	line = strdup("echo !$");
	resolved = filson_resolve_history(line);
	if (resolved != NULL && strcmp(resolved, "echo beta") == 0) {
		TEST_PASS("history_expand_last_arg");
	} else {
		TEST_FAIL("history_expand_last_arg", "!$ did not resolve to prior last argument");
	}
	free(line);
	free(resolved);
	filson_clear_history();
}

void
test_history_expand_last_arg_without_history(void)
{
	char *line, *resolved;

	filson_clear_history();
	line = strdup("echo !$");
	resolved = filson_resolve_history(line);
	if (resolved == NULL) {
		TEST_PASS("history_expand_last_arg_without_history");
	} else {
		TEST_FAIL("history_expand_last_arg_without_history", "Expected failure with empty history");
	}
	free(line);
	free(resolved);
}

int
main(void)
{
	printf("\n╔════════════════════════════════════════╗\n");
	printf("║   FILSON SHELL BUILT-IN UNIT TESTS   ║\n");
	printf("╚════════════════════════════════════════╝\n");
	TEST_SECTION("CD COMMAND");
	test_cd_no_args();
	test_cd_valid_directory();
	test_cd_invalid_directory();
	TEST_SECTION("HELP COMMAND");
	test_help_lists_builtins();
	test_help_return_value();
	TEST_SECTION("EXIT COMMAND");
	test_exit_return_value();
	TEST_SECTION("SET COMMAND");
	test_set_simple_variable();
	test_set_overwrite_variable();
	test_set_no_variable_name();
	test_set_no_value();
	test_set_empty_value();
	test_set_special_chars();
	TEST_SECTION("ECHO COMMAND");
	test_echo_literal_text();
	test_echo_variable_expansion();
	test_echo_mixed_text_and_vars();
	test_echo_undefined_variable();
	test_echo_no_args();
	test_echo_multiple_variables();
	test_echo_return_value();
	TEST_SECTION("HISTORY EXPANSION");
	test_history_expand_bangbang();
	test_history_expand_last_arg();
	test_history_expand_last_arg_without_history();
	printf("\n╔════════════════════════════════════════╗\n");
	printf("║          TEST RESULTS SUMMARY         ║\n");
	printf("╠════════════════════════════════════════╣\n");
	printf("║ PASSED: %d                            ║\n", tests_passed);
	printf("║ FAILED: %d                            ║\n", tests_failed);
	printf("║ TOTAL:  %d                            ║\n", tests_passed + tests_failed);
	printf("╚════════════════════════════════════════╝\n\n");
	return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
