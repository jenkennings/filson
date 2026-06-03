#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define TEST_PASS(name) printf("[PASS] %s\n", name); tests_passed++
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); tests_failed++

int tests_passed = 0;
int tests_failed = 0;

void
test_if_true_then(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_1.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_true_then", "failed to create script");
		return;
	}
	fprintf(f, "if true; then echo pass; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_1.sh 2>/dev/null > /tmp/if_out_1.txt");
	f = fopen("/tmp/if_out_1.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_true_then", "output file missing");
		unlink("/tmp/if_test_1.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "pass") != NULL) {
		TEST_PASS("if_true_then");
	} else {
		TEST_FAIL("if_true_then", "expected 'pass' in output");
	}
	unlink("/tmp/if_test_1.sh");
	unlink("/tmp/if_out_1.txt");
}

void
test_if_false_then(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_2.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_false_then", "failed to create script");
		return;
	}
	fprintf(f, "if false; then echo fail; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_2.sh 2>/dev/null > /tmp/if_out_2.txt");
	f = fopen("/tmp/if_out_2.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_false_then", "output file missing");
		unlink("/tmp/if_test_2.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "fail") == NULL) {
		TEST_PASS("if_false_then");
	} else {
		TEST_FAIL("if_false_then", "should not output 'fail'");
	}
	unlink("/tmp/if_test_2.sh");
	unlink("/tmp/if_out_2.txt");
}

void
test_if_then_else_true(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_3.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_then_else_true", "failed to create script");
		return;
	}
	fprintf(f, "if true; then echo branch1; else echo branch2; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_3.sh 2>/dev/null > /tmp/if_out_3.txt");
	f = fopen("/tmp/if_out_3.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_then_else_true", "output file missing");
		unlink("/tmp/if_test_3.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "branch1") != NULL && strstr(buf, "branch2") == NULL) {
		TEST_PASS("if_then_else_true");
	} else {
		TEST_FAIL("if_then_else_true", "expected 'branch1' only");
	}
	unlink("/tmp/if_test_3.sh");
	unlink("/tmp/if_out_3.txt");
}

void
test_if_then_else_false(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_4.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_then_else_false", "failed to create script");
		return;
	}
	fprintf(f, "if false; then echo branch1; else echo branch2; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_4.sh 2>/dev/null > /tmp/if_out_4.txt");
	f = fopen("/tmp/if_out_4.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_then_else_false", "output file missing");
		unlink("/tmp/if_test_4.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "branch2") != NULL && strstr(buf, "branch1") == NULL) {
		TEST_PASS("if_then_else_false");
	} else {
		TEST_FAIL("if_then_else_false", "expected 'branch2' only");
	}
	unlink("/tmp/if_test_4.sh");
	unlink("/tmp/if_out_4.txt");
}

void
test_if_elif_else(void)
{
	FILE *f;
	char buf[2048];
	char *p;

	f = fopen("/tmp/if_test_5.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_elif_else", "failed to create script");
		return;
	}
	fprintf(f, "if false; then echo branch1; elif true; then echo branch2; else echo branch3; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_5.sh 2>/dev/null > /tmp/if_out_5.txt");
	f = fopen("/tmp/if_out_5.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_elif_else", "output file missing");
		unlink("/tmp/if_test_5.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	p = strstr(buf, "branch2");
	if (p != NULL && strstr(buf, "branch1") == NULL && strstr(buf, "branch3") == NULL) {
		TEST_PASS("if_elif_else");
	} else {
		TEST_FAIL("if_elif_else", "expected 'branch2' only");
	}
	unlink("/tmp/if_test_5.sh");
	unlink("/tmp/if_out_5.txt");
}

void
test_if_with_test_file(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_6.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_with_test_file", "failed to create script");
		return;
	}
	fprintf(f, "if test -f /etc/passwd; then echo file_exists; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_6.sh 2>/dev/null > /tmp/if_out_6.txt");
	f = fopen("/tmp/if_out_6.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_with_test_file", "output file missing");
		unlink("/tmp/if_test_6.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "file_exists") != NULL) {
		TEST_PASS("if_with_test_file");
	} else {
		TEST_FAIL("if_with_test_file", "expected 'file_exists'");
	}
	unlink("/tmp/if_test_6.sh");
	unlink("/tmp/if_out_6.txt");
}

void
test_if_with_test_string_equal(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_7.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_with_test_string_equal", "failed to create script");
		return;
	}
	fprintf(f, "if test hello = hello; then echo equal; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_7.sh 2>/dev/null > /tmp/if_out_7.txt");
	f = fopen("/tmp/if_out_7.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_with_test_string_equal", "output file missing");
		unlink("/tmp/if_test_7.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "equal") != NULL) {
		TEST_PASS("if_with_test_string_equal");
	} else {
		TEST_FAIL("if_with_test_string_equal", "expected 'equal'");
	}
	unlink("/tmp/if_test_7.sh");
	unlink("/tmp/if_out_7.txt");
}

void
test_if_with_test_int_comparison(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_8.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_with_test_int_comparison", "failed to create script");
		return;
	}
	fprintf(f, "if test 5 -gt 3; then echo greater; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_8.sh 2>/dev/null > /tmp/if_out_8.txt");
	f = fopen("/tmp/if_out_8.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_with_test_int_comparison", "output file missing");
		unlink("/tmp/if_test_8.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "greater") != NULL) {
		TEST_PASS("if_with_test_int_comparison");
	} else {
		TEST_FAIL("if_with_test_int_comparison", "expected 'greater'");
	}
	unlink("/tmp/if_test_8.sh");
	unlink("/tmp/if_out_8.txt");
}

void
test_if_nested(void)
{
	FILE *f;
	char buf[2048];

	f = fopen("/tmp/if_test_9.sh", "w");
	if (f == NULL) {
		TEST_FAIL("if_nested", "failed to create script");
		return;
	}
	fprintf(f, "if true; then if true; then echo nested; fi; fi\n");
	fclose(f);

	system("./filson /tmp/if_test_9.sh 2>/dev/null > /tmp/if_out_9.txt");
	f = fopen("/tmp/if_out_9.txt", "r");
	if (f == NULL) {
		TEST_FAIL("if_nested", "output file missing");
		unlink("/tmp/if_test_9.sh");
		return;
	}
	buf[0] = '\0';
	while (fgets(buf + strlen(buf), sizeof(buf) - (int)strlen(buf), f) != NULL) {}
	fclose(f);

	if (strstr(buf, "nested") != NULL) {
		TEST_PASS("if_nested");
	} else {
		TEST_FAIL("if_nested", "expected 'nested'");
	}
	unlink("/tmp/if_test_9.sh");
	unlink("/tmp/if_out_9.txt");
}

int
main(void)
{
	printf("\n=== If/Then/Else Control Flow Tests ===\n\n");

	test_if_true_then();
	test_if_false_then();
	test_if_then_else_true();
	test_if_then_else_false();
	test_if_elif_else();
	test_if_with_test_file();
	test_if_with_test_string_equal();
	test_if_with_test_int_comparison();
	test_if_nested();

	printf("\n=== Summary ===\n");
	printf("Passed: %d\n", tests_passed);
	printf("Failed: %d\n", tests_failed);
	printf("\n");

	return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
