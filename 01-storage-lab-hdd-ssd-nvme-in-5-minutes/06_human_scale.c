/* Computer Storage in 5 Minutes: HDD, SSD and NVMe - slide 6: if an l1 hit took one second (C version of 06_human_scale.cpp) */
/* Build: make 06_human_scale_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdio.h>

/* seconds on the human scale, printed in the largest unit that keeps the number readable */
/* C has no std::string: the caller passes the buffer and its size */
static void human(double seconds, char *out, size_t out_size) {
    typedef struct { const char *name; double in_seconds; } Unit;
    static const Unit units[] = {
        {"months", 30.44 * 86400.0}, {"days", 86400.0}, {"hours", 3600.0}, {"minutes", 60.0}, {"seconds", 1.0},
    };
    for (size_t i = 0; i < sizeof units / sizeof units[0]; ++i) {
        if (seconds >= units[i].in_seconds) {
            snprintf(out, out_size, "%.1f %s", seconds / units[i].in_seconds, units[i].name);
            return;
        }
    }
    snprintf(out, out_size, "less than a second");
}

int main(void) {
    printf("typical orders of magnitude, not measurements: if an L1 hit took one second\n");

    typedef struct { const char *name; double ns; } Level;   /* typical, not measured */
    static const Level ladder[] = {
        {"L1 cache", 1},      {"L2 cache", 4},      {"L3 cache", 20},
        {"RAM", 100},         {"NVMe SSD", 100e3},  /* 100 microseconds */
        {"SATA SSD", 200e3},  {"HDD seek", 10e6},   /* 10 milliseconds */
    };
    const size_t levels = sizeof ladder / sizeof ladder[0];
    char buf[64];
    for (size_t i = 0; i < levels; ++i) {           /* scale: L1 = 1 second */
        double seconds = ladder[i].ns / ladder[0].ns;
        human(seconds, buf, sizeof buf);
        printf("%s: %s\n", ladder[i].name, buf);
    }
    /* RAM 1.7 minutes, NVMe 1.2 days, HDD seek 3.8 months */

    /* the same ladder against RAM: the cliff between memory and storage */
    const double ram_ns = ladder[3].ns;
    printf("\nhow many RAM accesses fit in one access of each device\n");
    for (size_t i = 0; i < levels; ++i) {
        if (ladder[i].ns <= ram_ns) continue;
        printf("  %s: %.0f x RAM\n", ladder[i].name, ladder[i].ns / ram_ns);
    }
    printf("replace the constants with the numbers of your own hardware (fio, episode 9)\n");
    return 0;
}
