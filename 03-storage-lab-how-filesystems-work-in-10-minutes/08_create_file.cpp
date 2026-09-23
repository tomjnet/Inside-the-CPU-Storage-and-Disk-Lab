// How Filesystems Work: From File Name to Disk Blocks - slide 8: lab: create a file in a tiny filesystem
// Build: make 08_create_file
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;

struct Extent { uint32_t start; uint32_t count; };  // a run of blocks

struct Inode {
    uint16_t mode  = 0;      // file or directory, permissions
    uint16_t links = 0;      // names that point here; 0 frees it
    uint64_t size  = 0;      // bytes
    std::vector<Extent> extents;       // where the data lives
};

static constexpr uint32_t ROOT = 2;                 // the root directory is inode 2 on ext4
static constexpr uint32_t FIRST_INODE = 11;         // ext4 reserves inodes 1 to 10
static constexpr uint64_t BLOCK = 4096;
static constexpr uint16_t IS_DIR = 0040755;
static constexpr uint16_t IS_FILE = 0100644;

// A tiny filesystem in memory: an inode table, a directory map and a block bitmap.
struct TinyFs {
    std::vector<Inode> inodes = std::vector<Inode>(32);              // fixed when the filesystem is made
    std::vector<bool> used = std::vector<bool>(64, false);           // block bitmap, 64 blocks of 4 KiB
    std::map<uint32_t, std::map<std::string, uint32_t>> dirs;        // directory inode -> (name -> inode)
    uint32_t next_inode = FIRST_INODE;                               // the model never reuses an inode number

    TinyFs() {
        for (uint32_t b = 0; b < 4; ++b) used[b] = true;             // superblock, two bitmaps, inode table
        inodes[ROOT].mode = IS_DIR;
        inodes[ROOT].links = 2;
        dirs[ROOT];
    }
    static uint32_t blocks_for(uint64_t n) { return uint32_t((n + BLOCK - 1) / BLOCK); }
    uint32_t free_blocks() const {
        uint32_t n = 0;
        for (bool u : used) n += u ? 0u : 1u;
        return n;
    }
    Extent alloc(uint32_t want);
    uint32_t create(uint32_t dir, std::string name, uint64_t n);
    void unlink(uint32_t dir, const std::string& name);
};

// slide 6, unchanged
Extent TinyFs::alloc(uint32_t want) {
    uint32_t b = 0, n = uint32_t(used.size());
    while (b < n && used[b]) ++b;              // first free block
    Extent e{b, 0};
    for (; b < n && !used[b] && e.count < want; ++b, ++e.count)
        used[b] = true;                        // grow the run
    return e;        // shorter than want: the file fragments
}

// ---- the slide ----
// create = one inode + the data blocks + one directory entry
uint32_t TinyFs::create(uint32_t dir, std::string name, uint64_t n) {
    uint32_t ino = next_inode++;               // from the inode table
    Inode& f = inodes[ino];
    f.links = 1;
    f.size = n;
    for (uint32_t need = blocks_for(n); need > 0;
         need -= f.extents.back().count)
        f.extents.push_back(alloc(need));      // from the block bitmap
    dirs[dir][name] = ino;                     // the name lives here
    return ino;
}
// ---- end of the slide ----

// Remove a name; when the last link goes, the blocks return to the bitmap.
void TinyFs::unlink(uint32_t dir, const std::string& name) {
    const uint32_t ino = dirs[dir][name];
    dirs[dir].erase(name);
    Inode& f = inodes[ino];
    if (--f.links > 0) return;
    for (const Extent& e : f.extents)
        for (uint32_t b = e.start; b < e.start + e.count; ++b) used[b] = false;
    f = Inode{};
}

static std::string bits(const TinyFs& fs) {
    std::string s;
    for (std::size_t i = 0; i < fs.used.size(); ++i) {
        if (i > 0 && i % 8 == 0) s += ' ';
        s += fs.used[i] ? '1' : '0';
    }
    return s;
}

// The slide's create() assumes the blocks exist; a real filesystem checks first and returns ENOSPC.
static uint32_t create_checked(TinyFs& fs, const std::string& name, uint64_t bytes) {
    if (fs.free_blocks() < TinyFs::blocks_for(bytes) || fs.next_inode >= fs.inodes.size()) {
        std::cout << "  " << name << ": ENOSPC, no space left on device\n";
        return 0;
    }
    const uint32_t ino = fs.create(ROOT, name, bytes);
    fs.inodes[ino].mode = IS_FILE;
    std::cout << "  " << name << ": inode " << ino << ", " << bytes << " bytes, extents";
    for (const Extent& e : fs.inodes[ino].extents) std::cout << " [start " << e.start << ", count " << e.count << "]";
    std::cout << "\n";
    return ino;
}

int main() {
    TinyFs fs;
    std::cout << "TinyFs: " << fs.used.size() << " blocks of " << BLOCK << " bytes, " << fs.inodes.size()
              << " inodes, blocks 0 to 3 hold the metadata\n";
    std::cout << "  bitmap  " << bits(fs) << "\n\ncreate three files\n";
    create_checked(fs, "a.log", 20000);             // 5 blocks
    create_checked(fs, "b.tmp", 12000);             // 3 blocks
    create_checked(fs, "c.db", 24000);              // 6 blocks
    std::cout << "  bitmap  " << bits(fs) << "\n\ndelete the middle one: a hole of 3 blocks\n";
    fs.unlink(ROOT, "b.tmp");
    std::cout << "  bitmap  " << bits(fs) << "\n\ncreate a file of 8 blocks: first fit fills the hole, then the end\n";
    const uint32_t d = create_checked(fs, "d.bin", 32768);
    std::cout << "  bitmap  " << bits(fs) << "\n\nthe root directory, the only place that stores names\n";
    for (const auto& [name, ino] : fs.dirs[ROOT]) std::cout << "  " << name << " -> inode " << ino << "\n";
    std::cout << "\na file too large for the free blocks\n";
    create_checked(fs, "huge.iso", 64ull * BLOCK);

    const std::vector<Extent>& ex = fs.inodes[d].extents;
    const bool ok = d != 0 && ex.size() == 2 && ex[0].start == 9 && ex[0].count == 3 && ex[1].start == 18 &&
                    ex[1].count == 5;
    if (!ok) {
        std::cout << "FAIL: d.bin did not come back as the two extents the slide promises\n";
        return 1;
    }
    std::cout << "\nd.bin is fragmented: 2 extents for one file, because the disk had a hole\n";
    return 0;
}
