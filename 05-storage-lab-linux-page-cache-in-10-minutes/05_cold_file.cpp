// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 5: a file that is really cold
// Build: make 05_cold_file
#include <cstddef>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const std::size_t kSize = std::size_t{32} << 20;   // 32 MiB
static const std::size_t kPage = 4096;                    // the unit of the page cache

#if defined(__linux__)
// The short write loop of the previous episode: write may accept fewer bytes than asked.
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

// a file that is on the device and NOT in the page cache
bool make_cold_file(const char* path, const std::vector<char>& d) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return false;
    bool ok = write_all(fd, d);       // 32 MiB: 8192 dirty pages
    ok = ok && fsync(fd) == 0;        // flushed: the pages are clean
    // clean pages can be dropped: one file, no root needed
    ok = ok && posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

// How many pages of the file are in the page cache right now: mincore answers for a mapping of the file.
static long cached_pages(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    void* p = mmap(nullptr, kSize, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return -1;
    std::vector<unsigned char> resident(kSize / kPage);
    long count = -1;
    if (mincore(p, kSize, resident.data()) == 0) {
        count = 0;
        for (unsigned char r : resident) count += r & 1;
    }
    munmap(p, kSize);
    return count;
}

static void read_whole_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    std::vector<char> buf(std::size_t{1} << 20);
    while (read(fd, buf.data(), buf.size()) > 0) {}
    close(fd);
}
#endif

int main() {
    std::vector<char> data(kSize);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);
    std::cout << "file size: " << (data.size() >> 20) << " MiB = " << data.size() / kPage << " pages of 4 KiB\n";

#if defined(__linux__)
    const std::string file = "/tmp/pagecache_lab_05_" + std::to_string(getpid()) + ".bin";
    const char* path = file.c_str();

    if (!make_cold_file(path, data)) {
        std::cout << "could not create the cold file: " << std::strerror(errno) << '\n';
        unlink(path);
        return 0;
    }
    const long after_drop = cached_pages(path);
    read_whole_file(path);
    const long after_read = cached_pages(path);
    unlink(path);

    std::cout << "pages of the file in the page cache\n";
    std::cout << "  after write, fsync and POSIX_FADV_DONTNEED: " << after_drop << '\n';
    std::cout << "  after one read of the whole file:           " << after_read << '\n';
    if (after_drop > 0)
        std::cout << "this filesystem kept some pages: tmpfs lives in the page cache, and the advice is only advice\n";
    else if (after_drop == 0)
        std::cout << "the file was cold: the next read has to go to the device\n";
    else
        std::cout << "mincore is not available here: " << std::strerror(errno) << '\n';
#else
    std::cout << "this sample needs Linux: it writes the file, calls fsync and posix_fadvise(POSIX_FADV_DONTNEED),\n"
                 "then counts the cached pages with mincore: 0 after the advice, 8192 after one read\n";
#endif
    return 0;
}
