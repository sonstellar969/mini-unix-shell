#include "shell.h"

#include "builtin.h"
#include "execute.h"
#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

int sh_last_status = 0;
int sh_interactive = 0;
int sh_in_child = 0;
int sh_exit_requested = 0;
int sh_exit_code = 0;

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
    if (!getcwd(cwd, sizeof cwd)) {
        fputs("msh$ ", stderr);
        return;
    }
    fprintf(stderr, "msh:%s$ ", cwd);
}

int main(void) {
    sh_interactive = isatty(STDIN_FILENO);

    char *line = NULL;
    size_t cap = 0;
    while (!sh_exit_requested) {
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

        Pipeline pipeline;
        if (parse_line(line, &pipeline) != 0) {
            sh_last_status = 2;
            continue;
        }
        sh_last_status = execute_pipeline(&pipeline);
        pipeline_free(&pipeline);
    }

    free(line);
    return sh_exit_requested ? sh_exit_code : sh_last_status;
}
