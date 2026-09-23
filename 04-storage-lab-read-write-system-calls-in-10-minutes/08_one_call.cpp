// Read and Write in C++: What Happens Behind the System Call - slide 8: lab: the same bytes in one call
// Build: make 08_one_call
// Run:   ./08_one_call        timed: both versions, then the ratio
//        ./08_one_call once   one untimed write of 1 MiB, for strace
#include <cstddef>
#include <cstdio>
#include <string>

#if defined(__linux__)
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

// slide 6: write may take fewer bytes than asked, so loop until all are in
static bool write_all(int fd, const char* p, size_t left) {
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return false;
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

static char buf[1 << 20];                          // 1 MiB, static storage
#endif

int main(int argc, char* argv[]) {
    const bool once = argc > 1 && std::string(argv[1]) == "once";
    if (!once)
        std::printf("same 1 MiB, same file, same page cache: 1,048,576 system calls against 1\n\n");

#if defined(__linux__)
    const char* path = "/tmp/storage_lab_08.bin";

    if (once) {                                    // the pass strace counts: 1 write for the data, 1 for this line
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open"); return 1; }
        bool ok = write_all(fd, buf, sizeof buf);
        close(fd);
        unlink(path);
        std::printf("once: 1 write call of %zu bytes: %s\n", sizeof buf, ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }

    // slide 7, the slow version, so the ratio below compares two runs of this program
    auto slow = bench_ns([&] {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        for (size_t i = 0; i < sizeof buf; ++i)
            if (write(fd, &buf[i], 1) != 1) break;
        close(fd);
    }, 1, 3);

    // the same 1 MiB in one write: 1 system call, 1 copy of 1 MiB
    auto fast = bench_ns([&] {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (!write_all(fd, buf, sizeof buf)) perror("write");
        close(fd);
    });
    // same bytes, same file, same page cache: only the call count differs
    std::printf("one call : %.2f ms\n", fast.median_ns / 1e6);
    std::printf("per byte : %.2f ms\n", slow.median_ns / 1e6);
    std::printf("ratio    : %.0fx (this machine)\n",
                slow.median_ns / fast.median_ns);

    // correctness, not timing, decides the exit code: the last run must have left exactly 1 MiB
    struct stat st{};
    bool size_ok = stat(path, &st) == 0 && static_cast<std::size_t>(st.st_size) == sizeof buf;
    std::printf("file size after the last run: %lld bytes (%s)\n", static_cast<long long>(st.st_size),
                size_ok ? "all of it arrived" : "WRONG");
    unlink(path);
    return size_ok ? 0 : 1;
#else
    std::printf("this sample needs Linux: it times one write of 1 MiB against 1,048,576 writes of 1 byte\n"
                "and prints the ratio, typically several hundred to more than a thousand\n");
    return 0;
#endif
}
