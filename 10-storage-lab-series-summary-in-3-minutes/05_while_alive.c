/* Inside the CPU: What We Learned About Storage - slide 5: thank you (C version of 05_while_alive.cpp) */
/* Build: make 05_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdio.h>

/* the imaginary tomjnet.h of the slide */
typedef struct { const char *name; } Lab;
static int episodes = 0;
static int alive(void) { return episodes < 3; }               /* three labs, then the demo ends */
static Lab next_lab(void) {
    static const char *labs[] = {"Inside the CPU: Networking and NIC Lab", "Inside the CPU: Low Latency C++ Lab",
                                 "Inside the CPU: PCIe and DMA Lab"};
    Lab lab = { labs[episodes++] };
    return lab;
}
static void measure(const Lab *lab) { printf("submitted: %s\n", lab->name); }
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::queue: a fixed ring buffer with a head and a tail, like an NVMe submission queue */
#define QCAP 8
typedef struct { Lab slot[QCAP]; size_t head, tail; } LabQueue;
static size_t q_size(const LabQueue *q) { return q->tail - q->head; }
static int q_push(LabQueue *q, Lab lab) {
    if (q_size(q) == QCAP) return 0;                             /* full: the caller must wait */
    q->slot[q->tail++ % QCAP] = lab;
    return 1;
}
static const Lab *q_back(const LabQueue *q) { return &q->slot[(q->tail - 1) % QCAP]; }
static const Lab *q_front(const LabQueue *q) { return &q->slot[q->head % QCAP]; }
static void q_pop(LabQueue *q) { q->head++; }

int main(void) {
    LabQueue next_labs = { {{NULL}}, 0, 0 };  /* a submission queue of labs */
    while (alive()) {
        if (!q_push(&next_labs, next_lab())) return 1;  /* Networking and NIC is next */
        measure(q_back(&next_labs));     /* your device, your tail */
        subscribe();                     /* lifetime benefit */
    }
    const size_t submitted = q_size(&next_labs);
    size_t completed = 0;
    while (q_size(&next_labs) > 0) {     /* the completion side: first in, first out */
        printf("completed %d: %s\n", (int)++completed, q_front(&next_labs)->name);
        q_pop(&next_labs);
    }
    printf("%d labs submitted, %d completed, in order: Networking and NIC first\n", (int)submitted, (int)completed);
    return submitted == completed ? 0 : 1;
}
