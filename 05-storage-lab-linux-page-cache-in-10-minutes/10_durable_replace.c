/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 10: durable replace: fsync where it matters (C version of 10_durable_replace.cpp) */
/* Build: make 10_durable_replace_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* the whole file as a C string in out (at most cap - 1 bytes); an empty string when it cannot be read */
static const char *contents_of(const char *path, char *out, size_t cap) {
    out[0] = '\0';
    FILE *in = fopen(path, "rb");
    if (in == NULL) return out;
    size_t n = fread(out, 1, cap - 1, in);
    out[n] = '\0';
    fclose(in);
    return out;
}

#if defined(__linux__)
static int write_all(int fd, const char *p, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        done += (size_t)n;
    }
    return 1;
}

/* a crash leaves the old file or the new one, never half of each */
static int durable_replace(const char *dir, const char *tmp, const char *dst,
                           const char *data, size_t len) {
    int fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    int ok = fd >= 0 && write_all(fd, data, len) && fsync(fd) == 0;
    if (fd >= 0) close(fd);                 /* the data is durable */
    ok = ok && rename(tmp, dst) == 0;       /* atomic switch of the name */
    int dfd = open(dir, O_RDONLY);          /* the name is metadata of */
    ok = ok && dfd >= 0 && fsync(dfd) == 0; /* the directory: flush it */
    if (dfd >= 0) close(dfd);
    return ok;
}
#else
/* the portable half: a temporary file, then a rename that replaces the old name */
static int replace_file(const char *tmp, const char *dst, const char *data, size_t len) {
    FILE *out = fopen(tmp, "wb");
    if (out == NULL) return 0;
    int ok = fwrite(data, 1, len, out) == len;
    ok = fclose(out) == 0 && ok;
#if defined(_WIN32)
    /* C rename on Windows refuses an existing target: MoveFileEx replaces it */
    ok = ok && MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    ok = ok && rename(tmp, dst) == 0;
#endif
    return ok;
}
#endif

int main(void) {
    const char *v1 = "settings version 1\n";
    const char *v2 = "settings version 2, written to a temporary file first\n";
    char held[256];

#if defined(__linux__)
    char dir[128], tmp[192], dst[192];
    snprintf(dir, sizeof dir, "/tmp/pagecache_lab_10_%ld", (long)getpid());
    snprintf(tmp, sizeof tmp, "%s/settings.conf.tmp", dir);
    snprintf(dst, sizeof dst, "%s/settings.conf", dir);
    if (mkdir(dir, 0700) != 0) {
        printf("could not create %s: %s\n", dir, strerror(errno));
        return 0;
    }

    const int first = durable_replace(dir, tmp, dst, v1, strlen(v1));
    printf("first save:  %s, file holds: %s", first ? "durable" : strerror(errno), contents_of(dst, held, sizeof held));
    const int second = durable_replace(dir, tmp, dst, v2, strlen(v2));
    printf("second save: %s, file holds: %s", second ? "durable" : strerror(errno), contents_of(dst, held, sizeof held));

    const int replaced = strcmp(contents_of(dst, held, sizeof held), v2) == 0;
    const int tmp_gone = access(tmp, F_OK) != 0;
    printf("temporary file left behind: %s\n", tmp_gone ? "no" : "yes");
    printf("cost per save: two device flushes, one for the data and one for the directory\n");

    unlink(tmp);
    unlink(dst);
    rmdir(dir);
    return (first && second && replaced && tmp_gone) ? 0 : 1;
#else
    const char *tmpdir = getenv("TEMP");
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    char dir[512], tmp[600], dst[600];
    snprintf(dir, sizeof dir, "%s/pagecache_lab_10", tmpdir);
    snprintf(tmp, sizeof tmp, "%s/settings.conf.tmp", dir);
    snprintf(dst, sizeof dst, "%s/settings.conf", dir);
#if defined(_WIN32)
    (void)_mkdir(dir);               /* it may already exist: that is fine */
#else
    (void)mkdir(dir, 0700);
#endif

    int replaced = 1;
    const char *versions[2] = { v1, v2 };
    for (int i = 0; i < 2; ++i) {
        replaced = replace_file(tmp, dst, versions[i], strlen(versions[i])) && replaced;
        replaced = replaced && strcmp(contents_of(dst, held, sizeof held), versions[i]) == 0;
        printf("saved, file holds: %s", contents_of(dst, held, sizeof held));
    }
    remove(tmp);
    remove(dst);
#if defined(_WIN32)
    (void)_rmdir(dir);
#else
    (void)rmdir(dir);
#endif

    printf("this is not Linux: the temporary file and the rename are portable, the durability is not.\n"
           "The Linux build adds fsync on the file and on the directory, the two calls that reach the device.\n");
    return replaced ? 0 : 1;
#endif
}
