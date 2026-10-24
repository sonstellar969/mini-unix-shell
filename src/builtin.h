#ifndef MSH_BUILTIN_H
#define MSH_BUILTIN_H

#include "shell.h"

void history_add(const char *line);
int builtin_is(const char *name);
int builtin_run(Command *cmd);

#endif
