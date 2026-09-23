/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 14: thank you (C version of 14_while_alive.cpp) */
/* Build: make 14_while_alive_c */
/* */
/* The thank-you slide as a program. tomjnet.h is imaginary, so its contents live here: a tiny queue pair */
/* for topics, where submit is a memory write and ring_doorbell announces the whole batch at once. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

/* ---- the imaginary tomjnet.h ---- */
typedef struct { char name[96]; } Topic;       /* the text lives inside the struct: no std::string */
static int episodes = 0;
static int alive(void) { return episodes < 3; }              /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Buffered I/O vs Direct I/O: How Low Latency Storage Works",
                                   "Storage Performance: IOPS, Throughput, Latency and Queue Depth",
                                   "Inside the CPU: What We Learned About Storage"};
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

/* C has no templates or std::vector: a queue pair of Topic over a fixed array, the owner passes a pointer */
enum { QUEUE_CAP = 16 };
typedef struct {
    Topic sq[QUEUE_CAP];
    size_t count;                   /* entries submitted */
    size_t announced;               /* entries the last doorbell covered */
    int doorbell_writes;
} TopicQueuePair;

static int submit(TopicQueuePair *q, Topic entry) {          /* a memory write, nothing crosses PCIe */
    if (q->count == QUEUE_CAP) return 0;                     /* full: the caller decides */
    q->sq[q->count++] = entry;
    return 1;
}
static void ring_doorbell(TopicQueuePair *q) {               /* one MMIO write for the whole batch */
    ++q->doorbell_writes;
    printf("  doorbell %d: %zu new entries\n", q->doorbell_writes, q->count - q->announced);
    for (; q->announced < q->count; ++q->announced) printf("    completed: %s\n", q->sq[q->announced].name);
}
/* ---- end of tomjnet.h ---- */

int main(void) {
    static TopicQueuePair todo;              /* one pair per core, zero initialised */
    while (alive()) {
        if (!submit(&todo, next_storage_topic())) return 1;   /* buffered vs direct I/O */
        if (!submit(&todo, viewer_request())) return 1;       /* 64 bytes each */
        ring_doorbell(&todo);                /* one MMIO write for both */
        subscribe();                         /* lifetime benefit */
    }

    printf("%zu topics submitted with %d doorbell writes\n", todo.count, todo.doorbell_writes);
    return 0;
}
