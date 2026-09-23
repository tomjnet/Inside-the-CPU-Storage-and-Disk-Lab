/* Computer Storage in 5 Minutes: HDD, SSD and NVMe - slide 7: thank you (C version of 07_while_alive.cpp) */
/* Build: make 07_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdio.h>

/* #include "tomjnet.h"   (the imaginary header of the slide: this block stands in for it) */
/* C has no std::string: the name lives inside the struct */
typedef struct { char name[96]; } Topic;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Blocks, Sectors and Pages: How Storage Is Organized",
                                   "How Filesystems Work: From File Name to Disk Blocks",
                                   "Read and Write in C++: What Happens Behind the System Call"};
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

/* C has no std::queue: a fixed ring buffer with a head, a tail and a count,
   the same shape as an NVMe submission queue */
#define QUEUE_SLOTS 8
typedef struct { Topic slots[QUEUE_SLOTS]; size_t head, tail, count; } TopicQueue;

static int queue_push(TopicQueue *q, Topic t) {
    if (q->count == QUEUE_SLOTS) return 0;                   /* full: the caller stops */
    q->slots[q->tail] = t;
    q->tail = (q->tail + 1) % QUEUE_SLOTS;
    ++q->count;
    return 1;
}
static const Topic *queue_front(const TopicQueue *q) { return &q->slots[q->head]; }
static void queue_pop(TopicQueue *q) {
    q->head = (q->head + 1) % QUEUE_SLOTS;
    --q->count;
}

int main(void) {
    static TopicQueue storage_lab;                            /* zero initialised: empty */
    while (alive()) {
        if (!queue_push(&storage_lab, next_storage_topic())) break;  /* submission queue */
        if (!queue_push(&storage_lab, viewer_request())) break;      /* queue depth 2 */
        subscribe();                                                 /* lifetime benefit */
    }

    printf("the Storage and Disk Lab queue, %zu topics, first in first out:\n", storage_lab.count);
    while (storage_lab.count > 0) {
        printf("  %s\n", queue_front(&storage_lab)->name);
        queue_pop(&storage_lab);
    }
    return 0;
}
