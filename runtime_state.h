#ifndef FILSON_RUNTIME_STATE_H
#define FILSON_RUNTIME_STATE_H

char *filson_lookup_alias(const char *name);
void filson_set_alias(const char *name, const char *value);
void filson_print_aliases(void);
void filson_print_one_alias(const char *name);
int filson_remove_alias(const char *name);
void filson_remove_all_aliases(void);

void filson_define_function(const char *name, const char *body);
char *filson_lookup_function(const char *name);
int filson_unset_function(const char *name);
void filson_print_function_definition(const char *name);
void filson_print_all_function_definitions(void);
void filson_print_all_function_names(void);
void filson_print_all_function_declarations(void);

char *filson_get_pospar(int idx);
int filson_get_pospar_count(void);
int filson_shift_posparams(int n);
int filson_has_active_function(void);
void filson_declare_local(const char *name);
int filson_return_from_function(int ret_value);
int filson_call_function(const char *name, char **args);

#endif
