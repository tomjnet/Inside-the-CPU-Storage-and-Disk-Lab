/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 6: the completion entry: 16 bytes and an interrupt (C version of 06_completion_entry.cpp) */
/* Build: make 06_completion_entry_c */
/* */
/* A model of the NVMe completion queue entry. The program decodes three status words (success, an error, */
/* and an old entry whose phase bit does not match) the way a driver does it: by reading memory only. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>

typedef struct {                    /* 16 bytes */
    uint32_t result;                /* command specific */
    uint32_t reserved;
    uint16_t sq_head;               /* how far the controller has read */
    uint16_t sq_id;                 /* which submission queue */
    uint16_t command_id;            /* matches the submission entry */
    uint16_t status;                /* bit 0: phase, rest: 0 = success */
} CompletionEntry;
_Static_assert(sizeof(CompletionEntry) == 16, "16 bytes");

static void decode(const CompletionEntry *c, unsigned expected_phase) {
    const unsigned phase = c->status & 1u;
    const unsigned code = (unsigned)(c->status >> 1);
    printf("  command %u from SQ %u: phase %u", (unsigned)c->command_id, (unsigned)c->sq_id, phase);
    if (phase != expected_phase) {
        printf(" (expected %u): an old entry, nothing new in the ring\n", expected_phase);
        return;
    }
    printf(", status code %u%s, SQ slots are free up to head %u\n", code,
           code == 0 ? ": success" : ": error, the request fails", (unsigned)c->sq_head);
}

int main(void) {
    printf("sizeof(CompletionEntry) = %zu bytes, %zu of them fit in one 4 KiB page\n\n",
           sizeof(CompletionEntry), 4096 / sizeof(CompletionEntry));

    /* first pass of the ring: the controller writes entries with the phase bit set to 1 */
    const CompletionEntry ok = {0, 0, 3, 1, 42, 1};                              /* code 0, phase 1 */
    const CompletionEntry failed = {0, 0, 4, 1, 43, (uint16_t)((0x281u << 1) | 1u)};  /* code 0x281: a media error, phase 1 */
    const CompletionEntry stale = {0, 0, 0, 1, 7, 0};                            /* phase 0: left over from an earlier pass */

    printf("the driver expects phase 1 on this pass of the ring:\n");
    decode(&ok, 1);
    decode(&failed, 1);
    decode(&stale, 1);

    printf("\nthe phase bit flips on every pass, so new entries are found without reading a device register\n");
    printf("one MSI-X vector per completion queue: the interrupt lands on the core that owns the pair\n");
    return 0;
}
