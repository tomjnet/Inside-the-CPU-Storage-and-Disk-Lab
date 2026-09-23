// Blocks, Sectors and Pages: How Storage Is Organized - slide 10: checking partition alignment
// Build: make 10_partition_alignment
#include <cstdint>
#include <iostream>

#if defined(__linux__)
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#endif

constexpr std::uint64_t kSector = 512;    // logical sector, bytes
constexpr std::uint64_t kBlock  = 4096;   // filesystem block, bytes

// a partition is an LBA range: [start, start + count)
struct Partition { const char* name; std::uint64_t start, count; };

// aligned: its first byte sits on a physical sector boundary
bool is_aligned(const Partition& p, std::uint64_t physical = 4096) {
    return (p.start * kSector) % physical == 0;
}

static void report(const Partition& p, std::uint64_t physical) {
    const std::uint64_t first_byte = p.start * kSector;
    std::cout << "  " << p.name << ": LBA " << p.start << " to " << p.start + p.count - 1 << " ("
              << p.count * kSector / (1024 * 1024) << " MiB), first byte " << first_byte << ", "
              << (is_aligned(p, physical) ? "aligned" : "MISALIGNED") << " to " << physical << '\n';
    // which physical sectors does filesystem block 0 of this partition cover?
    const std::uint64_t first_phys = first_byte / physical;
    const std::uint64_t last_phys = (first_byte + kBlock - 1) / physical;
    if (physical < kBlock || first_phys == last_phys)
        std::cout << "    filesystem block 0 ends on a physical sector boundary: one clean write\n";
    else
        std::cout << "    filesystem block 0 straddles physical sectors " << first_phys << " and " << last_phys
                  << ": two read, modify, write cycles\n";
}

#if defined(__linux__)
static std::uint64_t read_number(const std::filesystem::path& file, std::uint64_t fallback) {
    std::ifstream in(file);
    std::uint64_t value = 0;
    return (in >> value) ? value : fallback;
}

// /sys/block/DEV/PART/start counts 512 byte units, whatever the sector size of the drive is
static void report_this_machine() {
    namespace fs = std::filesystem;
    std::error_code ec;
    int partitions = 0;
    for (fs::directory_iterator it("/sys/block", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string dev = it->path().filename().string();
        if (dev.rfind("loop", 0) == 0 || dev.rfind("ram", 0) == 0) continue;
        const std::uint64_t logical = read_number(it->path() / "queue/logical_block_size", 512);
        const std::uint64_t physical = read_number(it->path() / "queue/physical_block_size", 512);
        std::cout << "  " << dev << ": logical " << logical << ", physical " << physical << '\n';
        std::error_code ec2;
        for (fs::directory_iterator sub(it->path(), ec2), end2; !ec2 && sub != end2; sub.increment(ec2)) {
            const std::string part = sub->path().filename().string();
            std::error_code ec3;
            if (part.rfind(dev, 0) != 0 || !fs::exists(sub->path() / "start", ec3)) continue;
            const Partition p{part.c_str(), read_number(sub->path() / "start", 0),
                              read_number(sub->path() / "size", 1)};
            report(p, physical);
            ++partitions;
        }
    }
    if (ec) std::cout << "  cannot list /sys/block: " << ec.message() << '\n';
    if (partitions == 0)
        std::cout << "  no partitions found here (WSL and containers show whole virtual disks):\n"
                     "  on real hardware every partition prints its start LBA and the verdict\n";
}
#endif

int main() {
    Partition modern{"sda1", 2048, 1048576};  // starts 1 MiB in: aligned
    Partition legacy{"hda1", 63, 1048576};    // old DOS layout: 32256 B
    // legacy: every 4 KiB block straddles two physical sectors,
    // so one block write costs two read, modify, write cycles

    std::cout << "model partitions on a 512e drive (logical 512, physical 4096):\n";
    report(modern, 4096);
    report(legacy, 4096);
    std::cout << "the same legacy partition on a 512n drive (physical 512):\n";
    report(legacy, 512);

    std::cout << "\nthis machine:\n";
#if defined(__linux__)
    report_this_machine();
#else
    std::cout << "  this sample needs Linux: it would read /sys/block/DEV/PART/start of every partition\n"
                 "  and print whether it is aligned to the physical sector of its drive\n";
#endif

    const bool ok = is_aligned(modern) && !is_aligned(legacy) && is_aligned(legacy, 512);
    return ok ? 0 : 1;
}
