#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include "history.h"
#include "jobcontrol.h"
#include "pipelines.h"
#include "autocomplete.h"
#include "expansion.h"
#include "shell_session.h"

extern int filson_last_cmd_success;

#define FILSON_RL_BUFSIZE 1024
#define FILSON_PROMPT "filson> "
#define FILSON_IDLE_TIMEOUT_SECS (45 * 60)
#define FILSON_TOK_BUFSIZE 64
#define FILSON_TOK_DELIM " \t\r\n\a"

enum filson_keycode {
	FILSON_KEY_CTRL_A = 1,
	FILSON_KEY_CTRL_B = 2,
	FILSON_KEY_CTRL_D = 4,
	FILSON_KEY_CTRL_E = 5,
	FILSON_KEY_CTRL_F = 6,
	FILSON_KEY_CTRL_K = 11,
	FILSON_KEY_CTRL_L = 12,
	FILSON_KEY_CTRL_N = 14,
	FILSON_KEY_CTRL_P = 16,
	FILSON_KEY_CTRL_S = 19,
	FILSON_KEY_CTRL_U = 21,
	FILSON_KEY_CTRL_W = 23,
	FILSON_KEY_CTRL_Y = 25,
	FILSON_KEY_TAB = '\t',
	FILSON_KEY_NEWLINE = '\n',
	FILSON_KEY_BACKSPACE = 8,
	FILSON_KEY_BACKSPACE_DEL = 127,
	FILSON_KEY_ESCAPE = 27,
	FILSON_KEY_PRINTABLE_MIN = 32,
	FILSON_KEY_PRINTABLE_MAX = 126
};

enum filson_esc_code {
	FILSON_ESC_CSI = '[',
	FILSON_ESC_UP = 'A',
	FILSON_ESC_DOWN = 'B',
	FILSON_ESC_RIGHT = 'C',
	FILSON_ESC_LEFT = 'D'
};

static char filson_heredoc_tmppath[64] = "";

static void filson_print_startup_banner(void);
static void filson_refresh_line(const char *buffer);
static void filson_refresh_line_cursor(const char *buffer, int cursor);
static void filson_set_kill_buffer(char **kill_buffer, const char *src, int len);
static void filson_toggle_prefix(char **buffer, int *bufsize, int *position, int *cursor,
    const char *prefix);
static int filson_heredoc_find(const char *line, char *delim_out, int *start_pos, int *end_pos);
static char *filson_prepare_heredoc(const char *line);
int filson_needs_continuation(const char *buf);
int filson_funcdef_parse(const char *line, char *name_out, int name_max,
    char **body_out, int *needs_more);

static void
filson_print_startup_banner(void)
{
	int use_color;
	char *no_color;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		return;
	}
	no_color = getenv("NO_COLOR");
	use_color = (no_color == NULL || no_color[0] == '\0');
	if (use_color) {
		printf("\033[38;5;153m              .                .\033[0m\n");
		printf("\033[38;5;153m         .        .       .      \033[0m\n");
		printf("\033[38;5;250m             /\\        /\\       \033[0m\n");
		printf("\033[38;5;250m            /  \\  /\\  /  \\      \033[0m\n");
		printf("\033[38;5;250m       /\\  / /\\ \\/  \\/ /\\ \\ /\\  \033[0m\n");
		printf("\033[38;5;250m      /  \\/_/  \\_/ /\\ \\_  \\/  \\ \033[0m\n");
		printf("\033[38;5;34m        Y   Y   Y  Y  Y   Y   Y    \033[0m\n");
		printf("\033[38;5;34m       YYY YYY YYY YY YYY YYY YYY  \033[0m\n");
		printf("\033[38;5;94m====================================\033[0m\n");
		printf("\033[1;38;5;179m              FILSON SHELL\033[0m\n");
		printf("\n");
	} else {
		printf("              .                .\n");
		printf("         .        .       .\n");
		printf("             /\\        /\\\n");
		printf("            /  \\  /\\  /  \\\n");
		printf("       /\\  / /\\ \\/  \\/ /\\ \\ /\\\n");
		printf("      /  \\/_/  \\_/ /\\ \\_  \\/  \\\n");
		printf("        Y   Y   Y  Y  Y   Y   Y\n");
		printf("       YYY YYY YYY YY YYY YYY YYY\n");
		printf("====================================\n");
		printf("              FILSON SHELL\n");
		printf("\n");
	}
}

static void
filson_refresh_line(const char *buffer)
{
	printf("\r%s%s\033[K", FILSON_PROMPT, buffer);
	fflush(stdout);
}

static void
filson_refresh_line_cursor(const char *buffer, int cursor)
{
	int len;
	int move_left;

	len = strlen(buffer);
	printf("\r%s%s\033[K", FILSON_PROMPT, buffer);
	move_left = len - cursor;
	if (move_left > 0) {
		printf("\033[%dD", move_left);
	}
	fflush(stdout);
}

static void
filson_set_kill_buffer(char **kill_buffer, const char *src, int len)
{
	char *next;

	if (kill_buffer == NULL) {
		return;
	}
	if (len < 0) {
		len = 0;
	}
	next = malloc(len + 1);
	if (next == NULL) {
		return;
	}
	if (len > 0) {
		memcpy(next, src, len);
	}
	next[len] = '\0';
	free(*kill_buffer);
	*kill_buffer = next;
}

static void
filson_toggle_prefix(char **buffer, int *bufsize, int *position, int *cursor,
    const char *prefix)
{
	int prefix_len;

	if (buffer == NULL || *buffer == NULL || bufsize == NULL || position == NULL ||
	    cursor == NULL || prefix == NULL) {
		return;
	}
	prefix_len = strlen(prefix);
	if (prefix_len == 0) {
		return;
	}
	if (strncmp(*buffer, prefix, prefix_len) == 0) {
		memmove(*buffer, *buffer + prefix_len, *position - prefix_len + 1);
		*position -= prefix_len;
		if (*cursor > prefix_len) {
			*cursor -= prefix_len;
		} else {
			*cursor = 0;
		}
		return;
	}
	while (*position + prefix_len >= *bufsize - 1) {
		*bufsize += FILSON_RL_BUFSIZE;
		*buffer = realloc(*buffer, *bufsize);
		if (!*buffer) {
			fprintf(stderr, "filson: allocation error\n");
			exit(EXIT_FAILURE);
		}
	}
	memmove(*buffer + prefix_len, *buffer, *position + 1);
	memcpy(*buffer, prefix, prefix_len);
	*position += prefix_len;
	*cursor += prefix_len;
}

char *
filson_read_line(void)
{
	int bufsize;
	int position;
	int cursor;
	int key;
	int interactive;
	int history_cursor;
	int history_count;
	char *buffer;
	char *kill_buffer;
	const char *history_entry;
	struct termios oldt;
	struct termios newt;
	fd_set rfds;
	struct timeval tv;
	int ready;

	bufsize = FILSON_RL_BUFSIZE;
	position = 0;
	cursor = 0;
	kill_buffer = NULL;
	buffer = malloc(sizeof(char) * bufsize);
	if (!buffer) {
		fprintf(stderr, "filson: allocation error\n");
		exit(EXIT_FAILURE);
	}
	buffer[0] = '\0';
	interactive = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
	history_count = filson_history_count_entries();
	history_cursor = history_count;
	if (interactive) {
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		newt.c_iflag &= ~(IXON | IXOFF);
		newt.c_cc[VMIN] = 1;
		newt.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	while (1) {
		if (interactive) {
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			tv.tv_sec = FILSON_IDLE_TIMEOUT_SECS;
			tv.tv_usec = 0;
			ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
			if (ready == 0) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				free(kill_buffer);
				free(buffer);
				fprintf(stderr, "\nfilson: idle timeout (45 minutes) - session ended\n");
				return NULL;
			}
		}
		key = getchar();
		if (interactive && key == FILSON_KEY_ESCAPE) {
			int esc1;
			int esc2;

			esc1 = getchar();
			if (esc1 == FILSON_ESC_CSI) {
				esc2 = getchar();
				if (esc2 == FILSON_ESC_UP || esc2 == FILSON_ESC_DOWN ||
				    esc2 == FILSON_ESC_RIGHT || esc2 == FILSON_ESC_LEFT) {
					if (esc2 == FILSON_ESC_UP && history_cursor > 0) {
						history_cursor--;
					} else if (esc2 == FILSON_ESC_DOWN && history_cursor < history_count) {
						history_cursor++;
					} else if (esc2 == FILSON_ESC_RIGHT && cursor < position) {
						cursor++;
						filson_refresh_line_cursor(buffer, cursor);
						continue;
					} else if (esc2 == FILSON_ESC_LEFT && cursor > 0) {
						cursor--;
						filson_refresh_line_cursor(buffer, cursor);
						continue;
					}
					if (history_cursor >= 0 && history_cursor < history_count) {
						history_entry = filson_history_get(history_cursor);
						if (history_entry == NULL) {
							history_entry = "";
						}
						while ((int)strlen(history_entry) >= bufsize) {
							bufsize += FILSON_RL_BUFSIZE;
							buffer = realloc(buffer, bufsize);
							if (!buffer) {
								fprintf(stderr, "filson: allocation error\n");
								exit(EXIT_FAILURE);
							}
						}
						strcpy(buffer, history_entry);
						position = strlen(buffer);
						cursor = position;
					} else {
						position = 0;
						cursor = 0;
						buffer[0] = '\0';
					}
					filson_refresh_line_cursor(buffer, cursor);
				}
			} else if (esc1 == 's' || esc1 == 'S') {
				filson_toggle_prefix(&buffer, &bufsize, &position, &cursor, "sudo ");
				filson_refresh_line_cursor(buffer, cursor);
			} else if (esc1 == '#') {
				filson_toggle_prefix(&buffer, &bufsize, &position, &cursor, "# ");
				filson_refresh_line_cursor(buffer, cursor);
			} else if (esc1 == 'i' || esc1 == 'I') {
				char pwd[256];
				int pwd_len;

				if (getcwd(pwd, sizeof(pwd)) != NULL) {
					pwd_len = strlen(pwd);
					while (position + pwd_len >= bufsize - 1) {
						bufsize += FILSON_RL_BUFSIZE;
						buffer = realloc(buffer, bufsize);
						if (!buffer) {
							fprintf(stderr, "filson: allocation error\n");
							exit(EXIT_FAILURE);
						}
					}
					memmove(buffer + cursor + pwd_len, buffer + cursor,
					    position - cursor + 1);
					memcpy(buffer + cursor, pwd, pwd_len);
					cursor += pwd_len;
					position += pwd_len;
					filson_refresh_line_cursor(buffer, cursor);
				}
			}
			continue;
		}
		if (interactive && key == FILSON_KEY_TAB) {
			filson_handle_autocomplete(&buffer, &bufsize, &position,
				builtin_str, filson_num_builtins(), filson_refresh_line);
			continue;
		}
		if (key == EOF || (interactive && key == FILSON_KEY_CTRL_D && position == 0)) {
			if (interactive) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
			}
			free(kill_buffer);
			free(buffer);
			return NULL;
		} else if (key == FILSON_KEY_NEWLINE) {
			if (interactive) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				printf("\n");
			}
			buffer[position] = '\0';
			free(kill_buffer);
			return buffer;
		} else if (interactive && key == FILSON_KEY_CTRL_A) {
			cursor = 0;
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && key == FILSON_KEY_CTRL_E) {
			cursor = position;
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && key == FILSON_KEY_CTRL_B) {
			if (cursor > 0) {
				cursor--;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_F) {
			if (cursor < position) {
				cursor++;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_S) {
			filson_toggle_prefix(&buffer, &bufsize, &position, &cursor, "sudo ");
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && key == FILSON_KEY_CTRL_K) {
			if (cursor < position) {
				filson_set_kill_buffer(&kill_buffer, buffer + cursor, position - cursor);
				position = cursor;
				buffer[position] = '\0';
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_U) {
			if (cursor > 0) {
				filson_set_kill_buffer(&kill_buffer, buffer, cursor);
				memmove(buffer, buffer + cursor, position - cursor + 1);
				position -= cursor;
				cursor = 0;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_W) {
			int start;

			start = cursor;
			while (start > 0 && isspace((unsigned char)buffer[start - 1])) {
				start--;
			}
			while (start > 0 && !isspace((unsigned char)buffer[start - 1])) {
				start--;
			}
			if (start < cursor) {
				filson_set_kill_buffer(&kill_buffer, buffer + start, cursor - start);
				memmove(buffer + start, buffer + cursor, position - cursor + 1);
				position -= (cursor - start);
				cursor = start;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_Y) {
			int ylen;

			if (kill_buffer != NULL) {
				ylen = strlen(kill_buffer);
				while (position + ylen >= bufsize - 1) {
					bufsize += FILSON_RL_BUFSIZE;
					buffer = realloc(buffer, bufsize);
					if (!buffer) {
						fprintf(stderr, "filson: allocation error\n");
						exit(EXIT_FAILURE);
					}
				}
				memmove(buffer + cursor + ylen, buffer + cursor, position - cursor + 1);
				memcpy(buffer + cursor, kill_buffer, ylen);
				cursor += ylen;
				position += ylen;
				filson_refresh_line_cursor(buffer, cursor);
				history_cursor = history_count;
			}
		} else if (interactive && key == FILSON_KEY_CTRL_L) {
			printf("\033[2J\033[H");
			filson_refresh_line_cursor(buffer, cursor);
		} else if (interactive && key == FILSON_KEY_CTRL_P) {
			if (history_cursor > 0) {
				history_cursor--;
				if (history_cursor >= 0 && history_cursor < history_count) {
					history_entry = filson_history_get(history_cursor);
					if (history_entry == NULL) {
						history_entry = "";
					}
					while ((int)strlen(history_entry) >= bufsize) {
						bufsize += FILSON_RL_BUFSIZE;
						buffer = realloc(buffer, bufsize);
						if (!buffer) {
							fprintf(stderr, "filson: allocation error\n");
							exit(EXIT_FAILURE);
						}
					}
					strcpy(buffer, history_entry);
					position = strlen(buffer);
					cursor = position;
					filson_refresh_line_cursor(buffer, cursor);
				}
			}
		} else if (interactive && key == FILSON_KEY_CTRL_N) {
			if (history_cursor < history_count) {
				history_cursor++;
				if (history_cursor >= 0 && history_cursor < history_count) {
					history_entry = filson_history_get(history_cursor);
					if (history_entry == NULL) {
						history_entry = "";
					}
					while ((int)strlen(history_entry) >= bufsize) {
						bufsize += FILSON_RL_BUFSIZE;
						buffer = realloc(buffer, bufsize);
						if (!buffer) {
							fprintf(stderr, "filson: allocation error\n");
							exit(EXIT_FAILURE);
						}
					}
					strcpy(buffer, history_entry);
					position = strlen(buffer);
					cursor = position;
				} else {
					position = 0;
					cursor = 0;
					buffer[0] = '\0';
				}
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key == FILSON_KEY_CTRL_D) {
			if (cursor < position) {
				memmove(buffer + cursor, buffer + cursor + 1, position - cursor);
				position--;
				buffer[position] = '\0';
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && (key == FILSON_KEY_BACKSPACE_DEL || key == '\b' ||
		    key == FILSON_KEY_BACKSPACE)) {
			if (cursor > 0) {
				memmove(buffer + cursor - 1, buffer + cursor, position - cursor + 1);
				cursor--;
				position--;
				history_cursor = history_count;
				filson_refresh_line_cursor(buffer, cursor);
			}
		} else if (interactive && key >= FILSON_KEY_PRINTABLE_MIN &&
		    key <= FILSON_KEY_PRINTABLE_MAX) {
			if (position >= bufsize - 2) {
				bufsize += FILSON_RL_BUFSIZE;
				buffer = realloc(buffer, bufsize);
				if (!buffer) {
					fprintf(stderr, "filson: allocation error\n");
					exit(EXIT_FAILURE);
				}
			}
			memmove(buffer + cursor + 1, buffer + cursor, position - cursor + 1);
			buffer[cursor] = (char)key;
			position++;
			cursor++;
			history_cursor = history_count;
			filson_refresh_line_cursor(buffer, cursor);
		} else {
			if (!interactive) {
				buffer[position] = key;
				position++;
				if (position >= bufsize) {
					bufsize += FILSON_RL_BUFSIZE;
					buffer = realloc(buffer, bufsize);
					if (!buffer) {
						fprintf(stderr, "filson: allocation error\n");
						exit(EXIT_FAILURE);
					}
				}
			}
		}
	}
}

char **
filson_split_line(char *line)
{
	int bufsize;
	int position;
	char **tokens;
	char *token_copy;
	char tokbuf[4096];
	int i;
	int j;
	int in_single;
	int in_double;
	int brace_depth;
	int paren_depth;
	int unquoted_start;
	int eq_tilde_escaped;
	int started_in_single;
	int started_in_double;

	bufsize = FILSON_TOK_BUFSIZE;
	position = 0;
	tokens = malloc(bufsize * sizeof(char *));
	if (!tokens) {
		fprintf(stderr, "filson: allocation error\n");
		exit(EXIT_FAILURE);
	}
	i = 0;
	while (line[i] != '\0') {
		while (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
			i++;
		}
		if (line[i] == '\0') {
			break;
		}
		j = 0;
		in_single = 0;
		in_double = 0;
		brace_depth = 0;
		paren_depth = 0;
		unquoted_start = 0;
		eq_tilde_escaped = 0;
		started_in_single = 0;
		started_in_double = 0;
		while (line[i] != '\0') {
			if (!in_double && line[i] == '\'') {
				if (!in_single && j == 0)
					started_in_single = 1;
				in_single = !in_single;
				i++;
				continue;
			}
			if (!in_single && line[i] == '"') {
				if (!in_double && j == 0)
					started_in_double = 1;
				if (brace_depth > 0 && j < (int)sizeof(tokbuf) - 1)
					tokbuf[j++] = '\x05';
				in_double = !in_double;
				i++;
				continue;
			}
			if (!in_single && !in_double) {
				if (brace_depth == 0 && line[i] == '\\' && line[i + 1] != '\0' && line[i + 1] != '\n') {
					if (j == 0)
						unquoted_start = 0;
					if (j > 0 && tokbuf[j - 1] == '=' && line[i + 1] == '~')
						eq_tilde_escaped = 1;
					i++;
					if (j < (int)sizeof(tokbuf) - 1)
						tokbuf[j++] = line[i];
					i++;
					continue;
				}
				if (line[i] == '$' && line[i + 1] == '{') {
					brace_depth++;
					if (j < (int)sizeof(tokbuf) - 2) {
						tokbuf[j++] = line[i];
						tokbuf[j++] = line[i + 1];
					}
					i += 2;
					continue;
				}
				if (brace_depth > 0 && line[i] == '}') {
					brace_depth--;
					if (j < (int)sizeof(tokbuf) - 1)
						tokbuf[j++] = line[i];
					i++;
					continue;
				}
				if (line[i] == '$' && line[i + 1] == '(') {
					paren_depth++;
					if (j < (int)sizeof(tokbuf) - 2) {
						tokbuf[j++] = line[i];
						tokbuf[j++] = line[i + 1];
					}
					i += 2;
					continue;
				}
				if (paren_depth > 0 && line[i] == ')') {
					paren_depth--;
					if (j < (int)sizeof(tokbuf) - 1)
						tokbuf[j++] = line[i];
					i++;
					continue;
				}
				if (brace_depth == 0 && paren_depth == 0 &&
				    (line[i] == ' ' || line[i] == '\t')) {
					break;
				}
			} else if (in_double && !in_single) {
				if (line[i] == '$' && line[i + 1] == '{') {
					brace_depth++;
				} else if (brace_depth > 0 && line[i] == '}') {
					brace_depth--;
				}
			}
			if (j < (int)sizeof(tokbuf) - 1) {
				if (j == 0 && !in_single && !in_double)
					unquoted_start = 1;
				tokbuf[j++] = line[i];
			}
			i++;
		}
		if (j == 0 && !started_in_double && !started_in_single) {
			continue;
		}
		tokbuf[j] = '\0';
		{
			int eqpos;
			char *tilde_exp;

			eqpos = -1;
			{
				int k;
				for (k = 0; k < j; k++) {
					if (tokbuf[k] == '=') {
						eqpos = k;
						break;
					}
				}
			}
			if (eqpos >= 0 && tokbuf[eqpos + 1] == '~' && !eq_tilde_escaped) {
				tilde_exp = filson_tilde_expand(tokbuf + eqpos + 1);
				if (tilde_exp != NULL) {
					int tlen = strlen(tilde_exp);
					token_copy = malloc(eqpos + 1 + tlen + 1);
					if (!token_copy) {
						free(tilde_exp);
						fprintf(stderr, "filson: allocation error\n");
						exit(EXIT_FAILURE);
					}
					memcpy(token_copy, tokbuf, eqpos + 1);
					memcpy(token_copy + eqpos + 1, tilde_exp, tlen + 1);
					free(tilde_exp);
					tokens[position] = token_copy;
					position++;
					if (position >= bufsize) {
						bufsize += FILSON_TOK_BUFSIZE;
						tokens = realloc(tokens, bufsize * sizeof(char *));
						if (!tokens) {
							fprintf(stderr, "filson: allocation error\n");
							exit(EXIT_FAILURE);
						}
					}
					continue;
				}
			}
		}
		if (unquoted_start && tokbuf[0] == '~') {
			char *tilde_exp = filson_tilde_expand(tokbuf);
			if (tilde_exp != NULL) {
				token_copy = tilde_exp;
				tokens[position] = token_copy;
				position++;
				if (position >= bufsize) {
					bufsize += FILSON_TOK_BUFSIZE;
					tokens = realloc(tokens, bufsize * sizeof(char *));
					if (!tokens) {
						fprintf(stderr, "filson: allocation error\n");
						exit(EXIT_FAILURE);
					}
				}
				continue;
			}
		}
		token_copy = malloc(j + 1 + ((started_in_single || started_in_double) ? 1 : 0));
		if (!token_copy) {
			fprintf(stderr, "filson: allocation error\n");
			exit(EXIT_FAILURE);
		}
		if (started_in_single) {
			token_copy[0] = '\x01';
			memcpy(token_copy + 1, tokbuf, j + 1);
		} else if (started_in_double) {
			token_copy[0] = '\x02';
			memcpy(token_copy + 1, tokbuf, j + 1);
		} else {
			memcpy(token_copy, tokbuf, j + 1);
		}
		tokens[position] = token_copy;
		position++;
		if (position >= bufsize) {
			bufsize += FILSON_TOK_BUFSIZE;
			tokens = realloc(tokens, bufsize * sizeof(char *));
			if (!tokens) {
				fprintf(stderr, "filson: allocation error\n");
				exit(EXIT_FAILURE);
			}
		}
	}
	tokens[position] = NULL;
	return tokens;
}

static int
filson_heredoc_find(const char *line, char *delim_out, int *start_pos, int *end_pos)
{
	int i;
	int in_single;
	int in_double;
	int arith_depth;
	int dstart;
	int dend;

	in_single = 0;
	in_double = 0;
	arith_depth = 0;
	for (i = 0; line[i] != '\0'; i++) {
		if (!in_double && line[i] == '\'') {
			in_single = !in_single;
			continue;
		}
		if (!in_single && line[i] == '"') {
			in_double = !in_double;
			continue;
		}
		if (!in_single && !in_double &&
		    line[i] == '$' && line[i + 1] == '(' && line[i + 2] == '(') {
			arith_depth++;
			i += 2;
			continue;
		}
		if (!in_single && !in_double && arith_depth > 0 &&
		    line[i] == ')' && line[i + 1] == ')') {
			arith_depth--;
			i++;
			continue;
		}
		if (!in_single && !in_double && arith_depth == 0 &&
		    line[i] == '<' && line[i + 1] == '<' && line[i + 2] != '<') {
			*start_pos = i;
			i += 2;
			while (line[i] == ' ' || line[i] == '\t') {
				i++;
			}
			dstart = i;
			while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' &&
			       line[i] != '\n' && line[i] != ';') {
				i++;
			}
			dend = i;
			if (dend <= dstart || dend - dstart >= 256) {
				return 0;
			}
			memcpy(delim_out, line + dstart, dend - dstart);
			delim_out[dend - dstart] = '\0';
			*end_pos = dend;
			return 1;
		}
	}
	return 0;
}

static char *
filson_prepare_heredoc(const char *line)
{
	char delim[256];
	int hd_start;
	int hd_end;
	char *body_line;
	char *new_line;
	int fd;
	int n;
	int interactive;
	char *expanded_line;
	int dlen;
	const char *p;
	const char *lend;
	const char *after_hdoc;
	int llen;
	char *seg;

	if (!filson_heredoc_find(line, delim, &hd_start, &hd_end)) {
		return NULL;
	}
	strcpy(filson_heredoc_tmppath, "/tmp/filson_hdoc_XXXXXX");
	fd = mkstemp(filson_heredoc_tmppath);
	if (fd < 0) {
		return NULL;
	}
	if (line[hd_end] == '\n') {
		dlen = strlen(delim);
		p = line + hd_end + 1;
		after_hdoc = NULL;
		while (*p != '\0') {
			lend = p;
			while (*lend != '\0' && *lend != '\n')
				lend++;
			if ((int)(lend - p) == dlen && memcmp(p, delim, dlen) == 0) {
				after_hdoc = (*lend == '\n') ? lend + 1 : lend;
				break;
			}
			llen = (int)(lend - p);
			seg = malloc(llen + 1);
			if (seg != NULL) {
				memcpy(seg, p, llen);
				seg[llen] = '\0';
				expanded_line = filson_expand_string_variables(seg);
				n = strlen(expanded_line);
				write(fd, expanded_line, n);
				write(fd, "\n", 1);
				if (expanded_line != seg)
					free(expanded_line);
				free(seg);
			}
			if (*lend == '\0')
				break;
			p = lend + 1;
		}
		close(fd);
		{
			int pfx = hd_start;
			int tlen = strlen(filson_heredoc_tmppath);
			int sfxlen = after_hdoc ? (int)strlen(after_hdoc) : 0;
			new_line = malloc(pfx + 2 + tlen + sfxlen + 2);
			if (new_line == NULL) {
				unlink(filson_heredoc_tmppath);
				filson_heredoc_tmppath[0] = '\0';
				return NULL;
			}
			snprintf(new_line, pfx + 2 + tlen + sfxlen + 2,
			    "%.*s< %s%s", pfx, line, filson_heredoc_tmppath,
			    after_hdoc ? after_hdoc : "");
		}
		return new_line;
	}
	interactive = isatty(STDIN_FILENO);
	while (1) {
		if (interactive) {
			write(STDOUT_FILENO, "heredoc> ", 9);
		}
		body_line = filson_read_line();
		if (body_line == NULL) {
			break;
		}
		if (strcmp(body_line, delim) == 0) {
			free(body_line);
			break;
		}
		expanded_line = filson_expand_string_variables(body_line);
		n = strlen(expanded_line);
		write(fd, expanded_line, n);
		write(fd, "\n", 1);
		if (expanded_line != body_line) {
			free(expanded_line);
		}
		free(body_line);
	}
	close(fd);
	new_line = malloc(hd_start + strlen(filson_heredoc_tmppath) +
	    (strlen(line) - hd_end) + 4);
	if (new_line == NULL) {
		unlink(filson_heredoc_tmppath);
		filson_heredoc_tmppath[0] = '\0';
		return NULL;
	}
	snprintf(new_line,
	    hd_start + strlen(filson_heredoc_tmppath) + (strlen(line) - hd_end) + 4,
	    "%.*s< %s%s", hd_start, line, filson_heredoc_tmppath, line + hd_end);
	return new_line;
}

int
filson_funcdef_parse(const char *line, char *name_out, int name_max,
    char **body_out, int *needs_more)
{
	const char *p;
	const char *after_name;
	const char *q;
	const char *body_start;
	const char *body_end;
	const char *brace_close;
	int name_len;
	int depth;
	int body_len;

	p = line;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) {
		return 0;
	}
	name_len = 0;
	while ((p[name_len] >= 'a' && p[name_len] <= 'z') ||
	       (p[name_len] >= 'A' && p[name_len] <= 'Z') ||
	       (p[name_len] >= '0' && p[name_len] <= '9') ||
	       p[name_len] == '_') {
		name_len++;
	}
	if (name_len == 0 || name_len >= name_max) {
		return 0;
	}
	after_name = p + name_len;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != '(') {
		return 0;
	}
	after_name++;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != ')') {
		return 0;
	}
	after_name++;
	while (*after_name == ' ' || *after_name == '\t') {
		after_name++;
	}
	if (*after_name != '{') {
		return 0;
	}
	memcpy(name_out, p, name_len);
	name_out[name_len] = '\0';
	q = after_name;
	depth = 0;
	brace_close = NULL;
	while (*q != '\0') {
		if (*q == '{') {
			depth++;
		} else if (*q == '}') {
			depth--;
			if (depth == 0) {
				brace_close = q;
				break;
			}
		}
		q++;
	}
	if (brace_close == NULL) {
		*needs_more = 1;
		*body_out = NULL;
		return 1;
	}
	body_start = after_name + 1;
	body_end = brace_close;
	while (body_start < body_end &&
	       (*body_start == ' ' || *body_start == '\t' || *body_start == '\n' ||
	       *body_start == ';')) {
		body_start++;
	}
	while (body_end > body_start &&
	       (*(body_end - 1) == ' ' || *(body_end - 1) == '\t' ||
	       *(body_end - 1) == ';' || *(body_end - 1) == '\n')) {
		body_end--;
	}
	body_len = body_end - body_start;
	*body_out = malloc(body_len + 1);
	if (*body_out == NULL) {
		return 0;
	}
	memcpy(*body_out, body_start, body_len);
	(*body_out)[body_len] = '\0';
	*needs_more = 0;
	return 1;
}

int
filson_needs_continuation(const char *buf)
{
	int in_single;
	int in_double;
	int loop_depth;
	int if_depth;
	int case_depth;
	int paren_depth;
	const char *p;

	in_single = 0;
	in_double = 0;
	loop_depth = 0;
	if_depth = 0;
	case_depth = 0;
	paren_depth = 0;
	p = buf;
	while (*p != '\0') {
		if (!in_double && *p == '\'') {
			in_single = !in_single;
			p++;
			continue;
		}
		if (!in_single && *p == '"') {
			in_double = !in_double;
			p++;
			continue;
		}
		if (in_single || in_double) {
			p++;
			continue;
		}
		if (*p == '$' && *(p + 1) == '(') {
			paren_depth++;
			p += 2;
			continue;
		}
		if (*p == '(' && paren_depth > 0) {
			paren_depth++;
			p++;
			continue;
		}
		if (*p == ')' && paren_depth > 0) {
			paren_depth--;
			p++;
			continue;
		}
		if (p == buf || isspace((unsigned char)p[-1]) || p[-1] == ';' ||
		    p[-1] == '\n') {
			if (strncmp(p, "for", 3) == 0 &&
			    (isspace((unsigned char)p[3]) || p[3] == '\0')) {
				loop_depth++;
			} else if (strncmp(p, "while", 5) == 0 &&
			    (isspace((unsigned char)p[5]) || p[5] == '\0')) {
				loop_depth++;
			} else if (strncmp(p, "until", 5) == 0 &&
			    (isspace((unsigned char)p[5]) || p[5] == '\0')) {
				loop_depth++;
			} else if (strncmp(p, "if", 2) == 0 &&
			    (isspace((unsigned char)p[2]) || p[2] == '\0')) {
				if_depth++;
			} else if (strncmp(p, "case", 4) == 0 &&
			    (isspace((unsigned char)p[4]) || p[4] == '\0')) {
				case_depth++;
			} else if (strncmp(p, "done", 4) == 0 &&
			    (p[4] == '\0' || isspace((unsigned char)p[4]) || p[4] == ';')) {
				if (loop_depth > 0) loop_depth--;
			} else if (strncmp(p, "fi", 2) == 0 &&
			    (p[2] == '\0' || isspace((unsigned char)p[2]) || p[2] == ';')) {
				if (if_depth > 0) if_depth--;
			} else if (strncmp(p, "esac", 4) == 0 &&
			    (p[4] == '\0' || isspace((unsigned char)p[4]) || p[4] == ';')) {
				if (case_depth > 0) case_depth--;
			}
		}
		p++;
	}
	if (in_single || in_double) return 1;
	if (loop_depth > 0 || if_depth > 0 || case_depth > 0) return 1;
	if (paren_depth > 0) return 1;
	return 0;
}

int
filson_loop(void)
{
	char *line;
	char *resolved;
	char *hd_line;
	char *func_body;
	char *trimmed;
	char func_name[256];
	int func_needs_more;
	int status;

	filson_print_startup_banner();
	status = 1;
	do {
		filson_reap_background_jobs();
		if (isatty(STDIN_FILENO)) {
			printf(FILSON_PROMPT);
			fflush(stdout);
		}
		line = filson_read_line();
		if (line == NULL) {
			if (isatty(STDIN_FILENO)) {
				printf("\n");
			}
			break;
		}
		trimmed = filson_trim(line);
		if (strcmp(trimmed, "!") == 0) {
			const char *last_entry;

			if (filson_history_count_entries() == 0) {
				fprintf(stderr, "filson: no commands in history\n");
				free(line);
				continue;
			}
			last_entry = filson_history_get(filson_history_count_entries() - 1);
			if (last_entry != NULL) {
				printf("%s\n", last_entry);
			}
			free(line);
			continue;
		}
		resolved = filson_resolve_history(line);
		if (resolved == NULL) {
			free(line);
			continue;
		}
		filson_add_history(resolved);
		if (filson_funcdef_parse(resolved, func_name, sizeof(func_name),
		    &func_body, &func_needs_more)) {
			if (func_needs_more) {
				const char *pp;
				int depth;
				int accum_len;
				int bufsize;
				char *accum;
				char *more;
				char *tmp;

				bufsize = strlen(resolved) + 4096;
				accum = malloc(bufsize);
				if (accum != NULL) {
					accum_len = strlen(resolved);
					strcpy(accum, resolved);
					depth = 0;
					pp = resolved;
					while (*pp != '\0') {
						if (*pp == '{') {
							depth++;
						} else if (*pp == '}') {
							depth--;
						}
						pp++;
					}
					while (depth > 0) {
						if (isatty(STDIN_FILENO)) {
							write(STDOUT_FILENO, "> ", 2);
						}
						more = filson_read_line();
						if (more == NULL) {
							break;
						}
						if (accum_len + (int)strlen(more) + 4 > bufsize) {
							bufsize = accum_len + strlen(more) + 4096;
							tmp = realloc(accum, bufsize);
							if (tmp == NULL) {
								free(more);
								break;
							}
							accum = tmp;
						}
						accum[accum_len++] = ';';
						memcpy(accum + accum_len, more, strlen(more));
						accum_len += strlen(more);
						accum[accum_len] = '\0';
						pp = more;
						while (*pp != '\0') {
							if (*pp == '{') {
								depth++;
							} else if (*pp == '}') {
								depth--;
							}
							pp++;
						}
						free(more);
					}
					filson_funcdef_parse(accum, func_name, sizeof(func_name),
					    &func_body, &func_needs_more);
					free(accum);
				}
			}
			if (func_body != NULL) {
				filson_define_function(func_name, func_body);
				free(func_body);
			}
			free(resolved);
			free(line);
			continue;
		}
		if (filson_needs_continuation(resolved)) {
			char *accum;
			char *more;
			char *tmp;
			int accum_len;
			int bufsize;

			bufsize = strlen(resolved) + 4096;
			accum = malloc(bufsize);
			if (accum != NULL) {
				accum_len = strlen(resolved);
				strcpy(accum, resolved);
				while (filson_needs_continuation(accum)) {
					if (isatty(STDIN_FILENO)) {
						write(STDOUT_FILENO, "> ", 2);
					}
					more = filson_read_line();
					if (more == NULL) break;
					if (accum_len + (int)strlen(more) + 4 > bufsize) {
						bufsize = accum_len + strlen(more) + 4096;
						tmp = realloc(accum, bufsize);
						if (tmp == NULL) {
							free(more);
							break;
						}
						accum = tmp;
					}
					accum[accum_len++] = '\n';
					memcpy(accum + accum_len, more, strlen(more));
					accum_len += strlen(more);
					accum[accum_len] = '\0';
					free(more);
				}
				free(resolved);
				free(line);
				hd_line = filson_prepare_heredoc(accum);
				if (hd_line != NULL) {
					status = filson_execute_and_chain(hd_line);
					free(hd_line);
					if (filson_heredoc_tmppath[0] != '\0') {
						unlink(filson_heredoc_tmppath);
						filson_heredoc_tmppath[0] = '\0';
					}
				} else {
					status = filson_execute_and_chain(accum);
				}
				free(accum);
				continue;
			}
		}
		hd_line = filson_prepare_heredoc(resolved);
		if (hd_line != NULL) {
			status = filson_execute_and_chain(hd_line);
			free(hd_line);
			if (filson_heredoc_tmppath[0] != '\0') {
				unlink(filson_heredoc_tmppath);
				filson_heredoc_tmppath[0] = '\0';
			}
		} else {
			status = filson_execute_and_chain(resolved);
		}
		free(resolved);
		free(line);
	} while (status);
	filson_clear_history();
	{
		extern int filson_exit_called;
		extern int filson_exit_code;
		if (filson_exit_called) {
			return filson_exit_code == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}
	if (!isatty(STDIN_FILENO)) {
		return filson_last_cmd_success ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
