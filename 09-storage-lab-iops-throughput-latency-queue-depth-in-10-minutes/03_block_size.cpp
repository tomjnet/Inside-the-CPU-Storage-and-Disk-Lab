// Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 3: block size links iops and throughput
// Build: make 03_block_size
#include <iostream>

int main() {
    std::cout << "typical NVMe orders of magnitude, not measurements\n\n";

    // throughput = IOPS x block size: one device, three block sizes
    struct Job { const char* name; double iops; double block_bytes; };
    const Job jobs[] = {
        {"4 KiB random read ", 500000.0, 4096.0},       // IOPS bound
        {"128 KiB sequential", 25000.0, 131072.0},
        {"1 MiB sequential  ", 3500.0, 1048576.0},      // bandwidth bound
    };
    for (const Job& j : jobs) {
        double mib_s = j.iops * j.block_bytes / (1024.0 * 1024.0);
        std::cout << j.name << "  " << j.iops << " IOPS  "
                  << mib_s << " MiB/s\n";               // no system calls
    }

    // the formula the other way round: the IOPS a throughput target costs at every block size
    const double target_mib_s = 1000.0;
    std::cout << "\nIOPS needed to move " << target_mib_s << " MiB/s:\n";
    for (const Job& j : jobs) {
        double iops = target_mib_s * 1024.0 * 1024.0 / j.block_bytes;
        std::cout << "  at " << j.block_bytes / 1024.0 << " KiB blocks: " << iops << " IOPS\n";
    }
    std::cout << "\nsmall blocks: the fixed cost per I/O is the limit. Large blocks: the link is.\n";
    return 0;
}
