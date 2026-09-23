// Blocks, Sectors and Pages: How Storage Is Organized - slide 11: aligning your own i/o
// Build: make 11_align_io
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

constexpr std::uint64_t kBlock = 4096;    // filesystem block, bytes

// round the start down and the end up to the block size
constexpr std::uint64_t align_down(std::uint64_t x, std::uint64_t a) {
    return x / a * a;
}
constexpr std::uint64_t align_up(std::uint64_t x, std::uint64_t a) {
    return (x + a - 1) / a * a;
}

static_assert(align_down(8292, kBlock) == 8192 && align_up(9292, kBlock) == 12288);
static_assert(align_down(8192, kBlock) == 8192 && align_up(8192, kBlock) == 8192);   // aligned stays put

static unsigned char pattern(std::uint64_t i) { return static_cast<unsigned char>(i % 251); }

int main() {
    // want bytes [8292, 9292): one aligned read covers them
    std::uint64_t start = align_down(8292, kBlock);         // 8192
    std::uint64_t end   = align_up(8292 + 1000, kBlock);    // 12288
    alignas(4096) static unsigned char buffer[4096];  // aligned memory

    std::cout << "wanted  [8292, 9292): 1000 bytes\n";
    std::cout << "aligned [" << start << ", " << end << "): " << (end - start) / kBlock << " whole block, "
              << end - start << " bytes\n";
    std::cout << "buffer address modulo 4096: " << reinterpret_cast<std::uintptr_t>(buffer) % 4096 << '\n';

    // a 16 KiB file with a known pattern, in the temporary directory
    std::error_code ec;
    std::filesystem::path file = std::filesystem::temp_directory_path(ec);
    if (ec) file = ".";
    file /= "tomjnet_storage_lab_02_align_io.bin";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        for (std::uint64_t i = 0; i < 4 * kBlock; ++i) out.put(static_cast<char>(pattern(i)));
    }

    // one aligned request: seek to a block boundary, read a whole block
    bool ok = end - start == sizeof(buffer);
    {
        std::ifstream in(file, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(start));
        in.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(sizeof(buffer)));
        ok = ok && in.gcount() == static_cast<std::streamsize>(sizeof(buffer));
    }

    // pick our 1000 bytes out of the middle of the buffer
    const std::uint64_t skip = 8292 - start;
    std::uint64_t matching = 0;
    for (std::uint64_t i = 0; i < 1000; ++i)
        if (buffer[static_cast<std::size_t>(skip + i)] == pattern(8292 + i)) ++matching;
    std::cout << "our bytes start " << skip << " bytes into the buffer: " << matching
              << " of 1000 match the file\n";
    ok = ok && matching == 1000;

    std::filesystem::remove(file, ec);
    std::cout << "buffered I/O would have accepted [8292, 9292) as it is: the page cache rounds for you.\n"
                 "O_DIRECT would not: offset, length and buffer address must be multiples of the\n"
                 "logical sector size, or the call fails with EINVAL (episode 8)\n";
    return ok ? 0 : 1;
}
