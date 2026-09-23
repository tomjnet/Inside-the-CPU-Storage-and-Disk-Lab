// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 7: buffered against direct: time one write
// Build: make 07_buffered_vs_direct
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static volatile std::uint64_t g_sink = 0;
static void sink(std::uint64_t x) { g_sink = g_sink + x; }   // keeps the result alive: the loop cannot be deleted

struct BenchResult { double min_ns; double median_ns; };

template <class F>
static BenchResult bench_ns(F&& f, int warmup = 3, int repeats = 21) {
    for (int i = 0; i < warmup; ++i) f();                     // warm up: caches, branch predictor, page faults
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return BenchResult{samples.front(), samples[samples.size() / 2]};
}

static void row(const char* name, const BenchResult& r) {
    std::cout << "  " << std::left << std::setw(34) << name << std::right << std::fixed << std::setprecision(0)
              << std::setw(12) << r.min_ns << " ns min" << std::setw(12) << r.median_ns << " ns median\n";
}

#if defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

// A temporary file under /tmp, opened buffered or with O_DIRECT, closed and removed on every way out of main().
struct TmpFile {
    std::string path;
    int fd = -1;
    bool direct = false;
    TmpFile(const char* tag, bool want_direct) : path("/tmp/storage-lab-08-" + std::string(tag) + "-" + std::to_string(getpid())) {
        if (want_direct) {
            fd = open(path.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0600);
            direct = fd >= 0;
            if (direct) {   // some filesystems accept the flag at open and refuse the first transfer: try one
                void* probe = nullptr;
                if (posix_memalign(&probe, 4096, 4096) == 0) {
                    std::memset(probe, 0, 4096);
                    if (pwrite(fd, probe, 4096, 0) != 4096) direct = false;
                    free(probe);
                }
                if (!direct) close(fd);
            }
            if (!direct) std::cout << "O_DIRECT refused here (" << std::strerror(errno) << "): the direct row is buffered too\n";
        }
        if (!direct) fd = open(path.c_str(), O_RDWR | O_CREAT, 0600);
    }
    ~TmpFile() {
        if (fd >= 0) close(fd);
        unlink(path.c_str());
    }
    TmpFile(const TmpFile&) = delete;
    TmpFile& operator=(const TmpFile&) = delete;
};
#endif

int main() {
    constexpr std::size_t kBlock = 4096;

    // Portable floor: a buffered write is at least one copy of the block into the page cache.
    static char src[kBlock];
    static char dst[kBlock];
    std::memset(src, 'B', kBlock);
    auto copy = bench_ns([&] {
        std::memcpy(dst, src, kBlock);
        sink(static_cast<std::uint64_t>(dst[kBlock / 2]));
    });
    std::cout << "one 4 KiB block, this machine (warm up 3, 21 repeats):\n";
    row("memcpy of the block (the floor)", copy);

#if defined(__linux__)
    TmpFile buffered_file("buffered", false);
    TmpFile direct_file("direct", true);
    if (buffered_file.fd < 0 || direct_file.fd < 0) {
        std::cout << "cannot create the files under /tmp: " << std::strerror(errno) << "\n";
        return 1;
    }
    const int fd_buf = buffered_file.fd;
    const int fd_dir = direct_file.fd;
    void* buf = nullptr;
    if (posix_memalign(&buf, kBlock, kBlock) != 0) return 1;
    std::memset(buf, 'D', kBlock);

    // buffered: write() is a copy into the page cache, the device waits
    auto buffered = bench_ns([&] { sink(pwrite(fd_buf, buf, kBlock, 0)); });
    // buffered plus fsync: now the device is inside the measurement
    auto synced = bench_ns([&] {
        sink(pwrite(fd_buf, buf, kBlock, 0));
        sink(fsync(fd_buf));
    });
    // direct: every call is one device write, no dirty page left behind
    auto direct = bench_ns([&] { sink(pwrite(fd_dir, buf, kBlock, 0)); });

    free(buf);
    row("buffered pwrite", buffered);
    row("buffered pwrite + fsync", synced);
    row(direct_file.direct ? "direct pwrite (O_DIRECT)" : "direct pwrite (fell back: buffered)", direct);
    std::cout << std::setprecision(1)
              << "  direct / buffered median        : " << direct.median_ns / buffered.median_ns << "x (this machine)\n"
              << "  fsync  / direct median          : " << synced.median_ns / direct.median_ns << "x (this machine)\n"
              << "direct I/O is not faster than a copy into RAM: it puts the device into every call, so there is\n"
              << "no writeback later and the median sits close to the tail. A virtual disk (WSL, a cloud volume)\n"
              << "shows other numbers than an NVMe drive on real hardware: read your own.\n";
#else
    std::cout << "this sample needs Linux: it times pwrite on a buffered file, pwrite plus fsync, and pwrite on an\n"
              << "O_DIRECT file; typical on NVMe: about a microsecond, then tens to hundreds of microseconds\n";
#endif
    return 0;
}
