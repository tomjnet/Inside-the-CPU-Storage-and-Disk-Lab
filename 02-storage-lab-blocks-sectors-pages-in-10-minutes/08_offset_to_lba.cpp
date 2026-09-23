// Blocks, Sectors and Pages: How Storage Is Organized - slide 8: from byte offset to lba and block
// Build: make 08_offset_to_lba
#include <cstdint>
#include <iomanip>
#include <iostream>

constexpr std::uint64_t kSector = 512;    // logical sector, bytes
constexpr std::uint64_t kBlock  = 4096;   // filesystem block, bytes
struct Where { std::uint64_t lba, in_sector, block, in_block; };

// pure arithmetic: no system call, no device access
Where locate(std::uint64_t part_start_lba, std::uint64_t offset) {
    Where w{};
    w.lba       = part_start_lba + offset / kSector;
    w.in_sector = offset % kSector;
    w.block     = offset / kBlock;        // 8 sectors per block
    w.in_block  = offset % kBlock;        // 0 means block aligned
    return w;
}

int main() {
    const std::uint64_t part_start_lba = 2048;   // the usual first partition: 1 MiB into the device
    const std::uint64_t offsets[] = {0, 511, 512, 4095, 4096, 8192, 9216, 10000, 1048576};

    std::cout << "partition starts at LBA " << part_start_lba << " = byte "
              << part_start_lba * kSector << " of the device\n";
    std::cout << "sector " << kSector << " B, block " << kBlock << " B, "
              << kBlock / kSector << " sectors per block\n\n";
    std::cout << std::setw(10) << "offset" << std::setw(8) << "LBA" << std::setw(11) << "in sector"
              << std::setw(8) << "block" << std::setw(10) << "in block" << "  aligned\n";

    bool ok = true;
    for (std::uint64_t offset : offsets) {
        const Where w = locate(part_start_lba, offset);
        std::cout << std::setw(10) << offset << std::setw(8) << w.lba << std::setw(11) << w.in_sector
                  << std::setw(8) << w.block << std::setw(10) << w.in_block << "  "
                  << (w.in_block == 0 ? "block" : (w.in_sector == 0 ? "sector only" : "no")) << '\n';
        // the way back must give the same byte: LBA and remainder rebuild the offset
        const std::uint64_t back = (w.lba - part_start_lba) * kSector + w.in_sector;
        if (back != offset || w.block * kBlock + w.in_block != offset) ok = false;
    }

    const Where hand = locate(part_start_lba, 10000);   // the one the video does by hand
    std::cout << "\noffset 10000: sector " << 10000 / kSector << " of the partition, LBA " << hand.lba
              << ", block " << hand.block << '\n';
    std::cout << (ok ? "round trip check passed\n" : "round trip check FAILED\n");
    return ok ? 0 : 1;
}
