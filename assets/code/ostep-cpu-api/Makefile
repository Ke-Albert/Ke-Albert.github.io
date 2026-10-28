CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2
PROGRAMS = q2_file q3_order q4_exec q5_wait q6_waitpid q7_stdout q8_pipe

.PHONY: all run clean
all: $(PROGRAMS)

$(PROGRAMS): %: %.c common.h
	$(CC) $(CFLAGS) $< -o $@

run: all
	@set -e; for program in $(PROGRAMS); do echo "=== $$program ==="; ./$$program; done

clean:
	rm -f $(PROGRAMS) q2_output.txt
