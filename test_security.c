#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/resource.h>

#define TEST_PASS(name) printf("[PASS] %s\n", name); tests_passed++
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); tests_failed++

int tests_passed = 0;
int tests_failed = 0;

int filson_is_valid_varname(const char *name);
int filson_arg_count(char **args);
int filson_path_is_safe(void);
int filson_set(char **args);
int filson_execute_and_chain(char *line);

void
test_valid_varname_simple(void)
{
	if (filson_is_valid_varname("MYVAR") == 1) {
		TEST_PASS("valid_varname_simple");
	} else {
		TEST_FAIL("valid_varname_simple", "Should accept simple alphanumeric");
	}
}

void
test_valid_varname_underscore(void)
{
	if (filson_is_valid_varname("MY_VAR_123") == 1) {
		TEST_PASS("valid_varname_underscore");
	} else {
		TEST_FAIL("valid_varname_underscore", "Should accept underscores");
	}
}

void
test_invalid_varname_starts_with_digit(void)
{
	if (filson_is_valid_varname("1VAR") == 0) {
		TEST_PASS("invalid_varname_starts_with_digit");
	} else {
		TEST_FAIL("invalid_varname_starts_with_digit", "Should reject starting with digit");
	}
}

void
test_invalid_varname_with_dash(void)
{
	if (filson_is_valid_varname("MY-VAR") == 0) {
		TEST_PASS("invalid_varname_with_dash");
	} else {
		TEST_FAIL("invalid_varname_with_dash", "Should reject dashes");
	}
}

void
test_invalid_varname_with_space(void)
{
	if (filson_is_valid_varname("MY VAR") == 0) {
		TEST_PASS("invalid_varname_with_space");
	} else {
		TEST_FAIL("invalid_varname_with_space", "Should reject spaces");
	}
}

void
test_invalid_varname_with_special_chars(void)
{
	if (filson_is_valid_varname("MY$VAR") == 0) {
		TEST_PASS("invalid_varname_with_special_chars");
	} else {
		TEST_FAIL("invalid_varname_with_special_chars", "Should reject special chars");
	}
}

void
test_invalid_varname_null(void)
{
	if (filson_is_valid_varname(NULL) == 0) {
		TEST_PASS("invalid_varname_null");
	} else {
		TEST_FAIL("invalid_varname_null", "Should reject NULL");
	}
}

void
test_invalid_varname_empty(void)
{
	if (filson_is_valid_varname("") == 0) {
		TEST_PASS("invalid_varname_empty");
	} else {
		TEST_FAIL("invalid_varname_empty", "Should reject empty string");
	}
}

void
test_arg_count_empty(void)
{
	char *args[] = {NULL};

	if (filson_arg_count(args) == 0) {
		TEST_PASS("arg_count_empty");
	} else {
		TEST_FAIL("arg_count_empty", "Should count 0 args");
	}
}

void
test_arg_count_single(void)
{
	char *args[] = {"ls", NULL};

	if (filson_arg_count(args) == 1) {
		TEST_PASS("arg_count_single");
	} else {
		TEST_FAIL("arg_count_single", "Should count 1 arg");
	}
}

void
test_arg_count_multiple(void)
{
	char *args[] = {"ls", "-la", "/tmp", NULL};

	if (filson_arg_count(args) == 3) {
		TEST_PASS("arg_count_multiple");
	} else {
		TEST_FAIL("arg_count_multiple", "Should count 3 args");
	}
}

void
test_set_with_invalid_varname(void)
{
	char *args[] = {"set", "123INVALID", "value", NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_set(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1 && getenv("123INVALID") == NULL) {
		TEST_PASS("set_with_invalid_varname");
	} else {
		TEST_FAIL("set_with_invalid_varname", "Should reject invalid varname");
	}
}

void
test_set_with_special_char_varname(void)
{
	char *args[] = {"set", "MY$VAR", "value", NULL};
	int stderr_backup, devnull, result;

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_set(args);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);
	if (result == 1 && getenv("MY$VAR") == NULL) {
		TEST_PASS("set_with_special_char_varname");
	} else {
		TEST_FAIL("set_with_special_char_varname", "Should reject special chars");
	}
}

void
test_set_with_valid_varname(void)
{
	char *args[] = {"set", "SECUREVAR", "secure_value", NULL};
	char *value;
	int result;

	result = filson_set(args);
	value = getenv("SECUREVAR");
	if (result == 1 && value != NULL && strcmp(value, "secure_value") == 0) {
		TEST_PASS("set_with_valid_varname");
	} else {
		TEST_FAIL("set_with_valid_varname", "Should accept valid varname");
	}
}

void
test_pipeline_stage_limit(void)
{
	int i, result, stderr_backup, devnull;
	char line[4096];
	int pos;

	pos = 0;
	pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "echo x");
	for (i = 0; i < 80 && pos < (int)sizeof(line) - 8; i++) {
		pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " | cat");
	}

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_execute_and_chain(line);
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);

	if (result != 0) {
		TEST_PASS("pipeline_stage_limit");
	} else {
		TEST_FAIL("pipeline_stage_limit", "Should reject >64 pipeline stages without crashing");
	}
}

void
test_unsafe_path_blocks_exec(void)
{
	char *old_path;
	char *args[] = {"true", NULL};
	int stderr_backup, devnull, result;
	extern int filson_last_cmd_success;
	extern int filson_execute(char **args, int background, char *segment);

	old_path = getenv("PATH");
	setenv("PATH", ".:/usr/bin:/bin", 1);

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_execute(args, 0, "true");
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);

	if (old_path != NULL) {
		setenv("PATH", old_path, 1);
	}

	if (result == 1 && filson_last_cmd_success == 0) {
		TEST_PASS("unsafe_path_blocks_exec");
	} else {
		TEST_FAIL("unsafe_path_blocks_exec", "Should block external execution when PATH is unsafe");
	}
}

void
test_safe_path_allows_exec(void)
{
	char *old_path;
	char *args[] = {"true", NULL};
	int stderr_backup, devnull, result;
	extern int filson_last_cmd_success;
	extern int filson_execute(char **args, int background, char *segment);

	old_path = getenv("PATH");
	setenv("PATH", "/usr/bin:/bin", 1);

	stderr_backup = dup(2);
	devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 2);
	result = filson_execute(args, 0, "true");
	dup2(stderr_backup, 2);
	close(devnull);
	close(stderr_backup);

	if (old_path != NULL) {
		setenv("PATH", old_path, 1);
	}

	if (result == 1 && filson_last_cmd_success == 1) {
		TEST_PASS("safe_path_allows_exec");
	} else {
		TEST_FAIL("safe_path_allows_exec", "Should allow external execution when PATH is safe");
	}
}

int
main(void)
{
	printf("\n╔════════════════════════════════════════╗\n");
	printf("║   FILSON SHELL SECURITY UNIT TESTS   ║\n");
	printf("╚════════════════════════════════════════╝\n");
	printf("\n=== Variable Name Validation ===\n");
	test_valid_varname_simple();
	test_valid_varname_underscore();
	test_invalid_varname_starts_with_digit();
	test_invalid_varname_with_dash();
	test_invalid_varname_with_space();
	test_invalid_varname_with_special_chars();
	test_invalid_varname_null();
	test_invalid_varname_empty();
	printf("\n=== Argument Count Validation ===\n");
	test_arg_count_empty();
	test_arg_count_single();
	test_arg_count_multiple();
	printf("\n=== Set Command Security ===\n");
	test_set_with_invalid_varname();
	test_set_with_special_char_varname();
	test_set_with_valid_varname();
	printf("\n=== Pipeline Security ===\n");
	test_pipeline_stage_limit();
	printf("\n=== PATH Enforcement ===\n");
	test_unsafe_path_blocks_exec();
	test_safe_path_allows_exec();
	printf("\n╔════════════════════════════════════════╗\n");
	printf("║          TEST RESULTS SUMMARY         ║\n");
	printf("╠════════════════════════════════════════╣\n");
	printf("║ PASSED: %d                            ║\n", tests_passed);
	printf("║ FAILED: %d                            ║\n", tests_failed);
	printf("║ TOTAL:  %d                            ║\n", tests_passed + tests_failed);
	printf("╚════════════════════════════════════════╝\n\n");
	return tests_failed > 0 ? 1 : 0;
}
