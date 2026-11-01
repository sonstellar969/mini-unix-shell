#ifndef MSH_PARSER_H
#define MSH_PARSER_H

#include "shell.h"

/* Parse one list segment starting at *pos.
 * Returns 0 and advances *pos past the segment operator.
 * *op is SH_OP_END, SH_OP_SEMI, SH_OP_AND, SH_OP_OR, or SH_OP_BG.
 * *cut is the index where the command text ends (the operator, or the end).
 * Returns 1 when only whitespace or a comment remains.
 * Returns -1 on a syntax error. The caller frees the pipeline only on 0. */
int parse_segment(const char *line, size_t *pos, Pipeline *out, int *op, size_t *cut);
void pipeline_free(Pipeline *pipeline);

#endif
