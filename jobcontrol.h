#ifndef JOBCONTROL_H
#define JOBCONTROL_H

#include <sys/types.h>

int filson_jobs(char **args);
int filson_fg(char **args);
int filson_bg(char **args);
int filson_add_job(pid_t pid, const char *segment, int stopped);
void filson_reap_background_jobs(void);

#endif