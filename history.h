#ifndef HISTORY_H
#define HISTORY_H

int filson_history(char **args);
void filson_add_history(const char *line);
void filson_clear_history(void);
char *filson_resolve_history(char *line);
char *filson_trim(char *s);

#endif