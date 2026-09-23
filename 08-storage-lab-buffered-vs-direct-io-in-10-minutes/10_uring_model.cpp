// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 10: io_uring: two rings shared with the kernel
// Build: make 10_uring_model
//
// A model, not the real interface: the real one needs liburing (sudo apt install liburing-dev) or the raw
// io_uring_setup, io_uring_enter and mmap calls. The shape is the same: two rings, one producer each.
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

// A single producer, single consumer ring: the producer moves the tail, the consumer moves the head.
template <class T, std::size_t N>
struct Ring {
    std::array<T, N> slots{};
    std::uint32_t head = 0, tail = 0;                       // free running counters, index = counter % N
    std::size_t size() const { return tail - head; }
    bool push(const T& x) {
        if (size() == N) return false;                      // full: the producer must wait for the consumer
        slots[tail % N] = x;
        ++tail;
        return true;
    }
    std::optional<T> pop() {
        if (head == tail) return std::nullopt;              // empty
        T x = slots[head % N];
        ++head;
        return x;
    }
};

// io_uring model: two rings in memory shared with the kernel
struct Sqe { int opcode, fd; std::uint64_t off, user_data; };
struct Cqe { std::uint64_t user_data; std::int32_t res; };

constexpr int kRead = 22;                                   // IORING_OP_READ in the real interface
static std::vector<char> g_file(16 * 4096, 'F');            // the "file" behind file descriptor 3
static int g_system_calls = 0;

// The kernel side of io_uring_enter: consume every submission, do the I/O, post one completion each.
static int enter(Ring<Sqe, 8>& sq, Ring<Cqe, 16>& cq) {
    ++g_system_calls;
    int submitted = 0;
    while (auto s = sq.pop()) {
        std::int32_t res = -22;                             // -EINVAL for anything the model does not know
        if (s->opcode == kRead && s->off + 4096 <= g_file.size()) res = 4096;
        cq.push(Cqe{s->user_data, res});
        ++submitted;
    }
    std::cout << "  kernel: io_uring_enter consumed " << submitted << " entries in 1 system call\n";
    return 1;
}

static void use(const Cqe& c) {
    std::cout << "  completion: user_data " << c.user_data << ", res " << c.res << " bytes\n";
}

int main() {
    const int fd = 3;

    Ring<Sqe, 8> sq;   // application writes the tail, kernel reads
    Ring<Cqe, 16> cq;  // kernel writes the tail, application reads
    for (std::uint64_t i = 0; i < 4; ++i) sq.push({kRead, fd, i * 4096, i});
    std::cout << "submission ring: head " << sq.head << ", tail " << sq.tail << " (4 entries, 0 system calls so far)\n";
    int calls = enter(sq, cq);           // one system call submits all four
    std::cout << "completion ring: head " << cq.head << ", tail " << cq.tail << "\n";
    while (auto c = cq.pop()) use(*c);   // completions: zero system calls

    std::cout << "\nsystem calls for 4 reads of 4 KiB:\n"
              << "  pread, one at a time : 4 (and the thread sleeps in each one)\n"
              << "  io_uring             : " << calls << " (total counted by the model: " << g_system_calls << ")\n"
              << "  io_uring with SQPOLL : 0 (a kernel thread polls the submission ring)\n";

    // A full ring is back pressure, not an error: the 9th push into 8 slots is refused.
    int accepted = 0;
    for (std::uint64_t i = 0; i < 9; ++i) accepted += sq.push({kRead, fd, i * 4096, 100 + i}) ? 1 : 0;
    std::cout << "\n9 pushes into a ring of 8: " << accepted << " accepted, the application must submit first\n";

    const bool ok = calls == 1 && cq.head == 4 && cq.tail == 4 && accepted == 8;
    if (!ok) std::cout << "FAIL: the ring lost or invented an entry\n";
    return ok ? 0 : 1;
}
