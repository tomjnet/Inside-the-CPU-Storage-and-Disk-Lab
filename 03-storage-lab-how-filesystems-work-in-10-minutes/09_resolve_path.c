/* How Filesystems Work: From File Name to Disk Blocks - slide 9: lab: resolve a path in the model (C version of 09_resolve_path.cpp) */
/* Build: make 09_resolve_path_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t start; uint32_t count; } Extent;  /* a run of blocks */

/* C has no std::vector: each inode holds a fixed extent array and its length */
#define MAX_EXTENTS 8
typedef struct {
    uint16_t mode;           /* file or directory, permissions */
    uint16_t links;          /* names that point here; 0 frees it */
    uint64_t size;           /* bytes */
    Extent extents[MAX_EXTENTS];       /* where the data lives */
    size_t n_extents;
} Inode;

#define ROOT ((uint32_t)2)                  /* the root directory is inode 2 on ext4 */
#define FIRST_INODE ((uint32_t)11)          /* ext4 reserves inodes 1 to 10 */
#define BLOCK ((uint64_t)4096)
#define IS_DIR ((uint16_t)0040755)
#define IS_FILE ((uint16_t)0100644)

#define NINODES 32
#define NBLOCKS 64
#define MAX_DIRENTS 32
#define NAME_MAX_LEN 32

/* C has no std::map: the directories are one table of (directory inode, name, inode) rows */
typedef struct { uint32_t dir; uint32_t ino; char name[NAME_MAX_LEN]; int live; } Dirent;

/* The tiny filesystem of slide 8, plus make_dir, rename and the path resolution of this slide. */
typedef struct {
    Inode inodes[NINODES];
    unsigned char used[NBLOCKS];            /* 1 = used */
    Dirent dirents[MAX_DIRENTS];
    uint32_t next_inode;
} TinyFs;

static void fs_init(TinyFs *fs) {
    memset(fs, 0, sizeof *fs);
    for (uint32_t b = 0; b < 4; ++b) fs->used[b] = 1;         /* superblock, two bitmaps, inode table */
    fs->inodes[ROOT].mode = IS_DIR;
    fs->inodes[ROOT].links = 2;
    fs->next_inode = FIRST_INODE;
}
static uint32_t blocks_for(uint64_t n) { return (uint32_t)((n + BLOCK - 1) / BLOCK); }

/* the lookup in one directory: NULL when the name is not there */
static Dirent *dir_find(TinyFs *fs, uint32_t dir, const char *name, size_t len) {
    for (size_t i = 0; i < MAX_DIRENTS; ++i) {
        Dirent *d = &fs->dirents[i];
        if (d->live && d->dir == dir && strlen(d->name) == len && memcmp(d->name, name, len) == 0) return d;
    }
    return NULL;
}
static void dir_set(TinyFs *fs, uint32_t dir, const char *name, uint32_t ino) {
    Dirent *d = dir_find(fs, dir, name, strlen(name));
    for (size_t i = 0; d == NULL && i < MAX_DIRENTS; ++i)
        if (!fs->dirents[i].live) d = &fs->dirents[i];
    if (d == NULL) return;                                    /* table full: the model drops the name */
    d->dir = dir;
    d->ino = ino;
    d->live = 1;
    snprintf(d->name, sizeof d->name, "%s", name);
}

/* slide 6, unchanged */
static Extent fs_alloc(TinyFs *fs, uint32_t want) {
    uint32_t b = 0, n = NBLOCKS;
    while (b < n && fs->used[b]) ++b;          /* first free block */
    Extent e = {b, 0};
    for (; b < n && !fs->used[b] && e.count < want; ++b, ++e.count)
        fs->used[b] = 1;                       /* grow the run */
    return e;        /* shorter than want: the file fragments */
}

/* slide 8, unchanged */
static uint32_t fs_create(TinyFs *fs, uint32_t dir, const char *name, uint64_t n) {
    uint32_t ino = fs->next_inode++;           /* from the inode table */
    Inode *f = &fs->inodes[ino];
    f->links = 1;
    f->size = n;
    for (uint32_t need = blocks_for(n); need > 0 && f->n_extents < MAX_EXTENTS;
         need -= f->extents[f->n_extents - 1].count)
        f->extents[f->n_extents++] = fs_alloc(fs, need);  /* from the block bitmap */
    dir_set(fs, dir, name, ino);               /* the name lives here */
    return ino;
}

/* A directory is an inode too; in this model its rows live in `dirents` instead of in data blocks. */
static uint32_t fs_make_dir(TinyFs *fs, uint32_t dir, const char *name) {
    const uint32_t ino = fs_create(fs, dir, name, 0);
    fs->inodes[ino].mode = IS_DIR;
    fs->inodes[ino].links = 2;                 /* its name in the parent, plus its own "." entry */
    return ino;
}

/* rename = remove one directory entry, add another with the same inode number. Nothing else moves. */
static void fs_rename(TinyFs *fs, uint32_t from, const char *name, uint32_t to, const char *new_name) {
    Dirent *d = dir_find(fs, from, name, strlen(name));
    if (d == NULL) return;
    const uint32_t ino = d->ino;
    d->live = 0;
    dir_set(fs, to, new_name, ino);
}

/* ---- the slide ---- */
/* Path resolution: one directory lookup per component. */
static uint32_t fs_resolve(TinyFs *fs, const char *path) {
    uint32_t ino = ROOT;                         /* inode 2 on ext4 */
    for (const char *p = path; *p != '\0'; ) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len > 0) {                           /* skip the leading slash */
            const Dirent *d = dir_find(fs, ino, p, len);  /* read that directory */
            if (d == NULL) return 0;             /* ENOENT */
            ino = d->ino;                        /* on to the next inode */
        }
        p += len;
        if (*p == '/') ++p;
    }
    return ino;    /* the kernel caches these hops: the dentry cache */
}
/* ---- end of the slide ---- */

static void show(TinyFs *fs, const char *path) {
    const uint32_t ino = fs_resolve(fs, path);
    printf("  %s -> ", path);
    if (ino == 0) {
        printf("0 (ENOENT, no such file or directory)\n");
        return;
    }
    const Inode *f = &fs->inodes[ino];
    printf("inode %" PRIu32 "%s", ino, f->mode == IS_DIR ? " (directory)" : " (file)");
    for (size_t i = 0; i < f->n_extents; ++i)
        printf(" [start %" PRIu32 ", count %" PRIu32 "]", f->extents[i].start, f->extents[i].count);
    printf("\n");
}

int main(void) {
    /* static: the tables stay off the stack */
    static TinyFs fs;
    fs_init(&fs);
    const uint32_t home = fs_make_dir(&fs, ROOT, "home");
    const uint32_t tom = fs_make_dir(&fs, home, "tom");
    fs_make_dir(&fs, home, "ana");
    const uint32_t notes = fs_create(&fs, tom, "notes.md", 3000);
    const uint32_t report = fs_create(&fs, tom, "report.txt", 10000);
    fs.inodes[notes].mode = IS_FILE;
    fs.inodes[report].mode = IS_FILE;

    printf("every hop of /home/tom/report.txt: one directory lookup per component\n");
    show(&fs, "/");
    show(&fs, "/home");
    show(&fs, "/home/tom");
    show(&fs, "/home/tom/report.txt");
    printf("\na name that is not in the directory\n");
    show(&fs, "/home/tom/missing.txt");

    printf("\nrename /home/tom/report.txt to /home/ana/final.txt\n");
    const size_t before_count = fs.inodes[report].n_extents;
    const uint32_t before_start = fs.inodes[report].extents[0].start;
    fs_rename(&fs, tom, "report.txt", fs_resolve(&fs, "/home/ana"), "final.txt");
    show(&fs, "/home/tom/report.txt");
    show(&fs, "/home/ana/final.txt");
    const uint32_t after = fs_resolve(&fs, "/home/ana/final.txt");
    printf("  same inode, same extents: only two directory entries changed, no data block moved\n");

    const int ok = fs_resolve(&fs, "/home/tom") == tom && after == report &&
                   fs_resolve(&fs, "/home/tom/report.txt") == 0 && fs.inodes[after].n_extents == before_count &&
                   fs.inodes[after].extents[0].start == before_start;
    if (!ok) {
        printf("FAIL: path resolution or rename broke the model\n");
        return 1;
    }
    return 0;
}
