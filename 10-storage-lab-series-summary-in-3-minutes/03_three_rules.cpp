// Inside the CPU: What We Learned About Storage - slide 3: three rules: batch, know your cache, measure the tail
// Build: make 03_three_rules
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

// the harness of the Low Latency C++ Lab: warm up, repeat, keep minimum and median
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

// reads the whole file in 64 KiB pieces and returns the number of bytes it saw
static std::uint64_t read_all(const std::filesystem::path& path) {
    static std::vector<char> buf(64 * 1024);
    std::ifstream in(path, std::ios::binary);
    std::uint64_t bytes = 0;
    while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {
        bytes += static_cast<std::uint64_t>(in.gcount());
    }
    sink(bytes);
    return bytes;
}

// 4 KiB reads at random block offsets, one at a time (queue depth 1): one latency in nanoseconds per read
static std::vector<double> random_read_latencies(const std::filesystem::path& path, std::uint64_t file_bytes,
                                                 int reads) {
    std::vector<double> lat;
    lat.reserve(static_cast<std::size_t>(reads));
    std::ifstream in(path, std::ios::binary);
    std::vector<char> block(4096);
    std::mt19937 rng(12345);                                  // fixed seed: every run does the same work
    const std::uint64_t blocks = file_bytes / 4096;
    for (int i = 0; i < reads && blocks > 0 && in; ++i) {
        const std::uint64_t lba = rng() % blocks;
        auto t0 = std::chrono::steady_clock::now();
        in.seekg(static_cast<std::streamoff>(lba * 4096));
        in.read(block.data(), 4096);
        auto t1 = std::chrono::steady_clock::now();
        sink(static_cast<std::uint64_t>(static_cast<unsigned char>(block[0])));
        lat.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return lat;
}

int main() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "tomjnet_storage_lab_10_three_rules.bin";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cout << "cannot create " << path.string() << ": nothing to measure here\n";
        return 0;
    }

    std::vector<char> data(256 * 1024, 'x');    // 4096 records of 64 B
    auto tiny = bench_ns([&] {                  // rule 1: 4096 system calls
        for (std::size_t i = 0; i < data.size(); i += 64) {
            out.write(&data[i], 64); out.flush(); }
    });
    auto batch = bench_ns([&] {                 // batched: one system call
        out.write(data.data(), 262144); out.flush(); });
    auto warm = bench_ns([&] { read_all(path); });  // rule 2: page cache:
                                                // RAM speed, not the device

    const bool written = static_cast<bool>(out);
    out.close();
    std::error_code ec;
    const std::uint64_t file_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
    const std::uint64_t seen = read_all(path);
    std::vector<double> lat = random_read_latencies(path, file_bytes, 2000);
    if (lat.empty()) lat.push_back(0.0);

    std::sort(lat.begin(), lat.end());          // rule 3: 4 KiB random I/O
    auto p50 = lat[lat.size() / 2];             // at queue depth 1: median
    auto p999 = lat[lat.size() * 999 / 1000];   // and the tail, p99.9
    auto p99 = lat[lat.size() * 99 / 100];

    const std::uint64_t expected = 2ull * 24ull * 262144ull;  // two benchmarks, 3 warm up runs plus 21 repeats each
    std::cout << "file: " << path.string() << ", " << file_bytes / 1024 << " KiB\n\n";

    std::cout << "rule 1: batch system calls (256 KiB per run, this machine)\n";
    std::cout << "  4096 writes of 64 B, flush after each: median " << tiny.median_ns / 1e6 << " ms, min "
              << tiny.min_ns / 1e6 << " ms\n";
    std::cout << "  one write of 256 KiB, one flush:        median " << batch.median_ns / 1e6 << " ms, min "
              << batch.min_ns / 1e6 << " ms\n";
    if (batch.median_ns > 0.0) {
        std::cout << "  ratio: " << tiny.median_ns / batch.median_ns << "x, this machine: the bytes are the same, "
                  << "the mode switches are not\n";
    }

    std::cout << "\nrule 2: know when you are hitting the page cache\n";
    const double mib = static_cast<double>(seen) / (1024.0 * 1024.0);
    std::cout << "  warm read of " << mib << " MiB we just wrote: median " << warm.median_ns / 1e6 << " ms";
    if (warm.median_ns > 0.0) std::cout << ", about " << mib / (warm.median_ns / 1e9) / 1024.0 << " GiB per second";
    std::cout << "\n  that is memory speed, not your SSD: the pages never left RAM\n";
    std::cout << "  to reach the device: O_DIRECT (episode 8) or fio with direct=1 (episode 9)\n";

    std::cout << "\nrule 3: measure tails at the queue depth you run (" << lat.size()
              << " random 4 KiB reads, queue depth 1)\n";
    std::cout << "  p50 " << p50 / 1e3 << " us, p99 " << p99 / 1e3 << " us, p99.9 " << p999 / 1e3 << " us, max "
              << lat.back() / 1e3 << " us, this machine\n";
    if (p50 > 0.0) std::cout << "  p99.9 is " << p999 / p50 << "x the median: the average would hide it\n";
    std::cout << "  honest note: these reads hit the page cache too (rule 2); a cold NVMe read is typically near 100 us\n";

    std::filesystem::remove(path, ec);
    const bool ok = written && file_bytes == expected && seen == expected;
    std::cout << "\n" << (ok ? "every byte written was read back: " : "MISMATCH: expected ") << expected << " bytes\n";
    return ok ? 0 : 1;
}
