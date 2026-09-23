// Blocks, Sectors and Pages: How Storage Is Organized - slide 9: a misaligned write touches two blocks
// Build: make 09_misaligned_write
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

constexpr std::uint64_t kBlock = 4096;    // filesystem block, bytes

// blocks a write touches: one device write each, plus one
// read for every block that is only partly overwritten
std::uint64_t blocks_touched(std::uint64_t offset, std::uint64_t len) {
    std::uint64_t first = offset / kBlock;
    std::uint64_t last  = (offset + len - 1) / kBlock;
    return last - first + 1;
}

// A model block device: it can only read or write one whole block, and it counts both.
struct ModelDevice {
    std::vector<unsigned char> media;
    std::uint64_t block_reads = 0;
    std::uint64_t block_writes = 0;

    explicit ModelDevice(std::uint64_t blocks) : media(static_cast<std::size_t>(blocks * kBlock), 0) {}

    // what the owner of the block (page cache or firmware) must do for a byte range
    void write(std::uint64_t offset, const std::vector<unsigned char>& src) {
        const std::uint64_t len = src.size();
        const std::uint64_t first = offset / kBlock;
        const std::uint64_t last = (offset + len - 1) / kBlock;
        for (std::uint64_t b = first; b <= last; ++b) {
            const std::uint64_t lo = std::max(offset, b * kBlock);
            const std::uint64_t hi = std::min(offset + len, (b + 1) * kBlock);
            std::vector<unsigned char> staging(static_cast<std::size_t>(kBlock), 0);
            const auto block_begin = media.begin() + static_cast<std::ptrdiff_t>(b * kBlock);
            if (hi - lo != kBlock) {                              // partly overwritten: read the old block
                std::copy_n(block_begin, static_cast<std::ptrdiff_t>(kBlock), staging.begin());
                ++block_reads;
            }
            std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(lo - offset),   // modify
                        static_cast<std::ptrdiff_t>(hi - lo),
                        staging.begin() + static_cast<std::ptrdiff_t>(lo - b * kBlock));
            std::copy_n(staging.begin(), static_cast<std::ptrdiff_t>(kBlock), block_begin);   // write it back
            ++block_writes;
        }
    }
};

static bool run(const char* label, std::uint64_t offset, std::uint64_t len, std::uint64_t expected_blocks) {
    ModelDevice dev(8);
    std::vector<unsigned char> reference(dev.media.size(), 0);
    std::vector<unsigned char> payload(static_cast<std::size_t>(len), 0xAB);

    dev.write(offset, payload);
    std::fill_n(reference.begin() + static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(len),
                static_cast<unsigned char>(0xAB));

    const std::uint64_t touched = blocks_touched(offset, len);
    std::cout << label << ": " << len << " bytes at offset " << offset << '\n'
              << "  blocks touched " << touched << " (first " << offset / kBlock << ", last "
              << (offset + len - 1) / kBlock << ")\n"
              << "  model device: " << dev.block_reads << " block reads, " << dev.block_writes
              << " block writes, " << (dev.block_reads + dev.block_writes) * kBlock << " bytes moved\n";
    const bool ok = dev.media == reference && touched == expected_blocks && dev.block_writes == touched;
    if (!ok) std::cout << "  CHECK FAILED\n";
    return ok;
}

int main() {
    auto aligned    = blocks_touched(8192, 4096);        // 1: block 2
    auto misaligned = blocks_touched(8192 + 1024, 4096); // 2: blocks 2, 3

    std::cout << "blocks_touched(8192, 4096)        = " << aligned << '\n';
    std::cout << "blocks_touched(8192 + 1024, 4096) = " << misaligned << "\n\n";

    bool ok = aligned == 1 && misaligned == 2;
    ok = run("aligned write", 8192, 4096, 1) && ok;
    ok = run("misaligned write", 8192 + 1024, 4096, 2) && ok;
    ok = run("tiny write inside one block", 8192 + 100, 10, 1) && ok;
    ok = run("large aligned write", 4096, 4 * 4096, 4) && ok;

    std::cout << "\nsame 4096 bytes from the program: the misaligned write costs 2 reads and 2 writes,\n"
                 "the aligned one 0 reads and 1 write\n";
    return ok ? 0 : 1;
}
