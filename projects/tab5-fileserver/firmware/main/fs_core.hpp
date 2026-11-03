#pragma once

#include <cstddef>
#include <cstdint>

namespace tab5_fs {

enum class DeliveryMode {
    download_attachment,
    stream_html,
};

struct DirEntry {
    char name[128];
    bool is_dir;
    bool is_html;
    uint64_t size;
    int64_t mtime;
};

// Generate an 8-digit numeric passcode ("10000000".."99999999") from a random seed.
void format_passcode(uint32_t random_value, char out[9]) noexcept;

// Format "12345678" into "1234 5678" for display.
void format_passcode_display(const char code[9], char out[12]) noexcept;

// Strip spaces/hyphens and validate an 8-digit numeric passcode.
bool normalize_passcode_input(const char* input, char out[9]) noexcept;

// Constant-time verification of candidate passcode against active 8-digit passcode.
bool verify_passcode(const char* candidate, const char active_code[9]) noexcept;

// URL-decode src into dst. Rejects invalid %XX or embedded NULs.
bool url_decode(const char* src, char* dst, size_t dst_cap) noexcept;

// Extract and URL-decode a query parameter from a raw query string.
bool get_query_param(const char* query, const char* key, char* out, size_t out_cap) noexcept;

// Extract a cookie value from an HTTP Cookie header.
bool extract_cookie_value(const char* cookie_header, const char* cookie_name,
                          char* out, size_t out_cap) noexcept;

// Sanitize a relative SD path (e.g. "/", "/files/QuickNote") and map it under root_dir ("/sdcard").
// Rejects any path traversal (".."), backslashes, or control characters.
bool resolve_sd_path(const char* root_dir, const char* req_rel_path,
                     char* out_vfs_path, size_t out_vfs_cap,
                     char* out_clean_rel, size_t out_rel_cap) noexcept;

// Validate a single filename or directory name (no slashes, "..", or control chars).
bool validate_filename(const char* name) noexcept;

// Join root_dir + dir_rel_path + child_name safely into out_vfs_path.
bool join_sd_child_path(const char* root_dir, const char* dir_rel_path, const char* child_name,
                        char* out_vfs_path, size_t out_vfs_cap) noexcept;

// Returns true iff filename ends with .html or .htm (case-insensitive).
bool is_html_file(const char* filename) noexcept;

// Requirement 8 & 9: HTML files stream inline; all other files download as attachment.
DeliveryMode classify_file_delivery(const char* filename) noexcept;

// Resolve MIME type for a filename.
const char* mime_type_for_filename(const char* filename) noexcept;

// Read and sort directory contents (directories first, then alphabetical).
bool list_directory(const char* vfs_path, DirEntry* entries, size_t max_entries,
                    size_t& out_count, size_t& out_total_dirs, size_t& out_total_files) noexcept;

// Format byte count into human-readable string (e.g. "14.2 KB", "59.6 GB").
void format_bytes(uint64_t bytes, char* out, size_t out_cap) noexcept;

// Parse a DNS/mDNS query packet and build an A-record response if it matches tab5 hostnames
// (or any hostname when wildcard_portal is true). Returns response length or 0 if ignored.
size_t handle_dns_query(const uint8_t* query, size_t query_len,
                        uint8_t ip_a, uint8_t ip_b, uint8_t ip_c, uint8_t ip_d,
                        bool wildcard_portal, bool is_mdns,
                        uint8_t* resp_buf, size_t resp_cap) noexcept;

}  // namespace tab5_fs
