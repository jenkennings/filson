#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PASS(name) printf("[PASS] %s\n", name); tests_passed++
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); tests_failed++

int tests_passed = 0;
int tests_failed = 0;

void
test_script_file_execution(void)
{
	FILE *f;
	char buf[2048];
	int rc;

	f = fopen("/tmp/filson_script_test.sh", "w");
	if (f == NULL) {
		TEST_FAIL("script_file_execution", "failed to create script file");
		return;
	}
	fprintf(f, "echo script_ok\n");
	fprintf(f, "echo chain_start && echo chain_ok\n");
	fprintf(f, "INLINE_SCRIPT_VAR=script_value\n");
	fprintf(f, "echo $INLINE_SCRIPT_VAR\n");
	fprintf(f, "exit\n");
	fclose(f);

	rc = system("./filson /tmp/filson_script_test.sh > /tmp/filson_script_out.txt 2> /tmp/filson_script_err.txt");
	if (rc != 0) {
		TEST_FAIL("script_file_execution", "filson script mode returned non-zero");
		unlink("/tmp/filson_script_test.sh");
		return;
	}
	f = fopen("/tmp/filson_script_out.txt", "r");
	if (f == NULL) {
		TEST_FAIL("script_file_execution", "output file missing");
		unlink("/tmp/filson_script_test.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);
	if (strstr(buf, "script_ok") == NULL || strstr(buf, "chain_ok") == NULL || strstr(buf, "script_value") == NULL) {
		TEST_FAIL("script_file_execution", "script output missing expected content");
	} else {
		TEST_PASS("script_file_execution");
	}

	unlink("/tmp/filson_script_test.sh");
	unlink("/tmp/filson_script_out.txt");
	unlink("/tmp/filson_script_err.txt");
}

void
test_script_missing_file_fails(void)
{
	int rc;

	rc = system("./filson /tmp/filson_script_does_not_exist.sh > /tmp/filson_script_out.txt 2> /tmp/filson_script_err.txt");
	if (rc == 0) {
		TEST_FAIL("script_missing_file_fails", "expected non-zero exit");
	} else {
		TEST_PASS("script_missing_file_fails");
	}
	unlink("/tmp/filson_script_out.txt");
	unlink("/tmp/filson_script_err.txt");
}

int
main(void)
{
	printf("\n=== SCRIPT EXECUTION TESTS ===\n");
	test_script_file_execution();
	test_script_missing_file_fails();
	printf("\nPassed: %d\n", tests_passed);
	printf("Failed: %d\n", tests_failed);
	return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
