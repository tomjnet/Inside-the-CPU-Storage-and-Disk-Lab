// Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 10: measuring write amplification
// Build: make 10_write_amplification
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

// ---- the model of slide 7 -------------------------------------------------------------------------------
constexpr int BLOCKS = 6, PAGES = 4, LBAS = 16; // 24 pages, 16 seen
enum class Page { Free, Valid, Stale };
struct Block {
    Page state[PAGES] = {};    // erase resets every page to Free
    int  lba[PAGES] = {};      // reverse map, read by the collector
    int  next = 0, erases = 0; // pages are programmed in order
};
struct Ftl {
    Block nand[BLOCKS];        // the flash: 6 erase blocks of 4 pages
    int   map[LBAS], active = 0;  // LBA to page (-1 none), open block
    long  host_writes = 0, nand_writes = 0;
};

Ftl make_ftl() {                                  // a factory fresh drive: every page erased, nothing mapped
    Ftl f;
    for (int& m : f.map) m = -1;
    return f;
}
Page& state_of(Ftl& f, int ppn) { return f.nand[ppn / PAGES].state[ppn % PAGES]; }
int erased_blocks(const Ftl& f) {                 // blocks that are fully erased and not the open one
    int n = 0;
    for (int i = 0; i < BLOCKS; ++i)
        if (i != f.active && f.nand[i].next == 0) ++n;
    return n;
}
int victim(const Ftl& f) {                        // greedy: most stale pages; ties go to the block with fewer erases
    int best = -1, most = 0;
    for (int i = 0; i < BLOCKS; ++i) {
        if (i == f.active) continue;
        int stale = 0;
        for (Page p : f.nand[i].state)
            if (p == Page::Stale) ++stale;
        bool tie = best >= 0 && stale == most && f.nand[i].erases < f.nand[best].erases;
        if (stale > most || tie) { most = stale; best = i; }
    }
    return best;
}

// ---- slide 12 ---------------------------------------------------------------------------------------------
// dynamic wear leveling: open the erased block with fewest erases
int erased_block(const Ftl& f) {
    int best = -1;
    for (int i = 0; i < BLOCKS; ++i) {
        const Block& b = f.nand[i];
        if (i == f.active || b.next != 0) continue;   // in use
        if (best < 0 || b.erases < f.nand[best].erases) best = i;
    }
    return best;  // cold data never moves: static leveling fixes that
}

// ---- slide 8 (garbage_collect is declared first because write calls it) -------------------------------------
bool garbage_collect(Ftl& f);
void program(Ftl& f, int lba) {     // always the next free page
    if (f.nand[f.active].next == PAGES) f.active = erased_block(f);
    Block& b = f.nand[f.active];
    b.state[b.next] = Page::Valid;  b.lba[b.next] = lba;
    f.map[lba] = f.active * PAGES + b.next++;  ++f.nand_writes;
}
void write(Ftl& f, int lba) {       // one host write, never in place
    ++f.host_writes;
    if (f.map[lba] >= 0) state_of(f, f.map[lba]) = Page::Stale;
    program(f, lba);                // one map update: O(1)
    while (erased_blocks(f) < 2 && garbage_collect(f)) {}
}

// ---- slide 9 ----------------------------------------------------------------------------------------------
// pick the block with the most stale pages, save the rest, erase
bool garbage_collect(Ftl& f) {
    int v = victim(f);                 // most stale, never the active
    if (v < 0) return false;           // nothing stale: nothing to gain
    Block& b = f.nand[v];
    for (int p = 0; p < PAGES; ++p)    // each valid page: 1 NAND write
        if (b.state[p] == Page::Valid) program(f, b.lba[p]);
    int worn = b.erases + 1;
    b = Block{};  b.erases = worn;     // erase: the whole block at once
    return true;
}

// ---- printing and the correctness check --------------------------------------------------------------------
void print_blocks(const Ftl& f) {                 // V7 = valid copy of LBA 7, S7 = stale copy, -- = free page
    for (int i = 0; i < BLOCKS; ++i) {
        const Block& b = f.nand[i];
        std::cout << "  block " << i << " [";
        for (int p = 0; p < PAGES; ++p) {
            if (b.state[p] == Page::Free) std::cout << "  -- ";
            else std::cout << (b.state[p] == Page::Valid ? "  V" : "  S") << std::setw(2) << std::left << b.lba[p] << std::right;
        }
        std::cout << " ]  erases " << b.erases << (i == f.active ? "  (open)" : "") << "\n";
    }
}
bool consistent(const Ftl& f) {                   // every mapped LBA points at a Valid page that carries that LBA
    int valid = 0, mapped = 0;
    for (const Block& b : f.nand)
        for (Page p : b.state)
            if (p == Page::Valid) ++valid;
    for (int lba = 0; lba < LBAS; ++lba) {
        if (f.map[lba] < 0) continue;
        ++mapped;
        const Block& b = f.nand[f.map[lba] / PAGES];
        int p = f.map[lba] % PAGES;
        if (b.state[p] != Page::Valid || b.lba[p] != lba) return false;
    }
    return valid == mapped;
}
double waf(const Ftl& f) { return f.host_writes == 0 ? 0.0 : double(f.nand_writes) / double(f.host_writes); }
void report(const char* name, const Ftl& f) {
    std::cout << "  " << std::setw(24) << std::left << name << std::right << " host " << std::setw(5) << f.host_writes
              << "  NAND " << std::setw(5) << f.nand_writes << "  WAF " << std::fixed << std::setprecision(2) << waf(f)
              << "  erases";
    for (const Block& b : f.nand) std::cout << ' ' << b.erases;
    std::cout << (consistent(f) ? "" : "  MAP BROKEN") << "\n";
}

int main() {
    std::cout << "write amplification factor = NAND writes / host writes (a model with a fixed seed)\n";
    // one hot LBA on an empty drive: victims are fully stale
    Ftl hot = make_ftl();
    for (int i = 0; i < 1000; ++i) write(hot, 7);
    report("one hot LBA", hot);            // WAF 1.00: nothing to move

    // full drive, random overwrites: valid pages ride along
    Ftl full = make_ftl();
    for (int lba = 0; lba < LBAS; ++lba) write(full, lba);
    std::mt19937 rng(42);
    for (int i = 0; i < 1000; ++i) write(full, int(rng() % LBAS));
    report("full, random", full);          // WAF = NAND / host writes

    std::cout << "\nsame random overwrites, fewer LBAs in use (the rest of the drive is spare space):\n";
    bool ok = consistent(hot) && consistent(full);
    for (int used : {4, 8, 12, 16}) {
        Ftl f = make_ftl();
        for (int lba = 0; lba < used; ++lba) write(f, lba);
        f.host_writes = f.nand_writes = 0;                    // count the steady state only, not the first fill
        std::mt19937 r(42);
        for (int i = 0; i < 4000; ++i) write(f, int(r() % unsigned(used)));
        std::string name = std::to_string(used) + " of 16 LBAs in use";
        report(name.c_str(), f);
        ok = ok && consistent(f);
    }
    std::cout << "more free space: emptier victims, fewer copies. A nearly full SSD is slower and wears faster.\n";
    return ok ? 0 : 1;
}
