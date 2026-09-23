// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <string>

// #include "tomjnet.h"  (imaginary: everything it would declare is right here)
struct Page { std::string lesson; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static std::string next_episode() { return "Inside an SSD: NAND, Pages, Blocks and the Flash Translation Layer"; }

struct PageCache {
    std::string lab;
    Page current;
    Page& read() {
        static const char* lessons[] = {"Inside an SSD: NAND and the flash translation layer",
                                        "Inside NVMe: submission queues, completion queues and PCIe",
                                        "Buffered I/O vs Direct I/O: O_DIRECT and io_uring"};
        current = Page{lessons[episodes++]};
        return current;
    }
};

static PageCache open_lab(const std::string& first) {
    std::cout << "next in the Storage and Disk Lab: " << first << '\n';
    return PageCache{"Storage and Disk Lab", Page{}};
}
static void learn(const Page& page) { std::cout << "  learned: " << page.lesson << '\n'; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    PageCache cache = open_lab(next_episode());  // inside an SSD
    while (alive()) {
        Page& page = cache.read();       // a hit: no device involved
        learn(page);                     // 4 KiB at a time
        subscribe();                     // fsync: a durable choice
    }
    std::cout << cache.lab << ": " << episodes << " episodes served from the cache\n";
    return 0;
}
