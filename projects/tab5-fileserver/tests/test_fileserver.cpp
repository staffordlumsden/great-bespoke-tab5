#include "../firmware/main/fs_core.hpp"
#include "../firmware/main/web_assets.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace tab5_fs;

void test_passcode_logic() {
    char code[9] = {0};
    format_passcode(0, code);
    assert(std::strcmp(code, "10000000") == 0);

    format_passcode(38291037, code);
    assert(std::strlen(code) == 8);
    assert(std::strcmp(code, "48291037") == 0);

    char display[12] = {0};
    format_passcode_display(code, display);
    assert(std::strcmp(display, "4829 1037") == 0);

    // Valid inputs (exact, spaced, hyphenated)
    assert(verify_passcode("48291037", code));
    assert(verify_passcode("4829 1037", code));
    assert(verify_passcode("4829-1037", code));
    assert(verify_passcode("  4829 1037 \n", code));

    // Reject wrong length, wrong digit, alpha chars
    assert(!verify_passcode("48291038", code));
    assert(!verify_passcode("4829103", code));
    assert(!verify_passcode("482910370", code));
    assert(!verify_passcode("4829abcd", code));
    assert(!verify_passcode("", code));
}

void test_url_and_cookies() {
    char out[128] = {0};
    assert(url_decode("/files/My%20Document%2B1.html", out, sizeof(out)));
    assert(std::strcmp(out, "/files/My Document+1.html") == 0);

    // Reject embedded NUL %00 and truncated %2
    assert(!url_decode("/files/bad%00name.txt", out, sizeof(out)));
    assert(!url_decode("/files/bad%2", out, sizeof(out)));

    // Query params
    assert(get_query_param("path=%2Ffiles%2FQuickNote&name=note%201.md&code=48291037", "path", out, sizeof(out)));
    assert(std::strcmp(out, "/files/QuickNote") == 0);
    assert(get_query_param("path=%2Ffiles%2FQuickNote&name=note%201.md&code=48291037", "name", out, sizeof(out)));
    assert(std::strcmp(out, "note 1.md") == 0);
    assert(get_query_param("path=%2Ffiles%2FQuickNote&name=note%201.md&code=48291037", "code", out, sizeof(out)));
    assert(std::strcmp(out, "48291037") == 0);

    // Cookies
    assert(extract_cookie_value("theme=dark; tab5_passcode=48291037; foo=bar", "tab5_passcode", out, sizeof(out)));
    assert(std::strcmp(out, "48291037") == 0);
}

void test_sd_path_sanitization() {
    char vfs[256] = {0};
    char rel[256] = {0};

    assert(resolve_sd_path("/sdcard", "/", vfs, sizeof(vfs), rel, sizeof(rel)));
    assert(std::strcmp(vfs, "/sdcard") == 0);
    assert(std::strcmp(rel, "/") == 0);

    assert(resolve_sd_path("/sdcard", "///files//QuickNote/Daily/", vfs, sizeof(vfs), rel, sizeof(rel)));
    assert(std::strcmp(vfs, "/sdcard/files/QuickNote/Daily") == 0);
    assert(std::strcmp(rel, "/files/QuickNote/Daily") == 0);

    // Reject path traversal
    assert(!resolve_sd_path("/sdcard", "/files/../../etc/passwd", vfs, sizeof(vfs), rel, sizeof(rel)));
    assert(!resolve_sd_path("/sdcard", "..", vfs, sizeof(vfs), rel, sizeof(rel)));
    assert(!resolve_sd_path("/sdcard", "/files\\secret", vfs, sizeof(vfs), rel, sizeof(rel)));

    // Validate child joins
    assert(join_sd_child_path("/sdcard", "/files", "report.html", vfs, sizeof(vfs)));
    assert(std::strcmp(vfs, "/sdcard/files/report.html") == 0);
    assert(!join_sd_child_path("/sdcard", "/files", "../evil.bin", vfs, sizeof(vfs)));
    assert(!join_sd_child_path("/sdcard", "/files", "sub/file.bin", vfs, sizeof(vfs)));
}

void test_delivery_classification() {
    // Requirement 8 & 9: HTML files stream inline, all other files download as attachments
    assert(classify_file_delivery("index.html") == DeliveryMode::stream_html);
    assert(classify_file_delivery("REPORT.HTML") == DeliveryMode::stream_html);
    assert(classify_file_delivery("page.htm") == DeliveryMode::stream_html);
    assert(classify_file_delivery("PAGE.HTM") == DeliveryMode::stream_html);

    assert(classify_file_delivery("firmware.bin") == DeliveryMode::download_attachment);
    assert(classify_file_delivery("notes.md") == DeliveryMode::download_attachment);
    assert(classify_file_delivery("archive.zip") == DeliveryMode::download_attachment);
    assert(classify_file_delivery("data.json") == DeliveryMode::download_attachment);
    assert(classify_file_delivery("photo.png") == DeliveryMode::download_attachment);
    assert(classify_file_delivery("html_notes.txt") == DeliveryMode::download_attachment);
}

void test_directory_listing() {
    const fs::path tmp = fs::temp_directory_path() / "tab5_fs_test_root";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "firmware");
    fs::create_directories(tmp / "files");
    fs::create_directories(tmp / ".Spotlight-V100");
    {
        std::ofstream(tmp / "index.html") << "<h1>Hello Tab5</h1>";
        std::ofstream(tmp / "readme.txt") << "Sample text";
        std::ofstream(tmp / "._hidden") << "macOS resource fork";
    }

    DirEntry entries[16]{};
    size_t count = 0, dirs = 0, files = 0;
    assert(list_directory(tmp.c_str(), entries, 16, count, dirs, files));
    assert(dirs == 2);
    assert(files == 2);
    assert(count == 4);

    // Directories sorted first alphabetically: "files", "firmware"
    assert(entries[0].is_dir && std::strcmp(entries[0].name, "files") == 0);
    assert(entries[1].is_dir && std::strcmp(entries[1].name, "firmware") == 0);
    // Files sorted next alphabetically: "index.html" (is_html=true), "readme.txt" (is_html=false)
    assert(!entries[2].is_dir && std::strcmp(entries[2].name, "index.html") == 0 && entries[2].is_html);
    assert(!entries[3].is_dir && std::strcmp(entries[3].name, "readme.txt") == 0 && !entries[3].is_html);

    fs::remove_all(tmp);
}

void test_mdns_dns_responder() {
    // Build a DNS query for "tab5.local" type A (1) class IN (1)
    const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        4, 't', 'a', 'b', '5',
        5, 'l', 'o', 'c', 'a', 'l',
        0,
        0x00, 0x01, 0x00, 0x01
    };
    uint8_t resp[256] = {0};
    const size_t rlen = handle_dns_query(query, sizeof(query), 192, 168, 4, 1, false, true, resp, sizeof(resp));
    assert(rlen > sizeof(query));
    assert(resp[0] == 0x12 && resp[1] == 0x34);
    assert((resp[2] & 0x80) != 0);  // QR=1 response
    assert(resp[rlen - 4] == 192 && resp[rlen - 3] == 168 && resp[rlen - 2] == 4 && resp[rlen - 1] == 1);

    // Non-matching hostname ignored when wildcard_portal is false
    const uint8_t other_q[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        3, 'c', 'o', 'm',
        0,
        0x00, 0x01, 0x00, 0x01
    };
    assert(handle_dns_query(other_q, sizeof(other_q), 192, 168, 4, 1, false, true, resp, sizeof(resp)) == 0);
}

void test_web_ui_contract() {
    // Verify Console palette colors, PixelFonts, dark/light toggle, 8-digit passcode, upload and HTML stream
    assert(std::strstr(tab5_fs_web::kIndexHtml, "/fonts/pixelfont13.ttf") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "/fonts/pixelfont11.ttf") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "#101215") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "#eef1f4") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "light-mode") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "STREAM HTML") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "DOWNLOAD") != nullptr);
    assert(std::strstr(tab5_fs_web::kIndexHtml, "/api/upload") != nullptr);
}

int main() {
    test_passcode_logic();
    test_url_and_cookies();
    test_sd_path_sanitization();
    test_delivery_classification();
    test_directory_listing();
    test_mdns_dns_responder();
    test_web_ui_contract();
    std::printf("PASS: all Tab5FileServer host unit and sanitizer tests passed\n");
    return 0;
}
