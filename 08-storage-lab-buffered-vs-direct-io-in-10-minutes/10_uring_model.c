/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 10: io_uring: two rings shared with the kernel (C version of 10_uring_model.cpp) */
/* Build: make 10_uring_model_c */
/*
 * A model, not the real interface: the real one needs liburing (sudo apt install liburing-dev) or the raw
 * io_uring_setup, io_uring_enter and mmap calls. The shape is the same: two rings, one producer each.
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

/* io_uring model: two rings in memory shared with the kernel */
typedef struct { int opcode, fd; uint64_t off, user_data; } Sqe;
typedef struct { uint64_t user_data; int32_t res; } Cqe;

/* A single producer, single consumer ring: the producer moves the tail, the consumer moves the head.
   C has no templates: one ring type per entry type, the same code in each. */
#define SQ_N 8
#define CQ_N 16
typedef struct { Sqe slots[SQ_N]; uint32_t head, tail; } SqRing;   /* free running counters, index = counter % N */
typedef struct { Cqe slots[CQ_N]; uint32_t head, tail; } CqRing;

static int sq_push(SqRing *r, Sqe x) {
    if (r->tail - r->head == SQ_N) return 0;              /* full: the producer must wait for the consumer */
    r->slots[r->tail % SQ_N] = x;
    ++r->tail;
    return 1;
}
static int sq_pop(SqRing *r, Sqe *out) {                  /* 0 = empty, in place of std::optional */
    if (r->head == r->tail) return 0;
    *out = r->slots[r->head % SQ_N];
    ++r->head;
    return 1;
}
static int cq_push(CqRing *r, Cqe x) {
    if (r->tail - r->head == CQ_N) return 0;
    r->slots[r->tail % CQ_N] = x;
    ++r->tail;
    return 1;
}
static int cq_pop(CqRing *r, Cqe *out) {
    if (r->head == r->tail) return 0;
    *out = r->slots[r->head % CQ_N];
    ++r->head;
    return 1;
}

enum { kRead = 22 };                                       /* IORING_OP_READ in the real interface */
#define FILE_SIZE ((uint64_t)16 * 4096)                            /* the "file" behind file descriptor 3 */
static int g_system_calls = 0;

/* The kernel side of io_uring_enter: consume every submission, do the I/O, post one completion each. */
static int enter(SqRing *sq, CqRing *cq) {
    ++g_system_calls;
    int submitted = 0;
    Sqe s;
    while (sq_pop(sq, &s)) {
        int32_t res = -22;                                 /* -EINVAL for anything the model does not know */
        if (s.opcode == kRead && s.off + 4096 <= FILE_SIZE) res = 4096;
        Cqe c = { s.user_data, res };
        (void)cq_push(cq, c);
        ++submitted;
    }
    printf("  kernel: io_uring_enter consumed %d entries in 1 system call\n", submitted);
    return 1;
}

static void use(const Cqe *c) {
    printf("  completion: user_data %" PRIu64 ", res %d bytes\n", c->user_data, (int)c->res);
}

int main(void) {
    const int fd = 3;

    SqRing sq = {0};   /* application writes the tail, kernel reads */
    CqRing cq = {0};   /* kernel writes the tail, application reads */
    for (uint64_t i = 0; i < 4; ++i) {
        Sqe s = { kRead, fd, i * 4096, i };
        (void)sq_push(&sq, s);
    }
    printf("submission ring: head %u, tail %u (4 entries, 0 system calls so far)\n", (unsigned)sq.head, (unsigned)sq.tail);
    int calls = enter(&sq, &cq);          /* one system call submits all four */
    printf("completion ring: head %u, tail %u\n", (unsigned)cq.head, (unsigned)cq.tail);
    Cqe c;
    while (cq_pop(&cq, &c)) use(&c);      /* completions: zero system calls */

    printf("\nsystem calls for 4 reads of 4 KiB:\n"
           "  pread, one at a time : 4 (and the thread sleeps in each one)\n"
           "  io_uring             : %d (total counted by the model: %d)\n"
           "  io_uring with SQPOLL : 0 (a kernel thread polls the submission ring)\n", calls, g_system_calls);

    /* A full ring is back pressure, not an error: the 9th push into 8 slots is refused. */
    int accepted = 0;
    for (uint64_t i = 0; i < 9; ++i) {
        Sqe s = { kRead, fd, i * 4096, 100 + i };
        accepted += sq_push(&sq, s);
    }
    printf("\n9 pushes into a ring of 8: %d accepted, the application must submit first\n", accepted);

    const int ok = calls == 1 && cq.head == 4 && cq.tail == 4 && accepted == 8;
    if (!ok) printf("FAIL: the ring lost or invented an entry\n");
    return ok ? 0 : 1;
}
