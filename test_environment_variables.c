#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int filson_set(char **args);
int filson_execute(char **args, int argc, int background, char *segment);
extern int filson_last_cmd_success;

#define TEST_PASS(name) printf("[PASS] %s\n", name)
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); failures++
#define TEST_START printf("\n=== Running Environment Variable Tests ===\n\n")
#define TEST_END printf("\n=== Test Summary ===\nTotal failures: %d\n\n", failures)

int failures = 0;

void
test_set_simple_variable(void)
{
	char *args[] = {"set", "TEST_VAR", "test_value", NULL};
	char *value;
	int result;

	result = filson_set(args);
	if (result != 1) {
		TEST_FAIL("set_simple_variable", "Expected return value 1");
		return;
	}
	value = getenv("TEST_VAR");
	if (value == NULL) {
		TEST_FAIL("set_simple_variable", "Variable not set in environment");
		return;
	}
	if (strcmp(value, "test_value") != 0) {
		TEST_FAIL("set_simple_variable", "Variable value mismatch");
		return;
	}
	TEST_PASS("set_simple_variable");
}

void
test_overwrite_variable(void)
{
	char *args[] = {"set", "OVERWRITE_TEST", "value1", NULL};
	char *args2[] = {"set", "OVERWRITE_TEST", "value2", NULL};
	char *value1, *value2;
	int result;

	filson_set(args);
	value1 = getenv("OVERWRITE_TEST");
	if (strcmp(value1, "value1") != 0) {
		TEST_FAIL("overwrite_variable", "First set failed");
		return;
	}
	result = filson_set(args2);
	if (result != 1) {
		TEST_FAIL("overwrite_variable", "Expected return value 1 on overwrite");
		return;
	}
	value2 = getenv("OVERWRITE_TEST");
	if (strcmp(value2, "value2") != 0) {
		TEST_FAIL("overwrite_variable", "Overwrite failed");
		return;
	}
	TEST_PASS("overwrite_variable");
}

void
test_set_path_variable(void)
{
	char *args[] = {"set", "PATH", "/custom/bin:/usr/bin:/bin", NULL};
	char *value;
	int result;

	result = filson_set(args);
	if (result != 1) {
		TEST_FAIL("set_path_variable", "Expected return value 1");
		return;
	}
	value = getenv("PATH");
	if (value == NULL) {
		TEST_FAIL("set_path_variable", "PATH not set");
		return;
	}
	if (strcmp(value, "/custom/bin:/usr/bin:/bin") != 0) {
		TEST_FAIL("set_path_variable", "PATH value incorrect");
		return;
	}
	TEST_PASS("set_path_variable");
}

void
test_missing_variable_name(void)
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
	if (result != 1) {
		TEST_FAIL("missing_variable_name", "Expected return value 1");
		return;
	}
	TEST_PASS("missing_variable_name");
}

void
test_missing_variable_value(void)
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
	if (result != 1) {
		TEST_FAIL("missing_variable_value", "Expected return value 1");
		return;
	}
	TEST_PASS("missing_variable_value");
}

void
test_child_inherits_env(void)
{
	char *args[] = {"set", "CHILD_TEST", "inherited_value", NULL};
	char *value;
	pid_t pid;
	int status;

	filson_set(args);
	pid = fork();
	if (pid == 0) {
		value = getenv("CHILD_TEST");
		if (value != NULL && strcmp(value, "inherited_value") == 0) {
			exit(EXIT_SUCCESS);
		} else {
			exit(EXIT_FAILURE);
		}
	} else if (pid > 0) {
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
			TEST_PASS("child_inherits_env");
		} else {
			TEST_FAIL("child_inherits_env", "Child did not inherit environment variable");
		}
	} else {
		TEST_FAIL("child_inherits_env", "fork() failed");
	}
}

void
test_multiple_variables(void)
{
	char *args1[] = {"set", "VAR1", "value1", NULL};
	char *args2[] = {"set", "VAR2", "value2", NULL};
	char *args3[] = {"set", "VAR3", "value3", NULL};
	char *val1, *val2, *val3;

	filson_set(args1);
	filson_set(args2);
	filson_set(args3);
	val1 = getenv("VAR1");
	val2 = getenv("VAR2");
	val3 = getenv("VAR3");
	if (val1 == NULL || strcmp(val1, "value1") != 0) {
		TEST_FAIL("multiple_variables", "VAR1 not set correctly");
		return;
	}
	if (val2 == NULL || strcmp(val2, "value2") != 0) {
		TEST_FAIL("multiple_variables", "VAR2 not set correctly");
		return;
	}
	if (val3 == NULL || strcmp(val3, "value3") != 0) {
		TEST_FAIL("multiple_variables", "VAR3 not set correctly");
		return;
	}
	TEST_PASS("multiple_variables");
}

void
test_empty_string_value(void)
{
	char *args[] = {"set", "EMPTY_VAR", "", NULL};
	char *value;
	int result;

	result = filson_set(args);
	if (result != 1) {
		TEST_FAIL("empty_string_value", "Expected return value 1");
		return;
	}
	value = getenv("EMPTY_VAR");
	if (value == NULL) {
		TEST_FAIL("empty_string_value", "Variable not set");
		return;
	}
	if (strlen(value) != 0) {
		TEST_FAIL("empty_string_value", "Expected empty string value");
		return;
	}
	TEST_PASS("empty_string_value");
}

void
test_special_characters_in_value(void)
{
	char *args[] = {"set", "SPECIAL_VAR", "!@#$%^&*()", NULL};
	char *value;
	int result;

	result = filson_set(args);
	if (result != 1) {
		TEST_FAIL("special_characters_in_value", "Expected return value 1");
		return;
	}
	value = getenv("SPECIAL_VAR");
	if (value == NULL || strcmp(value, "!@#$%^&*()") != 0) {
		TEST_FAIL("special_characters_in_value", "Special characters not preserved");
		return;
	}
	TEST_PASS("special_characters_in_value");
}

void
test_case_sensitivity(void)
{
	char *args1[] = {"set", "CaseSensitive", "value1", NULL};
	char *args2[] = {"set", "casesensitive", "value2", NULL};
	char *val1, *val2;

	filson_set(args1);
	filson_set(args2);
	val1 = getenv("CaseSensitive");
	val2 = getenv("casesensitive");
	if (val1 == NULL || strcmp(val1, "value1") != 0) {
		TEST_FAIL("case_sensitivity", "CaseSensitive not preserved");
		return;
	}
	if (val2 == NULL || strcmp(val2, "value2") != 0) {
		TEST_FAIL("case_sensitivity", "casesensitive not set");
		return;
	}
	TEST_PASS("case_sensitivity");
}

void
test_inline_assignment_with_command(void)
{
	char *args[] = {"INLINE_TEST_VAR=inline_value", "echo", "$INLINE_TEST_VAR", NULL};
	int stdout_backup, fd_out;
	FILE *f;
	char buf[256];

	stdout_backup = dup(1);
	fd_out = open("/tmp/filson_inline_assign_out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(fd_out, 1);
	filson_execute(args, 3, 0, "INLINE_TEST_VAR=inline_value echo $INLINE_TEST_VAR");
	fflush(stdout);
	dup2(stdout_backup, 1);
	close(fd_out);
	close(stdout_backup);
	f = fopen("/tmp/filson_inline_assign_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("inline_assignment_with_command", "output file missing");
		return;
	}
	if (fgets(buf, sizeof(buf), f) == NULL) {
		fclose(f);
		TEST_FAIL("inline_assignment_with_command", "output empty");
		unlink("/tmp/filson_inline_assign_out.txt");
		return;
	}
	fclose(f);
	unlink("/tmp/filson_inline_assign_out.txt");
	if (strstr(buf, "inline_value") == NULL) {
		TEST_FAIL("inline_assignment_with_command", "inline value not visible to command");
		return;
	}
	if (getenv("INLINE_TEST_VAR") != NULL) {
		TEST_FAIL("inline_assignment_with_command", "inline assignment leaked to shell env");
		unsetenv("INLINE_TEST_VAR");
		return;
	}
	if (!filson_last_cmd_success) {
		TEST_FAIL("inline_assignment_with_command", "command reported failure");
		return;
	}
	TEST_PASS("inline_assignment_with_command");
}

void
test_assignment_only_sets_shell_env(void)
{
	char *args[] = {"ONLY_ASSIGN_VAR=only_value", NULL};
	char *value;

	unsetenv("ONLY_ASSIGN_VAR");
	filson_execute(args, 1, 0, "ONLY_ASSIGN_VAR=only_value");
	value = getenv("ONLY_ASSIGN_VAR");
	if (value == NULL || strcmp(value, "only_value") != 0) {
		TEST_FAIL("assignment_only_sets_shell_env", "assignment-only form did not persist");
		return;
	}
	if (!filson_last_cmd_success) {
		TEST_FAIL("assignment_only_sets_shell_env", "assignment-only form reported failure");
		return;
	}
	TEST_PASS("assignment_only_sets_shell_env");
}

int
main(void)
{
	TEST_START;
	test_set_simple_variable();
	test_overwrite_variable();
	test_set_path_variable();
	test_missing_variable_name();
	test_missing_variable_value();
	test_child_inherits_env();
	test_multiple_variables();
	test_empty_string_value();
	test_special_characters_in_value();
	test_case_sensitivity();
	test_inline_assignment_with_command();
	test_assignment_only_sets_shell_env();
	TEST_END;
	return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
