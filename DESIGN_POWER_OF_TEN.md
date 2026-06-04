# Power of Ten Alignment Plan

## The Rules (JPL/Holzmann)

1. Restrict to simple control flow — no goto, no recursion
2. All loops must have a fixed, statically verifiable upper bound
3. No dynamic memory allocation after initialization
4. No function longer than what fits on one printed page (~60 lines)
5. At minimum two assertions per function
6. Declare data at the smallest possible scope
7. Check the return value of every non-void function
8. Preprocessor use limited to file inclusion and simple macros
9. No function pointers
10. Compile with all warnings enabled; zero warnings tolerated

---

## Current Status by Rule

### Rule 1 — No recursion, no goto

**Status: FAILING**

`filson_execute` → `filson_run_command_only` → `filson_call_function` →
re-enters `filson_execute` to run a shell function body. This is indirect
recursion with no depth limit. A malicious or buggy script can overflow
the stack.

**Plan:**
- Add `filson_call_depth` counter (global int, initialized to 0)
- Increment on entry to `filson_call_function`, decrement on exit
- Reject with `warnx` if depth exceeds a fixed constant (e.g. 8, matching
  the existing `FILSON_MAX_CALL_DEPTH` in runtime_state.c)
- Audit all modules for additional recursion paths (pipelines.c command
  substitution likely re-enters the same call chain)

---

### Rule 2 — Fixed upper bound on all loops

**Status: FAILING**

Two violations:

1. `filson_is_valid_varname`: `for (i = 1; name[i] != '\0'; i++)` walks
   unbounded memory. No maximum name length is enforced before the loop.

2. `filson_launch` `do...while (!WIFEXITED && !WIFSIGNALED && !WIFSTOPPED)`:
   theoretically terminates but has no hard iteration cap; a process that
   is repeatedly stopped and continued can spin indefinitely.

**Plan:**
- Define `FILSON_MAX_VARNAME_LEN 128` and add `i < FILSON_MAX_VARNAME_LEN`
  as a second loop condition in `filson_is_valid_varname`
- Add a `waitpid_attempts` counter to the `do...while` in `filson_launch`
  with a hard cap (e.g. 4096 iterations) before forcibly failing
- Audit all string-walking loops across expansion.c and parameter_expansion.c

---

### Rule 3 — No dynamic allocation after initialization

**Status: FAILING (by design)**

A shell is inherently allocation-heavy: command lines, expansions, glob
results, and history entries all require heap memory. Full compliance is
not achievable without fundamentally redesigning the shell around fixed
arena buffers.

**Pragmatic plan:**
- Document each allocation site and pair it with a definite free path
- Convert any allocation whose size can be statically bounded to a
  stack-allocated fixed buffer with an explicit size check
- Candidates: `filson_is_valid_varname` name copy, inline assignment
  `names[]` and `old_values[]` arrays (already on stack — good)
- Do not introduce `malloc` for any new features

---

### Rule 4 — No function longer than ~60 lines

**Status: PASSING (borderline)**

All functions are within range. `filson_run_command_only` is the longest
at approximately 55 lines. Monitor when adding features.

**Plan:** Enforce at review time. No function may be extended beyond 60
lines without splitting.

---

### Rule 5 — Minimum two assertions per function

**Status: FAILING**

Functions with zero assertions:
- `filson_is_valid_varname`
- `filson_arg_count`
- `filson_path_is_safe`
- `filson_num_builtins`
- `filson_execute`
- `filson_free_assignment_buffers`
- `filson_restore_assignment_environment`

Functions have only one assertion:
- `filson_run_command_only` (asserts `args != NULL` only)
- `filson_run_with_temp_assignments` (asserts `args != NULL` only)

**Plan:**
Add precondition assertions to every function. Examples:
- `filson_is_valid_varname`: assert `name != NULL`; assert `name[0]` is
  inspectable (covered by null check but should be explicit)
- `filson_arg_count`: assert `args != NULL`
- `filson_execute`: assert `args != NULL`; assert `background == 0 ||
  background == 1`
- `filson_free_assignment_buffers`: assert `count >= 0`; assert
  `names != NULL`
- `filson_restore_assignment_environment`: assert `count >= 0`; assert
  `had_old != NULL`

---

### Rule 6 — Data at smallest scope

**Status: PASSING**

All local variables are declared at function scope. The three globals
(`filson_last_cmd_success`, `filson_break_flag`, `filson_continue_flag`)
are necessary for cross-function signaling and cannot be scoped further
without architectural changes.

**Plan:** No action required. Prevent new globals from being introduced.

---

### Rule 7 — Check all return values

**Status: FAILING**

Unchecked return values:
- `unsetenv()` in `filson_restore_assignment_environment` — silently
  discards error
- `printf()` calls in `filson_launch` (job notifications) — unchecked
- `setenv()` in `filson_restore_assignment_environment` — checked in the
  forward path but not during restore

**Plan:**
- Capture `unsetenv` return and call `warn` on failure in restore function
- Decide policy on `printf` failures: acceptable to ignore in a shell
  context given stderr fallback, but should be explicit with `(void)` cast
  to document the intent
- Add `(void)` cast to all intentionally-ignored return values to
  satisfy the rule's spirit and silence `-Wunused-result`

---

### Rule 8 — Preprocessor limited to includes and simple macros

**Status: PASSING**

Only `#include` and `#define` constants. No function-like macros, no
conditional compilation, no token pasting.

**Plan:** No action required. Reject any macro that contains logic or
multiple statements.

---

### Rule 9 — No function pointers

**Status: PASSING (just fixed)**

`builtin_func[]` pointer array replaced with `filson_dispatch_builtin`
direct-call switch. All builtin dispatch is now statically resolvable.

**Plan:** No action required. Reject function pointer types in any new code.

---

### Rule 10 — Zero warnings at maximum warning level

**Status: UNKNOWN**

Current Makefile warning flags have not been verified against the full set:
`-Wall -Wextra -Wpedantic -Werror`.

**Plan:**
- Audit Makefile for current `-W` flags
- Add `-Wextra -Wpedantic` if not present
- Add `-Werror` to enforce zero-warning policy at compile time
- Fix any warnings exposed, in priority order: unused variables, implicit
  function declarations, sign comparison, missing return

---

## Execution Order

The following order minimizes risk while delivering the highest safety
return first:

1. **Rule 10** — Add `-Wextra -Wpedantic -Werror` to Makefile; fix all
   warnings. Reveals real bugs at zero behavioral cost.

2. **Rule 5** — Add assertions to all functions. Reveals contract
   violations at runtime during testing.

3. **Rule 7** — Check all return values. Low-risk, catches real failure
   paths.

4. **Rule 2** — Cap all loops. Add `FILSON_MAX_VARNAME_LEN`; cap the
   `waitpid` loop.

5. **Rule 1** — Enforce recursion depth limit in `filson_call_function`.
   Requires careful testing of nested function calls.

6. **Rule 3** — Ongoing: convert bounded allocations to stack buffers
   opportunistically, document the rest.
