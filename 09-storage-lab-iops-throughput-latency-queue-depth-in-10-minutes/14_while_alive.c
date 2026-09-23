/* Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 14: thank you (C version of 14_while_alive.cpp) */
/* Build: make 14_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>

/* the imaginary tomjnet.h of the slide */
typedef struct { char name[64]; } Topic;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Inside the CPU: What We Learned About Storage",
                                   "Networking and NIC Lab", "Low Latency C++ Lab"};
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

/* C has no std::queue: a fixed ring buffer, push at the tail, pop at the head */
#define QCAP 8
typedef struct { Topic items[QCAP]; size_t head, count; } TopicQueue;
static int queue_push(TopicQueue *q, Topic t) {
    if (q->count == QCAP) return 0;                          /* full: the submitter must wait */
    q->items[(q->head + q->count) % QCAP] = t;
    ++q->count;
    return 1;
}
static Topic queue_pop(TopicQueue *q) {
    Topic t = q->items[q->head];
    q->head = (q->head + 1) % QCAP;
    --q->count;
    return t;
}

int main(void) {
    static TopicQueue todo;                  /* queue depth: 8, C has no growing container */
    int ok = 1;
    while (alive()) {
        ok &= queue_push(&todo, next_storage_topic());   /* next: the series summary */
        ok &= queue_push(&todo, viewer_request());       /* submitted, done later */
        subscribe();                                     /* lifetime benefit */
    }

    printf("in flight: %u topics, completed in submission order:\n", (unsigned)todo.count);
    while (todo.count > 0) {
        Topic t = queue_pop(&todo);
        printf("  %s\n", t.name);
    }
    return ok ? 0 : 1;
}
