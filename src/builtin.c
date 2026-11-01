#include "builtin.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

static int builtin_help(void) {
    fputs(
        "msh, a small unix shell\n"
        "\n"
        "a shell reads a line and asks the operating system to run it.\n"
        "builtins: cd, pwd, exit, help\n"
        "external commands run with fork and execvp.\n"
        "pipelines: cmd1 | cmd2 | cmd3\n"
        "redirection: < file, > file, >> file\n"
        "$? is the exit status from the previous command.\n",
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

    /* chdir changes this process. a child would lose the change on exit. */
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
           strcmp(name, "help") == 0;
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
    } else {
        fprintf(stderr, "msh: %s: not a builtin\n", name);
        status = 1;
    }
    /* flush before _exit, and before a redirected builtin restores its fds. */
    fflush(stdout);
    fflush(stderr);
    return status;
}
