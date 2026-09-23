// Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 11: random reads in c++
// Build: make 11_random_read
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// the harness of the Low Latency C++ Lab (only the part this file uses)
static volatile std::uint64_t g_sink = 0;
static void sink(std::uint64_t x) { g_sink = g_sink + x; }   // keeps the result alive: the loop cannot be deleted

static double ns(std::chrono::steady_clock::duration d) {
    return std::chrono::duration<double, std::nano>(d).count();
}

constexpr unsigned kBlocks = 4096;                           // 4096 blocks of 4 KiB: a 16 MiB test file

static double percentile(const std::vector<double>& sorted, double p) {
    double rank = p / 100.0 * double(sorted.size() - 1);
    return sorted[std::size_t(rank)];
}

static void report(const char* label, std::vector<double>& lat_ns) {
    std::sort(lat_ns.begin(), lat_ns.end());
    double mean = std::accumulate(lat_ns.begin(), lat_ns.end(), 0.0) / double(lat_ns.size());
    std::cout << std::fixed << std::setprecision(1);
    std::cout << label << " (this machine, " << lat_ns.size() << " reads of 4 KiB at queue depth 1)\n";
    std::cout << "  p50    " << percentile(lat_ns, 50.0) / 1000.0 << " us\n";
    std::cout << "  p99    " << percentile(lat_ns, 99.0) / 1000.0 << " us\n";
    std::cout << "  p99.9  " << percentile(lat_ns, 99.9) / 1000.0 << " us\n";
    std::cout << "  max    " << lat_ns.back() / 1000.0 << " us\n";
    std::cout << "  mean   " << mean / 1000.0 << " us, so " << 1e9 / mean << " IOPS (1 s / mean latency)\n";
}

#if defined(__linux__)
static std::vector<double> random_reads(int fd, void* buf, int kReads) {
    // 4 KiB random reads: one pread = one system call = one I/O
    std::mt19937 rng(42);                        // same offsets every run
    std::vector<double> lat_ns;                  // one sample per read
    for (int i = 0; i < kReads; ++i) {
        off_t off = off_t(rng() % kBlocks) * 4096;    // block aligned
        auto t0 = std::chrono::steady_clock::now();
        ssize_t n = pread(fd, buf, 4096, off);   // queue depth 1
        auto t1 = std::chrono::steady_clock::now();
        sink(std::uint64_t(n));                  // keep the result alive
        lat_ns.push_back(ns(t1 - t0));           // no copy of the data
    }
    return lat_ns;
}
#else
static std::vector<double> random_reads(std::ifstream& in, char* buf, int kReads) {
    // the same loop with the portable stream: seekg plus read instead of one pread
    std::mt19937 rng(42);
    std::vector<double> lat_ns;
    for (int i = 0; i < kReads; ++i) {
        std::streamoff off = std::streamoff(rng() % kBlocks) * 4096;
        auto t0 = std::chrono::steady_clock::now();
        in.seekg(off);
        in.read(buf, 4096);
        auto t1 = std::chrono::steady_clock::now();
        sink(static_cast<std::uint64_t>(in.gcount()));
        lat_ns.push_back(ns(t1 - t0));
    }
    return lat_ns;
}
#endif

int main() {
    namespace fs = std::filesystem;
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path path = fs::temp_directory_path() / ("tomjnet_storage_lab_09_" + std::to_string(stamp) + ".dat");

    {   // the test file: 16 MiB of a repeating pattern
        std::vector<char> block(4096);
        for (std::size_t i = 0; i < block.size(); ++i) block[i] = static_cast<char>('a' + i % 26);
        std::ofstream out(path, std::ios::binary);
        for (unsigned b = 0; b < kBlocks; ++b) out.write(block.data(), static_cast<std::streamsize>(block.size()));
        out.flush();
        if (!out) {
            std::cout << "cannot write " << path.string() << ": nothing to measure here\n";
            return 0;
        }
    }
    std::cout << "test file: " << path.string() << " (" << fs::file_size(path) / (1024 * 1024) << " MiB)\n\n";
    int status = 0;

#if defined(__linux__)
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cout << "open failed: " << std::strerror(errno) << '\n';
    } else {
        fsync(fd);                                           // the data reaches the device before the direct pass
        static char page_buf[4096];
        for (unsigned b = 0; b < kBlocks; ++b) {             // warm up: every block is in the page cache now
            ssize_t n = pread(fd, page_buf, 4096, off_t(b) * 4096);
            if (n != 4096) status = 1;                       // a short read of our own file is a real error
        }
        std::vector<double> buffered = random_reads(fd, page_buf, 100000);
        report("buffered: page cache hits, a system call plus a copy", buffered);
        close(fd);

        // the same loop with O_DIRECT: every read goes to the device (episode 8: aligned buffer, size and offset)
        int dfd = open(path.c_str(), O_RDONLY | O_DIRECT);
        void* aligned = nullptr;
        if (dfd < 0) {
            std::cout << "\nO_DIRECT refused here: " << std::strerror(errno)
                      << "\non a real filesystem over NVMe you would see about 100 us per read, often 50 to 100\n"
                         "times the buffered median\n";
        } else if (posix_memalign(&aligned, 4096, 4096) != 0) {
            aligned = nullptr;
            std::cout << "\nposix_memalign failed: no direct pass\n";
        } else {
            std::vector<double> direct = random_reads(dfd, aligned, 5000);
            std::cout << '\n';
            report("direct: O_DIRECT, the page cache is bypassed", direct);
            std::cout << "\nbuffered measures RAM, direct measures the device (in a VM or in WSL: a virtual disk).\n"
                         "Take the timings on real Linux hardware.\n";
        }
        std::free(aligned);
        if (dfd >= 0) close(dfd);
    }
#else
    std::cout << "this sample needs Linux: pread, then O_DIRECT to reach the device instead of the page cache.\n"
                 "Here the same random reads go through std::ifstream, so they measure the file cache of the OS.\n\n";
    {
        std::ifstream in(path, std::ios::binary);
        static char page_buf[4096];
        for (unsigned b = 0; b < kBlocks; ++b) in.read(page_buf, 4096);      // warm up: the whole file once
        if (!in) status = 1;                                 // a short read of our own file is a real error
        std::vector<double> buffered = random_reads(in, page_buf, 100000);
        report("buffered: std::ifstream, seekg plus read", buffered);
    }
#endif

    std::error_code ec;
    fs::remove(path, ec);                                    // the temporary file never stays behind
    std::cout << "\nremoved the test file" << (ec ? " (failed)" : "") << ", g_sink = " << g_sink << '\n';
    return status;
}
