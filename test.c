#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test.h"

extern int filson_last_cmd_success;

static int
filson_test_file_exists(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	struct stat sb;

	return stat(path, &sb) == 0;
}

static int
filson_test_is_file(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	struct stat sb;

	if (stat(path, &sb) != 0) {
		return 0;
	}
	return S_ISREG(sb.st_mode);
}

static int
filson_test_is_dir(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	struct stat sb;

	if (stat(path, &sb) != 0) {
		return 0;
	}
	return S_ISDIR(sb.st_mode);
}

static int
filson_test_is_readable(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	return access(path, R_OK) == 0;
}

static int
filson_test_is_writable(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	return access(path, W_OK) == 0;
}

static int
filson_test_is_executable(const char *path)
{
	assert(path != NULL);
	assert(path[0] != '\0');
	return access(path, X_OK) == 0;
}

static int
filson_test_string_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	return strcmp(s1, s2) == 0;
}

static int
filson_test_string_not_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	if (s1 == NULL || s2 == NULL) {
		return 1;
	}
	return strcmp(s1, s2) != 0;
}

static int
filson_test_string_empty(const char *s)
{
	assert(s != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	return s == NULL || s[0] == '\0';
}

static int
filson_test_string_not_empty(const char *s)
{
	assert(s != NULL);
	assert(filson_last_cmd_success == 0 || filson_last_cmd_success == 1);
	return s != NULL && s[0] != '\0';
}

static int
filson_test_int_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 0;
	}
	return v1 == v2;
}

static int
filson_test_int_not_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 1;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 1;
	}
	return v1 != v2;
}

static int
filson_test_int_less_than(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 0;
	}
	return v1 < v2;
}

static int
filson_test_int_less_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 0;
	}
	return v1 <= v2;
}

static int
filson_test_int_greater_than(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 0;
	}
	return v1 > v2;
}

static int
filson_test_int_greater_equal(const char *s1, const char *s2)
{
	assert(s1 != NULL);
	assert(s2 != NULL);
	long v1, v2;
	char *endp1, *endp2;

	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	v1 = strtol(s1, &endp1, 10);
	v2 = strtol(s2, &endp2, 10);
	if (*endp1 != '\0' || *endp2 != '\0') {
		return 0;
	}
	return v1 >= v2;
}

int
static int
filson_test_unary(char op, const char *arg)
{
	assert(arg != NULL);
	assert(sizeof(char) == 1);
	switch (op) {
	case 'f': return filson_test_is_file(arg);
	case 'd': return filson_test_is_dir(arg);
	case 'e': return filson_test_file_exists(arg);
	case 'r': return filson_test_is_readable(arg);
	case 'w': return filson_test_is_writable(arg);
	case 'x': return filson_test_is_executable(arg);
	case 'z': return filson_test_string_empty(arg);
	case 'n': return filson_test_string_not_empty(arg);
	default:
		fprintf(stderr, "filson: test: unknown unary operator: -%c\n", op);
		return -1;
	}
}

static int
filson_test_binary(const char *left, const char *op, const char *right)
{
	assert(left != NULL);
	assert(op != NULL);
	if (strcmp(op, "=") == 0)   return filson_test_string_equal(left, right);
	if (strcmp(op, "!=") == 0)  return filson_test_string_not_equal(left, right);
	if (strcmp(op, "-eq") == 0) return filson_test_int_equal(left, right);
	if (strcmp(op, "-ne") == 0) return filson_test_int_not_equal(left, right);
	if (strcmp(op, "-lt") == 0) return filson_test_int_less_than(left, right);
	if (strcmp(op, "-le") == 0) return filson_test_int_less_equal(left, right);
	if (strcmp(op, "-gt") == 0) return filson_test_int_greater_than(left, right);
	if (strcmp(op, "-ge") == 0) return filson_test_int_greater_equal(left, right);
	fprintf(stderr, "filson: test: unknown binary operator: %s\n", op);
	return -1;
}

int
filson_test(char **args)
{
	assert(args != NULL);
	assert(args[0] != NULL);
	int result, argc;

	argc = 0;
	while (args[argc] != NULL)
		argc++;

	if (argc < 2) {
		filson_last_cmd_success = 0;
		return 1;
	}

	if (args[1][0] == '-' && args[1][1] != '\0' && args[1][2] == '\0') {
		if (argc < 3) {
			filson_last_cmd_success = 0;
			return 1;
		}
		result = filson_test_unary(args[1][1], args[2]);
	} else if (argc >= 3) {
		result = filson_test_binary(args[1], args[2],
		    (argc > 3) ? args[3] : NULL);
	} else {
		result = filson_test_string_not_empty(args[1]);
	}

	if (result < 0) {
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_last_cmd_success = result;
	return 1;
}
