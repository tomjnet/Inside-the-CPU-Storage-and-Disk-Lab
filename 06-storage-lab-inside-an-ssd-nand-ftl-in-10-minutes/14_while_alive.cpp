// Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer - slide 14: thank you
// Build: make 14_while_alive
#include <iostream>
#include <queue>
#include <string>

// ---- the imaginary tomjnet.h of the slide ------------------------------------------------------------------
// #include "tomjnet.h"
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Inside NVMe: submission queues, completion queues and PCIe",
                                   "Buffered I/O vs direct I/O",
                                   "IOPS, throughput, latency and queue depth"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> todo;
    while (alive()) {
        todo.push(next_storage_topic()); // next: inside NVMe queues
        todo.push(viewer_request());     // the submission queue
        subscribe();                     // no write amplification
    }

    std::cout << "queued " << todo.size() << " topics, first in, first out:\n";
    while (!todo.empty()) {
        std::cout << "  " << todo.front().name << "\n";
        todo.pop();
    }
    return 0;
}
