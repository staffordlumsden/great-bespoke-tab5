#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "boot_policy.h"
#include "line_editor.h"
#include "shell_core.h"

namespace {

using tab5_shell::parse_command;
using tab5_shell::normalize_path;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string run(tab5_shell::Shell& shell, const std::string& command, int& status) {
    std::string output;
    status = shell.execute(command, [&](std::string_view text) { output.append(text); });
    return output;
}

void test_parser() {
    const auto parsed = parse_command("echo \"hello world\" 'two words' escaped\\ space \"\"");
    require(parsed.ok, "quoted command should parse");
    require(parsed.arguments == std::vector<std::string>{
                                   "echo", "hello world", "two words", "escaped space", ""},
            "quoted argument values differ");

    const auto unterminated = parse_command("echo 'missing");
    require(!unterminated.ok, "unterminated quote must fail");
    require(unterminated.error.find("single quote") != std::string::npos,
            "quote error should be specific");
}

void test_paths() {
    auto result = normalize_path("/home", "notes/./today");
    require(result.ok && result.logical == "/home/notes/today",
            "relative path normalization failed");
    result = normalize_path("/home/notes", "../../../../etc");
    require(result.ok && result.logical == "/etc",
            "absolute traversal should normalize visibly, then be rejected by mount policy");
    result = normalize_path("/home", "~");
    require(result.ok && result.logical == "/home", "~ should expand to /home");
    result = normalize_path("/sd", "~/notes");
    require(result.ok && result.logical == "/home/notes", "~/notes should expand to /home/notes");
    result = normalize_path("/home", "~foo");
    require(result.ok && result.logical == "/home/~foo", "~foo without slash should not expand to /home");
    require(tab5_shell::writable_logical_path("/home/a"), "home should be writable");
    require(!tab5_shell::writable_logical_path("/sd/files/QuickNote/a"),
            "other app data must not be writable");
}

void test_filesystem() {
    char temporary[] = "/tmp/tab5-shell-test-XXXXXX";
    char* root_value = mkdtemp(temporary);
    require(root_value != nullptr, "mkdtemp failed");
    const std::filesystem::path root(root_value);
    const std::filesystem::path home = root / "files" / "Tab5Shell";
    const std::filesystem::path outside = root / "outside";
    std::filesystem::create_directories(outside);

    tab5_shell::Shell shell(
        {home.string(), root.string(), true},
        [] { return tab5_shell::MemoryInfo{1000, 700, 500}; },
        [] { return tab5_shell::DiskInfo{2000, 750, true}; });
    std::string error;
    require(shell.prepare(error), "prepare failed: " + error);
    require(std::filesystem::is_directory(home), "home directory was not created");

    int status = 0;
    require(run(shell, "pwd", status) == "/home\n" && status == 0, "pwd failed");
    require(run(shell, "mkdir -p 'Notes/Today'", status).empty() && status == 0,
            "mkdir -p failed");
    require(std::filesystem::is_directory(home / "Notes" / "Today"), "nested mkdir missing");
    require(run(shell, "mkdir Notes", status).find("File exists") != std::string::npos &&
                status == 1,
            "mkdir without -p on existing directory must fail");
    require(run(shell, "mkdir -p Notes", status).empty() && status == 0,
            "mkdir -p on existing directory must succeed");
    require(run(shell, "mkdir -z foo", status).find("invalid option") != std::string::npos &&
                status == 2,
            "mkdir with invalid option must fail");
    require(run(shell, "mkdir -- SubDir", status).empty() && status == 0,
            "mkdir with -- delimiter must succeed");
    require(run(shell, "cd Notes/Today", status).empty() && status == 0, "cd failed");
    require(run(shell, "pwd", status) == "/home/Notes/Today\n", "relative cd path is wrong");
    require(run(shell, "cd ~", status).empty() && status == 0, "cd ~ failed");
    require(run(shell, "pwd", status) == "/home\n", "cd ~ should return to /home");
    require(run(shell, "cd", status).empty() && status == 0, "cd without args failed");
    require(run(shell, "pwd", status) == "/home\n", "cd default should return to /home");

    require(run(shell, "touch 'a file.txt'", status).empty() && status == 0, "touch failed");
    const auto listing = run(shell, "ls", status);
    require(status == 0 && listing.find("a file.txt\n") != std::string::npos,
            "ls did not show touched file");

    {
        std::ofstream file(home / "text.txt", std::ios::binary);
        file << "alpha\nbeta\n";
    }
    require(run(shell, "cat text.txt", status) == "alpha\nbeta\n" && status == 0,
            "cat in /home failed");
    require(run(shell, "cd Notes/Today", status).empty() && status == 0, "cd failed");
    require(run(shell, "cat ../../text.txt", status) == "alpha\nbeta\n" && status == 0,
            "cat failed");
    require(run(shell, "cat /home/Notes", status).find("not a regular file") != std::string::npos &&
                status == 1,
            "cat on directory must fail");
    require(run(shell, "cat missing.txt", status).find("No such file") != std::string::npos &&
                status == 1,
            "cat on nonexistent file must fail");
    require(run(shell, "cd /home/text.txt", status).find("not a directory") != std::string::npos &&
                status == 1,
            "cd to file must fail");
    require(run(shell, "echo 'hello world' shell", status) == "hello world shell\n",
            "echo quoting failed");
    require(run(shell, "df", status).find(" /sd\n") != std::string::npos && status == 0,
            "df failed");
    require(run(shell, "free", status).find("heap") != std::string::npos && status == 0,
            "free failed");

    std::filesystem::create_directories(root / "firmware");
    std::filesystem::create_directories(root / "private");
    std::filesystem::create_directories(root / "files" / "QuickNote");
    {
        std::ofstream priv(root / "private" / "secret.txt", std::ios::binary);
        priv << "secret_data\n";
    }
    require(run(shell, "cat /sd/private/secret.txt", status).find("permission denied") !=
                std::string::npos && status == 126,
            "reading /sd/private must be denied");
    require(run(shell, "ls /sd/private", status).find("permission denied") !=
                std::string::npos && status == 126,
            "listing /sd/private must be denied");
    require(run(shell, "cd /sd/private", status).find("permission denied") !=
                std::string::npos && status == 126,
            "cd to /sd/private must be denied");
    require(run(shell, "touch /sd/firmware/no", status).find("permission denied") !=
                std::string::npos && status == 126,
            "firmware mutation must be denied");
    require(run(shell, "touch /sd/private/no", status).find("permission denied") !=
                std::string::npos && status == 126,
            "private mutation must be denied");
    require(run(shell, "touch /sd/files/QuickNote/no", status).find("permission denied") !=
                std::string::npos && status == 126,
            "other app mutation must be denied");
    require(run(shell, "touch ../../../../../../tmp/escape", status).find("outside") !=
                std::string::npos,
            "traversal outside virtual mounts must be denied");

    std::filesystem::create_symlink(outside, home / "escape-link");
    require(run(shell, "cat /home/escape-link", status).find("symbolic links") != std::string::npos &&
                status == 126,
            "symlink escape must be denied");

    {
        std::ofstream file(home / "large.txt", std::ios::binary);
        for (int index = 0; index < 20000; ++index) file.put('x');
    }
    const auto bounded = run(shell, "cat /home/large.txt", status);
    require(status == 1 && bounded.find("[output truncated]") != std::string::npos &&
                bounded.size() < tab5_shell::kMaximumOutputBytes + 100,
            "cat output bound failed status=" + std::to_string(status) +
                " size=" + std::to_string(bounded.size()));

    require(run(shell, "rm /home/large.txt", status).empty() && status == 0,
            "rm file failed");
    require(!std::filesystem::exists(home / "large.txt"), "rm left file behind");
    require(run(shell, "rm -r Notes", status).find("recursive removal is not implemented") !=
                std::string::npos && status == 2,
            "rm -r must be rejected");
    require(run(shell, "rm -R Notes", status).find("recursive removal is not implemented") !=
                std::string::npos && status == 2,
            "rm -R must be rejected");
    require(run(shell, "rm -rf Notes", status).find("recursive removal is not implemented") !=
                std::string::npos && status == 2,
            "rm -rf must be rejected");
    require(run(shell, "rm -f -r /home/Notes", status).find("recursive removal is not implemented") !=
                std::string::npos && status == 2,
            "rm -f -r must be rejected");
    require(run(shell, "rm /home/Notes", status).find("not empty") != std::string::npos &&
                status == 1,
            "rm non-empty directory must fail");
    require(run(shell, "rm -f missing", status).empty() && status == 0, "rm -f failed");
    require(run(shell, "rm -- '/home/a file.txt'", status).empty() && status == 0,
            "rm -- file failed");
    require(run(shell, "exit", status) == "exit: returning to Console\n" &&
                status == tab5_shell::kStatusExit,
            "exit command should return kStatusExit");
    require(run(shell, "exit extra", status).find("usage: exit") != std::string::npos &&
                status == 2,
            "exit with extra args should report usage");
    require(run(shell, "unknown", status).find("command not found") != std::string::npos &&
                status == 127,
            "unknown command status failed");

    std::filesystem::remove_all(root);
}

void test_without_sd() {
    tab5_shell::Shell shell({"/missing/home", "/missing/sd", false});
    std::string error;
    require(!shell.prepare(error), "prepare should fail without SD");
    int status = 0;
    require(run(shell, "echo diagnostics", status) == "diagnostics\n" && status == 0,
            "diagnostic prompt command should work without SD");
    const auto touch_output = run(shell, "touch /home/no-sd", status);
    require(touch_output.find("SD unavailable") != std::string::npos && status == 1,
            "filesystem command should report missing SD: " + touch_output +
                " status=" + std::to_string(status));
}

void test_boot_policy() {
    tab5_shell::BootSignals signals{true, true, true, false, false};
    require(tab5_shell::can_confirm_local_shell(signals),
            "local shell must not depend on radio or SD");
    signals.display_ready = false;
    require(!tab5_shell::can_confirm_local_shell(signals),
            "display remains a boot requirement");
}

void test_line_editor() {
    tab5_shell::LineEditor editor;
    for (char character : std::string("echo one")) {
        const std::string key(1, character);
        require(editor.feed(key).changed, "printable key should change line");
    }
    require(editor.feed("\x1b[D").changed && editor.cursor() == 7, "left arrow failed");
    require(editor.feed("\x7f").changed && editor.line() == "echo oe", "backspace failed");
    editor.remember("pwd");
    editor.reset();
    require(editor.feed("\x1b[A").changed && editor.line() == "pwd", "history up failed");
    require(editor.feed("\x1b[B").changed && editor.line().empty(), "history down failed");
    require(editor.feed("\x03").interrupt, "Ctrl+C should interrupt input line");
    require(editor.feed("\r").execute, "Return should execute");

    // Test Ctrl+A, Ctrl+E, Ctrl+K, Ctrl+U, Ctrl+W
    for (char character : std::string("first second third")) {
        editor.feed(std::string(1, character));
    }
    require(editor.feed("\x01").changed && editor.cursor() == 0, "Ctrl+A should move cursor to 0");
    require(editor.feed("\x05").changed && editor.cursor() == 18, "Ctrl+E should move cursor to end");
    require(editor.feed("\x17").changed && editor.line() == "first second " && editor.cursor() == 13,
            "Ctrl+W should erase previous word");
    require(editor.feed("\x01").changed && editor.cursor() == 0, "Ctrl+A should move cursor to 0");
    require(editor.feed("\x0b").changed && editor.line().empty() && editor.cursor() == 0,
            "Ctrl+K should kill to end of line");
    for (char character : std::string("test line")) {
        editor.feed(std::string(1, character));
    }
    require(editor.feed("\x15").changed && editor.line().empty() && editor.cursor() == 0,
            "Ctrl+U should clear line before cursor");
}

}  // namespace

int main() {
    try {
        test_parser();
        test_paths();
        test_filesystem();
        test_without_sd();
        test_boot_policy();
        test_line_editor();
        std::cout << "PASS tab5-shell host tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL tab5-shell host tests: " << error.what() << "\n";
        return 1;
    }
}
