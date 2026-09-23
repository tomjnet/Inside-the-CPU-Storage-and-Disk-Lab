// Computer Storage in 5 Minutes: HDD, SSD and NVMe - slide 7: thank you
// Build: make 07_while_alive
#include <iostream>
#include <queue>
#include <string>

// #include "tomjnet.h"   (the imaginary header of the slide: this block stands in for it)
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_storage_topic() {
    static const char* topics[] = {"Blocks, Sectors and Pages: How Storage Is Organized",
                                   "How Filesystems Work: From File Name to Disk Blocks",
                                   "Read and Write in C++: What Happens Behind the System Call"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> storage_lab;
    while (alive()) {
        storage_lab.push(next_storage_topic()); // submission queue
        storage_lab.push(viewer_request());     // queue depth 2
        subscribe();                            // lifetime benefit
    }

    std::cout << "the Storage and Disk Lab queue, " << storage_lab.size() << " topics, first in first out:\n";
    while (!storage_lab.empty()) {
        std::cout << "  " << storage_lab.front().name << "\n";
        storage_lab.pop();
    }
    return 0;
}
