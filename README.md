# msh

`msh` is a small UNIX shell in POSIX C. It reads a line from standard input and runs it. Built-in commands that must change the shell itself run inside the shell process. Every other command is a child created with `fork` and `execvp`. Pipelines are children joined by anonymous pipes. The shell never calls `system()` and never hands the line to another shell.


## Features

- External commands, resolved with `execvp` and the current `PATH`
- Builtins: `cd`, `pwd`, `exit`, `help`, `history`, `jobs`, `wait`
- Pipelines of more than one command: `cmd1 | cmd2 | cmd3`
- Redirection: `< file`, `> file`, `>> file`
- Background commands with `&`, reaped so they do not stay zombies
- `$?` for the previous status, and `~` for the home directory
- Single and double quotes
- A foreground process group, so Ctrl+C stops the running command and leaves the shell in place

## Build and run

Requirements are a C11 compiler (`cc`, clang, or gcc), Make, and Python 3 for the tests. There are no third-party libraries.

```sh
make
./msh
make test
make bench
```

`make test` runs 36 functional tests and one terminal test that sends Ctrl+C. `make bench` reprints the timing measurements.

A non-interactive script is a normal input stream:

```sh
printf 'echo hello\npwd\n' | ./msh
```

The prompt is printed on standard error, so command output and the prompt stay apart.

## Example

```text
msh:~$ echo hello | cat
hello
msh:~$ echo one > /tmp/note.txt
msh:~$ echo two >> /tmp/note.txt
msh:~$ cat < /tmp/note.txt
one
two
msh:~$ printf 'b\na\n' | sort
a
b
msh:~$ sleep 30
^C
msh:~$
```

`sleep 30 &` prints a job id and a process-group id on standard error. `jobs` lists it. `wait` collects it.

## Layout

```text
src/main.c       read-eval loop, prompt, signals
src/parser.c     one-pass parser for words, pipes, redirection
src/execute.c    fork, pipes, redirection, process groups
src/builtin.c    cd, pwd, exit, help, history
src/jobs.c       background jobs and wait
tests/           functional tests, Ctrl+C test, benchmarks
```

## Limits

`msh` is a demonstration of process creation, file descriptors, and pipes. It does not implement `&&`, `||`, `;`, globbing, command substitution, shell variables beyond `$?`, `~user`, a separate `2>` redirection, or Ctrl+Z job suspension. A builtin used inside a pipeline runs in a child, so `cd /tmp | cat` does not change the shell's directory. That is the same rule a full shell uses.

## Sample cases

Normal, boundary, and invalid inputs are in `samples/session.txt`. The test file `tests/test_shell.py` is the executable version of those cases.

## Original work

The C sources, tests, and this README were written for this shell. System calls come from the C library and follow POSIX. Algorithm references are listed in `REPORT.md`.
