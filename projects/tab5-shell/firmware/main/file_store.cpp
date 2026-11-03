#include "shell_core.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tab5_shell {
bool Shell::file_path(std::string_view path, bool write, std::string& physical,
                      std::string& error) const {
    std::string logical;
    if (!resolve(path, write, logical, physical, error)) return false;
    if (physical.empty()) { error = "virtual root is not a file"; return false; }
    return true;
}

bool Shell::read_file(std::string_view path, std::string& data, std::string& error,
                      std::size_t limit) const {
    data.clear();
    std::string physical;
    if (!file_path(path, false, physical, error)) return false;
    struct stat info{};
    if (stat(physical.c_str(), &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > limit) {
        error = "missing, non-regular or oversized file";
        return false;
    }
    FILE* f = fopen(physical.c_str(), "rb");
    if (!f) { error = "cannot open file"; return false; }
    data.resize(static_cast<std::size_t>(info.st_size));
    bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    // Also detect growth between stat and read, rather than silently truncating.
    ok = fgetc(f) == EOF && !ferror(f) && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) { data.clear(); error = "file read failed or file changed"; }
    return ok;
}

// FAT rename cannot replace an existing destination. Like QuickNote, flush and
// fsync a new file, rotate the old file, then publish. Never delete the sole old
// copy on an error. A power loss can leave .tab5-prev/.tab5-new for recovery.
bool Shell::write_file(std::string_view path, std::string_view data, std::string& error) const {
    std::string final, fresh, previous;
    if (data.size() > 65536) { error = "file exceeds 64 KiB"; return false; }
    if (!file_path(path, true, final, error) ||
        !file_path(std::string(path) + ".tab5-new", true, fresh, error) ||
        !file_path(std::string(path) + ".tab5-prev", true, previous, error)) return false;
    struct stat info{};
    if (!stat(previous.c_str(), &info)) {
        error = "recovery file exists: inspect .tab5-prev before saving";
        return false;
    }
    if (errno != ENOENT) { error = "cannot inspect recovery file"; return false; }
    bool existed = stat(final.c_str(), &info) == 0;
    if ((existed && !S_ISREG(info.st_mode)) || (!existed && errno != ENOENT)) {
        error = "destination is not a regular file"; return false;
    }
    int fd = open(fresh.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { error = "cannot create .tab5-new (check recovery files)"; return false; }
    FILE* f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(fresh.c_str()); error = "cannot open writer"; return false; }
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) { unlink(fresh.c_str()); error = "write/flush failed; original preserved"; return false; }
    if (existed && rename(final.c_str(), previous.c_str())) {
        unlink(fresh.c_str()); error = "cannot preserve original"; return false;
    }
    if (rename(fresh.c_str(), final.c_str())) {
        if (existed) (void)rename(previous.c_str(), final.c_str());
        error = "publish failed; inspect recovery files"; return false;
    }
    if (existed && unlink(previous.c_str())) {
        error = "file saved; previous copy cleanup failed"; return false;
    }
    return true;
}
} // namespace tab5_shell
