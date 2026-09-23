/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>
#include <string.h>

/* The imaginary "tomjnet.h" of the slide. */
typedef struct { char name[96]; } Topic;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Storage Performance: IOPS, Throughput, Latency and Queue Depth",
                                   "Inside the CPU: What We Learned About Storage",
                                   "Inside the CPU: Networking and NIC Lab"};
    Topic t;
    snprintf(t.name, sizeof t.name, "%s", topics[episodes++]);
    return t;
}
static Topic viewer_request(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "viewer request #%d", episodes);
    return t;
}
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::queue: a fixed array with a head and a tail index */
typedef struct { Topic items[8]; int head, tail; } TopicQueue;
static void push(TopicQueue *q, Topic t) { if (q->tail < 8) q->items[q->tail++] = t; }

/* #include "tomjnet.h" */

int main(void) {
    TopicQueue submission;                          /* queue depth: you */
    memset(&submission, 0, sizeof submission);
    while (alive()) {
        push(&submission, next_storage_topic());    /* buffered */
        push(&submission, viewer_request());        /* direct, no cache */
        subscribe();                                /* fsync: durable */
    }

    printf("submission queue, in order:\n");
    while (submission.head != submission.tail) {
        printf("  %s\n", submission.items[submission.head].name);
        ++submission.head;
    }
    return 0;
}
