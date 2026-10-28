#include "common.h"

int main(void)
{
    int ready[2];
    if (pipe(ready) < 0) fail("pipe");
    pid_t pid = spawn();
    if (pid == 0) {
        close(ready[0]);
        if (puts("hello") == EOF || fflush(stdout) == EOF) fail("stdout");
        /* Signal only after hello has actually left the stdio buffer. */
        write_all(ready[1], "!", 1);
        close(ready[1]);
        return 0;
    }
    close(ready[1]);
    char token;
    ssize_t n;
    do {
        n = read(ready[0], &token, 1);
    } while (n < 0 && errno == EINTR);
    if (n < 0) fail("read");
    if (n != 1) {
        fprintf(stderr, "Child did not signal readiness\n");
        return 1;
    }
    close(ready[0]);
    puts("goodbye");
    /* No wait() or waitpid(): this short-lived parent exits immediately. */
    return 0;
}
