#include "parser.h"

#include <ctype.h>

static void skip_ws(const char *s, size_t *i) {
    while (s[*i] && isspace((unsigned char)s[*i])) {
        (*i)++;
    }
}

static void command_clear(Command *cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
        cmd->argv[i] = NULL;
    }
    free(cmd->infile);
    free(cmd->outfile);
    free(cmd->errfile);
    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->errfile = NULL;
    cmd->argc = 0;
    cmd->append = 0;
    cmd->err_append = 0;
}

void pipeline_free(Pipeline *pipeline) {
    if (!pipeline) {
        return;
    }
    for (int i = 0; i < pipeline->ncmds; i++) {
        command_clear(&pipeline->cmds[i]);
    }
    pipeline->ncmds = 0;
    pipeline->background = 0;
}

static int add_arg(Command *cmd, char *word) {
    if (cmd->argc >= SH_MAX_ARGS - 1) {
        free(word);
        fprintf(stderr, "msh: too many arguments\n");
        return -1;
    }
    cmd->argv[cmd->argc++] = word;
    cmd->argv[cmd->argc] = NULL;
    return 0;
}

static char *expand_word(char *word, int quoted) {
    if (quoted) {
        return word;
    }
    if (strcmp(word, "$?") == 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", sh_last_status);
        free(word);
        return xstrdup(buf);
    }
    if (word[0] == '~' && (word[1] == '\0' || word[1] == '/')) {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            return word;
        }
        size_t home_len = strlen(home);
        size_t word_len = strlen(word);
        char *expanded = malloc(home_len + word_len);
        if (!expanded) {
            fprintf(stderr, "msh: out of memory\n");
            exit(EXIT_FAILURE);
        }
        memcpy(expanded, home, home_len);
        memcpy(expanded + home_len, word + 1, word_len);
        free(word);
        return expanded;
    }
    return word;
}

/* Read one shell word. Returns 1 if a word was stored, 0 if the next
 * character is not a word, and -1 on a syntax or memory error. */
static int read_word(const char *s, size_t *i, char **out) {
    size_t n = strlen(s);
    if (*i >= n) {
        return 0;
    }
    unsigned char c = (unsigned char)s[*i];
    if (isspace(c) || s[*i] == '|' || s[*i] == '<' || s[*i] == '>' || s[*i] == '&' ||
        s[*i] == ';' || s[*i] == '#') {
        return 0;
    }

    char *buf = malloc(n + 1);
    if (!buf) {
        fprintf(stderr, "msh: out of memory\n");
        exit(EXIT_FAILURE);
    }
    size_t k = 0;
    int quote = 0;
    int any_quote = 0;

    while (*i < n) {
        char ch = s[*i];
        if (!quote && (isspace((unsigned char)ch) || ch == '|' || ch == '<' || ch == '>' ||
                       ch == '&' || ch == ';')) {
            break;
        }
        if (!quote && ch == '#') {
            break;
        }
        if (ch == '\'' || ch == '"') {
            if (!quote) {
                quote = ch;
                any_quote = 1;
                (*i)++;
                continue;
            }
            if (quote == ch) {
                quote = 0;
                (*i)++;
                continue;
            }
        }
        if (quote == '"' && ch == '\\' && *i + 1 < n) {
            char next = s[*i + 1];
            if (next == '"' || next == '\\' || next == '$') {
                buf[k++] = next;
                *i += 2;
                continue;
            }
        }
        buf[k++] = ch;
        (*i)++;
    }

    if (quote) {
        free(buf);
        fprintf(stderr, "msh: syntax error: unclosed quote\n");
        return -1;
    }
    buf[k] = '\0';
    *out = expand_word(buf, any_quote);
    return 1;
}

static int finish_command(Pipeline *pipeline, int idx);

static int command_is_empty(const Command *cmd) {
    return cmd->argc == 0 && !cmd->infile && !cmd->outfile && !cmd->errfile;
}

/* Finish the current segment. *i already points past the operator.
 * need_more rejects a trailing && or ||. */
static int stop_segment(const char *line, size_t *i, Pipeline *out, int idx, int op, int *op_out,
                        int need_more) {
    if (command_is_empty(&out->cmds[idx]) || finish_command(out, idx) != 0) {
        if (command_is_empty(&out->cmds[idx])) {
            fprintf(stderr, "msh: syntax error: missing command\n");
        }
        return -1;
    }
    if (op == SH_OP_BG) {
        out->background = 1;
    }
    if (need_more) {
        size_t j = *i;
        skip_ws(line, &j);
        if (line[j] == '\0' || line[j] == '#') {
            fprintf(stderr, "msh: syntax error: incomplete list\n");
            return -1;
        }
    }
    *op_out = op;
    return 0;
}

static int take_redirect(Command *cmd, int input, int append, int error, char *word) {
    if (input) {
        if (cmd->infile) {
            free(word);
            fprintf(stderr, "msh: syntax error: duplicate input redirection\n");
            return -1;
        }
        cmd->infile = word;
        return 0;
    }
    if (error) {
        if (cmd->errfile) {
            free(word);
            fprintf(stderr, "msh: syntax error: duplicate error redirection\n");
            return -1;
        }
        cmd->errfile = word;
        cmd->err_append = append;
        return 0;
    }
    if (cmd->outfile) {
        free(word);
        fprintf(stderr, "msh: syntax error: duplicate output redirection\n");
        return -1;
    }
    cmd->outfile = word;
    cmd->append = append;
    return 0;
}

static int finish_command(Pipeline *pipeline, int idx) {
    Command *cmd = &pipeline->cmds[idx];
    if (cmd->argc == 0) {
        fprintf(stderr, "msh: syntax error: missing command\n");
        return -1;
    }
    cmd->argv[cmd->argc] = NULL;
    return 0;
}

static int read_target(const char *line, size_t *i, Pipeline *out, char **word) {
    skip_ws(line, i);
    int rc = read_word(line, i, word);
    if (rc < 0) {
        pipeline_free(out);
        return -1;
    }
    if (rc == 0) {
        fprintf(stderr, "msh: syntax error: missing redirection target\n");
        pipeline_free(out);
        return -1;
    }
    return 0;
}

int parse_segment(const char *line, size_t *pos, Pipeline *out, int *op, size_t *cut) {
    memset(out, 0, sizeof *out);
    out->ncmds = 1;
    *op = SH_OP_END;

    size_t i = *pos;
    size_t n = strlen(line);
    int idx = 0;
    int saw = 0;

    while (i < n) {
        skip_ws(line, &i);
        if (i >= n || line[i] == '#') {
            break;
        }
        saw = 1;

        if (line[i] == '|' && line[i + 1] == '|') {
            size_t at = i;
            i += 2;
            if (stop_segment(line, &i, out, idx, SH_OP_OR, op, 1) != 0) {
                pipeline_free(out);
                return -1;
            }
            *cut = at;
            *pos = i;
            return 0;
        }

        if (line[i] == '|') {
            if (command_is_empty(&out->cmds[idx]) || finish_command(out, idx) != 0) {
                if (command_is_empty(&out->cmds[idx])) {
                    fprintf(stderr, "msh: syntax error: empty command near '|'\n");
                }
                pipeline_free(out);
                return -1;
            }
            if (out->ncmds >= SH_MAX_CMDS) {
                fprintf(stderr, "msh: too many commands in a pipeline\n");
                pipeline_free(out);
                return -1;
            }
            idx++;
            out->ncmds++;
            i++;
            continue;
        }

        if (line[i] == '&' && line[i + 1] == '&') {
            size_t at = i;
            i += 2;
            if (stop_segment(line, &i, out, idx, SH_OP_AND, op, 1) != 0) {
                pipeline_free(out);
                return -1;
            }
            *cut = at;
            *pos = i;
            return 0;
        }

        if (line[i] == '&') {
            size_t at = i;
            i++;
            if (stop_segment(line, &i, out, idx, SH_OP_BG, op, 0) != 0) {
                pipeline_free(out);
                return -1;
            }
            *cut = at;
            *pos = i;
            return 0;
        }

        if (line[i] == ';') {
            size_t at = i;
            i++;
            if (stop_segment(line, &i, out, idx, SH_OP_SEMI, op, 0) != 0) {
                pipeline_free(out);
                return -1;
            }
            *cut = at;
            *pos = i;
            return 0;
        }

        if (line[i] == '2' && line[i + 1] == '>') {
            int append = 0;
            i += 2;
            if (line[i] == '>') {
                append = 1;
                i++;
            }
            if (line[i] == '&') {
                fprintf(stderr, "msh: syntax error: unsupported redirection\n");
                pipeline_free(out);
                return -1;
            }
            char *word = NULL;
            if (read_target(line, &i, out, &word) != 0) {
                return -1;
            }
            if (take_redirect(&out->cmds[idx], 0, append, 1, word) != 0) {
                pipeline_free(out);
                return -1;
            }
            continue;
        }

        if (line[i] == '<' || line[i] == '>') {
            int input = line[i] == '<';
            int append = 0;
            i++;
            if (!input && line[i] == '>') {
                append = 1;
                i++;
            }
            if (!input && line[i] == '&') {
                fprintf(stderr, "msh: syntax error: unsupported redirection\n");
                pipeline_free(out);
                return -1;
            }
            char *word = NULL;
            if (read_target(line, &i, out, &word) != 0) {
                return -1;
            }
            if (take_redirect(&out->cmds[idx], input, append, 0, word) != 0) {
                pipeline_free(out);
                return -1;
            }
            continue;
        }

        char *word = NULL;
        int rc = read_word(line, &i, &word);
        if (rc < 0) {
            pipeline_free(out);
            return -1;
        }
        if (rc == 0) {
            fprintf(stderr, "msh: syntax error near '%c'\n", line[i]);
            pipeline_free(out);
            return -1;
        }
        if (add_arg(&out->cmds[idx], word) != 0) {
            pipeline_free(out);
            return -1;
        }
    }

    if (!saw) {
        pipeline_free(out);
        *cut = i;
        *pos = i;
        return 1;
    }
    if (command_is_empty(&out->cmds[idx]) || finish_command(out, idx) != 0) {
        if (command_is_empty(&out->cmds[idx])) {
            fprintf(stderr, "msh: syntax error: missing command\n");
        }
        pipeline_free(out);
        return -1;
    }
    *cut = i;
    *pos = i;
    return 0;
}
