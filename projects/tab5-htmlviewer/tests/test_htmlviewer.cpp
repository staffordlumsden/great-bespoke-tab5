#include "../firmware/main/html_core.hpp"
#include "../firmware/main/html_fonts.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

using namespace tab5_html;

static void test_font_family_resolution() {
    // Generic / unspecified sans-serif -> Montserrat
    assert(resolve_font_family("sans-serif") == FontFamily::montserrat);
    assert(resolve_font_family("system-ui, -apple-system, BlinkMacSystemFont, sans-serif") == FontFamily::montserrat);
    assert(resolve_font_family("'Inter', 'Roboto', sans-serif") == FontFamily::montserrat);
    assert(resolve_font_family("Montserrat, sans-serif") == FontFamily::montserrat);

    // Generic / unspecified serif -> Times New Roman
    assert(resolve_font_family("serif") == FontFamily::times_new_roman);
    assert(resolve_font_family("'Times New Roman', Times, serif") == FontFamily::times_new_roman);
    assert(resolve_font_family("'Crimson Pro', Garamond, serif") == FontFamily::times_new_roman);
    assert(resolve_font_family("'Playfair Display', Merriweather, serif") == FontFamily::times_new_roman);

    // Specific installed document fonts
    assert(resolve_font_family("Georgia, 'Times New Roman', serif") == FontFamily::georgia);
    assert(resolve_font_family("Arial, Helvetica, sans-serif") == FontFamily::arial);
    assert(resolve_font_family("Helvetica Neue, Arial, sans-serif") == FontFamily::arial);
    assert(resolve_font_family("Verdana, Geneva, sans-serif") == FontFamily::verdana);
    assert(resolve_font_family("'Courier New', Courier, monospace") == FontFamily::courier_new);
    assert(resolve_font_family("monospace") == FontFamily::courier_new);
}

static void test_truetype_metrics_and_measurement() {
    assert(fonts_init());
    const FontFamily families[] = {
        FontFamily::montserrat,
        FontFamily::times_new_roman,
        FontFamily::georgia,
        FontFamily::arial,
        FontFamily::verdana,
        FontFamily::courier_new,
    };
    for (FontFamily fam : families) {
        for (int b = 0; b <= 1; ++b) {
            for (int it = 0; it <= 1; ++it) {
                const FontMetrics fm = font_get_metrics(fam, 20, b != 0, it != 0);
                assert(fm.ascent_px > 0);
                assert(fm.line_height_px >= 20);
                const int w = font_measure_utf8(fam, 20, b != 0, it != 0, "Hello Tab5 — 2026!", 20);
                assert(w > 40);
            }
        }
    }
}

static void test_html_and_css_parser() {
    HtmlDocument doc{};
    assert(doc_alloc(&doc));

    const char* sample =
        "<!DOCTYPE html><html><head><title>Sydney &amp; Tab5 Report</title>"
        "<style>"
        ":root { --heading-font: 'Times New Roman', serif; --body-font: sans-serif; --accent: #b84a39; }"
        "body { font-family: var(--body-font); background-color: #fbf8f3; color: #1e242b; }"
        "h1, h2 { font-family: var(--heading-font); color: var(--accent); }"
        ".georgia-quote { font-family: Georgia, serif; font-style: italic; border-left: 4px solid #2f6fed; }"
        "</style></head><body>"
        "<h1 id=\"intro\">Introduction &mdash; Overview</h1>"
        "<p>Body text in default sans-serif (Montserrat) with <strong>bold</strong>, "
        "<span style=\"font-family: 'Times New Roman', serif;\">Times serif span</span>, and "
        "<code>inline_code()</code>.</p>"
        "<blockquote class=\"georgia-quote\">Quoted block in Georgia italic.</blockquote>"
        "<ul><li>First item</li><li>Second item with <a href=\"details/page2.html#sec\">link</a></li></ul>"
        "<table><tr><th>Header 1</th><th>Header 2</th></tr><tr><td>Cell A</td><td>Cell B</td></tr></table>"
        "</body></html>";

    assert(parse_html_document(sample, std::strlen(sample), &doc));
    assert(std::strcmp(doc.title, "Sydney & Tab5 Report") == 0);
    assert(doc.base_family == FontFamily::montserrat);
    assert(doc.has_doc_bg);
    assert(doc.outline_count == 1);
    assert(doc.outline[0].level == 1);
    assert(doc.link_count == 1);
    assert(std::strcmp(doc.links[0].href, "details/page2.html#sec") == 0);

    // Verify H1 got Times New Roman from var(--heading-font)
    assert(doc.blocks[0].type == BlockType::heading);
    const HtmlSpan& h1_span = doc.spans[doc.blocks[0].span_start];
    assert(h1_span.family == FontFamily::times_new_roman);

    // Verify paragraph spans: Montserrat -> Montserrat bold -> Times New Roman -> Courier New
    assert(doc.blocks[1].type == BlockType::paragraph);
    bool saw_montserrat = false;
    bool saw_times = false;
    bool saw_courier = false;
    for (uint16_t i = 0; i < doc.blocks[1].span_count; ++i) {
        const HtmlSpan& s = doc.spans[doc.blocks[1].span_start + i];
        if (s.family == FontFamily::montserrat) saw_montserrat = true;
        if (s.family == FontFamily::times_new_roman) saw_times = true;
        if (s.family == FontFamily::courier_new) saw_courier = true;
    }
    assert(saw_montserrat && saw_times && saw_courier);

    // Verify blockquote got Georgia
    assert(doc.blocks[2].type == BlockType::blockquote);
    assert(doc.spans[doc.blocks[2].span_start].family == FontFamily::georgia);

    doc_free(&doc);
}

static void test_href_and_picker_scanning() {
    char rel[192], anchor[48];
    assert(resolve_relative_href("/docs/guide/index.html", "../about.html#team", rel, sizeof(rel), anchor, sizeof(anchor)));
    assert(std::strcmp(rel, "/docs/about.html") == 0);
    assert(std::strcmp(anchor, "team") == 0);

    // Traversal above SD root must fail
    assert(!resolve_relative_href("/index.html", "../../etc/passwd", rel, sizeof(rel), anchor, sizeof(anchor)));

    // Test SD directory & recursive HTML scanner
    char tmp_dir[] = "/tmp/tab5_html_test_XXXXXX";
    assert(mkdtemp(tmp_dir) != nullptr);
    char sub_dir[256];
    std::snprintf(sub_dir, sizeof(sub_dir), "%s/articles", tmp_dir);
    mkdir(sub_dir, 0755);

    char f1[256], f2[256], f3[256];
    std::snprintf(f1, sizeof(f1), "%s/index.html", tmp_dir);
    std::snprintf(f2, sizeof(f2), "%s/articles/paper.htm", tmp_dir);
    std::snprintf(f3, sizeof(f3), "%s/notes.txt", tmp_dir);
    FILE* fp = std::fopen(f1, "wb"); std::fputs("<h1>Root</h1>", fp); std::fclose(fp);
    fp = std::fopen(f2, "wb"); std::fputs("<h1>Article</h1>", fp); std::fclose(fp);
    fp = std::fopen(f3, "wb"); std::fputs("plain text", fp); std::fclose(fp);

    PickerEntry entries[16];
    size_t count = 0;
    assert(scan_all_html_files(tmp_dir, entries, 16, count, 4));
    assert(count == 2);

    assert(list_picker_directory(tmp_dir, "/", true, entries, 16, count));
    // Root with html_only=true should have 1 dir ("articles") + 1 html ("index.html")
    assert(count == 2);
    assert(entries[0].is_dir);
    assert(entries[1].is_html);

    unlink(f1);
    unlink(f2);
    unlink(f3);
    rmdir(sub_dir);
    rmdir(tmp_dir);
}

int main() {
    test_font_family_resolution();
    test_truetype_metrics_and_measurement();
    test_html_and_css_parser();
    test_href_and_picker_scanning();
    std::printf("PASS: all Tab5HtmlViewer host unit and sanitizer tests passed\n");
    return 0;
}
