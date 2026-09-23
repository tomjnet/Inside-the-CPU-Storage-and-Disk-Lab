// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 7: write against write plus fsync
// Build: make 07_write_fsync
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

template <class F>
static double ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static void line(const char* label, double millis) {
    const double mib = static_cast<double>(kSize >> 20);
    std::cout << "  " << label << std::fixed << std::setprecision(2) << std::setw(9) << millis << " ms  "
              << std::setw(9) << mib / (millis / 1000.0) << " MiB/s\n";
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
#endif

int main() {
    std::vector<char> data(kSize);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);

#if defined(__linux__)
    const std::string base = "/tmp/pagecache_lab_07_" + std::to_string(getpid());
    const std::string f1 = base + "_a.bin", f2 = base + "_b.bin", f3 = base + "_c.bin";
    int fd = open(f1.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    int fd2 = open(f2.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    int fd3 = open(f3.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0 || fd2 < 0 || fd3 < 0) {
        std::cout << "could not create the files under /tmp: " << std::strerror(errno) << '\n';
    } else {
        // write returns when the bytes are in the page cache, not on disk
        double cached = ms([&] {
            write_all(fd, data);                  // 32 MiB at memcpy speed
        });
        // fsync returns when the device says the data is durable
        double durable = ms([&] {
            write_all(fd2, data);                 // the same 8192 pages
            fsync(fd2);                           // waits for the device
        });
        // fdatasync: the same, minus metadata such as the modification time
        double data_only = ms([&] {
            write_all(fd3, data);
            fdatasync(fd3);
        });

        std::cout << "32 MiB written three ways (this machine):\n";
        line("write alone:          ", cached);
        line("write plus fsync:     ", durable);
        line("write plus fdatasync: ", data_only);
        std::cout << "  fsync against write alone on this machine: " << durable / cached << "x\n";
        std::cout << "the first line is the speed of a copy into RAM: it says nothing about the disk\n";
        std::cout << "on tmpfs or under a hypervisor that ignores flushes the three lines come out close\n";
    }
    if (fd >= 0) close(fd);
    if (fd2 >= 0) close(fd2);
    if (fd3 >= 0) close(fd3);
    unlink(f1.c_str());
    unlink(f2.c_str());
    unlink(f3.c_str());
#else
    const std::string file = (std::filesystem::temp_directory_path() / "pagecache_lab_07.bin").string();
    double buffered = ms([&] {
        std::ofstream out(file, std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();   // empties the buffer of the stream into the cache of the operating system: NOT an fsync
    });
    std::remove(file.c_str());

    std::cout << "32 MiB written with std::ofstream (this machine):\n";
    line("write and flush:      ", buffered);
    std::cout << "this is not Linux: std::ofstream::flush hands the bytes to the operating system cache and stops there.\n"
                 "Portable C++ has no fsync; the Linux build times write alone against write plus fsync and fdatasync.\n";
#endif
    return 0;
}
