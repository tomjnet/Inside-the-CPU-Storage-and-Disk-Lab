// Read and Write in C++: What Happens Behind the System Call - slide 11: flush is not fsync
// Build: make 11_flush_vs_fsync
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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

static int show(const char* path) {                // what another process would see in the file right now
    std::ifstream in(path);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) { std::printf("  file: %s\n", line.c_str()); ++lines; }
    return lines;
}

int main() {
    const std::string file = (std::filesystem::temp_directory_path() / "storage_lab_11.log").string();
    const char* path = file.c_str();
    int expected = 1;

    // portable part: before flush the line lives only in the stream buffer, after flush it is in the file
    {
        std::ofstream out(path);
        out << "order 41 accepted\n";
        std::printf("before flush, the file holds %d lines: the bytes are still in the stream buffer\n", show(path));
        auto t_flush = bench_ns([&] { out.flush(); }, 0, 1);
        std::printf("flush: %.3f ms (this machine): one system call into the page cache, no device wait\n",
                    t_flush.median_ns / 1e6);
        std::printf("after flush, the file holds:\n");
        show(path);
    }

#if defined(__linux__)
    std::printf("\nnow the slide: flush, then write and fsync on a file descriptor\n");
    std::ofstream log(path);
    log << "order 42 accepted\n";
    log.flush();   // user buffer to page cache: survives a crash of
                   // this process, not a power cut
    int fd = open(path, O_WRONLY | O_APPEND);
    if (write(fd, "order 43 accepted\n", 18) != 18) perror("write");
    auto t = bench_ns([&] { if (fsync(fd)) perror("fsync"); }, 0, 1);
    std::printf("fsync: %.2f ms\n", t.median_ns / 1e6);  // device wait
    close(fd);

    log.close();
    expected = 2;
    std::printf("(this machine; typical: 0.1 ms on NVMe, 10 ms or more on a hard disk, close to zero when\n"
                " the temp folder is a tmpfs, because RAM has no device to wait for)\n");
#else
    std::printf("\nthis sample needs Linux for the second half: write on a file descriptor, then fsync(fd),\n"
                "which returns only when the device confirms (typically 0.1 to 10 ms)\n");
#endif

    int lines = show(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return lines == expected ? 0 : 1;
}
