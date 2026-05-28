# Environment Variables Feature - Testing Guide

## Overview
This directory contains a comprehensive test suite for the Filson shell environment variables feature (#5).

## Files

| File | Purpose |
|------|---------|
| `filson.c` | Main shell implementation with `filson_set()` function |
| `DESIGN_ENVIRONMENT_VARIABLES.md` | Architecture and design decisions |
| `SPEC_ENVIRONMENT_VARIABLES.md` | Detailed specification and requirements |
| `test_environment_variables.c` | Comprehensive unit tests (10 test cases) |
| `TEST_GUIDE.md` | This file - testing instructions |

## Building Tests

### Compile the test suite with filson.c

```bash
gcc -o test_env test_environment_variables.c filson.c -Wall -Wextra
```

**Note:** The test file includes the `filson_set()` function directly. You may need to extract `filson_set()` into a separate `filson_env.c` file to avoid symbol conflicts if compiling with other Filson functions.

### Compile main Filson shell

```bash
gcc -o filson filson.c -Wall -Wextra
```

## Running Tests

### Run unit tests

```bash
./test_env
```

**Expected output:**
```
=== Running Environment Variable Tests ===

[PASS] set_simple_variable
[PASS] overwrite_variable
[PASS] set_path_variable
[PASS] missing_variable_name
[PASS] missing_variable_value
[PASS] child_inherits_env
[PASS] multiple_variables
[PASS] empty_string_value
[PASS] special_characters_in_value
[PASS] case_sensitivity

=== Test Summary ===
Total failures: 0
```

### Run manual integration tests in shell

```bash
./filson
filson> set MYVAR hello
filson> echo $MYVAR
hello
filson> set PATH /custom/bin:/usr/bin:/bin
filson> which echo
/usr/bin/echo
filson> help
```

## Test Coverage

### Test Cases (10 total)

1. **test_set_simple_variable** ✓
   - Tests basic `set VAR value` functionality
   - Verifies variable appears in environment

2. **test_overwrite_variable** ✓
   - Tests overwriting existing variable
   - Verifies new value replaces old value

3. **test_set_path_variable** ✓
   - Tests setting PATH specifically
   - Important for shell functionality

4. **test_missing_variable_name** ✓
   - Tests error handling when variable name missing
   - Verifies graceful error with stderr suppression

5. **test_missing_variable_value** ✓
   - Tests error handling when value missing
   - Verifies graceful error with stderr suppression

6. **test_child_inherits_env** ✓
   - Tests environment inheritance via fork/exec
   - Verifies child processes see set variables

7. **test_multiple_variables** ✓
   - Tests setting multiple variables
   - Verifies all persist correctly

8. **test_empty_string_value** ✓
   - Tests setting empty string as value
   - Edge case handling

9. **test_special_characters_in_value** ✓
   - Tests special characters in values
   - Verifies no shell interpretation

10. **test_case_sensitivity** ✓
    - Tests case-sensitive variable names
    - Verifies PATH ≠ path

## Expected Test Results

All 10 tests should PASS when:
- `filson_set()` is properly implemented
- Environment variables are correctly set via `setenv()`
- Child processes properly inherit environment
- Error handling is implemented for missing arguments

## Test Methodology

### Unit Testing
- Direct function calls with controlled inputs
- Environment state verification with `getenv()`
- Return value checks
- Error condition handling

### Integration Testing
- Manual shell interaction
- Command execution with inherited environment
- Built-in command functionality

## Interpreting Failures

| Failure | Cause | Fix |
|---------|-------|-----|
| set_simple_variable fails | `setenv()` not working or filson_set() broken | Check filson_set() implementation |
| child_inherits_env fails | Environment not propagated to children | Verify fork() → execvp() process |
| missing_variable_* fails | Error handling broken or return value wrong | Check argument validation in filson_set() |
| multiple_variables fails | Environment not persisting | Check setenv() not clearing previous vars |

## Running in CI/CD

To run tests in continuous integration:

```bash
#!/bin/bash
gcc -o test_env test_environment_variables.c filson.c -Wall -Wextra
./test_env
if [ $? -ne 0 ]; then
    echo "Tests FAILED"
    exit 1
fi
echo "All tests PASSED"
exit 0
```

## Limitations & Future Work

### Current Limitations
- Tests assume single-threaded shell
- No test for signal handling interactions
- No test for resource exhaustion (too many variables)
- Cannot test concurrent shell instances

### Future Test Cases
- Performance test: Setting 1000+ variables
- Stress test: Rapidly creating/destroying child processes
- Signal handling: Behavior with SIGTERM, SIGKILL
- Memory leak detection with valgrind
- Thread safety (if implementing multi-threaded shell)

## Debugging Tests

### Run with verbose output
```bash
gcc -o test_env test_environment_variables.c filson.c -Wall -Wextra -DDEBUG
```

### Use gdb for breakpoint debugging
```bash
gdb ./test_env
(gdb) break filson_set
(gdb) run
(gdb) step
```

### Check environment at runtime
```c
extern char **environ;
for (int i = 0; environ[i]; i++) {
    printf("%s\n", environ[i]);
}
```

## Checklist for Feature Validation

- [ ] Unit tests compile without errors
- [ ] All 10 unit tests pass
- [ ] Manual shell testing works
- [ ] `set` command appears in `help` output
- [ ] Variables persist across multiple `set` commands
- [ ] Child processes receive environment variables
- [ ] Error messages display on invalid input
- [ ] No memory leaks (valgrind clean)
- [ ] Code follows project style guidelines
