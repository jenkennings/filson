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
#define FILSON_READLINE_MAX_ITER 65536
#define FILSON_HEREDOC_MAX_LINES 65536

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
	(void)fflush(stdout);
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
	(void)fflush(stdout);
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

struct filson_rl {
	char *buf;
	int bufsize;
	int pos;
	int cur;
	char *kill_buf;
	int hist_cur;
	int hist_count;
	int interactive;
};

static void
filson_rls_grow(struct filson_rl *s, int needed)
{
	while (s->pos + needed >= s->bufsize - 1) {
		s->bufsize += FILSON_RL_BUFSIZE;
		s->buf = realloc(s->buf, s->bufsize);
		if (!s->buf) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	}
}

static void
filson_rls_load_hist(struct filson_rl *s, int idx)
{
	const char *entry;

	if (idx >= 0 && idx < s->hist_count) {
		entry = filson_history_get(idx);
		if (entry == NULL) entry = "";
		while ((int)strlen(entry) >= s->bufsize) {
			s->bufsize += FILSON_RL_BUFSIZE;
			s->buf = realloc(s->buf, s->bufsize);
			if (!s->buf) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
		}
		strcpy(s->buf, entry);
		s->pos = strlen(s->buf);
		s->cur = s->pos;
	} else {
		s->pos = 0; s->cur = 0; s->buf[0] = '\0';
	}
}

static int
filson_rls_esc_csi(struct filson_rl *s, int esc2)
{
	if (esc2 == FILSON_ESC_RIGHT && s->cur < s->pos) {
		s->cur++;
		filson_refresh_line_cursor(s->buf, s->cur);
		return 1;
	}
	if (esc2 == FILSON_ESC_LEFT && s->cur > 0) {
		s->cur--;
		filson_refresh_line_cursor(s->buf, s->cur);
		return 1;
	}
	if (esc2 == FILSON_ESC_UP && s->hist_cur > 0) s->hist_cur--;
	else if (esc2 == FILSON_ESC_DOWN && s->hist_cur < s->hist_count) s->hist_cur++;
	filson_rls_load_hist(s, s->hist_cur);
	filson_refresh_line_cursor(s->buf, s->cur);
	return 1;
}

static int
filson_rls_escape(struct filson_rl *s)
{
	int esc1, esc2;
	char pwd[256];
	int pwd_len;

	esc1 = getchar();
	if (esc1 == FILSON_ESC_CSI) {
		esc2 = getchar();
		if (esc2 == FILSON_ESC_UP || esc2 == FILSON_ESC_DOWN ||
		    esc2 == FILSON_ESC_RIGHT || esc2 == FILSON_ESC_LEFT)
			return filson_rls_esc_csi(s, esc2);
		return 1;
	}
	if (esc1 == 's' || esc1 == 'S') {
		filson_toggle_prefix(&s->buf, &s->bufsize, &s->pos, &s->cur, "sudo ");
		filson_refresh_line_cursor(s->buf, s->cur);
		return 1;
	}
	if (esc1 == '#') {
		filson_toggle_prefix(&s->buf, &s->bufsize, &s->pos, &s->cur, "# ");
		filson_refresh_line_cursor(s->buf, s->cur);
		return 1;
	}
	if (esc1 == 'i' || esc1 == 'I') {
		if (getcwd(pwd, sizeof(pwd)) != NULL) {
			pwd_len = strlen(pwd);
			filson_rls_grow(s, pwd_len + 1);
			memmove(s->buf + s->cur + pwd_len, s->buf + s->cur, s->pos - s->cur + 1);
			memcpy(s->buf + s->cur, pwd, pwd_len);
			s->cur += pwd_len; s->pos += pwd_len;
			filson_refresh_line_cursor(s->buf, s->cur);
		}
	}
	return 1;
}

static int
filson_rls_ctrl(struct filson_rl *s, int key)
{
	int start, ylen;

	switch (key) {
	case FILSON_KEY_CTRL_A: s->cur = 0; break;
	case FILSON_KEY_CTRL_E: s->cur = s->pos; break;
	case FILSON_KEY_CTRL_B: if (s->cur > 0) s->cur--; break;
	case FILSON_KEY_CTRL_F: if (s->cur < s->pos) s->cur++; break;
	case FILSON_KEY_CTRL_S:
		filson_toggle_prefix(&s->buf, &s->bufsize, &s->pos, &s->cur, "sudo ");
		break;
	case FILSON_KEY_CTRL_L: printf("\033[2J\033[H"); filson_refresh_line_cursor(s->buf, s->cur); return 1;
	case FILSON_KEY_CTRL_K:
		if (s->cur < s->pos) {
			filson_set_kill_buffer(&s->kill_buf, s->buf + s->cur, s->pos - s->cur);
			s->pos = s->cur; s->buf[s->pos] = '\0';
		}
		break;
	case FILSON_KEY_CTRL_U:
		if (s->cur > 0) {
			filson_set_kill_buffer(&s->kill_buf, s->buf, s->cur);
			memmove(s->buf, s->buf + s->cur, s->pos - s->cur + 1);
			s->pos -= s->cur; s->cur = 0;
		}
		break;
	case FILSON_KEY_CTRL_W:
		start = s->cur;
		while (start > 0 && isspace((unsigned char)s->buf[start - 1])) start--;
		while (start > 0 && !isspace((unsigned char)s->buf[start - 1])) start--;
		if (start < s->cur) {
			filson_set_kill_buffer(&s->kill_buf, s->buf + start, s->cur - start);
			memmove(s->buf + start, s->buf + s->cur, s->pos - s->cur + 1);
			s->pos -= (s->cur - start); s->cur = start;
		}
		break;
	case FILSON_KEY_CTRL_Y:
		if (s->kill_buf != NULL) {
			ylen = strlen(s->kill_buf);
			filson_rls_grow(s, ylen + 1);
			memmove(s->buf + s->cur + ylen, s->buf + s->cur, s->pos - s->cur + 1);
			memcpy(s->buf + s->cur, s->kill_buf, ylen);
			s->cur += ylen; s->pos += ylen; s->hist_cur = s->hist_count;
		}
		break;
	case FILSON_KEY_CTRL_P: if (s->hist_cur > 0) { s->hist_cur--; filson_rls_load_hist(s, s->hist_cur); } break;
	case FILSON_KEY_CTRL_N: s->hist_cur++; filson_rls_load_hist(s, s->hist_cur); break;
	case FILSON_KEY_CTRL_D:
		if (s->cur < s->pos) {
			memmove(s->buf + s->cur, s->buf + s->cur + 1, s->pos - s->cur);
			s->pos--; s->buf[s->pos] = '\0';
		}
		break;
	default: return 0;
	}
	filson_refresh_line_cursor(s->buf, s->cur);
	return 1;
}

static void
filson_rls_init(struct filson_rl *s, struct termios *oldt_p)
{
	struct termios newt;

	s->bufsize = FILSON_RL_BUFSIZE;
	s->pos = 0; s->cur = 0; s->kill_buf = NULL;
	s->buf = malloc(sizeof(char) * s->bufsize);
	if (!s->buf) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	s->buf[0] = '\0';
	s->interactive = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, oldt_p) == 0;
	s->hist_count = filson_history_count_entries();
	s->hist_cur = s->hist_count;
	if (s->interactive) {
		newt = *oldt_p;
		newt.c_lflag &= ~(ICANON | ECHO);
		newt.c_iflag &= ~(IXON | IXOFF);
		newt.c_cc[VMIN] = 1; newt.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
}

static int
filson_rls_key(struct filson_rl *s, int key, struct termios *oldt_p)
{
	if (s->interactive && key == FILSON_KEY_ESCAPE) { filson_rls_escape(s); return 1; }
	if (s->interactive && key == FILSON_KEY_TAB) {
		filson_handle_autocomplete(&s->buf, &s->bufsize, &s->pos,
		    builtin_str, filson_num_builtins(), filson_refresh_line);
		return 1;
	}
	if (key == EOF || (s->interactive && key == FILSON_KEY_CTRL_D && s->pos == 0)) {
		if (s->interactive) tcsetattr(STDIN_FILENO, TCSANOW, oldt_p);
		free(s->kill_buf); free(s->buf); s->buf = NULL;
		return -1;
	}
	if (key == FILSON_KEY_NEWLINE) {
		if (s->interactive) { tcsetattr(STDIN_FILENO, TCSANOW, oldt_p); printf("\n"); }
		s->buf[s->pos] = '\0';
		free(s->kill_buf); s->kill_buf = NULL;
		return 0;
	}
	if (s->interactive && (key == FILSON_KEY_BACKSPACE_DEL || key == '\b' ||
	    key == FILSON_KEY_BACKSPACE)) {
		if (s->cur > 0) {
			memmove(s->buf + s->cur - 1, s->buf + s->cur, s->pos - s->cur + 1);
			s->cur--; s->pos--; s->hist_cur = s->hist_count;
			filson_refresh_line_cursor(s->buf, s->cur);
		}
		return 1;
	}
	if (s->interactive && key >= FILSON_KEY_PRINTABLE_MIN && key <= FILSON_KEY_PRINTABLE_MAX) {
		filson_rls_grow(s, 2);
		memmove(s->buf + s->cur + 1, s->buf + s->cur, s->pos - s->cur + 1);
		s->buf[s->cur] = (char)key; s->pos++; s->cur++;
		s->hist_cur = s->hist_count;
		filson_refresh_line_cursor(s->buf, s->cur);
		return 1;
	}
	if (s->interactive) { filson_rls_ctrl(s, key); return 1; }
	s->buf[s->pos] = key;
	s->pos++;
	if (s->pos >= s->bufsize) {
		s->bufsize += FILSON_RL_BUFSIZE;
		s->buf = realloc(s->buf, s->bufsize);
		if (!s->buf) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	}
	return 1;
}

char *
filson_read_line(void)
{
	struct filson_rl s;
	struct termios oldt;
	fd_set rfds;
	struct timeval tv;
	int key, ready, r;

	filson_rls_init(&s, &oldt);
	{ int _iter = 0; while (_iter++ < FILSON_READLINE_MAX_ITER) {
		if (s.interactive) {
			FD_ZERO(&rfds); FD_SET(STDIN_FILENO, &rfds);
			tv.tv_sec = FILSON_IDLE_TIMEOUT_SECS; tv.tv_usec = 0;
			ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
			if (ready == 0) {
				tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
				free(s.kill_buf); free(s.buf);
				fprintf(stderr, "\nfilson: idle timeout (45 minutes) - session ended\n");
				return NULL;
			}
		}
		key = getchar();
		r = filson_rls_key(&s, key, &oldt);
		if (r < 0) return NULL;
		if (r == 0) return s.buf;
	} }
	return NULL;
}

static void
filson_sl_push_token(char ***tokens_p, int *pos_p, int *bufsize_p, char *tok)
{
	(*tokens_p)[*pos_p] = tok;
	(*pos_p)++;
	if (*pos_p >= *bufsize_p) {
		*bufsize_p += FILSON_TOK_BUFSIZE;
		*tokens_p = realloc(*tokens_p, *bufsize_p * sizeof(char *));
		if (!*tokens_p) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	}
}

static void
filson_sl_store_token(char ***tokens_p, int *pos_p, int *bufsize_p,
    char *tokbuf, int j, int started_in_single, int started_in_double,
    int unquoted_start, int eq_tilde_escaped)
{
	char *token_copy;
	int eqpos;

	if (j == 0 && !started_in_double && !started_in_single) return;
	tokbuf[j] = '\0';
	eqpos = -1;
	for (int k = 0; k < j; k++) { if (tokbuf[k] == '=') { eqpos = k; break; } }
	if (eqpos >= 0 && tokbuf[eqpos + 1] == '~' && !eq_tilde_escaped) {
		char *texp = filson_tilde_expand(tokbuf + eqpos + 1);
		if (texp != NULL) {
			int tlen = strlen(texp);
			token_copy = malloc(eqpos + 1 + tlen + 1);
			if (!token_copy) { free(texp); fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
			memcpy(token_copy, tokbuf, eqpos + 1);
			memcpy(token_copy + eqpos + 1, texp, tlen + 1);
			free(texp);
			filson_sl_push_token(tokens_p, pos_p, bufsize_p, token_copy);
			return;
		}
	}
	if (unquoted_start && tokbuf[0] == '~') {
		char *texp = filson_tilde_expand(tokbuf);
		if (texp != NULL) { filson_sl_push_token(tokens_p, pos_p, bufsize_p, texp); return; }
	}
	token_copy = malloc(j + 1 + ((started_in_single || started_in_double) ? 1 : 0));
	if (!token_copy) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	if (started_in_single) { token_copy[0] = '\x01'; memcpy(token_copy + 1, tokbuf, j + 1); }
	else if (started_in_double) { token_copy[0] = '\x02'; memcpy(token_copy + 1, tokbuf, j + 1); }
	else memcpy(token_copy, tokbuf, j + 1);
	filson_sl_push_token(tokens_p, pos_p, bufsize_p, token_copy);
}

static int
filson_sl_scan_token(const char *line, int i, char *tokbuf, int tokbuf_size,
    int *j_p, int *in_single_p, int *in_double_p,
    int *brace_depth_p, int *paren_depth_p,
    int *unquoted_start_p, int *eq_tilde_escaped_p,
    int *started_in_single_p, int *started_in_double_p)
{
	int j = *j_p;

	while (line[i] != '\0') {
		if (!*in_double_p && line[i] == '\'') {
			if (!*in_single_p && j == 0) *started_in_single_p = 1;
			*in_single_p = !*in_single_p; i++; continue;
		}
		if (!*in_single_p && line[i] == '"') {
			if (!*in_double_p && j == 0) *started_in_double_p = 1;
			if (*brace_depth_p > 0 && j < tokbuf_size - 1) tokbuf[j++] = '\x05';
			*in_double_p = !*in_double_p; i++; continue;
		}
		if (!*in_single_p && !*in_double_p) {
			if (*brace_depth_p == 0 && line[i] == '\\' && line[i+1] != '\0' && line[i+1] != '\n') {
				if (j == 0) *unquoted_start_p = 0;
				if (j > 0 && tokbuf[j-1] == '=' && line[i+1] == '~') *eq_tilde_escaped_p = 1;
				i++;
				if (j < tokbuf_size - 1) tokbuf[j++] = line[i];
				i++; continue;
			}
			if (line[i] == '$' && line[i+1] == '{') {
				(*brace_depth_p)++;
				if (j < tokbuf_size - 2) { tokbuf[j++] = line[i]; tokbuf[j++] = line[i+1]; }
				i += 2; continue;
			}
			if (*brace_depth_p > 0 && line[i] == '}') {
				(*brace_depth_p)--; if (j < tokbuf_size - 1) tokbuf[j++] = line[i];
				i++; continue;
			}
			if (line[i] == '$' && line[i+1] == '(') {
				(*paren_depth_p)++;
				if (j < tokbuf_size - 2) { tokbuf[j++] = line[i]; tokbuf[j++] = line[i+1]; }
				i += 2; continue;
			}
			if (*paren_depth_p > 0 && line[i] == ')') {
				(*paren_depth_p)--;
				if (j < tokbuf_size - 1) tokbuf[j++] = line[i];
				i++; continue;
			}
			if (*brace_depth_p == 0 && *paren_depth_p == 0 &&
			    (line[i] == ' ' || line[i] == '\t')) break;
		} else if (*in_double_p && !*in_single_p) {
			if (line[i] == '$' && line[i+1] == '{') (*brace_depth_p)++;
			else if (*brace_depth_p > 0 && line[i] == '}') (*brace_depth_p)--;
		}
		if (j < tokbuf_size - 1) {
			if (j == 0 && !*in_single_p && !*in_double_p) *unquoted_start_p = 1;
			tokbuf[j++] = line[i];
		}
		i++;
	}
	*j_p = j;
	return i;
}

char **
filson_split_line(char *line)
{
	int bufsize, position;
	char **tokens;
	char tokbuf[4096];
	int i;
	int in_single, in_double, brace_depth, paren_depth;
	int unquoted_start, eq_tilde_escaped;
	int started_in_single, started_in_double;

	bufsize = FILSON_TOK_BUFSIZE;
	position = 0;
	tokens = malloc(bufsize * sizeof(char *));
	if (!tokens) { fprintf(stderr, "filson: allocation error\n"); exit(EXIT_FAILURE); }
	i = 0;
	while (line[i] != '\0') {
		while (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') i++;
		if (line[i] == '\0') break;
		int j = 0; in_single = 0; in_double = 0;
		brace_depth = 0; paren_depth = 0;
		unquoted_start = 0; eq_tilde_escaped = 0;
		started_in_single = 0; started_in_double = 0;
		i = filson_sl_scan_token(line, i, tokbuf, (int)sizeof(tokbuf), &j,
		    &in_single, &in_double, &brace_depth, &paren_depth,
		    &unquoted_start, &eq_tilde_escaped,
		    &started_in_single, &started_in_double);
		filson_sl_store_token(&tokens, &position, &bufsize,
		    tokbuf, j, started_in_single, started_in_double,
		    unquoted_start, eq_tilde_escaped);
	}
	tokens[position] = NULL;
	return tokens;
}

static int
filson_heredoc_find(const char *line, char *delim_out, int *start_pos, int *end_pos)
{
	int in_single;
	int in_double;
	int arith_depth;
	int dstart;
	int dend;

	in_single = 0;
	in_double = 0;
	arith_depth = 0;
	for (int i = 0; line[i] != '\0'; i++) {
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
filson_phd_inline(const char *line, int hd_start, int hd_end, const char *delim, int fd)
{
	char *new_line, *seg, *expanded_line;
	const char *p, *lend, *after_hdoc;
	int dlen, llen, n;

	dlen = strlen(delim);
	p = line + hd_end + 1;
	after_hdoc = NULL;
	while (*p != '\0') {
		lend = p;
		while (*lend != '\0' && *lend != '\n') lend++;
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
			(void)write(fd, expanded_line, n);
			(void)write(fd, "\n", 1);
			if (expanded_line != seg) free(expanded_line);
			free(seg);
		}
		if (*lend == '\0') break;
		p = lend + 1;
	}
	(void)close(fd);
	{
		int pfx = hd_start;
		int tlen = strlen(filson_heredoc_tmppath);
		int sfxlen = after_hdoc ? (int)strlen(after_hdoc) : 0;
		new_line = malloc(pfx + 2 + tlen + sfxlen + 2);
		if (new_line == NULL) {
			(void)unlink(filson_heredoc_tmppath);
			filson_heredoc_tmppath[0] = '\0';
			return NULL;
		}
		snprintf(new_line, pfx + 2 + tlen + sfxlen + 2,
		    "%.*s< %s%s", pfx, line, filson_heredoc_tmppath,
		    after_hdoc ? after_hdoc : "");
	}
	return new_line;
}

static char *
filson_phd_interactive(const char *line, int hd_start, int hd_end,
    const char *delim, int fd)
{
	char *body_line, *expanded_line, *new_line;
	int interactive = isatty(STDIN_FILENO);
	int n;

	{ int _iter = 0; while (_iter++ < FILSON_HEREDOC_MAX_LINES) {
		if (interactive) (void)write(STDOUT_FILENO, "heredoc> ", 9);
		body_line = filson_read_line();
		if (body_line == NULL) break;
		if (strcmp(body_line, delim) == 0) { free(body_line); break; }
		expanded_line = filson_expand_string_variables(body_line);
		n = strlen(expanded_line);
		(void)write(fd, expanded_line, n);
		(void)write(fd, "\n", 1);
		if (expanded_line != body_line) free(expanded_line);
		free(body_line);
	} }
	(void)close(fd);
	new_line = malloc(hd_start + strlen(filson_heredoc_tmppath) +
	    (strlen(line) - hd_end) + 4);
	if (new_line == NULL) {
		(void)unlink(filson_heredoc_tmppath);
		filson_heredoc_tmppath[0] = '\0';
		return NULL;
	}
	snprintf(new_line,
	    hd_start + strlen(filson_heredoc_tmppath) + (strlen(line) - hd_end) + 4,
	    "%.*s< %s%s", hd_start, line, filson_heredoc_tmppath, line + hd_end);
	return new_line;
}

static char *
filson_prepare_heredoc(const char *line)
{
	char delim[256];
	int hd_start, hd_end, fd;

	if (!filson_heredoc_find(line, delim, &hd_start, &hd_end)) return NULL;
	strcpy(filson_heredoc_tmppath, "/tmp/filson_hdoc_XXXXXX");
	fd = mkstemp(filson_heredoc_tmppath);
	if (fd < 0) return NULL;
	if (line[hd_end] == '\n')
		return filson_phd_inline(line, hd_start, hd_end, delim, fd);
	return filson_phd_interactive(line, hd_start, hd_end, delim, fd);
}

static const char *
filson_fp_scan_name(const char *line, char *name_out, int name_max)
{
	const char *p = line;
	int name_len;

	while (*p == ' ' || *p == '\t') p++;
	if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_'))
		return NULL;
	name_len = 0;
	while ((p[name_len] >= 'a' && p[name_len] <= 'z') ||
	       (p[name_len] >= 'A' && p[name_len] <= 'Z') ||
	       (p[name_len] >= '0' && p[name_len] <= '9') ||
	       p[name_len] == '_')
		name_len++;
	if (name_len == 0 || name_len >= name_max) return NULL;
	memcpy(name_out, p, name_len);
	name_out[name_len] = '\0';
	return p + name_len;
}

static const char *
filson_fp_scan_parens(const char *p)
{
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '(') return NULL;
	p++;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != ')') return NULL;
	p++;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '{') return NULL;
	return p;
}

static int
filson_fp_extract_body(const char *brace_start, char **body_out, int *needs_more)
{
	const char *q = brace_start;
	const char *body_start, *body_end, *brace_close;
	int depth = 0, body_len;

	brace_close = NULL;
	while (*q != '\0') {
		if (*q == '{') depth++;
		else if (*q == '}') { depth--; if (depth == 0) { brace_close = q; break; } }
		q++;
	}
	if (brace_close == NULL) { *needs_more = 1; *body_out = NULL; return 1; }
	body_start = brace_start + 1;
	body_end = brace_close;
	while (body_start < body_end && (*body_start == ' ' || *body_start == '\t' ||
	    *body_start == '\n' || *body_start == ';'))
		body_start++;
	while (body_end > body_start && (*(body_end - 1) == ' ' || *(body_end - 1) == '\t' ||
	    *(body_end - 1) == ';' || *(body_end - 1) == '\n'))
		body_end--;
	body_len = body_end - body_start;
	*body_out = malloc(body_len + 1);
	if (*body_out == NULL) return 0;
	memcpy(*body_out, body_start, body_len);
	(*body_out)[body_len] = '\0';
	*needs_more = 0;
	return 1;
}

int
filson_funcdef_parse(const char *line, char *name_out, int name_max,
    char **body_out, int *needs_more)
{
	const char *after_name, *brace;

	after_name = filson_fp_scan_name(line, name_out, name_max);
	if (after_name == NULL) return 0;
	brace = filson_fp_scan_parens(after_name);
	if (brace == NULL) return 0;
	return filson_fp_extract_body(brace, body_out, needs_more);
}

static void
filson_nc_keyword(const char *p, const char *buf, int *loop_depth_p,
    int *if_depth_p, int *case_depth_p)
{
	if (p != buf && !isspace((unsigned char)p[-1]) && p[-1] != ';' && p[-1] != '\n') return;
	if (strncmp(p, "for", 3) == 0 && (isspace((unsigned char)p[3]) || p[3] == '\0'))
		(*loop_depth_p)++;
	else if (strncmp(p, "while", 5) == 0 && (isspace((unsigned char)p[5]) || p[5] == '\0'))
		(*loop_depth_p)++;
	else if (strncmp(p, "until", 5) == 0 && (isspace((unsigned char)p[5]) || p[5] == '\0'))
		(*loop_depth_p)++;
	else if (strncmp(p, "if", 2) == 0 && (isspace((unsigned char)p[2]) || p[2] == '\0'))
		(*if_depth_p)++;
	else if (strncmp(p, "case", 4) == 0 && (isspace((unsigned char)p[4]) || p[4] == '\0'))
		(*case_depth_p)++;
	else if (strncmp(p, "done", 4) == 0 &&
	    (p[4] == '\0' || isspace((unsigned char)p[4]) || p[4] == ';'))
		{ if (*loop_depth_p > 0) (*loop_depth_p)--; }
	else if (strncmp(p, "fi", 2) == 0 &&
	    (p[2] == '\0' || isspace((unsigned char)p[2]) || p[2] == ';'))
		{ if (*if_depth_p > 0) (*if_depth_p)--; }
	else if (strncmp(p, "esac", 4) == 0 &&
	    (p[4] == '\0' || isspace((unsigned char)p[4]) || p[4] == ';'))
		{ if (*case_depth_p > 0) (*case_depth_p)--; }
}

int
filson_needs_continuation(const char *buf)
{
	int in_single, in_double, loop_depth, if_depth, case_depth, paren_depth;
	const char *p;

	in_single = in_double = loop_depth = if_depth = case_depth = paren_depth = 0;
	p = buf;
	while (*p != '\0') {
		if (!in_double && *p == '\'') { in_single = !in_single; p++; continue; }
		if (!in_single && *p == '"') { in_double = !in_double; p++; continue; }
		if (in_single || in_double) { p++; continue; }
		if (*p == '$' && *(p + 1) == '(') { paren_depth++; p += 2; continue; }
		if (*p == '(' && paren_depth > 0) { paren_depth++; p++; continue; }
		if (*p == ')' && paren_depth > 0) { paren_depth--; p++; continue; }
		filson_nc_keyword(p, buf, &loop_depth, &if_depth, &case_depth);
		p++;
	}
	return in_single || in_double || loop_depth > 0 || if_depth > 0 ||
	    case_depth > 0 || paren_depth > 0;
}

static void
filson_loop_funcdef_accum(char *resolved, char *func_name, char **func_body_p)
{
	const char *pp;
	int depth, accum_len, bufsize;
	char *accum, *more, *tmp;
	int needs_more_dummy;

	bufsize = strlen(resolved) + 4096;
	accum = malloc(bufsize);
	if (accum == NULL) return;
	accum_len = strlen(resolved);
	strcpy(accum, resolved);
	depth = 0;
	pp = resolved;
	while (*pp != '\0') { if (*pp == '{') depth++; else if (*pp == '}') depth--; pp++; }
	while (depth > 0) {
		if (isatty(STDIN_FILENO)) (void)write(STDOUT_FILENO, "> ", 2);
		more = filson_read_line();
		if (more == NULL) break;
		if (accum_len + (int)strlen(more) + 4 > bufsize) {
			bufsize = accum_len + strlen(more) + 4096;
			tmp = realloc(accum, bufsize);
			if (tmp == NULL) { free(more); break; }
			accum = tmp;
		}
		accum[accum_len++] = ';';
		memcpy(accum + accum_len, more, strlen(more));
		accum_len += strlen(more);
		accum[accum_len] = '\0';
		pp = more;
		while (*pp != '\0') { if (*pp == '{') depth++; else if (*pp == '}') depth--; pp++; }
		free(more);
	}
	filson_funcdef_parse(accum, func_name, 256, func_body_p, &needs_more_dummy);
	free(accum);
}

static int
filson_loop_continuation(char *resolved, char *line, int *status_p)
{
	char *accum, *more, *tmp, *hd_line;
	int accum_len, bufsize;

	bufsize = strlen(resolved) + 4096;
	accum = malloc(bufsize);
	if (accum == NULL) return 0;
	accum_len = strlen(resolved);
	strcpy(accum, resolved);
	while (filson_needs_continuation(accum)) {
		if (isatty(STDIN_FILENO)) (void)write(STDOUT_FILENO, "> ", 2);
		more = filson_read_line();
		if (more == NULL) break;
		if (accum_len + (int)strlen(more) + 4 > bufsize) {
			bufsize = accum_len + strlen(more) + 4096;
			tmp = realloc(accum, bufsize);
			if (tmp == NULL) { free(more); break; }
			accum = tmp;
		}
		accum[accum_len++] = '\n';
		memcpy(accum + accum_len, more, strlen(more));
		accum_len += strlen(more);
		accum[accum_len] = '\0';
		free(more);
	}
	free(resolved); free(line);
	hd_line = filson_prepare_heredoc(accum);
	if (hd_line != NULL) {
		*status_p = filson_execute_and_chain(hd_line);
		free(hd_line);
		if (filson_heredoc_tmppath[0] != '\0') {
			(void)unlink(filson_heredoc_tmppath);
			filson_heredoc_tmppath[0] = '\0';
		}
	} else {
		*status_p = filson_execute_and_chain(accum);
	}
	free(accum);
	return 1;
}

int
filson_loop(void)
{
	char *line, *resolved, *hd_line, *func_body;
	char func_name[256];
	int func_needs_more, status;

	filson_print_startup_banner();
	status = 1;
	do {
		filson_reap_background_jobs();
		if (isatty(STDIN_FILENO)) { printf(FILSON_PROMPT); (void)fflush(stdout); }
		line = filson_read_line();
		if (line == NULL) { if (isatty(STDIN_FILENO)) printf("\n"); break; }
		{
			char *trimmed = filson_trim(line);
			if (strcmp(trimmed, "!") == 0) {
				const char *last = filson_history_count_entries() > 0 ?
				    filson_history_get(filson_history_count_entries() - 1) : NULL;
				if (last == NULL) fprintf(stderr, "filson: no commands in history\n");
				else printf("%s\n", last);
				free(line); continue;
			}
		}
		resolved = filson_resolve_history(line);
		if (resolved == NULL) { free(line); continue; }
		filson_add_history(resolved);
		if (filson_funcdef_parse(resolved, func_name, sizeof(func_name),
		    &func_body, &func_needs_more)) {
			if (func_needs_more)
				filson_loop_funcdef_accum(resolved, func_name, &func_body);
			if (func_body != NULL) { filson_define_function(func_name, func_body); free(func_body); }
			free(resolved); free(line); continue;
		}
		if (filson_needs_continuation(resolved)) {
			if (filson_loop_continuation(resolved, line, &status)) continue;
		}
		hd_line = filson_prepare_heredoc(resolved);
		if (hd_line != NULL) {
			status = filson_execute_and_chain(hd_line);
			free(hd_line);
			if (filson_heredoc_tmppath[0] != '\0') {
				(void)unlink(filson_heredoc_tmppath);
				filson_heredoc_tmppath[0] = '\0';
			}
		} else {
			status = filson_execute_and_chain(resolved);
		}
		free(resolved); free(line);
	} while (status);
	filson_clear_history();
	{
		extern int filson_exit_called;
		extern int filson_exit_code;
		if (filson_exit_called) return filson_exit_code == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (!isatty(STDIN_FILENO)) return filson_last_cmd_success ? EXIT_SUCCESS : EXIT_FAILURE;
	return EXIT_SUCCESS;
}
