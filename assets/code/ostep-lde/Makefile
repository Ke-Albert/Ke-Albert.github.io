CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2

.PHONY: all run test report clean
all: lde_bench

lde_bench: lde_bench.c
	$(CC) $(CFLAGS) $< -o $@

run: all
	./lde_bench

test: all
	python3 test_bench.py

report: all
	python3 run_experiment.py

clean:
	rm -f lde_bench
