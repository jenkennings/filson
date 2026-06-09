#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "jobcontrol.h"

#define FILSON_MAX_JOBS 64

extern int filson_last_cmd_success;

struct filson_job {
	int used;
	int id;
	pid_t pid;
	int stopped;
	char *segment;
};

static struct filson_job filson_jobs_table[FILSON_MAX_JOBS];
static int filson_next_job_id = 1;

static int
filson_find_job_slot_by_id(int job_id)
{
	for (int i = 0; i < FILSON_MAX_JOBS; i++) {
		if (filson_jobs_table[i].used && filson_jobs_table[i].id == job_id) {
			return i;
		}
	}
	return -1;
}

static int
filson_find_job_slot_by_pid(pid_t pid)
{
	for (int i = 0; i < FILSON_MAX_JOBS; i++) {
		if (filson_jobs_table[i].used && filson_jobs_table[i].pid == pid) {
			return i;
		}
	}
	return -1;
}

static int
filson_find_recent_job_slot(void)
{
	int best_slot, best_id;

	best_slot = -1;
	best_id = -1;
	for (int i = 0; i < FILSON_MAX_JOBS; i++) {
		if (filson_jobs_table[i].used && filson_jobs_table[i].id > best_id) {
			best_id = filson_jobs_table[i].id;
			best_slot = i;
		}
	}
	return best_slot;
}

static void
filson_remove_job_slot(int slot)
{
	if (slot < 0 || slot >= FILSON_MAX_JOBS || !filson_jobs_table[slot].used) {
		return;
	}
	free(filson_jobs_table[slot].segment);
	filson_jobs_table[slot].segment = NULL;
	filson_jobs_table[slot].used = 0;
}

int
filson_add_job(pid_t pid, const char *segment, int stopped)
{
	for (int i = 0; i < FILSON_MAX_JOBS; i++) {
		if (!filson_jobs_table[i].used) {
			filson_jobs_table[i].used = 1;
			filson_jobs_table[i].id = filson_next_job_id;
			filson_jobs_table[i].pid = pid;
			filson_jobs_table[i].stopped = stopped;
			filson_jobs_table[i].segment = strdup(segment);
			filson_next_job_id++;
			return filson_jobs_table[i].id;
		}
	}
	return -1;
}

void
filson_reap_background_jobs(void)
{
	int status, slot;
	pid_t pid;

	while (1) {
		pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
		if (pid <= 0) {
			break;
		}
		slot = filson_find_job_slot_by_pid(pid);
		if (slot < 0) {
			continue;
		}
		if (WIFEXITED(status)) {
			printf("\n[%d] Done %s\n",
			    filson_jobs_table[slot].id,
			    filson_jobs_table[slot].segment != NULL ? filson_jobs_table[slot].segment : "");
			filson_remove_job_slot(slot);
		} else if (WIFSIGNALED(status)) {
			printf("\n[%d] Killed %s\n",
			    filson_jobs_table[slot].id,
			    filson_jobs_table[slot].segment != NULL ? filson_jobs_table[slot].segment : "");
			filson_remove_job_slot(slot);
		} else if (WIFSTOPPED(status)) {
			filson_jobs_table[slot].stopped = 1;
			printf("\n[%d] Stopped %s\n",
			    filson_jobs_table[slot].id,
			    filson_jobs_table[slot].segment != NULL ? filson_jobs_table[slot].segment : "");
		} else if (WIFCONTINUED(status)) {
			filson_jobs_table[slot].stopped = 0;
		}
	}
}

int
filson_jobs(char **args)
{
	(void)args;
	filson_reap_background_jobs();
	for (int i = 0; i < FILSON_MAX_JOBS; i++) {
		if (filson_jobs_table[i].used) {
			printf("[%d] %s %d %s\n",
			    filson_jobs_table[i].id,
			    filson_jobs_table[i].stopped ? "Stopped" : "Running",
			    filson_jobs_table[i].pid,
			    filson_jobs_table[i].segment != NULL ? filson_jobs_table[i].segment : "");
		}
	}
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_fg(char **args)
{
	int slot, status;
	char *endptr;
	long id;

	if (args[1] == NULL) {
		slot = filson_find_recent_job_slot();
	} else {
		id = strtol(args[1], &endptr, 10);
		if (endptr == args[1] || *endptr != '\0' || id <= 0) {
			fprintf(stderr, "filson: fg expects a job id\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		slot = filson_find_job_slot_by_id((int)id);
	}
	if (slot < 0) {
		fprintf(stderr, "filson: fg: no such job\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (kill(filson_jobs_table[slot].pid, SIGCONT) != 0) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_jobs_table[slot].stopped = 0;
	if (waitpid(filson_jobs_table[slot].pid, &status, WUNTRACED) < 0) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (WIFSTOPPED(status)) {
		filson_jobs_table[slot].stopped = 1;
		filson_last_cmd_success = 0;
	} else {
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			filson_last_cmd_success = 1;
		} else {
			filson_last_cmd_success = 0;
		}
		filson_remove_job_slot(slot);
	}
	return 1;
}

int
filson_bg(char **args)
{
	int slot;
	char *endptr;
	long id;

	if (args[1] == NULL) {
		slot = filson_find_recent_job_slot();
	} else {
		id = strtol(args[1], &endptr, 10);
		if (endptr == args[1] || *endptr != '\0' || id <= 0) {
			fprintf(stderr, "filson: bg expects a job id\n");
			filson_last_cmd_success = 0;
			return 1;
		}
		slot = filson_find_job_slot_by_id((int)id);
	}
	if (slot < 0) {
		fprintf(stderr, "filson: bg: no such job\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	if (kill(filson_jobs_table[slot].pid, SIGCONT) != 0) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_jobs_table[slot].stopped = 0;
	filson_last_cmd_success = 1;
	return 1;
}

int
filson_wait(char **args)
{
	int slot, status;
	char *endptr;
	long id;
	pid_t pid;

	if (args[1] == NULL) {
		for (int i = 0; i < FILSON_MAX_JOBS; i++) {
			if (filson_jobs_table[i].used) {
				pid = waitpid(filson_jobs_table[i].pid, &status, 0);
				if (pid > 0) {
					filson_jobs_table[i].used = 0;
				}
			}
		}
		filson_last_cmd_success = 1;
		return 1;
	}
	id = strtol(args[1], &endptr, 10);
	if (endptr == args[1] || *endptr != '\0' || id <= 0) {
		fprintf(stderr, "filson: wait expects a job id\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	slot = filson_find_job_slot_by_id((int)id);
	if (slot < 0) {
		fprintf(stderr, "filson: wait: no such job\n");
		filson_last_cmd_success = 0;
		return 1;
	}
	pid = waitpid(filson_jobs_table[slot].pid, &status, 0);
	if (pid < 0) {
		perror("filson");
		filson_last_cmd_success = 0;
		return 1;
	}
	filson_remove_job_slot(slot);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		filson_last_cmd_success = 1;
	} else {
		filson_last_cmd_success = 0;
	}
	return 1;
}