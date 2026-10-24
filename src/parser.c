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
    cmd->infile = NULL;
    cmd->outfile = NULL;
    cmd->argc = 0;
    cmd->append = 0;
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
    if (isspace(c) || s[*i] == '|' || s[*i] == '<' || s[*i] == '>' || s[*i] == '&' || s[*i] == '#') {
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
        if (!quote && (isspace((unsigned char)ch) || ch == '|' || ch == '<' || ch == '>' || ch == '&')) {
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

static int command_is_empty(const Command *cmd) {
    return cmd->argc == 0 && !cmd->infile && !cmd->outfile;
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

int parse_line(const char *line, Pipeline *out) {
    memset(out, 0, sizeof *out);
    out->ncmds = 1;

    size_t i = 0;
    size_t n = strlen(line);
    int idx = 0;

    while (i < n) {
        skip_ws(line, &i);
        if (i >= n) {
            break;
        }
        if (line[i] == '#') {
            break;
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

        if (line[i] == '&') {
            i++;
            skip_ws(line, &i);
            if (i < n && line[i] != '#') {
                fprintf(stderr, "msh: syntax error: '&' must be the last token\n");
                pipeline_free(out);
                return -1;
            }
            out->background = 1;
            break;
        }

        if (line[i] == '<' || line[i] == '>') {
            int input = line[i] == '<';
            int append = 0;
            i++;
            if (!input && line[i] == '>') {
                append = 1;
                i++;
            }
            skip_ws(line, &i);
            char *word = NULL;
            int rc = read_word(line, &i, &word);
            if (rc < 0) {
                pipeline_free(out);
                return -1;
            }
            if (rc == 0) {
                fprintf(stderr, "msh: syntax error: missing redirection target\n");
                pipeline_free(out);
                return -1;
            }
            Command *cmd = &out->cmds[idx];
            if (input) {
                if (cmd->infile) {
                    free(word);
                    fprintf(stderr, "msh: syntax error: duplicate input redirection\n");
                    pipeline_free(out);
                    return -1;
                }
                cmd->infile = word;
            } else {
                if (cmd->outfile) {
                    free(word);
                    fprintf(stderr, "msh: syntax error: duplicate output redirection\n");
                    pipeline_free(out);
                    return -1;
                }
                cmd->outfile = word;
                cmd->append = append;
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

    if (command_is_empty(&out->cmds[idx])) {
        fprintf(stderr, "msh: syntax error: missing command\n");
        pipeline_free(out);
        return -1;
    }
    if (finish_command(out, idx) != 0) {
        pipeline_free(out);
        return -1;
    }
    return 0;
}
