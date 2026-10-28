#include "common.h"

int main(void)
{
    pid_t pid = spawn();
    if (pid == 0) {
        puts("Child: exiting with status 42");
        return 42;
    }
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("waitpid");
    printf("Parent: requested PID = %ld, waitpid() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
