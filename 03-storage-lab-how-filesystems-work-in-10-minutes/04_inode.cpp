// How Filesystems Work: From File Name to Disk Blocks - slide 4: the inode: everything except the name
// Build: make 04_inode
#include <cstdint>
#include <iostream>
#include <vector>

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;

// ---- the slide ----
// An inode: everything about a file except its name.
struct Extent { uint32_t start; uint32_t count; };  // a run of blocks

struct Inode {
    uint16_t mode  = 0;      // file or directory, permissions
    uint16_t links = 0;      // names that point here; 0 frees it
    uint64_t size  = 0;      // bytes
    std::vector<Extent> extents;       // where the data lives
};
// ext4: 256 byte inodes, 4 extents inline, then an extent tree.
// stat() reads the inode only: no data block is touched.
// ---- end of the slide ----

static constexpr uint64_t BLOCK = 4096;                      // filesystem block, bytes
static constexpr uint16_t REGULAR_FILE = 0100644;            // S_IFREG plus rw-r--r--, as stat prints it

int main() {
    // A 1 GiB file written in three runs: three extents describe all of it.
    Inode video;
    video.mode = REGULAR_FILE;
    video.links = 1;
    video.size = 1ull << 30;
    video.extents = {{34816, 131072}, {190000, 98304}, {400000, 32768}};

    uint64_t covered = 0;
    std::cout << "inode of a 1 GiB file, the model\n";
    std::cout << "  mode " << std::oct << video.mode << std::dec << "  links " << video.links
              << "  size " << video.size << " bytes\n";
    for (const Extent& e : video.extents) {
        std::cout << "  extent: start block " << e.start << ", count " << e.count << "\n";
        covered += static_cast<uint64_t>(e.count) * BLOCK;
    }
    std::cout << "  extents cover " << covered << " bytes with " << video.extents.size() << " entries\n";
    std::cout << "  no name field anywhere: sizeof(Inode) here is " << sizeof(Inode) << " bytes of metadata\n\n";

    // The older design: block pointers. 4 KiB blocks, 4 byte pointers: 1024 pointers per indirect block.
    const uint64_t per_block = BLOCK / 4;
    const uint64_t blocks = video.size / BLOCK;
    const uint64_t direct = 12;
    const uint64_t single = per_block;
    const uint64_t dbl = per_block * per_block;
    std::cout << "the same file with ext2 style block pointers\n";
    std::cout << "  data blocks to point at: " << blocks << "\n";
    std::cout << "  12 direct pointers reach      " << direct * BLOCK / 1024 << " KiB\n";
    std::cout << "  one indirect block reaches    " << (direct + single) * BLOCK / (1024 * 1024) << " MiB\n";
    std::cout << "  a double indirect reaches     " << (direct + single + dbl) * BLOCK / (1024ull * 1024 * 1024)
              << " GiB\n";
    const uint64_t left = blocks - direct - single;          // blocks that need the double indirect level
    const uint64_t pointer_blocks = 1 + 1 + (left + per_block - 1) / per_block;
    std::cout << "  pointer blocks needed: " << pointer_blocks << " (each one an extra read), against "
              << video.extents.size() << " extents inside the inode\n";

    if (covered != video.size) {
        std::cout << "FAIL: the extents do not cover the file\n";
        return 1;
    }
    return 0;
}
