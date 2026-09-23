/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 9: the controller side: fetch, dma, complete (C version of 09_controller.cpp) */
/* Build: make 09_controller_c */
/* */
/* The device side of the model. Until the doorbell is rung the controller sees nothing; after it, the */
/* controller fetches every entry, writes the data into host RAM (the model of the DMA) and posts completions. */
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

static void state(const char *when, const QueuePair *q) {
    printf("  %-26s sq_tail %d  sq_head %d  cq_tail %d  cq_head %d  doorbell writes %d\n",
           when, q->sq_tail, q->sq_head, q->cq_tail, q->cq_head, q->doorbell_writes);
}

int main(void) {
    static QueuePair q;
    for (uint64_t i = 0; i < 3; ++i) submit(&q, 0x10 + i, i * kPage);
    printf("3 reads queued, doorbell not rung yet\n");
    controller_run(&q);
    state("controller before doorbell", &q);
    printf("  the controller does not watch host memory: nothing happened\n\n");

    ring_sq_doorbell(&q);
    controller_run(&q);
    state("controller after doorbell", &q);

    printf("\ncompletion ring, as the controller left it:\n");
    for (Index i = 0; i != q.cq_tail; i = next(i)) {
        const CompletionEntry *c = &q.cq[i];
        printf("  cq[%d]: command %d, sq %d, sq_head %d, phase %d, status code %d\n",
               i, c->command_id, c->sq_id, c->sq_head, c->status & 1, c->status >> 1);
    }
    printf("\nhost RAM after the DMA writes (first byte of each buffer page, the fake NAND stores the low LBA byte):\n");
    int ok = 1;
    for (uint64_t i = 0; i < 3; ++i) {
        const int got = host_ram[i * kPage];
        printf("  page %d: 0x%x\n", (int)i, (unsigned)got);
        ok = ok && got == (int)(0x10 + i);
    }
    printf("the CPU copied nothing: it only wrote three entries and one doorbell register\n");
    return ok && q.cq_tail == 3 && q.sq_head == 3 ? 0 : 1;
}
