// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 8: submit, then ring the doorbell
// Build: make 08_submit
//
// Submitting is a memory write; the doorbell is the PCIe crossing. The program queues four reads and rings
// once, then does the same with one doorbell per command, and finally fills the ring until submit refuses.
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

bool submit(QueuePair& q, std::uint64_t lba, std::uint64_t buffer) {
    if (next(q.sq_tail) == q.sq_head) return false;  // ring full
    SubmissionEntry& e = q.sq[q.sq_tail];   // plain memory write
    e = SubmissionEntry{}; e.opcode = 0x02; // read
    e.command_id = q.next_id++;
    e.nsid = 1; e.slba = lba; e.prp1 = buffer;
    q.sq_tail = next(q.sq_tail);            // no PCIe traffic yet
    return true;
}
void ring_sq_doorbell(QueuePair& q) {       // one MMIO write
    q.sq_doorbell = q.sq_tail; ++q.doorbell_writes;
}

int main() {
    static QueuePair batched{};
    std::cout << "four reads, one doorbell:\n";
    for (std::uint64_t i = 0; i < 4; ++i) {
        const bool ok = submit(batched, 0x1000 + 8 * i, i * 4096);
        std::cout << "  submit lba " << 0x1000 + 8 * i << ": " << (ok ? "queued" : "ring full") << ", sq_tail " << batched.sq_tail
                  << ", doorbell register still " << batched.sq_doorbell << "\n";
    }
    ring_sq_doorbell(batched);
    std::cout << "  ring: doorbell register " << batched.sq_doorbell << ", doorbell writes " << batched.doorbell_writes << "\n\n";

    static QueuePair eager{};
    for (std::uint64_t i = 0; i < 4; ++i) {
        if (submit(eager, 0x1000 + 8 * i, i * 4096)) ring_sq_doorbell(eager);
    }
    std::cout << "four reads, one doorbell each: doorbell writes " << eager.doorbell_writes << "\n";
    std::cout << "same commands in the ring, " << eager.doorbell_writes / batched.doorbell_writes
              << "x the PCIe crossings: batch when you can\n\n";

    const SubmissionEntry& last = batched.sq[3];
    std::cout << "entry 3 in the ring: opcode " << static_cast<int>(last.opcode) << ", command id " << last.command_id << ", nsid "
              << last.nsid << ", slba " << last.slba << ", prp1 " << last.prp1 << "\n\n";

    int accepted = 4;                        // nobody consumes: the controller of this sample never runs
    while (submit(batched, 0x2000, 0)) ++accepted;
    std::cout << "with no controller the ring of " << kSize << " accepts " << accepted << " commands, then submit returns false\n";
    return accepted == kSize - 1 && batched.doorbell_writes == 1 && eager.doorbell_writes == 4 ? 0 : 1;
}
