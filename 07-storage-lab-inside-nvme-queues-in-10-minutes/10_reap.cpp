// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 10: reaping completions
// Build: make 10_reap
//
// The whole round trip of the model: submit four reads, ring once, the controller runs, reap four, ring
// once. Then the ring is filled until submit refuses, to show the full condition (size minus one).
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

static int errors = 0;
static void report_error(std::uint16_t id) { ++errors; std::cout << "    command " << id << " FAILED\n"; }
static void complete_request(std::uint16_t id) { std::cout << "    command " << id << " done: wake its thread\n"; }

int reap(QueuePair& q) {                    // interrupt handler or poll
    int done = 0;
    while (q.cq_head != q.cq_tail) {        // real driver: phase bit
        const CompletionEntry& c = q.cq[q.cq_head];
        if ((c.status >> 1) != 0) report_error(c.command_id);
        complete_request(c.command_id);     // wake the waiting thread
        q.cq_head = next(q.cq_head); ++done;
    }
    q.cq_doorbell = q.cq_head; ++q.doorbell_writes;  // one MMIO write
    return done;
}

static void state(const char* when, const QueuePair& q) {
    std::cout << "  " << std::left << std::setw(26) << when << std::right << " sq_tail " << q.sq_tail << "  sq_head " << q.sq_head
              << "  cq_tail " << q.cq_tail << "  cq_head " << q.cq_head << "  doorbell writes " << q.doorbell_writes << "\n";
}

int main() {
    static QueuePair q{};
    std::cout << "round 1: four reads, one doorbell each way\n";
    for (std::uint64_t i = 0; i < 4; ++i) submit(q, 0x10 + i, i * kPage);
    state("after 4 x submit", q);
    ring_sq_doorbell(q);
    state("after SQ doorbell", q);
    controller_run(q);
    state("after the controller", q);
    std::cout << "  MSI-X interrupt: the handler reaps\n";
    const int done = reap(q);
    state("after reap", q);
    std::cout << "  " << done << " commands, " << q.doorbell_writes << " doorbell writes\n\n";

    std::cout << "round 2: fill the ring while the controller sleeps\n";
    int accepted = 0;
    while (submit(q, 0x100, 0)) ++accepted;
    state("after the refused submit", q);
    std::cout << "  accepted " << accepted << " of " << kSize << ": full when tail + 1 == head, one slot stays empty\n";
    ring_sq_doorbell(q);
    controller_run(q);
    const int done2 = reap(q);
    state("after doorbell, run, reap", q);
    std::cout << "  " << done2 << " more commands with 2 more doorbell writes; errors: " << errors << "\n";
    return done == 4 && accepted == kSize - 1 && done2 == accepted && q.doorbell_writes == 4 && errors == 0 ? 0 : 1;
}
