// Read and Write in C++: What Happens Behind the System Call - slide 7: lab: one byte per system call
// Build: make 07_byte_at_a_time
// Run:   ./07_byte_at_a_time        timed: 1 warm up and 3 runs
//        ./07_byte_at_a_time once   one untimed pass, for strace (see 10_strace_count.sh)
#include <cstddef>
#include <cstdio>
#include <string>

#if defined(__linux__)
#include <algorithm>
#include <chrono>
#include <vector>

#include <fcntl.h>
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
#endif

int main(int argc, char* argv[]) {
    const bool once = argc > 1 && std::string(argv[1]) == "once";
    const double n_calls = 1048576.0;
    if (!once)
        std::printf("1 MiB at one byte per call is %.0f system calls; at a typical 100 to 300 ns for the mode\n"
                    "switch alone that is %.2f to %.2f s of overhead before write does any work\n\n",
                    n_calls, n_calls * 100e-9, n_calls * 300e-9);

#if defined(__linux__)
    const char* path = "/tmp/storage_lab_07.bin";

    if (once) {                                    // the pass strace counts: no timing loop around it
        static char bytes[1 << 20];
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open"); return 1; }
        std::size_t done = 0;
        while (done < sizeof bytes && write(fd, &bytes[done], 1) == 1) ++done;
        close(fd);
        unlink(path);
        std::printf("once: %zu write calls of 1 byte\n", done);
        return done == sizeof bytes ? 0 : 1;
    }

    // 1 MiB, one byte per write: 1,048,576 system calls
    static char buf[1 << 20];
    auto slow = bench_ns([&] {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        for (size_t i = 0; i < sizeof buf; ++i)
            if (write(fd, &buf[i], 1) != 1) break;   // mode switch each
        close(fd);
    }, 1, 3);                                        // 1 warm up, 3 runs
    // every call: enter the kernel, find the fd, copy 1 byte, return
    double calls = static_cast<double>(sizeof buf);
    std::printf("per call: %.0f ns\n", slow.median_ns / calls);

    std::printf("whole MiB: min %.1f ms, median %.1f ms (this machine)\n", slow.min_ns / 1e6, slow.median_ns / 1e6);
    std::printf("a virtual machine or WSL adds a layer to every kernel entry: take the number on real Linux hardware\n");
    unlink(path);
    return 0;
#else
    std::printf("this sample needs Linux: it times 1,048,576 write(fd, &buf[i], 1) calls and prints the cost per call\n");
    return 0;
#endif
}
