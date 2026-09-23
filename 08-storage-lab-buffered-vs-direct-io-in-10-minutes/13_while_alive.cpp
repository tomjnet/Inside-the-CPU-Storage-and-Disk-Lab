// Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <queue>
#include <string>

// The imaginary "tomjnet.h" of the slide.
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Storage Performance: IOPS, Throughput, Latency and Queue Depth",
                                   "Inside the CPU: What We Learned About Storage",
                                   "Inside the CPU: Networking and NIC Lab"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

// #include "tomjnet.h"

int main() {
    std::queue<Topic> submission;                 // queue depth: you
    while (alive()) {
        submission.push(next_storage_topic());    // buffered
        submission.push(viewer_request());        // direct, no cache
        subscribe();                              // fsync: durable
    }

    std::cout << "submission queue, in order:\n";
    while (!submission.empty()) {
        std::cout << "  " << submission.front().name << "\n";
        submission.pop();
    }
    return 0;
}
