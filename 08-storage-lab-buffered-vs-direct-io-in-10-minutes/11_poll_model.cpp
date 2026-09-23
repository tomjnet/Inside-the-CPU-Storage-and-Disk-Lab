// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 11: user space storage: spdk and polling
// Build: make 11_poll_model
//
// A model of what an SPDK style driver does with one NVMe queue pair. No device is touched: the "device" is
// a counter of model time, so the output is the same on every machine.
#include <cstdint>
#include <iomanip>
#include <iostream>

constexpr int kRead = 0x02;                                  // the NVMe read opcode

struct Command { int opcode; std::uint64_t lba; std::uint32_t blocks; };
struct Completion { std::uint16_t command_id; std::uint16_t status; };

struct QueuePair {
    static constexpr std::uint64_t kPollNs = 20;             // model: one trip around the poll loop
    static constexpr std::uint64_t kDeviceNs = 80000;        // model: the flash answers after 80 microseconds
    Command sq_entry{};
    Completion cq_entry{};
    std::uint32_t sq_tail = 0, cq_head = 0, cq_tail = 0;     // what the doorbells and the device write
    std::uint32_t sq_doorbells = 0, cq_doorbells = 0;        // MMIO writes, not system calls
    std::uint64_t now_ns = 0, done_at_ns = 0;
    bool in_flight = false;

    void submit(const Command& c) {                          // 64 bytes into the ring, then one MMIO write
        sq_entry = c;
        ++sq_tail;
        ++sq_doorbells;
        done_at_ns = now_ns + kDeviceNs;
        in_flight = true;
    }
    void device_tick() {                                     // the device side: post the completion when due
        now_ns += kPollNs;
        if (in_flight && now_ns >= done_at_ns) {
            cq_entry = Completion{static_cast<std::uint16_t>(sq_tail - 1), 0};
            ++cq_tail;
            in_flight = false;
        }
    }
    bool completion_ready() const { return cq_head != cq_tail; }   // a read of our own memory
    Completion reap() {
        ++cq_head;
        ++cq_doorbells;                                      // tell the device the entry is consumed
        return cq_entry;
    }
};

int main() {
    // SPDK model: the queues live in our process, nobody interrupts us
    QueuePair qp;                      // mapped NVMe queues, no kernel
    qp.submit({kRead, 0, 8});          // write the entry, ring the doorbell
    std::uint64_t polls = 0;
    while (!qp.completion_ready()) {   // busy poll: one core at 100 percent
        ++polls;                       // no system call, no interrupt,
        qp.device_tick();              // no context switch (model time)
    }
    auto done = qp.reap();             // cost: a whole core, not latency

    std::cout << "one read of " << qp.sq_entry.blocks << " blocks at LBA " << qp.sq_entry.lba << ", polled from user space (model):\n"
              << "  polls until the completion : " << polls << "\n"
              << "  model time                 : " << qp.now_ns / 1000 << " microseconds\n"
              << "  completion                 : command " << done.command_id << ", status " << done.status << "\n"
              << "  doorbell writes (MMIO)     : " << qp.sq_doorbells + qp.cq_doorbells << "\n"
              << "  system calls, interrupts, context switches : 0, 0, 0\n\n";

    // Why it pays off only on fast devices: the interrupt path adds a roughly fixed cost per request.
    constexpr double kInterruptPathUs = 5.0;                 // typical: interrupt, wake up, context switch, cold cache
    struct Device { const char* name; double latency_us; };
    const Device devices[] = {{"HDD seek", 10000.0}, {"SATA SSD", 200.0}, {"NVMe SSD", 80.0}, {"fastest NVMe", 10.0}};
    std::cout << "typical interrupt path of about " << kInterruptPathUs << " microseconds, as a share of one request:\n";
    for (const Device& d : devices)
        std::cout << "  " << std::left << std::setw(14) << d.name << std::right << std::setw(9) << d.latency_us
                  << " us device  ->  " << std::fixed << std::setprecision(2)
                  << 100.0 * kInterruptPathUs / (d.latency_us + kInterruptPathUs) << " percent overhead\n"
                  << std::defaultfloat << std::setprecision(6);
    std::cout << "the price of polling: one core at 100 percent, a device that no other process can use, and no\n"
              << "filesystem. These are typical orders of magnitude, not measurements.\n";

    const bool ok = polls == QueuePair::kDeviceNs / QueuePair::kPollNs && done.status == 0;
    if (!ok) std::cout << "FAIL: the model lost the completion\n";
    return ok ? 0 : 1;
}
