#include <string.h>
#include "common.h"

int main(void)
{
    /* Unbuffered stdout exposes the error directly in printf(). */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return 1;
    pid_t pid = spawn();
    if (pid == 0) {
        if (close(STDOUT_FILENO) < 0) fail("close");
        errno = 0;
        int result = printf("Child: this text cannot reach stdout\n");
        int saved_errno = errno;
        fprintf(stderr, "Child: printf() = %d, ferror(stdout) = %d, errno = %d (%s)\n",
                result, ferror(stdout), saved_errno, strerror(saved_errno));
        return result < 0 && saved_errno == EBADF ? 0 : 1;
    }
    reap(pid);
    puts("Parent: stdout still works");
    return 0;
}
