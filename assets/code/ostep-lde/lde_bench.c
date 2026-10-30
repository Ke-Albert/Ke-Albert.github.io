#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct options {
    uint64_t iterations, syscalls, warmup;
    size_t timer_samples;
    unsigned trials;
    int cpu;
};

struct switches { long voluntary, involuntary; int cpu; };

static void die(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

static int64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0) die("clock_gettime");
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + ts.tv_nsec;
}

static int64_t wall_ns(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) die("gettimeofday");
    return (int64_t)tv.tv_sec * INT64_C(1000000000) + tv.tv_usec * INT64_C(1000);
}

static struct rusage usage(void)
{
    struct rusage value;
    if (getrusage(RUSAGE_SELF, &value) < 0) die("getrusage");
    return value;
}

static struct switches difference(struct rusage before, struct rusage after)
{
    struct switches result = {after.ru_nvcsw - before.ru_nvcsw,
                              after.ru_nivcsw - before.ru_nivcsw, sched_getcpu()};
    if (result.cpu < 0) die("sched_getcpu");
    return result;
}

/* A closed peer is an error, not a successful zero-byte notification. */
static void transfer(int fd, void *buffer, size_t bytes, int writing)
{
    char *position = buffer;
    while (bytes != 0) {
        ssize_t n = writing ? write(fd, position, bytes) : read(fd, position, bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) die(writing ? "pipe write" : "pipe read");
        if (n == 0) {
            fprintf(stderr, "Unexpected pipe EOF/zero progress\n");
            exit(EXIT_FAILURE);
        }
        position += n;
        bytes -= (size_t)n;
    }
}

static void send_token(int fd)
{
    char token = '!';
    transfer(fd, &token, 1, 1);
}

static void receive_token(int fd)
{
    char token;
    transfer(fd, &token, 1, 0);
    if (token != '!') {
        fprintf(stderr, "Invalid pipe token\n");
        exit(EXIT_FAILURE);
    }
}

static int compare_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static void timer_probe(const char *name, int64_t (*clock_fn)(void), size_t count)
{
    int64_t *deltas = malloc(count * sizeof(*deltas));
    if (deltas == NULL) die("malloc timer samples");
    size_t zeros = 0, negative = 0;
    int64_t min_positive = INT64_MAX;
    /* Warm up vDSO/code/data before recording consecutive timestamp pairs. */
    for (unsigned i = 0; i < 1000; ++i) (void)clock_fn();
    for (size_t i = 0; i < count; ++i) {
        int64_t first = clock_fn();
        deltas[i] = clock_fn() - first;
        if (deltas[i] == 0) ++zeros;
        if (deltas[i] < 0) ++negative;
        if (deltas[i] > 0 && deltas[i] < min_positive) min_positive = deltas[i];
    }
    qsort(deltas, count, sizeof(*deltas), compare_i64);
    printf("# timer=%s,samples=%zu,zero=%zu,negative=%zu,min_positive_ns=%" PRId64
           ",p50_ns=%" PRId64 ",p90_ns=%" PRId64 ",p99_ns=%" PRId64
           ",max_ns=%" PRId64 "\n", name, count, zeros, negative,
           min_positive == INT64_MAX ? 0 : min_positive, deltas[count / 2],
           deltas[(count - 1) * 90 / 100], deltas[(count - 1) * 99 / 100],
           deltas[count - 1]);
    free(deltas);
}

static void print_result(const char *name, unsigned trial, uint64_t iterations,
                         int64_t elapsed, struct switches parent, struct switches child)
{
    if (elapsed <= 0) {
        fprintf(stderr, "Nonpositive elapsed time; increase iteration count\n");
        exit(EXIT_FAILURE);
    }
    printf("%s,%u,%" PRIu64 ",%" PRId64 ",%.3f,%ld,%ld,%ld,%ld,%d,%d\n",
           name, trial, iterations, elapsed, (double)elapsed / (double)iterations,
           parent.voluntary, parent.involuntary, child.voluntary, child.involuntary,
           parent.cpu, child.cpu);
}

static void null_reads(int fd, uint64_t iterations)
{
    char unused;
    for (uint64_t i = 0; i < iterations; ++i) {
        ssize_t n;
        do { n = read(fd, &unused, 0); } while (n < 0 && errno == EINTR);
        if (n < 0) die("zero-byte read");
        if (n != 0) {
            fprintf(stderr, "Zero-byte read returned data\n");
            exit(EXIT_FAILURE);
        }
    }
}

static void syscall_trial(const struct options *o, unsigned trial)
{
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) die("open /dev/null");
    null_reads(fd, o->warmup);
    struct rusage before = usage();
    int64_t begin = now_ns();
    null_reads(fd, o->syscalls);
    int64_t elapsed = now_ns() - begin;
    struct switches parent = difference(before, usage());
    if (close(fd) < 0) die("close /dev/null");
    struct switches no_child = {0, 0, -1};
    print_result("read_zero", trial, o->syscalls, elapsed, parent, no_child);
}

static void pipe_pair(int read_fd, int write_fd, uint64_t iterations)
{
    for (uint64_t i = 0; i < iterations; ++i) {
        send_token(write_fd);
        receive_token(read_fd);
    }
}

/* This baseline never needs another process. It is NOT a pure syscall baseline
 * that can be subtracted to recover the exact context-switch implementation cost. */
static void pipe_trial(const struct options *o, unsigned trial)
{
    int channel[2];
    if (pipe(channel) < 0) die("pipe baseline");
    pipe_pair(channel[0], channel[1], o->warmup);
    struct rusage before = usage();
    int64_t begin = now_ns();
    pipe_pair(channel[0], channel[1], o->iterations);
    int64_t elapsed = now_ns() - begin;
    struct switches parent = difference(before, usage());
    if (close(channel[0]) < 0 || close(channel[1]) < 0) die("close baseline");
    struct switches no_child = {0, 0, -1};
    print_result("pipe_pair", trial, o->iterations, elapsed, parent, no_child);
}

static void check_cpu(int expected)
{
    cpu_set_t actual;
    if (sched_getaffinity(0, sizeof(actual), &actual) < 0) die("sched_getaffinity");
    if (CPU_COUNT(&actual) != 1 || !CPU_ISSET(expected, &actual) || sched_getcpu() != expected) {
        fprintf(stderr, "Process is not pinned to CPU %d\n", expected);
        exit(EXIT_FAILURE);
    }
}

static void ping_pong_trial(const struct options *o, unsigned trial)
{
    int outbound[2], inbound[2];
    if (pipe(outbound) < 0) die("outbound pipe");
    if (pipe(inbound) < 0) die("inbound pipe");
    if (fflush(NULL) == EOF) die("fflush before fork");
    pid_t child = fork();
    if (child < 0) die("fork");
    if (child == 0) {
        close(outbound[1]);
        close(inbound[0]);
        check_cpu(o->cpu); /* fork inherits the parent's singleton affinity. */
        for (uint64_t i = 0; i < o->warmup; ++i) {
            receive_token(outbound[0]);
            send_token(inbound[1]);
        }
        struct rusage before = usage();
        send_token(inbound[1]); /* Ready: child setup and warm-up have finished. */
        for (uint64_t i = 0; i < o->iterations; ++i) {
            receive_token(outbound[0]);
            send_token(inbound[1]);
        }
        struct switches counts = difference(before, usage());
        transfer(inbound[1], &counts, sizeof(counts), 1);
        close(outbound[0]);
        close(inbound[1]);
        _exit(EXIT_SUCCESS);
    }
    close(outbound[0]);
    close(inbound[1]);
    check_cpu(o->cpu);
    pipe_pair(inbound[0], outbound[1], o->warmup);
    receive_token(inbound[0]); /* Setup/ready is excluded from elapsed time. */
    struct rusage before = usage();
    int64_t begin = now_ns();
    pipe_pair(inbound[0], outbound[1], o->iterations);
    int64_t elapsed = now_ns() - begin;
    struct switches parent = difference(before, usage()), child_counts;
    /* Statistics and waitpid are deliberately outside the measured interval. */
    transfer(inbound[0], &child_counts, sizeof(child_counts), 0);
    close(outbound[1]);
    close(inbound[0]);
    int status;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) die("waitpid");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || child_counts.cpu != o->cpu) {
        fprintf(stderr, "Child failed or ran on a different CPU\n");
        exit(EXIT_FAILURE);
    }
    print_result("ping_pong", trial, o->iterations, elapsed, parent, child_counts);
}

static void help(const char *program)
{
    printf("Usage: %s [--cpu N] [--iterations N] [--syscalls N] [--trials N]\n"
           "       [--warmup N] [--timer-samples N] [--help]\n"
           "Defaults: first allowed CPU, 200000 round trips, 2000000 reads,\n"
           "          7 trials, 10000 warm-up iterations, 200000 timer pairs.\n", program);
}

static uint64_t number(const char *text, uint64_t maximum)
{
    char *end;
    errno = 0;
    if (text[0] < '0' || text[0] > '9') goto invalid;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > maximum) goto invalid;
    return (uint64_t)value;
invalid:
    fprintf(stderr, "Invalid numeric argument: %s\n", text);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    struct options o = {200000, 2000000, 10000, 200000, 7, -1};
    for (int i = 1; i < argc; ++i) {
        const char *name = argv[i];
        if (strcmp(name, "--help") == 0) { help(argv[0]); return 0; }
        if (i + 1 >= argc) { help(argv[0]); return 1; }
        const char *value = argv[++i];
        if (strcmp(name, "--cpu") == 0) o.cpu = (int)number(value, CPU_SETSIZE - 1);
        else if (strcmp(name, "--iterations") == 0) o.iterations = number(value, 1000000000);
        else if (strcmp(name, "--syscalls") == 0) o.syscalls = number(value, 1000000000);
        else if (strcmp(name, "--warmup") == 0) o.warmup = number(value, 1000000000);
        else if (strcmp(name, "--trials") == 0) o.trials = (unsigned)number(value, 1000);
        else if (strcmp(name, "--timer-samples") == 0) o.timer_samples = (size_t)number(value, 10000000);
        else { fprintf(stderr, "Unknown option: %s\n", name); return 1; }
    }
    if (!o.iterations || !o.syscalls || !o.trials || !o.timer_samples) {
        fprintf(stderr, "Iteration/trial/sample counts must be positive\n");
        return 1;
    }
    cpu_set_t allowed, target;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) < 0) die("initial affinity");
    if (o.cpu < 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &allowed)) { o.cpu = cpu; break; }
        }
    }
    if (o.cpu < 0 || !CPU_ISSET(o.cpu, &allowed)) {
        fprintf(stderr, "Requested CPU is not in the process's allowed affinity mask\n");
        return 1;
    }
    CPU_ZERO(&target);
    CPU_SET(o.cpu, &target);
    if (sched_setaffinity(0, sizeof(target), &target) < 0) die("sched_setaffinity");
    check_cpu(o.cpu);
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) die("signal SIGPIPE");
    struct utsname system;
    if (uname(&system) < 0) die("uname");
    struct timespec resolution;
    if (clock_getres(CLOCK_MONOTONIC_RAW, &resolution) < 0) die("clock_getres");
    printf("# system=%s %s %s\n# cpu=%d,iterations=%" PRIu64 ",syscalls=%" PRIu64
           ",trials=%u,warmup=%" PRIu64 "\n", system.sysname, system.release,
           system.machine, o.cpu, o.iterations, o.syscalls, o.trials, o.warmup);
    printf("# clock=CLOCK_MONOTONIC_RAW,reported_resolution_ns=%" PRId64 "\n",
           (int64_t)resolution.tv_sec * INT64_C(1000000000) + resolution.tv_nsec);
    timer_probe("gettimeofday", wall_ns, o.timer_samples);
    timer_probe("monotonic_raw", now_ns, o.timer_samples);
    puts("benchmark,trial,iterations,elapsed_ns,ns_per_iteration,parent_voluntary,"
         "parent_involuntary,child_voluntary,child_involuntary,parent_cpu,child_cpu");
    for (unsigned trial = 1; trial <= o.trials; ++trial) {
        syscall_trial(&o, trial);
        pipe_trial(&o, trial);
        ping_pong_trial(&o, trial);
    }
    return 0;
}
