# msh

`msh` is a small UNIX shell written in C. A shell reads a line you type and asks the operating system to run it. An external command becomes a new process with `fork` and `execvp`. The shell waits for that process with `waitpid`. `cd` and `exit` run in the shell process, because a child cannot change the shell's directory or end the shell. Pipes and redirection use `pipe`, `open`, and `dup2`. The shell never calls `system()` and never hands the line to another shell. 

The demonstration environment for this topic is Linux. The sources are POSIX C11, so `make` and `make test` are the same commands on Linux and on the machine used to develop the code.

## Features

- External commands, found with `execvp` and `PATH`
- Builtins: `cd`, `pwd`, `exit`, `help`
- Pipelines: `cmd1 | cmd2 | cmd3`
- Redirection: `< file`, `> file`, `>> file`
- `$?` for the exit status of the previous command
- Single and double quotes

## Build and run

Requirements are a C11 compiler (`gcc` or `clang`), Make, and Python 3 for the tests. There are no third-party libraries.

```sh
make
./msh
make test
make bench
make pack
```

`make pack` writes `OS_MiniProject_MiniUNIXShell.zip`. Rename that archive and the PDF with the team number before submission.

`make test` runs 33 functional tests. `make bench` reprints the timing measurements.

```sh
printf 'echo hello\npwd\n' | ./msh
```

The prompt is printed on standard error.

## Example

```text
msh:/home/user$ echo hello | cat
hello
msh:/home/user$ echo one > /tmp/note.txt
msh:/home/user$ echo two >> /tmp/note.txt
msh:/home/user$ cat < /tmp/note.txt
one
two
msh:/home/user$ printf 'b\na\n' | sort
a
b
msh:/home/user$ echo $?
0
```

## Layout

```text
src/main.c       read a line and run it
src/parser.c     words, quotes, pipes, redirection
src/execute.c    fork, execvp, waitpid, pipe, dup2
src/builtin.c    cd, pwd, exit, help
tests/           functional tests and benchmarks
```

## Limits

This shell demonstrates process creation, pipes, and files. It does not implement background jobs, Ctrl+Z, `&&`, `||`, `;`, `2>`, globbing, command substitution, or variables other than `$?`. A builtin inside a pipeline runs in a child, so `cd /tmp | cat` does not change the shell's directory.

## Sample cases

Normal, boundary, and invalid inputs are in `samples/session.txt`. `tests/test_shell.py` is the executable version of those cases.


## Original work

The C sources, tests, and this README were written for this shell. System calls come from the C library and follow POSIX.

## Our Team

- Lakshman Reddy (25070721038)
- Chandra Shekhar (25070721055)
- Aditya Praveen (25070721057)
- Shashi Kiran Reddy (25070721045)

## License

The source is released under the MIT license. See `LICENSE`.
