#ifndef PIPELINES_H
#define PIPELINES_H

char *filson_normalize_script_ops(const char *line);
int filson_execute_and_chain(char *line);

#endif