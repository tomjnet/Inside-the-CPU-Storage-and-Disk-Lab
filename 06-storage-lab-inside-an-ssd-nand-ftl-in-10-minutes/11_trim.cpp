// Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 11: trim: tell the drive what you deleted
// Build: make 11_trim
#include <iomanip>
#include <iostream>
#include <random>

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

// ---- slide 11 ---------------------------------------------------------------------------------------------
// TRIM: the filesystem says this LBA holds nothing useful anymore
void trim(Ftl& f, int lba) {
    if (f.map[lba] < 0) return;
    state_of(f, f.map[lba]) = Page::Stale;  // no copy at GC time
    f.map[lba] = -1;                        // reads return zeros
}

void overwrite_odd_lbas(Ftl& f) {                  // the files that are still alive: 1000 random overwrites, fixed seed
    f.host_writes = f.nand_writes = 0;
    std::mt19937 rng(42);
    for (int i = 0; i < 1000; ++i) write(f, 1 + 2 * int(rng() % 8u));
}

int main() {
    Ftl full = make_ftl();
    for (int lba = 0; lba < LBAS; ++lba) write(full, lba);    // a full drive: 16 LBAs of files

    Ftl no_trim = full;                                       // the filesystem deleted the even LBAs, the drive never heard
    overwrite_odd_lbas(no_trim);
    std::cout << "even LBAs deleted by the filesystem, drive not told: the dead pages are copied at every collection\n";
    report("without TRIM", no_trim);

    // delete half of the data, keep overwriting the other half
    for (int lba = 0; lba < LBAS; lba += 2) trim(full, lba);
    // without TRIM the drive keeps moving those dead pages forever
    std::cout << "\nafter TRIM of the even LBAs: map[0] = " << full.map[0] << ", map[1] = " << full.map[1] << "\n";
    print_blocks(full);
    overwrite_odd_lbas(full);
    report("with TRIM", full);
    std::cout << "Linux: sudo fstrim -v /   (or the fstrim.timer of systemd, or the discard mount option)\n";
    return consistent(no_trim) && consistent(full) ? 0 : 1;
}
