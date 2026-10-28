#include <string.h>
#include "common.h"

int main(void)
{
    pid_t pid = spawn();
    if (pid == 0) {
        int status;
        errno = 0;
        pid_t result = wait(&status);
        int saved_errno = errno;
        printf("Child: wait() = %ld, errno = %d (%s)\n",
               (long)result, saved_errno, strerror(saved_errno));
        if (result != -1 || saved_errno != ECHILD) return 1;
        return 42;
    }
    int status;
    pid_t result;
    do {
        result = wait(&status);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("wait");
    printf("Parent: child PID = %ld, wait() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
