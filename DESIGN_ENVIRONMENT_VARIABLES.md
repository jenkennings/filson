# Environment Variables Feature - Design Document

## Feature Overview
This document describes the design and implementation of environment variable support in Filson shell (#5).

## Objective
Enable users to create, modify, and manage environment variables that persist across the shell session and are inherited by child processes.

## Design

### Architecture
The environment variable feature integrates with the existing built-in command system. No structural changes to the shell architecture are required—it leverages existing OS mechanisms.

```
User Input (set VAR VALUE)
    ↓
filson_execute()
    ↓
Matches "set" in builtin_str[]
    ↓
Calls filson_set(args)
    ↓
setenv(VAR, VALUE, 1)  [overwrite if exists]
    ↓
Shell environment updated
    ↓
Child processes inherit via fork()→execvp()
```

### Implementation Details

**Function Signature:**
```c
int filson_set(char **args)
```

**Parameters:**
- `args[0]`: "set" (command name)
- `args[1]`: Variable name (e.g., "PATH", "MYVAR")
- `args[2]`: Variable value (e.g., "/usr/bin:/bin")

**Behavior:**
- Validates that both variable name and value are provided
- Uses `setenv(name, value, 1)` to set environment variable with overwrite flag=1
- Prints error to stderr if setenv fails
- Always returns 1 to continue shell loop

**Error Handling:**
- Missing arguments: Print error message to stderr
- setenv() failure: Use perror() to print system error

### Memory Management
- No dynamic allocation in filson_set()
- Environment variables are managed by OS (no memory leaks)
- No cleanup required on shell exit—OS reclaims environment

### Child Process Inheritance
When a command is executed via `filson_launch()`:
1. `fork()` creates child process with copy of parent environment
2. `execvp()` replaces child process image with new program
3. Child inherits all environment variables set in parent shell
4. Parent process continues with its environment intact

**Note:** Changes to environment in child process do NOT affect parent shell.

### Scope
- **Local Scope:** Environment variables persist for the lifetime of the shell session
- **Persistence:** Variables survive until shell exits or are overwritten
- **Propagation:** All spawned processes inherit the full environment

## Usage Examples

```bash
filson> set PATH /usr/bin:/bin:/usr/local/bin
filson> set MYVAR hello
filson> set DEBUG 1
filson> echo $MYVAR        # Child process sees MYVAR
hello
filson> set PATH /custom   # Overwrites existing PATH
```

## Testing Strategy

See `SPEC_ENVIRONMENT_VARIABLES.md` and `test_environment_variables.c` for comprehensive test coverage.

## Future Enhancements

1. **Get Variables:** Implement `get VAR` command to print variable value
2. **Unset Variables:** Implement `unset VAR` command to remove variables
3. **Export Variables:** Distinguish local vs exported variables (bash-like behavior)
4. **Variable Expansion:** Support `$VAR` syntax in commands
5. **Alias Support:** Implement `alias` for command shortcuts

## Implementation Notes

- No POSIX functions beyond standard C library are required
- `setenv()` is POSIX.1-2001 and widely available on Unix systems
- Implementation follows existing Filson command pattern for consistency
- Feature is fully self-contained with no side effects on other shell functions
