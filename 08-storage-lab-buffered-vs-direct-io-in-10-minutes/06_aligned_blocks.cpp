// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 6: aligned buffers, 4 kib blocks
// Build: make 06_aligned_blocks
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

// The arithmetic of the contract is portable: a value is aligned when the remainder is zero.
static bool aligned(std::uint64_t value, std::uint64_t block) { return value % block == 0; }

#if defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// A temporary file under /tmp that is closed and removed on every way out of main().
struct TmpFile {
    std::string path;
    int fd = -1;
    bool direct = false;
    explicit TmpFile(const char* tag) : path("/tmp/storage-lab-08-" + std::string(tag) + "-" + std::to_string(getpid())) {
        fd = open(path.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0600);
        direct = fd >= 0;
        if (direct) {   // some filesystems accept the flag at open and refuse the first transfer: try one
            void* probe = nullptr;
            if (posix_memalign(&probe, 4096, 4096) == 0) {
                std::memset(probe, 0, 4096);
                if (pwrite(fd, probe, 4096, 0) != 4096) direct = false;
                free(probe);
            }
            if (!direct) close(fd);
        }
        if (!direct) {
            std::cout << "O_DIRECT refused here (" << std::strerror(errno) << "): falling back to buffered\n";
            fd = open(path.c_str(), O_RDWR | O_CREAT, 0600);
        }
    }
    ~TmpFile() {
        if (fd >= 0) close(fd);
        unlink(path.c_str());
    }
    TmpFile(const TmpFile&) = delete;
    TmpFile& operator=(const TmpFile&) = delete;
};
#endif

int main() {
    std::cout << "the contract of O_DIRECT, as arithmetic (block = 4096):\n";
    const std::uint64_t offsets[] = {0, 4096, 6000, 8192, 12288 + 512};
    for (std::uint64_t off : offsets)
        std::cout << "  offset " << off << (aligned(off, 4096) ? " : aligned" : " : NOT a multiple of 4096")
                  << ", block index " << off / 4096 << ", byte " << off % 4096 << " inside it\n";
    std::cout << "\n";

#if defined(__linux__)
    TmpFile file("blocks");
    if (file.fd < 0) {
        std::cout << "cannot create " << file.path << ": " << std::strerror(errno) << "\n";
        return 1;
    }
    const int fd = file.fd;

    constexpr std::size_t kBlock = 4096;   // multiple of the sector size
    void* buf = nullptr;                   // address aligned to 4096
    if (posix_memalign(&buf, kBlock, kBlock) != 0) return 1;
    std::memset(buf, 'D', kBlock);
    for (off_t i = 0; i < 16; ++i)         // offset and length aligned too
        if (pwrite(fd, buf, kBlock, i * 4096) != 4096) return 1;
    ssize_t n = pread(fd, buf, kBlock, 3 * 4096);  // from the device
    ssize_t bad = pread(fd, buf, kBlock, 6000);    // offset not aligned
    int why = errno;                       // EINVAL on ext4 and XFS
    free(buf);

    std::cout << "file descriptor is " << (file.direct ? "direct (O_DIRECT)" : "buffered (fall back)") << "\n"
              << "16 blocks of " << kBlock << " bytes written with pwrite\n"
              << "pread of block 3       : " << n << " bytes\n";
    if (bad < 0)
        std::cout << "pread at offset 6000   : -1, errno " << why << " (" << std::strerror(why) << ")\n";
    else
        std::cout << "pread at offset 6000   : " << bad << " bytes, so this path accepts a misaligned offset\n"
                  << "                         (a buffered descriptor always does; ext4 and XFS with O_DIRECT answer EINVAL)\n";

    // Third rule: the address of the buffer. One byte past an aligned address is as misaligned as it gets.
    alignas(4096) static char block[2 * 4096];
    const auto addr = reinterpret_cast<std::uintptr_t>(static_cast<void*>(block));
    std::cout << "static buffer address  : " << addr % 4096 << " past a 4096 boundary\n";
    const ssize_t odd = pread(fd, block + 1, kBlock, 0);
    const int odd_why = errno;
    if (odd < 0)
        std::cout << "pread into address + 1 : -1, errno " << odd_why << " (" << std::strerror(odd_why) << ")\n";
    else
        std::cout << "pread into address + 1 : " << odd << " bytes (newer kernels only ask for the DMA alignment of the\n"
                  << "                         device, often less than a block: do not rely on it, align to 4096)\n"
                  << "first byte read back   : '" << block[1] << "' (D is what we wrote)\n";
#else
    std::cout << "this sample needs Linux: posix_memalign, pwrite and pread of 4 KiB blocks on an O_DIRECT\n"
              << "file, and EINVAL for the read at offset 6000\n";
#endif
    return 0;
}
