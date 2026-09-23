/* Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 12: p50, p99 and p99.9 from the samples (C version of 12_percentiles.cpp) */
/* Build: make 12_percentiles_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* percentile of sorted samples: O(n log n) sort, O(1) per lookup */
static double percentile(const double *sorted, size_t n, double p) {
    double rank = p / 100.0 * (double)(n - 1);
    return sorted[(size_t)rank];
}

static double mean(const double *v, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += v[i];
    return sum / (double)n;
}

/* fixed-seed splitmix64 instead of std::mt19937: the same samples on every toolchain,
   but not the same numbers as the C++ run, so the printed values differ slightly */
static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* A model of the read latencies of an SSD at queue depth 1, in nanoseconds (not a measurement): a body of
   70 to 110 us, 1 read in 100 behind another command (+400 us), 1 in 1000 behind garbage collection (+5 ms).
   The caller owns the returned array and frees it: C has no std::vector. */
static double *model_latencies(size_t n) {
    uint64_t rng = 42;
    double *v = malloc(n * sizeof *v);
    if (v == NULL) return NULL;
    for (size_t i = 0; i < n; ++i) {
        double us = 70.0 + (double)(splitmix64(&rng) % 40u);
        uint32_t dice = (uint32_t)(splitmix64(&rng) % 1000u);
        if (dice < 10u) us += 400.0;
        if (dice == 0u) us += 5000.0;
        v[i] = us * 1000.0;
    }
    return v;
}

int main(void) {
    const size_t n = 100000;
    double *lat_ns = model_latencies(n);
    double *all = model_latencies(5000);
    if (lat_ns == NULL || all == NULL) {
        printf("out of memory\n");
        free(lat_ns);
        free(all);
        return 1;
    }

    /* 1000 samples hold ONE value above p99.9: collect 100 000 or more */
    qsort(lat_ns, n, sizeof lat_ns[0], cmp_double);
    double p50 = percentile(lat_ns, n, 50.0);       /* the typical read */
    double p99 = percentile(lat_ns, n, 99.0);       /* 1 in 100 is slower */
    double p999 = percentile(lat_ns, n, 99.9);      /* 1 in 1000: the tail */
    double iops = 1e9 / mean(lat_ns, n);            /* valid at queue depth 1 */

    printf("model latencies, %u samples (not a measurement)\n", (unsigned)n);
    printf("  mean   %.1f us\n", mean(lat_ns, n) / 1000.0);
    printf("  p50    %.1f us\n", p50 / 1000.0);
    printf("  p99    %.1f us\n", p99 / 1000.0);
    printf("  p99.9  %.1f us\n", p999 / 1000.0);
    printf("  max    %.1f us\n", lat_ns[n - 1] / 1000.0);
    printf("  IOPS at queue depth 1 = 1 s / mean = %.1f\n", iops);
    printf("  p99.9 / p50 = %.1fx: the mean does not show it\n\n", p999 / p50);

    /* the same device, five short tests of 1000 reads each: one value defines p99.9, so it jumps around */
    printf("p99.9 of five tests of only 1000 samples each:\n");
    double small[1000];
    for (size_t t = 0; t < 5; ++t) {
        for (size_t i = 0; i < 1000; ++i) small[i] = all[t * 1000 + i];
        qsort(small, 1000, sizeof small[0], cmp_double);
        printf("  test %u: p50 %.1f us   p99.9 %.1f us\n", (unsigned)(t + 1),
               percentile(small, 1000, 50.0) / 1000.0, percentile(small, 1000, 99.9) / 1000.0);
    }
    free(lat_ns);
    free(all);
    return 0;
}
