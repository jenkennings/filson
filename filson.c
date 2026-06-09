#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"
#include "globbing.h"
#include "shell_session.h"
#include "runtime_state.h"
#include "expansion.h"
#include "builtins.h"

int filson_is_valid_varname(const char *name);
int filson_path_is_safe(void);
int filson_arg_count(char **args);
int filson_execute(char **args, int argc, int background, char *segment);

static int filson_dispatch_builtin(int index, char **args);
static int filson_run_command_only(char **args, int argc, int background, char *segment);
static int filson_run_with_temp_assignments(char **args, int argc, int assign_count, int background, char *segment);
static void filson_free_assignment_buffers(int count, char **names, char **old_values);
static void filson_restore_assignment_environment(int count, char **names, char **old_values, int *had_old);

#define FILSON_MAX_ARGS 1024
#define FILSON_MAX_INLINE_ASSIGNMENTS 128

int filson_last_cmd_success = 1;
int filson_last_exit_status = 0;
int filson_exit_code = 0;
int filson_noglob = 0;
int filson_exit_called = 0;
int filson_break_flag = 0;
int filson_continue_flag = 0;

const char *builtin_str[] = {
	"cd",
	"help",
	"exit",
	"set",
	"echo",
	"pwd",
	"clear",
	"unset",
	"export",
	"type",
	"history",
	"jobs",
	"fg",
	"bg",
	"wait",
	"alias",
	"local",
	"return",
	"declare",
	"break",
	"continue",
	"read",
	"shift",
	"source",
	".",
	"eval",
	"unalias",
	"trap"
};

static int
filson_dispatch_builtin(int index, char **args)
{
	switch (index) {
	case 0: return filson_cd(args);
	case 1: return filson_help(args);
	case 2: return filson_exit(args);
	case 3: return filson_set(args);
	case 4: return filson_echo(args);
	case 5: return filson_pwd(args);
	case 6: return filson_clear(args);
	case 7: return filson_unset(args);
	case 8: return filson_export(args);
	case 9: return filson_type(args);
	case 10: return filson_history(args);
	case 11: return filson_jobs(args);
	case 12: return filson_fg(args);
	case 13: return filson_bg(args);
	case 14: return filson_wait(args);
	case 15: return filson_alias(args);
	case 16: return filson_local(args);
	case 17: return filson_return_stmt(args);
	case 18: return filson_declare_func(args);
	case 19: return filson_break(args);
	case 20: return filson_continue(args);
	case 21: return filson_read(args);
	case 22: return filson_shift(args);
	case 23: return filson_source(args);
	case 24: return filson_dot(args);
	case 25: return filson_eval(args);
	case 26: return filson_unalias(args);
	case 27: return filson_trap(args);
	default: return 1;
	}
}

int
filson_num_builtins(void)
{
	return sizeof(builtin_str) / sizeof(*builtin_str);
}

int
filson_is_valid_varname(const char *name)
{
	int i;

	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	if (!((name[0] >= 'a' && name[0] <= 'z') || 
	      (name[0] >= 'A' && name[0] <= 'Z') || 
	      name[0] == '_')) {
		return 0;
	}
	for (i = 1; name[i] != '\0'; i++) {
		if (!((name[i] >= 'a' && name[i] <= 'z') || 
		      (name[i] >= 'A' && name[i] <= 'Z') || 
		      (name[i] >= '0' && name[i] <= '9') || 
		      name[i] == '_')) {
			return 0;
		}
	}
	return 1;
}

int
filson_arg_count(char **args)
{
	int count;

	if (args == NULL) {
		return 0;
	}
	count = 0;
	while (count <= FILSON_MAX_ARGS && args[count] != NULL) {
		count++;
	}
	return count;
}

int
filson_path_is_safe(void)
{
	char *path_str;
	char *path_copy, *token;
	int safe;

	path_str = getenv("PATH");
	if (path_str == NULL) {
		return 1;
	}
	path_copy = strdup(path_str);
	if (path_copy == NULL) {
		return 1;
	}
	safe = 1;
	token = strtok(path_copy, ":");
	while (token != NULL) {
		if (strcmp(token, ".") == 0 || strcmp(token, "") == 0) {
			warnx("unsafe PATH: current directory entry found; refusing external execution");
			safe = 0;
			break;
		}
		token = strtok(NULL, ":");
	}
	free(path_copy);
	return safe;
}

static int filson_ifs_split(const char *s, char **out, int max);

int
filson_launch(char **args, int background, char *segment)
{
	pid_t pid;
	pid_t waited;
	int status;
	int job_id;
	char *pre_args[FILSON_MAX_ARGS + 1];
	int pre_ai;
	int argc_local;
	int ai;

	assert(args != NULL);
	assert(args[0] != NULL);

	argc_local = 0;
	while (args[argc_local] != NULL)
		argc_local++;
	pre_ai = 0;
	for (ai = 0; ai < argc_local && args[ai] != NULL; ai++) {
		char *ev;
		unsigned char first_byte;
		int ev_was_quoted;

		ev = filson_expand_string_variables(args[ai]);
		first_byte = (unsigned char)args[ai][0];
		ev_was_quoted = 0;
		if ((unsigned char)ev[0] == 0x02) {
			char *stripped = strdup(ev + 1);
			if (ev != args[ai])
				free(ev);
			ev = stripped;
			ev_was_quoted = 1;
		} else if (first_byte == 0x01 || first_byte == 0x02) {
			ev_was_quoted = 1;
		}
		if ((unsigned char)ev[0] == 0x03) {
			char *mp = ev + 1;
			for (;;) {
				char *mnext = strchr(mp, '\x1f');
				int mlen = mnext ? (int)(mnext - mp) : (int)strlen(mp);
				if (pre_ai < FILSON_MAX_ARGS)
					pre_args[pre_ai++] = strndup(mp, (size_t)mlen);
				if (!mnext)
					break;
				mp = mnext + 1;
			}
			if (ev != args[ai])
				free(ev);
			continue;
		}
		if (ev != args[ai] && ev[0] == '\0' &&
		    first_byte != 0x01 && first_byte != 0x02 &&
		    args[ai][0] == '$') {
			free(ev);
			continue;
		}
		if (!ev_was_quoted && ev != args[ai] && ev[0] != '\0') {
			char *ifs_parts[FILSON_MAX_ARGS + 1];
			int nparts = filson_ifs_split(ev, ifs_parts,
			    FILSON_MAX_ARGS - pre_ai);
			if (nparts > 0) {
				int pi;
				for (pi = 0; pi < nparts && pre_ai < FILSON_MAX_ARGS; pi++) {
					if (!filson_noglob && filson_has_glob_chars(ifs_parts[pi])) {
						glob_t g;
						int grc = filson_glob_expand(ifs_parts[pi], &g);
						if (grc == 0 && (g.gl_pathc != 1 ||
						    strcmp(g.gl_pathv[0], ifs_parts[pi]) != 0)) {
							int gi;
							for (gi = 0; gi < (int)g.gl_pathc &&
							    pre_ai < FILSON_MAX_ARGS; gi++)
								pre_args[pre_ai++] = strdup(g.gl_pathv[gi]);
							globfree(&g);
							free(ifs_parts[pi]);
							continue;
						} else if (grc == 0) {
							globfree(&g);
						}
					}
					pre_args[pre_ai++] = ifs_parts[pi];
				}
				if (ev != args[ai])
					free(ev);
				continue;
			} else if (nparts == 0) {
				if (ev != args[ai])
					free(ev);
				continue;
			}
		}
		if (first_byte != 0x01 && first_byte != 0x02 &&
		    !ev_was_quoted &&
		    !filson_noglob && filson_has_glob_chars(ev)) {
			glob_t g;
			int grc = filson_glob_expand(ev, &g);
			if (grc == 0) {
				if (g.gl_pathc == 1 && strcmp(g.gl_pathv[0], ev) == 0) {
					globfree(&g);
				} else {
					int gi;
					for (gi = 0; gi < (int)g.gl_pathc &&
					    pre_ai < FILSON_MAX_ARGS; gi++)
						pre_args[pre_ai++] = strdup(g.gl_pathv[gi]);
					globfree(&g);
					if (ev != args[ai])
						free(ev);
					continue;
				}
			}
		}
		pre_args[pre_ai++] = (ev != args[ai]) ? ev : strdup(ev);
	}
	pre_args[pre_ai] = NULL;

	pid = fork();
	if (pid == 0) {
		execvp(pre_args[0], pre_args);
		warn("%s", pre_args[0]);
		_exit(1);
	} else if (pid < 0) {
		warn("fork");
		filson_last_cmd_success = 0;
	} else {
		if (background) {
			job_id = filson_add_job(pid, segment, 0);
			if (job_id < 0) {
				warnx("too many background jobs");
				filson_last_cmd_success = 0;
			} else {
				printf("[%d] %d\n", job_id, pid);
				filson_last_cmd_success = 1;
			}
		} else {
			do {
				waited = waitpid(pid, &status, WUNTRACED);
				if (waited < 0) {
					warn("waitpid");
					filson_last_cmd_success = 0;
					goto launch_done;
				}
			} while (!WIFEXITED(status) && !WIFSIGNALED(status) && !WIFSTOPPED(status));
			if (WIFSTOPPED(status)) {
				job_id = filson_add_job(pid, segment, 1);
				if (job_id >= 0) {
					printf("[%d] Stopped %s\n", job_id, segment);
				}
				filson_last_cmd_success = 0;
			} else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
				filson_last_cmd_success = 1;
				filson_last_exit_status = 0;
			} else {
				filson_last_cmd_success = 0;
				if (WIFEXITED(status)) {
					filson_last_exit_status = WEXITSTATUS(status);
				} else if (WIFSIGNALED(status)) {
					filson_last_exit_status = 128 + WTERMSIG(status);
				}
			}
		}
	}
launch_done:
	for (ai = 0; ai < pre_ai; ai++) {
		if (pre_args[ai] != NULL)
			free(pre_args[ai]);
	}
	return 1;
}

static int
filson_ifs_split(const char *s, char **out, int max)
{
	const char *ifs;
	const char *p;
	const char *start;
	int n;
	int k;
	int all_ws;

	ifs = getenv("IFS");
	if (ifs == NULL)
		ifs = " \t\n";
	if (ifs[0] == '\0') {
		if (max > 0)
			out[0] = strdup(s);
		return (max > 0) ? 1 : 0;
	}
	all_ws = 1;
	for (k = 0; ifs[k]; k++) {
		if (ifs[k] != ' ' && ifs[k] != '\t' && ifs[k] != '\n') {
			all_ws = 0;
			break;
		}
	}
	n = 0;
	p = s;
	while (*p && strchr(ifs, (unsigned char)*p) &&
	    (*p == ' ' || *p == '\t' || *p == '\n'))
		p++;
	if (*p == '\0')
		return 0;
	while (*p && n < max) {
		start = p;
		while (*p && !strchr(ifs, (unsigned char)*p))
			p++;
		if (p > start) {
			out[n++] = strndup(start, (size_t)(p - start));
		} else {
			out[n++] = strdup("");
		}
		if (!*p)
			break;
		if (*p == ' ' || *p == '\t' || *p == '\n') {
			while (*p && strchr(ifs, (unsigned char)*p) &&
			    (*p == ' ' || *p == '\t' || *p == '\n'))
				p++;
			if (*p && strchr(ifs, (unsigned char)*p) &&
			    *p != ' ' && *p != '\t' && *p != '\n') {
				p++;
				while (*p && strchr(ifs, (unsigned char)*p) &&
				    (*p == ' ' || *p == '\t' || *p == '\n'))
					p++;
			}
		} else {
			p++;
			while (*p && strchr(ifs, (unsigned char)*p) &&
			    (*p == ' ' || *p == '\t' || *p == '\n'))
				p++;
		}
	}
	(void)all_ws;
	return n;
}

static int
filson_run_command_only(char **args, int argc, int background, char *segment)
{
	int i;
	int result;
	char *alias_value;
	char *alias_expanded;

	assert(args != NULL);

	if (args[0] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	if (argc > FILSON_MAX_ARGS) {
		warnx("too many arguments");
		filson_last_cmd_success = 0;
		return 1;
	}
	alias_expanded = NULL;
	alias_value = filson_lookup_alias(args[0]);
	if (alias_value != NULL) {
		alias_expanded = strdup(alias_value);
		if (alias_expanded == NULL) {
			warn(NULL);
			filson_last_cmd_success = 0;
			return 1;
		}
		args[0] = alias_expanded;
	}
	if (filson_lookup_function(args[0]) != NULL) {
		int ai;
		char *exp_args[FILSON_MAX_ARGS + 1];

		for (ai = 0; ai < argc && args[ai] != NULL; ai++) {
			if (ai > 0 && (unsigned char)args[ai][0] != 0x01)
				exp_args[ai] = filson_expand_string_variables(args[ai]);
			else
				exp_args[ai] = args[ai];
		}
		exp_args[ai] = NULL;
		result = filson_call_function(exp_args[0], exp_args);
		for (ai = 1; ai < argc && args[ai] != NULL; ai++) {
			if (exp_args[ai] != args[ai])
				free(exp_args[ai]);
		}
		free(alias_expanded);
		return result;
	}
	for (i = 0; i < filson_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			int ai;
			int out_ai;
			char *exp_args[FILSON_MAX_ARGS + 1];

			if (background) {
				warnx("cannot run built-in in background");
				filson_last_cmd_success = 0;
				free(alias_expanded);
				return 1;
			}
			out_ai = 0;
			for (ai = 0; ai < argc && args[ai] != NULL; ai++) {
				char *ev;
				unsigned char first_byte;
				int ev_was_quoted;
				ev = filson_expand_string_variables(args[ai]);
				first_byte = (unsigned char)args[ai][0];
				ev_was_quoted = 0;
				if ((unsigned char)ev[0] == 0x02) {
					char *stripped = strdup(ev + 1);
					if (ev != args[ai])
						free(ev);
					ev = stripped;
					ev_was_quoted = 1;
				} else if (first_byte == 0x01 || first_byte == 0x02) {
					ev_was_quoted = 1;
				}
				if ((unsigned char)ev[0] == 0x03) {
					char *mp = ev + 1;
					for (;;) {
						char *mnext = strchr(mp, '\x1f');
						int mlen = mnext ? (int)(mnext - mp) : (int)strlen(mp);
						if (out_ai < FILSON_MAX_ARGS)
							exp_args[out_ai++] = strndup(mp, (size_t)mlen);
						if (!mnext)
							break;
						mp = mnext + 1;
					}
					if (ev != args[ai])
						free(ev);
					continue;
				}
				if (ev != args[ai] && ev[0] == '\0' &&
				    first_byte != 0x01 && first_byte != 0x02 &&
				    args[ai][0] == '$') {
					free(ev);
					continue;
				}
				if (!ev_was_quoted && ev != args[ai] && ev[0] != '\0') {
					char *ifs_parts[FILSON_MAX_ARGS + 1];
					int nparts = filson_ifs_split(ev, ifs_parts,
					    FILSON_MAX_ARGS - out_ai);
					if (nparts > 0) {
						int pi;
						for (pi = 0; pi < nparts && out_ai < FILSON_MAX_ARGS; pi++) {
							if (!filson_noglob && filson_has_glob_chars(ifs_parts[pi])) {
								glob_t g;
								int grc = filson_glob_expand(ifs_parts[pi], &g);
								if (grc == 0 && (g.gl_pathc != 1 ||
								    strcmp(g.gl_pathv[0], ifs_parts[pi]) != 0)) {
									int gi;
									for (gi = 0; gi < (int)g.gl_pathc &&
									    out_ai < FILSON_MAX_ARGS; gi++)
										exp_args[out_ai++] = strdup(g.gl_pathv[gi]);
									globfree(&g);
									free(ifs_parts[pi]);
									continue;
								} else if (grc == 0) {
									globfree(&g);
								}
							}
							exp_args[out_ai++] = ifs_parts[pi];
						}
						if (ev != args[ai])
							free(ev);
						continue;
					} else if (nparts == 0) {
						if (ev != args[ai])
							free(ev);
						continue;
					}
				}
				if (first_byte != 0x01 && first_byte != 0x02 &&
				    !ev_was_quoted &&
				    !filson_noglob && filson_has_glob_chars(ev)) {
					glob_t g;
					int grc = filson_glob_expand(ev, &g);
					if (grc == 0) {
						if (g.gl_pathc == 1 && strcmp(g.gl_pathv[0], ev) == 0) {
							globfree(&g);
						} else {
							int gi;
							for (gi = 0; gi < (int)g.gl_pathc &&
							    out_ai < FILSON_MAX_ARGS; gi++) {
								exp_args[out_ai++] = strdup(g.gl_pathv[gi]);
							}
							globfree(&g);
							if (ev != args[ai])
								free(ev);
							continue;
						}
					}
				}
				exp_args[out_ai++] = ev;
			}
			exp_args[out_ai] = NULL;
			result = filson_dispatch_builtin(i, exp_args);
			for (ai = 0; ai < out_ai; ai++) {
				if (exp_args[ai] != args[ai])
					free(exp_args[ai]);
			}
			free(alias_expanded);
			return result;
		}
	}
	if (!filson_path_is_safe()) {
		filson_last_cmd_success = 0;
		free(alias_expanded);
		return 1;
	}
	result = filson_launch(args, background, segment);
	free(alias_expanded);
	return result;
}

static int
filson_run_with_temp_assignments(char **args, int argc, int assign_count, int background, char *segment)
{
	char *names[FILSON_MAX_INLINE_ASSIGNMENTS];
	char *old_values[FILSON_MAX_INLINE_ASSIGNMENTS];
	int had_old[FILSON_MAX_INLINE_ASSIGNMENTS];
	const char *value;
	const char *old_value;
	int i;
	int result;

	assert(args != NULL);
	if (assign_count > FILSON_MAX_INLINE_ASSIGNMENTS) {
		warnx("too many inline assignments");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (assign_count > argc) {
		filson_last_cmd_success = 0;
		warnx("invalid assignment count");
		return 1;
	}
	for (i = 0; i < assign_count; i++) {
		names[i] = NULL;
		old_values[i] = NULL;
		had_old[i] = 0;
	}
	for (i = 0; i < assign_count; i++) {
		if (!filson_parse_assignment_token(args[i], &names[i], &value)) {
			filson_last_cmd_success = 0;
			filson_free_assignment_buffers(i, names, old_values);
			warnx("invalid inline assignment");
			return 1;
		}
		old_value = getenv(names[i]);
		if (old_value != NULL) {
			had_old[i] = 1;
			old_values[i] = strdup(old_value);
			if (old_values[i] == NULL) {
				filson_last_cmd_success = 0;
				filson_free_assignment_buffers(i + 1, names, old_values);
				warn(NULL);
				return 1;
			}
		}
		if (setenv(names[i], value, 1) != 0) {
			warn("setenv");
			filson_last_cmd_success = 0;
			filson_restore_assignment_environment(i, names, old_values, had_old);
			filson_free_assignment_buffers(i + 1, names, old_values);
			return 1;
		}
	}
	result = filson_run_command_only(args + assign_count, argc - assign_count, background, segment);
	filson_restore_assignment_environment(assign_count, names, old_values, had_old);
	filson_free_assignment_buffers(assign_count, names, old_values);
	return result;
}

static void
filson_free_assignment_buffers(int count, char **names, char **old_values)
{
	int i;

	for (i = 0; i < count; i++) {
		free(names[i]);
		free(old_values[i]);
	}
}

static void
filson_restore_assignment_environment(int count, char **names, char **old_values, int *had_old)
{
	int i;

	for (i = 0; i < count; i++) {
		unsetenv(names[i]);
		if (had_old[i]) {
			setenv(names[i], old_values[i], 1);
		}
	}
}

int
filson_execute(char **args, int argc, int background, char *segment)
{
	int i;
	int assign_count;
	char *name;
	const char *value;

	if (args == NULL || argc <= 0 || args[0] == NULL) {
		filson_last_cmd_success = 1;
		return 1;
	}
	if (argc > FILSON_MAX_ARGS) {
		warnx("too many arguments");
		filson_last_cmd_success = 0;
		return 1;
	}
	assign_count = 0;
	while (assign_count < argc && args[assign_count] != NULL && filson_is_assignment_token(args[assign_count])) {
		assign_count++;
	}
	if (assign_count > 0) {
		if (assign_count == argc || args[assign_count] == NULL) {
			for (i = 0; i < assign_count; i++) {
				char *expanded_value;

				if (!filson_parse_assignment_token(args[i], &name, &value)) {
					warnx("invalid assignment");
					filson_last_cmd_success = 0;
					return 1;
				}
				expanded_value = filson_expand_string_variables(value);
				if (setenv(name, expanded_value != value ? expanded_value : value, 1) != 0) {
					warn("setenv");
					free(name);
					if (expanded_value != value)
						free(expanded_value);
					filson_last_cmd_success = 0;
					return 1;
				}
				free(name);
				if (expanded_value != value)
					free(expanded_value);
			}
			filson_last_cmd_success = 1;
			return 1;
		}
		return filson_run_with_temp_assignments(args, argc, assign_count, background, segment);
	}
	return filson_run_command_only(args, argc, background, segment);
}

