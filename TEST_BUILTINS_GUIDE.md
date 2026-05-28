# Filson Shell Built-in Unit Tests

## Overview

This test suite provides comprehensive unit testing for all built-in commands in the Filson shell:
- **cd** - Change directory (3 tests)
- **help** - Display help (2 tests)
- **exit** - Exit shell (1 test)
- **set** - Set environment variables (6 tests)
- **echo** - Echo with variable expansion (7 tests)

**Total: 19 unit tests**

## Building the Tests

Compile the test suite:

```bash
gcc -o test_builtins test_builtins.c filson.c -Wall -Wextra
```

## Running the Tests

Execute the test suite:

```bash
./test_builtins
```

Expected output:
```
╔════════════════════════════════════════╗
║   FILSON SHELL BUILT-IN UNIT TESTS   ║
╚════════════════════════════════════════╝

=== CD COMMAND ===
[PASS] cd_no_args
[PASS] cd_valid_directory
[PASS] cd_invalid_directory

=== HELP COMMAND ===
[PASS] help_lists_builtins
[PASS] help_return_value

=== EXIT COMMAND ===
[PASS] exit_return_value

=== SET COMMAND ===
[PASS] set_simple_variable
[PASS] set_overwrite_variable
[PASS] set_no_variable_name
[PASS] set_no_value
[PASS] set_empty_value
[PASS] set_special_chars

=== ECHO COMMAND ===
[PASS] echo_literal_text
[PASS] echo_variable_expansion
[PASS] echo_mixed_text_and_vars
[PASS] echo_undefined_variable
[PASS] echo_no_args
[PASS] echo_multiple_variables
[PASS] echo_return_value

╔════════════════════════════════════════╗
║          TEST RESULTS SUMMARY         ║
╠════════════════════════════════════════╣
║ PASSED: 19                            ║
║ FAILED: 0                             ║
║ TOTAL:  19                            ║
╚════════════════════════════════════════╝
```

## Test Details

### CD Command (3 tests)
- **cd_no_args** - Verify error handling when no directory specified
- **cd_valid_directory** - Test successful directory change
- **cd_invalid_directory** - Test error on non-existent directory

### HELP Command (2 tests)
- **help_lists_builtins** - Verify help prints built-in commands
- **help_return_value** - Verify return value is 1

### EXIT Command (1 test)
- **exit_return_value** - Verify exit returns 0 (exit shell)

### SET Command (6 tests)
- **set_simple_variable** - Basic variable setting
- **set_overwrite_variable** - Overwriting existing variable
- **set_no_variable_name** - Error when variable name missing
- **set_no_value** - Error when value missing
- **set_empty_value** - Setting empty string as value
- **set_special_chars** - Variables with special characters

### ECHO Command (7 tests)
- **echo_literal_text** - Printing literal text
- **echo_variable_expansion** - Expanding $VAR syntax
- **echo_mixed_text_and_vars** - Combining literals and variables
- **echo_undefined_variable** - Handling undefined variables
- **echo_no_args** - Echo with no arguments (just newline)
- **echo_multiple_variables** - Expanding multiple variables
- **echo_return_value** - Verify return value is 1

## Test Implementation Details

### I/O Capture
Tests capture stdout/stderr using file descriptors to verify output without polluting the console.

### Error Suppression
Error messages are redirected to `/dev/null` during tests to keep output clean.

### Temporary Files
Some tests write to `/tmp/` files and clean them up after testing.

### Environment Management
- Tests set environment variables for testing purposes
- Each test is self-contained
- Previous state is restored after tests

## Troubleshooting

### Test Fails
1. Verify filson.c compiles without errors
2. Check that all function signatures match
3. Ensure `/tmp/` directory is writable
4. Run with verbose output: Add `-DDEBUG` flag to gcc

### Specific Test Failures

| Test | Cause | Fix |
|------|-------|-----|
| cd_* fails | Current directory issues | Ensure you have permission to /tmp |
| help_* fails | Output format changed | Check filson_help() implementation |
| set_* fails | setenv() issues | Verify environment variable handling |
| echo_* fails | Output capture issues | Check /tmp/ permissions |

## Performance

All tests run in < 1 second total.

## Integration with CI/CD

```bash
#!/bin/bash
set -e

echo "Building tests..."
gcc -o test_builtins test_builtins.c filson.c -Wall -Wextra

echo "Running tests..."
./test_builtins

if [ $? -eq 0 ]; then
    echo "✓ All tests passed"
    exit 0
else
    echo "✗ Tests failed"
    exit 1
fi
```

## Future Test Enhancements

- Memory leak detection with valgrind
- Performance benchmarking
- Signal handling tests
- Resource exhaustion tests
- Integration tests with shell loop
