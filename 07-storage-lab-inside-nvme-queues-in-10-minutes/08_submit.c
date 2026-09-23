/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 8: submit, then ring the doorbell (C version of 08_submit.cpp) */
/* Build: make 08_submit_c */
/* */
/* Submitting is a memory write; the doorbell is the PCIe crossing. The program queues four reads and rings */
/* once, then does the same with one doorbell per command, and finally fills the ring until submit refuses. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/* C has no references: the pair is passed by pointer */
static int submit(QueuePair *q, uint64_t lba, uint64_t buffer) {
    if (next(q->sq_tail) == q->sq_head) return 0;    /* ring full */
    SubmissionEntry *e = &q->sq[q->sq_tail];  /* plain memory write */
    memset(e, 0, sizeof *e); e->opcode = 0x02; /* read */
    e->command_id = q->next_id++;
    e->nsid = 1; e->slba = lba; e->prp1 = buffer;
    q->sq_tail = next(q->sq_tail);            /* no PCIe traffic yet */
    return 1;
}
static void ring_sq_doorbell(QueuePair *q) {  /* one MMIO write */
    q->sq_doorbell = q->sq_tail; ++q->doorbell_writes;
}

int main(void) {
    static QueuePair batched;
    printf("four reads, one doorbell:\n");
    for (uint64_t i = 0; i < 4; ++i) {
        const int ok = submit(&batched, 0x1000 + 8 * i, i * 4096);
        printf("  submit lba %" PRIu64 ": %s, sq_tail %d, doorbell register still %d\n",
               (uint64_t)(0x1000 + 8 * i), ok ? "queued" : "ring full", batched.sq_tail, batched.sq_doorbell);
    }
    ring_sq_doorbell(&batched);
    printf("  ring: doorbell register %d, doorbell writes %d\n\n", batched.sq_doorbell, batched.doorbell_writes);

    static QueuePair eager;
    for (uint64_t i = 0; i < 4; ++i) {
        if (submit(&eager, 0x1000 + 8 * i, i * 4096)) ring_sq_doorbell(&eager);
    }
    printf("four reads, one doorbell each: doorbell writes %d\n", eager.doorbell_writes);
    printf("same commands in the ring, %dx the PCIe crossings: batch when you can\n\n",
           eager.doorbell_writes / batched.doorbell_writes);

    const SubmissionEntry *last = &batched.sq[3];
    printf("entry 3 in the ring: opcode %d, command id %d, nsid %" PRIu32 ", slba %" PRIu64 ", prp1 %" PRIu64 "\n\n",
           last->opcode, last->command_id, last->nsid, last->slba, last->prp1);

    int accepted = 4;                         /* nobody consumes: the controller of this sample never runs */
    while (submit(&batched, 0x2000, 0)) ++accepted;
    printf("with no controller the ring of %d accepts %d commands, then submit returns false\n", kSize, accepted);
    return accepted == kSize - 1 && batched.doorbell_writes == 1 && eager.doorbell_writes == 4 ? 0 : 1;
}
