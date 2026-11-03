#include "fs_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace tab5_fs {

namespace {

int hex_val(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

int ascii_casecmp(const char* a, const char* b) noexcept {
    while (*a && *b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return std::tolower(static_cast<unsigned char>(*a)) -
           std::tolower(static_cast<unsigned char>(*b));
}

bool ends_with_ci(const char* str, const char* suffix) noexcept {
    if (!str || !suffix) return false;
    const size_t slen = std::strlen(str);
    const size_t xlen = std::strlen(suffix);
    if (xlen > slen) return false;
    return ascii_casecmp(str + (slen - xlen), suffix) == 0;
}

}  // namespace

void format_passcode(uint32_t random_value, char out[9]) noexcept {
    if (!out) return;
    // Range 10000000 .. 99999999 so the first digit is never '0'
    const uint32_t code = 10000000U + (random_value % 90000000U);
    std::snprintf(out, 9, "%08u", static_cast<unsigned>(code));
}

void format_passcode_display(const char code[9], char out[12]) noexcept {
    if (!out) return;
    if (!code || std::strlen(code) < 8) {
        std::snprintf(out, 12, "---- ----");
        return;
    }
    std::snprintf(out, 12, "%.4s %.4s", code, code + 4);
}

bool normalize_passcode_input(const char* input, char out[9]) noexcept {
    if (!input || !out) return false;
    size_t digits = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];
        if (c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= '0' && c <= '9') {
            if (digits >= 8) return false;
            out[digits++] = c;
        } else {
            return false;
        }
    }
    out[digits] = '\0';
    return digits == 8;
}

bool verify_passcode(const char* candidate, const char active_code[9]) noexcept {
    if (!candidate || !active_code) return false;
    if (std::strlen(active_code) != 8) return false;
    char norm[9] = {0};
    if (!normalize_passcode_input(candidate, norm)) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < 8; ++i) {
        diff |= static_cast<uint8_t>(norm[i] ^ active_code[i]);
    }
    return diff == 0;
}

bool url_decode(const char* src, char* dst, size_t dst_cap) noexcept {
    if (!src || !dst || dst_cap == 0) return false;
    size_t w = 0;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        if (w + 1 >= dst_cap) return false;
        const char c = src[i];
        if (c == '%') {
            const int hi = hex_val(src[i + 1]);
            const int lo = (hi >= 0) ? hex_val(src[i + 2]) : -1;
            if (hi < 0 || lo < 0) return false;
            const uint8_t decoded = static_cast<uint8_t>((hi << 4) | lo);
            if (decoded == 0) return false;  // Reject embedded NUL
            dst[w++] = static_cast<char>(decoded);
            i += 2;
        } else if (c == '+') {
            dst[w++] = ' ';
        } else {
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
    return true;
}

bool get_query_param(const char* query, const char* key, char* out, size_t out_cap) noexcept {
    if (!query || !key || !key[0] || !out || out_cap == 0) return false;
    const size_t key_len = std::strlen(key);
    const char* p = query;
    if (*p == '?') ++p;

    while (*p) {
        const char* amp = std::strchr(p, '&');
        const size_t seg_len = amp ? static_cast<size_t>(amp - p) : std::strlen(p);
        const char* eq = static_cast<const char*>(std::memchr(p, '=', seg_len));
        if (eq) {
            const size_t klen = static_cast<size_t>(eq - p);
            if (klen == key_len && std::strncmp(p, key, key_len) == 0) {
                const size_t vlen = seg_len - klen - 1;
                char raw[384];
                if (vlen >= sizeof(raw)) return false;
                std::memcpy(raw, eq + 1, vlen);
                raw[vlen] = '\0';
                return url_decode(raw, out, out_cap);
            }
        }
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

bool extract_cookie_value(const char* cookie_header, const char* cookie_name,
                          char* out, size_t out_cap) noexcept {
    if (!cookie_header || !cookie_name || !cookie_name[0] || !out || out_cap == 0) return false;
    const size_t name_len = std::strlen(cookie_name);
    const char* p = cookie_header;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        if (!*p) break;
        const char* semi = std::strchr(p, ';');
        const size_t tok_len = semi ? static_cast<size_t>(semi - p) : std::strlen(p);
        const char* eq = static_cast<const char*>(std::memchr(p, '=', tok_len));
        if (eq) {
            size_t klen = static_cast<size_t>(eq - p);
            while (klen > 0 && (p[klen - 1] == ' ' || p[klen - 1] == '\t')) --klen;
            if (klen == name_len && std::strncmp(p, cookie_name, name_len) == 0) {
                const char* val_start = eq + 1;
                size_t vlen = tok_len - static_cast<size_t>(val_start - p);
                while (vlen > 0 && (*val_start == ' ' || *val_start == '"')) {
                    ++val_start;
                    --vlen;
                }
                while (vlen > 0 && (val_start[vlen - 1] == ' ' || val_start[vlen - 1] == '"')) {
                    --vlen;
                }
                if (vlen + 1 > out_cap) return false;
                std::memcpy(out, val_start, vlen);
                out[vlen] = '\0';
                return true;
            }
        }
        if (!semi) break;
        p = semi + 1;
    }
    return false;
}

bool resolve_sd_path(const char* root_dir, const char* req_rel_path,
                     char* out_vfs_path, size_t out_vfs_cap,
                     char* out_clean_rel, size_t out_rel_cap) noexcept {
    if (!root_dir || !root_dir[0] || !out_vfs_path || out_vfs_cap < 4) return false;

    const char* rel = req_rel_path ? req_rel_path : "/";
    for (size_t i = 0; rel[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(rel[i]);
        if (c < 32 || c == '\\') return false;
    }

    char clean[256];
    clean[0] = '/';
    clean[1] = '\0';
    size_t clean_len = 1;

    const char* p = rel;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        const char* slash = std::strchr(p, '/');
        const size_t seg_len = slash ? static_cast<size_t>(slash - p) : std::strlen(p);
        if (seg_len == 0) break;
        if (seg_len == 1 && p[0] == '.') {
            // Skip "."
        } else if (seg_len == 2 && p[0] == '.' && p[1] == '.') {
            // Reject any parent traversal
            return false;
        } else {
            if (clean_len > 1) {
                if (clean_len + 1 >= sizeof(clean)) return false;
                clean[clean_len++] = '/';
            }
            if (clean_len + seg_len >= sizeof(clean)) return false;
            std::memcpy(clean + clean_len, p, seg_len);
            clean_len += seg_len;
            clean[clean_len] = '\0';
        }
        if (!slash) break;
        p = slash + 1;
    }

    if (out_clean_rel && out_rel_cap > 0) {
        if (clean_len + 1 > out_rel_cap) return false;
        std::memcpy(out_clean_rel, clean, clean_len + 1);
    }

    size_t root_len = std::strlen(root_dir);
    while (root_len > 1 && root_dir[root_len - 1] == '/') --root_len;

    if (clean_len == 1 && clean[0] == '/') {
        if (root_len + 1 > out_vfs_cap) return false;
        std::memcpy(out_vfs_path, root_dir, root_len);
        out_vfs_path[root_len] = '\0';
    } else {
        if (root_len + clean_len + 1 > out_vfs_cap) return false;
        std::memcpy(out_vfs_path, root_dir, root_len);
        std::memcpy(out_vfs_path + root_len, clean, clean_len + 1);
    }
    return true;
}

bool validate_filename(const char* name) noexcept {
    if (!name || !name[0]) return false;
    const size_t len = std::strlen(name);
    if (len > 120) return false;
    if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

bool join_sd_child_path(const char* root_dir, const char* dir_rel_path, const char* child_name,
                        char* out_vfs_path, size_t out_vfs_cap) noexcept {
    if (!validate_filename(child_name)) return false;
    char dir_vfs[256];
    char clean_rel[256];
    if (!resolve_sd_path(root_dir, dir_rel_path, dir_vfs, sizeof(dir_vfs), clean_rel, sizeof(clean_rel))) {
        return false;
    }
    const int n = std::snprintf(out_vfs_path, out_vfs_cap, "%s/%s", dir_vfs, child_name);
    return n > 0 && static_cast<size_t>(n) < out_vfs_cap;
}

bool is_html_file(const char* filename) noexcept {
    return ends_with_ci(filename, ".html") || ends_with_ci(filename, ".htm");
}

DeliveryMode classify_file_delivery(const char* filename) noexcept {
    return is_html_file(filename) ? DeliveryMode::stream_html : DeliveryMode::download_attachment;
}

const char* mime_type_for_filename(const char* filename) noexcept {
    if (!filename) return "application/octet-stream";
    if (is_html_file(filename)) return "text/html; charset=utf-8";
    if (ends_with_ci(filename, ".txt") || ends_with_ci(filename, ".log")) return "text/plain; charset=utf-8";
    if (ends_with_ci(filename, ".md")) return "text/markdown; charset=utf-8";
    if (ends_with_ci(filename, ".json")) return "application/json; charset=utf-8";
    if (ends_with_ci(filename, ".csv")) return "text/csv; charset=utf-8";
    if (ends_with_ci(filename, ".css")) return "text/css; charset=utf-8";
    if (ends_with_ci(filename, ".js")) return "application/javascript; charset=utf-8";
    if (ends_with_ci(filename, ".png")) return "image/png";
    if (ends_with_ci(filename, ".jpg") || ends_with_ci(filename, ".jpeg")) return "image/jpeg";
    if (ends_with_ci(filename, ".gif")) return "image/gif";
    if (ends_with_ci(filename, ".svg")) return "image/svg+xml";
    if (ends_with_ci(filename, ".ico")) return "image/x-icon";
    if (ends_with_ci(filename, ".pdf")) return "application/pdf";
    if (ends_with_ci(filename, ".zip")) return "application/zip";
    if (ends_with_ci(filename, ".xml")) return "application/xml";
    return "application/octet-stream";
}

bool list_directory(const char* vfs_path, DirEntry* entries, size_t max_entries,
                    size_t& out_count, size_t& out_total_dirs, size_t& out_total_files) noexcept {
    out_count = 0;
    out_total_dirs = 0;
    out_total_files = 0;
    if (!vfs_path || !vfs_path[0]) return false;

    DIR* dir = opendir(vfs_path);
    if (!dir) return false;

    char full_item_path[384];
    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        const char* name = de->d_name;
        if (!name || !name[0]) continue;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
        // Skip macOS AppleDouble resource fork clutter and system volume directories
        if (name[0] == '.' && (name[1] == '_' ||
            std::strcmp(name, ".Spotlight-V100") == 0 ||
            std::strcmp(name, ".Trashes") == 0 ||
            std::strcmp(name, ".fseventsd") == 0 ||
            std::strcmp(name, ".TemporaryItems") == 0)) {
            continue;
        }

        const int n = std::snprintf(full_item_path, sizeof(full_item_path), "%s/%s", vfs_path, name);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(full_item_path)) continue;

        struct stat st{};
        bool is_dir = (de->d_type == DT_DIR);
        uint64_t fsize = 0;
        int64_t fmtime = 0;
        if (stat(full_item_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            fsize = is_dir ? 0 : static_cast<uint64_t>(st.st_size);
            fmtime = static_cast<int64_t>(st.st_mtime);
        }

        if (is_dir) ++out_total_dirs;
        else ++out_total_files;

        if (entries && out_count < max_entries) {
            DirEntry& item = entries[out_count++];
            std::snprintf(item.name, sizeof(item.name), "%.127s", name);
            item.is_dir = is_dir;
            item.is_html = !is_dir && is_html_file(name);
            item.size = fsize;
            item.mtime = fmtime;
        }
    }
    closedir(dir);

    if (entries && out_count > 1) {
        std::sort(entries, entries + out_count, [](const DirEntry& a, const DirEntry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return ascii_casecmp(a.name, b.name) < 0;
        });
    }
    return true;
}

void format_bytes(uint64_t bytes, char* out, size_t out_cap) noexcept {
    if (!out || out_cap == 0) return;
    if (bytes < 1024ULL) {
        std::snprintf(out, out_cap, "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(out, out_cap, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(out, out_cap, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(out, out_cap, "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
}

size_t handle_dns_query(const uint8_t* query, size_t query_len,
                        uint8_t ip_a, uint8_t ip_b, uint8_t ip_c, uint8_t ip_d,
                        bool wildcard_portal, bool is_mdns,
                        uint8_t* resp_buf, size_t resp_cap) noexcept {
    if (!query || query_len < 12 || !resp_buf || resp_cap < 128) return 0;

    const uint16_t flags = static_cast<uint16_t>((query[2] << 8) | query[3]);
    if (flags & 0x8000U) return 0;  // Ignore responses

    const uint16_t qdcount = static_cast<uint16_t>((query[4] << 8) | query[5]);
    if (qdcount == 0) return 0;

    // Parse first question QNAME
    size_t pos = 12;
    char qname[128];
    size_t qname_len = 0;
    while (pos < query_len) {
        const uint8_t label_len = query[pos++];
        if (label_len == 0) break;
        if ((label_len & 0xC0U) != 0) return 0;  // Compressed pointer in first question unsupported
        if (pos + label_len > query_len) return 0;
        if (qname_len > 0) {
            if (qname_len + 1 >= sizeof(qname)) return 0;
            qname[qname_len++] = '.';
        }
        if (qname_len + label_len >= sizeof(qname)) return 0;
        for (size_t i = 0; i < label_len; ++i) {
            qname[qname_len++] = static_cast<char>(std::tolower(query[pos + i]));
        }
        pos += label_len;
    }
    qname[qname_len] = '\0';

    if (pos + 4 > query_len) return 0;
    const uint16_t qtype = static_cast<uint16_t>((query[pos] << 8) | query[pos + 1]);
    pos += 4;
    const size_t question_end = pos;

    // Only answer A (1) or ANY (255)
    if (qtype != 1 && qtype != 255) return 0;

    const bool matches_tab5 =
        std::strcmp(qname, "tab5.local") == 0 ||
        std::strcmp(qname, "tab5-files.local") == 0 ||
        std::strcmp(qname, "tab5-fileserver.local") == 0 ||
        std::strcmp(qname, "tab5") == 0 ||
        std::strcmp(qname, "tab5.lan") == 0;

    if (!matches_tab5 && !wildcard_portal) return 0;

    const size_t qsection_len = question_end - 12;
    if (12 + qsection_len + 16 > resp_cap) return 0;

    // DNS Header
    resp_buf[0] = query[0];
    resp_buf[1] = query[1];
    resp_buf[2] = 0x84;  // QR=1 (Response), AA=1 (Authoritative)
    resp_buf[3] = 0x00;
    if (is_mdns) {
        // Echo questions or 0 questions; including question + 1 answer works with both mDNS and unicast DNS
    }
    resp_buf[4] = 0x00;
    resp_buf[5] = 0x01;  // QDCOUNT = 1
    resp_buf[6] = 0x00;
    resp_buf[7] = 0x01;  // ANCOUNT = 1
    resp_buf[8] = 0x00;
    resp_buf[9] = 0x00;
    resp_buf[10] = 0x00;
    resp_buf[11] = 0x00;

    std::memcpy(resp_buf + 12, query + 12, qsection_len);
    size_t out = 12 + qsection_len;

    // Answer RR: Pointer to QNAME at offset 12 (0xC00C)
    resp_buf[out++] = 0xC0;
    resp_buf[out++] = 0x0C;
    resp_buf[out++] = 0x00;
    resp_buf[out++] = 0x01;  // TYPE A
    resp_buf[out++] = is_mdns ? 0x80 : 0x00;  // CLASS IN (with cache-flush bit for mDNS)
    resp_buf[out++] = 0x01;
    // TTL: 60 seconds
    resp_buf[out++] = 0x00;
    resp_buf[out++] = 0x00;
    resp_buf[out++] = 0x00;
    resp_buf[out++] = 0x3C;
    // RDLENGTH: 4
    resp_buf[out++] = 0x00;
    resp_buf[out++] = 0x04;
    // RDATA: IPv4 address
    resp_buf[out++] = ip_a;
    resp_buf[out++] = ip_b;
    resp_buf[out++] = ip_c;
    resp_buf[out++] = ip_d;

    return out;
}

}  // namespace tab5_fs
