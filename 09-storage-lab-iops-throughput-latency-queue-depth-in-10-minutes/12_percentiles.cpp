// Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 12: p50, p99 and p99.9 from the samples
// Build: make 12_percentiles
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

// percentile of sorted samples: O(n log n) sort, O(1) per lookup
double percentile(const std::vector<double>& sorted, double p) {
    double rank = p / 100.0 * double(sorted.size() - 1);
    return sorted[std::size_t(rank)];
}

static double mean(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
}

// A model of the read latencies of an SSD at queue depth 1, in nanoseconds (not a measurement): a body of
// 70 to 110 us, 1 read in 100 behind another command (+400 us), 1 in 1000 behind garbage collection (+5 ms).
static std::vector<double> model_latencies(std::size_t n) {
    std::mt19937 rng(42);                                     // fixed seed: the same samples on every toolchain
    std::vector<double> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        double us = 70.0 + double(rng() % 40u);
        std::uint32_t dice = static_cast<std::uint32_t>(rng() % 1000u);
        if (dice < 10u) us += 400.0;
        if (dice == 0u) us += 5000.0;
        v.push_back(us * 1000.0);
    }
    return v;
}

int main() {
    std::vector<double> lat_ns = model_latencies(100000);

    // 1000 samples hold ONE value above p99.9: collect 100 000 or more
    std::sort(lat_ns.begin(), lat_ns.end());
    double p50 = percentile(lat_ns, 50.0);       // the typical read
    double p99 = percentile(lat_ns, 99.0);       // 1 in 100 is slower
    double p999 = percentile(lat_ns, 99.9);      // 1 in 1000: the tail
    double iops = 1e9 / mean(lat_ns);            // valid at queue depth 1

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "model latencies, " << lat_ns.size() << " samples (not a measurement)\n";
    std::cout << "  mean   " << mean(lat_ns) / 1000.0 << " us\n";
    std::cout << "  p50    " << p50 / 1000.0 << " us\n";
    std::cout << "  p99    " << p99 / 1000.0 << " us\n";
    std::cout << "  p99.9  " << p999 / 1000.0 << " us\n";
    std::cout << "  max    " << lat_ns.back() / 1000.0 << " us\n";
    std::cout << "  IOPS at queue depth 1 = 1 s / mean = " << iops << '\n';
    std::cout << "  p99.9 / p50 = " << p999 / p50 << "x: the mean does not show it\n\n";

    // the same device, five short tests of 1000 reads each: one value defines p99.9, so it jumps around
    std::cout << "p99.9 of five tests of only 1000 samples each:\n";
    std::vector<double> all = model_latencies(5000);
    for (std::size_t t = 0; t < 5; ++t) {
        auto first = all.begin() + static_cast<std::ptrdiff_t>(t * 1000);
        std::vector<double> small(first, first + 1000);
        std::sort(small.begin(), small.end());
        std::cout << "  test " << t + 1 << ": p50 " << percentile(small, 50.0) / 1000.0
                  << " us   p99.9 " << percentile(small, 99.9) / 1000.0 << " us\n";
    }
    return 0;
}
