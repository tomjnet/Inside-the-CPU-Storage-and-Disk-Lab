// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 9: the application caches itself
// Build: make 09_app_cache
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>

constexpr std::size_t kBlockSize = 4096;
constexpr std::uint64_t kBlocks = 64;          // the "device": a 256 KiB temporary file
using Block = std::array<char, kBlockSize>;

static std::filesystem::path g_device;
static std::uint64_t g_device_reads = 0;

// One read of one block from the device. The portable sample goes through std::ifstream, so the operating
// system still caches it; with an O_DIRECT descriptor (06_aligned_blocks.cpp) this is a real device read.
static Block read_block(std::uint64_t n) {
    Block b{};
    std::ifstream in(g_device, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(n * kBlockSize));
    in.read(b.data(), static_cast<std::streamsize>(b.size()));
    ++g_device_reads;
    return b;
}

// with O_DIRECT nobody caches for us: the application keeps its own
struct BlockCache {
    std::unordered_map<std::uint64_t, Block> blocks;  // by block number
    std::uint64_t hits = 0, misses = 0;
    const Block& get(std::uint64_t n) {
        auto it = blocks.find(n);
        if (it != blocks.end()) { ++hits; return it->second; }  // RAM
        ++misses;                     // one direct read: the device
        return blocks.emplace(n, read_block(n)).first->second;
    }
};

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    g_device = std::filesystem::temp_directory_path() / ("storage-lab-08-cache-" + std::to_string(stamp) + ".dat");
    {
        std::ofstream out(g_device, std::ios::binary);
        for (std::uint64_t n = 0; n < kBlocks; ++n) {
            Block b;
            b.fill(static_cast<char>('A' + n % 26));          // block n is full of one letter
            out.write(b.data(), static_cast<std::streamsize>(b.size()));
        }
    }

    // A database shaped pattern: 8 hot blocks (index roots) take 80 percent of the reads, the rest is uniform.
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<std::uint64_t> hot(0, 7);
    std::uniform_int_distribution<std::uint64_t> any(0, kBlocks - 1);

    BlockCache cache;
    bool ok = true;
    double hit_ns = 0, miss_ns = 0;
    constexpr int kReads = 20000;
    for (int i = 0; i < kReads; ++i) {
        const std::uint64_t n = percent(rng) < 80 ? hot(rng) : any(rng);
        const std::uint64_t before = cache.misses;
        const auto t0 = std::chrono::steady_clock::now();
        const Block& b = cache.get(n);
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        if (cache.misses != before) miss_ns += ns; else hit_ns += ns;
        if (b[0] != static_cast<char>('A' + n % 26) || b[kBlockSize - 1] != b[0]) ok = false;
    }
    std::filesystem::remove(g_device);

    std::cout << std::fixed << std::setprecision(1)
              << kReads << " reads of " << kBlocks << " blocks, 80 percent of them on 8 hot blocks\n"
              << "  hits         : " << cache.hits << "\n"
              << "  misses       : " << cache.misses << " (each one is one device read: " << g_device_reads << ")\n"
              << "  hit rate     : " << 100.0 * static_cast<double>(cache.hits) / kReads << " percent\n"
              << "  cached bytes : " << cache.blocks.size() * kBlockSize << "\n";
    if (cache.hits > 0 && cache.misses > 0)
        std::cout << "  average hit  : " << hit_ns / static_cast<double>(cache.hits) << " ns (this machine)\n"
                  << "  average miss : " << miss_ns / static_cast<double>(cache.misses) << " ns (this machine, through the\n"
                  << "                 operating system cache: a real O_DIRECT miss on NVMe is about 100000 ns)\n";
    std::cout << "without the cache every one of the " << kReads << " reads would have gone to the device\n"
              << "this model never evicts: a real buffer pool has a size limit, an eviction policy (LRU, clock),\n"
              << "its own read ahead and its own batching of dirty blocks\n";
    if (!ok) {
        std::cout << "FAIL: a cached block does not hold the data of its block number\n";
        return 1;
    }
    return 0;
}
