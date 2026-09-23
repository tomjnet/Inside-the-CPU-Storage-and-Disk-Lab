/* Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 4: the submission queue entry: 64 bytes (C version of 04_submission_entry.cpp) */
/* Build: make 04_submission_entry_c */
/* */
/* A model of the NVMe submission queue entry (the read and write layout). The program builds one read */
/* command, prints where every field sits and dumps the 64 bytes the driver would place in the ring. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {                    /* 64 bytes, fixed by the spec */
    uint8_t  opcode, flags;         /* 0x02 read, 0x01 write */
    uint16_t command_id;            /* comes back in the completion */
    uint32_t nsid;                  /* namespace: which drive */
    uint64_t reserved, metadata;
    uint64_t prp1, prp2;            /* physical addresses of the buffer */
    uint64_t slba;                  /* starting LBA */
    uint32_t nlb;                   /* low 16 bits: blocks minus 1 */
    uint32_t cdw13_15[3];           /* hints, protection info */
} SubmissionEntry;
_Static_assert(sizeof(SubmissionEntry) == 64, "one cache line");

static void field(const char *name, size_t offset, size_t size) {
    printf("  %-12s offset %2zu  size %2zu\n", name, offset, size);
}

int main(void) {
    printf("sizeof(SubmissionEntry) = %zu bytes (one 64 byte cache line)\n", sizeof(SubmissionEntry));
    field("opcode", offsetof(SubmissionEntry, opcode), sizeof(uint8_t));
    field("flags", offsetof(SubmissionEntry, flags), sizeof(uint8_t));
    field("command_id", offsetof(SubmissionEntry, command_id), sizeof(uint16_t));
    field("nsid", offsetof(SubmissionEntry, nsid), sizeof(uint32_t));
    field("reserved", offsetof(SubmissionEntry, reserved), sizeof(uint64_t));
    field("metadata", offsetof(SubmissionEntry, metadata), sizeof(uint64_t));
    field("prp1", offsetof(SubmissionEntry, prp1), sizeof(uint64_t));
    field("prp2", offsetof(SubmissionEntry, prp2), sizeof(uint64_t));
    field("slba", offsetof(SubmissionEntry, slba), sizeof(uint64_t));
    field("nlb", offsetof(SubmissionEntry, nlb), sizeof(uint32_t));
    field("cdw13_15", offsetof(SubmissionEntry, cdw13_15), 3 * sizeof(uint32_t));

    /* one read: 8 blocks starting at LBA 0x1000 of namespace 1, into the page at physical address 0x7f000 */
    SubmissionEntry e;
    memset(&e, 0, sizeof e);        /* zero every byte, padding included */
    e.opcode = 0x02;
    e.command_id = 42;
    e.nsid = 1;
    e.prp1 = 0x7f000;
    e.slba = 0x1000;
    e.nlb = 8 - 1;                  /* zero based: 0 means one block */

    unsigned char raw[sizeof(SubmissionEntry)];
    memcpy(raw, &e, sizeof e);
    printf("\nread of 8 blocks at LBA 0x1000, command id 42, as the controller fetches it:\n");
    for (size_t i = 0; i < sizeof raw; ++i) {
        if (i % 16 == 0) printf("  %02zu: ", i);
        printf("%02x ", (unsigned)raw[i]);
        if (i % 16 == 15) printf("\n");
    }
    printf("\nno file name, no offset, no inode: by the time a request gets here it is only blocks\n");
    printf("entries in one 4 KiB page: %zu\n", 4096 / sizeof(SubmissionEntry));
    return 0;
}
