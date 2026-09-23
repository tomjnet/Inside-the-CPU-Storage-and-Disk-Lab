/* Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 5: little's law (C version of 05_littles_law.cpp) */
/* Build: make 05_littles_law_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>

/* Little's law: in flight = IOPS x latency (latency in seconds) */
static double iops_at(double qd, double latency_s) { return qd / latency_s; }

/* The model SSD of the queue depth slide: 32 units that work in parallel, a service time of 100 us when
   one unit is busy and 160 us when all 32 are (they share the controller and the channels). */
static double model_latency_s(double qd) {
    const double units = 32.0;
    double busy = qd < units ? qd : units;
    double service_s = 100e-6 * (1.0 + 0.6 * (busy - 1.0) / (units - 1.0));
    double iops = busy / service_s;                  /* the device completes this many per second */
    return qd / iops;                                /* Little's law gives the latency a request sees */
}

int main(void) {
    /* queue depth 1: one I/O at a time, the device mostly waits for us */
    double qd1 = iops_at(1.0, 100e-6);           /* 10 000 IOPS */
    /* queue depth 32: the SSD works on many NAND dies in parallel */
    double qd32 = iops_at(32.0, 160e-6);         /* 200 000 IOPS */
    /* past the knee: more queue only adds waiting, IOPS stay flat */
    double qd256 = iops_at(256.0, 1280e-6);      /* 200 000 IOPS, 8x latency */

    printf("QD   1 at  100 us: %.0f IOPS\n", qd1);
    printf("QD  32 at  160 us: %.0f IOPS\n", qd32);
    printf("QD 256 at 1280 us: %.0f IOPS\n\n", qd256);

    printf("model SSD (not a measurement): the knee sits at queue depth 32\n");
    printf("   QD      IOPS   latency us\n");
    for (double qd = 1.0; qd <= 256.0; qd *= 2.0) {
        double lat = model_latency_s(qd);
        printf("%5.0f%10.0f%13.0f%s", qd, iops_at(qd, lat), lat * 1e6,
               qd > 32.0 ? "   waiting in the queue\n" : "\n");
    }

    /* the sanity check of the fio report on slide 10: 412k IOPS x 76.9 us */
    printf("\nfio check: 412000 IOPS x 76.9 us = %.1f in flight (iodepth was 32)\n", 412000.0 * 76.9e-6);
    return 0;
}
