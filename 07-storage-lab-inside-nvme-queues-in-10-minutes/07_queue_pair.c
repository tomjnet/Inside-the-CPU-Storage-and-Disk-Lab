/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 7: a queue pair as two rings (C version of 07_queue_pair.cpp) */
/* Build: make 07_queue_pair_c */
/* */
/* The data structure of the model: two arrays, four indexes, two doorbell registers. The program prints */
/* what one pair costs in memory, who owns each index and how the indexes wrap around. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>

typedef struct {                    /* 64 bytes, fixed by the spec */
    uint8_t  opcode, flags;         /* 0x02 read, 0x01 write */
    uint16_t command_id;            /* comes back in the completion */
    uint32_t nsid;                  /* namespace: which drive */
    uint64_t reserved, metadata;
    uint64_t prp1, prp2;            /* physical addresses of the buffer */
    uint64_t slba;                  /* starting LBA */
    uint32_t nlb;                   /* low 16 bits: blocks minus 1 */
    uint32_t cdw13_15[3];           /* hints, protection info */
} SubmissionEntry;
_Static_assert(sizeof(SubmissionEntry) == 64, "one cache line");

typedef struct {                    /* 16 bytes */
    uint32_t result;                /* command specific */
    uint32_t reserved;
    uint16_t sq_head;               /* how far the controller has read */
    uint16_t sq_id;                 /* which submission queue */
    uint16_t command_id;            /* matches the submission entry */
    uint16_t status;                /* bit 0: phase, rest: 0 = success */
} CompletionEntry;
_Static_assert(sizeof(CompletionEntry) == 16, "16 bytes");

typedef uint16_t Index;
enum { kSize = 8 };                 /* real queues: up to 65536 */
static Index next(Index i) { return (Index)((i + 1) % kSize); }
typedef struct {                    /* one per CPU core: no lock */
    SubmissionEntry sq[kSize];      /* host writes, controller reads */
    CompletionEntry cq[kSize];      /* controller writes, host reads */
    Index sq_tail, cq_head;         /* moved by the host */
    Index sq_head, cq_tail;         /* moved by the controller */
    Index sq_doorbell, cq_doorbell; /* MMIO registers */
    Index next_id;                  /* command id generator */
    int doorbell_writes;            /* each one crosses PCIe */
} QueuePair;                        /* C has no member initialisers: a static object starts all zero */

int main(void) {
    static QueuePair q;
    printf("model: %d entries per ring, %d usable (full when tail + 1 == head)\n", kSize, kSize - 1);
    printf("  submission ring: %zu bytes, completion ring: %zu bytes\n", sizeof q.sq, sizeof q.cq);
    printf("  indexes at start: sq_tail %d, sq_head %d, cq_tail %d, cq_head %d, doorbells %d and %d\n",
           q.sq_tail, q.sq_head, q.cq_tail, q.cq_head, q.sq_doorbell, q.cq_doorbell);
    printf("  command ids start at %d, doorbell writes so far: %d\n\n", q.next_id, q.doorbell_writes);

    printf("the indexes wrap around: ");
    Index i = 0;
    for (int step = 0; step < 10; ++step) {
        printf("%d%s", i, step < 9 ? " -> " : "\n");
        i = next(i);
    }

    printf("\nwho writes what (one writer per index, so one core needs no lock):\n");
    printf("  host:       sq entries, sq_tail, cq_head, both doorbell registers\n");
    printf("  controller: cq entries, sq_head, cq_tail\n\n");

    const long long depth = 1024;   /* a typical I/O queue depth a Linux driver asks for */
    const long long sq_bytes = depth * (long long)sizeof(SubmissionEntry);
    const long long cq_bytes = depth * (long long)sizeof(CompletionEntry);
    printf("a typical real pair with %lld entries: %lld KiB of submission ring plus %lld KiB of completion ring\n",
           depth, sq_bytes / 1024, cq_bytes / 1024);
    printf("one pair per core on a 16 core machine: %lld KiB of host RAM\n", 16 * (sq_bytes + cq_bytes) / 1024);
    return 0;
}
