// Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 14: thank you
// Build: make 14_while_alive
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Inside the CPU: What We Learned About Storage",
                                   "Networking and NIC Lab", "Low Latency C++ Lab"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> todo;                  // queue depth: unbounded
    while (alive()) {
        todo.push(next_storage_topic());     // next: the series summary
        todo.push(viewer_request());         // submitted, done later
        subscribe();                         // lifetime benefit
    }

    std::cout << "in flight: " << todo.size() << " topics, completed in submission order:\n";
    while (!todo.empty()) {
        std::cout << "  " << todo.front().name << '\n';
        todo.pop();
    }
    return 0;
}
