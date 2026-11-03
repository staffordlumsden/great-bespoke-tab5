#include "shell_core.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace tab5_shell {
namespace {

constexpr int kStatusOk = 0;
constexpr int kStatusFailure = 1;
constexpr int kStatusUsage = 2;
constexpr int kStatusDenied = 126;
constexpr int kStatusNotFound = 127;

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool path_is(std::string_view path, std::string_view root) {
    return path == root ||
           (starts_with(path, root) && path.size() > root.size() &&
            path[root.size()] == '/');
}

std::string join(std::string_view left, std::string_view right) {
    std::string result(left);
    if (result.empty() || result.back() != '/') result.push_back('/');
    result.append(right);
    return result;
}

std::string error_text(std::string_view context, int error_number) {
    std::string result(context);
    result.append(": ");
    result.append(std::strerror(error_number));
    return result;
}

std::string size_text(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[48];
    if (unit == 0)
        std::snprintf(buffer, sizeof(buffer), "%llu %s",
                      static_cast<unsigned long long>(bytes), units[unit]);
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

std::string safe_name(std::string_view name) {
    std::string result(name);
    for (char& character : result) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 32 || value == 127) character = '?';
    }
    return result;
}

int path_lstat(const char* path, struct stat* info) {
#ifdef ESP_PLATFORM
    // FATFS exposes no symbolic links; stat is the portable ESP-IDF path.
    return stat(path, info);
#else
    return lstat(path, info);
#endif
}

std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (end > start) parts.emplace_back(path.substr(start, end - start));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return parts;
}

int resolution_status(const std::string& error) {
    return starts_with(error, "SD unavailable") ? kStatusFailure : kStatusDenied;
}

}  // namespace

ParseResult parse_command(std::string_view line) {
    ParseResult result;
    std::string token;
    bool token_started = false;
    char quote = 0;
    bool escaped = false;

    for (char character : line) {
        if (escaped) {
            token.push_back(character);
            token_started = true;
            escaped = false;
            continue;
        }
        if (character == '\\' && quote != '\'') {
            escaped = true;
            token_started = true;
            continue;
        }
        if (quote) {
            if (character == quote) {
                quote = 0;
            } else {
                token.push_back(character);
            }
            token_started = true;
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            token_started = true;
            continue;
        }
        if (character == ' ' || character == '\t') {
            if (token_started) {
                result.arguments.push_back(std::move(token));
                token.clear();
                token_started = false;
            }
            continue;
        }
        token.push_back(character);
        token_started = true;
    }

    if (escaped) {
        result.error = "trailing escape";
        return result;
    }
    if (quote) {
        result.error = quote == '\'' ? "unterminated single quote"
                                     : "unterminated double quote";
        return result;
    }
    if (token_started) result.arguments.push_back(std::move(token));
    result.ok = true;
    return result;
}

PathResult normalize_path(std::string_view cwd, std::string_view path) {
    PathResult result;
    for (unsigned char c : path) {
        if (c < 32 || c == 127 || c == '\\' || c == ':') {
            result.error = "invalid path character";
            return result;
        }
    }
    if (path.size() > 1024 || cwd.size() > 1024) {
        result.error = "path too long";
        return result;
    }

    std::string combined;
    if (path == "~" || starts_with(path, "~/")) {
        combined = "/home";
        if (path.size() > 1) combined.append(path.substr(1));
    } else if (!path.empty() && path.front() == '/') {
        combined.assign(path);
    } else {
        combined.assign(cwd);
        if (combined.empty() || combined.front() != '/') combined = "/home";
        if (!path.empty()) combined = join(combined, path);
    }

    std::vector<std::string> parts;
    for (const auto& part : split_path(combined)) {
        if (part == ".") continue;
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        if (part.back() == '.' || part.back() == ' ') {
            result.error = "ambiguous FAT path component";
            return result;
        }
        parts.push_back(part);
    }
    result.logical = "/";
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index) result.logical.push_back('/');
        result.logical.append(parts[index]);
    }
    result.ok = true;
    return result;
}

bool writable_logical_path(std::string_view path) {
    return path_is(path, "/home");
}

Shell::Shell(Mounts mounts, MemoryProvider memory, DiskProvider disk)
    : mounts_(std::move(mounts)), memory_(std::move(memory)), disk_(std::move(disk)) {
    commands_ = {
        {"help", &Shell::help},   {"clear", &Shell::clear},
        {"pwd", &Shell::pwd},     {"cd", &Shell::cd},
        {"ls", &Shell::ls},       {"mkdir", &Shell::mkdir},
        {"cat", &Shell::cat},     {"echo", &Shell::echo},
        {"touch", &Shell::touch}, {"rm", &Shell::rm},
        {"df", &Shell::df},       {"free", &Shell::free},
        {"exit", &Shell::exit},
    };
}

bool Shell::prepare(std::string& error) {
    home_available_ = false;
    if (!mounts_.sd_available) {
        error = "SD unavailable; filesystem commands are disabled";
        return false;
    }
    if (mounts_.home_root.empty() || mounts_.sd_root.empty()) {
        error = "invalid filesystem mount";
        return false;
    }

    if (!path_is(mounts_.home_root, mounts_.sd_root) ||
        mounts_.home_root == mounts_.sd_root) {
        error = "home mount is outside /sd";
        return false;
    }
    if (!ensure_no_symlink(mounts_.home_root, error)) return false;
    if (!make_directories(mounts_.home_root, error)) return false;

    struct stat info {};
    if (path_lstat(mounts_.home_root.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        error = error_text("home is not a directory", errno ? errno : ENOTDIR);
        return false;
    }
    home_available_ = true;
    return true;
}

int Shell::execute(std::string_view line, const Output& output) {
    if (!output) return kStatusFailure;
    if (line.size() > kMaximumCommandBytes) {
        output("error: command exceeds 512 bytes\n");
        return kStatusUsage;
    }

    const ParseResult parsed = parse_command(line);
    if (!parsed.ok) {
        output("error: " + parsed.error + "\n");
        return kStatusUsage;
    }
    if (parsed.arguments.empty()) return kStatusOk;

    const auto command = commands_.find(parsed.arguments.front());
    if (command == commands_.end()) {
        output("error: " + safe_name(parsed.arguments.front()) + ": command not found\n");
        return kStatusNotFound;
    }

    current_output_ = &output;
    output_bytes_ = 0;
    output_truncated_ = false;
    Arguments arguments(parsed.arguments.begin() + 1, parsed.arguments.end());
    int status = (this->*(command->second))(arguments, output);

    if (output_truncated_) {
        static constexpr std::string_view marker = "\n[output truncated]\n";
        output(marker);
        if (status == kStatusOk) status = kStatusFailure;
    }
    current_output_ = nullptr;
    return status;
}

void Shell::emit(std::string_view text) {
    if (!current_output_ || !*current_output_ || output_bytes_ >= kMaximumOutputBytes) {
        if (!text.empty()) output_truncated_ = true;
        return;
    }
    const std::size_t remaining = kMaximumOutputBytes - output_bytes_;
    const std::size_t count = std::min(remaining, text.size());
    if (count) (*current_output_)(text.substr(0, count));
    output_bytes_ += count;
    if (count != text.size()) output_truncated_ = true;
}

int Shell::unavailable() {
    emit("error: SD unavailable; home and /sd are inaccessible\n");
    return kStatusFailure;
}

bool Shell::resolve(std::string_view path, bool for_write, std::string& logical,
                    std::string& physical, std::string& error) const {
    const PathResult normalized = normalize_path(cwd_, path);
    if (!normalized.ok) {
        error = normalized.error;
        return false;
    }
    logical = normalized.logical;
    if (logical == "/") {
        physical.clear();
        if (for_write) {
            error = "permission denied: write access is limited to /home";
            return false;
        }
        return true;
    }
    if (!mounts_.sd_available) {
        error = "SD unavailable";
        return false;
    }

    std::string folded = logical;
    for (char& c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (path_is(folded, "/sd/private")) {
        error = "permission denied: /private is protected";
        return false;
    }

    std::string_view suffix;
    if (path_is(logical, "/home")) {
        suffix = std::string_view(logical).substr(5);
        if (!suffix.empty() && suffix.front() == '/') suffix.remove_prefix(1);
        physical = suffix.empty() ? mounts_.home_root : join(mounts_.home_root, suffix);
    } else if (path_is(logical, "/sd")) {
        suffix = std::string_view(logical).substr(3);
        if (!suffix.empty() && suffix.front() == '/') suffix.remove_prefix(1);
        physical = suffix.empty() ? mounts_.sd_root : join(mounts_.sd_root, suffix);
    } else {
        error = "path is outside /home and /sd";
        return false;
    }

    if (for_write && !writable_logical_path(logical)) {
        error = "permission denied: write access is limited to /home";
        return false;
    }
    if (!ensure_no_symlink(physical, error)) return false;
    return true;
}

bool Shell::ensure_no_symlink(std::string_view physical, std::string& error) const {
    std::string_view current_root = mounts_.sd_root;
    if (!path_is(physical, current_root)) {
        error = "path escaped the removable-card root";
        return false;
    }

    std::string current(mounts_.sd_root);
    const std::string_view relative = physical.substr(mounts_.sd_root.size());
    std::size_t start = 0;
    while (start < relative.size()) {
        while (start < relative.size() && relative[start] == '/') ++start;
        if (start >= relative.size()) break;
        const std::size_t slash = relative.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? relative.size() : slash;
        current = join(current, relative.substr(start, end - start));
        struct stat info {};
        if (path_lstat(current.c_str(), &info) != 0) {
            if (errno == ENOENT) return true;
            error = error_text("inspect path", errno);
            return false;
        }
        if (S_ISLNK(info.st_mode)) {
            error = "permission denied: symbolic links are not followed";
            return false;
        }
        start = end;
    }
    return true;
}

bool Shell::make_directories(std::string_view physical, std::string& error) const {
    if (!path_is(physical, mounts_.sd_root)) {
        error = "directory escaped the removable-card root";
        return false;
    }
    std::string current(mounts_.sd_root);
    const std::string_view relative = physical.substr(mounts_.sd_root.size());
    std::size_t start = 0;
    while (start < relative.size()) {
        while (start < relative.size() && relative[start] == '/') ++start;
        if (start >= relative.size()) break;
        const std::size_t slash = relative.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? relative.size() : slash;
        current = join(current, relative.substr(start, end - start));
        struct stat info {};
        if (path_lstat(current.c_str(), &info) == 0) {
            if (S_ISLNK(info.st_mode)) {
                error = "permission denied: symbolic links are not followed";
                return false;
            }
            if (!S_ISDIR(info.st_mode)) {
                error = "path component is not a directory";
                return false;
            }
        } else if (errno == ENOENT) {
            if (::mkdir(current.c_str(), 0775) != 0 && errno != EEXIST) {
                error = error_text("create directory", errno);
                return false;
            }
        } else {
            error = error_text("inspect directory", errno);
            return false;
        }
        start = end;
    }
    return true;
}

int Shell::help(const Arguments&, const Output&) {
    emit("Tab5 Shell 0.1.0 - local command set\n"
         "  help             show this text\n"
         "  clear            clear the terminal\n"
         "  pwd              print logical directory\n"
         "  cd [path]        change directory (default /home)\n"
         "  ls [path]        list a directory\n"
         "  mkdir [-p] path  create directories under /home\n"
         "  cat path...      print files, bounded to 16 KiB\n"
         "  echo text...     print arguments\n"
         "  touch path...    create files under /home\n"
         "  rm [-f] path...  remove files or empty dirs under /home\n"
         "  df               show removable-card space\n"
         "  free             show local P4 heap statistics\n"
         "  exit             return to Console launcher\n"
         "/home is writable; /sd is read-only. Quote paths containing spaces.\n");
    return kStatusOk;
}

int Shell::clear(const Arguments& arguments, const Output&) {
    if (!arguments.empty()) {
        emit("usage: clear\n");
        return kStatusUsage;
    }
    emit("\x1b[2J\x1b[H");
    return kStatusOk;
}

int Shell::pwd(const Arguments& arguments, const Output&) {
    if (!arguments.empty()) {
        emit("usage: pwd\n");
        return kStatusUsage;
    }
    emit(cwd_);
    emit("\n");
    return kStatusOk;
}

int Shell::cd(const Arguments& arguments, const Output&) {
    if (arguments.size() > 1) {
        emit("usage: cd [path]\n");
        return kStatusUsage;
    }
    if (!mounts_.sd_available) return unavailable();
    const std::string_view target = arguments.empty() ? std::string_view("/home")
                                                       : std::string_view(arguments[0]);
    std::string logical;
    std::string physical;
    std::string error;
    if (!resolve(target, false, logical, physical, error)) {
        emit("error: " + error + "\n");
        return resolution_status(error);
    }
    if (logical != "/") {
        struct stat info {};
        if (stat(physical.c_str(), &info) != 0) {
            emit("cd: " + error_text(logical, errno) + "\n");
            return kStatusFailure;
        }
        if (!S_ISDIR(info.st_mode)) {
            emit("cd: not a directory: " + logical + "\n");
            return kStatusFailure;
        }
    }
    cwd_ = logical;
    return kStatusOk;
}

int Shell::ls(const Arguments& arguments, const Output&) {
    if (arguments.size() > 1) {
        emit("usage: ls [path]\n");
        return kStatusUsage;
    }
    if (!mounts_.sd_available) return unavailable();
    const std::string_view target = arguments.empty() ? std::string_view(cwd_)
                                                       : std::string_view(arguments[0]);
    std::string logical;
    std::string physical;
    std::string error;
    if (!resolve(target, false, logical, physical, error)) {
        emit("ls: " + error + "\n");
        return resolution_status(error);
    }

    if (logical == "/") {
        emit("home/\nsd/\n");
        return kStatusOk;
    }

    struct stat info {};
    if (path_lstat(physical.c_str(), &info) != 0) {
        emit("ls: " + error_text(logical, errno) + "\n");
        return kStatusFailure;
    }
    if (!S_ISDIR(info.st_mode)) {
        std::size_t slash = logical.find_last_of('/');
        emit(safe_name(slash == std::string::npos ? logical : logical.substr(slash + 1)));
        emit("\n");
        return kStatusOk;
    }

    DIR* directory = opendir(physical.c_str());
    if (!directory) {
        emit("ls: " + error_text(logical, errno) + "\n");
        return kStatusFailure;
    }
    std::vector<std::string> names;
    bool capped = false;
    while (dirent* entry = readdir(directory)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        if (names.size() >= 200) {
            capped = true;
            break;
        }
        names.emplace_back(entry->d_name);
    }
    closedir(directory);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        const std::string path = join(physical, name);
        struct stat child {};
        const bool is_directory = path_lstat(path.c_str(), &child) == 0 && S_ISDIR(child.st_mode);
        emit(safe_name(name));
        if (is_directory) emit("/");
        emit("\n");
    }
    if (capped) {
        emit("[listing stopped after 200 entries]\n");
        return kStatusFailure;
    }
    return kStatusOk;
}

int Shell::mkdir(const Arguments& arguments, const Output&) {
    if (arguments.empty()) {
        emit("usage: mkdir [-p] path...\n");
        return kStatusUsage;
    }
    bool parents = false;
    std::size_t first = 0;
    for (; first < arguments.size(); ++first) {
        const std::string& arg = arguments[first];
        if (arg == "--") {
            ++first;
            break;
        }
        if (arg.empty() || arg[0] != '-' || arg == "-") {
            break;
        }
        if (arg == "-p") {
            parents = true;
            continue;
        }
        emit("mkdir: invalid option -- '" + arg.substr(1) + "'\n");
        return kStatusUsage;
    }
    if (first >= arguments.size()) {
        emit("usage: mkdir [-p] path...\n");
        return kStatusUsage;
    }

    int status = kStatusOk;
    for (; first < arguments.size(); ++first) {
        std::string logical;
        std::string physical;
        std::string error;
        if (!resolve(arguments[first], true, logical, physical, error)) {
            emit("mkdir: " + error + "\n");
            status = resolution_status(error);
            continue;
        }
        if (logical == "/home") {
            emit("mkdir: cannot create /home\n");
            status = kStatusFailure;
            continue;
        }
        const int result = parents ? (make_directories(physical, error) ? 0 : -1)
                                   : ::mkdir(physical.c_str(), 0775);
        if (result != 0) {
            const int saved = errno;
            emit("mkdir: " + error_text(logical, saved) + "\n");
            status = kStatusFailure;
        }
    }
    return status;
}

int Shell::cat(const Arguments& arguments, const Output&) {
    if (arguments.empty()) {
        emit("usage: cat path...\n");
        return kStatusUsage;
    }
    int status = kStatusOk;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        std::string logical;
        std::string physical;
        std::string error;
        if (!resolve(arguments[index], false, logical, physical, error)) {
            emit("cat: " + error + "\n");
            status = resolution_status(error);
            continue;
        }
        struct stat info {};
        if (path_lstat(physical.c_str(), &info) != 0) {
            emit("cat: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
            continue;
        }
        if (!S_ISREG(info.st_mode)) {
            emit("cat: not a regular file: " + logical + "\n");
            status = kStatusFailure;
            continue;
        }
        const int fd = open(physical.c_str(), O_RDONLY);
        if (fd < 0) {
            emit("cat: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
            continue;
        }
        char buffer[1024];
        for (;;) {
            const ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count < 0) {
                emit("cat: " + error_text(logical, errno) + "\n");
                status = kStatusFailure;
                break;
            }
            if (count == 0) break;
            for (ssize_t i = 0; i < count; ++i) {
                const unsigned char c = static_cast<unsigned char>(buffer[i]);
                if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127)
                    buffer[i] = '?';
            }
            const std::size_t available = kMaximumOutputBytes - output_bytes_;
            if (static_cast<std::size_t>(count) > available) {
                emit(std::string_view(buffer, available));
                output_truncated_ = true;
                status = kStatusFailure;
                break;
            }
            emit(std::string_view(buffer, static_cast<std::size_t>(count)));
        }
        close(fd);
        if (output_truncated_) break;
    }
    return status;
}

int Shell::echo(const Arguments& arguments, const Output&) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index) emit(" ");
        emit(arguments[index]);
    }
    emit("\n");
    return kStatusOk;
}

int Shell::touch(const Arguments& arguments, const Output&) {
    if (arguments.empty()) {
        emit("usage: touch path...\n");
        return kStatusUsage;
    }
    int status = kStatusOk;
    for (const auto& argument : arguments) {
        std::string logical;
        std::string physical;
        std::string error;
        if (!resolve(argument, true, logical, physical, error)) {
            emit("touch: " + error + "\n");
            status = resolution_status(error);
            continue;
        }
        const int fd = open(physical.c_str(), O_WRONLY | O_CREAT, 0664);
        if (fd < 0) {
            emit("touch: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
            continue;
        }
        close(fd);
        if (utime(physical.c_str(), nullptr) != 0) {
            emit("touch: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
        }
    }
    return status;
}

int Shell::rm(const Arguments& arguments, const Output&) {
    if (arguments.empty()) {
        emit("usage: rm [-f] path...\n");
        return kStatusUsage;
    }
    bool force = false;
    std::size_t first = 0;
    for (; first < arguments.size(); ++first) {
        const std::string& arg = arguments[first];
        if (arg == "--") {
            ++first;
            break;
        }
        if (arg.empty() || arg[0] != '-' || arg == "-") {
            break;
        }
        if (arg == "-r" || arg == "-R" || arg == "-rf" || arg == "-fr" ||
            arg == "-rF" || arg == "-Fr") {
            emit("rm: recursive removal is not implemented\n");
            return kStatusUsage;
        }
        if (arg == "-f") {
            force = true;
            continue;
        }
        emit("rm: invalid option -- '" + arg.substr(1) + "'\n");
        return kStatusUsage;
    }
    if (first >= arguments.size()) {
        emit("usage: rm [-f] path...\n");
        return kStatusUsage;
    }

    int status = kStatusOk;
    for (; first < arguments.size(); ++first) {
        std::string logical;
        std::string physical;
        std::string error;
        if (!resolve(arguments[first], true, logical, physical, error)) {
            emit("rm: " + error + "\n");
            status = resolution_status(error);
            continue;
        }
        if (logical == "/home") {
            emit("rm: refusing to remove /home\n");
            status = kStatusDenied;
            continue;
        }
        struct stat info {};
        if (path_lstat(physical.c_str(), &info) != 0) {
            if (force && errno == ENOENT) continue;
            emit("rm: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
            continue;
        }
        const int result = S_ISDIR(info.st_mode) ? rmdir(physical.c_str())
                                                  : unlink(physical.c_str());
        if (result != 0) {
            emit("rm: " + error_text(logical, errno) + "\n");
            status = kStatusFailure;
        }
    }
    return status;
}

int Shell::exit(const Arguments& arguments, const Output&) {
    if (!arguments.empty()) {
        emit("usage: exit\n");
        return kStatusUsage;
    }
    emit("exit: returning to Console\n");
    return kStatusExit;
}

int Shell::df(const Arguments& arguments, const Output&) {
    if (!arguments.empty()) {
        emit("usage: df\n");
        return kStatusUsage;
    }
    if (!mounts_.sd_available) return unavailable();
    if (!disk_) {
        emit("df: disk provider unavailable\n");
        return kStatusFailure;
    }
    const DiskInfo info = disk_();
    if (!info.available) {
        emit("df: removable-card statistics unavailable\n");
        return kStatusFailure;
    }
    const std::uint64_t total = info.total_bytes;
    const std::uint64_t free = info.free_bytes;
    const std::uint64_t used = total >= free ? total - free : 0;
    emit("Filesystem      Size      Used     Avail  Mounted on\n");
    emit("sd          " + size_text(total) + "  " + size_text(used) + "  " +
         size_text(free) + "  /sd\n");
    return kStatusOk;
}

int Shell::free(const Arguments& arguments, const Output&) {
    if (!arguments.empty()) {
        emit("usage: free\n");
        return kStatusUsage;
    }
    if (!memory_) {
        emit("free: memory provider unavailable\n");
        return kStatusFailure;
    }
    const MemoryInfo info = memory_();
    emit("             total        free       minimum\n");
    emit("heap    " + size_text(info.total_bytes) + "  " + size_text(info.free_bytes) +
         "  " + size_text(info.minimum_free_bytes) + "\n");
    return kStatusOk;
}

}  // namespace tab5_shell
