// How Filesystems Work: From File Name to Disk Blocks - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <map>
#include <string>

// The imaginary tomjnet.h of the slide.
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Read and Write in C++: What Happens Behind the System Call",
                                   "Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately",
                                   "Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::map<std::string, Topic> lab;          // name to topic
    while (alive()) {
        lab["next"] = next_storage_topic();    // read and write
        lab["request"] = viewer_request();     // one lookup per name
        subscribe();                           // lifetime benefit
        std::cout << "  next: " << lab["next"].name << "\n";
    }
    std::cout << "the directory of the lab, a name to topic table:\n";
    for (const auto& [name, topic] : lab) std::cout << "  " << name << " -> " << topic.name << "\n";
    return 0;
}
