#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

/* Forward declarations from filson.c */
int filson_set(char **args);

/* Test framework macros */
#define TEST_PASS(name) printf("[PASS] %s\n", name)
#define TEST_FAIL(name, msg) printf("[FAIL] %s: %s\n", name, msg); failures++
#define TEST_START printf("\n=== Running Environment Variable Tests ===\n\n")
#define TEST_END printf("\n=== Test Summary ===\nTotal failures: %d\n\n", failures)

int failures = 0;

/* Test 1: Set simple variable */
void test_set_simple_variable(void)
{
    char *args[] = {"set", "TEST_VAR", "test_value", NULL};
    int result = filson_set(args);
    
    if (result != 1) {
        TEST_FAIL("set_simple_variable", "Expected return value 1");
        return;
    }
    
    char *value = getenv("TEST_VAR");
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

/* Test 2: Overwrite existing variable */
void test_overwrite_variable(void)
{
    char *args[] = {"set", "OVERWRITE_TEST", "value1", NULL};
    filson_set(args);
    
    char *value1 = getenv("OVERWRITE_TEST");
    if (strcmp(value1, "value1") != 0) {
        TEST_FAIL("overwrite_variable", "First set failed");
        return;
    }
    
    char *args2[] = {"set", "OVERWRITE_TEST", "value2", NULL};
    int result = filson_set(args2);
    
    if (result != 1) {
        TEST_FAIL("overwrite_variable", "Expected return value 1 on overwrite");
        return;
    }
    
    char *value2 = getenv("OVERWRITE_TEST");
    if (strcmp(value2, "value2") != 0) {
        TEST_FAIL("overwrite_variable", "Overwrite failed");
        return;
    }
    
    TEST_PASS("overwrite_variable");
}

/* Test 3: Set PATH variable */
void test_set_path_variable(void)
{
    char *args[] = {"set", "PATH", "/custom/bin:/usr/bin:/bin", NULL};
    int result = filson_set(args);
    
    if (result != 1) {
        TEST_FAIL("set_path_variable", "Expected return value 1");
        return;
    }
    
    char *value = getenv("PATH");
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

/* Test 4: Missing variable name */
void test_missing_variable_name(void)
{
    char *args[] = {"set", NULL, NULL};
    
    /* Redirect stderr to /dev/null to suppress error message during test */
    int stderr_backup = dup(2);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, 2);
    
    int result = filson_set(args);
    
    /* Restore stderr */
    dup2(stderr_backup, 2);
    close(devnull);
    close(stderr_backup);
    
    if (result != 1) {
        TEST_FAIL("missing_variable_name", "Expected return value 1");
        return;
    }
    
    TEST_PASS("missing_variable_name");
}

/* Test 5: Missing variable value */
void test_missing_variable_value(void)
{
    char *args[] = {"set", "VAR_NAME", NULL};
    
    /* Redirect stderr to /dev/null */
    int stderr_backup = dup(2);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, 2);
    
    int result = filson_set(args);
    
    /* Restore stderr */
    dup2(stderr_backup, 2);
    close(devnull);
    close(stderr_backup);
    
    if (result != 1) {
        TEST_FAIL("missing_variable_value", "Expected return value 1");
        return;
    }
    
    TEST_PASS("missing_variable_value");
}

/* Test 6: Child process inherits environment variable */
void test_child_inherits_env(void)
{
    /* Set a test variable */
    char *args[] = {"set", "CHILD_TEST", "inherited_value", NULL};
    filson_set(args);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        /* Child process */
        char *value = getenv("CHILD_TEST");
        if (value != NULL && strcmp(value, "inherited_value") == 0) {
            exit(EXIT_SUCCESS);
        } else {
            exit(EXIT_FAILURE);
        }
    } else if (pid > 0) {
        /* Parent process */
        int status;
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

/* Test 7: Multiple variables */
void test_multiple_variables(void)
{
    char *args1[] = {"set", "VAR1", "value1", NULL};
    char *args2[] = {"set", "VAR2", "value2", NULL};
    char *args3[] = {"set", "VAR3", "value3", NULL};
    
    filson_set(args1);
    filson_set(args2);
    filson_set(args3);
    
    char *val1 = getenv("VAR1");
    char *val2 = getenv("VAR2");
    char *val3 = getenv("VAR3");
    
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

/* Test 8: Empty string value */
void test_empty_string_value(void)
{
    char *args[] = {"set", "EMPTY_VAR", "", NULL};
    int result = filson_set(args);
    
    if (result != 1) {
        TEST_FAIL("empty_string_value", "Expected return value 1");
        return;
    }
    
    char *value = getenv("EMPTY_VAR");
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

/* Test 9: Variable with special characters */
void test_special_characters_in_value(void)
{
    char *args[] = {"set", "SPECIAL_VAR", "!@#$%^&*()", NULL};
    int result = filson_set(args);
    
    if (result != 1) {
        TEST_FAIL("special_characters_in_value", "Expected return value 1");
        return;
    }
    
    char *value = getenv("SPECIAL_VAR");
    if (value == NULL || strcmp(value, "!@#$%^&*()") != 0) {
        TEST_FAIL("special_characters_in_value", "Special characters not preserved");
        return;
    }
    
    TEST_PASS("special_characters_in_value");
}

/* Test 10: Case sensitivity */
void test_case_sensitivity(void)
{
    char *args1[] = {"set", "CaseSensitive", "value1", NULL};
    char *args2[] = {"set", "casesensitive", "value2", NULL};
    
    filson_set(args1);
    filson_set(args2);
    
    char *val1 = getenv("CaseSensitive");
    char *val2 = getenv("casesensitive");
    
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

int main(void)
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
    
    TEST_END;
    
    return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
