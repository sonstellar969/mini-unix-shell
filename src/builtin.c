#include "builtin.h"

#include "jobs.h"

#include <errno.h>
#include <limits.h>
#include <unistd.h>

#define HIST_MAX 100

static char *hist[HIST_MAX];
static int hist_len;

void history_add(const char *line) {
    if (hist_len == HIST_MAX) {
        free(hist[0]);
        memmove(&hist[0], &hist[1], (HIST_MAX - 1) * sizeof hist[0]);
        hist_len--;
    }
    hist[hist_len++] = xstrdup(line);
}

static int builtin_history(void) {
    for (int i = 0; i < hist_len; i++) {
        printf("%4d  %s\n", i + 1, hist[i]);
    }
    return 0;
}

static int builtin_help(void) {
    fputs(
        "msh, a minimal UNIX shell\n"
        "\n"
        "Builtins: cd, pwd, exit, help, history, jobs, wait\n"
        "External commands run with fork and execvp.\n"
        "Pipelines: cmd1 | cmd2 | cmd3\n"
        "Redirection: < file, > file, >> file\n"
        "Background: command &\n"
        "Status of the previous command: $?\n"
        "Home shortcut: ~ and ~/\n"
        "Ctrl+C stops the foreground command and returns to msh.\n",
        stdout);
    return 0;
}

static int builtin_pwd(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) {
        fprintf(stderr, "msh: pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

static int builtin_cd(Command *cmd) {
    if (cmd->argc > 2) {
        fprintf(stderr, "msh: cd: too many arguments\n");
        return 1;
    }

    char previous[PATH_MAX];
    if (!getcwd(previous, sizeof previous)) {
        previous[0] = '\0';
    }

    const char *dest;
    int print_dest = 0;
    if (cmd->argc == 1) {
        dest = getenv("HOME");
        if (!dest || !dest[0]) {
            fprintf(stderr, "msh: cd: HOME not set\n");
            return 1;
        }
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        dest = getenv("OLDPWD");
        if (!dest || !dest[0]) {
            fprintf(stderr, "msh: cd: OLDPWD not set\n");
            return 1;
        }
        print_dest = 1;
    } else {
        dest = cmd->argv[1];
    }

    if (chdir(dest) < 0) {
        fprintf(stderr, "msh: cd: %s: %s\n", dest, strerror(errno));
        return 1;
    }
    if (previous[0]) {
        setenv("OLDPWD", previous, 1);
    }
    char now[PATH_MAX];
    if (getcwd(now, sizeof now)) {
        setenv("PWD", now, 1);
        if (print_dest) {
            printf("%s\n", now);
        }
    }
    return 0;
}

static int builtin_exit(Command *cmd) {
    int code = sh_last_status;
    if (cmd->argc > 2) {
        fprintf(stderr, "msh: exit: too many arguments\n");
        return 1;
    }
    if (cmd->argc == 2) {
        char *end = NULL;
        errno = 0;
        long value = strtol(cmd->argv[1], &end, 10);
        if (end == cmd->argv[1] || *end != '\0' || errno == ERANGE || value < 0 || value > 255) {
            fprintf(stderr, "msh: exit: numeric status in 0..255 required\n");
            return 1;
        }
        code = (int)value;
    }
    if (sh_in_child) {
        _exit(code);
    }
    sh_exit_requested = 1;
    sh_exit_code = code;
    return code;
}

int builtin_is(const char *name) {
    if (!name) {
        return 0;
    }
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 || strcmp(name, "exit") == 0 ||
           strcmp(name, "help") == 0 || strcmp(name, "history") == 0 || strcmp(name, "jobs") == 0 ||
           strcmp(name, "wait") == 0;
}

int builtin_run(Command *cmd) {
    const char *name = cmd->argv[0];
    int status;
    if (strcmp(name, "cd") == 0) {
        status = builtin_cd(cmd);
    } else if (strcmp(name, "pwd") == 0) {
        status = builtin_pwd();
    } else if (strcmp(name, "exit") == 0) {
        status = builtin_exit(cmd);
    } else if (strcmp(name, "help") == 0) {
        status = builtin_help();
    } else if (strcmp(name, "history") == 0) {
        status = builtin_history();
    } else if (strcmp(name, "jobs") == 0) {
        jobs_list();
        status = 0;
    } else if (strcmp(name, "wait") == 0) {
        status = jobs_wait_all();
    } else {
        fprintf(stderr, "msh: %s: not a builtin\n", name);
        status = 1;
    }
    /* _exit() does not flush, and a redirected builtin swaps file descriptors
     * under stdio. Push the buffer out before either of those happens. */
    fflush(stdout);
    fflush(stderr);
    return status;
}
