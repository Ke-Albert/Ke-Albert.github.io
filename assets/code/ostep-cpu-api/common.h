#ifndef HOMEWORK_COMMON_H
#define HOMEWORK_COMMON_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static inline void fail(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

static inline pid_t spawn(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) fail("fork");
    return pid;
}

static inline void write_all(int fd, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) fail("write");
        data += n;
        size -= (size_t)n;
    }
}

static inline void reap(pid_t pid)
{
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) fail("waitpid");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Child failed\n");
        exit(EXIT_FAILURE);
    }
}
#endif
