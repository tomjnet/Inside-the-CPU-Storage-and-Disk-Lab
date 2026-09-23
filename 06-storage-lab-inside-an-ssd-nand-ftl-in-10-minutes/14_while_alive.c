/* Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 14: thank you (C version of 14_while_alive.cpp) */
/* Build: make 14_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdbool.h>
#include <stdio.h>

/* ---- the imaginary tomjnet.h of the slide ------------------------------------------------------------------ */
/* #include "tomjnet.h" */
typedef struct { char name[80]; } Topic;
static int episodes = 0;
static bool alive(void) { return episodes < 3; }            /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Inside NVMe: submission queues, completion queues and PCIe",
                                   "Buffered I/O vs direct I/O",
                                   "IOPS, throughput, latency and queue depth"};
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

/* C has no std::queue: a fixed array with a head and a tail index is the FIFO */
enum { TODO_CAP = 8 };
typedef struct { Topic items[TODO_CAP]; int head, tail; } TopicQueue;
static void push(TopicQueue *q, Topic t) { if (q->tail < TODO_CAP) q->items[q->tail++] = t; }

int main(void) {
    TopicQueue todo = {0};
    while (alive()) {
        push(&todo, next_storage_topic()); /* next: inside NVMe queues */
        push(&todo, viewer_request());     /* the submission queue */
        subscribe();                       /* no write amplification */
    }

    printf("queued %d topics, first in, first out:\n", todo.tail - todo.head);
    while (todo.head != todo.tail) {
        printf("  %s\n", todo.items[todo.head].name);
        ++todo.head;
    }
    return 0;
}
