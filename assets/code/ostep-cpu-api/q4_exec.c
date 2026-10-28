#define _GNU_SOURCE
#include "common.h"

int main(void)
{
    const char *names[] = {"execl", "execle", "execlp", "execv", "execvp", "execvpe"};
    char *args[] = {"ls", "-1", "/bin/ls", NULL};
    char *env[] = {"PATH=/bin:/usr/bin", "LANG=C", NULL};
    /* The p variants search the caller's PATH, including execvpe(). */
    if (setenv("PATH", "/bin:/usr/bin", 1) < 0) fail("setenv");
    for (int i = 0; i < 6; ++i) {
        printf("=== %s ===\n", names[i]);
        pid_t pid = spawn();
        if (pid == 0) {
            switch (i) {
            case 0: execl("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
            case 1: execle("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL, env); break;
            case 2: execlp("ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
            case 3: execv("/bin/ls", args); break;
            case 4: execvp("ls", args); break;
            case 5: execvpe("ls", args, env); break;
            }
            /* A successful exec never returns to this code. */
            fail(names[i]);
        }
        reap(pid);
    }
    return 0;
}
