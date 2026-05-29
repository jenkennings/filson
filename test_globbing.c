#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "globbing.h"

#define TEST_DIR "/tmp/filson_glob_test"

int tests_passed = 0;
int tests_failed = 0;

void
assert_equal(const char *test_name, const char *expected, const char *actual)
{
	if (strcmp(expected, actual) == 0) {
		printf("[PASS] %s\n", test_name);
		tests_passed++;
	} else {
		printf("[FAIL] %s\n", test_name);
		printf("  Expected: %s\n", expected);
		printf("  Got:      %s\n", actual);
		tests_failed++;
	}
}

void
assert_true(const char *test_name, int condition)
{
	if (condition) {
		printf("[PASS] %s\n", test_name);
		tests_passed++;
	} else {
		printf("[FAIL] %s\n", test_name);
		tests_failed++;
	}
}

int
count_args(char **args)
{
	int count = 0;
	while (args[count] != NULL) {
		count++;
	}
	return count;
}

void
setup_test_files(void)
{
	system("mkdir -p " TEST_DIR);
	system("cd " TEST_DIR " && touch file1.txt file2.txt file3.txt");
	system("cd " TEST_DIR " && touch test1.c test2.c");
	system("cd " TEST_DIR " && mkdir subdir");
	system("cd " TEST_DIR " && touch subdir/nested.txt");
}

void
cleanup_test_files(void)
{
	system("rm -rf " TEST_DIR);
}

void
test_no_glob_patterns(void)
{
	char *line = "ls file1.txt";
	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(10);
	strcpy(args[1], "file1.txt");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	assert_true("no_glob_returns_same_args", count_args(result) == 2);
	assert_equal("no_glob_first_arg", "ls", result[0]);
	assert_equal("no_glob_second_arg", "file1.txt", result[1]);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}
}

void
test_asterisk_glob(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char *line = "ls *.txt";
	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(6);
	strcpy(args[1], "*.txt");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	int count = count_args(result);
	
	assert_true("asterisk_glob_expands_files", count >= 3);
	assert_true("asterisk_glob_contains_file1", 
		strstr(result[1], "file1") != NULL || 
		strstr(result[2], "file1") != NULL ||
		strstr(result[3], "file1") != NULL);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}

	chdir(cwd);
}

void
test_question_mark_glob(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(10);
	strcpy(args[1], "file?.txt");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	int count = count_args(result);
	
	assert_true("question_mark_glob_expands", count >= 3);
	assert_equal("question_mark_first_arg", "ls", result[0]);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}

	chdir(cwd);
}

void
test_mixed_patterns_and_literals(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char **args = malloc(4 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(6);
	strcpy(args[1], "*.txt");
	args[2] = malloc(10);
	strcpy(args[2], "test1.c");
	args[3] = NULL;

	char **result = filson_expand_globs(args);
	int count = count_args(result);
	
	assert_true("mixed_patterns_expands_correctly", count >= 4);
	assert_equal("mixed_patterns_first_arg", "ls", result[0]);
	assert_true("mixed_patterns_contains_literal",
		strcmp(result[count - 1], "test1.c") == 0);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args[2]);
		free(args);
	}

	chdir(cwd);
}

void
test_no_matches_pattern(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(10);
	strcpy(args[1], "*.nonexist");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	assert_true("no_matches_returns_pattern", 
		strstr(result[1], "nonexist") != NULL);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}

	chdir(cwd);
}

void
test_globbing_with_command(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(5);
	strcpy(args[0], "echo");
	args[1] = malloc(6);
	strcpy(args[1], "*.txt");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	assert_equal("globbing_with_command_first_arg", "echo", result[0]);
	assert_true("globbing_with_command_expands", count_args(result) >= 3);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}

	chdir(cwd);
}

void
test_bracket_glob(void)
{
	char cwd[256];
	getcwd(cwd, sizeof(cwd));
	chdir(TEST_DIR);

	char **args = malloc(3 * sizeof(char *));
	args[0] = malloc(3);
	strcpy(args[0], "ls");
	args[1] = malloc(10);
	strcpy(args[1], "file[12].txt");
	args[2] = NULL;

	char **result = filson_expand_globs(args);
	int count = count_args(result);
	
	assert_true("bracket_glob_expands", count >= 2);
	assert_equal("bracket_glob_first_arg", "ls", result[0]);

	if (result != args) {
		filson_free_expanded_args(result);
	} else {
		free(args[0]);
		free(args[1]);
		free(args);
	}

	chdir(cwd);
}

int
main(void)
{
	printf("\n╔════════════════════════════════════════╗\n");
	printf("║     FILSON SHELL GLOBBING TESTS      ║\n");
	printf("╚════════════════════════════════════════╝\n\n");

	setup_test_files();

	printf("=== Basic Globbing ===\n");
	test_no_glob_patterns();
	test_asterisk_glob();
	test_question_mark_glob();
	test_bracket_glob();

	printf("\n=== Advanced Globbing ===\n");
	test_mixed_patterns_and_literals();
	test_no_matches_pattern();
	test_globbing_with_command();

	cleanup_test_files();

	printf("\n╔════════════════════════════════════════╗\n");
	printf("║          TEST RESULTS SUMMARY        ║\n");
	printf("╠════════════════════════════════════════╣\n");
	printf("║ PASSED: %-30d║\n", tests_passed);
	printf("║ FAILED: %-30d║\n", tests_failed);
	printf("║ TOTAL:  %-30d║\n", tests_passed + tests_failed);
	printf("╚════════════════════════════════════════╝\n\n");

	return tests_failed > 0 ? 1 : 0;
}
