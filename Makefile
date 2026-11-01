CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

SRC := src/main.c src/parser.c src/execute.c src/builtin.c
OBJ := $(SRC:.c=.o)

.PHONY: all test bench pack clean

all: msh

msh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/shell.h src/parser.h src/builtin.h src/execute.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: msh
	python3 tests/test_shell.py

bench: msh tests/measure_pipe
	python3 tests/benchmark.py

tests/measure_pipe: tests/measure_pipe.c
	$(CC) $(CFLAGS) -o $@ $<

pack:
	rm -f OS_MiniProject_MiniUNIXShell.zip
	zip -r OS_MiniProject_MiniUNIXShell.zip \
		LICENSE Makefile README.md REPORT.md SUBMISSION.md samples src tests report \
		-x 'src/*.o' 'msh' 'tests/measure_pipe' '*/.DS_Store'

clean:
	rm -f $(OBJ) msh tests/measure_pipe OS_MiniProject_MiniUNIXShell.zip
