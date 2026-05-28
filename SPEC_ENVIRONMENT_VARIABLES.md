# Environment Variables Feature - Specification

## Feature: Set Environment Variables (filson_set)

### Command: set
```
set <variable_name> <variable_value>
```

### Description
Sets an environment variable in the Filson shell. The variable is then inherited by all child processes spawned from the shell.

### Parameters
| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| variable_name | string | Name of environment variable (e.g., PATH, MYVAR) | Yes |
| variable_value | string | Value to assign to the variable | Yes |

### Return Value
- **1** on success
- **1** on failure (error printed to stderr)

### Exit Behavior
- Function always returns 1, allowing the shell loop to continue

### Error Cases

| Error | Message | Cause |
|-------|---------|-------|
| Missing variable_name | `filson: expected arguments to "set" <var> <value>` | `args[1]` is NULL |
| Missing variable_value | `filson: expected arguments to "set" <var> <value>` | `args[2]` is NULL |
| setenv() system error | System-specific error via perror() | OS call fails (rare) |

### Examples

#### Example 1: Set Simple Variable
```
Input:  set GREETING hello
Output: (no output on success)
Result: GREETING=hello in environment
```

#### Example 2: Set PATH
```
Input:  set PATH /custom/bin:/usr/bin:/bin
Output: (no output on success)
Result: PATH=/custom/bin:/usr/bin:/bin in environment
```

#### Example 3: Missing Argument
```
Input:  set ONLYNAME
Output: filson: expected arguments to "set" <var> <value>
Result: Variable not set, shell continues
```

#### Example 4: No Arguments
```
Input:  set
Output: filson: expected arguments to "set" <var> <value>
Result: Variable not set, shell continues
```

### Inheritance Model

**Parent Process (Shell):**
- Maintains environment with all set variables
- Changes persist for shell session lifetime

**Child Process (Spawned Command):**
- Inherits copy of parent environment via fork()
- Any environment changes in child are isolated to that process
- Child process cannot modify parent shell's environment

### Implementation Notes

1. **Overwrite Behavior:** If variable already exists, it is replaced (setenv flag = 1)
2. **No Variable Expansion:** The `set` command does not expand `$VAR` syntax; it treats value literally
3. **No Quoting Support:** Spaces in value are treated as token separators by `filson_split_line()`
4. **Case Sensitivity:** Variable names are case-sensitive (PATH ≠ path)
5. **Special Characters:** Variable values containing spaces must be handled by future quote support

### Constraints

- Variable names and values must not contain spaces (unless future quote support added)
- Variable names should follow standard Unix naming conventions (alphanumeric and underscore)
- No validation of variable name or value format—OS will handle

### Related Functionality

- **Command Execution:** Child processes inherit environment via `filson_launch()` → `fork()` → `execvp()`
- **Built-in Commands:** `set` is a built-in command, not forked as separate process
- **Shell Loop:** `filson_loop()` continues after `set` command executes

### Testing Requirements

1. Verify variable is set in shell's environment
2. Verify child processes can access set variables
3. Verify error messages on missing arguments
4. Verify overwriting existing variables
5. Verify multiple variables can be set
6. Verify special variables like PATH work correctly
