#ifndef BUILTINS_H
#define BUILTINS_H

int filson_cd(char **args);
int filson_help(char **args);
int filson_exit(char **args);
int filson_set(char **args);
int filson_echo(char **args);
int filson_pwd(char **args);
int filson_clear(char **args);
int filson_unset(char **args);
int filson_export(char **args);
int filson_type(char **args);
int filson_alias(char **args);
int filson_ssh(char **args);
int filson_local(char **args);
int filson_return_stmt(char **args);
int filson_unset_func(char **args);
int filson_declare_func(char **args);
int filson_break(char **args);
int filson_continue(char **args);
int filson_read(char **args);
int filson_shift(char **args);
int filson_source(char **args);
int filson_dot(char **args);

#endif