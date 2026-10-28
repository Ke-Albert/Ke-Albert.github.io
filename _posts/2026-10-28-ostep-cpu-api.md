---
layout: post
title: "OSTEP CPU API：8 道进程作业源码与逐段解析"
date: 2026-10-28 12:00:00 +0800
tag: 操作系统
description: "面向 C 语言初学者，左右对照阅读 fork、exec、wait、文件描述符与管道的 8 道作业。"
excerpt: "左侧源码，右侧解析：从 fork 后的变量到用管道连接两个子进程，逐段理解 Linux 进程 API。"
---

本文整理 OSTEP CPU API 的 8 道作业，面向刚接触 C 语言的读者。源码沿用本地实验版本，解析由学习对话整理，补充公共辅助函数与运行方法。

**阅读方式：**宽屏下左侧是源码，右侧是对应解析；每段标注原文件行号。窄屏下每段依次显示源码和解析。各段按顺序拼接就是完整原文件，空行也保留；可以直接下载完整源码，无需手动拼接。

[下载全部源码（ZIP）]({{ '/assets/code/ostep-cpu-api/ostep-cpu-api.zip' | relative_url }})

<link rel="stylesheet" href="{{ '/assets/css/ostep-cpu-api.css' | relative_url }}" />

## 阅读前：几个 C 语言符号

| 写法 | 含义 |
| --- | --- |
| `int x = 100;` | 声明整数变量 x，初始值为 100；分号结束一条语句 |
| `=` / `==` / `!=` | 赋值 / 判断相等 / 判断不相等 |
| `{ ... }` | 一组语句组成的代码块；缩进方便阅读 |
| `if (条件)` | 条件成立才执行后面的语句或代码块 |
| `&&` / <code>&#124;&#124;</code> / `!` | 逻辑且 / 逻辑或 / 逻辑非；前两者会短路求值 |
| `&x` | 变量 x 的地址，函数可通过地址把结果写回变量 |
| `char buffer[256]` | 能存放 256 个字符的数组，下标从 0 开始 |
| `char *p` | 指向字符的指针，可用于访问字符串或字节数据 |
| `条件 ? A : B` | 条件成立取 A，否则取 B |
| `/* ... */` | 注释，不会执行 |
| `return 0;` | 从 main 返回时，表示当前进程成功结束 |

进程是正在运行的程序。`fork()` 创建子进程，成功后父子都继续执行后面的代码：父进程得到子进程 PID，子进程得到 0。变量各有副本，但继承的文件描述符可能指向同一个内核对象。

## 编译与运行

解压源码后，在解压目录执行：

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 fork_variable.c -o fork_variable
./fork_variable
make -C homework
make -C homework run
```

第 2～8 题依赖同目录中的 `common.h`。下面展示 Linux 实验的典型输出；PID、进程间调度顺序和部分错误的数值可能不同。第 2 题每次运行都会清空运行目录中的 `q2_output.txt`。


## 作业 1：fork 后，父子进程共享变量吗？

先令 `x = 100`，创建子进程后分别修改 x，观察另一个进程是否受到影响。

[下载 fork_variable.c]({{ '/assets/code/ostep-cpu-api/fork_variable.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–8 行**

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
```

</div>
<div class="cpu-explain" markdown="1">

`errno.h` 提供错误编号；`stdio.h` 提供打印函数；`stdlib.h` 提供退出状态常量；`sys/wait.h` 提供等待进程的功能；`unistd.h` 提供 `fork()` 等接口。

`int main(void)` 是程序入口，返回整数，不接收参数；大括号开始函数体。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 9–13 行**

```c
    int x = 100;
    printf("Before fork: x = %d\n", x);
    /* Flush buffered output so fork does not duplicate it. */
    fflush(stdout);

```

</div>
<div class="cpu-explain" markdown="1">

`int x = 100` 声明整数并赋值。`printf()` 中 `%d` 用 x 的值替换，`\n` 表示换行。

`fflush(stdout)` 把输出缓冲中的文字交给底层输出，避免尚未输出的缓冲随 fork 被复制，最后重复打印。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 14–19 行**

```c
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

```

</div>
<div class="cpu-explain" markdown="1">

`pid_t` 是存放进程编号的类型。`fork()` 返回 -1 表示创建失败，此时 `perror("fork")` 打印错误原因，返回 `EXIT_FAILURE` 表示失败退出。

成功时，父进程的 pid 大于 0，子进程的 pid 等于 0；两者从调用返回处继续运行。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 20–26 行**

```c
    if (pid == 0) {
        printf("Child: inherited x = %d\n", x);
        x = 200;
        printf("Child: after changing x, x = %d\n", x);
        return EXIT_SUCCESS;
    }

```

</div>
<div class="cpu-explain" markdown="1">

`if (pid == 0)` 只有子进程进入。它先打印继承的 x，值为 100；再将自己的 x 改成 200，并打印。

`return EXIT_SUCCESS` 成功结束子进程，后面的父进程代码不会被它执行。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 27–35 行**

```c
    /* Wait for the child to finish to make the output order predictable. */
    int status;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("waitpid");
        return EXIT_FAILURE;
    }
```

</div>
<div class="cpu-explain" markdown="1">

父进程声明 `status` 接收退出信息。`waitpid(pid, &status, 0)` 等待并回收指定子进程。

若返回 -1 且 `errno == EINTR`，表示被信号中断，`continue` 重新等待；其他错误打印原因并失败退出。这里的等待使父进程在子进程结束后才继续打印。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 36–40 行**

```c
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Child did not exit successfully\n");
        return EXIT_FAILURE;
    }

```

</div>
<div class="cpu-explain" markdown="1">

`WIFEXITED(status)` 判断是否正常退出；确认正常后，`WEXITSTATUS(status)` 提取退出码。

`!` 表示非，`||` 表示或：没有正常退出，或者退出码不是成功值，就报告错误。逻辑或短路，前项成立时不再计算后项。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 41–45 行**

```c
    printf("Parent: after the child changed x, x = %d\n", x);
    x = 300;
    printf("Parent: after changing x, x = %d\n", x);
    return EXIT_SUCCESS;
}
```

</div>
<div class="cpu-explain" markdown="1">

父进程此时打印 x，仍然是 100，说明子进程修改的是自己的副本。随后父进程把自己的 x 改为 300，打印并成功结束。

</div>
</div>

</div>

典型输出：

```text
Before fork: x = 100
Child: inherited x = 100
Child: after changing x, x = 200
Parent: after the child changed x, x = 100
Parent: after changing x, x = 300
```

**结论：**fork 后普通变量各有副本，修改互不影响。Linux 常用写时复制实现这种语义：开始可共享物理内存页，修改时再复制；这不等于父子共享同一个可随意互改的变量。


## 作业 2：fork 后共享打开的文件

先打开文件，再创建子进程，让父子各写入 1000 条记录。

[下载 q2_file.c]({{ '/assets/code/ostep-cpu-api/q2_file.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–5 行**

```c
#include <fcntl.h>
#include "common.h"

int main(void)
{
```

</div>
<div class="cpu-explain" markdown="1">

`fcntl.h` 提供 `open()` 和文件打开选项；`common.h` 提供公共辅助函数，文末会解释。其余是程序入口。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 6–8 行**

```c
    int fd = open("q2_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fail("open");
    printf("Before fork: fd = %d\n", fd);
```

</div>
<div class="cpu-explain" markdown="1">

`open()` 返回文件描述符，存入整数 fd。`O_WRONLY` 表示只写，`O_CREAT` 表示不存在就创建，`O_TRUNC` 表示存在就清空；`|` 把选项组合起来。

`0644` 是八进制权限，通常表示所有者可读写，其他人只读；新建权限还受 umask 影响。返回负数则报错。`printf()` 打印 fd，常见为 3，但不保证。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 9–11 行**

```c
    pid_t pid = spawn();
    const char *who = pid == 0 ? "child" : "parent";
    /* Each record is submitted with one write in the normal case. */
```

</div>
<div class="cpu-explain" markdown="1">

`spawn()` 包装了 fork。父子继承的 fd 指向同一个内核打开文件描述。

`const char *who` 指向只读使用的文字：子进程 pid 为 0，选择 `"child"`；父进程选择 `"parent"`。注释说明正常情况下一条记录由一次 write 提交。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 12–16 行**

```c
    for (int i = 0; i < 1000; ++i) {
        char line[64];
        int len = snprintf(line, sizeof(line), "%s %04d\n", who, i);
        write_all(fd, line, (size_t)len);
    }
```

</div>
<div class="cpu-explain" markdown="1">

`for` 让 i 从 0 到 999，每轮结束 `++i` 加 1。父子各循环 1000 次。

`char line[64]` 是临时字符数组。`snprintf()` 按容量限制把文字写进数组：`%s` 放 who，`%04d` 将整数补零到至少 4 位，`\n` 换行。`len` 是生成文字的字节数，不包含字符串结束符；本题的行都能放下。

`write_all()` 向 fd 写入 len 个字节，`(size_t)` 将长度转成大小类型。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 17–21 行**

```c
    if (close(fd) < 0) fail("close");
    if (pid != 0) {
        reap(pid);
        puts("Both processes wrote 1000 records to q2_output.txt");
    }
```

</div>
<div class="cpu-explain" markdown="1">

父子各关闭自己的 fd；一方关闭不会关闭另一方的描述符。

只有父进程进入 `pid != 0` 分支，等待子进程完成，再打印“双方各写了 1000 条”的提示。等待在父进程写入之后，因此前面的写入仍可并发。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 22–23 行**

```c
    return 0;
}
```

</div>
<div class="cpu-explain" markdown="1">

父子最终各自返回 0，成功退出。

</div>
</div>

</div>

**结论：**描述符表各自独立，但这里指向同一个打开文件描述，因此共享文件偏移量。当前 Linux 对普通文件的这种共享偏移更新是原子的，正常完成的写入接着共享位置继续写，不会因 fork 各从偏移 0 覆盖。

父子记录可能交错，也可能连续出现很多同一方记录；每一方内部仍按 0000～0999 写入。多次 write 组成的逻辑记录不因此具有整体原子性；短写后补写也可能被其他写入穿插。


## 作业 3：不用 wait，保证子进程先输出

子进程输出 hello 并刷新后，通过管道通知父进程；父进程收到通知才输出 goodbye。

[下载 q3_order.c]({{ '/assets/code/ostep-cpu-api/q3_order.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–4 行**

```c
#include "common.h"

int main(void)
{
```

</div>
<div class="cpu-explain" markdown="1">

引入公共头文件并进入 main，与前面相同。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 5–7 行**

```c
    int ready[2];
    if (pipe(ready) < 0) fail("pipe");
    pid_t pid = spawn();
```

</div>
<div class="cpu-explain" markdown="1">

`int ready[2]` 保存两个文件描述符。`pipe(ready)` 创建管道：`ready[0]` 是读端，`ready[1]` 是写端。失败则报错。

先创建管道再 fork，所以父子都继承两端的描述符，指向同一条内核管道。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 8–10 行**

```c
    if (pid == 0) {
        close(ready[0]);
        if (puts("hello") == EOF || fflush(stdout) == EOF) fail("stdout");
```

</div>
<div class="cpu-explain" markdown="1">

子进程进入 `pid == 0` 分支，关闭不需要的读端。`puts()` 输出 hello 并换行，`fflush(stdout)` 确保文字离开输出缓冲。

任一操作返回 EOF 就报错；`||` 左侧已成立时不会执行右侧。必须先刷新再通知，否则重定向到文件时，hello 可能还滞留在缓冲中。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 11–15 行**

```c
        /* Signal only after hello has actually left the stdio buffer. */
        write_all(ready[1], "!", 1);
        close(ready[1]);
        return 0;
    }
```

</div>
<div class="cpu-explain" markdown="1">

注释强调发送时机。`write_all(ready[1], "!", 1)` 向管道发送 1 个字节，意思是“hello 已经输出”。字符本身不重要，也不会被打印到终端。

关闭写端并返回 0，子进程结束，不执行后面的父进程代码。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 16–21 行**

```c
    close(ready[1]);
    char token;
    ssize_t n;
    do {
        n = read(ready[0], &token, 1);
    } while (n < 0 && errno == EINTR);
```

</div>
<div class="cpu-explain" markdown="1">

父进程关闭自己的写端，声明字符 token 接收通知、带符号整数 n 接收读取结果。

`read(ready[0], &token, 1)` 最多读一个字节到 token 的地址。没有数据但仍有写端打开时会阻塞；被信号中断则重试。`&&` 要求两个条件同时成立。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 22–26 行**

```c
    if (n < 0) fail("read");
    if (n != 1) {
        fprintf(stderr, "Child did not signal readiness\n");
        return 1;
    }
```

</div>
<div class="cpu-explain" markdown="1">

n 小于 0 表示错误；n 等于 1 表示收到通知；n 等于 0 表示所有写端关闭且已无数据。

这里非 1 就向 `stderr`（标准错误流）打印“没有收到通知”，返回 1。程序不检查字符内容，只要求一个字节到达。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 27–31 行**

```c
    close(ready[0]);
    puts("goodbye");
    /* No wait() or waitpid(): this short-lived parent exits immediately. */
    return 0;
}
```

</div>
<div class="cpu-explain" markdown="1">

收到通知后关闭读端，打印 goodbye。这里没有 wait 或 waitpid，等待的是通知事件，而不是子进程退出。父进程随后成功退出。

</div>
</div>

</div>

输出顺序固定：

```text
hello
goodbye
```

**为什么父进程关闭写端后，读取还会等待？**父进程只关闭自己持有的引用，子进程还持有同一个管道的写端。即使子进程还没获得 CPU，它也已在 fork 时继承了写端。

```text
父进程 ready[0] → 同一个管道 ← 子进程 ready[1]
父进程写端已关闭              子进程读端已关闭
```

描述符独立意味着关闭操作互不影响；指向同一个管道意味着可以通信。只有所有写端都关闭且数据读完，read 才返回 0。

本题父进程短暂运行后退出；长期运行的程序仍需另行回收子进程，避免僵尸进程积累。


## 作业 4：exec 的六种变体

fork 创建进程；exec 将当前进程的程序映像替换成另一个程序，PID 不变。本题每轮在子进程中运行 ls。

[下载 q4_exec.c]({{ '/assets/code/ostep-cpu-api/q4_exec.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–5 行**

```c
#define _GNU_SOURCE
#include "common.h"

int main(void)
{
```

</div>
<div class="cpu-explain" markdown="1">

`#define _GNU_SOURCE` 在所有头文件之前启用 GNU 扩展声明，因为 execvpe 是 GNU 扩展。之后是公共头文件和程序入口。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 6–8 行**

```c
    const char *names[] = {"execl", "execle", "execlp", "execv", "execvp", "execvpe"};
    char *args[] = {"ls", "-1", "/bin/ls", NULL};
    char *env[] = {"PATH=/bin:/usr/bin", "LANG=C", NULL};
```

</div>
<div class="cpu-explain" markdown="1">

`names` 保存六种函数的名称文字，只用于打印提示，不会通过文字调用函数。

`args` 是参数数组，相当于命令 `ls -1 /bin/ls`：`ls` 是新程序看到的 argv[0]，`-1` 是数字 1，表示每项一行；`/bin/ls` 是要列出的文件，末尾 NULL 表示数组结束。

`env` 是显式环境数组：PATH 指定搜索目录，LANG=C 指定语言环境，NULL 结束数组。创建数组本身不会改变当前环境。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 9–10 行**

```c
    /* The p variants search the caller's PATH, including execvpe(). */
    if (setenv("PATH", "/bin:/usr/bin", 1) < 0) fail("setenv");
```

</div>
<div class="cpu-explain" markdown="1">

`setenv("PATH", "/bin:/usr/bin", 1)` 修改当前进程的 PATH，1 表示覆盖已有值。之后创建的子进程继承它。

带 p 的调用可搜索 ls；尤其 execvpe 使用调用者环境中的 PATH 搜索，而不是用传入 env 中的 PATH。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 11–15 行**

```c
    for (int i = 0; i < 6; ++i) {
        printf("=== %s ===\n", names[i]);
        pid_t pid = spawn();
        if (pid == 0) {
            switch (i) {
```

</div>
<div class="cpu-explain" markdown="1">

循环六次，打印函数名，每轮创建一个子进程。只有子进程进入 switch，根据 i 的值选择一个 case。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 16–16 行**

```c
            case 0: execl("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execl`：指定路径，参数逐项列出，以字符空指针结束；继承环境。第一个 `/bin/ls` 是程序路径，后面的 `/bin/ls` 是传给它的文件参数。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 17–17 行**

```c
            case 1: execle("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL, env); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execle`：参数逐项列出，空指针标记之后再传入 env，显式指定新程序环境。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 18–18 行**

```c
            case 2: execlp("ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execlp`：逐项参数，但用名字 ls 根据 PATH 搜索程序；继承环境。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 19–19 行**

```c
            case 3: execv("/bin/ls", args); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execv`：指定路径，参数用 args 数组提供；继承环境。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 20–20 行**

```c
            case 4: execvp("ls", args); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execvp`：根据 PATH 搜索程序，参数用数组提供；继承环境。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 21–21 行**

```c
            case 5: execvpe("ls", args, env); break;
```

</div>
<div class="cpu-explain" markdown="1">

`execvpe`：根据调用者 PATH 搜索程序，参数用数组，新程序环境由 env 提供。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 22–25 行**

```c
            }
            /* A successful exec never returns to this code. */
            fail(names[i]);
        }
```

</div>
<div class="cpu-explain" markdown="1">

switch 结束。exec 成功时不会返回，子进程开始运行 ls；ls 结束后进程退出，不回到原代码。

只有失败时才会返回 -1，执行 case 后的 break，离开 switch，再由 `fail(names[i])` 打印调用名称及错误。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 26–29 行**

```c
        reap(pid);
    }
    return 0;
}
```

</div>
<div class="cpu-explain" markdown="1">

父进程等待本轮子进程成功结束，再进入下一轮，所以六次输出依次完成。循环结束后父进程返回 0。

</div>
</div>

</div>

记忆方式：l = list（逐项参数），v = vector（参数数组），p = PATH 搜索，e = 显式环境。六种方式都执行 `ls -1 /bin/ls`，每种都会输出一行 `/bin/ls`，前面有对应函数名的提示。


## 作业 5：wait 的返回值与等待范围

子进程没有自己的子进程，调用 wait 会失败；父进程则能等待并回收它。

[下载 q5_wait.c]({{ '/assets/code/ostep-cpu-api/q5_wait.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–7 行**

```c
#include <string.h>
#include "common.h"

int main(void)
{
    pid_t pid = spawn();
    if (pid == 0) {
```

</div>
<div class="cpu-explain" markdown="1">

`string.h` 提供 strerror，将错误编号转换成说明文字。创建子进程并进入子进程分支。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 8–11 行**

```c
        int status;
        errno = 0;
        pid_t result = wait(&status);
        int saved_errno = errno;
```

</div>
<div class="cpu-explain" markdown="1">

status 接收退出状态；errno 先清零。`wait(&status)` 尝试回收自己的一个子进程。这里没有子进程，所以立即返回 -1，错误是 ECHILD。

立即将 errno 存入 saved_errno，保留本次错误，避免后续函数影响它。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 12–16 行**

```c
        printf("Child: wait() = %ld, errno = %d (%s)\n",
               (long)result, saved_errno, strerror(saved_errno));
        if (result != -1 || saved_errno != ECHILD) return 1;
        return 42;
    }
```

</div>
<div class="cpu-explain" markdown="1">

打印返回值、错误编号和说明。`%ld` 对应 long 整数，所以把 result 转成 long；`%s` 对应 strerror 返回的文字。

若不是预期的 -1 和 ECHILD，就返回 1；符合预期则以 42 退出，供父进程检查。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 17–22 行**

```c
    int status;
    pid_t result;
    do {
        result = wait(&status);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("wait");
```

</div>
<div class="cpu-explain" markdown="1">

父进程准备自己的 status 和 result，调用 wait，若被信号中断就重试；其他错误报错。子进程已经退出也没关系，父进程仍可取得并回收它的退出信息。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 23–25 行**

```c
    printf("Parent: child PID = %ld, wait() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
```

</div>
<div class="cpu-explain" markdown="1">

打印创建时的 PID 和 wait 返回的 PID，它们应该相同。`WIFEXITED(status)` 判断是否正常退出；前面的 ! 表示“不”。未正常退出则返回 1。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 26–28 行**

```c
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
```

</div>
<div class="cpu-explain" markdown="1">

确认正常退出后，用 WEXITSTATUS 提取退出码，应该为 42。最后同时检查 PID 和退出码，符合预期返回 0，否则返回 1。

</div>
</div>

</div>

**wait 返回的是 PID，不是退出码。**退出信息写入 status，要先判断正常退出，再提取退出码。子进程当然也能调用 wait，但只能等待自己的子进程，不能用它等待父进程。


## 作业 6：waitpid 可以指定等待谁

与上一题对照，父进程使用 waitpid 明确指定等待的子进程。

[下载 q6_waitpid.c]({{ '/assets/code/ostep-cpu-api/q6_waitpid.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–9 行**

```c
#include "common.h"

int main(void)
{
    pid_t pid = spawn();
    if (pid == 0) {
        puts("Child: exiting with status 42");
        return 42;
    }
```

</div>
<div class="cpu-explain" markdown="1">

创建子进程。子进程打印提示，返回 42；父进程跳过此分支。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 10–15 行**

```c
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("waitpid");
```

</div>
<div class="cpu-explain" markdown="1">

`waitpid(pid, &status, 0)` 的三个参数依次为：目标子进程 PID、接收状态的地址、等待选项。0 表示默认阻塞方式。被信号中断就重试，其他错误报错。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 16–21 行**

```c
    printf("Parent: requested PID = %ld, waitpid() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
```

</div>
<div class="cpu-explain" markdown="1">

打印目标 PID 和实际返回的 PID，确认正常退出，再提取退出码。最后验证返回 PID 正确且退出码为 42。

</div>
</div>

</div>

`wait(&status)` 相当于 `waitpid(-1, &status, 0)`，可回收任意一个可等待的子进程；`waitpid(pid, ...)` 可指定某个子进程。

若把选项改为 `WNOHANG`，目标还在运行且没有可报告状态时返回 0，父进程可以继续工作；这是额外用法，本题源码仍采用阻塞等待。


## 作业 7：关闭标准输出后调用 printf

关闭子进程的标准输出描述符，再观察 printf 的返回值和错误。

[下载 q7_stdout.c]({{ '/assets/code/ostep-cpu-api/q7_stdout.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–7 行**

```c
#include <string.h>
#include "common.h"

int main(void)
{
    /* Unbuffered stdout exposes the error directly in printf(). */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return 1;
```

</div>
<div class="cpu-explain" markdown="1">

引入错误说明和公共功能。`setvbuf(stdout, NULL, _IONBF, 0)` 将标准输出设为无缓冲，使错误直接在 printf 调用时暴露；NULL 表示不提供自定义缓冲，最后的 0 在此模式下不需要指定缓冲区容量。失败则退出。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 8–13 行**

```c
    pid_t pid = spawn();
    if (pid == 0) {
        if (close(STDOUT_FILENO) < 0) fail("close");
        errno = 0;
        int result = printf("Child: this text cannot reach stdout\n");
        int saved_errno = errno;
```

</div>
<div class="cpu-explain" markdown="1">

创建子进程，关闭 `STDOUT_FILENO`，即描述符 1。errno 清零后调用 printf，保存返回值和错误。

`stdout` 是 C 的流对象，`STDOUT_FILENO` 是系统描述符；关闭后流对象仍在，底层输出通道失效。无缓冲使写入立即失败，printf 返回负数，错误为 EBADF。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 14–17 行**

```c
        fprintf(stderr, "Child: printf() = %d, ferror(stdout) = %d, errno = %d (%s)\n",
                result, ferror(stdout), saved_errno, strerror(saved_errno));
        return result < 0 && saved_errno == EBADF ? 0 : 1;
    }
```

</div>
<div class="cpu-explain" markdown="1">

通过没有关闭的 `stderr` 报告结果。stderr 通常对应描述符 2。`ferror(stdout)` 非零表示流的错误标志已设置。

若 printf 返回负数且错误为 EBADF，实验符合预期，子进程返回 0；这里输出失败正是实验希望观察的现象。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 18–21 行**

```c
    reap(pid);
    puts("Parent: stdout still works");
    return 0;
}
```

</div>
<div class="cpu-explain" markdown="1">

父进程回收子进程，然后正常打印提示。子进程关闭自己的描述符 1，不影响父进程的描述符 1。

</div>
</div>

</div>

典型结果：

```text
Child: printf() = -1, ferror(stdout) = 1, errno = 9 (Bad file descriptor)
Parent: stdout still works
```

如果保留默认缓冲，printf 可能先把文字放进内存，看似成功；错误到 fflush 或退出刷新时才暴露。流错误标志的非零数值和 errno 数字不应当作跨系统固定值。


## 作业 8：用管道连接两个子进程

第一个子进程产生文本，第二个子进程读取并输出。父进程负责创建、关闭多余端点和回收。

[下载 q8_pipe.c]({{ '/assets/code/ostep-cpu-api/q8_pipe.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–6 行**

```c
#include "common.h"

int main(void)
{
    int channel[2];
    if (pipe(channel) < 0) fail("pipe");
```

</div>
<div class="cpu-explain" markdown="1">

引入公共功能，创建管道。channel[0] 是读端，channel[1] 是写端。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 7–9 行**

```c
    pid_t producer = spawn();
    if (producer == 0) {
        close(channel[0]);
```

</div>
<div class="cpu-explain" markdown="1">

创建生产者 producer，子进程进入分支，关闭自己的读端。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 10–11 行**

```c
        if (dup2(channel[1], STDOUT_FILENO) < 0) fail("dup2");
        if (channel[1] != STDOUT_FILENO) close(channel[1]);
```

</div>
<div class="cpu-explain" markdown="1">

`dup2(channel[1], STDOUT_FILENO)` 让描述符 1 指向管道写端，原本打开的目标会被替换。以后 printf 仍写标准输出，但目的地变成管道。

重定向后关闭原始写端描述符，只留下描述符 1。若原写端本来就是 1，则不能关闭，所以先判断是否相等。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 12–15 行**

```c
        printf("hello from child 1\nthis is a pipe\n");
        if (fflush(stdout) == EOF) fail("fflush");
        return 0;
    }
```

</div>
<div class="cpu-explain" markdown="1">

printf 写两行文字到管道，fflush 确保缓冲被写出。生产者成功退出时，系统会关闭它剩余的描述符，包括连接管道的标准输出。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 16–20 行**

```c
    pid_t consumer = spawn();
    if (consumer == 0) {
        close(channel[1]);
        if (dup2(channel[0], STDIN_FILENO) < 0) fail("dup2");
        if (channel[0] != STDIN_FILENO) close(channel[0]);
```

</div>
<div class="cpu-explain" markdown="1">

只有父进程到这里创建消费者 consumer，生产者已在自己的分支返回。消费者关闭写端，用 dup2 将标准输入（描述符 0）连接管道读端，然后关闭多余原始读端。

两个子进程是同一个父进程的孩子，消费者由父进程创建，未继承生产者对标准输出的修改。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 21–26 行**

```c
        char buffer[256];
        for (;;) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) fail("read");
            if (n == 0) break;
```

</div>
<div class="cpu-explain" markdown="1">

buffer 是 256 字节数组，`for (;;)` 是无限循环。read 从已重定向的标准输入读取最多 256 字节，返回实际字节数 n。数组名传参时转换为首元素指针，所以这里不用 &token 那样的写法。

被信号中断则 continue 重试；其他错误退出；n 等于 0 表示读完且所有写端已关闭，break 跳出循环。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 27–30 行**

```c
            write_all(STDOUT_FILENO, buffer, (size_t)n);
        }
        return 0;
    }
```

</div>
<div class="cpu-explain" markdown="1">

把刚读到的 n 个字节写到消费者的标准输出。消费者只重定向了输入，输出仍连接终端或外部指定的输出位置。

必须写 n 个字节，不能写整个数组；read 不会自动添加字符串结束符。读取循环结束后消费者成功退出。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 31–34 行**

```c
    /* Without closing the parent's write end, the consumer cannot see EOF. */
    close(channel[0]);
    close(channel[1]);
    reap(producer);
```

</div>
<div class="cpu-explain" markdown="1">

注释说明：父进程若保留写端，消费者就无法看到 EOF。父进程不传输数据，所以关闭自己的两端。随后等待生产者。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 35–38 行**

```c
    reap(consumer);
    puts("Parent: both children finished");
    return 0;
}
```

</div>
<div class="cpu-explain" markdown="1">

再等待消费者，然后打印完成提示。父进程等待生产者时，消费者仍能同时运行；阻塞父进程不会暂停其他进程。最后返回 0。

</div>
</div>

</div>

输出：

```text
hello from child 1
this is a pipe
Parent: both children finished
```

数据流向：

```text
生产者 printf → 标准输出 1 → 管道 → 消费者标准输入 0
                                     ↓
                                  read → buffer → write_all → 终端
```

管道是字节流，单次读取不一定对应单次写入，所以消费者循环读取直到 EOF。所有进程都应关闭不需要的端点，尤其不能遗漏父进程的写端。


## 附录：公共辅助函数

第 2～8 题使用下面的完整公共头文件。它把重复的错误处理、创建进程、写入和回收操作封装起来。

[下载 common.h]({{ '/assets/code/ostep-cpu-api/common.h' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 1–9 行**

```c
#ifndef HOMEWORK_COMMON_H
#define HOMEWORK_COMMON_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

```

</div>
<div class="cpu-explain" markdown="1">

`#ifndef` 与 `#define` 配合文末的 `#endif`，防止一个编译单元重复包含本头文件。各头文件依次提供错误编号、输入输出、退出、类型、等待进程和系统调用声明。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 10–15 行**

```c
static inline void fail(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

```

</div>
<div class="cpu-explain" markdown="1">

`fail(const char *what)` 接收错误操作的名称，perror 打印名称和 errno 对应的说明，exit 立即以失败状态结束当前进程。

`void` 表示不返回值；`static inline` 让这类函数可定义在头文件中，具有编译单元内部链接，并允许内联优化，是否真的内联由编译器决定。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 16–23 行**

```c
static inline pid_t spawn(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) fail("fork");
    return pid;
}

```

</div>
<div class="cpu-explain" markdown="1">

spawn 先用 `fflush(NULL)` 刷新所有打开的输出流，避免 fork 复制未刷新缓冲。再 fork，失败则报错，成功返回各自拿到的 pid。这里没有检查 fflush 返回值，是该教学辅助函数的简化。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 24–34 行**

```c
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

```

</div>
<div class="cpu-explain" markdown="1">

write_all 接收描述符、数据地址、剩余字节数。只要还有数据，就调用 write。被信号中断则重试，其他负返回或零进展则失败退出。

`data += n` 将指针向后移动 n 个字节；`size -= (size_t)n` 扣除已写字节，下一轮继续写剩余部分。因此能处理短写，但并不把多次调用变成一个不可分割的写入。

</div>
</div>

<div class="cpu-row">
<div class="cpu-source" markdown="1">

**源码 · 第 35–46 行**

```c
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
```

</div>
<div class="cpu-explain" markdown="1">

reap 等待指定子进程。EINTR 时重试，其他错误退出；随后要求正常退出且退出码为 0，否则报告 Child failed。

作业 5、6 故意让子进程返回 42，所以这两题自己解析状态，不使用要求退出码为 0 的 reap。末尾 #endif 结束头文件保护。

</div>
</div>

</div>

## 本次实验的范围

源码保持实验原样，适合学习正常运行路径。某些关闭和输出调用未逐一检查，部分创建失败后的资源清理也作了简化；它们不是完整的生产环境进程管理框架。附录明确列出这些辅助函数，避免把自定义封装误认为 C 或 POSIX 自带接口。
