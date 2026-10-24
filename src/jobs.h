#ifndef MSH_JOBS_H
#define MSH_JOBS_H

#include "shell.h"

/* Returns the job id, or -1 if the table is full. */
int jobs_add(pid_t pgid, const pid_t *pids, int npids, const char *cmdline);
void jobs_reap(int announce);
void jobs_list(void);
int jobs_wait_all(void);

#endif
