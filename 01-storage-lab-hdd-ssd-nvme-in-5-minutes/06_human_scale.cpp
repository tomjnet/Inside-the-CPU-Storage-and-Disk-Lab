// Computer Storage in 5 Minutes: HDD, SSD and NVMe - slide 6: if an l1 hit took one second
// Build: make 06_human_scale
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// seconds on the human scale, printed in the largest unit that keeps the number readable
static std::string human(double seconds) {
    struct Unit { const char* name; double in_seconds; };
    constexpr Unit units[] = {
        {"months", 30.44 * 86400.0}, {"days", 86400.0}, {"hours", 3600.0}, {"minutes", 60.0}, {"seconds", 1.0},
    };
    for (const Unit& u : units) {
        if (seconds >= u.in_seconds) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(1) << seconds / u.in_seconds << ' ' << u.name;
            return out.str();
        }
    }
    return "less than a second";
}

int main() {
    std::cout << "typical orders of magnitude, not measurements: if an L1 hit took one second\n";

    struct Level { const char* name; double ns; };  // typical, not measured
    constexpr Level ladder[] = {
        {"L1 cache", 1},      {"L2 cache", 4},      {"L3 cache", 20},
        {"RAM", 100},         {"NVMe SSD", 100e3},  // 100 microseconds
        {"SATA SSD", 200e3},  {"HDD seek", 10e6},   // 10 milliseconds
    };
    for (const Level& l : ladder) {                 // scale: L1 = 1 second
        double seconds = l.ns / ladder[0].ns;
        std::cout << l.name << ": " << human(seconds) << '\n';
    }
    // RAM 1.7 minutes, NVMe 1.2 days, HDD seek 3.8 months

    // the same ladder against RAM: the cliff between memory and storage
    const double ram_ns = ladder[3].ns;
    std::cout << "\nhow many RAM accesses fit in one access of each device\n";
    for (const Level& l : ladder) {
        if (l.ns <= ram_ns) continue;
        std::cout << "  " << l.name << ": " << std::fixed << std::setprecision(0) << l.ns / ram_ns << " x RAM\n";
    }
    std::cout << "replace the constants with the numbers of your own hardware (fio, episode 9)\n";
    return 0;
}
