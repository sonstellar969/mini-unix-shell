#include "execute.h"

#include "builtin.h"
#include "jobs.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static void child_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    /* Suspension is intentionally not implemented. Leaving SIGTSTP at
     * SIG_DFL would stop a child while the shell is still blocked in wait. */
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_DFL);
}

static int open_input(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "msh: %s: %s\n", path, strerror(errno));
    }
    return fd;
}

static int open_output(const char *path, int append) {
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        fprintf(stderr, "msh: %s: %s\n", path, strerror(errno));
    }
    return fd;
}

static void restore_fd(int saved, int target) {
    if (saved >= 0) {
        if (dup2(saved, target) < 0) {
            fprintf(stderr, "msh: dup2: %s\n", strerror(errno));
        }
        close(saved);
    }
}

/* Install fd onto target. If open() returned target itself, leave it there. */
static int move_fd(int fd, int target) {
    if (fd == target) {
        return 0;
    }
    if (dup2(fd, target) < 0) {
        fprintf(stderr, "msh: dup2: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int redirect_one(const char *path, int input, int append, int target, int *saved) {
    int fd = input ? open_input(path) : open_output(path, append);
    if (fd < 0) {
        return -1;
    }
    int copy = dup(target);
    if (copy < 0) {
        fprintf(stderr, "msh: dup: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (move_fd(fd, target) < 0) {
        restore_fd(copy, target);
        return -1;
    }
    *saved = copy;
    return 0;
}

static int apply_redirs(const Command *cmd, int *saved_in, int *saved_out, int *saved_err) {
    *saved_in = -1;
    *saved_out = -1;
    *saved_err = -1;
    if (cmd->infile && redirect_one(cmd->infile, 1, 0, STDIN_FILENO, saved_in) != 0) {
        return -1;
    }
    if (cmd->outfile && redirect_one(cmd->outfile, 0, cmd->append, STDOUT_FILENO, saved_out) != 0) {
        restore_fd(*saved_in, STDIN_FILENO);
        *saved_in = -1;
        return -1;
    }
    if (cmd->errfile &&
        redirect_one(cmd->errfile, 0, cmd->err_append, STDERR_FILENO, saved_err) != 0) {
        restore_fd(*saved_out, STDOUT_FILENO);
        restore_fd(*saved_in, STDIN_FILENO);
        *saved_out = -1;
        *saved_in = -1;
        return -1;
    }
    return 0;
}

static int run_parent_builtin(Command *cmd) {
    int saved_in;
    int saved_out;
    int saved_err;
    fflush(stdout);
    fflush(stderr);
    if (apply_redirs(cmd, &saved_in, &saved_out, &saved_err) != 0) {
        return 1;
    }
    int status = builtin_run(cmd);
    fflush(stdout);
    fflush(stderr);
    restore_fd(saved_in, STDIN_FILENO);
    restore_fd(saved_out, STDOUT_FILENO);
    restore_fd(saved_err, STDERR_FILENO);
    return status;
}

static void close_pair_list(int *fds, int count) {
    for (int i = 0; i < count; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
            fds[i] = -1;
        }
    }
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

static void run_child(Pipeline *pipeline, int index, int *pipefds) {
    sh_in_child = 1;
    Command *cmd = &pipeline->cmds[index];
    int ncmds = pipeline->ncmds;
    pid_t pgid = sh_pgid;

    if (pgid == 0) {
        pgid = getpid();
    }
    setpgid(0, pgid);
    child_signals();

    if (index > 0 && dup2(pipefds[(index - 1) * 2], STDIN_FILENO) < 0) {
        _exit(1);
    }
    if (index < ncmds - 1 && dup2(pipefds[index * 2 + 1], STDOUT_FILENO) < 0) {
        _exit(1);
    }
    if (pipefds) {
        for (int fd_i = 0; fd_i < (ncmds - 1) * 2; fd_i++) {
            int fd = pipefds[fd_i];
            if (fd >= 0 && fd != STDIN_FILENO && fd != STDOUT_FILENO) {
                close(fd);
                pipefds[fd_i] = -1;
            }
        }
    }

    /* File redirections override the pipe ends for this stage, including
     * a stage that is neither first nor last. */
    if (cmd->infile) {
        int fd = open_input(cmd->infile);
        if (fd < 0 || move_fd(fd, STDIN_FILENO) < 0) {
            _exit(1);
        }
    }
    if (cmd->outfile) {
        int fd = open_output(cmd->outfile, cmd->append);
        if (fd < 0 || move_fd(fd, STDOUT_FILENO) < 0) {
            _exit(1);
        }
    }
    if (cmd->errfile) {
        int fd = open_output(cmd->errfile, cmd->err_append);
        if (fd < 0 || move_fd(fd, STDERR_FILENO) < 0) {
            _exit(1);
        }
    }

    if (builtin_is(cmd->argv[0])) {
        int status = builtin_run(cmd);
        _exit(status);
    }

    execvp(cmd->argv[0], cmd->argv);
    if (errno == ENOENT) {
        fprintf(stderr, "msh: %s: command not found\n", cmd->argv[0]);
        _exit(127);
    }
    fprintf(stderr, "msh: %s: %s\n", cmd->argv[0], strerror(errno));
    _exit(126);
}

static int wait_pipeline(pid_t pgid, pid_t last_pid) {
    int status = 0;
    int saw_last = 0;
    while (!saw_last) {
        int st;
        pid_t pid = waitpid(-pgid, &st, 0);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pid == last_pid) {
            status = decode_status(st);
            saw_last = 1;
        }
    }
    /* Collect the remaining members so they do not stay zombies. */
    while (1) {
        int st;
        pid_t pid = waitpid(-pgid, &st, 0);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }
    return status;
}

int execute_pipeline(Pipeline *pipeline, const char *cmdline) {
    if (pipeline->ncmds == 1 && !pipeline->background && builtin_is(pipeline->cmds[0].argv[0])) {
        return run_parent_builtin(&pipeline->cmds[0]);
    }

    int ncmds = pipeline->ncmds;
    int pipe_count = ncmds > 1 ? (ncmds - 1) * 2 : 0;
    int *pipefds = NULL;
    if (pipe_count) {
        pipefds = calloc((size_t)pipe_count, sizeof(int));
        if (!pipefds) {
            fprintf(stderr, "msh: out of memory\n");
            return 1;
        }
        for (int i = 0; i < pipe_count; i++) {
            pipefds[i] = -1;
        }
        for (int i = 0; i < ncmds - 1; i++) {
            int pair[2];
            if (pipe(pair) < 0) {
                fprintf(stderr, "msh: pipe: %s\n", strerror(errno));
                close_pair_list(pipefds, pipe_count);
                free(pipefds);
                return 1;
            }
            pipefds[i * 2] = pair[0];
            pipefds[i * 2 + 1] = pair[1];
        }
    }

    pid_t pids[SH_MAX_CMDS];
    pid_t pgid = 0;
    int started = 0;

    for (int i = 0; i < ncmds; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "msh: fork: %s\n", strerror(errno));
            if (pgid > 0) {
                kill(-pgid, SIGTERM);
            }
            for (int j = 0; j < started; j++) {
                waitpid(pids[j], NULL, 0);
            }
            close_pair_list(pipefds, pipe_count);
            free(pipefds);
            return 1;
        }
        if (pid == 0) {
            /* sh_pgid is borrowed so the child can join the pipeline group. */
            sh_pgid = pgid;
            run_child(pipeline, i, pipefds);
            _exit(1);
        }
        if (pgid == 0) {
            pgid = pid;
        }
        /* The child sets the same group. EACCES or ESRCH means it already ran. */
        (void)setpgid(pid, pgid);
        pids[started++] = pid;
    }

    close_pair_list(pipefds, pipe_count);
    free(pipefds);

    if (pipeline->background) {
        int id = jobs_add(pgid, pids, started, cmdline);
        if (id < 0) {
            kill(-pgid, SIGTERM);
            for (int j = 0; j < started; j++) {
                waitpid(pids[j], NULL, 0);
            }
            return 1;
        }
        fprintf(stderr, "[%d] %ld\n", id, (long)pgid);
        return 0;
    }

    if (sh_owns_tty) {
        tcsetpgrp(sh_terminal, pgid);
    }
    int status = wait_pipeline(pgid, pids[started - 1]);
    if (sh_owns_tty) {
        tcsetpgrp(sh_terminal, sh_pgid);
    }
    if (sh_interactive && status == 128 + SIGINT) {
        fputc('\n', stderr);
    }
    return status;
}
