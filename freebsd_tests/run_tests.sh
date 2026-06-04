#!/bin/sh
TESTDIR="$(cd "$(dirname "$0")" && pwd)"
SHELL_BIN="${1:-$TESTDIR/../filson}"
TMPPASS="$(mktemp)"
TMPFAIL="$(mktemp)"
echo 0 > "$TMPPASS"
echo 0 > "$TMPFAIL"

run_one() {
	testfile="$1"
	name="${testfile#$TESTDIR/}"
	ext="${testfile##*.}"
	base="${testfile%.*}"
	expected_exit="$ext"

	stdout_file=""
	stderr_file=""
	[ -f "${base}.${ext}.stdout" ] && stdout_file="${base}.${ext}.stdout"
	[ -f "${base}.${ext}.stderr" ] && stderr_file="${base}.${ext}.stderr"

	actual_stdout="$(mktemp)"
	actual_stderr="$(mktemp)"

	timeout 5s "$SHELL_BIN" "$testfile" >"$actual_stdout" 2>"$actual_stderr"
	actual_exit=$?
	if [ "$actual_exit" = "124" ]; then
		rm -f "$actual_stdout" "$actual_stderr"
		f=$(cat "$TMPFAIL")
		echo $((f + 1)) > "$TMPFAIL"
		echo "FAIL $name (timeout)"
		return
	fi

	result="PASS"
	reason=""

	if [ "$actual_exit" != "$expected_exit" ]; then
		result="FAIL"
		reason="exit $actual_exit (expected $expected_exit)"
	fi

	if [ "$result" = "PASS" ] && [ -n "$stdout_file" ]; then
		if ! diff -q "$stdout_file" "$actual_stdout" >/dev/null 2>&1; then
			result="FAIL"
			reason="stdout mismatch"
		fi
	fi

	if [ "$result" = "PASS" ] && [ -n "$stderr_file" ]; then
		if ! diff -q "$stderr_file" "$actual_stderr" >/dev/null 2>&1; then
			result="FAIL"
			reason="stderr mismatch"
		fi
	fi

	rm -f "$actual_stdout" "$actual_stderr"

	if [ "$result" = "PASS" ]; then
		p=$(cat "$TMPPASS")
		echo $((p + 1)) > "$TMPPASS"
		echo "PASS $name"
	else
		f=$(cat "$TMPFAIL")
		echo $((f + 1)) > "$TMPFAIL"
		echo "FAIL $name ($reason)"
	fi
}

for f in $(find "$TESTDIR/builtins" "$TESTDIR/expansion" -type f \
	| grep -E '\.[0-9]+$' \
	| sort); do
	run_one "$f"
done

pass=$(cat "$TMPPASS")
fail=$(cat "$TMPFAIL")
total=$((pass + fail))
rm -f "$TMPPASS" "$TMPFAIL"

echo "---"
echo "PASSED: $pass / $total"
echo "FAILED: $fail / $total"
