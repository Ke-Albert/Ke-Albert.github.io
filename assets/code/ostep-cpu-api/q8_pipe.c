#include "common.h"

int main(void)
{
    int channel[2];
    if (pipe(channel) < 0) fail("pipe");
    pid_t producer = spawn();
    if (producer == 0) {
        close(channel[0]);
        if (dup2(channel[1], STDOUT_FILENO) < 0) fail("dup2");
        if (channel[1] != STDOUT_FILENO) close(channel[1]);
        printf("hello from child 1\nthis is a pipe\n");
        if (fflush(stdout) == EOF) fail("fflush");
        return 0;
    }
    pid_t consumer = spawn();
    if (consumer == 0) {
        close(channel[1]);
        if (dup2(channel[0], STDIN_FILENO) < 0) fail("dup2");
        if (channel[0] != STDIN_FILENO) close(channel[0]);
        char buffer[256];
        for (;;) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) fail("read");
            if (n == 0) break;
            write_all(STDOUT_FILENO, buffer, (size_t)n);
        }
        return 0;
    }
    /* Without closing the parent's write end, the consumer cannot see EOF. */
    close(channel[0]);
    close(channel[1]);
    reap(producer);
    reap(consumer);
    puts("Parent: both children finished");
    return 0;
}
