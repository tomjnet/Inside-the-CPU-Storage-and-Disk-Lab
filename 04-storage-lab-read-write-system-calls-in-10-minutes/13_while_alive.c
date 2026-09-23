/* Read and Write in C++: What Happens Behind the System Call - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

/* #include "tomjnet.h"   (the imaginary header of the slide: this block stands in for it) */
typedef struct { char name[96]; } Topic;       /* the text lives inside the struct: no std::string */
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Linux Page Cache: why disk I/O often never touches the disk immediately",
                                   "Inside an SSD: NAND, pages, blocks and the flash translation layer",
                                   "Inside NVMe: submission queues, completion queues and PCIe"};
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

/* C has no std::queue: a fixed array with a head and a tail index, first in first out */
enum { QUEUE_CAP = 8 };
typedef struct { Topic items[QUEUE_CAP]; size_t head; size_t tail; } TopicQueue;
static int push(TopicQueue *q, Topic t) {
    if (q->tail == QUEUE_CAP) return 1;        /* full: the caller decides */
    q->items[q->tail++] = t;
    return 0;
}

int main(void) {
    static TopicQueue todo;                    /* zero initialised: head = tail = 0 */
    while (alive()) {
        if (push(&todo, next_storage_topic()) != 0) return 1;   /* next: the Linux page cache */
        if (push(&todo, viewer_request()) != 0) return 1;       /* batched: one call, not 1M */
        subscribe();                                            /* fsync for your feed */
    }

    printf("\nqueued, first in first out:\n");
    while (todo.head != todo.tail) {
        printf("  %s\n", todo.items[todo.head].name);   /* '\n', no fflush: no forced flush per line */
        ++todo.head;
    }
    return 0;
}
