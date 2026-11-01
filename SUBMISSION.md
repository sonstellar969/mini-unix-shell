# Topic submission

Copy the fields below into the topic form. Fill the team member list on the form itself. This file has no roll numbers.

## Title

msh: A Minimal UNIX Shell

Official topic 21: a command-line interpreter that supports commands and process creation with fork() and exec() on Linux, in C.

## Problem statement

A shell is the program that reads a typed command and asks the operating system to run it. Reading about fork, exec, and pipes leaves those steps abstract. This project builds msh, a small POSIX shell that does the steps itself. It runs external programs as new processes, keeps cd and exit in the shell process, connects commands with pipes, and redirects input and output to files. The deliverable is a working command-line program, with tests that include invalid input, and with a measurement of what process creation costs.

## Objectives

1. Parse one command line into words, quotes, a pipeline, and `<`, `>`, `>>`.
2. Create external processes with `fork` and `execvp`, and collect them with `waitpid`.
3. Run `cd`, `pwd`, `exit`, and `help` in the shell process when they are a simple command.
4. Join pipeline stages with anonymous pipes and apply file redirection with `dup2`.
5. Reject a missing command, a bad redirection, and an unknown program without killing the shell.
6. Measure a built-in command against an external process and against a two-process pipeline.

## Planned tools and language

- Language: C11
- Interface: command line
- System calls: `fork`, `execvp`, `pipe`, `dup2`, `waitpid`, `open`, `chdir`, `getcwd`
- Build: Make and `cc` (clang or gcc)
- Tests: Python 3
- Libraries: the C library only. No `system()`, no embedded shell, no third-party packages.

## Expected outcome

A program named `msh` that runs external commands, builtins, pipes, and redirection from a terminal. The repository holds modular source, an automated test suite, sample input and output (including invalid lines), and timings for a builtin, an external command, and a pipeline. A written report describes the design, the tests, and those measurements.
