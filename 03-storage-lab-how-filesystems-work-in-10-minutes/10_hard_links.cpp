// How Filesystems Work: From File Name to Disk Blocks - slide 10: hard links with std::filesystem
// Build: make 10_hard_links
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <sys/stat.h>
#endif

// Linux only: the inode number behind a name, read with stat(2), the call the stat command makes.
static void print_inode(const std::filesystem::path& p) {
#if defined(__linux__)
    struct stat st {};
    if (stat(p.c_str(), &st) == 0)
        std::cout << "  " << p.filename().string() << ": inode " << st.st_ino << ", links " << st.st_nlink
                  << ", 512 byte blocks " << st.st_blocks << "\n";
#else
    std::cout << "  " << p.filename().string() << ": the inode number needs Linux (stat or ls -i show it)\n";
#endif
}

int main() {
    // A private directory under the temporary folder, removed before exit.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("tomjnet_fs_lab_" + std::to_string(stamp));
    std::error_code ignore;
    try {
        std::filesystem::create_directory(dir);
        std::cout << "working in " << dir.string() << "\n";

        // ---- the slide ----
        namespace fs = std::filesystem;
        fs::path a = dir / "report.txt", b = dir / "alias.txt";
        std::ofstream(a) << "hello filesystem";      // one inode, one block
        fs::create_hard_link(a, b);                  // second name, same inode
        std::cout << fs::file_size(b) << " bytes, "
                  << fs::hard_link_count(a) << " links\n";  // 16 bytes, 2 links
        fs::remove(a);                               // unlink: links 2 to 1
        std::cout << fs::exists(b) << ' '
                  << fs::hard_link_count(b) << '\n'; // 1 1: the data is alive
        fs::remove(b);                               // links 0: blocks freed
        // ---- end of the slide ----

        // The same again, this time looking at the inode numbers and reading the data through the alias.
        std::cout << "\ntwo names, one inode\n";
        std::ofstream(a) << "hello filesystem";
        fs::create_hard_link(a, b);
        print_inode(a);
        print_inode(b);
        if (fs::hard_link_count(a) != 2)
            std::cout << "  note: this C++ runtime cannot read link counts here, so it printed 1 where the slide says 2\n";
        fs::remove(a);
        std::string text;
        std::ifstream in(b);
        std::getline(in, text);
        in.close();
        std::cout << "  report.txt removed, alias.txt still reads: " << text << "\n";
        const bool intact = text == "hello filesystem";
        fs::remove(b);
        std::cout << "  alias.txt removed: the link count reached 0 and the blocks went back to the bitmap\n";
        std::filesystem::remove_all(dir, ignore);
        if (!intact) {
            std::cout << "FAIL: the data did not survive the removal of its first name\n";
            return 1;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // A filesystem may refuse hard links (FAT, some network and container mounts): say so and stop cleanly.
        std::cout << "this filesystem refused the demo: " << e.what() << "\n";
        std::cout << "on ext4, XFS, Btrfs or NTFS the link count goes 1, 2, 1 and the data outlives its first name\n";
        std::filesystem::remove_all(dir, ignore);
    }
    return 0;
}
