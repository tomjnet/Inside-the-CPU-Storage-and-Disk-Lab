/* How Filesystems Work: From File Name to Disk Blocks - slide 10: hard links with std::filesystem (C version of 10_hard_links.cpp) */
/* Build: make 10_hard_links_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C has no std::filesystem: the same steps with the operating system's own calls.
   Linux: mkdir, link, stat, rmdir. Windows: CreateDirectoryA, CreateHardLinkA,
   GetFileInformationByHandle, RemoveDirectoryA. fopen and remove are standard C on both. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SEP "\\"
static double now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e9 / (double)f.QuadPart;
}
#else
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define SEP "/"
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
#endif

/* the reason of the last refused call: errno on Linux, GetLastError on Windows */
static char g_why[160];
static void refused(const char *what) {
#if defined(_WIN32)
    snprintf(g_why, sizeof g_why, "%s: Windows error %lu", what, (unsigned long)GetLastError());
#else
    snprintf(g_why, sizeof g_why, "%s: %s", what, strerror(errno));
#endif
}

static int make_dir(const char *path) {
#if defined(_WIN32)
    if (CreateDirectoryA(path, NULL)) return 0;
#else
    if (mkdir(path, 0700) == 0) return 0;
#endif
    refused("create_directory");
    return -1;
}
static void remove_dir(const char *path) {
#if defined(_WIN32)
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
}

/* a second name for the same inode */
static int make_hard_link(const char *from, const char *to) {
#if defined(_WIN32)
    if (CreateHardLinkA(to, from, NULL)) return 0;
#else
    if (link(from, to) == 0) return 0;
#endif
    refused("create_hard_link");
    return -1;
}

/* the link count stored in the inode; -1 when it cannot be read */
static long link_count(const char *path) {
#if defined(_WIN32)
    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    BY_HANDLE_FILE_INFORMATION info;
    const BOOL ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    return ok ? (long)info.nNumberOfLinks : -1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_nlink;
#endif
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) { refused("open for writing"); return -1; }
    const size_t n = strlen(text);
    const int ok = fwrite(text, 1, n, f) == n;
    if (fclose(f) != 0 || !ok) { refused("write"); return -1; }
    return 0;
}
static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    fclose(f);
    return n;
}
static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}
static int remove_file(const char *path) {
    if (remove(path) == 0) return 0;
    refused("remove");
    return -1;
}

/* the part after the last separator */
static const char *filename(const char *path) {
    const char *s = strrchr(path, SEP[0]);
    return s ? s + 1 : path;
}

/* Linux only: the inode number behind a name, read with stat(2), the call the stat command makes. */
static void print_inode(const char *p) {
#if defined(__linux__)
    struct stat st;
    if (stat(p, &st) == 0)
        printf("  %s: inode %ju, links %ju, 512 byte blocks %jd\n", filename(p), (uintmax_t)st.st_ino,
               (uintmax_t)st.st_nlink, (intmax_t)st.st_blocks);
#else
    printf("  %s: the inode number needs Linux (stat or ls -i show it)\n", filename(p));
#endif
}

int main(void) {
    /* A private directory under the temporary folder, removed before exit. */
#if defined(_WIN32)
    const char *tmp = getenv("TEMP");
    if (tmp == NULL || tmp[0] == '\0') tmp = ".";
#else
    const char *tmp = "/tmp";
#endif
    char dir[512], a[600], b[600];
    snprintf(dir, sizeof dir, "%s" SEP "tomjnet_fs_lab_%llu", tmp, (unsigned long long)now_ns());
    if (make_dir(dir) != 0) {
        printf("this filesystem refused the demo: %s\n", g_why);
        return 0;
    }
    printf("working in %s\n", dir);

    /* ---- the slide ---- */
    snprintf(a, sizeof a, "%s" SEP "report.txt", dir);
    snprintf(b, sizeof b, "%s" SEP "alias.txt", dir);
    int intact = 0;
    if (write_text(a, "hello filesystem") != 0) goto refused_demo;  /* one inode, one block */
    if (make_hard_link(a, b) != 0) goto refused_demo;              /* second name, same inode */
    printf("%ld bytes, %ld links\n", file_size(b), link_count(a));  /* 16 bytes, 2 links */
    if (remove_file(a) != 0) goto refused_demo;                     /* unlink: links 2 to 1 */
    printf("%d %ld\n", exists(b), link_count(b));                   /* 1 1: the data is alive */
    if (remove_file(b) != 0) goto refused_demo;                     /* links 0: blocks freed */
    /* ---- end of the slide ---- */

    /* The same again, this time looking at the inode numbers and reading the data through the alias. */
    printf("\ntwo names, one inode\n");
    if (write_text(a, "hello filesystem") != 0) goto refused_demo;
    if (make_hard_link(a, b) != 0) goto refused_demo;
    print_inode(a);
    print_inode(b);
    if (link_count(a) != 2)
        printf("  note: this platform cannot read link counts here, so it printed %ld where the slide says 2\n",
               link_count(a));
    if (remove_file(a) != 0) goto refused_demo;
    char text[64] = "";
    FILE *in = fopen(b, "rb");
    if (in == NULL) { refused("open for reading"); goto refused_demo; }
    if (fgets(text, sizeof text, in) == NULL) text[0] = '\0';
    fclose(in);
    text[strcspn(text, "\r\n")] = '\0';
    printf("  report.txt removed, alias.txt still reads: %s\n", text);
    intact = strcmp(text, "hello filesystem") == 0;
    if (remove_file(b) != 0) goto refused_demo;
    printf("  alias.txt removed: the link count reached 0 and the blocks went back to the bitmap\n");
    remove_dir(dir);
    if (!intact) {
        printf("FAIL: the data did not survive the removal of its first name\n");
        return 1;
    }
    return 0;

refused_demo:
    /* A filesystem may refuse hard links (FAT, some network and container mounts): say so and stop cleanly.
       C has no destructor and no remove_all: the cleanup is explicit, one call per name. */
    printf("this filesystem refused the demo: %s\n", g_why);
    printf("on ext4, XFS, Btrfs or NTFS the link count goes 1, 2, 1 and the data outlives its first name\n");
    remove(a);
    remove(b);
    remove_dir(dir);
    return 0;
}
