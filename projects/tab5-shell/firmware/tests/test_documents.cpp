#include "document_mode.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace tab5_shell;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    char dir[] = "/tmp/tab5-documents-XXXXXX";
    const auto root = std::filesystem::path(mkdtemp(dir));
    Shell shell({(root/"home").string(), root.string(), true});
    std::string error, contents;
    check(shell.prepare(error), "prepare");
    for (auto path : {"/sd/PRIVATE/key", "/sd/Private/key", "/home/a.", "/home/a ", "/home/a:b", "/home/a\\b"}) {
        check(!shell.read_file(path, contents, error), "ambiguous/private path accepted");
    }
    check(!normalize_path("/home", std::string("a\0b",3)).ok, "NUL path accepted");
    check(shell.write_file("note.txt", "first\nsecond\n", error), "initial write");
    DocumentMode editor(shell);
    check(editor.open("note.txt", false, false, error), "editor open");
    check(!editor.dirty(), "starts dirty");
    editor.feed("X"); editor.feed("\x1b[B"); editor.feed("\x05"); editor.feed("!");
    check(editor.text() == "Xfirst\nsecond!\n", "edit/navigation");
    check(editor.checkpoint(), "checkpoint");
    check(shell.read_file("note.txt.tab5-draft", contents, error) && contents == editor.text(), "draft content");
    editor.feed("\x17"); for (char c : std::string("first")) editor.feed(std::string(1,c)); editor.feed("\r");
    auto screen = editor.screen(20,10);
    check(editor.cursor_row() == 1 && editor.cursor_col() == 1, "search position");
    editor.feed("\x13");
    check(!editor.dirty() && shell.read_file("note.txt", contents, error) && contents == editor.text(), "real save");
    editor.feed("\x18"); check(!editor.active(), "clean exit");
    check(shell.write_file("note.txt.tab5-draft", "recovered", error), "draft seed");
    check(editor.open("note.txt", false, true, error) && editor.text() == "recovered" && editor.dirty(), "recovery");
    editor.feed("\x18"); editor.feed("n"); check(!editor.active(), "dirty leave");
    check(!editor.open("note.txt", false, false, error), "existing draft silently overwritten");
    std::filesystem::remove(root/"home/note.txt.tab5-draft");
    check(editor.open("note.txt", false, false, error), "reopen");
    editor.feed("Q");
    check(shell.write_file("note.txt", "external edit", error), "external edit");
    editor.feed("\x13"); check(editor.dirty() && editor.message().find("changed") != std::string::npos, "lost update");
    // Simulated power cut after rotating original but before publication.
    std::filesystem::rename(root/"home/note.txt", root/"home/note.txt.tab5-prev");
    check(!shell.write_file("note.txt", "replacement", error), "recovery original overwritten");
    check(std::filesystem::exists(root/"home/note.txt.tab5-prev"), "old copy lost");
    std::filesystem::create_symlink(root/"outside", root/"home/link.tab5-new");
    check(!shell.write_file("link", "bad", error), "transaction symlink accepted");
    check(!shell.write_file("/sd/other", "bad", error), "outside save accepted");
    check(shell.write_file("binary", std::string("a\0b",3), error), "binary seed");
    check(!editor.open("binary", false, false, error), "binary editable");
    std::string md = "# Heading\n- item one\n1. numbered\n```c\n  int x = 1;\n```\n" + std::string(180,'w') + "\n\x1b[2J";
    auto rows = markdown_rows(md, 20);
    check(rows[0].text == "Heading" && rows[0].style == 33, "heading");
    check(rows[1].text == "* item one", "list");
    check(rows[4].text == "  int x = 1;" && rows[4].style == 36, "code whitespace");
    for (const auto& row : rows) check(row.text.size() <= 20 && row.text.find('\x1b') == std::string::npos, "wrap/escape safety");
    check(shell.write_file("readme.md", md, error), "markdown seed");
    DocumentMode viewer(shell);
    check(viewer.open("readme.md", true, false, error), "viewer open");
    auto first = viewer.screen(20,8); viewer.feed("\x1b[6~");
    auto next = viewer.screen(20,8); check(first[1].text != next[1].text, "scroll");
    viewer.feed("q"); check(!viewer.active(), "viewer exit");
    std::filesystem::remove_all(root);
    std::cout << "PASS documents/filesystem recovery/Markdown ASan+UBSan\n";
}
