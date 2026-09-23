// Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 10: durable replace: fsync where it matters
// Build: make 10_durable_replace
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <filesystem>
#endif

static std::vector<char> bytes_of(const std::string& text) { return std::vector<char>(text.begin(), text.end()); }

static std::string contents_of(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#if defined(__linux__)
static bool write_all(int fd, const std::vector<char>& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// a crash leaves the old file or the new one, never half of each
bool durable_replace(const char* dir, const char* tmp, const char* dst,
                     const std::vector<char>& data) {
    int fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    bool ok = fd >= 0 && write_all(fd, data) && fsync(fd) == 0;
    close(fd);                              // the data is durable
    ok = ok && rename(tmp, dst) == 0;       // atomic switch of the name
    int dfd = open(dir, O_RDONLY);          // the name is metadata of
    ok = ok && fsync(dfd) == 0;             // the directory: flush it
    close(dfd);
    return ok;
}
#endif

int main() {
    const std::string v1 = "settings version 1\n";
    const std::string v2 = "settings version 2, written to a temporary file first\n";

#if defined(__linux__)
    const std::string dir = "/tmp/pagecache_lab_10_" + std::to_string(getpid());
    const std::string tmp = dir + "/settings.conf.tmp";
    const std::string dst = dir + "/settings.conf";
    if (mkdir(dir.c_str(), 0700) != 0) {
        std::cout << "could not create " << dir << ": " << std::strerror(errno) << '\n';
        return 0;
    }

    const bool first = durable_replace(dir.c_str(), tmp.c_str(), dst.c_str(), bytes_of(v1));
    std::cout << "first save:  " << (first ? "durable" : std::strerror(errno)) << ", file holds: " << contents_of(dst);
    const bool second = durable_replace(dir.c_str(), tmp.c_str(), dst.c_str(), bytes_of(v2));
    std::cout << "second save: " << (second ? "durable" : std::strerror(errno)) << ", file holds: " << contents_of(dst);

    const bool replaced = contents_of(dst) == v2;
    const bool tmp_gone = access(tmp.c_str(), F_OK) != 0;
    std::cout << "temporary file left behind: " << (tmp_gone ? "no" : "yes") << '\n';
    std::cout << "cost per save: two device flushes, one for the data and one for the directory\n";

    unlink(tmp.c_str());
    unlink(dst.c_str());
    rmdir(dir.c_str());
    return (first && second && replaced && tmp_gone) ? 0 : 1;
#else
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pagecache_lab_10";
    fs::create_directories(dir);
    const std::string tmp = (dir / "settings.conf.tmp").string();
    const std::string dst = (dir / "settings.conf").string();

    bool replaced = true;
    for (const std::string& version : {v1, v2}) {
        const std::vector<char> data = bytes_of(version);
        {
            std::ofstream out(tmp, std::ios::binary);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        fs::rename(tmp, dst);   // the atomic switch of the name is portable
        replaced = replaced && contents_of(dst) == version;
        std::cout << "saved, file holds: " << contents_of(dst);
    }
    fs::remove_all(dir);

    std::cout << "this is not Linux: the temporary file and the rename are portable, the durability is not.\n"
                 "The Linux build adds fsync on the file and on the directory, the two calls that reach the device.\n";
    return replaced ? 0 : 1;
#endif
}
