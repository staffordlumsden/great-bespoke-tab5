#pragma once
#include "shell_core.h"
#include <string>
namespace tab5_shell {
inline constexpr std::size_t kLuaMemoryLimit=256*1024;
struct ScriptStats { std::size_t peak_bytes{0}; unsigned instructions{0}; };
int run_lua(std::string_view source, const std::vector<std::string>& arguments,
            const Shell::Output& output, ScriptStats& stats);
std::string sha256_hex(std::string_view bytes);
struct PackageMetadata { std::string name, version, entry, sha256; };
bool parse_package(std::string_view text, PackageMetadata& metadata, std::string& error);
class Packages {
public:
    explicit Packages(Shell& shell) : shell_(shell) {}
    int command(const std::vector<std::string>& args, const Shell::Output& output);
    int run(std::string_view name, const std::vector<std::string>& args, const Shell::Output& output);
private:
    bool load(std::string_view name, PackageMetadata& metadata, std::string& source, std::string& error);
    Shell& shell_;
};
} // namespace tab5_shell
