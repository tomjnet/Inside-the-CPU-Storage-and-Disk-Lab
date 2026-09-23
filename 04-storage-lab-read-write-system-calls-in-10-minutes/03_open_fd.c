/* Read and Write in C++: What Happens Behind the System Call - slide 3: the file descriptor (C version of 03_open_fd.cpp) */
/* Build: make 03_open_fd_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

int main(void) {
#if defined(__linux__)
    /* open is a system call too: the kernel hands back a small int */
    int fd = open("/tmp/lab.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }

    /* the fd is an index into the open file table of this process */
    /* 0 = stdin, 1 = stdout, 2 = stderr: the first free one is often 3 */
    printf("fd = %d\n", fd);

    const char msg[] = "hello, disk\n";
    ssize_t n = write(fd, msg, sizeof msg - 1);   /* 1 system call, 1 copy */
    printf("write returned %zd\n", n);            /* offset moved by n */
    close(fd);                                    /* frees the table slot */

    /* the kernel keeps the offset: two writes land one after the other, and the freed slot is reused */
    int again = open("/tmp/lab.bin", O_WRONLY | O_APPEND);
    if (again < 0) { perror("open"); unlink("/tmp/lab.bin"); return 1; }
    printf("\nreopened: fd = %d (the slot that close freed is handed out again)\n", again);
    for (int i = 0; i < 2; ++i) {
        ssize_t m = write(again, msg, sizeof msg - 1);
        long long offset = (long long)lseek(again, 0, SEEK_CUR);
        printf("write returned %zd, file offset is now %lld\n", m, offset);
    }
    close(again);

    /* a closed descriptor is just a number: the kernel refuses it */
    ssize_t bad = write(again, msg, 1);
    printf("write on the closed fd returned %zd: %s\n", bad, bad < 0 ? strerror(errno) : "no error?");

    unlink("/tmp/lab.bin");
    return n == (ssize_t)(sizeof msg - 1) ? 0 : 1;
#else
    printf("this sample needs Linux: open returns a file descriptor (usually 3), write returns 12,\n"
           "and the kernel advances the file offset after every write\n");
    return 0;
#endif
}
