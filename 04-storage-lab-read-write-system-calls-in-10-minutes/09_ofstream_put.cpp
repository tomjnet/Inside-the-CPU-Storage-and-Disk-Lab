// Read and Write in C++: What Happens Behind the System Call - slide 9: lab: std::ofstream, one byte per put
// Build: make 09_ofstream_put
// Run:   ./09_ofstream_put        timed: put() a byte at a time, then "\n" against std::endl
//        ./09_ofstream_put once   one untimed pass, for strace (see 10_strace_count.sh)
// Portable: std::ofstream buffers in user space on every platform, so this one runs on Windows too.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

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

static char buf[1 << 20];                          // 1 MiB, static storage

int main(int argc, char* argv[]) {
    const bool once = argc > 1 && std::string(argv[1]) == "once";
    const std::string path = (std::filesystem::temp_directory_path() / "storage_lab_09.bin").string();
    std::error_code ec;

    if (once) {                                    // the pass strace counts: no timing loop around it
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            for (std::size_t i = 0; i < sizeof buf; ++i) out.put(buf[i]);
        }
        std::uintmax_t size = std::filesystem::file_size(path, ec);
        std::filesystem::remove(path, ec);
        std::printf("once: %zu put calls, %llu bytes in the file\n", sizeof buf, static_cast<unsigned long long>(size));
        return size == sizeof buf ? 0 : 1;
    }

    // one byte per put(), but put() is a function call, not a system call
    auto buffered = bench_ns([&] {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (size_t i = 0; i < sizeof buf; ++i)
            out.put(buf[i]);     // lands in the stream's user space buffer
    });                          // buffer full or close: one write(2)
    // about 8 KiB per write in libstdc++: about 128 calls, not a million
    std::printf("ofstream : %.2f ms\n", buffered.median_ns / 1e6);

    std::printf("per put  : %.1f ns (this machine): the price of a function call, not of a system call\n",
                buffered.median_ns / static_cast<double>(sizeof buf));
    std::uintmax_t size = std::filesystem::file_size(path, ec);
    std::printf("file size: %llu bytes\n\n", static_cast<unsigned long long>(size));

    // the trap: std::endl is "\n" plus flush, so every line becomes its own system call
    const int lines = 20000;
    auto newline = bench_ns([&] {
        std::ofstream out(path, std::ios::trunc);
        for (int i = 0; i < lines; ++i) out << "order accepted" << '\n';
    }, 1, 5);
    auto with_endl = bench_ns([&] {
        std::ofstream out(path, std::ios::trunc);
        for (int i = 0; i < lines; ++i) out << "order accepted" << std::endl;
    }, 1, 5);
    std::printf("%d log lines ending in '\\n'      : %.2f ms\n", lines, newline.median_ns / 1e6);
    std::printf("%d log lines ending in std::endl : %.2f ms\n", lines, with_endl.median_ns / 1e6);
    std::printf("ratio: %.0fx (this machine): endl flushes, one system call per line\n",
                with_endl.median_ns / newline.median_ns);

    std::filesystem::remove(path, ec);
    return size == sizeof buf ? 0 : 1;
}
