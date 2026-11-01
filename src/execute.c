#include "execute.h"

#include "builtin.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

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

/* point target at fd. if open already returned target, leave it. */
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

static int apply_redirs(const Command *cmd, int *saved_in, int *saved_out) {
    *saved_in = -1;
    *saved_out = -1;
    if (cmd->infile && redirect_one(cmd->infile, 1, 0, STDIN_FILENO, saved_in) != 0) {
        return -1;
    }
    if (cmd->outfile && redirect_one(cmd->outfile, 0, cmd->append, STDOUT_FILENO, saved_out) != 0) {
        restore_fd(*saved_in, STDIN_FILENO);
        *saved_in = -1;
        return -1;
    }
    return 0;
}

static int run_parent_builtin(Command *cmd) {
    int saved_in;
    int saved_out;
    fflush(stdout);
    fflush(stderr);
    if (apply_redirs(cmd, &saved_in, &saved_out) != 0) {
        return 1;
    }
    int status = builtin_run(cmd);
    fflush(stdout);
    fflush(stderr);
    restore_fd(saved_in, STDIN_FILENO);
    restore_fd(saved_out, STDOUT_FILENO);
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

static void child_redirect(const Command *cmd) {
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
}

static void run_child(Pipeline *pipeline, int index, int *pipefds) {
    sh_in_child = 1;
    Command *cmd = &pipeline->cmds[index];
    int ncmds = pipeline->ncmds;

    /* connect this stage to the previous and next pipe ends. */
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

    /* a named file replaces the pipe end for this stage. */
    child_redirect(cmd);

    if (builtin_is(cmd->argv[0])) {
        int status = builtin_run(cmd);
        _exit(status);
    }

    /* replace this child with the program. the parent keeps running. */
    execvp(cmd->argv[0], cmd->argv);
    if (errno == ENOENT) {
        fprintf(stderr, "msh: %s: command not found\n", cmd->argv[0]);
        _exit(127);
    }
    fprintf(stderr, "msh: %s: %s\n", cmd->argv[0], strerror(errno));
    _exit(126);
}

static int wait_pipeline(pid_t *pids, int count) {
    int status = 0;
    for (int i = 0; i < count; i++) {
        int st;
        while (waitpid(pids[i], &st, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 1;
        }
        if (i == count - 1) {
            status = decode_status(st);
        }
    }
    return status;
}

int execute_pipeline(Pipeline *pipeline) {
    if (pipeline->ncmds == 1 && builtin_is(pipeline->cmds[0].argv[0])) {
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
            /* pipe is a kernel buffer shared by two processes. */
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
    int started = 0;

    for (int i = 0; i < ncmds; i++) {
        /* fork copies the shell. the child becomes one pipeline stage. */
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "msh: fork: %s\n", strerror(errno));
            for (int j = 0; j < started; j++) {
                waitpid(pids[j], NULL, 0);
            }
            close_pair_list(pipefds, pipe_count);
            free(pipefds);
            return 1;
        }
        if (pid == 0) {
            run_child(pipeline, i, pipefds);
            _exit(1);
        }
        pids[started++] = pid;
    }

    close_pair_list(pipefds, pipe_count);
    free(pipefds);

    /* waitpid reaps every child. the last stage sets the status. */
    return wait_pipeline(pids, started);
}
