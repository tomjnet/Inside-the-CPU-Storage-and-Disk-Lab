/* Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 9: garbage_collect: move, then erase (C version of 09_garbage_collect.cpp) */
/* Build: make 09_garbage_collect_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
/* ---- the model of slide 7 ------------------------------------------------------------------------------- */
enum { BLOCKS = 6, PAGES = 4, LBAS = 16 };  /* 24 pages, 16 seen */
typedef enum { PAGE_FREE, PAGE_VALID, PAGE_STALE } Page;
typedef struct {
    Page state[PAGES];         /* erase resets every page to Free (PAGE_FREE is 0) */
    int  lba[PAGES];           /* reverse map, read by the collector */
    int  next, erases;         /* pages are programmed in order */
} Block;
typedef struct {
    Block nand[BLOCKS];        /* the flash: 6 erase blocks of 4 pages */
    int   map[LBAS], active;   /* LBA to page (-1 none), open block */
    long  host_writes, nand_writes;
} Ftl;

static Ftl make_ftl(void) {                       /* a factory fresh drive: every page erased, nothing mapped */
    Ftl f = {0};                                  /* C has no default member initializers: zero it all */
    for (int i = 0; i < LBAS; ++i) f.map[i] = -1;
    return f;
}
/* C has no references: return a pointer to the page state */
static Page *state_of(Ftl *f, int ppn) { return &f->nand[ppn / PAGES].state[ppn % PAGES]; }
static int erased_blocks(const Ftl *f) {          /* blocks that are fully erased and not the open one */
    int n = 0;
    for (int i = 0; i < BLOCKS; ++i)
        if (i != f->active && f->nand[i].next == 0) ++n;
    return n;
}
static int victim(const Ftl *f) {                 /* greedy: most stale pages; ties go to the block with fewer erases */
    int best = -1, most = 0;
    for (int i = 0; i < BLOCKS; ++i) {
        if (i == f->active) continue;
        int stale = 0;
        for (int p = 0; p < PAGES; ++p)
            if (f->nand[i].state[p] == PAGE_STALE) ++stale;
        bool tie = best >= 0 && stale == most && f->nand[i].erases < f->nand[best].erases;
        if (stale > most || tie) { most = stale; best = i; }
    }
    return best;
}

/* ---- slide 12 --------------------------------------------------------------------------------------------- */
/* dynamic wear leveling: open the erased block with fewest erases */
static int erased_block(const Ftl *f) {
    int best = -1;
    for (int i = 0; i < BLOCKS; ++i) {
        const Block *b = &f->nand[i];
        if (i == f->active || b->next != 0) continue;   /* in use */
        if (best < 0 || b->erases < f->nand[best].erases) best = i;
    }
    return best;  /* cold data never moves: static leveling fixes that */
}

/* ---- slide 8 (garbage_collect is declared first because write calls it) ------------------------------------- */
static bool garbage_collect(Ftl *f);
static void program(Ftl *f, int lba) {  /* always the next free page */
    if (f->nand[f->active].next == PAGES) f->active = erased_block(f);
    Block *b = &f->nand[f->active];
    b->state[b->next] = PAGE_VALID;  b->lba[b->next] = lba;
    f->map[lba] = f->active * PAGES + b->next++;  ++f->nand_writes;
}
static void write(Ftl *f, int lba) {    /* one host write, never in place */
    ++f->host_writes;
    if (f->map[lba] >= 0) *state_of(f, f->map[lba]) = PAGE_STALE;
    program(f, lba);                    /* one map update: O(1) */
    while (erased_blocks(f) < 2 && garbage_collect(f)) {}
}

/* ---- slide 9 ---------------------------------------------------------------------------------------------- */
/* pick the block with the most stale pages, save the rest, erase */
static bool garbage_collect(Ftl *f) {
    int v = victim(f);                  /* most stale, never the active */
    if (v < 0) return false;            /* nothing stale: nothing to gain */
    Block *b = &f->nand[v];
    for (int p = 0; p < PAGES; ++p)     /* each valid page: 1 NAND write */
        if (b->state[p] == PAGE_VALID) program(f, b->lba[p]);
    int worn = b->erases + 1;
    *b = (Block){0};  b->erases = worn; /* erase: the whole block at once */
    return true;
}

/* ---- printing and the correctness check -------------------------------------------------------------------- */
static void print_blocks(const Ftl *f) {          /* V7 = valid copy of LBA 7, S7 = stale copy, -- = free page */
    for (int i = 0; i < BLOCKS; ++i) {
        const Block *b = &f->nand[i];
        printf("  block %d [", i);
        for (int p = 0; p < PAGES; ++p) {
            if (b->state[p] == PAGE_FREE) printf("  -- ");
            else printf("%s%-2d", b->state[p] == PAGE_VALID ? "  V" : "  S", b->lba[p]);
        }
        printf(" ]  erases %d%s\n", b->erases, i == f->active ? "  (open)" : "");
    }
}
static bool consistent(const Ftl *f) {         /* every mapped LBA points at a Valid page that carries that LBA */
    int valid = 0, mapped = 0;
    for (int i = 0; i < BLOCKS; ++i)
        for (int p = 0; p < PAGES; ++p)
            if (f->nand[i].state[p] == PAGE_VALID) ++valid;
    for (int lba = 0; lba < LBAS; ++lba) {
        if (f->map[lba] < 0) continue;
        ++mapped;
        const Block *b = &f->nand[f->map[lba] / PAGES];
        int p = f->map[lba] % PAGES;
        if (b->state[p] != PAGE_VALID || b->lba[p] != lba) return false;
    }
    return valid == mapped;
}
static double waf(const Ftl *f) { return f->host_writes == 0 ? 0.0 : (double)f->nand_writes / (double)f->host_writes; }
static void report(const char *name, const Ftl *f) {
    printf("  %-24s host %5ld  NAND %5ld  WAF %.2f  erases", name, f->host_writes, f->nand_writes, waf(f));
    for (int i = 0; i < BLOCKS; ++i) printf(" %d", f->nand[i].erases);
    printf("%s\n", consistent(f) ? "" : "  MAP BROKEN");
}

/* a host write without the trigger of slide 8, so that main can run one collection by hand and print around it */
static void write_no_gc(Ftl *f, int lba) {
    ++f->host_writes;
    if (f->map[lba] >= 0) *state_of(f, f->map[lba]) = PAGE_STALE;
    program(f, lba);
}

int main(void) {
    Ftl f = make_ftl();
    for (int lba = 0; lba < LBAS; ++lba) write(&f, lba);      /* fill the drive: blocks 0 to 3 are full of valid data */
    const int overwrites[] = {0, 1, 2, 5};                    /* four overwrites: block 0 gets 3 stale pages, block 1 one */
    for (int i = 0; i < 4; ++i) write_no_gc(&f, overwrites[i]);
    printf("before: block 0 holds the most stale pages, so it is the victim\n");
    print_blocks(&f);
    printf("  victim(f) = %d, erased blocks left = %d\n", victim(&f), erased_blocks(&f));

    int before[LBAS];
    for (int lba = 0; lba < LBAS; ++lba) before[lba] = f.map[lba];
    long nand_before = f.nand_writes;
    bool collected = garbage_collect(&f);

    printf("\nafter one garbage_collect(): %s\n", collected ? "block erased" : "nothing to do");
    print_blocks(&f);
    for (int lba = 0; lba < LBAS; ++lba)
        if (before[lba] != f.map[lba])
            printf("  moved LBA %d: page %d to page %d\n", lba, before[lba], f.map[lba]);
    printf("  extra NAND writes the host never asked for: %ld\n", f.nand_writes - nand_before);
    report("after the collection", &f);

    Ftl clean = make_ftl();
    for (int lba = 0; lba < 4; ++lba) write(&clean, lba);
    bool again = garbage_collect(&clean);
    printf("\na drive with no stale page: garbage_collect() returns %s\n", again ? "true" : "false");
    return collected && !again && consistent(&f) && consistent(&clean) ? 0 : 1;
}
