// Read and Write in C++: What Happens Behind the System Call - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <queue>
#include <string>

// the imaginary "tomjnet.h" of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Linux Page Cache: why disk I/O often never touches the disk immediately",
                                   "Inside an SSD: NAND, pages, blocks and the flash translation layer",
                                   "Inside NVMe: submission queues, completion queues and PCIe"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

// #include "tomjnet.h"

int main() {
    std::queue<Topic> todo;
    while (alive()) {
        todo.push(next_storage_topic());   // next: the Linux page cache
        todo.push(viewer_request());       // batched: one call, not 1M
        subscribe();                       // fsync for your feed
    }

    std::cout << "\nqueued, first in first out:\n";
    while (!todo.empty()) {
        std::cout << "  " << todo.front().name << '\n';    // '\n', not std::endl: no forced flush per line
        todo.pop();
    }
    return 0;
}
