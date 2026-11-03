#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tab5_shell {

inline constexpr std::size_t kMaximumCommandBytes = 512;
inline constexpr std::size_t kMaximumOutputBytes = 16384;
inline constexpr int kStatusExit = 255;

struct MemoryInfo {
    std::uint64_t total_bytes{0};
    std::uint64_t free_bytes{0};
    std::uint64_t minimum_free_bytes{0};
};

using MemoryProvider = std::function<MemoryInfo()>;

struct DiskInfo {
    std::uint64_t total_bytes{0};
    std::uint64_t free_bytes{0};
    bool available{false};
};

using DiskProvider = std::function<DiskInfo()>;

struct Mounts {
    std::string home_root;
    std::string sd_root;
    bool sd_available{false};
};

struct ParseResult {
    bool ok{false};
    std::vector<std::string> arguments;
    std::string error;
};

struct PathResult {
    bool ok{false};
    std::string logical;
    std::string error;
};

ParseResult parse_command(std::string_view line);
PathResult normalize_path(std::string_view cwd, std::string_view path);
bool writable_logical_path(std::string_view path);

class Shell {
public:
    using Output = std::function<void(std::string_view)>;

    explicit Shell(Mounts mounts, MemoryProvider memory = {}, DiskProvider disk = {});

    bool prepare(std::string& error);
    int execute(std::string_view line, const Output& output);

    const std::string& cwd() const { return cwd_; }
    bool sd_available() const { return mounts_.sd_available; }
    bool home_available() const { return home_available_; }

    // Native modules share the same mount and symlink policy as commands.
    bool file_path(std::string_view path, bool write, std::string& physical,
                   std::string& error) const;
    bool read_file(std::string_view path, std::string& data, std::string& error,
                   std::size_t limit = 65536) const;
    bool write_file(std::string_view path, std::string_view data, std::string& error) const;

private:
    using Arguments = std::vector<std::string>;
    using Handler = int (Shell::*)(const Arguments&, const Output&);

    void emit(std::string_view text);
    int unavailable();
    bool resolve(std::string_view path, bool for_write, std::string& logical,
                 std::string& physical, std::string& error) const;
    bool ensure_no_symlink(std::string_view physical, std::string& error) const;
    bool make_directories(std::string_view physical, std::string& error) const;

    int help(const Arguments& arguments, const Output&);
    int clear(const Arguments& arguments, const Output&);
    int pwd(const Arguments& arguments, const Output&);
    int cd(const Arguments& arguments, const Output&);
    int ls(const Arguments& arguments, const Output&);
    int mkdir(const Arguments& arguments, const Output&);
    int cat(const Arguments& arguments, const Output&);
    int echo(const Arguments& arguments, const Output&);
    int touch(const Arguments& arguments, const Output&);
    int rm(const Arguments& arguments, const Output&);
    int df(const Arguments& arguments, const Output&);
    int free(const Arguments& arguments, const Output&);
    int exit(const Arguments& arguments, const Output&);

    Mounts mounts_;
    MemoryProvider memory_;
    DiskProvider disk_;
    std::unordered_map<std::string, Handler> commands_;
    std::string cwd_{"/home"};
    bool home_available_{false};
    const Output* current_output_{nullptr};
    std::size_t output_bytes_{0};
    bool output_truncated_{false};
};

}  // namespace tab5_shell
