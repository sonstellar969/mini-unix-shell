CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

SRC := src/main.c src/parser.c src/execute.c src/builtin.c src/jobs.c
OBJ := $(SRC:.c=.o)

.PHONY: all test bench clean

all: msh

msh: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/shell.h src/parser.h src/builtin.h src/execute.h src/jobs.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: msh
	python3 tests/test_shell.py
	python3 tests/test_signal.py

bench: msh tests/measure_pipe
	python3 tests/benchmark.py

tests/measure_pipe: tests/measure_pipe.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(OBJ) msh tests/measure_pipe
