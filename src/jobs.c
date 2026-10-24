#include "jobs.h"

#include <errno.h>
#include <sys/wait.h>

typedef struct {
    int used;
    int id;
    pid_t pgid;
    pid_t pids[SH_MAX_CMDS];
    int alive[SH_MAX_CMDS];
    int exit_status[SH_MAX_CMDS];
    int npids;
    char *cmdline;
} Job;

static Job table[SH_MAX_JOBS];
static int held_status;
static int held;

static void remember(int status) {
    held_status = status;
    held = 1;
}

static int decode_status(int st) {
    if (WIFEXITED(st)) {
        return WEXITSTATUS(st);
    }
    if (WIFSIGNALED(st)) {
        return 128 + WTERMSIG(st);
    }
    return 1;
}

static int next_id(void) {
    for (int id = 1; id < 100000; id++) {
        int used = 0;
        for (int i = 0; i < SH_MAX_JOBS; i++) {
            if (table[i].used && table[i].id == id) {
                used = 1;
                break;
            }
        }
        if (!used) {
            return id;
        }
    }
    return 1;
}

static void clear_job(Job *job, int announce) {
    if (announce && job->cmdline) {
        fprintf(stderr, "[%d] done %s\n", job->id, job->cmdline);
    }
    free(job->cmdline);
    memset(job, 0, sizeof *job);
}

static void note_pid(pid_t pid, int status, int announce) {
    for (int i = 0; i < SH_MAX_JOBS; i++) {
        Job *job = &table[i];
        if (!job->used) {
            continue;
        }
        int all_dead = 1;
        int matched = 0;
        for (int p = 0; p < job->npids; p++) {
            if (job->pids[p] == pid && job->alive[p]) {
                job->alive[p] = 0;
                job->exit_status[p] = status;
                matched = 1;
            }
            if (job->alive[p]) {
                all_dead = 0;
            }
        }
        if (matched && all_dead) {
            remember(job->exit_status[job->npids - 1]);
            clear_job(job, announce);
            return;
        }
    }
}

int jobs_add(pid_t pgid, const pid_t *pids, int npids, const char *cmdline) {
    for (int i = 0; i < SH_MAX_JOBS; i++) {
        if (table[i].used) {
            continue;
        }
        Job *job = &table[i];
        memset(job, 0, sizeof *job);
        job->used = 1;
        job->id = next_id();
        job->pgid = pgid;
        job->npids = npids;
        job->cmdline = xstrdup(cmdline ? cmdline : "");
        for (int p = 0; p < npids; p++) {
            job->pids[p] = pids[p];
            job->alive[p] = 1;
        }
        return job->id;
    }
    fprintf(stderr, "msh: too many background jobs\n");
    return -1;
}

void jobs_reap(int announce) {
    int st;
    while (1) {
        pid_t pid = waitpid(-1, &st, WNOHANG);
        if (pid == 0 || (pid < 0 && errno == ECHILD)) {
            return;
        }
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        note_pid(pid, decode_status(st), announce);
    }
}

void jobs_list(void) {
    jobs_reap(0);
    for (int i = 0; i < SH_MAX_JOBS; i++) {
        Job *job = &table[i];
        if (!job->used) {
            continue;
        }
        printf("[%d] running pgid=%ld %s\n", job->id, (long)job->pgid, job->cmdline);
    }
}

int jobs_wait_all(void) {
    for (int i = 0; i < SH_MAX_JOBS; i++) {
        Job *job = &table[i];
        if (!job->used) {
            continue;
        }
        for (int p = 0; p < job->npids; p++) {
            if (!job->alive[p]) {
                continue;
            }
            int st;
            pid_t pid;
            do {
                pid = waitpid(job->pids[p], &st, 0);
            } while (pid < 0 && errno == EINTR);
            if (pid > 0) {
                note_pid(pid, decode_status(st), 0);
            }
        }
    }
    if (!held) {
        return 0;
    }
    int status = held_status;
    held = 0;
    return status;
}
