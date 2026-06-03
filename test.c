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
	struct stat sb;

	return stat(path, &sb) == 0;
}

static int
filson_test_is_file(const char *path)
{
	struct stat sb;

	if (stat(path, &sb) != 0) {
		return 0;
	}
	return S_ISREG(sb.st_mode);
}

static int
filson_test_is_dir(const char *path)
{
	struct stat sb;

	if (stat(path, &sb) != 0) {
		return 0;
	}
	return S_ISDIR(sb.st_mode);
}

static int
filson_test_is_readable(const char *path)
{
	return access(path, R_OK) == 0;
}

static int
filson_test_is_writable(const char *path)
{
	return access(path, W_OK) == 0;
}

static int
filson_test_is_executable(const char *path)
{
	return access(path, X_OK) == 0;
}

static int
filson_test_string_equal(const char *s1, const char *s2)
{
	if (s1 == NULL || s2 == NULL) {
		return 0;
	}
	return strcmp(s1, s2) == 0;
}

static int
filson_test_string_not_equal(const char *s1, const char *s2)
{
	if (s1 == NULL || s2 == NULL) {
		return 1;
	}
	return strcmp(s1, s2) != 0;
}

static int
filson_test_string_empty(const char *s)
{
	return s == NULL || s[0] == '\0';
}

static int
filson_test_string_not_empty(const char *s)
{
	return s != NULL && s[0] != '\0';
}

static int
filson_test_int_equal(const char *s1, const char *s2)
{
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
filson_test(char **args)
{
	int result;
	int i;
	int argc;

	argc = 0;
	while (args[argc] != NULL) {
		argc++;
	}

	if (argc < 2) {
		filson_last_cmd_success = 0;
		return 1;
	}

	i = 1;
	result = 1;

	if (args[i][0] == '-' && args[i][1] != '\0' && args[i][2] == '\0') {
		char op = args[i][1];

		switch (op) {
		case 'f':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_is_file(args[i + 1]);
			break;
		case 'd':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_is_dir(args[i + 1]);
			break;
		case 'e':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_file_exists(args[i + 1]);
			break;
		case 'r':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_is_readable(args[i + 1]);
			break;
		case 'w':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_is_writable(args[i + 1]);
			break;
		case 'x':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_is_executable(args[i + 1]);
			break;
		case 'z':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_string_empty(args[i + 1]);
			break;
		case 'n':
			if (i + 1 >= argc) {
				filson_last_cmd_success = 0;
				return 1;
			}
			result = filson_test_string_not_empty(args[i + 1]);
			break;
		default:
			fprintf(stderr, "filson: test: unknown unary operator: -%c\n", op);
			filson_last_cmd_success = 0;
			return 1;
		}
	} else if (i + 1 < argc) {
		char *left = args[i];
		char *op = args[i + 1];
		char *right = (i + 2 < argc) ? args[i + 2] : NULL;

		if (strcmp(op, "=") == 0) {
			result = filson_test_string_equal(left, right);
		} else if (strcmp(op, "!=") == 0) {
			result = filson_test_string_not_equal(left, right);
		} else if (strcmp(op, "-eq") == 0) {
			result = filson_test_int_equal(left, right);
		} else if (strcmp(op, "-ne") == 0) {
			result = filson_test_int_not_equal(left, right);
		} else if (strcmp(op, "-lt") == 0) {
			result = filson_test_int_less_than(left, right);
		} else if (strcmp(op, "-le") == 0) {
			result = filson_test_int_less_equal(left, right);
		} else if (strcmp(op, "-gt") == 0) {
			result = filson_test_int_greater_than(left, right);
		} else if (strcmp(op, "-ge") == 0) {
			result = filson_test_int_greater_equal(left, right);
		} else {
			fprintf(stderr, "filson: test: unknown binary operator: %s\n", op);
			filson_last_cmd_success = 0;
			return 1;
		}
	} else {
		result = filson_test_string_not_empty(args[i]);
	}

	filson_last_cmd_success = result;
	return 1;
}
