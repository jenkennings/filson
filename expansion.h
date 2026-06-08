#ifndef FILSON_EXPANSION_H
#define FILSON_EXPANSION_H

char *filson_tilde_expand(const char *str);
char *filson_expand_string_variables(const char *str);
int filson_is_assignment_token(const char *token);
int filson_parse_assignment_token(const char *token, char **name_out, const char **value_out);

#endif
