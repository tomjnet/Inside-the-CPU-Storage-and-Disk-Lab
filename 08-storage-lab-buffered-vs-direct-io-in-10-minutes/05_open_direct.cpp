// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 5: open with o_direct, and expect a no
// Build: make 05_open_direct
#include <cstdio>
#include <iostream>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// The same question for any directory: does the filesystem behind it accept O_DIRECT?
static void probe(const std::string& dir) {
    const std::string file = dir + "/storage-lab-08-probe-" + std::to_string(getpid()) + ".dat";
    int fd = open(file.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0600);
    if (fd >= 0) {
        std::cout << "  " << dir << ": O_DIRECT accepted at open\n";
        close(fd);
    } else {
        std::cout << "  " << dir << ": refused, errno " << errno << " (" << std::strerror(errno) << ")\n";
    }
    unlink(file.c_str());
}
#endif

int main() {
    std::cout << "what each path skips:\n"
              << "  buffered I/O : nothing (page cache, kernel, device)\n"
              << "  direct I/O   : the page cache (kernel, device)\n"
              << "  SPDK         : the kernel (NVMe queues polled from user space, device)\n\n";

#if defined(__linux__)
    const std::string name = "/tmp/storage-lab-08-open-" + std::to_string(getpid()) + ".dat";
    const char* path = name.c_str();

    // O_DIRECT: skip the page cache, the device does DMA on our buffer
    int fd = open(path, O_RDWR | O_CREAT | O_DIRECT, 0600);
    bool direct = fd >= 0;
    if (fd < 0 && errno == EINVAL) {
        // tmpfs on older kernels, some network filesystems: refused
        std::printf("O_DIRECT refused: %s\n", std::strerror(errno));
        fd = open(path, O_RDWR | O_CREAT, 0600);   // fall back: buffered
    }
    // cost per call: 1 system call, 0 copies into the page cache

    if (fd < 0) {
        std::printf("open failed: %s\n", std::strerror(errno));
        return 1;
    }
    const int flags = fcntl(fd, F_GETFL);
    std::cout << path << "\n"
              << "  opened with O_DIRECT : " << (direct ? "yes" : "no, fell back to buffered") << "\n"
              << "  O_DIRECT in F_GETFL  : " << ((flags & O_DIRECT) != 0 ? "set" : "not set") << "\n\n";
    close(fd);
    unlink(path);

    std::cout << "the same question for other filesystems of this machine:\n";
    probe("/tmp");
    probe("/dev/shm");   // tmpfs: refused with EINVAL before Linux 6.6, accepted since
    std::cout << "a refusal can also arrive later, at the first read or write: check every return value\n";
#else
    std::cout << "this sample needs Linux: open() with O_DIRECT, the EINVAL answer of a filesystem\n"
              << "that refuses direct I/O, and the fall back to a buffered file descriptor\n";
#endif
    return 0;
}
