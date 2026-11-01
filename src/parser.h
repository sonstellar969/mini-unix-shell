#ifndef MSH_PARSER_H
#define MSH_PARSER_H

#include "shell.h"

int parse_line(const char *line, Pipeline *out);
void pipeline_free(Pipeline *pipeline);

#endif
