#include "storage_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "tab5_hal.h"

extern const uint8_t tab5_fuse_server_py_start[] asm("_binary_tab5_fuse_server_py_start");
extern const uint8_t tab5_fuse_server_py_end[] asm("_binary_tab5_fuse_server_py_end");

namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

String hexEncodeText(const char* text) {
    if (!text) return "";
    static constexpr char kHex[] = "0123456789abcdef";
    String out;
    size_t n = strlen(text);
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = static_cast<uint8_t>(text[i]);
        out += kHex[b >> 4];
        out += kHex[b & 0x0F];
    }
    return out;
}

void appendHexBytes(String& out, const uint8_t* data, size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.reserve(out.length() + len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
    }
}

bool decodeHexChunk(const String& hex, size_t& index, uint8_t* buffer, size_t capacity, size_t& length) {
    length = 0;
    while (index + 1 < hex.length() && length < capacity) {
        int hi = hexVal(hex[index]);
        int lo = hexVal(hex[index + 1]);
        if (hi < 0 || lo < 0) return false;
        buffer[length++] = static_cast<uint8_t>((hi << 4) | lo);
        index += 2;
    }
    return true;
}

String hexDecodeText(const String& hex) {
    String out;
    out.reserve(hex.length() / 2);
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        out += static_cast<char>((hi << 4) | lo);
    }
    return out;
}

String normalizeRelPath(String raw) {
    if (!raw.length() || raw[0] != '/') raw = String("/") + raw;
    std::vector<String> parts;
    size_t i = 0;
    while (i < raw.length()) {
        while (i < raw.length() && raw[i] == '/') ++i;
        if (i >= raw.length()) break;
        size_t j = i;
        while (j < raw.length() && raw[j] != '/') ++j;
        String seg = raw.substring(i, j);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (seg.length() && seg != ".") {
            parts.push_back(seg);
        }
        i = j;
    }
    if (parts.empty()) return "/";
    String out;
    for (const auto& s : parts) {
        out += "/";
        out += s;
    }
    return out;
}

String toVfsSdPath(const String& normPath) {
    if (normPath == "/") return "/sdcard";
    return String("/sdcard") + normPath;
}

int errnoOr(int fallback) {
    return errno ? errno : fallback;
}

}  // namespace

void StorageBridge::reset() {
    running_ = false;
    lineBuf_.clear();
}

void StorageBridge::sendReply(SshClient& ssh, const String& id, const String& payload) {
    String line = String("RSP\t") + id + "\t" + payload + "\n";
    if (!ssh.writeBridge(reinterpret_cast<const uint8_t*>(line.c_str()), line.length())) {
        running_ = false;
        lineBuf_.clear();
    }
}

void StorageBridge::sendError(SshClient& ssh, const String& id, int code) {
    sendReply(ssh, id, String("ERR\t") + code);
}

void StorageBridge::handleRequest(SshClient& ssh, const String& line) {
    std::vector<String> parts;
    int start = 0;
    for (;;) {
        int tab = line.indexOf('\t', static_cast<size_t>(start));
        if (tab < 0) {
            parts.push_back(line.substring(static_cast<size_t>(start)));
            break;
        }
        parts.push_back(line.substring(static_cast<size_t>(start), static_cast<size_t>(tab)));
        start = tab + 1;
    }
    if (parts.size() < 5 || parts[0] != "REQ") return;

    const String& id = parts[1];
    const String& op = parts[2];
    String volume = parts[3];
    volume.toLowerCase();
    String normPath = normalizeRelPath(hexDecodeText(parts[4]));

    // USB MSC storage is omitted in v0.1.0; keep ~/usb safely present as an empty read-only root.
    if (volume == "usb") {
        if (normPath == "/" && op == "stat") {
            sendReply(ssh, id, "OK\tSTAT\tdir\t0\t0555\t0");
        } else if (normPath == "/" && op == "list") {
            sendReply(ssh, id, "OK\tLIST\t");
        } else {
            sendError(ssh, id, EIO);
        }
        return;
    }

    if (volume != "sd" && volume != "microsd") {
        sendError(ssh, id, EINVAL);
        return;
    }

    if (tab5_sd_mount() != ESP_OK) {
        sendError(ssh, id, EIO);
        return;
    }

    String full = toVfsSdPath(normPath);

    if (op == "stat") {
        struct stat st{};
        if (stat(full.c_str(), &st) != 0) {
            sendError(ssh, id, errnoOr(ENOENT));
            return;
        }
        bool isDir = S_ISDIR(st.st_mode);
        unsigned long sz = isDir ? 0UL : static_cast<unsigned long>(st.st_size);
        unsigned long mtime = static_cast<unsigned long>(st.st_mtime);
        sendReply(ssh, id, String("OK\tSTAT\t") + (isDir ? "dir" : "file") + "\t" +
                           String(sz) + "\t" + (isDir ? "0755" : "0644") + "\t" + String(mtime));
    } else if (op == "list") {
        DIR* dir = opendir(full.c_str());
        if (!dir) {
            sendError(ssh, id, errnoOr(ENOENT));
            return;
        }
        String payload = "OK\tLIST\t";
        bool first = true;
        struct dirent* ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            String child = full + "/" + ent->d_name;
            struct stat st{};
            bool isDir = false;
            unsigned long sz = 0;
            unsigned long mtime = 0;
            if (stat(child.c_str(), &st) == 0) {
                isDir = S_ISDIR(st.st_mode);
                sz = isDir ? 0UL : static_cast<unsigned long>(st.st_size);
                mtime = static_cast<unsigned long>(st.st_mtime);
            } else {
                isDir = (ent->d_type == DT_DIR);
            }
            if (!first) payload += "|";
            first = false;
            payload += hexEncodeText(ent->d_name);
            payload += ",";
            payload += isDir ? "dir" : "file";
            payload += ",";
            payload += String(sz);
            payload += ",";
            payload += isDir ? "0755" : "0644";
            payload += ",";
            payload += String(mtime);
        }
        closedir(dir);
        sendReply(ssh, id, payload);
    } else if (op == "read") {
        if (parts.size() < 7) {
            sendError(ssh, id, EINVAL);
            return;
        }
        uint32_t offset = static_cast<uint32_t>(strtoul(parts[5].c_str(), nullptr, 10));
        uint32_t requested = std::min<uint32_t>(static_cast<uint32_t>(strtoul(parts[6].c_str(), nullptr, 10)), 4096);
        FILE* fp = fopen(full.c_str(), "rb");
        if (!fp) {
            sendError(ssh, id, errnoOr(ENOENT));
            return;
        }
        fseek(fp, static_cast<long>(offset), SEEK_SET);
        String data;
        data.reserve(requested * 2);
        uint8_t buf[512];
        uint32_t remaining = requested;
        while (remaining > 0) {
            size_t chunk = std::min<size_t>(remaining, sizeof(buf));
            size_t n = fread(buf, 1, chunk, fp);
            if (n == 0) break;
            appendHexBytes(data, buf, n);
            remaining -= static_cast<uint32_t>(n);
        }
        fclose(fp);
        sendReply(ssh, id, String("OK\tDATA\t") + data);
    } else if (op == "write") {
        if (parts.size() < 7) {
            sendError(ssh, id, EINVAL);
            return;
        }
        uint32_t offset = static_cast<uint32_t>(strtoul(parts[5].c_str(), nullptr, 10));
        FILE* fp = fopen(full.c_str(), offset == 0 ? "wb" : "r+b");
        if (!fp && offset > 0) fp = fopen(full.c_str(), "w+b");
        if (!fp) {
            sendError(ssh, id, errnoOr(EIO));
            return;
        }
        if (offset > 0) fseek(fp, static_cast<long>(offset), SEEK_SET);
        const String& hex = parts[6];
        uint8_t buf[512];
        size_t idx = 0;
        while (idx < hex.length()) {
            size_t len = 0;
            if (!decodeHexChunk(hex, idx, buf, sizeof(buf), len)) {
                fclose(fp);
                sendError(ssh, id, EINVAL);
                return;
            }
            if (len && fwrite(buf, 1, len, fp) != len) {
                fclose(fp);
                sendError(ssh, id, errnoOr(EIO));
                return;
            }
        }
        fclose(fp);
        sendReply(ssh, id, "OK\tDONE");
    } else if (op == "truncate") {
        if (parts.size() < 6) {
            sendError(ssh, id, EINVAL);
            return;
        }
        long size = strtol(parts[5].c_str(), nullptr, 10);
        if (truncate(full.c_str(), static_cast<off_t>(std::max(0L, size))) == 0) {
            sendReply(ssh, id, "OK\tDONE");
        } else {
            sendError(ssh, id, errnoOr(EIO));
        }
    } else if (op == "mkdir") {
        mkdir(full.c_str(), 0755) == 0 ? sendReply(ssh, id, "OK\tDONE") : sendError(ssh, id, errnoOr(EIO));
    } else if (op == "rmdir") {
        rmdir(full.c_str()) == 0 ? sendReply(ssh, id, "OK\tDONE") : sendError(ssh, id, errnoOr(EIO));
    } else if (op == "unlink") {
        unlink(full.c_str()) == 0 ? sendReply(ssh, id, "OK\tDONE") : sendError(ssh, id, errnoOr(ENOENT));
    } else if (op == "rename") {
        if (parts.size() < 6) {
            sendError(ssh, id, EINVAL);
            return;
        }
        String dest = toVfsSdPath(normalizeRelPath(hexDecodeText(parts[5])));
        rename(full.c_str(), dest.c_str()) == 0 ? sendReply(ssh, id, "OK\tDONE") : sendError(ssh, id, errnoOr(EIO));
    } else {
        sendError(ssh, id, ENOSYS);
    }
}

bool StorageBridge::start(SshClient& ssh, const String& remoteHome, const LogFn& log) {
    if (running_ || !ssh.connected()) return running_;
    remoteHome_ = remoteHome;
    String output;
    String error;
    if (log) log("Tab5 storage: preparing ~/sd bridge");

    const char* setupScript = R"SH(mkdir -p "$HOME/.tab5/bin" "$HOME/.tab5/mnt" &&
cat > "$HOME/.tab5/bin/tab5-server-setup.sh" <<'TAB5_SETUP_EOF'
#!/bin/sh
set -eu
if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo: sudo sh ~/.tab5/bin/tab5-server-setup.sh" >&2
  exit 1
fi
if command -v apt-get >/dev/null 2>&1; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y python3 python3-pip fuse3 python3-fusepy || \
  DEBIAN_FRONTEND=noninteractive apt-get install -y python3 python3-pip fuse3
elif command -v dnf >/dev/null 2>&1; then
  dnf install -y python3 python3-pip fuse3 fuse-libs
elif command -v apk >/dev/null 2>&1; then
  apk add --no-cache python3 py3-pip fuse3 fuse
fi
python3 -c 'import fuse' >/dev/null 2>&1 || python3 -m pip install --break-system-packages fusepy || python3 -m pip install fusepy
echo "Tab5 server setup complete."
TAB5_SETUP_EOF
chmod +x "$HOME/.tab5/bin/tab5-server-setup.sh")SH";

    ssh.execCommand(setupScript, output, error, 8000);

    const char* prepScript = R"SH(
pkill -9 -f '[t]ab5-fuse-launch.sh' >/dev/null 2>&1 || true
pkill -9 -f '[t]ab5-fuse-server.py' >/dev/null 2>&1 || true
if command -v fusermount3 >/dev/null 2>&1; then
  fusermount3 -uz "$HOME/.tab5/mnt" >/dev/null 2>&1 || true
elif command -v fusermount >/dev/null 2>&1; then
  fusermount -uz "$HOME/.tab5/mnt" >/dev/null 2>&1 || true
fi
mkdir -p "$HOME/.tab5/bin" "$HOME/.tab5/mnt" || exit 1
ln -sfn "$HOME/.tab5/mnt/sd" "$HOME/sd" || true
ln -sfn "$HOME/.tab5/mnt/usb" "$HOME/usb" || true
rm -f "$HOME/.tab5/rpc.in" "$HOME/.tab5/rpc.out"
)SH";

    if (!ssh.execCommand(prepScript, output, error, 12000)) {
        if (log) log(String("Tab5 storage prep failed: ") + error);
        return false;
    }

    const char* imageScript = R"SH(cat > "$HOME/.tab5/bin/tab5-image" <<'TAB5_IMAGE_EOF'
#!/bin/sh
if [ $# -lt 1 ]; then
  echo "usage: image <file> [fit|center|half|quarter]" >&2
  exit 2
fi
target=$1
mode=${2:-fit}
dir=$(dirname -- "$target") || exit 1
base=$(basename -- "$target") || exit 1
cd -P -- "$dir" 2>/dev/null || { echo "image: cannot access $target" >&2; exit 1; }
abs="$PWD/$base"
[ -f "$abs" ] || { echo "image: not a file: $target" >&2; exit 1; }
hex=$(python3 -c 'import sys; print(sys.argv[1].encode("utf-8").hex())' "$abs") || exit 1
printf '\033]777;tab5-image;pathhex=%s;mode=%s\a\n' "$hex" "$mode"
TAB5_IMAGE_EOF
chmod +x "$HOME/.tab5/bin/tab5-image" &&
mkdir -p "$HOME/.local/bin" &&
ln -sfn "$HOME/.tab5/bin/tab5-image" "$HOME/.tab5/bin/image" &&
ln -sfn "$HOME/.tab5/bin/tab5-image" "$HOME/.local/bin/tab5-image" &&
ln -sfn "$HOME/.tab5/bin/tab5-image" "$HOME/.local/bin/image")SH";

    ssh.execCommand(imageScript, output, error, 8000);

    size_t pyLen = static_cast<size_t>(tab5_fuse_server_py_end - tab5_fuse_server_py_start);
    while (pyLen > 0 && tab5_fuse_server_py_start[pyLen - 1] == '\0') {
        --pyLen;
    }

    auto uploadServerScript = [&]() -> bool {
        char checkCmd[192];
        snprintf(checkCmd, sizeof(checkCmd),
                 "[ -x \"$HOME/.tab5/bin/tab5-fuse-server.py\" ] && [ \"$(wc -c < \"$HOME/.tab5/bin/tab5-fuse-server.py\" | tr -d ' ')\" = \"%u\" ]",
                 static_cast<unsigned>(pyLen));
        if (ssh.execCommand(checkCmd, output, error, 4000)) {
            return true;
        }
        if (ssh.execCommand("rm -f \"$HOME/.tab5/bin/tab5-fuse-server.py.tmp\"", output, error, 4000)) {
            static constexpr char kHex[] = "0123456789abcdef";
            const size_t kChunkBytes = 1500;
            bool hexOk = true;
            for (size_t off = 0; off < pyLen; off += kChunkBytes) {
                size_t chunk = std::min(kChunkBytes, pyLen - off);
                String cmd;
                cmd.reserve(160 + chunk * 2);
                cmd = "python3 -c \"import pathlib,sys; (pathlib.Path.home()/'.tab5/bin/tab5-fuse-server.py.tmp').open('ab').write(bytes.fromhex(sys.argv[1]))\" ";
                for (size_t i = 0; i < chunk; ++i) {
                    uint8_t b = tab5_fuse_server_py_start[off + i];
                    cmd += kHex[b >> 4];
                    cmd += kHex[b & 0x0F];
                }
                if (!ssh.execCommand(cmd, output, error, 8000)) {
                    hexOk = false;
                    break;
                }
            }
            if (hexOk && ssh.execCommand("chmod 0755 \"$HOME/.tab5/bin/tab5-fuse-server.py.tmp\" && mv -f \"$HOME/.tab5/bin/tab5-fuse-server.py.tmp\" \"$HOME/.tab5/bin/tab5-fuse-server.py\"",
                                         output, error, 4000)) {
                return true;
            }
        }
        return ssh.scpUploadMemory(tab5_fuse_server_py_start, pyLen, ".tab5/bin/tab5-fuse-server.py", error, 0755);
    };

    if (!uploadServerScript()) {
        if (log) log(String("Tab5 storage upload failed: ") + error);
        return false;
    }

    if (!ssh.execCommand("python3 -c 'import fuse' >/dev/null 2>&1", output, error, 4000)) {
        if (log) log("Tab5 ~/sd skipped (host lacks fusepy/libfuse); SSH session active");
        return false;
    }

    const char* launcherScript = R"SH(cat > "$HOME/.tab5/bin/tab5-fuse-launch.sh" <<'TAB5_FUSE_LAUNCH_EOF'
#!/bin/sh
base="$HOME/.tab5"
rpc_in="$base/rpc.in"
rpc_out="$base/rpc.out"
rm -f "$rpc_in" "$rpc_out"
mkfifo "$rpc_in" "$rpc_out" || exit 1
cleanup() {
  [ -n "${py_pid:-}" ] && kill "$py_pid" >/dev/null 2>&1 || true
  [ -n "${out_pid:-}" ] && kill "$out_pid" >/dev/null 2>&1 || true
  rm -f "$rpc_in" "$rpc_out"
}
trap cleanup EXIT INT TERM
python3 "$base/bin/tab5-fuse-server.py" --volume all --mount "$base/mnt" --rpc-in "$rpc_in" --rpc-out "$rpc_out" &
py_pid=$!
cat "$rpc_out" &
out_pid=$!
cat > "$rpc_in"
TAB5_FUSE_LAUNCH_EOF
chmod +x "$HOME/.tab5/bin/tab5-fuse-launch.sh")SH";

    if (!ssh.execCommand(launcherScript, output, error, 8000)) {
        if (log) log(String("Tab5 storage launcher deploy failed: ") + error);
        return false;
    }

    if (!ssh.startBridge("sh \"$HOME/.tab5/bin/tab5-fuse-launch.sh\"", error)) {
        if (log) {
            log(String("Tab5 storage bridge failed: ") + error);
            log("Run on server if fusepy missing: sudo sh ~/.tab5/bin/tab5-server-setup.sh");
        }
        return false;
    }

    lineBuf_.clear();
    running_ = true;
    if (log) log("Tab5 storage mounted on host at ~/sd (FUSE)");
    return true;
}

void StorageBridge::poll(SshClient& ssh) {
    if (!running_ || !ssh.connected()) return;
    uint8_t buf[512];
    for (;;) {
        int n = ssh.readBridge(buf, sizeof(buf));
        if (n < 0) {
            running_ = false;
            lineBuf_.clear();
            return;
        }
        if (n == 0) break;
        for (int i = 0; i < n; ++i) {
            char c = static_cast<char>(buf[i]);
            if (c == '\n') {
                String req = lineBuf_;
                lineBuf_.clear();
                req.trim();
                if (req.length()) handleRequest(ssh, req);
            } else if (c != '\r' && lineBuf_.length() < 16384) {
                lineBuf_ += c;
            }
        }
    }
}
