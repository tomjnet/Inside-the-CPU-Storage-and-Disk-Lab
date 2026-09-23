// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 4: the submission queue entry: 64 bytes
// Build: make 04_submission_entry
//
// A model of the NVMe submission queue entry (the read and write layout). The program builds one read
// command, prints where every field sits and dumps the 64 bytes the driver would place in the ring.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

struct SubmissionEntry {            // 64 bytes, fixed by the spec
    std::uint8_t  opcode, flags;    // 0x02 read, 0x01 write
    std::uint16_t command_id;       // comes back in the completion
    std::uint32_t nsid;             // namespace: which drive
    std::uint64_t reserved, metadata;
    std::uint64_t prp1, prp2;       // physical addresses of the buffer
    std::uint64_t slba;             // starting LBA
    std::uint32_t nlb;              // low 16 bits: blocks minus 1
    std::uint32_t cdw13_15[3];      // hints, protection info
};
static_assert(sizeof(SubmissionEntry) == 64);  // one cache line

static void field(const char* name, std::size_t offset, std::size_t size) {
    std::cout << "  " << std::left << std::setw(12) << name << " offset " << std::right << std::setw(2) << offset
              << "  size " << std::setw(2) << size << "\n";
}

int main() {
    std::cout << "sizeof(SubmissionEntry) = " << sizeof(SubmissionEntry) << " bytes (one 64 byte cache line)\n";
    field("opcode", offsetof(SubmissionEntry, opcode), sizeof(std::uint8_t));
    field("flags", offsetof(SubmissionEntry, flags), sizeof(std::uint8_t));
    field("command_id", offsetof(SubmissionEntry, command_id), sizeof(std::uint16_t));
    field("nsid", offsetof(SubmissionEntry, nsid), sizeof(std::uint32_t));
    field("reserved", offsetof(SubmissionEntry, reserved), sizeof(std::uint64_t));
    field("metadata", offsetof(SubmissionEntry, metadata), sizeof(std::uint64_t));
    field("prp1", offsetof(SubmissionEntry, prp1), sizeof(std::uint64_t));
    field("prp2", offsetof(SubmissionEntry, prp2), sizeof(std::uint64_t));
    field("slba", offsetof(SubmissionEntry, slba), sizeof(std::uint64_t));
    field("nlb", offsetof(SubmissionEntry, nlb), sizeof(std::uint32_t));
    field("cdw13_15", offsetof(SubmissionEntry, cdw13_15), 3 * sizeof(std::uint32_t));

    // one read: 8 blocks starting at LBA 0x1000 of namespace 1, into the page at physical address 0x7f000
    SubmissionEntry e{};
    e.opcode = 0x02;
    e.command_id = 42;
    e.nsid = 1;
    e.prp1 = 0x7f000;
    e.slba = 0x1000;
    e.nlb = 8 - 1;                  // zero based: 0 means one block

    unsigned char raw[sizeof(SubmissionEntry)];
    std::memcpy(raw, &e, sizeof e);
    std::cout << "\nread of 8 blocks at LBA 0x1000, command id 42, as the controller fetches it:\n";
    for (std::size_t i = 0; i < sizeof raw; ++i) {
        if (i % 16 == 0) std::cout << "  " << std::setw(2) << std::setfill('0') << std::dec << i << ": ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(raw[i]) << ' ';
        if (i % 16 == 15) std::cout << "\n";
    }
    std::cout << std::dec << std::setfill(' ');
    std::cout << "\nno file name, no offset, no inode: by the time a request gets here it is only blocks\n";
    std::cout << "entries in one 4 KiB page: " << 4096 / sizeof(SubmissionEntry) << "\n";
    return 0;
}
