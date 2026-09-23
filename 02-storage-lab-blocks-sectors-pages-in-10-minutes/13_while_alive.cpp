// Blocks, Sectors and Pages: How Storage Is Organized - slide 13: thank you
// Build: make 13_while_alive
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// The imaginary "tomjnet.h" of the slide, written out so the program builds.
constexpr std::uint64_t kSector = 512;
constexpr std::uint64_t kBlock = 4096;
constexpr std::uint64_t kPartitionStart = 2048;              // 1 MiB in: aligned
using Lba = std::uint64_t;
struct Topic { std::string name; };

struct Disk {
    std::uint64_t block_size;
    std::vector<Topic> blocks;                               // one topic per filesystem block
    Lba append(Topic t) {                                    // returns the first LBA of the new block
        blocks.push_back(std::move(t));
        return kPartitionStart + (blocks.size() - 1) * (block_size / kSector);
    }
    const Topic& read(Lba lba) const {
        return blocks[static_cast<std::size_t>((lba - kPartitionStart) / (block_size / kSector))];
    }
};

static std::size_t episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Disk format(std::uint64_t block_size) {
    std::cout << "formatted: " << block_size << " byte blocks, partition starts at LBA " << kPartitionStart << '\n';
    return Disk{block_size, {}};
}
static Topic next_storage_topic() {
    static const char* topics[] = {"How Filesystems Work: From File Name to Disk Blocks",
                                   "Read and Write in C++: What Happens Behind the System Call",
                                   "Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately"};
    return Topic{topics[episodes++]};
}
static void learn(const Topic& t) { std::cout << "  learned: " << t.name << '\n'; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    Disk channel = format(kBlock);       // 4 KiB blocks, aligned
    while (alive()) {
        Lba next = channel.append(next_storage_topic());
        std::cout << "block written at LBA " << next << '\n';
        learn(channel.read(next));       // one block at a time
        subscribe();                     // written once, kept forever
    }
    return 0;
}
