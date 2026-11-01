# Topic submission

Copy the fields below into the topic form. Fill the team member list on the form itself. This file has no roll numbers or course codes.

## Title

msh: A Minimal UNIX Shell

Official topic 21: a command-line interpreter that supports commands and process creation with fork() and exec() on Linux, in C.

## Problem statement

The shell is the program that turns a typed line into processes, pipes, and files. Reading about `fork`, `exec`, pipes, and signals leaves those steps abstract. This project builds `msh`, a small POSIX shell that does the steps itself. It must run external programs, keep builtins that change the shell (`cd`, `exit`) inside the shell process, connect commands with pipes, redirect input and output, start background commands, and stay alive when Ctrl+C stops the foreground command. A slide deck is not the deliverable. The deliverable is a working command-line program, with tests that include invalid input, and with a measurement of what process creation costs.

## Objectives

1. Parse one command line into a pipeline of commands, including quotes, `<`, `>`, `>>`, and a trailing `&`.
2. Create external processes with `fork` and `execvp`, and collect them with `waitpid`.
3. Run `cd`, `pwd`, `exit`, `help`, `history`, `jobs`, and `wait` in the shell process when they are a simple command.
4. Join pipeline stages with anonymous pipes and apply file redirection with `dup2`.
5. Put each pipeline in its own process group and hand that group the terminal, so Ctrl+C interrupts the command and not the shell.
6. Track background jobs and reap them so they do not remain zombies.
7. Reject a missing command, a bad redirection, an unknown program, and a bad `exit` code without killing the shell.
8. Measure a built-in command against an external process and against a two-process pipeline.

## Planned tools and language

- Language: C11
- Interface: command line
- System calls: `fork`, `execvp`, `pipe`, `dup2`, `waitpid`, `setpgid`, `tcsetpgrp`, `sigaction`, `chdir`, `getcwd`
- Build: Make and `cc` (clang or gcc)
- Tests: Python 3
- Libraries: the C library only. No `system()`, no embedded shell, no third-party packages.

## Expected outcome

A program named `msh` that runs external commands, builtins, multi-stage pipes, redirection, and background jobs from a terminal. Ctrl+C stops the foreground command and returns to the `msh` prompt. The repository holds modular source, an automated test suite, sample input and output (including invalid lines), and timings for a builtin, an external command, and a pipeline. A written report describes the design, the tests, and those measurements.
