#include "shell.h"

#include "builtin.h"
#include "execute.h"
#include "jobs.h"
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

int sh_last_status = 0;
int sh_interactive = 0;
int sh_owns_tty = 0;
int sh_in_child = 0;
int sh_exit_requested = 0;
int sh_exit_code = 0;
pid_t sh_pgid = 0;
int sh_terminal = STDIN_FILENO;

static void ignore_signal(int signo) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    sigaction(signo, &action, NULL);
}

static void setup_signals(void) {
    ignore_signal(SIGINT);
    ignore_signal(SIGQUIT);
    ignore_signal(SIGTSTP);
    ignore_signal(SIGTTIN);
    ignore_signal(SIGTTOU);
}

static void take_terminal(void) {
    sh_interactive = isatty(STDIN_FILENO);
    sh_terminal = STDIN_FILENO;
    if (!sh_interactive) {
        return;
    }
    sh_pgid = getpid();
    if (setpgid(0, sh_pgid) < 0 && errno != EPERM) {
        perror("msh: setpgid");
    }
    if (tcsetpgrp(sh_terminal, sh_pgid) == 0) {
        sh_owns_tty = 1;
    }
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }
}

static void print_prompt(void) {
    char cwd[4096];
    const char *shown = cwd;
    if (!getcwd(cwd, sizeof cwd)) {
        fputs("msh$ ", stderr);
        return;
    }
    const char *home = getenv("HOME");
    char pretty[4096];
    if (home && home[0] && strncmp(cwd, home, strlen(home)) == 0 &&
        (cwd[strlen(home)] == '\0' || cwd[strlen(home)] == '/')) {
        snprintf(pretty, sizeof pretty, "~%s", cwd + strlen(home));
        shown = pretty;
    }
    fprintf(stderr, "msh:%s$ ", shown);
}

int main(void) {
    take_terminal();
    setup_signals();

    char *line = NULL;
    size_t cap = 0;
    while (!sh_exit_requested) {
        jobs_reap(sh_interactive);
        if (sh_interactive) {
            print_prompt();
        }
        errno = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (sh_interactive) {
                fputc('\n', stderr);
            }
            break;
        }
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        history_add(line);
        Pipeline pipeline;
        if (parse_line(line, &pipeline) != 0) {
            sh_last_status = 2;
            continue;
        }
        sh_last_status = execute_pipeline(&pipeline, line);
        pipeline_free(&pipeline);
    }

    free(line);
    return sh_exit_requested ? sh_exit_code : sh_last_status;
}
