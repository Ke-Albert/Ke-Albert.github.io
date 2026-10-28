#include <fcntl.h>
#include "common.h"

int main(void)
{
    int fd = open("q2_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fail("open");
    printf("Before fork: fd = %d\n", fd);
    pid_t pid = spawn();
    const char *who = pid == 0 ? "child" : "parent";
    /* Each record is submitted with one write in the normal case. */
    for (int i = 0; i < 1000; ++i) {
        char line[64];
        int len = snprintf(line, sizeof(line), "%s %04d\n", who, i);
        write_all(fd, line, (size_t)len);
    }
    if (close(fd) < 0) fail("close");
    if (pid != 0) {
        reap(pid);
        puts("Both processes wrote 1000 records to q2_output.txt");
    }
    return 0;
}
