# msh

`msh` is a Mini UNIX Shell written in C. It is a command-line interpreter: a typed command runs as a new process created with `fork` and `execvp`. Built-in commands that must change the shell itself (`cd`, `exit`) run in the shell process. Pipelines, redirection, background jobs, and Ctrl+C handling are built on the same process and file-descriptor calls. The shell never calls `system()` and never hands the line to another shell.

The demonstration environment named for this topic is Linux. The operating-systems laboratory machines run Fedora. The sources are POSIX C11, so `make`, `make test`, and `make bench` are the same commands on Fedora and on the machine used to develop the code.


## Features

- External commands, resolved with `execvp` and the current `PATH`
- Builtins: `cd`, `pwd`, `exit`, `help`, `history`, `jobs`, `wait`
- Pipelines of more than one command: `cmd1 | cmd2 | cmd3`
- Lists: `cmd1 ; cmd2`, `cmd1 && cmd2`, `cmd1 || cmd2`
- Redirection: `< file`, `> file`, `>> file`, `2> file`, `2>> file`
- Background commands with `&`, including a command after the `&`, reaped so they do not stay zombies
- `$?` for the previous status, and `~` for the home directory
- Single and double quotes
- A foreground process group, so Ctrl+C stops the running command and leaves the shell in place

## Build and run

Requirements are a C11 compiler (`gcc` or `clang`), Make, and Python 3 for the tests. On Fedora, `gcc`, `make`, and `python3` are enough. There are no third-party libraries.

```sh
make
./msh
make test
make bench
make pack
```

`make pack` writes `OS_MiniProject_MiniUNIXShell.zip` containing the sources, tests, samples, README, and report, and omitting `.git` and build products. Rename that archive to `OS_MiniProject_TeamXX_MiniUNIXShell.zip`, and rename the PDF to `OS_MiniProject_TeamXX_Report.pdf`, once the team number is known.

`make test` runs 40 functional tests and one terminal test that sends Ctrl+C. `make bench` reprints the timing measurements.

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
src/parser.c     one-pass parser for words, pipes, lists, redirection
src/execute.c    fork, pipes, redirection, process groups
src/builtin.c    cd, pwd, exit, help, history
src/jobs.c       background jobs and wait
tests/           functional tests, Ctrl+C test, benchmarks
```

## Limits

`msh` is a demonstration of process creation, file descriptors, and pipes. It does not implement globbing, command substitution, shell variables beyond `$?`, `~user`, combining descriptors with `2>&1`, or Ctrl+Z job suspension. A builtin used inside a pipeline runs in a child, so `cd /tmp | cat` does not change the shell's directory. That is the same rule a full shell uses.

## Sample cases

Normal, boundary, and invalid inputs are in `samples/session.txt`. The test file `tests/test_shell.py` is the executable version of those cases.

## Original work

The C sources, tests, and this README were written for this shell. System calls come from the C library and follow POSIX. Algorithm references are listed in `REPORT.md`.

## Our Team

- Lakshman Reddy (25070721038)
- Chandra Shekhar (25070721055)
- Aditya Praveen (25070721057)
- Shashi Kiran Reddy (25070721045)

## License

The source is released under the MIT license. See `LICENSE`.
