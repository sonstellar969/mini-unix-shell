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

static char *segment_text(const char *line, size_t begin, size_t end) {
    while (begin < end && isspace((unsigned char)line[begin])) {
        begin++;
    }
    while (end > begin && isspace((unsigned char)line[end - 1])) {
        end--;
    }
    size_t n = end - begin;
    char *copy = malloc(n + 1);
    if (!copy) {
        fprintf(stderr, "msh: out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, line + begin, n);
    copy[n] = '\0';
    return copy;
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
        size_t pos = 0;
        int run = 1;
        while (!sh_exit_requested) {
            size_t begin = pos;
            size_t cut = pos;
            Pipeline pipeline;
            int op = SH_OP_END;
            int rc = parse_segment(line, &pos, &pipeline, &op, &cut);
            if (rc < 0) {
                sh_last_status = 2;
                break;
            }
            if (rc > 0) {
                break;
            }
            if (run) {
                char *text = segment_text(line, begin, cut);
                sh_last_status = execute_pipeline(&pipeline, text);
                free(text);
            }
            pipeline_free(&pipeline);
            if (sh_exit_requested || op == SH_OP_END) {
                break;
            }
            if (op == SH_OP_AND) {
                run = sh_last_status == 0;
            } else if (op == SH_OP_OR) {
                run = sh_last_status != 0;
            } else {
                run = 1;
            }
        }
    }

    free(line);
    return sh_exit_requested ? sh_exit_code : sh_last_status;
}
