// How Filesystems Work: From File Name to Disk Blocks - slide 6: free space: the bitmaps
// Build: make 06_alloc_blocks
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using std::uint32_t;

struct Extent { uint32_t start; uint32_t count; };  // a run of blocks

// The part of the tiny filesystem this slide needs: the block bitmap and its allocator.
struct TinyFs {
    std::vector<bool> used;                         // one bit per block: true = used
    Extent alloc(uint32_t want);
};

// ---- the slide ----
// First fit on the block bitmap: one bit per block, O(blocks) scan.
Extent TinyFs::alloc(uint32_t want) {
    uint32_t b = 0, n = uint32_t(used.size());
    while (b < n && used[b]) ++b;              // first free block
    Extent e{b, 0};
    for (; b < n && !used[b] && e.count < want; ++b, ++e.count)
        used[b] = true;                        // grow the run
    return e;        // shorter than want: the file fragments
}
// ---- end of the slide ----

static std::string bits(const TinyFs& fs) {
    std::string s;
    for (std::size_t i = 0; i < fs.used.size(); ++i) {
        if (i > 0 && i % 8 == 0) s += ' ';
        s += fs.used[i] ? '1' : '0';
    }
    return s;
}

int main() {
    // The bitmap of the slide's figure: blocks 0 to 9 and 13 to 19 are used, a hole of 3 at block 10.
    TinyFs fs;
    fs.used.assign(32, false);
    for (uint32_t b = 0; b < 10; ++b) fs.used[b] = true;
    for (uint32_t b = 13; b < 20; ++b) fs.used[b] = true;

    std::cout << "block bitmap, 1 = used, 0 = free (32 blocks, 8 per group)\n";
    std::cout << "  before      " << bits(fs) << "\n";

    // One request for 5 blocks: the caller keeps asking until it is covered, as create() does on slide 8.
    std::vector<Extent> got;
    uint32_t need = 5;
    while (need > 0) {
        Extent e = fs.alloc(need);
        if (e.count == 0) break;                   // disk full: a real filesystem returns ENOSPC
        std::cout << "  alloc(" << need << ") -> extent: start " << e.start << ", count " << e.count << "\n";
        got.push_back(e);
        need -= e.count;
    }
    std::cout << "  after       " << bits(fs) << "\n";
    std::cout << "  one request for 5 blocks came back as " << got.size() << " extents: the file is fragmented\n\n";

    // How far one bitmap block reaches on a real filesystem.
    const std::uint64_t block = 4096;
    const std::uint64_t bits_per_block = block * 8;
    std::cout << "one 4 KiB bitmap block = " << bits_per_block << " bits = "
              << bits_per_block * block / (1024 * 1024) << " MiB of disk (one ext4 block group)\n";

    const bool ok = got.size() == 2 && got[0].start == 10 && got[0].count == 3 && got[1].start == 20 &&
                    got[1].count == 2;
    if (!ok) {
        std::cout << "FAIL: first fit did not return the extents of the slide\n";
        return 1;
    }
    return 0;
}
