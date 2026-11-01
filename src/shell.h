#ifndef MSH_SHELL_H
#define MSH_SHELL_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define SH_MAX_ARGS 128
#define SH_MAX_CMDS 32

typedef struct {
    char *argv[SH_MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
    int append;
} Command;

typedef struct {
    Command cmds[SH_MAX_CMDS];
    int ncmds;
} Pipeline;

extern int sh_last_status;
extern int sh_interactive;
extern int sh_in_child;
extern int sh_exit_requested;
extern int sh_exit_code;

static inline char *xstrdup(const char *s) {
    char *copy = strdup(s);
    if (!copy) {
        fprintf(stderr, "msh: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return copy;
}

#endif
