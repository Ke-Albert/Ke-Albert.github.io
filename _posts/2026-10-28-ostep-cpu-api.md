---
layout: post
title: "OSTEP CPU API：从文件描述符到进程通信的 8 道作业"
date: 2026-10-28 12:00:00 +0800
tag: 操作系统
description: "先区分文件描述符、标准流和大写常量，再逐段对照源码理解 fork、exec、wait 与管道通信。"
excerpt: "STDOUT_FILENO 与 stdout 有什么区别？先建立输入输出知识地图，再按步骤阅读 8 道进程作业。"
last_modified_at: 2026-10-30T16:25:24.562307+08:00
---


本文从“程序怎样找到输入输出的目标”开始，再逐步走到父子进程通信。8 道作业的源码保留原样；每一步先说明目的，再解释关键语句，最后把运行现象与原因连起来。

讲解组织参考 [labml 的注释式代码阅读方式](https://nn.labml.ai/transformers/mha.html)：用小段代码配对应说明，并给每一步提供定位链接。本文仍将**源码放在左侧、解析放在右侧**；手机端每步按源码、解析顺序阅读。原文件行号显示在代码左侧。

[下载全部源码（ZIP）]({{ '/assets/code/ostep-cpu-api/ostep-cpu-api.zip' | relative_url }})

<link rel="stylesheet" href="{{ '/assets/css/ostep-cpu-api.css' | relative_url }}" />

<nav class="cpu-index" aria-label="学习导航">
<a href="#fd-primer">先修知识</a>
<a href="#lesson-1">1 · fork 与变量</a>
<a href="#lesson-2">2 · 共享文件</a>
<a href="#lesson-3">3 · 管道通知</a>
<a href="#lesson-4">4 · exec</a>
<a href="#lesson-5">5 · wait</a>
<a href="#lesson-6">6 · waitpid</a>
<a href="#lesson-7">7 · 关闭输出</a>
<a href="#lesson-8">8 · 连接进程</a>
<a href="#helpers">公共函数</a>
</nav>

<div class="cpu-primer" markdown="1">

<h2 id="fd-primer">先修知识：文件描述符与输入输出名称</h2>

读后面的代码时，每碰到一个输入输出名字，先问三件事：**它是整数编号，还是 C 的流？它属于哪个进程？它现在指向什么？**这比孤立记住一串大写名称更有用。

### 1. 文件描述符是进程用来找资源的编号

文件描述符（file descriptor，简称 fd）是一个非负整数。操作系统为每个进程维护一张描述符表：程序把编号交给 read、write、close 等函数，内核据此找到要操作的对象。对象可以是普通文件，也可以是管道或终端。

```text
程序中的整数 fd
       ↓ 作为索引
当前进程的描述符表项
       ↓ 引用
内核中的打开状态 → 文件、管道或终端
```

所以，`int fd` 只是一个普通整数变量；`open()` 返回的编号恰好保存在这里。换成 `int output` 也可以，变量名不决定它的用途。open 成功通常返回当前未使用的最小编号；关闭编号后，它还能被后续打开操作重新使用。**相同编号出现在不同进程中，不代表指向相同对象。**这些是 Linux 文件接口的基本约定，见 [open 手册](https://man7.org/linux/man-pages/man2/open.2.html)。

### 2. 三个标准通道：大写的是编号，小写的是流

程序正常启动时，通常有下面三组对应关系。它们常连接终端，但也可以被重定向到文件或管道；“标准输出”不等于“永远输出到屏幕”。

| 用途 | 大写名称：整数描述符 | 数值 | 小写名称：C 标准流 | 常见操作 |
| --- | --- | --- | --- | --- |
| 标准输入：程序从哪里读 | `STDIN_FILENO` | 0 | `stdin` | `read(0, ...)`；`scanf(...)` 默认读 stdin |
| 标准输出：正常结果到哪里写 | `STDOUT_FILENO` | 1 | `stdout` | `write(1, ...)`；`printf(...)`、`puts(...)` |
| 标准错误：诊断信息到哪里写 | `STDERR_FILENO` | 2 | `stderr` | `write(2, ...)`；`fprintf(stderr, ...)`、`perror(...)` |

三个 `*_FILENO` 在 `<unistd.h>` 中定义。FILENO 可以理解为“file number”。它们让代码表达用途，而不是散落难懂的 0、1、2。

小写三者由 `<stdio.h>` 提供，类型为 `FILE *`；`FILE` 是 C 库管理流的类型名称。流可在描述符之上管理缓冲、错误标志等信息。因此两层接口的参数不能随意互换：

```c
write(STDOUT_FILENO, data, size);  /* 接收整数描述符 */
fprintf(stdout, "%s", text);      /* 接收 FILE * 流 */
fflush(stdout);                   /* 刷新这个流 */
close(STDOUT_FILENO);             /* 关闭这个描述符 */
```

在本组程序中，关系可以画成：

```text
printf / puts → stdout（C 库流与缓冲） → 描述符 1 → 终端 / 文件 / 管道
write        ──────────────────────→ 描述符 1 → 同一输出目标
```

`close(1)` 不会顺手销毁 stdout 流对象；之后再通过它写入，底层操作会失败。反过来，流中的字符还可能没被真正写出，这就是第 3、7、8 题关心 fflush 或无缓冲模式的原因。流和原始描述符操作混用时应处理好缓冲与顺序；本组例子通过先刷新或禁用缓冲控制它们。[stdin/stdout/stderr 手册](https://man7.org/linux/man-pages/man3/stdin.3.html)说明了这两层关系。

### 3. 8 道作业里的描述符，分别用来做什么？

| 出现位置 | 名称 | 谁持有或使用 | 实际用途 |
| --- | --- | --- | --- |
| 第 1 题 | 隐式的 1、2 | 父子各自持有 | printf 写标准输出；perror、fprintf 写标准错误；本题没有显式 open/pipe |
| 第 2 题 | `fd` | fork 后父子各一份 | 指向 fork 前打开的 q2_output.txt；两方写记录，共享文件偏移 |
| 第 3 题 | `ready[0]` | 初始父子都有，子关闭后由父使用 | 管道读端，父进程接收完成通知 |
| 第 3 题 | `ready[1]` | 初始父子都有，父关闭后由子使用 | 管道写端，子进程发送一个通知字节 |
| 第 4 题 | 隐式的标准通道 | 子进程执行 ls 时继承 | 提示及 ls 结果从标准输出流出；本例未设置执行时关闭标志 |
| 第 5、6 题 | 隐式的 1、2 | 父子分别使用 | 打印等待结果或错误；wait/waitpid 的 PID、status 都不是描述符 |
| 第 7 题 | `STDOUT_FILENO` | 子进程关闭自己的 1 | 故意让 printf 的底层输出失败；父进程的 1 不受影响 |
| 第 7 题 | `stderr` 对应的 2 | 子进程保留 | 把失败结果打印出来，不依赖已关闭的输出通道 |
| 第 8 题 | `channel[1]`、重定向后的 1 | 生产者使用 | 管道写端；把 printf 的标准输出送入管道 |
| 第 8 题 | `channel[0]`、重定向后的 0 | 消费者使用 | 管道读端；让 read 从管道接收文本 |
| 第 8 题 | 消费者的 1 | 消费者保留 | 把收到的文本输出到终端或外部指定的位置 |
| 公共函数 | `write_all` 的参数 `fd` | 调用它的当前进程 | 泛指要写的目标；可传普通文件、管道写端或标准输出 |

**`ready[0]` 的 0 是数组下标，不是描述符值 0。**如果标准通道均已占用，pipe 可能把 3、4 放入数组：这时 ready[0] 为 3、ready[1] 为 4。不能写死这个假设；后续始终使用数组中的实际返回值。[pipe 手册](https://man7.org/linux/man-pages/man2/pipe.2.html)规定 `[0]` 对应读端、`[1]` 对应写端。

### 4. fork 复制表项，close 只关闭自己的引用

下面假设 pipe 返回读端 3、写端 4，只用于示意。fork 后两个进程各有一张表，但表项引用同一条管道：

```text
父进程                         内核                       子进程
3（读端） ───────────────→ 同一管道的读端 ←─────────────── 3（读端）
4（写端） ───────────────→ 同一管道的写端 ←─────────────── 4（写端）
```

第 3 题父关闭 4、子关闭 3 后：

```text
父进程 3（读） ←────────── 管道中的字节 ←────────── 子进程 4（写）
父进程自己的 4 已关闭                           子进程自己的 3 已关闭
```

父进程先 read 时，自己的写端虽然关闭了，**子进程仍持有写端**。内核判断还有可能收到数据，所以管道为空时会阻塞。即使子进程尚未被调度，它也已在 fork 时继承描述符。只有所有写端都关闭且剩余数据读完，读取才到达 EOF。这一条件见 [pipe(7) 的 EOF 说明](https://man7.org/linux/man-pages/man7/pipe.7.html)。

第 2 题的普通文件也有类似的引用关系：各自的 fd 表项引用同一个打开文件描述，共享偏移；变量 x 的普通内存副本则不以这种方式共享。不要把“变量副本”与“描述符引用的对象”混为一谈。

### 5. dup2 改的是目标编号的去向

```c
dup2(oldfd, newfd);
```

读作：**让 newfd 引用 oldfd 所指向的打开对象。**它不把数据从 oldfd 拷贝到 newfd。若两者不同且 newfd 已打开，会先原子地关闭并重新绑定目标；成功后可关闭不再需要的原编号。若两者相同，则无需重新绑定。见 [dup2 手册](https://man7.org/linux/man-pages/man2/dup.2.html)。

第 8 题的两次调用分别是：

| 调用 | 被修改的目标编号 | 之后产生的效果 |
| --- | --- | --- |
| `dup2(channel[1], STDOUT_FILENO)` | 生产者的 1 | printf 仍写 stdout，但文字进入管道写端 |
| `dup2(channel[0], STDIN_FILENO)` | 消费者的 0 | read(0, ...) 从管道读端取数据 |

假设管道原来是 3、4，连接完成并关闭多余端点后：

```text
生产者：1 → 管道写端 → 管道读端 → 0：消费者
                                      │
                                read → 内存 buffer
                                      │
                                  write 到 1 → 终端
父进程：自己的 3、4 全部关闭，只负责回收两个孩子
```

### 6. 大写名称分类速查：不要都当成描述符

C 的标识符区分大小写。大写通常是宏或类型的命名习惯，不是“全部属于某种输入输出对象”的语言规则。

| 类别 | 名称 | 在本组程序中的用途 |
| --- | --- | --- |
| 标准描述符宏 | `STDIN_FILENO`、`STDOUT_FILENO`、`STDERR_FILENO` | 三个整数编号 0、1、2；前两个直接出现在第 7、8 题，第三个用于理解 stderr 对应的底层通道 |
| 流类型 | `FILE` | C 输入输出流的类型；FILE * 与 int fd 是不同层次 |
| 输出失败/结束标记 | `EOF` | 负的 int 常量；本组用来检查 puts、fflush 等是否失败，不是描述符 |
| 空指针宏 | `NULL` | 参数数组终止标记；或函数约定的特殊指针参数，如 fflush(NULL) |
| 打开选项 | `O_WRONLY`、`O_CREAT`、`O_TRUNC` | 只写、无则创建、存在则清空；传给 open，用单个 `|` 组合 |
| 缓冲选项 | `_IONBF` | 传给 setvbuf，选择无缓冲；它不是编号，也不是错误 |
| 错误编号 | `EINTR`、`ECHILD`、`EBADF` | 被信号中断、没有可等待子进程、无效描述符；与 errno 比较 |
| 退出状态 | `EXIT_SUCCESS`、`EXIT_FAILURE` | 告诉外界当前进程成功或失败；不表示已读字节数 |
| 等待状态宏 | `WIFEXITED(status)`、`WEXITSTATUS(status)` | 判断正常退出，再提取退出码；输入是 wait 系列写出的状态整数 |
| 等待选项（拓展） | `WNOHANG` | waitpid 的非阻塞选项；本文第 6 题扩展说明，实际源码传 0 |
| 功能声明开关 | `_GNU_SOURCE` | 在 include 前启用 GNU 扩展声明，供 execvpe 使用 |
| 自定义头文件保护 | `HOMEWORK_COMMON_H` | 防止重复包含 common.h，与资源编号无关 |
| 环境变量名 | `PATH`、`LANG` | 出现在字符串中的配置名称，分别管程序查找路径和语言环境；不是 C 宏 |

为了认出其他示例中的相近名称，可以再记两组**未在本组源码中使用的选项**：

| 接口 | 本文用到 | 常见的其他选择 |
| --- | --- | --- |
| open 的访问方式 | `O_WRONLY`：只写 | `O_RDONLY`：只读；`O_RDWR`：可读写；访问方式选其一，再组合创建等其他标志 |
| setvbuf 的缓冲方式 | `_IONBF`：无缓冲 | `_IOLBF`：行缓冲；`_IOFBF`：全缓冲；三者选其一 |

行缓冲通常遇到换行会刷新；全缓冲通常积累一批再写，显式 fflush 可要求提前刷新。缓冲模式的定义可查 [setvbuf 手册](https://man7.org/linux/man-pages/man3/setbuf.3.html)。

O_APPEND 是 open 的“追加写”选项；**本题源码没有使用它**。同理 WNOHANG 是拓展知识，不要误以为正在运行的 waitpid 已经是非阻塞的。

### 7. 同一个 0，必须看它出现在哪里

| 表达式或结果 | 0 的意思 |
| --- | --- |
| `STDIN_FILENO` 的值 | 标准输入描述符编号 |
| `ready[0]` | 数组的第一个元素；其中存放的描述符未必是 0 |
| `fork()` 返回 0 | 当前执行者是子进程 |
| `read()` 返回 0（本组正长度读取） | EOF：没有更多字节可读；不是成功读到字符 0 |
| `waitpid(pid, &status, 0)` 第三个参数 | 不额外开启等待选项；使用默认阻塞方式 |
| `waitpid(..., WNOHANG)` 返回 0 | 当前没有可报告状态，不等于 EOF |
| `return 0`（从 main 返回） | 当前进程成功退出 |
| 字符串末尾的 `'\0'` | 数值为 0 的字符，标记字符串结束，不是 EOF |

EOF 不会作为一个特殊字节从管道中读出来；它由“读返回 0”表达。NULL 则用于指针语境；虽然某些实现以 0 表示空指针常量，学习时仍应按类型和函数约定区分。[read 手册](https://man7.org/linux/man-pages/man2/read.2.html)列出了返回值规则。

</div>

## 阅读前：几个 C 语言符号

| 写法 | 含义 |
| --- | --- |
| `int x = 100;` | 声明整数并赋初值，分号结束语句 |
| `=` / `==` / `!=` | 赋值 / 相等比较 / 不等比较 |
| `{ ... }` / `if (条件)` | 代码块 / 条件成立才执行 |
| `&&` / <code>&#124;&#124;</code> / `!` | 逻辑且 / 或 / 非；前两者短路求值 |
| `&x` / `char *p` | 取变量地址 / 声明字符指针 |
| `char buffer[256]` | 字符数组，容量 256，下标从 0 开始 |
| `条件 ? A : B` | 成立取 A，否则取 B |
| `/* ... */` | 注释，不会执行 |

进程是正在运行的程序。fork 创建子进程，exec 替换当前进程运行的程序，wait 系列等待并回收自己的子进程。这三件事将在下面分别观察。

## 编译与运行

解压源码后，在解压目录执行：

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 fork_variable.c -o fork_variable
./fork_variable
make -C homework
make -C homework run
```

第 2～8 题依赖同目录中的 common.h。PID 和父子调度顺序可能变化；第 2 题每次清空运行目录中的 q2_output.txt。每个作业下方可以单独下载原始文件，文末附公共封装解析。


<span id="lesson-1"></span>

## 作业 1：fork 后，父子进程共享变量吗？

先令 `x = 100`，创建子进程后分别修改 x，观察另一个进程是否受到影响。

[下载 fork_variable.c]({{ '/assets/code/ostep-cpu-api/fork_variable.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q1-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L1–L8</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6
7
8</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-1" aria-label="此步骤的永久链接">#</a> 准备进程实验所需的接口

这些 `#include` 是把函数声明和常量定义引入当前文件，不会在这里执行函数。`stdio.h` 管打印，`unistd.h` 管 fork；`sys/wait.h` 管等待子进程；`errno.h` 和 `stdlib.h` 分别管错误编号与退出。

`int main(void)` 是程序入口：返回值为整数，不接收参数。下面的大括号开始函数体。

</div>
</div>

<div class="cpu-row" id="q1-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L9–L13</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">9
10
11
12
13</pre>
<div class="cpu-code" markdown="1">

```c
    int x = 100;
    printf("Before fork: x = %d\n", x);
    /* Flush buffered output so fork does not duplicate it. */
    fflush(stdout);

```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-2" aria-label="此步骤的永久链接">#</a> 在复制进程之前，先确定变量和输出

`int x = 100` 创建整数变量。`printf` 用 x 替换 `%d`，`\n` 表示换行。

打印函数可能先把文字留在用户态缓冲。**先 fflush，再 fork**，就不会把尚未输出的同一段文字复制到两个进程、最后各刷新一遍。此处 `stdout` 是流对象，不是数字 1。

</div>
</div>

<div class="cpu-row" id="q1-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L14–L19</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">14
15
16
17
18
19</pre>
<div class="cpu-code" markdown="1">

```c
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-3" aria-label="此步骤的永久链接">#</a> 一次调用，两个进程分别返回

`pid_t` 用来保存进程编号。`fork()` 失败返回 -1，打印错误并结束；成功后父进程得到子进程 PID，子进程得到 0。

这不是一个进程把下一段执行两遍，而是两个进程分别往下走。它们都拥有变量 x，但 x 已属于各自的地址空间。

</div>
</div>

<div class="cpu-row" id="q1-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L20–L26</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">20
21
22
23
24
25
26</pre>
<div class="cpu-code" markdown="1">

```c
    if (pid == 0) {
        printf("Child: inherited x = %d\n", x);
        x = 200;
        printf("Child: after changing x, x = %d\n", x);
        return EXIT_SUCCESS;
    }

```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-4" aria-label="此步骤的永久链接">#</a> 子进程修改的是自己的 x

`pid == 0` 把子进程导入这一分支。先打印继承值 100，再赋值为 200，随后打印 200。

`return EXIT_SUCCESS` 从 main 返回，结束子进程，因此子进程不会继续执行下面的父进程逻辑。预期：子进程的两次打印分别看到 100 和 200。

</div>
</div>

<div class="cpu-row" id="q1-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L27–L35</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">27
28
29
30
31
32
33
34
35</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-5" aria-label="此步骤的永久链接">#</a> 父进程等到实验对象结束

`status` 不是 PID，而是接收退出信息的整数。`&status` 把它的地址交给 waitpid，让函数写回结果。

`waitpid(pid, &status, 0)` 等待指定子进程。若返回 -1 且 errno 是 EINTR，`continue` 让循环再等一次；其他失败则报错退出。这个等待只控制后续打印的先后。

</div>
</div>

<div class="cpu-row" id="q1-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L36–L40</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">36
37
38
39
40</pre>
<div class="cpu-code" markdown="1">

```c
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Child did not exit successfully\n");
        return EXIT_FAILURE;
    }

```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-6" aria-label="此步骤的永久链接">#</a> 先判断退出方式，再取退出码

`WIFEXITED(status)` 判断是否正常退出；成立后才能用 `WEXITSTATUS(status)` 解释退出码。它们是处理状态的宏，不是文件描述符。

条件读作：“不是正常退出，**或者**退出码不是成功值”。`||` 短路，所以第一项已成立时不会再提取正常退出码。

</div>
</div>

<div class="cpu-row" id="q1-step-7">
<div class="cpu-source" markdown="1">

<span class="cpu-location">fork_variable.c · L41–L45</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">41
42
43
44
45</pre>
<div class="cpu-code" markdown="1">

```c
    printf("Parent: after the child changed x, x = %d\n", x);
    x = 300;
    printf("Parent: after changing x, x = %d\n", x);
    return EXIT_SUCCESS;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q1-step-7" aria-label="此步骤的永久链接">#</a> 回到父进程，检查变量隔离

父进程此时看到的 x 仍为 100，尽管子进程已经把自己的 x 改成 200。随后父进程把自己的 x 改成 300。

**观察对象是数值是否互相影响，等待只是让输出易读。**最后返回成功状态。

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


<span id="lesson-2"></span>

## 作业 2：fork 后共享打开的文件

先打开文件，再创建子进程，让父子各写入 1000 条记录。

[下载 q2_file.c]({{ '/assets/code/ostep-cpu-api/q2_file.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q2-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L1–L5</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5</pre>
<div class="cpu-code" markdown="1">

```c
#include <fcntl.h>
#include "common.h"

int main(void)
{
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-1" aria-label="此步骤的永久链接">#</a> 先认识文件接口与公共封装

`fcntl.h` 提供 open 的选项，`common.h` 提供本文附录中的辅助函数。`spawn`、`write_all`、`reap` 是本作业自定义名称，不能当成系统自带 API。

</div>
</div>

<div class="cpu-row" id="q2-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L6–L8</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">6
7
8</pre>
<div class="cpu-code" markdown="1">

```c
    int fd = open("q2_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fail("open");
    printf("Before fork: fd = %d\n", fd);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-2" aria-label="此步骤的永久链接">#</a> 只打开一次，且在 fork 之前打开

open 的第一个参数是文件名；三个 `O_*` 标志通过按位或 `|` 组合：只写、不存在则创建、存在则清空。0644 是八进制创建权限，还要受 umask 限制。

成功得到非负整数 fd，失败得到 -1。打印出的 fd 常为 3，但不要依赖这个数。**此时只打开一次文件，随后才创建子进程。**

</div>
</div>

<div class="cpu-row" id="q2-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L9–L11</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">9
10
11</pre>
<div class="cpu-code" markdown="1">

```c
    pid_t pid = spawn();
    const char *who = pid == 0 ? "child" : "parent";
    /* Each record is submitted with one write in the normal case. */
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-3" aria-label="此步骤的永久链接">#</a> 变量副本不同，底层打开状态共享

spawn 之后，父子各有自己的 fd 变量和描述符表项，它们却引用同一个内核打开文件描述。文件偏移量存在共享的打开状态中。

`pid == 0 ? "child" : "parent"` 让 who 指向当前身份文字；`const char *` 表示通过这个指针只读字符。注释说正常情况下一行提交一次 write。

</div>
</div>

<div class="cpu-row" id="q2-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L12–L16</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">12
13
14
15
16</pre>
<div class="cpu-code" markdown="1">

```c
    for (int i = 0; i < 1000; ++i) {
        char line[64];
        int len = snprintf(line, sizeof(line), "%s %04d\n", who, i);
        write_all(fd, line, (size_t)len);
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-4" aria-label="此步骤的永久链接">#</a> 先在内存里拼一行，再写到文件

for 的三个部分是：i 从 0 开始、i 小于 1000 才继续、每轮结束加 1。父子各有自己的循环。

`line[64]` 是临时数组，snprintf 根据 `sizeof(line)` 限制容量。`%s` 放身份，`%04d` 将序号补零到四位。比如 `parent 0007\n`。len 不包括字符串结束符，本题各行都能放下。

write_all 才真正写入 fd；`(size_t)len` 转成大小类型。每轮写一个序号，共 1000 行。

</div>
</div>

<div class="cpu-row" id="q2-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L17–L21</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">17
18
19
20
21</pre>
<div class="cpu-code" markdown="1">

```c
    if (close(fd) < 0) fail("close");
    if (pid != 0) {
        reap(pid);
        puts("Both processes wrote 1000 records to q2_output.txt");
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-5" aria-label="此步骤的永久链接">#</a> 各自关闭引用，父进程最后报告完成

close 只关闭当前进程的描述符，不会使另一个进程的 fd 失效。父进程写完后进入 `pid != 0`，回收子进程，才打印完成。

等待发生在写入之后，因此没有把两方的写入变成串行。记录先后由调度决定。

</div>
</div>

<div class="cpu-row" id="q2-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q2_file.c · L22–L23</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">22
23</pre>
<div class="cpu-code" markdown="1">

```c
    return 0;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q2-step-6" aria-label="此步骤的永久链接">#</a> 双方独立结束

父子分别从自己的 main 返回 0。每方写 1000 行，正常结果是文件中有 2000 行，而不是某一方覆盖另一方。

</div>
</div>

</div>

**结论：**描述符表各自独立，但这里指向同一个打开文件描述，因此共享文件偏移量。当前 Linux 对普通文件的这种共享偏移更新是原子的，正常完成的写入接着共享位置继续写，不会因 fork 各从偏移 0 覆盖。

父子记录可能交错，也可能连续出现很多同一方记录；每一方内部仍按 0000～0999 写入。多次 write 组成的逻辑记录不因此具有整体原子性；短写后补写也可能被其他写入穿插。


<span id="lesson-3"></span>

## 作业 3：不用 wait，保证子进程先输出

子进程输出 hello 并刷新后，通过管道通知父进程；父进程收到通知才输出 goodbye。

[下载 q3_order.c]({{ '/assets/code/ostep-cpu-api/q3_order.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q3-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L1–L4</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4</pre>
<div class="cpu-code" markdown="1">

```c
#include "common.h"

int main(void)
{
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-1" aria-label="此步骤的永久链接">#</a> 把本题看作一次通知

这里的公共封装和程序入口与前题相同。本题要等待的是“hello 已经输出”这个事件，不要求父进程通过 wait 等到子进程退出。

</div>
</div>

<div class="cpu-row" id="q3-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L5–L7</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">5
6
7</pre>
<div class="cpu-code" markdown="1">

```c
    int ready[2];
    if (pipe(ready) < 0) fail("pipe");
    pid_t pid = spawn();
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-2" aria-label="此步骤的永久链接">#</a> 先建立双方共用的通道

ready 是容纳两个整数的数组，pipe 把读端描述符填入 `[0]`，写端填入 `[1]`。下标不等于描述符编号。

先 pipe 后 spawn，让父子各持有两端引用，但内核中只有这一条管道。之后必须由各方关闭不需要的一端。

</div>
</div>

<div class="cpu-row" id="q3-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L8–L10</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">8
9
10</pre>
<div class="cpu-code" markdown="1">

```c
    if (pid == 0) {
        close(ready[0]);
        if (puts("hello") == EOF || fflush(stdout) == EOF) fail("stdout");
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-3" aria-label="此步骤的永久链接">#</a> 子进程先完成输出

子进程关闭读端，因为它只发送。puts 向标准输出写 hello 并自动换行；fflush 把可能缓冲的文字真正交给底层。任一失败就报错。

**通知必须晚于刷新。**否则在重定向到文件时，父进程可能收到通知并输出 goodbye，而 hello 还停留在子进程缓冲中。

</div>
</div>

<div class="cpu-row" id="q3-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L11–L15</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">11
12
13
14
15</pre>
<div class="cpu-code" markdown="1">

```c
        /* Signal only after hello has actually left the stdio buffer. */
        write_all(ready[1], "!", 1);
        close(ready[1]);
        return 0;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-4" aria-label="此步骤的永久链接">#</a> 用一个字节表示“完成”

向 ready[1] 写入 `"!"` 的第一个字节。写到的是管道，所以终端不会显示这个感叹号；接收方只关心有一个字节到达。

发送后关闭写端并返回。return 使子进程结束，后面只剩父进程继续。

</div>
</div>

<div class="cpu-row" id="q3-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L16–L21</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">16
17
18
19
20
21</pre>
<div class="cpu-code" markdown="1">

```c
    close(ready[1]);
    char token;
    ssize_t n;
    do {
        n = read(ready[0], &token, 1);
    } while (n < 0 && errno == EINTR);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-5" aria-label="此步骤的永久链接">#</a> 父进程阻塞在接收点

父进程关闭自己的写端，但子进程的写端还在。token 保存字符，ssize_t 类型的 n 可保存字节数或 -1。

read 从 ready[0] 读最多一个字节到 `&token`。管道暂时为空、但还有写端时，调用会阻塞。即使父进程先运行到这里，也得等子进程发来通知。EINTR 表示被信号中断，可重试。

</div>
</div>

<div class="cpu-row" id="q3-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L22–L26</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">22
23
24
25
26</pre>
<div class="cpu-code" markdown="1">

```c
    if (n < 0) fail("read");
    if (n != 1) {
        fprintf(stderr, "Child did not signal readiness\n");
        return 1;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-6" aria-label="此步骤的永久链接">#</a> 把“没收到”与“读取错误”分开

n 小于 0 是系统调用错误，交给 fail。n 不等于 1 则没有收到约定字节：这里主要是 n 为 0，即所有写端关闭且无剩余数据。

fprintf 指定向 stderr 打印错误，随后返回 1；不能在没有收到通知时继续假定 hello 已输出。

</div>
</div>

<div class="cpu-row" id="q3-step-7">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q3_order.c · L27–L31</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">27
28
29
30
31</pre>
<div class="cpu-code" markdown="1">

```c
    close(ready[0]);
    puts("goodbye");
    /* No wait() or waitpid(): this short-lived parent exits immediately. */
    return 0;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q3-step-7" aria-label="此步骤的永久链接">#</a> 接到通知，才允许输出 goodbye

收到字节后关闭读端，再 puts goodbye。这里的前后关系是由通信建立的，不取决于父子谁先获得 CPU。

父进程直接成功退出，没有调用 wait 或 waitpid。接到通知时，子进程可能还没执行完自己的最后几条语句。

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


<span id="lesson-4"></span>

## 作业 4：exec 的六种变体

fork 创建进程；exec 将当前进程的程序映像替换成另一个程序，PID 不变。本题每轮在子进程中运行 ls。

[下载 q4_exec.c]({{ '/assets/code/ostep-cpu-api/q4_exec.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q4-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L1–L5</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5</pre>
<div class="cpu-code" markdown="1">

```c
#define _GNU_SOURCE
#include "common.h"

int main(void)
{
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-1" aria-label="此步骤的永久链接">#</a> 先声明本例使用 GNU 扩展

`_GNU_SOURCE` 是编译时的功能开关，要在所有头文件之前定义，供 execvpe 的声明使用。它不负责创建进程，也不控制输入输出。

</div>
</div>

<div class="cpu-row" id="q4-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L6–L8</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">6
7
8</pre>
<div class="cpu-code" markdown="1">

```c
    const char *names[] = {"execl", "execle", "execlp", "execv", "execvp", "execvpe"};
    char *args[] = {"ls", "-1", "/bin/ls", NULL};
    char *env[] = {"PATH=/bin:/usr/bin", "LANG=C", NULL};
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-2" aria-label="此步骤的永久链接">#</a> 准备名称、命令参数和新环境

names 是提示文字数组，不会通过这些字符串调用函数。args 对应命令 `ls -1 /bin/ls`：argv[0] 为 ls，`-1` 是数字 1，后面的路径是要列出的文件。NULL 表示数组结束。

env 是另一组字符串，给带 e 的调用设置新程序环境。PATH 决定搜索目录，LANG=C 选择语言环境。**把配置写进数组，不等于修改当前进程环境。**

</div>
</div>

<div class="cpu-row" id="q4-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L9–L10</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">9
10</pre>
<div class="cpu-code" markdown="1">

```c
    /* The p variants search the caller's PATH, including execvpe(). */
    if (setenv("PATH", "/bin:/usr/bin", 1) < 0) fail("setenv");
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-3" aria-label="此步骤的永久链接">#</a> 让带 p 的调用能找到 ls

setenv 真正把调用者环境中的 PATH 改为 `/bin:/usr/bin`；末尾的 1 表示允许覆盖旧值。之后创建的子进程继承它。

execvpe 的查找也使用调用者 PATH，而不是其 env 参数中的 PATH。两个动作分别是“到哪里找程序”和“新程序拿到什么环境”。

</div>
</div>

<div class="cpu-row" id="q4-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L11–L15</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">11
12
13
14
15</pre>
<div class="cpu-code" markdown="1">

```c
    for (int i = 0; i < 6; ++i) {
        printf("=== %s ===\n", names[i]);
        pid_t pid = spawn();
        if (pid == 0) {
            switch (i) {
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-4" aria-label="此步骤的永久链接">#</a> 每轮只实验一种替换方式

循环 i=0～5，先打印名称，再创建一个子进程。子进程根据 switch(i) 选择匹配的 case。父进程不会进入 switch，它在后面等待。

</div>
</div>

<div class="cpu-row" id="q4-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L16–L16</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">16</pre>
<div class="cpu-code" markdown="1">

```c
            case 0: execl("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-5" aria-label="此步骤的永久链接">#</a> execl：路径明确，参数逐项写

第一个 `/bin/ls` 指定可执行文件；从第二个参数起才是新程序收到的参数。末尾字符空指针标记参数结束。

l 表示 list；不带 e，就继承环境。后一个 `/bin/ls` 是被列出的文件，不是又执行一次 ls。

</div>
</div>

<div class="cpu-row" id="q4-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L17–L17</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">17</pre>
<div class="cpu-code" markdown="1">

```c
            case 1: execle("/bin/ls", "ls", "-1", "/bin/ls", (char *)NULL, env); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-6" aria-label="此步骤的永久链接">#</a> execle：逐项参数，显式环境

与 execl 的参数形式相同，但在参数结束标记之后，再传 env。e 表示显式提供环境，替换后的程序使用这组环境字符串。

</div>
</div>

<div class="cpu-row" id="q4-step-7">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L18–L18</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">18</pre>
<div class="cpu-code" markdown="1">

```c
            case 2: execlp("ls", "ls", "-1", "/bin/ls", (char *)NULL); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-7" aria-label="此步骤的永久链接">#</a> execlp：用名字搜索程序

可执行文件只写 ls，p 表示根据 PATH 搜索；传给新程序的参数仍逐项列出，环境仍继承。

</div>
</div>

<div class="cpu-row" id="q4-step-8">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L19–L19</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">19</pre>
<div class="cpu-code" markdown="1">

```c
            case 3: execv("/bin/ls", args); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-8" aria-label="此步骤的永久链接">#</a> execv：参数来自数组

指定可执行文件路径，args 数组替代前面逐项列出的参数。v 表示 vector，数组末尾的 NULL 标记结束。

</div>
</div>

<div class="cpu-row" id="q4-step-9">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L20–L20</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">20</pre>
<div class="cpu-code" markdown="1">

```c
            case 4: execvp("ls", args); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-9" aria-label="此步骤的永久链接">#</a> execvp：参数数组，加 PATH 搜索

v 负责参数数组，p 负责根据 PATH 查找 ls。这是两件独立的事，不要把 args 中的文件参数误当成查找目录。

</div>
</div>

<div class="cpu-row" id="q4-step-10">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L21–L21</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">21</pre>
<div class="cpu-code" markdown="1">

```c
            case 5: execvpe("ls", args, env); break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-10" aria-label="此步骤的永久链接">#</a> execvpe：再加上显式环境

args 提供参数，调用者 PATH 找到 ls，env 提供新程序环境。它在本例中的目标行为仍与前面五种相同。

</div>
</div>

<div class="cpu-row" id="q4-step-11">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L22–L25</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">22
23
24
25</pre>
<div class="cpu-code" markdown="1">

```c
            }
            /* A successful exec never returns to this code. */
            fail(names[i]);
        }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-11" aria-label="此步骤的永久链接">#</a> 只有替换失败，才走到 fail

exec 成功会把当前子进程变成运行 ls 的进程，不会返回旧代码。ls 结束时该进程结束，PID 在替换时不变。

只有失败返回 -1 时，才会执行 case 后的 break 离开 switch，再调用 fail 打印错误。

</div>
</div>

<div class="cpu-row" id="q4-step-12">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q4_exec.c · L26–L29</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">26
27
28
29</pre>
<div class="cpu-code" markdown="1">

```c
        reap(pid);
    }
    return 0;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q4-step-12" aria-label="此步骤的永久链接">#</a> 一轮结束，再试下一种

父进程用 reap 回收本轮子进程，再开始下一轮。六次输出因此不会彼此混杂。最后父进程返回 0。

</div>
</div>

</div>

记忆方式：l = list（逐项参数），v = vector（参数数组），p = PATH 搜索，e = 显式环境。六种方式都执行 `ls -1 /bin/ls`，每种都会输出一行 `/bin/ls`，前面有对应函数名的提示。


<span id="lesson-5"></span>

## 作业 5：wait 的返回值与等待范围

子进程没有自己的子进程，调用 wait 会失败；父进程则能等待并回收它。

[下载 q5_wait.c]({{ '/assets/code/ostep-cpu-api/q5_wait.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q5-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L1–L7</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6
7</pre>
<div class="cpu-code" markdown="1">

```c
#include <string.h>
#include "common.h"

int main(void)
{
    pid_t pid = spawn();
    if (pid == 0) {
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-1" aria-label="此步骤的永久链接">#</a> 先造出没有子进程的子进程

string.h 让程序能用 strerror 把错误编号转成文字。spawn 后子进程进入分支；它自己没有再 fork，所以没有孩子可以等。

</div>
</div>

<div class="cpu-row" id="q5-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L8–L11</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">8
9
10
11</pre>
<div class="cpu-code" markdown="1">

```c
        int status;
        errno = 0;
        pid_t result = wait(&status);
        int saved_errno = errno;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-2" aria-label="此步骤的永久链接">#</a> wait 的等待范围是自己的孩子

status 准备接收退出信息；先把 errno 清零，再调用 wait。当前子进程没有自己的子进程，立即失败，返回 -1，errno 为 ECHILD。

saved_errno 立即保存此次错误，避免后续调用影响它。这里 wait 失败后没有可用的退出状态，代码也没有读取 status。

</div>
</div>

<div class="cpu-row" id="q5-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L12–L16</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">12
13
14
15
16</pre>
<div class="cpu-code" markdown="1">

```c
        printf("Child: wait() = %ld, errno = %d (%s)\n",
               (long)result, saved_errno, strerror(saved_errno));
        if (result != -1 || saved_errno != ECHILD) return 1;
        return 42;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-3" aria-label="此步骤的永久链接">#</a> 把预期失败当作实验结果

打印 result、错误编号和 strerror 的文字。`(long)` 对应 `%ld`。如果不是 -1 和 ECHILD 的组合，实验不符合预期，返回 1。

符合预期就返回 42，供父进程验证。42 是人为选择的退出码，与 PID 无关。

</div>
</div>

<div class="cpu-row" id="q5-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L17–L22</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">17
18
19
20
21
22</pre>
<div class="cpu-code" markdown="1">

```c
    int status;
    pid_t result;
    do {
        result = wait(&status);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("wait");
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-4" aria-label="此步骤的永久链接">#</a> 父进程可以正常回收它

父进程拥有刚创建的子进程，所以 wait 有对象可等。若子进程已经退出，就直接收集保留的退出信息；EINTR 则重试。其余错误报错。

</div>
</div>

<div class="cpu-row" id="q5-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L23–L25</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">23
24
25</pre>
<div class="cpu-code" markdown="1">

```c
    printf("Parent: child PID = %ld, wait() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-5" aria-label="此步骤的永久链接">#</a> 返回值与状态输出是两条通道

result 应等于创建时得到的子进程 PID。status 则包含退出方式与退出码；先用 WIFEXITED 检查是否正常退出，失败就返回 1。

**wait 的直接返回值不是 42。**

</div>
</div>

<div class="cpu-row" id="q5-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q5_wait.c · L26–L28</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">26
27
28</pre>
<div class="cpu-code" markdown="1">

```c
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q5-step-6" aria-label="此步骤的永久链接">#</a> 从 status 中取出 42

在正常退出的前提下，WEXITSTATUS 提取 42。最后验证 PID 和退出码都正确，返回 0；任一不对则返回 1。

</div>
</div>

</div>

**wait 返回的是 PID，不是退出码。**退出信息写入 status，要先判断正常退出，再提取退出码。子进程当然也能调用 wait，但只能等待自己的子进程，不能用它等待父进程。


<span id="lesson-6"></span>

## 作业 6：waitpid 可以指定等待谁

与上一题对照，父进程使用 waitpid 明确指定等待的子进程。

[下载 q6_waitpid.c]({{ '/assets/code/ostep-cpu-api/q6_waitpid.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q6-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q6_waitpid.c · L1–L9</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6
7
8
9</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q6-step-1" aria-label="此步骤的永久链接">#</a> 准备一个明确的等待目标

创建子进程，打印提示并以 42 退出。父进程保留 spawn 返回的子进程 PID，跳过分支。

</div>
</div>

<div class="cpu-row" id="q6-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q6_waitpid.c · L10–L15</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">10
11
12
13
14
15</pre>
<div class="cpu-code" markdown="1">

```c
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) fail("waitpid");
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q6-step-2" aria-label="此步骤的永久链接">#</a> 把目标 PID 交给 waitpid

三个参数读作：等待 pid、把状态写到 status、采用默认阻塞选项 0。被信号中断就重试，其余等待错误退出。

与 wait 的区别在第一个参数：本次明确指定哪一个孩子，而不是任意一个。

</div>
</div>

<div class="cpu-row" id="q6-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q6_waitpid.c · L16–L21</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">16
17
18
19
20
21</pre>
<div class="cpu-code" markdown="1">

```c
    printf("Parent: requested PID = %ld, waitpid() returned PID = %ld\n",
           (long)pid, (long)result);
    if (!WIFEXITED(status)) return 1;
    printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return result == pid && WEXITSTATUS(status) == 42 ? 0 : 1;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q6-step-3" aria-label="此步骤的永久链接">#</a> 仍然先判断退出方式，再取退出码

打印请求 PID 与返回 PID，验证正常退出，再取出 42。最后的条件表达式把整个实验结果转换成父进程自己的退出码 0 或 1。

这里 42 是子进程的状态，0 是父进程表示实验成功的状态。

</div>
</div>

</div>

`wait(&status)` 相当于 `waitpid(-1, &status, 0)`，可回收任意一个可等待的子进程；`waitpid(pid, ...)` 可指定某个子进程。

若把选项改为 `WNOHANG`，目标还在运行且没有可报告状态时返回 0，父进程可以继续工作；这是额外用法，本题源码仍采用阻塞等待。


<span id="lesson-7"></span>

## 作业 7：关闭标准输出后调用 printf

关闭子进程的标准输出描述符，再观察 printf 的返回值和错误。

[下载 q7_stdout.c]({{ '/assets/code/ostep-cpu-api/q7_stdout.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q7-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q7_stdout.c · L1–L7</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6
7</pre>
<div class="cpu-code" markdown="1">

```c
#include <string.h>
#include "common.h"

int main(void)
{
    /* Unbuffered stdout exposes the error directly in printf(). */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return 1;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q7-step-1" aria-label="此步骤的永久链接">#</a> 去掉缓冲，让错误立即可见

setvbuf 设置的是 stdout 这个 FILE 流：NULL 表示不提供自定义缓冲，`_IONBF` 表示无缓冲，最后的容量参数在这种模式下不需要。

这样 printf 会直接尝试底层写入，而不只把字符暂存内存。设置失败则返回 1。

</div>
</div>

<div class="cpu-row" id="q7-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q7_stdout.c · L8–L13</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">8
9
10
11
12
13</pre>
<div class="cpu-code" markdown="1">

```c
    pid_t pid = spawn();
    if (pid == 0) {
        if (close(STDOUT_FILENO) < 0) fail("close");
        errno = 0;
        int result = printf("Child: this text cannot reach stdout\n");
        int saved_errno = errno;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q7-step-2" aria-label="此步骤的永久链接">#</a> 只关闭子进程的描述符 1

spawn 后仅子进程调用 close(STDOUT_FILENO)。宏展开的值是 1，但其含义是标准输出这个编号。stdout 流对象仍在，底层描述符却已关闭。

接着 printf 尝试写入并失败：返回负数，errno 为 EBADF。立即保存 errno，供后面报告。文字不会到达标准输出。

</div>
</div>

<div class="cpu-row" id="q7-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q7_stdout.c · L14–L17</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">14
15
16
17</pre>
<div class="cpu-code" markdown="1">

```c
        fprintf(stderr, "Child: printf() = %d, ferror(stdout) = %d, errno = %d (%s)\n",
                result, ferror(stdout), saved_errno, strerror(saved_errno));
        return result < 0 && saved_errno == EBADF ? 0 : 1;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q7-step-3" aria-label="此步骤的永久链接">#</a> 用另一条通道报告失败

stderr 仍有效，通常对应描述符 2，所以可以 fprintf 报告刚才的失败。ferror(stdout) 非零表示流错误标志已设置。

预期本来就是输出失败，因此 `result < 0 && saved_errno == EBADF` 成立时返回 0，表示实验成功，不是打印成功。

</div>
</div>

<div class="cpu-row" id="q7-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q7_stdout.c · L18–L21</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">18
19
20
21</pre>
<div class="cpu-code" markdown="1">

```c
    reap(pid);
    puts("Parent: stdout still works");
    return 0;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q7-step-4" aria-label="此步骤的永久链接">#</a> 父进程的标准输出仍有效

父进程回收子进程，再 puts 提示。子进程的 close 只删掉自己的引用，父进程仍拥有独立的描述符表项 1。正常打印验证了这一点。

</div>
</div>

</div>

典型结果：

```text
Child: printf() = -1, ferror(stdout) = 1, errno = 9 (Bad file descriptor)
Parent: stdout still works
```

如果保留默认缓冲，printf 可能先把文字放进内存，看似成功；错误到 fflush 或退出刷新时才暴露。流错误标志的非零数值和 errno 数字不应当作跨系统固定值。


<span id="lesson-8"></span>

## 作业 8：用管道连接两个子进程

第一个子进程产生文本，第二个子进程读取并输出。父进程负责创建、关闭多余端点和回收。

[下载 q8_pipe.c]({{ '/assets/code/ostep-cpu-api/q8_pipe.c' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q8-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L1–L6</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6</pre>
<div class="cpu-code" markdown="1">

```c
#include "common.h"

int main(void)
{
    int channel[2];
    if (pipe(channel) < 0) fail("pipe");
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-1" aria-label="此步骤的永久链接">#</a> 建立一条传文本的管道

channel[0] 保存读端描述符，channel[1] 保存写端。与第 3 题相同，这次传递的不是完成通知，而是完整文本。

</div>
</div>

<div class="cpu-row" id="q8-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L7–L9</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">7
8
9</pre>
<div class="cpu-code" markdown="1">

```c
    pid_t producer = spawn();
    if (producer == 0) {
        close(channel[0]);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-2" aria-label="此步骤的永久链接">#</a> 第一个孩子只负责产生文本

父进程创建 producer。生产者进入分支，关闭不使用的读端；它将把文字送到另一头的消费者。

</div>
</div>

<div class="cpu-row" id="q8-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L10–L11</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">10
11</pre>
<div class="cpu-code" markdown="1">

```c
        if (dup2(channel[1], STDOUT_FILENO) < 0) fail("dup2");
        if (channel[1] != STDOUT_FILENO) close(channel[1]);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-3" aria-label="此步骤的永久链接">#</a> 把标准输出 1 改接到管道

dup2 的顺序是 **原描述符、目标描述符**。`dup2(channel[1], STDOUT_FILENO)` 让编号 1 引用管道写端，替换原来的目标。它不复制文字。

成功后，通常有 channel[1] 和 1 两个编号指向同一写端。关闭原编号，只留标准输出 1；如果原编号已经是 1，便不能关闭，因此先比较。

</div>
</div>

<div class="cpu-row" id="q8-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L12–L15</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">12
13
14
15</pre>
<div class="cpu-code" markdown="1">

```c
        printf("hello from child 1\nthis is a pipe\n");
        if (fflush(stdout) == EOF) fail("fflush");
        return 0;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-4" aria-label="此步骤的永久链接">#</a> 保持 printf 写法，改变它的目的地

printf 仍然写 stdout，但描述符 1 已接到管道，所以两行文字进入管道。fflush 确保内容离开用户态缓冲。

生产者返回时，系统关闭它剩余的描述符，包括写端的引用 1。它不能替父进程关闭另一份写端。

</div>
</div>

<div class="cpu-row" id="q8-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L16–L20</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">16
17
18
19
20</pre>
<div class="cpu-code" markdown="1">

```c
    pid_t consumer = spawn();
    if (consumer == 0) {
        close(channel[1]);
        if (dup2(channel[0], STDIN_FILENO) < 0) fail("dup2");
        if (channel[0] != STDIN_FILENO) close(channel[0]);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-5" aria-label="此步骤的永久链接">#</a> 第二个孩子把标准输入接到读端

只有父进程会执行到第二次 spawn；生产者已在自己的分支返回。消费者关闭写端，将描述符 0 改接到管道读端，再关闭多余的原始读端。

消费者由父进程创建，因此继承父进程的标准输出，不会继承兄弟进程修改后的输出。它可以正常把读到的文字显示出来。

</div>
</div>

<div class="cpu-row" id="q8-step-6">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L21–L26</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">21
22
23
24
25
26</pre>
<div class="cpu-code" markdown="1">

```c
        char buffer[256];
        for (;;) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) fail("read");
            if (n == 0) break;
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-6" aria-label="此步骤的永久链接">#</a> 循环读到真正的末尾

buffer 保存最多 256 字节，for(;;) 持续循环。read 从描述符 0 读取，数组名在这里转换成首元素指针。n 是实际读到的字节数。

EINTR 时 continue 重试；其他负数报错；n 为 0 时 break。这个 0 是 read 的 EOF 返回值，不是 stdin 的编号，也不是 EOF 宏。

</div>
</div>

<div class="cpu-row" id="q8-step-7">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L27–L30</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">27
28
29
30</pre>
<div class="cpu-code" markdown="1">

```c
            write_all(STDOUT_FILENO, buffer, (size_t)n);
        }
        return 0;
    }
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-7" aria-label="此步骤的永久链接">#</a> 只输出这次真正读到的字节

向消费者的描述符 1 写 n 个字节；没有重定向消费者的输出，因此它仍到终端或外部设定的输出。

不能把整个 256 字节数组都写出，也不能把它直接当以零结尾的字符串：read 不会自动添加结束符。读完后消费者返回。

</div>
</div>

<div class="cpu-row" id="q8-step-8">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L31–L34</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">31
32
33
34</pre>
<div class="cpu-code" markdown="1">

```c
    /* Without closing the parent's write end, the consumer cannot see EOF. */
    close(channel[0]);
    close(channel[1]);
    reap(producer);
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-8" aria-label="此步骤的永久链接">#</a> 父进程不传数据，应交还两端引用

父进程关闭自己的读端和写端，再等待生产者。**尤其要关闭写端**，否则生产者结束后仍有写端引用，消费者读空后会一直等。

</div>
</div>

<div class="cpu-row" id="q8-step-9">
<div class="cpu-source" markdown="1">

<span class="cpu-location">q8_pipe.c · L35–L38</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">35
36
37
38</pre>
<div class="cpu-code" markdown="1">

```c
    reap(consumer);
    puts("Parent: both children finished");
    return 0;
}
```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q8-step-9" aria-label="此步骤的永久链接">#</a> 回收两个孩子，再输出完成提示

接着回收消费者，最后打印两者完成。父进程等待某个孩子时，另一个孩子仍可运行，不会跟着暂停。

因此整个通道可以一边生产一边消费，直到所有写端关闭并读空。

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


<span id="helpers"></span>

## 附录：公共辅助函数

第 2～8 题使用下面的完整公共头文件。它把重复的错误处理、创建进程、写入和回收操作封装起来。

[下载 common.h]({{ '/assets/code/ostep-cpu-api/common.h' | relative_url }})

<div class="cpu-compare">

<div class="cpu-row" id="q9-step-1">
<div class="cpu-source" markdown="1">

<span class="cpu-location">common.h · L1–L9</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">1
2
3
4
5
6
7
8
9</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q9-step-1" aria-label="此步骤的永久链接">#</a> 用头文件保护避免重复定义

ifndef 检查保护宏是否未定义；define 标记已经包含过，文末 endif 结束条件。HOMEWORK_COMMON_H 是自定义保护名，不是系统错误常量。

各 include 引入错误、输入输出、退出、类型、等待和基础系统调用功能。

</div>
</div>

<div class="cpu-row" id="q9-step-2">
<div class="cpu-source" markdown="1">

<span class="cpu-location">common.h · L10–L15</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">10
11
12
13
14
15</pre>
<div class="cpu-code" markdown="1">

```c
static inline void fail(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

```

</div>
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q9-step-2" aria-label="此步骤的永久链接">#</a> fail：报告原因，然后结束当前进程

what 指向操作名称，perror 输出名称和 errno 对应说明；exit(EXIT_FAILURE) 失败退出，影响的是调用它的那个进程。

void 表示不返回值。static inline 适合把这种小函数写进头文件：内部链接，允许编译器选择内联，不保证实际展开。

</div>
</div>

<div class="cpu-row" id="q9-step-3">
<div class="cpu-source" markdown="1">

<span class="cpu-location">common.h · L16–L23</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">16
17
18
19
20
21
22
23</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q9-step-3" aria-label="此步骤的永久链接">#</a> spawn：把重复的 fork 检查集中起来

fflush(NULL) 刷新所有打开的输出流；fork 后检查负返回，失败调用 fail，成功把 pid 返回给各自的调用者。

这里 NULL 表示 fflush 的特殊“全部输出流”参数；它不是文件描述符。此教学封装没有检查 fflush 的返回值。

</div>
</div>

<div class="cpu-row" id="q9-step-4">
<div class="cpu-source" markdown="1">

<span class="cpu-location">common.h · L24–L34</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">24
25
26
27
28
29
30
31
32
33
34</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q9-step-4" aria-label="此步骤的永久链接">#</a> write_all：从短写处继续，而非重写整段

只要 size 大于 0，就 write 当前 data 指针处的数据。EINTR 重试，其他失败或零进展报错。

写成功 n 字节后，`data += n` 移动指针，`size -= n` 减少剩余长度，下轮只补剩余内容。它确保写全，但不会让多次 write 成为一次整体原子操作。

</div>
</div>

<div class="cpu-row" id="q9-step-5">
<div class="cpu-source" markdown="1">

<span class="cpu-location">common.h · L35–L46</span>

<div class="cpu-listing">
<pre class="cpu-lines" aria-hidden="true">35
36
37
38
39
40
41
42
43
44
45
46</pre>
<div class="cpu-code" markdown="1">

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
</div>
</div>
<div class="cpu-explain" markdown="1">

### <a class="cpu-permalink" href="#q9-step-5" aria-label="此步骤的永久链接">#</a> reap：等待并要求成功退出

waitpid 被 EINTR 打断时继续等，其他错误立即报告。结束后要求正常退出且退出码为 0，否则当前进程失败退出。

这也是第 5、6 题不用 reap 的原因：它们的子进程故意返回 42，应自己解释状态。最后 endif 结束头文件保护。

</div>
</div>

</div>

## 本次实验的范围

源码保持实验原样，适合学习正常运行路径。某些关闭和输出调用未逐一检查，部分创建失败后的资源清理也作了简化；它们不是完整的生产环境进程管理框架。附录明确列出这些辅助函数，避免把自定义封装误认为 C 或 POSIX 自带接口。

## 参考与阅读顺序

先用开头的分类表辨认名称，再按第 1～8 题顺序看代码；遇到 spawn、write_all、reap，回到公共函数附录看它究竟封装了什么。

- 讲解结构参考：[labml：Multi-Headed Attention](https://nn.labml.ai/transformers/mha.html)。本文仅借鉴注释式组织方式，未复用其模型讲解正文。
- 文件接口：[Linux open(2)](https://man7.org/linux/man-pages/man2/open.2.html)、[pipe(2)](https://man7.org/linux/man-pages/man2/pipe.2.html)、[dup(2)/dup2](https://man7.org/linux/man-pages/man2/dup.2.html)。
- 标准流与读取：[stdin(3)](https://man7.org/linux/man-pages/man3/stdin.3.html)、[read(2)](https://man7.org/linux/man-pages/man2/read.2.html)、[pipe(7)](https://man7.org/linux/man-pages/man7/pipe.7.html)、[setvbuf(3)](https://man7.org/linux/man-pages/man3/setbuf.3.html)。

<!-- obsidian-backlinks:start -->
## 引用本文的笔记

- [OSTEP LDE：逐段读懂系统调用与上下文切换测量]({% post_url 2026-10-30-ostep-lde %})：用两条管道测量同一 CPU 上两个进程的交接，并解释局部变量如何传值。
<!-- obsidian-backlinks:end -->
