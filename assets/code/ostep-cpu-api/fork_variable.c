#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int x = 100;
    printf("Before fork: x = %d\n", x);
    /* Flush buffered output so fork does not duplicate it. */
    fflush(stdout);

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        printf("Child: inherited x = %d\n", x);
        x = 200;
        printf("Child: after changing x, x = %d\n", x);
        return EXIT_SUCCESS;
    }

    /* Wait for the child to finish to make the output order predictable. */
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Child did not exit successfully\n");
        return EXIT_FAILURE;
    }

    printf("Parent: after the child changed x, x = %d\n", x);
    x = 300;
    printf("Parent: after changing x, x = %d\n", x);
    return EXIT_SUCCESS;
}
