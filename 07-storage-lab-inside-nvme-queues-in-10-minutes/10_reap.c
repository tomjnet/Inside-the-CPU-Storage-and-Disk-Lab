/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 10: reaping completions (C version of 10_reap.cpp) */
/* Build: make 10_reap_c */
/* */
/* The whole round trip of the model: submit four reads, ring once, the controller runs, reap four, ring */
/* once. Then the ring is filled until submit refuses, to show the full condition (size minus one). */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

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

/* Host RAM of the model: four 4 KiB pages. prp1 is the "physical address" of a page, here an offset into it. */
#define kPage ((uint64_t)4096)
static uint8_t host_ram[4 * 4096];

/* What the DMA comment of the slide stands for: the device writes the block into host RAM, no CPU copy. */
/* The fake NAND returns the low byte of the LBA in every byte of the block. */
static void dma_write(uint64_t prp1, uint64_t lba) {
    memset(host_ram + prp1, (int)(lba & 0xff), (size_t)kPage);
}

static void controller_run(QueuePair *q) {    /* the device side */
    while (q->sq_head != q->sq_doorbell) {    /* fetch by DMA */
        const SubmissionEntry *e = &q->sq[q->sq_head];
        q->sq_head = next(q->sq_head);
        /* DMA: NAND data goes straight to e->prp1, no CPU copy */
        dma_write(e->prp1, e->slba);          /* (sample only: the model of that transfer) */
        const CompletionEntry c = {0, 0, q->sq_head, 1, e->command_id, 1};
        q->cq[q->cq_tail] = c;
        q->cq_tail = next(q->cq_tail);        /* then one MSI-X interrupt */
    }
}

static int errors = 0;
static void report_error(uint16_t id) { ++errors; printf("    command %d FAILED\n", id); }
static void complete_request(uint16_t id) { printf("    command %d done: wake its thread\n", id); }

static int reap(QueuePair *q) {               /* interrupt handler or poll */
    int done = 0;
    while (q->cq_head != q->cq_tail) {        /* real driver: phase bit */
        const CompletionEntry *c = &q->cq[q->cq_head];
        if ((c->status >> 1) != 0) report_error(c->command_id);
        complete_request(c->command_id);      /* wake the waiting thread */
        q->cq_head = next(q->cq_head); ++done;
    }
    q->cq_doorbell = q->cq_head; ++q->doorbell_writes;  /* one MMIO write */
    return done;
}

static void state(const char *when, const QueuePair *q) {
    printf("  %-26s sq_tail %d  sq_head %d  cq_tail %d  cq_head %d  doorbell writes %d\n",
           when, q->sq_tail, q->sq_head, q->cq_tail, q->cq_head, q->doorbell_writes);
}

int main(void) {
    static QueuePair q;
    printf("round 1: four reads, one doorbell each way\n");
    for (uint64_t i = 0; i < 4; ++i) submit(&q, 0x10 + i, i * kPage);
    state("after 4 x submit", &q);
    ring_sq_doorbell(&q);
    state("after SQ doorbell", &q);
    controller_run(&q);
    state("after the controller", &q);
    printf("  MSI-X interrupt: the handler reaps\n");
    const int done = reap(&q);
    state("after reap", &q);
    printf("  %d commands, %d doorbell writes\n\n", done, q.doorbell_writes);

    printf("round 2: fill the ring while the controller sleeps\n");
    int accepted = 0;
    while (submit(&q, 0x100, 0)) ++accepted;
    state("after the refused submit", &q);
    printf("  accepted %d of %d: full when tail + 1 == head, one slot stays empty\n", accepted, kSize);
    ring_sq_doorbell(&q);
    controller_run(&q);
    const int done2 = reap(&q);
    state("after doorbell, run, reap", &q);
    printf("  %d more commands with 2 more doorbell writes; errors: %d\n", done2, errors);
    return done == 4 && accepted == kSize - 1 && done2 == accepted && q.doorbell_writes == 4 && errors == 0 ? 0 : 1;
}
