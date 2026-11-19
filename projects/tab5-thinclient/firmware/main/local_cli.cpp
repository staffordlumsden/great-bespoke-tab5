#include "local_cli.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings_store.hpp"
#include "tab5_hal.h"
#include "tofu.hpp"

namespace {

bool endsWithIgnoreCase(const String& s, const char* suffix) {
    if (!suffix) return false;
    size_t slen = strlen(suffix);
    if (s.length() < slen) return false;
    String tail = s.substring(s.length() - slen);
    tail.toLowerCase();
    return tail == suffix;
}

const char* ansiColorForFilename(const String& name, bool isDir) {
    if (isDir) return "\x1B[1;34m";  // bright blue directories
    if (endsWithIgnoreCase(name, ".py") || endsWithIgnoreCase(name, ".sh")) return "\x1B[1;32m";  // bright green scripts
    if (endsWithIgnoreCase(name, ".png") || endsWithIgnoreCase(name, ".jpg") ||
        endsWithIgnoreCase(name, ".jpeg") || endsWithIgnoreCase(name, ".bmp")) return "\x1B[1;35m";  // magenta images
    if (endsWithIgnoreCase(name, ".json") || endsWithIgnoreCase(name, ".txt") ||
        endsWithIgnoreCase(name, ".md") || endsWithIgnoreCase(name, ".log")) return "\x1B[36m";  // cyan text/config
    if (endsWithIgnoreCase(name, ".bin") || endsWithIgnoreCase(name, ".elf") ||
        endsWithIgnoreCase(name, ".zip") || endsWithIgnoreCase(name, ".tar")) return "\x1B[33m";  // yellow binaries
    return "\x1B[0m";
}

bool fileExists(const String& path) {
    if (!path.length()) return false;
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool parseUserHostArgs(const std::vector<String>& tokens, size_t startIdx, SshProfile& out) {
    if (startIdx >= tokens.size()) return false;
    const String& target = tokens[startIdx];
    int at = target.indexOf('@');
    if (at <= 0 || at + 1 >= static_cast<int>(target.length())) return false;
    out.user = target.substring(0, static_cast<size_t>(at));
    out.host = target.substring(static_cast<size_t>(at + 1));
    int colon = out.host.indexOf(':');
    if (colon > 0) {
        long p = out.host.substring(static_cast<size_t>(colon + 1)).toInt();
        if (p >= 1 && p <= 65535) out.port = static_cast<uint16_t>(p);
        out.host = out.host.substring(0, static_cast<size_t>(colon));
    }
    for (size_t i = startIdx + 1; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "-p") {
            long p = tokens[i + 1].toInt();
            if (p >= 1 && p <= 65535) out.port = static_cast<uint16_t>(p);
            ++i;
        } else if (tokens[i] == "-i") {
            out.keyPath = settings_store::resolveSdKeyPath(tokens[i + 1]);
            ++i;
        }
    }
    if (!out.name.length()) {
        out.name = out.user + "@" + out.host;
    }
    return out.host.length() > 0 && out.user.length() > 0;
}

bool copyVfsFile(const String& src, const String& dst, String& err) {
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) {
        err = String("cannot open ") + src;
        return false;
    }
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) {
        fclose(in);
        err = String("cannot create ") + dst;
        return false;
    }
    uint8_t buf[2048];
    size_t n = 0;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            err = "write failed";
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

String humanSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
        snprintf(buf, sizeof(buf), "%.1f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (1024ULL * 1024ULL)) {
        snprintf(buf, sizeof(buf), "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, sizeof(buf), "%.1f KiB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return String(buf);
}

void printMan(CliContext& ctx, const String& topic) {
    String t = topic;
    t.toLowerCase();
    if (!t.length() || t == "help") {
        ctx.appendLine("\x1B[1;36mTab5ThinClient Local Shell Commands:\x1B[0m");
        ctx.appendLine("  \x1B[1;32mstatus\x1B[0m           Show Wi-Fi, SD, rotation, and SSH profile status");
        ctx.appendLine("  \x1B[1;32mwifi\x1B[0m \x1B[33m<scan|connect|disconnect|retry>\x1B[0m   Manage Wi-Fi");
        ctx.appendLine("  \x1B[1;32mssh\x1B[0m \x1B[33m[#|name|ping|add|rm|sync|tofu]\x1B[0m  Interactive SSH manager or connect");
        ctx.appendLine("  \x1B[1;32mconnect\x1B[0m / \x1B[1;32mdisc\x1B[0m   Connect active SSH profile / disconnect");
        ctx.appendLine("  \x1B[1;32mscp\x1B[0m \x1B[33m<src> <dst>\x1B[0m  Transfer files (prefix remote with ':' e.g. :/path)");
        ctx.appendLine("  \x1B[1;32mpython\x1B[0m \x1B[33m[file|-i]\x1B[0m Run MicroPython file on SD or enter REPL");
        ctx.appendLine("  \x1B[1;32mimage\x1B[0m \x1B[33m<f> [mode]\x1B[0m View JPG/PNG/BMP on SD (fit|center|half|quarter)");
        ctx.appendLine("  \x1B[1;32mls|cd|pwd|cat|head|tail|stat|mkdir|rmdir|cp|mv|rm|touch|df\x1B[0m");
        ctx.appendLine("  \x1B[1;32mrot\x1B[0m \x1B[33m<0|90|180|270>\x1B[0m   Rotate display & touch (landscape / portrait)");
        ctx.appendLine("  \x1B[1;32mCtrl++\x1B[0m / \x1B[1;32mCtrl+-\x1B[0m  Increase / decrease terminal font size");
        ctx.appendLine("  \x1B[1;32mtheme\x1B[0m \x1B[33m<dark|classic>\x1B[0m Switch Console Dark / Classic terminal palette");
        ctx.appendLine("  \x1B[1;32msd-sync\x1B[0m \x1B[33m[--replace]\x1B[0m  Re-import /sdcard/private SSH profiles & keys");
        ctx.appendLine("  \x1B[1;32mconsole\x1B[0m          Unmount SD cleanly and return to Tab5 Console");
        return;
    }
    if (t == "ssh") {
        ctx.appendLine("\x1B[1;36mssh / scp usage:\x1B[0m");
        ctx.appendLine("  \x1B[1;32mssh\x1B[0m                              Open interactive SSH manager (choose/add/ping)");
        ctx.appendLine("  \x1B[1;32mssh\x1B[0m \x1B[33m<#|name>\x1B[0m                     Select saved profile (#1..N) and connect");
        ctx.appendLine("  \x1B[1;32mssh ping\x1B[0m \x1B[33m<#|name|host[:port]>\x1B[0m    TCP probe & read SSH banner + RTT ms");
        ctx.appendLine("  \x1B[1;32mssh\x1B[0m \x1B[33m<user>@<host> [-p P] [-i K]\x1B[0m Save/update & connect directly");
        ctx.appendLine("  \x1B[1;32mssh add\x1B[0m \x1B[33m<name> <user>@<host> ...\x1B[0m Add or update profile in NVS");
        ctx.appendLine("  \x1B[1;32mssh rm\x1B[0m \x1B[33m<index|name|host>\x1B[0m        Remove a stale SSH profile");
        ctx.appendLine("  \x1B[1;32mssh clear\x1B[0m                        Remove all SSH profiles from NVS");
        ctx.appendLine("  \x1B[1;32mssh sync\x1B[0m \x1B[33m[--replace]\x1B[0m            Sync from /sdcard/private/ files");
        ctx.appendLine("  \x1B[1;32mssh tofu\x1B[0m \x1B[33m[clear [host]]\x1B[0m         List or reset cached host keys");
        ctx.appendLine("  \x1B[1;32mscp\x1B[0m \x1B[33m:remote sd:/local\x1B[0m           Download from active SSH host");
        return;
    }
    if (t == "python") {
        ctx.appendLine("\x1B[1;36mMicroPython REPL & Script Runner:\x1B[0m");
        ctx.appendLine("  \x1B[1;32mpython\x1B[0m                  Start interactive MicroPython REPL (type 'exit' to leave)");
        ctx.appendLine("  \x1B[1;32mpython\x1B[0m \x1B[33m/demos/hat.py\x1B[0m    Run script from SD card (gfx & Pin/ADC/I2C/SPI/UART)");
        ctx.appendLine("  \x1B[90mKeyboard interrupt\x1B[0m      Ctrl+C or Esc stops running scripts");
        return;
    }
    ctx.appendLine(String("\x1B[33mNo manual entry for '") + t + "'. Try 'man help', 'man ssh', 'man python'.\x1B[0m");
}

}  // namespace

String normalizeSdRelPath(const String& rawPath, const String& cwd) {
    String p = rawPath;
    p.trim();
    if (p.startsWith("sd:")) p = p.substring(3);
    if (p.startsWith("/sdcard/")) p = p.substring(7);
    else if (p == "/sdcard") p = "/";

    if (!p.length()) p = cwd;
    else if (p[0] != '/') {
        p = (cwd == "/" ? String("/") : cwd + "/") + p;
    }

    std::vector<String> segs;
    size_t i = 0;
    while (i < p.length()) {
        while (i < p.length() && p[i] == '/') ++i;
        if (i >= p.length()) break;
        size_t j = i;
        while (j < p.length() && p[j] != '/') ++j;
        String seg = p.substring(i, j);
        if (seg == "..") {
            if (!segs.empty()) segs.pop_back();
        } else if (seg.length() && seg != ".") {
            segs.push_back(seg);
        }
        i = j;
    }
    if (segs.empty()) return "/";
    String out;
    for (const auto& s : segs) {
        out += "/";
        out += s;
    }
    return out;
}

String sdVfsPath(const String& rawPath, const String& cwd) {
    String rel = normalizeSdRelPath(rawPath, cwd);
    if (rel == "/") return "/sdcard";
    return String("/sdcard") + rel;
}

std::vector<String> splitCommandTokens(const String& line) {
    std::vector<String> out;
    size_t i = 0;
    const size_t n = line.length();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n) break;
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i++];
            size_t start = i;
            while (i < n && line[i] != q) ++i;
            out.push_back(line.substring(start, i));
            if (i < n && line[i] == q) ++i;
        } else {
            size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            out.push_back(line.substring(start, i));
        }
    }
    return out;
}

namespace {

bool parseHostPortTarget(const String& raw, String& host, uint16_t& port) {
    String s = raw;
    s.trim();
    if (!s.length()) return false;
    int at = s.indexOf('@');
    if (at >= 0) s = s.substring(static_cast<size_t>(at + 1));
    port = 22;
    int colon = s.lastIndexOf(':');
    if (colon > 0) {
        long p = s.substring(static_cast<size_t>(colon + 1)).toInt();
        if (p >= 1 && p <= 65535) {
            port = static_cast<uint16_t>(p);
            s = s.substring(0, static_cast<size_t>(colon));
        }
    }
    s.trim();
    if (!s.length()) return false;
    host = s;
    return true;
}

bool resolveOneBasedSshIndex(const CliContext& ctx, const String& tok, size_t& outIdx) {
    if (!tok.length() || ctx.config.ssh.empty()) return false;
    for (size_t i = 0; i < tok.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    }
    long num = tok.toInt();
    if (num >= 1 && static_cast<size_t>(num) <= ctx.config.ssh.size()) {
        outIdx = static_cast<size_t>(num - 1);
        return true;
    }
    if (num == 0) {
        outIdx = 0;
        return true;
    }
    return false;
}

void renderSshBoxMenu(CliContext& ctx) {
    const bool sdUp = ctx.isSdMounted ? ctx.isSdMounted() : (tab5_sd_mount() == ESP_OK);
    ctx.appendLine("\x1B[1;36mSSH HOSTS\x1B[0m  (* = active)");
    ctx.appendLine("\x1B[36m----------------------------------------\x1B[0m");
    if (ctx.config.ssh.empty()) {
        ctx.appendLine("\x1B[33mNo hosts. Add one or sync from SD.\x1B[0m");
    } else {
        for (size_t i = 0; i < ctx.config.ssh.size(); ++i) {
            const auto& p = ctx.config.ssh[i];
            const bool active = i == ctx.config.keyboard.activeSsh;
            String target = p.user + "@" + p.host;
            if (p.port != 22) target += String(":") + static_cast<unsigned int>(p.port);
            String auth = "prompt";
            const char* color = "\x1B[33m";
            if (p.keyPath.length()) {
                const bool ok = sdUp && fileExists(p.keyPath);
                auth = ok ? "key ready" : "key missing";
                color = ok ? "\x1B[32m" : "\x1B[31m";
            } else if (p.password.length()) {
                auth = "password";
                color = "\x1B[35m";
            }
            ctx.appendLine(String(active ? "\x1B[1;32m* " : "\x1B[1;36m  ") +
                           static_cast<unsigned int>(i + 1) + ". " +
                           (p.name.length() ? p.name : p.host) + "\x1B[0m");
            ctx.appendLine(String("    \x1B[36m") + target + "\x1B[0m  " + color + auth + "\x1B[0m");
        }
    }
    ctx.appendLine("\x1B[36m----------------------------------------\x1B[0m");
    ctx.appendLine("\x1B[32m[number]\x1B[0m Connect  \x1B[33m[A]\x1B[0m Add  \x1B[36m[P]\x1B[0m Ping");
    ctx.appendLine("\x1B[31m[D #]\x1B[0m Delete     \x1B[35m[S]\x1B[0m Sync  \x1B[37m[R]\x1B[0m Refresh");
    ctx.appendLine("\x1B[90m[Q]\x1B[0m Exit");
}

void runSshPing(CliContext& ctx, const String& rawTarget) {
    String label = rawTarget;
    String host;
    uint16_t port = 22;
    size_t idx = 0;
    if (resolveOneBasedSshIndex(ctx, rawTarget, idx)) {
        const auto& p = ctx.config.ssh[idx];
        host = p.host;
        port = p.port ? p.port : 22;
        label = (p.name.length() ? p.name : p.host) + " (" + host + ":" + static_cast<unsigned int>(port) + ")";
    } else {
        bool matchedName = false;
        for (const auto& p : ctx.config.ssh) {
            if (p.name.equalsIgnoreCase(rawTarget)) {
                host = p.host;
                port = p.port ? p.port : 22;
                label = p.name + " (" + host + ":" + static_cast<unsigned int>(port) + ")";
                matchedName = true;
                break;
            }
        }
        if (!matchedName) {
            if (!parseHostPortTarget(rawTarget, host, port)) {
                ctx.appendLine("\x1B[1;31mInvalid ping target. Use host #, name, or IP[:port].\x1B[0m");
                return;
            }
            label = host + ":" + static_cast<unsigned int>(port);
        }
    }

    uint32_t rtt = 0;
    String info;
    bool ok = false;
    if (ctx.pingHost) {
        ok = ctx.pingHost(host, port, rtt, info);
    } else {
        ok = SshClient::probeHost(host, port, 3000, rtt, info);
    }

    ctx.appendLine(String("\x1B[1;36mPING\x1B[0m  ") + label);
    if (ok) {
        ctx.appendLine(String("\x1B[1;32mReachable\x1B[0m  RTT: ") +
                       static_cast<unsigned long>(rtt) + " ms");
    } else {
        ctx.appendLine("\x1B[1;31mUnreachable\x1B[0m");
    }
    ctx.appendLine(String("\x1B[36mInfo:\x1B[0m ") + (info.length() ? info : String("-")));

}

void handleSshSessionLine(CliContext& ctx, const String& rawLine) {
    auto* sess = ctx.sshSession;
    if (!sess || !sess->active) return;
    String trimmed = rawLine;
    trimmed.trim();

    if (sess->step == SshMenuStep::PingTarget) {
        sess->step = SshMenuStep::Menu;
        if (!trimmed.length() || trimmed.equalsIgnoreCase("q") || trimmed.equalsIgnoreCase("cancel")) {
            ctx.appendLine("\x1B[90mPing cancelled.\x1B[0m");
            return;
        }
        runSshPing(ctx, trimmed);
        return;
    }

    if (sess->step == SshMenuStep::AddHost) {
        if (!trimmed.length() || trimmed.equalsIgnoreCase("q") || trimmed.equalsIgnoreCase("cancel")) {
            sess->step = SshMenuStep::Menu;
            ctx.appendLine("\x1B[90mAdd host cancelled.\x1B[0m");
            return;
        }
        sess->draft = SshProfile{};
        int at = trimmed.indexOf('@');
        if (at > 0 && at + 1 < static_cast<int>(trimmed.length())) {
            sess->draft.user = trimmed.substring(0, static_cast<size_t>(at));
            trimmed = trimmed.substring(static_cast<size_t>(at + 1));
        }
        String host;
        uint16_t port = 22;
        if (!parseHostPortTarget(trimmed, host, port)) {
            ctx.appendLine("\x1B[1;31mInvalid host. Enter IP or hostname (or blank to cancel):\x1B[0m");
            return;
        }
        sess->draft.host = host;
        sess->draft.port = port;
        sess->step = SshMenuStep::AddName;
        return;
    }

    if (sess->step == SshMenuStep::AddName) {
        sess->draft.name = trimmed.length() ? trimmed : sess->draft.host;
        sess->step = SshMenuStep::AddUser;
        return;
    }

    if (sess->step == SshMenuStep::AddUser) {
        if (trimmed.length()) {
            sess->draft.user = trimmed;
        } else if (!sess->draft.user.length()) {
            sess->draft.user = "user";
        }
        sess->step = SshMenuStep::AddPort;
        return;
    }

    if (sess->step == SshMenuStep::AddPort) {
        if (trimmed.length()) {
            long p = trimmed.toInt();
            if (p >= 1 && p <= 65535) sess->draft.port = static_cast<uint16_t>(p);
        }
        sess->step = SshMenuStep::AddKeyPath;
        return;
    }

    if (sess->step == SshMenuStep::AddKeyPath) {
        if (trimmed == "-" || trimmed.equalsIgnoreCase("none")) {
            sess->draft.keyPath.clear();
        } else if (trimmed.length()) {
            sess->draft.keyPath = settings_store::resolveSdKeyPath(trimmed);
        }
        sess->step = SshMenuStep::AddPassword;
        return;
    }

    if (sess->step == SshMenuStep::AddPassword) {
        sess->draft.password = (trimmed == "-") ? String("") : trimmed;
        int idx = settings_store::upsertSshProfile(ctx.config, sess->draft, true);
        ctx.saveConfig();
        sess->step = SshMenuStep::Menu;
        ctx.appendLine(String("\x1B[1;32mSaved host #") + static_cast<unsigned int>(idx + 1) +
                       ": "+ sess->draft.name + "\x1B[0m");
        ctx.appendLine(String("  ") + sess->draft.user + "@" + sess->draft.host + ":" +
                       static_cast<unsigned int>(sess->draft.port));
        renderSshBoxMenu(ctx);
        return;
    }

    // Main interactive Menu step
    if (!trimmed.length() || trimmed.equalsIgnoreCase("r") || trimmed.equalsIgnoreCase("l") ||
        trimmed.equalsIgnoreCase("ls") || trimmed.equalsIgnoreCase("list")) {
        renderSshBoxMenu(ctx);
        return;
    }

    auto toks = splitCommandTokens(trimmed);
    if (toks.empty()) return;
    String op = toks[0];
    op.toLowerCase();

    if (op == "q" || op == "quit" || op == "exit" || op == "x") {
        sess->active = false;
        sess->step = SshMenuStep::Inactive;
        ctx.appendLine("\x1B[36mExited SSH session manager.\x1B[0m");
        return;
    }
    if (op == "a" || op == "add") {
        sess->draft = SshProfile{};
        sess->step = SshMenuStep::AddHost;
        return;
    }
    if (op == "s" || op == "sync") {
        ctx.syncSdProfiles(false);
        renderSshBoxMenu(ctx);
        return;
    }
    if (op == "p" || op == "ping") {
        if (toks.size() >= 2) {
            runSshPing(ctx, toks[1]);
        } else {
            sess->step = SshMenuStep::PingTarget;
        }
        return;
    }
    if (op == "d" || op == "del" || op == "rm" || op == "delete") {
        if (toks.size() < 2) {
            ctx.appendLine("\x1B[33mUsage: d <#|name|host>\x1B[0m");
            return;
        }
        size_t idx = 0;
        bool removed = false;
        if (resolveOneBasedSshIndex(ctx, toks[1], idx)) {
            removed = settings_store::removeSshProfile(ctx.config, String(static_cast<unsigned int>(idx)));
        } else {
            removed = settings_store::removeSshProfile(ctx.config, toks[1]);
        }
        if (removed) {
            ctx.saveConfig();
            ctx.appendLine(String("\x1B[1;32mRemoved host:\x1B[0m ") + toks[1]);
            renderSshBoxMenu(ctx);
        } else {
            ctx.appendLine(String("\x1B[1;31mHost not found:\x1B[0m ") + toks[1]);
        }
        return;
    }

    String targetTok = (op == "c" || op == "connect") && toks.size() >= 2 ? toks[1] : toks[0];
    size_t selIdx = 0;
    bool matched = resolveOneBasedSshIndex(ctx, targetTok, selIdx);
    if (!matched) {
        for (size_t i = 0; i < ctx.config.ssh.size(); ++i) {
            if (ctx.config.ssh[i].name.equalsIgnoreCase(targetTok) ||
                ctx.config.ssh[i].host.equalsIgnoreCase(targetTok)) {
                selIdx = i;
                matched = true;
                break;
            }
        }
    }
    if (matched) {
        ctx.config.keyboard.activeSsh = static_cast<uint8_t>(selIdx);
        ctx.saveConfig();
        sess->active = false;
        sess->step = SshMenuStep::Inactive;
        ctx.connectSsh();
        return;
    }
    if (targetTok.indexOf('@') > 0) {
        SshProfile direct;
        if (parseUserHostArgs(toks, (op == "c" || op == "connect") ? 1 : 0, direct)) {
            settings_store::upsertSshProfile(ctx.config, direct, true);
            ctx.saveConfig();
            sess->active = false;
            sess->step = SshMenuStep::Inactive;
            ctx.connectSsh();
            return;
        }
    }
    ctx.appendLine("\x1B[33mEnter [1-N] to connect, [A] add, [P] ping, [D #] delete, [S] sync, [Q] quit.\x1B[0m");
}

}  // namespace

String sshSessionPlainPrompt(const SshCliSession& session) {
    switch (session.step) {
        case SshMenuStep::AddHost: return "ssh/add host|ip> ";
        case SshMenuStep::AddName: return "ssh/add name> ";
        case SshMenuStep::AddUser: return "ssh/add user> ";
        case SshMenuStep::AddPort: return "ssh/add port [22]> ";
        case SshMenuStep::AddKeyPath: return "ssh/add key ('-'=none)> ";
        case SshMenuStep::AddPassword: return "ssh/add password> ";
        case SshMenuStep::PingTarget: return "ssh/ping target> ";
        case SshMenuStep::Menu:
        case SshMenuStep::Inactive:
        default: return "ssh> ";
    }
}

String sshSessionColoredPrompt(const SshCliSession& session) {
    switch (session.step) {
        case SshMenuStep::AddHost: return "\x1B[1;33mssh/add\x1B[0m host/IP (blank=cancel)> ";
        case SshMenuStep::AddName: {
            String def = session.draft.host.length() ? session.draft.host : String("host");
            return String("\x1B[1;33mssh/add\x1B[0m name [") + def + "]> ";
        }
        case SshMenuStep::AddUser: {
            String def = session.draft.user.length() ? session.draft.user : String("user");
            return String("\x1B[1;33mssh/add\x1B[0m user [") + def + "]> ";
        }
        case SshMenuStep::AddPort:
            return String("\x1B[1;33mssh/add\x1B[0m port [") + static_cast<unsigned int>(session.draft.port) + "]> ";
        case SshMenuStep::AddKeyPath:
            return "\x1B[1;33mssh/add\x1B[0m key path (e.g. /private/id_ecdsa_tab5, '-'=none)> ";
        case SshMenuStep::AddPassword:
            return "\x1B[1;33mssh/add\x1B[0m password (optional, blank=none)> ";
        case SshMenuStep::PingTarget:
            return "\x1B[1;36mssh/ping\x1B[0m host # or IP[:port]> ";
        case SshMenuStep::Menu:
        case SshMenuStep::Inactive:
        default:
            return "\x1B[1;36mssh\x1B[0m \x1B[90m[1-N|a|p|d|s|q]\x1B[0m> ";
    }
}

void executeLocalCliCommand(CliContext& ctx, const String& rawLine) {
    if (ctx.sshSession && ctx.sshSession->active) {
        handleSshSessionLine(ctx, rawLine);
        return;
    }

    String line = rawLine;
    line.trim();
    if (!line.length()) return;

    if (ctx.pythonReplMode) {
        if (line == "exit" || line == "quit" || line == "exit()" || line == "quit()") {
            ctx.python.resetRepl();
            ctx.pythonReplMode = false;
            ctx.appendLine("Exited MicroPython REPL.");
            return;
        }
        bool more = false;
        ctx.python.execInteractiveLine(rawLine, more);
        return;
    }

    auto tokens = splitCommandTokens(line);
    if (tokens.empty()) return;
    String cmd = tokens[0];
    cmd.toLowerCase();

    if (cmd == "clear" || cmd == "cls") {
        ctx.clearTerminal();
        return;
    }
    if (cmd == "help" || cmd == "?") {
        printMan(ctx, "help");
        return;
    }
    if (cmd == "man") {
        printMan(ctx, tokens.size() > 1 ? tokens[1] : String("help"));
        return;
    }
    if (cmd == "console" || cmd == "exit" || cmd == "quit") {
        ctx.returnToConsole();
        return;
    }
    if (cmd == "status") {
        ctx.wifi.poll();
        String wifiLine = String("\x1B[1;36mWi-Fi:\x1B[0m ") +
                          (ctx.wifi.hasIp()
                               ? (String("\x1B[1;32m") + ctx.wifi.ssid() + " " + ctx.wifi.ipAddress() + "\x1B[0m")
                               : (String("\x1B[33m") + ctx.wifi.statusMessage() + "\x1B[0m"));
        ctx.appendLine(wifiLine);
        uint64_t total = 0, freeB = 0;
        if (tab5_sd_mount() == ESP_OK && tab5_sd_usage(&total, &freeB)) {
            ctx.appendLine(String("\x1B[1;36mSD:\x1B[0m    \x1B[1;32mmounted\x1B[0m, ") + humanSize(freeB) +
                           " free / " + humanSize(total));
        } else {
            ctx.appendLine("\x1B[1;36mSD:\x1B[0m    \x1B[33mnot mounted\x1B[0m");
        }
        ctx.appendLine(String("\x1B[1;36mSSH:\x1B[0m   ") + static_cast<unsigned int>(ctx.config.ssh.size()) +
                       " profile(s) | active=" + static_cast<unsigned int>(ctx.config.keyboard.activeSsh) +
                       " | " + (ctx.ssh.connected() ? "\x1B[1;32mCONNECTED\x1B[0m" : "\x1B[90mDISCONNECTED\x1B[0m"));
        ctx.appendLine(String("\x1B[1;36mDisp:\x1B[0m  rot=") +
                       static_cast<unsigned int>(ctx.config.keyboard.displayRotation * 90) +
                       "deg | theme=" + (ctx.config.keyboard.darkTheme ? "console-dark" : "classic"));
        return;
    }
    if (cmd == "sd-sync") {
        bool replace = (tokens.size() >= 2 && (tokens[1] == "--replace" || tokens[1] == "-f"));
        ctx.syncSdProfiles(replace);
        return;
    }
    if (cmd == "rot" || cmd == "rotate") {
        if (tokens.size() < 2) {
            ctx.appendLine(String("Current rotation: ") + static_cast<unsigned int>(ctx.config.keyboard.displayRotation * 90) + " deg");
            return;
        }
        long deg = tokens[1].toInt();
        uint8_t rot = ctx.config.keyboard.displayRotation;
        if (deg == 0 || deg == 1 || deg == 2 || deg == 3) rot = static_cast<uint8_t>(deg);
        else if (deg == 90) rot = 1;
        else if (deg == 180) rot = 2;
        else if (deg == 270) rot = 3;
        ctx.setRotation(rot);
        ctx.appendLine(String("Rotation set to ") + static_cast<unsigned int>(rot * 90) + " deg");
        return;
    }
    if (cmd == "theme") {
        if (tokens.size() >= 2) {
            String th = tokens[1];
            th.toLowerCase();
            ctx.setDarkTheme(th != "classic");
        }
        ctx.appendLine(String("Theme: ") + (ctx.config.keyboard.darkTheme ? "console-dark" : "classic"));
        return;
    }
    if (cmd == "wifi") {
        String sub = tokens.size() > 1 ? tokens[1] : String("status");
        sub.toLowerCase();
        if (sub == "scan") {
            ctx.appendLine(ctx.wifi.scan() ? "Wi-Fi scan started (check WIFI tab)." : ctx.wifi.statusMessage());
        } else if (sub == "disconnect") {
            ctx.wifi.disconnect();
            ctx.appendLine("Wi-Fi disconnected.");
        } else if (sub == "retry") {
            ctx.wifi.retry();
            ctx.appendLine("Wi-Fi reconnect triggered.");
        } else if (sub == "connect" && tokens.size() >= 3) {
            String pass = tokens.size() >= 4 ? tokens[3] : String("");
            ctx.appendLine(ctx.wifi.connect(tokens[2], pass) ? (String("Joining ") + tokens[2] + "...") : ctx.wifi.statusMessage());
        } else {
            ctx.wifi.poll();
            ctx.appendLine(String("Wi-Fi: ") + ctx.wifi.ssid() + " | " + ctx.wifi.ipAddress() + " | " + ctx.wifi.statusMessage());
        }
        return;
    }
    if (cmd == "connect") {
        ctx.connectSsh();
        return;
    }
    if (cmd == "disconnect" || cmd == "disc") {
        ctx.disconnectSsh();
        return;
    }
    if (cmd == "ssh") {
        if (tokens.size() >= 2) {
            String arg = tokens[1];
            arg.toLowerCase();
            if (arg == "sync") {
                bool replace = (tokens.size() >= 3 && (tokens[2] == "--replace" || tokens[2] == "-f"));
                ctx.syncSdProfiles(replace);
                return;
            }
            if (arg == "clear" || arg == "purge") {
                settings_store::clearSshProfiles(ctx.config);
                ctx.saveConfig();
                ctx.appendLine("\x1B[1;33mCleared all SSH profiles from NVS.\x1B[0m Run 'ssh sync' or 'ssh <user>@<host>' to add.");
                return;
            }
            if (arg == "rm" || arg == "del" || arg == "remove") {
                if (tokens.size() < 3) {
                    ctx.appendLine("\x1B[33musage:\x1B[0m ssh rm <index|name|host|user@host>");
                    return;
                }
                if (settings_store::removeSshProfile(ctx.config, tokens[2])) {
                    ctx.saveConfig();
                    ctx.appendLine(String("\x1B[1;32mRemoved SSH profile:\x1B[0m ") + tokens[2]);
                } else {
                    ctx.appendLine(String("\x1B[1;31mNo matching SSH profile:\x1B[0m ") + tokens[2]);
                }
                return;
            }
            if (arg == "add") {
                if (tokens.size() < 4) {
                    ctx.appendLine("\x1B[33musage:\x1B[0m ssh add <name> <user>@<host> [-p port] [-i key]");
                    return;
                }
                SshProfile p;
                p.name = tokens[2];
                if (!parseUserHostArgs(tokens, 3, p)) {
                    ctx.appendLine("\x1B[1;31mInvalid target; expected <user>@<host>\x1B[0m");
                    return;
                }
                int idx = settings_store::upsertSshProfile(ctx.config, p, true);
                ctx.saveConfig();
                ctx.appendLine(String("\x1B[1;32mSaved SSH profile [") + idx + "]:\x1B[0m " +
                               p.name + " -> " + p.user + "@" + p.host + ":" + static_cast<unsigned int>(p.port));
                return;
            }
            if (arg == "tofu") {
                String sub = tokens.size() >= 3 ? tokens[2] : String("list");
                sub.toLowerCase();
                std::vector<tab5::tofu::Entry> entries;
                tab5::tofu::list_entries(entries);
                if (sub == "clear" || sub == "rm") {
                    String hostFilter = tokens.size() >= 4 ? tokens[3] : String("");
                    int removed = 0;
                    for (const auto& e : entries) {
                        if (!hostFilter.length() || strcmp(e.host, hostFilter.c_str()) == 0 ||
                            strcmp(e.key, hostFilter.c_str()) == 0) {
                            if (tab5::tofu::remove_by_key(e.key)) ++removed;
                        }
                    }
                    ctx.appendLine(String("\x1B[1;32mCleared ") + removed + " TOFU host key(s).\x1B[0m");
                    return;
                }
                if (entries.empty()) {
                    ctx.appendLine("\x1B[90mNo cached TOFU host keys in NVS.\x1B[0m");
                    return;
                }
                for (const auto& e : entries) {
                    char fpHex[17] = {0};
                    for (int b = 0; b < 8; ++b) snprintf(fpHex + b * 2, 3, "%02x", e.fp[b]);
                    ctx.appendLine(String("\x1B[36m") + (e.host[0] ? e.host : "<legacy>") + "\x1B[0m:" +
                                   static_cast<unsigned int>(e.port) + "  \x1B[90mkey=" + e.key +
                                   " sha256:" + fpHex + "...\x1B[0m");
                }
                return;
            }
            if (arg == "disconnect" || arg == "disc") {
                ctx.disconnectSsh();
                return;
            }
            if (arg == "ping" || arg == "probe") {
                if (tokens.size() < 3) {
                    ctx.appendLine("\x1B[33musage:\x1B[0m ssh ping <#|name|host[:port]>");
                    return;
                }
                runSshPing(ctx, tokens[2]);
                return;
            }
            if (tokens[1].indexOf('@') > 0) {
                SshProfile direct;
                if (!parseUserHostArgs(tokens, 1, direct)) {
                    ctx.appendLine("\x1B[1;31mInvalid SSH target. Use <user>@<host> [-p port] [-i key]\x1B[0m");
                    return;
                }
                settings_store::upsertSshProfile(ctx.config, direct, true);
                ctx.saveConfig();
                ctx.connectSsh();
                return;
            }
            bool matched = false;
            size_t selIdx = 0;
            if (resolveOneBasedSshIndex(ctx, arg, selIdx)) {
                ctx.config.keyboard.activeSsh = static_cast<uint8_t>(selIdx);
                matched = true;
            } else {
                for (size_t i = 0; i < ctx.config.ssh.size(); ++i) {
                    String n = ctx.config.ssh[i].name;
                    String h = ctx.config.ssh[i].host;
                    n.toLowerCase();
                    h.toLowerCase();
                    if (n == arg || h == arg) {
                        ctx.config.keyboard.activeSsh = static_cast<uint8_t>(i);
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) {
                ctx.appendLine(String("\x1B[1;31mUnknown SSH profile:\x1B[0m ") + tokens[1] +
                               " (use 'ssh' for interactive menu or 'ssh <user>@<host>')");
                return;
            }
            ctx.saveConfig();
            ctx.connectSsh();
            return;
        }
        if (ctx.sshSession) {
            ctx.sshSession->active = true;
            ctx.sshSession->step = SshMenuStep::Menu;
        }
        renderSshBoxMenu(ctx);
        return;
    }
    if (cmd == "scp") {
        if (tokens.size() < 3) {
            ctx.appendLine("usage: scp <:remote sd:/local | sd:/local :remote>");
            return;
        }
        if (ctx.config.ssh.empty() || ctx.config.keyboard.activeSsh >= ctx.config.ssh.size()) {
            ctx.appendLine("scp: no active SSH profile");
            return;
        }
        if (tab5_sd_mount() != ESP_OK) {
            ctx.appendLine("scp: SD mount failed");
            return;
        }
        const auto& prof = ctx.config.ssh[ctx.config.keyboard.activeSsh];
        String err;
        SshClient tempSsh;
        ctx.appendLine(String("scp: connecting to ") + prof.host + "...");
        if (!tempSsh.connect(prof, 80, 24, err)) {
            ctx.appendLine(String("scp failed: ") + err);
            return;
        }
        const String& src = tokens[1];
        const String& dst = tokens[2];
        bool ok = false;
        if (src.startsWith(":")) {
            String remote = src.substring(1);
            String local = sdVfsPath(dst, ctx.sdCwd);
            ok = tempSsh.scpDownloadFile(remote, local, err);
            ctx.appendLine(ok ? (String("Downloaded ") + remote + " -> " + local) : (String("scp failed: ") + err));
        } else if (dst.startsWith(":")) {
            String local = sdVfsPath(src, ctx.sdCwd);
            String remote = dst.substring(1);
            ok = tempSsh.scpUploadFile(local, remote, err);
            ctx.appendLine(ok ? (String("Uploaded ") + local + " -> " + remote) : (String("scp failed: ") + err));
        } else {
            ctx.appendLine("scp: prefix remote path with ':' (e.g. :/path/on/host)");
        }
        tempSsh.disconnect();
        return;
    }
    if (cmd == "python" || cmd == "py") {
        if (tokens.size() == 1 || tokens[1] == "-i") {
            if (!ctx.python.beginSession()) {
                ctx.appendLine("python: VM init failed");
                return;
            }
            ctx.pythonReplMode = true;
            ctx.appendLine("MicroPython REPL ready (type 'exit' to leave).");
            return;
        }
        if (tab5_sd_mount() != ESP_OK) {
            ctx.appendLine("python: SD mount failed");
            return;
        }
        String path = sdVfsPath(tokens[1], ctx.sdCwd);
        std::vector<String> argv;
        argv.push_back(tokens[1]);
        for (size_t i = 2; i < tokens.size(); ++i) argv.push_back(tokens[i]);
        String err;
        if (!ctx.python.runScriptFile(path, argv, err)) {
            ctx.appendLine(String("python error: ") + err);
        }
        return;
    }
    if (cmd == "image") {
        if (tokens.size() < 2) {
            ctx.appendLine("usage: image <path> [fit|center|half|quarter]");
            return;
        }
        if (tab5_sd_mount() != ESP_OK) {
            ctx.appendLine("image: SD mount failed");
            return;
        }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        String mode = tokens.size() >= 3 ? tokens[2] : String("fit");
        String err;
        if (!ctx.showImage(vfs, mode, err)) {
            ctx.appendLine(String("image: ") + err);
        }
        return;
    }

    // Filesystem commands on microSD (/sdcard)
    if (tab5_sd_mount() != ESP_OK) {
        ctx.appendLine(cmd + ": microSD card not available");
        return;
    }

    if (cmd == "pwd") {
        ctx.appendLine(String("sd:") + ctx.sdCwd);
    } else if (cmd == "cd") {
        String rel = normalizeSdRelPath(tokens.size() > 1 ? tokens[1] : String("/"), ctx.sdCwd);
        String vfs = sdVfsPath(rel, "/");
        struct stat st{};
        if (stat(vfs.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            ctx.appendLine(String("cd: not a directory: sd:") + rel);
        } else {
            ctx.sdCwd = rel;
        }
    } else if (cmd == "ls" || cmd == "dir") {
        String target = tokens.size() > 1 ? tokens[1] : ctx.sdCwd;
        String vfs = sdVfsPath(target, ctx.sdCwd);
        DIR* dir = opendir(vfs.c_str());
        if (!dir) {
            ctx.appendLine(String("ls: cannot open ") + vfs);
            return;
        }
        struct dirent* ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            String child = vfs + "/" + ent->d_name;
            struct stat st{};
            bool isDir = false;
            unsigned long sz = 0;
            if (stat(child.c_str(), &st) == 0) {
                isDir = S_ISDIR(st.st_mode);
                sz = static_cast<unsigned long>(st.st_size);
            } else {
                isDir = (ent->d_type == DT_DIR);
            }
            char lineBuf[360];
            const char* col = ansiColorForFilename(ent->d_name, isDir);
            snprintf(lineBuf, sizeof(lineBuf), "%s%-5s\x1B[0m \x1B[90m%9lu\x1B[0m  %s%s%s\x1B[0m",
                     isDir ? "\x1B[1;34m" : "\x1B[90m",
                     isDir ? "<DIR>" : "FILE",
                     isDir ? 0UL : sz,
                     col,
                     ent->d_name,
                     isDir ? "/" : "");
            ctx.appendLine(lineBuf);
        }
        closedir(dir);
    } else if (cmd == "df") {
        uint64_t total = 0, freeB = 0;
        if (tab5_sd_usage(&total, &freeB)) {
            ctx.appendLine(String("\x1B[1;36mmicroSD:\x1B[0m total \x1B[37m") + humanSize(total) +
                           "\x1B[0m, used \x1B[33m" + humanSize(total > freeB ? total - freeB : 0) +
                           "\x1B[0m, free \x1B[1;32m" + humanSize(freeB) + "\x1B[0m");
        } else {
            ctx.appendLine("\x1B[1;31mdf: unavailable\x1B[0m");
        }
    } else if (cmd == "cat" || cmd == "head" || cmd == "tail") {
        if (tokens.size() < 2) {
            ctx.appendLine(String("usage: ") + cmd + " <file>");
            return;
        }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        FILE* fp = fopen(vfs.c_str(), "rb");
        if (!fp) {
            ctx.appendLine(cmd + ": cannot open " + vfs);
            return;
        }
        std::vector<String> lines;
        String cur;
        char buf[512];
        size_t n = 0;
        size_t totalRead = 0;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0 && totalRead < 65536) {
            totalRead += n;
            for (size_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                } else if (buf[i] != '\r') {
                    cur += buf[i];
                }
            }
        }
        if (cur.length()) lines.push_back(cur);
        fclose(fp);
        size_t start = 0;
        size_t end = lines.size();
        if (cmd == "head" && end > 20) end = 20;
        if (cmd == "tail" && lines.size() > 20) start = lines.size() - 20;
        for (size_t i = start; i < end; ++i) ctx.appendLine(lines[i]);
    } else if (cmd == "stat") {
        if (tokens.size() < 2) { ctx.appendLine("usage: stat <path>"); return; }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        struct stat st{};
        if (stat(vfs.c_str(), &st) != 0) {
            ctx.appendLine(String("stat: not found ") + vfs);
        } else {
            ctx.appendLine(String(S_ISDIR(st.st_mode) ? "dir " : "file ") +
                           static_cast<unsigned long>(st.st_size) + " bytes  " + vfs);
        }
    } else if (cmd == "mkdir") {
        if (tokens.size() < 2) { ctx.appendLine("usage: mkdir <dir>"); return; }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        ctx.appendLine(mkdir(vfs.c_str(), 0755) == 0 ? String("mkdir: ok") : (String("mkdir failed: ") + vfs));
    } else if (cmd == "rmdir") {
        if (tokens.size() < 2) { ctx.appendLine("usage: rmdir <dir>"); return; }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        ctx.appendLine(rmdir(vfs.c_str()) == 0 ? String("rmdir: ok") : (String("rmdir failed: ") + vfs));
    } else if (cmd == "rm" || cmd == "del") {
        if (tokens.size() < 2) { ctx.appendLine("usage: rm <file>"); return; }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        ctx.appendLine(unlink(vfs.c_str()) == 0 ? String("rm: ok") : (String("rm failed: ") + vfs));
    } else if (cmd == "touch") {
        if (tokens.size() < 2) { ctx.appendLine("usage: touch <file>"); return; }
        String vfs = sdVfsPath(tokens[1], ctx.sdCwd);
        FILE* fp = fopen(vfs.c_str(), "ab");
        if (fp) { fclose(fp); }
        else { ctx.appendLine(String("touch failed: ") + vfs); }
    } else if (cmd == "cp") {
        if (tokens.size() < 3) { ctx.appendLine("usage: cp <src> <dst>"); return; }
        String err;
        bool ok = copyVfsFile(sdVfsPath(tokens[1], ctx.sdCwd), sdVfsPath(tokens[2], ctx.sdCwd), err);
        ctx.appendLine(ok ? String("cp: ok") : (String("cp failed: ") + err));
    } else if (cmd == "mv") {
        if (tokens.size() < 3) { ctx.appendLine("usage: mv <src> <dst>"); return; }
        String s = sdVfsPath(tokens[1], ctx.sdCwd);
        String d = sdVfsPath(tokens[2], ctx.sdCwd);
        ctx.appendLine(rename(s.c_str(), d.c_str()) == 0 ? String("mv: ok") : String("mv failed"));
    } else if (cmd == "chmod") {
        ctx.appendLine("chmod: FAT/exFAT microSD ignores POSIX mode bits.");
    } else {
        ctx.appendLine(String("\x1B[1;31mUnknown command:\x1B[0m ") + tokens[0] + " \x1B[90m(type 'help')\x1B[0m");
    }
}
