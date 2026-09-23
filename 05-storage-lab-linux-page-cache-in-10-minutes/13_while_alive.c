/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>

/* #include "tomjnet.h"  (imaginary: everything it would declare is right here) */
typedef struct { const char *lesson; } Page;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static const char *next_episode(void) { return "Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer"; }

typedef struct {
    const char *lab;
    Page current;
} PageCache;

/* C has no member functions and no references: a function that takes the cache and returns a pointer */
static Page *cache_read(PageCache *cache) {
    static const char *lessons[] = { "Inside an SSD: NAND and the flash translation layer",
                                     "Inside NVMe: submission queues, completion queues and PCIe",
                                     "Buffered I/O vs Direct I/O: O_DIRECT and io_uring" };
    cache->current.lesson = lessons[episodes++];
    return &cache->current;
}

static PageCache open_lab(const char *first) {
    printf("next in the Storage and Disk Lab: %s\n", first);
    PageCache c = { "Storage and Disk Lab", { "" } };
    return c;
}
static void learn(const Page *page) { printf("  learned: %s\n", page->lesson); }
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

int main(void) {
    PageCache cache = open_lab(next_episode());  /* inside an SSD */
    while (alive()) {
        Page *page = cache_read(&cache); /* a hit: no device involved */
        learn(page);                     /* 4 KiB at a time */
        subscribe();                     /* fsync: a durable choice */
    }
    printf("%s: %d episodes served from the cache\n", cache.lab, episodes);
    return 0;
}
