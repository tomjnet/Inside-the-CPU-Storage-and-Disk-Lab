// Read and Write in C++: What Happens Behind the System Call - slide 6: the return value: short writes
// Build: make 06_write_all
#include <cstddef>
#include <cstdio>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// write may take fewer bytes than asked: loop until all are in
bool write_all(int fd, const char* p, size_t left) {
    while (left > 0) {
        ssize_t n = write(fd, p, left);        // 1 system call per turn
        if (n < 0 && errno == EINTR) continue; // a signal: try again
        if (n < 0) return false;               // real error: see errno
        p += n;                                // short write: advance
        left -= static_cast<size_t>(n);
    }
    return true;
}

static char buf[1 << 20];                      // 1 MiB, static storage
#endif

int main() {
    const std::size_t mib = std::size_t{1} << 20;
    const std::size_t pipe_bytes = 64 * 1024;  // the default capacity of a Linux pipe
    std::printf("1 MiB into a 64 KiB pipe: write can take at most %zu of %zu bytes per call,\n"
                "so a loop needs at least %zu turns\n\n", pipe_bytes, mib, mib / pipe_bytes);

#if defined(__linux__)
    // 1. a short write, on purpose: a non blocking pipe that nobody reads
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); return 1; }
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    ssize_t first = write(fds[1], buf, sizeof buf);
    std::printf("pipe: asked for %zu bytes, write returned %zd (a short write, not an error)\n", sizeof buf, first);
    ssize_t second = write(fds[1], buf, sizeof buf);
    std::printf("pipe: asked again, write returned %zd: %s\n", second, second < 0 ? std::strerror(errno) : "accepted more");
    close(fds[0]);
    close(fds[1]);

    // 2. write_all on a regular file: the loop normally turns once
    const char* path = "/tmp/storage_lab_06.bin";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    bool ok = write_all(fd, buf, sizeof buf);
    close(fd);
    struct stat st{};
    if (stat(path, &st) != 0) { perror("stat"); return 1; }
    std::printf("file: write_all returned %s, the file holds %lld bytes\n", ok ? "true" : "false",
                static_cast<long long>(st.st_size));

    // 3. read has the same contract: it may return fewer bytes, and 0 means end of file
    int in = open(path, O_RDONLY);
    if (in < 0) { perror("open"); return 1; }
    static char block[48 * 1024];              // does not divide 1 MiB: the last read is short
    long long total = 0;
    int calls = 0;
    for (;;) {
        ssize_t n = read(in, block, sizeof block);
        ++calls;
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { std::printf("read: call %d returned %zd: end of file\n", calls, n); break; }
        if (static_cast<std::size_t>(n) < sizeof block)
            std::printf("read: call %d returned %zd of %zu bytes asked: a short read\n", calls, n, sizeof block);
        total += n;
    }
    close(in);
    unlink(path);
    std::printf("read: %lld bytes in %d system calls\n", total, calls);
    return ok && first > 0 && static_cast<std::size_t>(total) == mib ? 0 : 1;
#else
    std::printf("this sample needs Linux: a non blocking pipe accepts 65536 of 1048576 bytes (a short write),\n"
                "write_all loops until the file holds all of them, and read returns 0 at end of file\n");
    return 0;
#endif
}
