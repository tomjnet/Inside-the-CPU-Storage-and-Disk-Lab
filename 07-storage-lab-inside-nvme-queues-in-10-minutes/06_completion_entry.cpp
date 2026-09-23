// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 6: the completion entry: 16 bytes and an interrupt
// Build: make 06_completion_entry
//
// A model of the NVMe completion queue entry. The program decodes three status words (success, an error,
// and an old entry whose phase bit does not match) the way a driver does it: by reading memory only.
#include <cstdint>
#include <iostream>

struct CompletionEntry {            // 16 bytes
    std::uint32_t result;           // command specific
    std::uint32_t reserved;
    std::uint16_t sq_head;          // how far the controller has read
    std::uint16_t sq_id;            // which submission queue
    std::uint16_t command_id;       // matches the submission entry
    std::uint16_t status;           // bit 0: phase, rest: 0 = success
};
static_assert(sizeof(CompletionEntry) == 16);

static void decode(const CompletionEntry& c, unsigned expected_phase) {
    const unsigned phase = c.status & 1u;
    const unsigned code = static_cast<unsigned>(c.status >> 1);
    std::cout << "  command " << c.command_id << " from SQ " << c.sq_id << ": phase " << phase;
    if (phase != expected_phase) {
        std::cout << " (expected " << expected_phase << "): an old entry, nothing new in the ring\n";
        return;
    }
    std::cout << ", status code " << code << (code == 0 ? ": success" : ": error, the request fails")
              << ", SQ slots are free up to head " << c.sq_head << "\n";
}

int main() {
    std::cout << "sizeof(CompletionEntry) = " << sizeof(CompletionEntry) << " bytes, "
              << 4096 / sizeof(CompletionEntry) << " of them fit in one 4 KiB page\n\n";

    // first pass of the ring: the controller writes entries with the phase bit set to 1
    const CompletionEntry ok{0, 0, 3, 1, 42, 1};                  // code 0, phase 1
    const CompletionEntry failed{0, 0, 4, 1, 43, (0x281u << 1) | 1u};  // code 0x281: a media error, phase 1
    const CompletionEntry stale{0, 0, 0, 1, 7, 0};                 // phase 0: left over from an earlier pass

    std::cout << "the driver expects phase 1 on this pass of the ring:\n";
    decode(ok, 1);
    decode(failed, 1);
    decode(stale, 1);

    std::cout << "\nthe phase bit flips on every pass, so new entries are found without reading a device register\n";
    std::cout << "one MSI-X vector per completion queue: the interrupt lands on the core that owns the pair\n";
    return 0;
}
