// Inside the CPU: What We Learned About Storage - slide 5: thank you
// Build: make 05_while_alive
#include <cstddef>
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Lab { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                  // three labs, then the demo ends
static Lab next_lab() {
    static const char* labs[] = {"Inside the CPU: Networking and NIC Lab", "Inside the CPU: Low Latency C++ Lab",
                                 "Inside the CPU: PCIe and DMA Lab"};
    return Lab{labs[episodes++]};
}
static void measure(const Lab& lab) { std::cout << "submitted: " << lab.name << "\n"; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Lab> next_labs;           // a submission queue of labs
    while (alive()) {
        next_labs.push(next_lab());      // Networking and NIC is next
        measure(next_labs.back());       // your device, your tail
        subscribe();                     // lifetime benefit
    }
    const std::size_t submitted = next_labs.size();
    std::size_t completed = 0;
    while (!next_labs.empty()) {         // the completion side: first in, first out
        std::cout << "completed " << ++completed << ": " << next_labs.front().name << "\n";
        next_labs.pop();
    }
    std::cout << submitted << " labs submitted, " << completed << " completed, in order: Networking and NIC first\n";
    return submitted == completed ? 0 : 1;
}
