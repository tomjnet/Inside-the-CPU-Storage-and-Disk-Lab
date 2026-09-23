// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 6: first read against second read
// Build: make 06_read_twice
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <cstdio>
#include <filesystem>
#include <fstream>
#endif

static const std::size_t kSize = std::size_t{32} << 20;   // 32 MiB

// A plain stopwatch, not the repeating harness: here the second run IS the experiment.
template <class F>
static double ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

#if defined(__linux__)
static bool write_all(int fd, const std::vector<char>& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// slide 5: on the device and not in the page cache
static bool make_cold_file(const char* path, const std::vector<char>& d) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return false;
    bool ok = write_all(fd, d);
    ok = ok && fsync(fd) == 0;
    ok = ok && posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

// the whole file with 1 MiB read calls: 32 system calls
void read_all(const char* path, std::vector<char>& buf) {
    int fd = open(path, O_RDONLY);
    while (read(fd, buf.data(), buf.size()) > 0) {}  // copy to buf
    close(fd);
}
#else
// The portable version of the same loop: std::ifstream ends in the read call of the operating system too.
static void read_all_portable(const std::string& path, std::vector<char>& buf) {
    std::ifstream in(path, std::ios::binary);
    while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {}
}
#endif

static void report(double cold, double warm) {
    const double mib = static_cast<double>(kSize >> 20);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  first read:  " << std::setw(8) << cold << " ms  " << std::setw(9) << mib / (cold / 1000.0) << " MiB/s\n";
    std::cout << "  second read: " << std::setw(8) << warm << " ms  " << std::setw(9) << mib / (warm / 1000.0) << " MiB/s\n";
    std::cout << "  ratio on this machine: " << cold / warm << "x\n";
}

int main() {
    std::vector<char> data(kSize);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);
    std::vector<char> buf(std::size_t{1} << 20);

#if defined(__linux__)
    const std::string file = "/tmp/pagecache_lab_06_" + std::to_string(getpid()) + ".bin";
    const char* path = file.c_str();
    if (!make_cold_file(path, data)) {
        std::cout << "could not create the cold file: " << std::strerror(errno) << '\n';
        unlink(path);
        return 0;
    }

    // first: misses, the device works; second: hits, a memcpy
    double cold = ms([&] { read_all(path, buf); });
    double warm = ms([&] { read_all(path, buf); });
    unlink(path);

    std::cout << "32 MiB read twice with 32 read calls of 1 MiB (this machine):\n";
    report(cold, warm);
    std::cout << "same code, same system calls: the second time the pages were in the page cache\n";
    std::cout << "inside a virtual machine or WSL the gap can shrink: the host caches the virtual disk too\n";
#else
    const std::string file = (std::filesystem::temp_directory_path() / "pagecache_lab_06.bin").string();
    {
        std::ofstream out(file, std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    double cold = ms([&] { read_all_portable(file, buf); });
    double warm = ms([&] { read_all_portable(file, buf); });
    std::remove(file.c_str());

    std::cout << "32 MiB read twice with std::ifstream (this machine):\n";
    report(cold, warm);
    std::cout << "this is not Linux: both reads are probably hits, because the file was just written and portable\n"
                 "C++ cannot evict it. The Linux build drops it with posix_fadvise and shows a real miss.\n";
#endif
    return 0;
}
