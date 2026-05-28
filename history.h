#ifndef HISTORY_H
#define HISTORY_H

int filson_history(char **args);
void filson_add_history(const char *line);
void filson_clear_history(void);
char *filson_resolve_history(char *line);
char *filson_trim(char *s);
int filson_history_count_entries(void);
const char *filson_history_get(int idx);

#endif