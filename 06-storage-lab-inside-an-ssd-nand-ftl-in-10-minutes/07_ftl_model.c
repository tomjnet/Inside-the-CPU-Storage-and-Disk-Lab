/* Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 7: an ftl model in c++ (C version of 07_ftl_model.cpp) */
/* Build: make 07_ftl_model_c */
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

int main(void) {
    Ftl f = make_ftl();
    printf("geometry: %d erase blocks x %d pages = %d physical pages, the host sees %d LBAs\n",
           BLOCKS, PAGES, BLOCKS * PAGES, LBAS);
    printf("over provisioning: %d spare pages, %d percent of the visible capacity (a real consumer drive is closer to 7)\n",
           BLOCKS * PAGES - LBAS, 100 * (BLOCKS * PAGES - LBAS) / LBAS);
    printf("sizeof(Block) %zu B, sizeof(Ftl) %zu B\n\n", sizeof(Block), sizeof(Ftl));

    printf("factory fresh: every page is Free, every LBA is unmapped\n");
    print_blocks(&f);

    for (int lba = 0; lba < 6; ++lba) write(&f, lba);        /* six host writes: pages fill in order */
    printf("\nafter writing LBA 0 to 5 once each:\n");
    print_blocks(&f);
    for (int lba = 0; lba < 8; ++lba) {
        printf("  map[%d] = ", lba);
        if (f.map[lba] < 0) printf("-1 (unmapped)\n");
        else printf("%d  (block %d, page %d)\n", f.map[lba], f.map[lba] / PAGES, f.map[lba] % PAGES);
    }
    report("six first writes", &f);
    return consistent(&f) ? 0 : 1;
}
