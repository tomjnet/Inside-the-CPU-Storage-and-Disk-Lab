// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 7: a queue pair as two rings
// Build: make 07_queue_pair
//
// The data structure of the model: two arrays, four indexes, two doorbell registers. The program prints
// what one pair costs in memory, who owns each index and how the indexes wrap around.
#include <cstdint>
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

struct CompletionEntry {            // 16 bytes
    std::uint32_t result;           // command specific
    std::uint32_t reserved;
    std::uint16_t sq_head;          // how far the controller has read
    std::uint16_t sq_id;            // which submission queue
    std::uint16_t command_id;       // matches the submission entry
    std::uint16_t status;           // bit 0: phase, rest: 0 = success
};
static_assert(sizeof(CompletionEntry) == 16);

using Index = std::uint16_t;
constexpr Index kSize = 8;          // real queues: up to 65536
Index next(Index i) { return static_cast<Index>((i + 1) % kSize); }
struct QueuePair {                  // one per CPU core: no lock
    SubmissionEntry sq[kSize];      // host writes, controller reads
    CompletionEntry cq[kSize];      // controller writes, host reads
    Index sq_tail = 0, cq_head = 0; // moved by the host
    Index sq_head = 0, cq_tail = 0; // moved by the controller
    Index sq_doorbell = 0, cq_doorbell = 0;   // MMIO registers
    Index next_id = 0;              // command id generator
    int doorbell_writes = 0;        // each one crosses PCIe
};

int main() {
    static QueuePair q{};
    std::cout << "model: " << kSize << " entries per ring, " << kSize - 1 << " usable (full when tail + 1 == head)\n";
    std::cout << "  submission ring: " << sizeof q.sq << " bytes, completion ring: " << sizeof q.cq << " bytes\n";
    std::cout << "  indexes at start: sq_tail " << q.sq_tail << ", sq_head " << q.sq_head << ", cq_tail " << q.cq_tail
              << ", cq_head " << q.cq_head << ", doorbells " << q.sq_doorbell << " and " << q.cq_doorbell << "\n";
    std::cout << "  command ids start at " << q.next_id << ", doorbell writes so far: " << q.doorbell_writes << "\n\n";

    std::cout << "the indexes wrap around: ";
    Index i = 0;
    for (int step = 0; step < 10; ++step) {
        std::cout << i << (step < 9 ? " -> " : "\n");
        i = next(i);
    }

    std::cout << "\nwho writes what (one writer per index, so one core needs no lock):\n";
    std::cout << "  host:       sq entries, sq_tail, cq_head, both doorbell registers\n";
    std::cout << "  controller: cq entries, sq_head, cq_tail\n\n";

    const long long depth = 1024;   // a typical I/O queue depth a Linux driver asks for
    const long long sq_bytes = depth * static_cast<long long>(sizeof(SubmissionEntry));
    const long long cq_bytes = depth * static_cast<long long>(sizeof(CompletionEntry));
    std::cout << "a typical real pair with " << depth << " entries: " << sq_bytes / 1024 << " KiB of submission ring plus "
              << cq_bytes / 1024 << " KiB of completion ring\n";
    std::cout << "one pair per core on a 16 core machine: " << 16 * (sq_bytes + cq_bytes) / 1024 << " KiB of host RAM\n";
    return 0;
}
