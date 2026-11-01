#ifndef MSH_BUILTIN_H
#define MSH_BUILTIN_H

#include "shell.h"

int builtin_is(const char *name);
int builtin_run(Command *cmd);

#endif
