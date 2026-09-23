/* Blocks, Sectors and Pages: How Storage Is Organized - slide 10: checking partition alignment (C version of 10_partition_alignment.cpp) */
/* Build: make 10_partition_alignment_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__linux__)
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#endif

static const uint64_t kSector = 512;    /* logical sector, bytes */
static const uint64_t kBlock  = 4096;   /* filesystem block, bytes */

/* a partition is an LBA range: [start, start + count) */
typedef struct { const char *name; uint64_t start, count; } Partition;

/* aligned: its first byte sits on a physical sector boundary
   (C has no default arguments: the caller passes 4096) */
static int is_aligned(const Partition *p, uint64_t physical) {
    return (p->start * kSector) % physical == 0;
}

static void report(const Partition *p, uint64_t physical) {
    const uint64_t first_byte = p->start * kSector;
    printf("  %s: LBA %" PRIu64 " to %" PRIu64 " (%" PRIu64 " MiB), first byte %" PRIu64 ", %s to %" PRIu64 "\n",
           p->name, p->start, p->start + p->count - 1, p->count * kSector / (1024 * 1024), first_byte,
           is_aligned(p, physical) ? "aligned" : "MISALIGNED", physical);
    /* which physical sectors does filesystem block 0 of this partition cover? */
    const uint64_t first_phys = first_byte / physical;
    const uint64_t last_phys = (first_byte + kBlock - 1) / physical;
    if (physical < kBlock || first_phys == last_phys)
        printf("    filesystem block 0 ends on a physical sector boundary: one clean write\n");
    else
        printf("    filesystem block 0 straddles physical sectors %" PRIu64 " and %" PRIu64
               ": two read, modify, write cycles\n", first_phys, last_phys);
}

#if defined(__linux__)
static uint64_t read_number(const char *file, uint64_t fallback) {
    FILE *in = fopen(file, "r");
    uint64_t value = 0;
    if (in == NULL) return fallback;
    int got = fscanf(in, "%" SCNu64, &value);
    fclose(in);
    return got == 1 ? value : fallback;
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* /sys/block/DEV/PART/start counts 512 byte units, whatever the sector size of the drive is */
static void report_this_machine(void) {
    char path[1024];                     /* room for two 255 byte directory names */
    int partitions = 0;
    DIR *top = opendir("/sys/block");
    if (top == NULL) {
        printf("  cannot list /sys/block: %s\n", strerror(errno));
    } else {
        struct dirent *it;
        while ((it = readdir(top)) != NULL) {
            const char *dev = it->d_name;
            if (dev[0] == '.' || starts_with(dev, "loop") || starts_with(dev, "ram")) continue;
            snprintf(path, sizeof path, "/sys/block/%s/queue/logical_block_size", dev);
            const uint64_t logical = read_number(path, 512);
            snprintf(path, sizeof path, "/sys/block/%s/queue/physical_block_size", dev);
            const uint64_t physical = read_number(path, 512);
            printf("  %s: logical %" PRIu64 ", physical %" PRIu64 "\n", dev, logical, physical);
            snprintf(path, sizeof path, "/sys/block/%s", dev);
            DIR *sub = opendir(path);
            if (sub == NULL) continue;
            struct dirent *s;
            while ((s = readdir(sub)) != NULL) {
                const char *part = s->d_name;
                struct stat st;
                if (!starts_with(part, dev)) continue;
                snprintf(path, sizeof path, "/sys/block/%s/%s/start", dev, part);
                if (stat(path, &st) != 0) continue;
                const uint64_t start = read_number(path, 0);
                snprintf(path, sizeof path, "/sys/block/%s/%s/size", dev, part);
                const Partition p = {part, start, read_number(path, 1)};
                report(&p, physical);
                ++partitions;
            }
            closedir(sub);
        }
        closedir(top);
    }
    if (partitions == 0)
        printf("  no partitions found here (WSL and containers show whole virtual disks):\n"
               "  on real hardware every partition prints its start LBA and the verdict\n");
}
#endif

int main(void) {
    Partition modern = {"sda1", 2048, 1048576};  /* starts 1 MiB in: aligned */
    Partition legacy = {"hda1", 63, 1048576};    /* old DOS layout: 32256 B */
    /* legacy: every 4 KiB block straddles two physical sectors,
       so one block write costs two read, modify, write cycles */

    printf("model partitions on a 512e drive (logical 512, physical 4096):\n");
    report(&modern, 4096);
    report(&legacy, 4096);
    printf("the same legacy partition on a 512n drive (physical 512):\n");
    report(&legacy, 512);

    printf("\nthis machine:\n");
#if defined(__linux__)
    report_this_machine();
#else
    printf("  this sample needs Linux: it would read /sys/block/DEV/PART/start of every partition\n"
           "  and print whether it is aligned to the physical sector of its drive\n");
#endif

    const int ok = is_aligned(&modern, 4096) && !is_aligned(&legacy, 4096) && is_aligned(&legacy, 512);
    return ok ? 0 : 1;
}
