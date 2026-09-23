/* How Filesystems Work: From File Name to Disk Blocks - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
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

/* The imaginary tomjnet.h of the slide. C has no std::string: the name lives inside the struct. */
typedef struct { char name[96]; } Topic;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_storage_topic(void) {
    static const char *topics[] = {"Read and Write in C++: What Happens Behind the System Call",
                                   "Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately",
                                   "Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer"};
    Topic t;
    snprintf(t.name, sizeof t.name, "%s", topics[episodes++]);
    return t;
}
static Topic viewer_request(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "viewer request #%d", episodes);
    return t;
}
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::map: a small directory of (name, topic) rows, looked up by name, inserted when missing */
#define LAB_SLOTS 8
typedef struct { char name[16]; Topic topic; } Entry;
typedef struct { Entry rows[LAB_SLOTS]; size_t count; } Directory;

static Topic *lookup(Directory *d, const char *name) {
    for (size_t i = 0; i < d->count; ++i)
        if (strcmp(d->rows[i].name, name) == 0) return &d->rows[i].topic;
    if (d->count == LAB_SLOTS) return NULL;                  /* full */
    Entry *e = &d->rows[d->count++];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->topic.name[0] = '\0';
    return &e->topic;
}
static int cmp_entry(const void *a, const void *b) {
    return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

int main(void) {
    static Directory lab;                      /* name to topic */
    while (alive()) {
        Topic *next = lookup(&lab, "next");
        Topic *request = lookup(&lab, "request");
        if (next == NULL || request == NULL) break;
        *next = next_storage_topic();          /* read and write */
        *request = viewer_request();           /* one lookup per name */
        subscribe();                           /* lifetime benefit */
        printf("  next: %s\n", lookup(&lab, "next")->name);
    }
    printf("the directory of the lab, a name to topic table:\n");
    qsort(lab.rows, lab.count, sizeof lab.rows[0], cmp_entry);  /* std::map iterates in name order */
    for (size_t i = 0; i < lab.count; ++i) printf("  %s -> %s\n", lab.rows[i].name, lab.rows[i].topic.name);
    return 0;
}
