// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 9: the controller side: fetch, dma, complete
// Build: make 09_controller
//
// The device side of the model. Until the doorbell is rung the controller sees nothing; after it, the
// controller fetches every entry, writes the data into host RAM (the model of the DMA) and posts completions.
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

// Host RAM of the model: four 4 KiB pages. prp1 is the "physical address" of a page, here an offset into it.
constexpr std::uint64_t kPage = 4096;
static std::uint8_t host_ram[4 * kPage];

// What the DMA comment of the slide stands for: the device writes the block into host RAM, no CPU copy.
// The fake NAND returns the low byte of the LBA in every byte of the block.
static void dma_write(std::uint64_t prp1, std::uint64_t lba) {
    std::memset(host_ram + prp1, static_cast<int>(lba & 0xff), kPage);
}

void controller_run(QueuePair& q) {         // the device side
    while (q.sq_head != q.sq_doorbell) {    // fetch by DMA
        const SubmissionEntry& e = q.sq[q.sq_head];
        q.sq_head = next(q.sq_head);
        // DMA: NAND data goes straight to e.prp1, no CPU copy
        dma_write(e.prp1, e.slba);          // (sample only: the model of that transfer)
        q.cq[q.cq_tail] = {0, 0, q.sq_head, 1, e.command_id, 1};
        q.cq_tail = next(q.cq_tail);        // then one MSI-X interrupt
    }
}

static void state(const char* when, const QueuePair& q) {
    std::cout << "  " << std::left << std::setw(26) << when << std::right << " sq_tail " << q.sq_tail << "  sq_head " << q.sq_head
              << "  cq_tail " << q.cq_tail << "  cq_head " << q.cq_head << "  doorbell writes " << q.doorbell_writes << "\n";
}

int main() {
    static QueuePair q{};
    for (std::uint64_t i = 0; i < 3; ++i) submit(q, 0x10 + i, i * kPage);
    std::cout << "3 reads queued, doorbell not rung yet\n";
    controller_run(q);
    state("controller before doorbell", q);
    std::cout << "  the controller does not watch host memory: nothing happened\n\n";

    ring_sq_doorbell(q);
    controller_run(q);
    state("controller after doorbell", q);

    std::cout << "\ncompletion ring, as the controller left it:\n";
    for (Index i = 0; i != q.cq_tail; i = next(i)) {
        const CompletionEntry& c = q.cq[i];
        std::cout << "  cq[" << i << "]: command " << c.command_id << ", sq " << c.sq_id << ", sq_head " << c.sq_head << ", phase "
                  << (c.status & 1) << ", status code " << (c.status >> 1) << "\n";
    }
    std::cout << "\nhost RAM after the DMA writes (first byte of each buffer page, the fake NAND stores the low LBA byte):\n";
    bool ok = true;
    for (std::uint64_t i = 0; i < 3; ++i) {
        const int got = host_ram[i * kPage];
        std::cout << "  page " << i << ": 0x" << std::hex << got << std::dec << "\n";
        ok = ok && got == static_cast<int>(0x10 + i);
    }
    std::cout << "the CPU copied nothing: it only wrote three entries and one doorbell register\n";
    return ok && q.cq_tail == 3 && q.sq_head == 3 ? 0 : 1;
}
