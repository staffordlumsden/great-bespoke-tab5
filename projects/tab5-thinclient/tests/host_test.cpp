#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include "app_config.hpp"
#include "keyboard_mapper.hpp"
#include "settings_store.hpp"
#include "ssh_key_utils.hpp"
#include "ssh_socket.hpp"
#include "terminal_buffer.hpp"
#include "terminal_emulator.hpp"

namespace {

void test_ssh_socket_nonblocking() {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(sshSocketSetNonblocking(sockets[0]));
    char byte;
    // A zero-length receive looks like EOF even though the peer is open.
    // Full-packet ciphers can trigger this with the 1 KiB staging buffer.
    assert(recv(sockets[0], &byte, 0, 0) == 0);
    assert(recv(sockets[0], &byte, 1, 0) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    assert(send(sockets[1], "x", 1, 0) == 1);
    assert(recv(sockets[0], &byte, 1, 0) == 1 && byte == 'x');
    close(sockets[0]);
    close(sockets[1]);
    assert(!sshSocketSetNonblocking(-1));
}

void test_terminal_buffer() {
    TerminalBuffer buf(50);
    buf.setViewport(10, 4);
    buf.append("\x1B[1;32malpha\x1B[0m\nbeta\ngamma\ndelta\nepsilon\n");
    assert(buf.atBottom());
    assert(buf.lineAt(0) == "beta" || buf.lineAt(0) == "gamma");

    buf.scroll(1);
    assert(!buf.atBottom());
    buf.scrollToBottom();
    assert(buf.atBottom());

    // Long ANSI-colored string should wrap by visible cells (10), not raw bytes
    buf.clear();
    buf.setViewport(10, 4);
    buf.append("\x1B[1;34m0123456789abc\x1B[0m\n");
    assert(buf.lineAt(0).startsWith("\x1B[1;34m0123456789"));
    assert(buf.lineAt(1).startsWith("\x1B[1;34mabc"));
}

void test_terminal_emulator() {
    TerminalEmulator emu;
    emu.resize(20, 6);

    const char* hello = "Hello\r\nWorld!\x1b[31mRed\x1b[0m";
    emu.write(hello, std::strlen(hello));

    assert(emu.displayCell(0, 0).ch == "H");
    assert(emu.displayCell(4, 0).ch == "o");
    assert(emu.displayCell(0, 1).ch == "W");
    assert(emu.displayCell(6, 1).ch == "R");
    assert(emu.displayCell(6, 1).fg == 1);

    // OSC 777 image hook
    const char* osc = "\x1b]777;tab5-image;path=/sdcard/demo.png\x07";
    emu.write(osc, std::strlen(osc));
    assert(emu.hasOscMessage());
    assert(emu.popOscMessage() == "777;tab5-image;path=/sdcard/demo.png");

    // Scrollback & resize
    for (int i = 0; i < 20; ++i) {
        emu.write("line\r\n");
    }
    assert(emu.scrollbackRows() > 0);
    emu.resize(40, 10);
    assert(emu.columns() == 40);
    assert(emu.rows() == 10);

    // TUI query responses and private CSI ? u / > 7 u must not jump cursor
    emu.reset();
    emu.write("\x1b[4;7H");
    assert(emu.cursorRow() == 3 && emu.cursorColumn() == 6);
    emu.write("\x1b[>7u\x1b[?u");
    assert(emu.cursorRow() == 3 && emu.cursorColumn() == 6);

    emu.write("\x1b[6n\x1b[c\x1b]10;?\x1b\\\x1b]11;?\x07");
    assert(emu.hasReply());
    assert(emu.popReply() == "\x1b[4;7R");
    assert(emu.popReply() == "\x1b[?62;22c");
    assert(emu.popReply() == "\x1b]10;rgb:e6e6/eded/f3f3\x1b\\");
    assert(emu.popReply() == "\x1b]11;rgb:1010/1212/1515\x1b\\");
    assert(!emu.hasReply());

    // Direct 24-bit RGB color packing matches ansiColor565
    emu.write("\x1b[38;2;18;52;86mX");
    assert(emu.displayCell(6, 3).fg == (0x01000000UL | (18UL << 16) | (52UL << 8) | 86UL));
}

void test_keyboard_mapper() {
    KeyboardMapper mapper;
    KeyboardConfig kcfg;
    kcfg.layout = "us";
    mapper.configure(kcfg);

    // Plain 'a' HID usage = 0x04
    KeyAction out = mapper.mapHid(0x00, 0x04);
    assert(out.type == KeyActionType::Text && out.text == "a");

    // Shift + 'a' -> 'A'
    out = mapper.mapHid(0x02, 0x04);
    assert(out.type == KeyActionType::Text && out.text == "A");

    // Ctrl+C -> ETX (0x03)
    out = mapper.mapHid(0x01, 0x06);
    assert(out.type == KeyActionType::Text && out.text.length() == 1 && out.text[0] == '\x03');

    // Esc -> Menu or Esc depending on mapper
    out = mapper.mapHid(0x00, 0x29);
    assert(out.type == KeyActionType::Menu || out.text == "\x1b");

    // Ctrl + = / Ctrl + Shift + = -> FontStep(+1); Ctrl + - -> FontStep(-1)
    out = mapper.mapHid(0x01, 0x2E);
    assert(out.type == KeyActionType::FontStep && out.value == 1);
    out = mapper.mapHid(0x03, 0x2E);
    assert(out.type == KeyActionType::FontStep && out.value == 1);
    out = mapper.mapHid(0x01, 0x2D);
    assert(out.type == KeyActionType::FontStep && out.value == -1);
    out = mapper.mapHid(0x01, 0x57);
    assert(out.type == KeyActionType::FontStep && out.value == 1);
    out = mapper.mapHid(0x01, 0x56);
    assert(out.type == KeyActionType::FontStep && out.value == -1);
}

void test_settings_store_and_sd_import() {
    assert(settings_store::resolveSdKeyPath("/private/id_ed25519") == "/sdcard/private/id_ed25519");
    assert(settings_store::resolveSdKeyPath("sd:/private/id_rsa") == "/sdcard/private/id_rsa");
    assert(settings_store::resolveSdKeyPath("host_key") == "/sdcard/private/host_key");
    assert(settings_store::resolveSdKeyPath("/sdcard/private/k") == "/sdcard/private/k");

    AppConfig cfg;
    cfg.keyboard.layout = "us";
    cfg.keyboard.darkTheme = true;
    cfg.keyboard.displayRotation = 1;
    cfg.keyboard.terminalFont = static_cast<uint8_t>(TerminalFontId::Large);

    WifiProfile w;
    w.name = "home";
    w.ssid = "TestNet";
    w.password = "secret123";
    cfg.wifi.push_back(w);

    SshProfile p;
    p.name = "lan-box";
    p.host = "192.0.2.10";
    p.port = 2222;
    p.user = "alice";
    p.keyPath = "/sdcard/private/id_ed25519";
    cfg.ssh.push_back(p);

    const String json = settings_store::serializeConfigJson(cfg);
    AppConfig roundtrip;
    assert(settings_store::deserializeConfigJson(roundtrip, json));
    assert(roundtrip.keyboard.darkTheme == true);
    assert(roundtrip.keyboard.displayRotation == 1);
    assert(roundtrip.keyboard.terminalFont == static_cast<uint8_t>(TerminalFontId::Large));
    assert(roundtrip.wifi.size() == 1);
    assert(roundtrip.wifi[0].ssid == "TestNet");
    assert(roundtrip.ssh.size() == 1);
    assert(roundtrip.ssh[0].host == "192.0.2.10");
    assert(roundtrip.ssh[0].port == 2222);
    assert(roundtrip.ssh[0].user == "alice");

    // Tab5-Terminal JSON import (single profile object; skips duplicate host+user)
    const String termDupJson =
        R"({"host":"192.0.2.10","port":2222,"username":"alice","private_key_file":"/private/id_ed25519"})";
    assert(settings_store::importTerminalJsonText(roundtrip, termDupJson) == 0);

    const String termNewJson =
        R"({"host":"192.0.2.20","port":22,"username":"bob","private_key_file":"/private/bob_key"})";
    const int addedJson = settings_store::importTerminalJsonText(roundtrip, termNewJson);
    assert(addedJson == 1);
    assert(roundtrip.ssh.size() == 2);
    assert(roundtrip.ssh[1].host == "192.0.2.20");
    assert(roundtrip.ssh[1].user == "bob");
    assert(roundtrip.ssh[1].keyPath == "/sdcard/private/bob_key");

    // Space-delimited tab5-shell-profiles.txt import: name user@host [-p port] [-i key]
    const String shellProfiles =
        "# comment\n"
        "lab-nas carol@192.0.2.30 -p 2222 -i /private/carol_ed25519\n"
        "build-srv bob@192.0.2.20 -p 22 -i bob_key\n";
    const int addedPipe = settings_store::importShellProfilesText(roundtrip, shellProfiles);
    assert(addedPipe == 1);
    assert(roundtrip.ssh.size() == 3);
    assert(roundtrip.ssh[2].name == "lab-nas");
    assert(roundtrip.ssh[2].host == "192.0.2.30");
    assert(roundtrip.ssh[2].port == 2222);
    assert(roundtrip.ssh[2].user == "carol");
    assert(roundtrip.ssh[2].keyPath == "/sdcard/private/carol_ed25519");

    // Stale host IP update under same profile name refreshes existing entry in place
    const String updatedShellProfiles = "lab-nas carol@192.0.2.99 -p 22 -i /private/carol_new\n";
    assert(settings_store::importShellProfilesText(roundtrip, updatedShellProfiles) == 0);
    assert(roundtrip.ssh.size() == 3);
    assert(roundtrip.ssh[2].host == "192.0.2.99");
    assert(roundtrip.ssh[2].port == 22);
    assert(roundtrip.ssh[2].keyPath == "/sdcard/private/carol_new");

    // Upsert, remove stale profile, clear all
    SshProfile direct;
    direct.name = "dave@192.0.2.50";
    direct.user = "dave";
    direct.host = "192.0.2.50";
    assert(settings_store::upsertSshProfile(roundtrip, direct, true) == 3);
    assert(roundtrip.keyboard.activeSsh == 3);
    assert(settings_store::removeSshProfile(roundtrip, "lab-nas"));
    assert(roundtrip.ssh.size() == 3);
    // Higher revision clears stale NVS entries before importing curated profiles
    const String curatedRev =
        R"({"revision":2,"profiles":[{"name":"mac-mini","host":"192.0.2.11","username":"alice","private_key_file":"id_ecdsa_tab5"}]})";
    assert(settings_store::importTerminalJsonText(roundtrip, curatedRev) == 1);
    assert(roundtrip.ssh.size() == 1);
    assert(roundtrip.ssh[0].name == "mac-mini");
    assert(roundtrip.keyboard.sdSyncRevision == 2);

    // Same revision does not wipe newly added hosts
    SshProfile extra;
    extra.name = "extra";
    extra.user = "bob";
    extra.host = "192.0.2.12";
    settings_store::upsertSshProfile(roundtrip, extra, false);
    assert(settings_store::importTerminalJsonText(roundtrip, curatedRev) == 0);
    assert(roundtrip.ssh.size() == 2);

    settings_store::clearSshProfiles(roundtrip);
    assert(roundtrip.ssh.empty());
    assert(roundtrip.keyboard.activeSsh == 0);
}

void test_ssh_key_utils() {
    // Non-EC / OpenSSH / encrypted keys pass through unchanged
    const std::string opensshKey = "-----BEGIN OPENSSH PRIVATE KEY-----\nabcd\n-----END OPENSSH PRIVATE KEY-----\n";
    assert(normalizeEcPrivateKeyPemIfExplicit(opensshKey) == opensshKey);

    const std::string encKey =
        "-----BEGIN EC PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED\nabcd\n-----END EC PRIVATE KEY-----\n";
    assert(normalizeEcPrivateKeyPemIfExplicit(encKey) == encKey);

    // Already namedCurve P-256 EC key (synthetic dummy scalar) passes through unchanged
    const std::string alreadyNamed =
        "-----BEGIN EC PRIVATE KEY-----\n"
        "MHcCAQEEIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8goAoGCCqGSM49\n"
        "AwEHoUQDQgAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\n"
        "-----END EC PRIVATE KEY-----\n";
    assert(normalizeEcPrivateKeyPemIfExplicit(alreadyNamed) == alreadyNamed);

    // Explicit SpecifiedECDomain P-256 key (synthetic dummy scalar) normalizes to namedCurve
    const std::string explicitP256 =
        "-----BEGIN EC PRIVATE KEY-----\n"
        "MIGoAgEBBCAREREREREREREREREREREREREREREREREREREREREREaA7MDkCAQEwLAYHKoZIzj0B\n"
        "AQIhAP////8AAAABAAAAAAAAAAAAAAAA////////////////MAYEAQAEAQChRANCAAQiIiIiIiIi\n"
        "IiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIi\n"
        "-----END EC PRIVATE KEY-----\n";
    const std::string expectedNamedP256 =
        "-----BEGIN EC PRIVATE KEY-----\n"
        "MHcCAQEEIBERERERERERERERERERERERERERERERERERERERERERoAoGCCqGSM49\n"
        "AwEHoUQDQgAEIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIi\n"
        "IiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIg==\n"
        "-----END EC PRIVATE KEY-----\n";
    assert(normalizeEcPrivateKeyPemIfExplicit(explicitP256) == expectedNamedP256);
    assert(normalizeEcPrivateKeyPemIfExplicit(expectedNamedP256) == expectedNamedP256);

    // Malformed / short PEM body returns unchanged
    assert(normalizeEcPrivateKeyPemIfExplicit("-----BEGIN EC PRIVATE KEY-----\nAAAA\n-----END EC PRIVATE KEY-----\n")
               .find("BEGIN EC PRIVATE KEY") != std::string::npos);
}

}  // namespace

int main() {
    test_ssh_socket_nonblocking();
    test_terminal_buffer();
    test_terminal_emulator();
    test_keyboard_mapper();
    test_settings_store_and_sd_import();
    test_ssh_key_utils();
    std::printf("tab5-thinclient host tests: PASS\n");
    return 0;
}
