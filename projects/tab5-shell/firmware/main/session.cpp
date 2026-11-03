#include "session.h"
#include <algorithm>
#include <charconv>

namespace tab5_shell {
namespace {
bool identifier(std::string_view value, std::size_t limit) {
    return !value.empty() && value.size() <= limit && value.front() != '-' &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        });
}
bool port_number(std::string_view value, std::uint16_t& port) {
    unsigned n=0;
    auto r=std::from_chars(value.data(),value.data()+value.size(),n);
    if (r.ec != std::errc{} || r.ptr != value.data()+value.size() || n == 0 || n > 65535) return false;
    port=static_cast<std::uint16_t>(n); return true;
}
std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        auto end=text.find('\n');
        result.push_back(text.substr(0,end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end+1);
    }
    return result;
}
}
bool parse_ssh(const std::vector<std::string>& args, SshRequest& request, std::string& error) {
    request = {};
    std::string target;
    bool port_set=false, key_set=false;
    for (std::size_t i=0;i<args.size();++i) {
        if (args[i] == "-p" || args[i] == "-i" || args[i] == "--profile") {
            const auto flag=args[i];
            if (++i == args.size()) { error="missing SSH option value"; return false; }
            if (flag == "-p") {
                if (port_set || !port_number(args[i],request.port)) { error="invalid/duplicate port"; return false; }
                port_set=true;
            } else if (flag == "-i") {
                auto path=normalize_path("/",args[i]);
                if (key_set || !path.ok || path.logical != args[i] || !args[i].starts_with("/private/") || args[i].size()>150) {
                    error="key must be a safe /private/ path"; return false;
                }
                request.key=args[i]; key_set=true;
            } else {
                if (!request.profile.empty() || !identifier(args[i],32)) { error="invalid profile"; return false; }
                request.profile=args[i];
            }
        } else if (!target.empty() || args[i].starts_with('-')) { error="unsupported SSH argument"; return false; }
        else target=args[i];
    }
    if (!request.profile.empty()) {
        if (!target.empty() || port_set || key_set) { error="profile cannot be combined with a target/options"; return false; }
        return true;
    }
    auto at=target.find('@');
    if (at == std::string::npos || !identifier(target.substr(0,at),63) || !identifier(target.substr(at+1),127)) {
        error="usage: ssh [-p PORT] [-i /private/KEY] user@hostname | ssh --profile NAME"; return false;
    }
    request.user=target.substr(0,at); request.host=target.substr(at+1);
    for (char& c:request.host) if (c>='A' && c<='Z') c += 'a'-'A';
    return true;
}
bool select_profile(std::string_view contents, std::string_view name, SshRequest& request, std::string& error) {
    const std::string selected(name); // request may own the supplied string view
    if (contents.size()>16384) { error="profiles exceed 16 KiB"; return false; }
    bool found=false;
    for (auto line:lines(contents)) {
        if (line.empty() || line.front()=='#') continue;
        auto parsed=parse_command(line);
        if (!parsed.ok || parsed.arguments.empty()) { error="malformed profile file"; return false; }
        if (parsed.arguments.front()!=selected) continue;
        if (found) { error="duplicate profile"; return false; }
        std::vector<std::string> args(parsed.arguments.begin()+1, parsed.arguments.end());
        if (!parse_ssh(args,request,error) || !request.profile.empty()) { error="invalid profile target"; return false; }
        found=true;
    }
    if (!found) error="profile not found (none selected automatically)";
    return found;
}
bool known_host_matches(std::string_view contents, const SshRequest& request,
                        const std::array<std::uint8_t,32>& fingerprint) {
    if (contents.size()>16384) return false;
    bool matched=false;
    for (auto line:lines(contents)) {
        if (line.empty() || line.front()=='#') continue;
        auto p=parse_command(line);
        if (!p.ok || p.arguments.size()!=3) return false;
        std::uint16_t port=0;
        if (!identifier(p.arguments[0],127) || !port_number(p.arguments[1],port) || p.arguments[2].size()!=64) return false;
        std::array<std::uint8_t,32> fp{};
        for (std::size_t i=0;i<64;++i) {
            char c=p.arguments[2][i];
            int value=(c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1;
            if (value<0) return false;
            fp[i/2] |= static_cast<std::uint8_t>(value << ((i%2)?0:4));
        }
        if (p.arguments[0] == request.host && port == request.port) {
            if (matched) return false; // ambiguous duplicate pin fails closed
            unsigned difference=0;
            for (std::size_t i=0;i<32;++i) difference |= fp[i]^fingerprint[i];
            if (difference) return false;
            matched=true;
        }
    }
    return matched;
}
} // namespace tab5_shell
