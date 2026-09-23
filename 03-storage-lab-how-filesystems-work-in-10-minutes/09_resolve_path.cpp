// How Filesystems Work: From File Name to Disk Blocks - slide 9: lab: resolve a path in the model
// Build: make 09_resolve_path
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
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

// The tiny filesystem of slide 8, plus make_dir, rename and the path resolution of this slide.
struct TinyFs {
    std::vector<Inode> inodes = std::vector<Inode>(32);
    std::vector<bool> used = std::vector<bool>(64, false);
    std::map<uint32_t, std::map<std::string, uint32_t>> dirs;        // directory inode -> (name -> inode)
    uint32_t next_inode = FIRST_INODE;

    TinyFs() {
        for (uint32_t b = 0; b < 4; ++b) used[b] = true;             // superblock, two bitmaps, inode table
        inodes[ROOT].mode = IS_DIR;
        inodes[ROOT].links = 2;
        dirs[ROOT];
    }
    static uint32_t blocks_for(uint64_t n) { return uint32_t((n + BLOCK - 1) / BLOCK); }
    Extent alloc(uint32_t want);
    uint32_t create(uint32_t dir, std::string name, uint64_t n);
    uint32_t make_dir(uint32_t dir, const std::string& name);
    void rename(uint32_t from, const std::string& name, uint32_t to, const std::string& new_name);
    uint32_t resolve(const std::string& path);
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

// slide 8, unchanged
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

// A directory is an inode too; in this model its table lives in `dirs` instead of in data blocks.
uint32_t TinyFs::make_dir(uint32_t dir, const std::string& name) {
    const uint32_t ino = create(dir, name, 0);
    inodes[ino].mode = IS_DIR;
    inodes[ino].links = 2;                     // its name in the parent, plus its own "." entry
    dirs[ino];
    return ino;
}

// rename = remove one directory entry, add another with the same inode number. Nothing else moves.
void TinyFs::rename(uint32_t from, const std::string& name, uint32_t to, const std::string& new_name) {
    const uint32_t ino = dirs[from][name];
    dirs[from].erase(name);
    dirs[to][new_name] = ino;
}

// ---- the slide ----
// Path resolution: one directory lookup per component.
uint32_t TinyFs::resolve(const std::string& path) {
    uint32_t ino = ROOT;                         // inode 2 on ext4
    std::istringstream parts(path);
    for (std::string name; std::getline(parts, name, '/'); ) {
        if (name.empty()) continue;              // the leading slash
        auto it = dirs[ino].find(name);          // read that directory
        if (it == dirs[ino].end()) return 0;     // ENOENT
        ino = it->second;                        // on to the next inode
    }
    return ino;    // the kernel caches these hops: the dentry cache
}
// ---- end of the slide ----

static void show(TinyFs& fs, const std::string& path) {
    const uint32_t ino = fs.resolve(path);
    std::cout << "  " << path << " -> ";
    if (ino == 0) {
        std::cout << "0 (ENOENT, no such file or directory)\n";
        return;
    }
    const Inode& f = fs.inodes[ino];
    std::cout << "inode " << ino << (f.mode == IS_DIR ? " (directory)" : " (file)");
    for (const Extent& e : f.extents) std::cout << " [start " << e.start << ", count " << e.count << "]";
    std::cout << "\n";
}

int main() {
    TinyFs fs;
    const uint32_t home = fs.make_dir(ROOT, "home");
    const uint32_t tom = fs.make_dir(home, "tom");
    fs.make_dir(home, "ana");
    const uint32_t notes = fs.create(tom, "notes.md", 3000);
    const uint32_t report = fs.create(tom, "report.txt", 10000);
    fs.inodes[notes].mode = IS_FILE;
    fs.inodes[report].mode = IS_FILE;

    std::cout << "every hop of /home/tom/report.txt: one directory lookup per component\n";
    show(fs, "/");
    show(fs, "/home");
    show(fs, "/home/tom");
    show(fs, "/home/tom/report.txt");
    std::cout << "\na name that is not in the directory\n";
    show(fs, "/home/tom/missing.txt");

    std::cout << "\nrename /home/tom/report.txt to /home/ana/final.txt\n";
    const std::vector<Extent> before = fs.inodes[report].extents;
    fs.rename(tom, "report.txt", fs.resolve("/home/ana"), "final.txt");
    show(fs, "/home/tom/report.txt");
    show(fs, "/home/ana/final.txt");
    const uint32_t after = fs.resolve("/home/ana/final.txt");
    std::cout << "  same inode, same extents: only two directory entries changed, no data block moved\n";

    const bool ok = fs.resolve("/home/tom") == tom && after == report && fs.resolve("/home/tom/report.txt") == 0 &&
                    fs.inodes[after].extents.size() == before.size() &&
                    fs.inodes[after].extents[0].start == before[0].start;
    if (!ok) {
        std::cout << "FAIL: path resolution or rename broke the model\n";
        return 1;
    }
    return 0;
}
