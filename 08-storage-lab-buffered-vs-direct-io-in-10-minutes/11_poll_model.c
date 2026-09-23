/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 11: user space storage: spdk and polling (C version of 11_poll_model.cpp) */
/* Build: make 11_poll_model_c */
/*
 * A model of what an SPDK style driver does with one NVMe queue pair. No device is touched: the "device" is
 * a counter of model time, so the output is the same on every machine.
 */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { kRead = 0x02 };                                       /* the NVMe read opcode */

typedef struct { int opcode; uint64_t lba; uint32_t blocks; } Command;
typedef struct { uint16_t command_id; uint16_t status; } Completion;

#define K_POLL_NS ((uint64_t)20)                             /* model: one trip around the poll loop */
#define K_DEVICE_NS ((uint64_t)80000)                        /* model: the flash answers after 80 microseconds */

typedef struct {
    Command sq_entry;
    Completion cq_entry;
    uint32_t sq_tail, cq_head, cq_tail;                      /* what the doorbells and the device write */
    uint32_t sq_doorbells, cq_doorbells;                     /* MMIO writes, not system calls */
    uint64_t now_ns, done_at_ns;
    int in_flight;
} QueuePair;

static void qp_submit(QueuePair *qp, Command c) {           /* 64 bytes into the ring, then one MMIO write */
    qp->sq_entry = c;
    ++qp->sq_tail;
    ++qp->sq_doorbells;
    qp->done_at_ns = qp->now_ns + K_DEVICE_NS;
    qp->in_flight = 1;
}
static void qp_device_tick(QueuePair *qp) {                  /* the device side: post the completion when due */
    qp->now_ns += K_POLL_NS;
    if (qp->in_flight && qp->now_ns >= qp->done_at_ns) {
        Completion done = { (uint16_t)(qp->sq_tail - 1), 0 };
        qp->cq_entry = done;
        ++qp->cq_tail;
        qp->in_flight = 0;
    }
}
static int qp_completion_ready(const QueuePair *qp) { return qp->cq_head != qp->cq_tail; }   /* a read of our own memory */
static Completion qp_reap(QueuePair *qp) {
    ++qp->cq_head;
    ++qp->cq_doorbells;                                      /* tell the device the entry is consumed */
    return qp->cq_entry;
}

typedef struct { const char *name; double latency_us; } Device;

int main(void) {
    /* SPDK model: the queues live in our process, nobody interrupts us */
    QueuePair qp = {0};                /* mapped NVMe queues, no kernel */
    Command cmd = { kRead, 0, 8 };
    qp_submit(&qp, cmd);               /* write the entry, ring the doorbell */
    uint64_t polls = 0;
    while (!qp_completion_ready(&qp)) {   /* busy poll: one core at 100 percent */
        ++polls;                       /* no system call, no interrupt, */
        qp_device_tick(&qp);           /* no context switch (model time) */
    }
    Completion done = qp_reap(&qp);    /* cost: a whole core, not latency */

    printf("one read of %u blocks at LBA %" PRIu64 ", polled from user space (model):\n",
           (unsigned)qp.sq_entry.blocks, qp.sq_entry.lba);
    printf("  polls until the completion : %" PRIu64 "\n", polls);
    printf("  model time                 : %" PRIu64 " microseconds\n", qp.now_ns / 1000);
    printf("  completion                 : command %u, status %u\n", (unsigned)done.command_id, (unsigned)done.status);
    printf("  doorbell writes (MMIO)     : %u\n", (unsigned)(qp.sq_doorbells + qp.cq_doorbells));
    printf("  system calls, interrupts, context switches : 0, 0, 0\n\n");

    /* Why it pays off only on fast devices: the interrupt path adds a roughly fixed cost per request. */
    const double kInterruptPathUs = 5.0;                     /* typical: interrupt, wake up, context switch, cold cache */
    const Device devices[] = {{"HDD seek", 10000.0}, {"SATA SSD", 200.0}, {"NVMe SSD", 80.0}, {"fastest NVMe", 10.0}};
    printf("typical interrupt path of about %g microseconds, as a share of one request:\n", kInterruptPathUs);
    for (size_t i = 0; i < sizeof devices / sizeof devices[0]; ++i)
        printf("  %-14s%9g us device  ->  %.2f percent overhead\n", devices[i].name, devices[i].latency_us,
               100.0 * kInterruptPathUs / (devices[i].latency_us + kInterruptPathUs));
    printf("the price of polling: one core at 100 percent, a device that no other process can use, and no\n"
           "filesystem. These are typical orders of magnitude, not measurements.\n");

    const int ok = polls == K_DEVICE_NS / K_POLL_NS && done.status == 0;
    if (!ok) printf("FAIL: the model lost the completion\n");
    return ok ? 0 : 1;
}
