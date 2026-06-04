#ifndef SHELL_SESSION_H
#define SHELL_SESSION_H

extern const char *builtin_str[];

int filson_num_builtins(void);
char *filson_expand_string_variables(const char *str);
void filson_define_function(const char *name, const char *body);
char *filson_read_line(void);
char **filson_split_line(char *line);
int filson_loop(void);

#endif