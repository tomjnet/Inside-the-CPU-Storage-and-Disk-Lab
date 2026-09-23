/* Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 3: block size links iops and throughput (C version of 03_block_size.cpp) */
/* Build: make 03_block_size_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>

typedef struct { const char *name; double iops; double block_bytes; } Job;

int main(void) {
    printf("typical NVMe orders of magnitude, not measurements\n\n");

    /* throughput = IOPS x block size: one device, three block sizes */
    static const Job jobs[] = {
        {"4 KiB random read ", 500000.0, 4096.0},       /* IOPS bound */
        {"128 KiB sequential", 25000.0, 131072.0},
        {"1 MiB sequential  ", 3500.0, 1048576.0},      /* bandwidth bound */
    };
    const size_t n_jobs = sizeof jobs / sizeof jobs[0];
    for (size_t i = 0; i < n_jobs; ++i) {
        const Job *j = &jobs[i];
        double mib_s = j->iops * j->block_bytes / (1024.0 * 1024.0);
        printf("%s  %g IOPS  %g MiB/s\n", j->name, j->iops, mib_s);   /* no system calls */
    }

    /* the formula the other way round: the IOPS a throughput target costs at every block size */
    const double target_mib_s = 1000.0;
    printf("\nIOPS needed to move %g MiB/s:\n", target_mib_s);
    for (size_t i = 0; i < n_jobs; ++i) {
        const Job *j = &jobs[i];
        double iops = target_mib_s * 1024.0 * 1024.0 / j->block_bytes;
        printf("  at %g KiB blocks: %g IOPS\n", j->block_bytes / 1024.0, iops);
    }
    printf("\nsmall blocks: the fixed cost per I/O is the limit. Large blocks: the link is.\n");
    return 0;
}
