// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 8: mmap: a window into the page cache
// Build: make 08_mmap_cache
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const std::size_t kSize = std::size_t{32} << 20;   // 32 MiB = 8192 pages

// The answer both paths must give: one byte per page, added up.
static std::uint64_t expected_sum(const std::vector<char>& data) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < data.size(); i += 4096) sum += static_cast<unsigned char>(data[i]);
    return sum;
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

static long minor_faults() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_minflt;
}
#endif

int main() {
    std::vector<char> data(kSize);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);
    const std::uint64_t expected = expected_sum(data);
    std::cout << "one byte from each of " << kSize / 4096 << " pages, expected sum " << expected << '\n';

#if defined(__linux__)
    const std::string file = "/tmp/pagecache_lab_08_" + std::to_string(getpid()) + ".bin";
    const char* path = file.c_str();
    int wfd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    const bool written = wfd >= 0 && write_all(wfd, data);   // the pages are in the page cache now
    if (wfd >= 0) close(wfd);
    if (!written) {
        std::cout << "could not create the file under /tmp: " << std::strerror(errno) << '\n';
        unlink(path);
        return 0;
    }

    // map the file: the pointer names the page cache pages themselves
    int fd = open(path, O_RDONLY);
    void* p = mmap(nullptr, kSize, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        std::cout << "mmap refused: " << std::strerror(errno) << '\n';
        close(fd);
        unlink(path);
        return 0;
    }
    const long faults_before = minor_faults();
    const char* bytes = static_cast<const char*>(p);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kSize; i += 4096)       // one per page
        sum += static_cast<unsigned char>(bytes[i]);    // fault, no copy
    const long faults = minor_faults() - faults_before;
    munmap(p, kSize);
    close(fd);
    unlink(path);

    std::cout << "sum through the mapping: " << sum << (sum == expected ? "  (matches)" : "  (MISMATCH)") << '\n';
    std::cout << "minor page faults in the loop: " << faults << " for " << kSize / 4096 << " pages\n";
    std::cout << "fewer faults than pages is normal: the kernel maps the cached neighbours of a page in the same fault\n";
    std::cout << "no read call, no user buffer: the loop read the page cache pages in place\n";
    return sum == expected ? 0 : 1;
#else
    std::cout << "this sample needs Linux: it maps a 32 MiB file with mmap, adds one byte per page through the\n"
                 "pointer, and prints the minor page faults that connected the page cache pages to the process\n";
    return 0;
#endif
}
