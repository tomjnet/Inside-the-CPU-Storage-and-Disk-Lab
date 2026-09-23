// Read and Write in C++: What Happens Behind the System Call - slide 3: the file descriptor
// Build: make 03_open_fd
#include <cstdio>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

int main() {
#if defined(__linux__)
    // open is a system call too: the kernel hands back a small int
    int fd = open("/tmp/lab.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }

    // the fd is an index into the open file table of this process
    // 0 = stdin, 1 = stdout, 2 = stderr: the first free one is often 3
    std::printf("fd = %d\n", fd);

    const char msg[] = "hello, disk\n";
    ssize_t n = write(fd, msg, sizeof msg - 1);   // 1 system call, 1 copy
    std::printf("write returned %zd\n", n);       // offset moved by n
    close(fd);                                    // frees the table slot

    // the kernel keeps the offset: two writes land one after the other, and the freed slot is reused
    int again = open("/tmp/lab.bin", O_WRONLY | O_APPEND);
    if (again < 0) { perror("open"); return 1; }
    std::printf("\nreopened: fd = %d (the slot that close freed is handed out again)\n", again);
    for (int i = 0; i < 2; ++i) {
        ssize_t m = write(again, msg, sizeof msg - 1);
        long long offset = static_cast<long long>(lseek(again, 0, SEEK_CUR));
        std::printf("write returned %zd, file offset is now %lld\n", m, offset);
    }
    close(again);

    // a closed descriptor is just a number: the kernel refuses it
    ssize_t bad = write(again, msg, 1);
    std::printf("write on the closed fd returned %zd: %s\n", bad, bad < 0 ? std::strerror(errno) : "no error?");

    unlink("/tmp/lab.bin");
    return n == static_cast<ssize_t>(sizeof msg - 1) ? 0 : 1;
#else
    std::printf("this sample needs Linux: open returns a file descriptor (usually 3), write returns 12,\n"
                "and the kernel advances the file offset after every write\n");
    return 0;
#endif
}
