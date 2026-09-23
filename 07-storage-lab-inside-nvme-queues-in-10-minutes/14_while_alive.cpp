// Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 14: thank you
// Build: make 14_while_alive
//
// The thank-you slide as a program. tomjnet.h is imaginary, so its contents live here: a tiny queue pair
// for topics, where submit is a memory write and ring_doorbell announces the whole batch at once.
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ---- the imaginary tomjnet.h ----
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Buffered I/O vs Direct I/O: How Low Latency Storage Works",
                                   "Storage Performance: IOPS, Throughput, Latency and Queue Depth",
                                   "Inside the CPU: What We Learned About Storage"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

template <class T>
class QueuePair {
public:
    void submit(T entry) { sq_.push_back(std::move(entry)); }          // a memory write, nothing crosses PCIe
    void ring_doorbell() {                                             // one MMIO write for the whole batch
        ++doorbell_writes_;
        std::cout << "  doorbell " << doorbell_writes_ << ": " << sq_.size() - announced_ << " new entries\n";
        for (; announced_ < sq_.size(); ++announced_) std::cout << "    completed: " << sq_[announced_].name << "\n";
    }
    std::size_t submitted() const { return sq_.size(); }
    int doorbell_writes() const { return doorbell_writes_; }

private:
    std::vector<T> sq_;
    std::size_t announced_ = 0;
    int doorbell_writes_ = 0;
};
// ---- end of tomjnet.h ----

int main() {
    QueuePair<Topic> todo;                   // one pair per core
    while (alive()) {
        todo.submit(next_storage_topic());   // buffered vs direct I/O
        todo.submit(viewer_request());       // 64 bytes each
        todo.ring_doorbell();                // one MMIO write for both
        subscribe();                         // lifetime benefit
    }

    std::cout << todo.submitted() << " topics submitted with " << todo.doorbell_writes() << " doorbell writes\n";
    return 0;
}
