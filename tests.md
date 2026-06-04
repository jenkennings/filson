# FreeBSD sh Test Suite — Filson Results

**Date:** 2026-06-04  
**Shell under test:** filson (built from HEAD)  
**Test source:** FreeBSD src `bin/sh/tests/` (builtins, expansion)  
**Runner:** `freebsd_tests/run_tests.sh`  
**Total:** 30 tests — **15 PASS / 15 FAIL (50%)**

---

## Summary Table

| Test | Category | Result | Reason |
|------|----------|--------|--------|
| builtins/alias.0 | alias | FAIL | stdout mismatch — quoted alias values |
| builtins/alias.1 | alias | FAIL | exit 0 (expected 1) — unalias of nonexistent |
| builtins/alias3.0 | alias | FAIL | stdout mismatch — alias list format |
| builtins/break1.0 | break | **PASS** | |
| builtins/break3.0 | break | **PASS** | |
| builtins/break6.0 | break | **PASS** | |
| builtins/cd1.0 | cd | FAIL | timeout — `set -e` and symlink handling |
| builtins/cd2.0 | cd | FAIL | timeout — deep directory loop |
| builtins/cd9.0 | cd | FAIL | timeout — `cd -` (OLDPWD) hangs filson |
| builtins/echo1.0 | echo | **PASS** | |
| builtins/echo2.0 | echo | FAIL | timeout — `echo -e` escape sequences |
| builtins/echo3.0 | echo | **PASS** | |
| builtins/export1.0 | export | **PASS** | |
| builtins/for1.0 | for | **PASS** | |
| builtins/for2.0 | for | **PASS** | |
| builtins/for3.0 | for | **PASS** | |
| builtins/local1.0 | local | **PASS** | |
| builtins/local2.0 | local | FAIL | timeout — nested function scoping |
| builtins/local3.0 | local | FAIL | timeout — nested function scoping |
| builtins/return1.0 | return | **PASS** | |
| builtins/return4.0 | return | **PASS** | |
| builtins/return5.0 | return | **PASS** | |
| expansion/arith1.0 | arithmetic | FAIL | **HEAP CORRUPTION** + timeout |
| expansion/arith2.0 | arithmetic | FAIL | timeout — `$((1<<N))` parsed as heredoc |
| expansion/assign1.0 | assignment | FAIL | timeout — `eval` not a builtin |
| expansion/plus-minus1.0 | parameter | FAIL | timeout — `eval` not a builtin |
| expansion/plus-minus2.0 | parameter | **PASS** | |
| expansion/tilde1.0 | tilde | **PASS** | |
| expansion/trim1.0 | trim | FAIL | timeout — `set -f`, `eval` not implemented |
| expansion/trim2.0 | trim | FAIL | timeout — `set -f`, `eval` not implemented |

---

## Passing Tests (15)

These features work correctly in filson:

- `break` in loop and sourced-file contexts (break1, break3, break6)
- `echo -n` flag (echo1)
- `echo -e` with `\c` stop-output flag (echo3)
- `export -p` (export1)
- `for` loops including exit status propagation (for1, for2, for3)
- `local` basic variable scoping (local1)
- `return` from functions (return1, return4, return5)
- Simple parameter expansion `${e:-default}` (plus-minus2)
- Tilde expansion `~` and `~/path` (tilde1)

---

## Failing Tests — Detailed

### 1. Alias quoted-value parsing

**Tests:** alias.0, alias3.0

Filson does not accept quoted values with spaces in alias definitions:

```
alias quux="1 2 3"
```

Filson splits this at the space: `alias` receives `quux="1` as the name and `2` and `3` as extra tokens, issuing `invalid alias name`. POSIX sh requires the entire quoted string to be the alias value.

**Expected format:**
```
bar=''
foo=bar
quux='1 2 3'
```

**Filson format:** no output (all alias calls fail silently or with errors to stderr)

**Fix required:** alias parser must treat the value as a single token when quoted.

---

### 2. Unalias exit code

**Test:** alias.1

`unalias nonexistent` should return exit code 1. Filson returns 0.

**Fix required:** `filson_alias` should return a non-zero exit code when the named alias does not exist.

---

### 3. `cd -` (OLDPWD) hangs filson

**Test:** cd9.0

```sh
cd /dev
cd /bin
cd - >/dev/null
pwd
```

`cd -` is not implemented. Filson does not track `OLDPWD`. Instead of erroring, it hangs indefinitely — likely blocking on stdin waiting for more input after a parse error on `-`.

**Fix required:** Implement `OLDPWD` tracking in `filson_cd`; return error if `OLDPWD` is unset.

---

### 4. Arithmetic left-shift `<<` treated as heredoc

**Tests:** arith1.0, arith2.0

```sh
echo $((1<<40))
```

The `<<` token inside `$((...))` is misidentified as a heredoc operator. The arithmetic evaluator never sees the left-shift. The shell then waits for a heredoc delimiter, causing a hang.

```
filson: command substitution error
```

**Fix required:** The parser must recognise that inside `$((...))` the `<<` is an arithmetic operator, not a pipeline operator.

---

### 5. Heap corruption in arithmetic with positional parameters

**Test:** arith1.0

When the `check()` function runs `$(($1))` with a string like `"0&&0"`, filson produces:

```
free(): invalid pointer
double free or corruption (out)
```

This is a memory safety bug. Expanding a positional parameter inside arithmetic expression context corrupts the allocator state. This is likely in `parameter_expansion.c` or `expansion.c` — the string result of `$1` is freed while still referenced in the arithmetic evaluator's expression buffer.

**Severity: CRITICAL** — exploitable via crafted script input.

---

### 6. `eval` not a builtin

**Tests:** assign1.0, plus-minus1.0, trim1.0, trim2.0

Many expansion tests use `eval` to exercise parameter expansion in different contexts:

```sh
eval "$code"
```

Filson does not implement `eval`. Attempting to run it tries to execute `eval` as an external binary, fails with `eval: No such file or directory`, and the test exits with an unexpected code or triggers a double-free.

**Fix required:** Implement `eval` as a builtin that passes the concatenated arguments back through `filson_execute_and_chain`.

---

### 7. `set -e` and `set -f` not implemented

**Tests:** cd1.0, trim1.0, trim2.0

`set -e` (exit on error) and `set -f` (disable glob expansion) are used in several tests. Filson's `set` builtin does not implement these flags. Tests using them either produce wrong results or fall through to unexpected behaviour.

**Fix required:** Add `-e` (errexit) and `-f` (noglob) flag tracking to the shell's option state; honour them in `filson_execute` and `filson_expand_globs`.

---

### 8. Nested function local variable scoping

**Tests:** local2.0, local3.0

Both tests timeout. These tests use nested function calls where `local` variables in a callee must not affect the caller's scope. The timeout suggests either infinite recursion or a hang in the call stack management.

**Fix required:** Investigate `filson_call_function` and `runtime_state.c` for correct local variable save/restore on nested calls.

---

## Critical Bugs Found

| Severity | Bug | Trigger |
|----------|-----|---------|
| CRITICAL | Heap corruption / double-free | `$(($1))` with string positional param in arithmetic |
| HIGH | `<<` inside `$((...))` misidentified as heredoc | `$((1<<N))` |
| HIGH | `cd -` hangs instead of erroring | `cd -` when OLDPWD unset |
| MEDIUM | `eval` not implemented | `eval "expression"` |
| MEDIUM | `alias` rejects quoted values with spaces | `alias name="a b c"` |
| LOW | `unalias nonexistent` exits 0 | `unalias nosuchname` |
| LOW | `set -e` / `set -f` ignored | `set -e`, `set -f` |

---

## How to Reproduce

```sh
make
sh freebsd_tests/run_tests.sh
```

To run a single test:

```sh
./filson freebsd_tests/expansion/arith1.0
```

To test heap corruption directly:

```sh
echo 'f() { echo $(($1)); }; f "0&&0"' | ./filson
```

---

## What the FreeBSD Tests Cover

The selected tests come from two subdirectories:

- **builtins/** — individual builtin commands: alias, break, cd, echo, export, for, local, return
- **expansion/** — parameter and arithmetic expansion: `$((...))`, `${v:-default}`, `${v:=default}`, `${v#pattern}`, `${v%pattern}`, tilde

Tests use the FreeBSD format: a file named `name.N` is a shell script expected to exit with code `N`. Optional `name.N.stdout` and `name.N.stderr` files specify expected output.

The test runner (`freebsd_tests/run_tests.sh`) runs each test with a 5-second timeout and checks exit code, stdout, and stderr against expectations.
