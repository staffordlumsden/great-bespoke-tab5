#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <vector>

#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_config.hpp"
#include "arduino_compat.hpp"
#include "image_viewer.hpp"
#include "input_tab5_kbd.hpp"
#include "keyboard_mapper.hpp"
#include "local_cli.hpp"
#include "python_runner.hpp"
#include "settings_store.hpp"
#include "ssh_client.hpp"
#include "storage_bridge.hpp"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "terminal_buffer.hpp"
#include "terminal_emulator.hpp"
#include "thinclient_gfx.hpp"
#include "wifi_runtime.hpp"

namespace {

enum class Screen : uint8_t {
    Terminal = 0,
    WifiList,
    WifiScan,
    WifiEdit,
    SshList,
    SshEdit,
    ConfigEdit,
};

struct RawKeyEvent {
    uint8_t usage;
    uint8_t modifiers;
};

struct UiRect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
    bool contains(int px, int py) const {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }
    bool containsPadded(int px, int py, int pad) const {
        return px >= (x - pad) && px < (x + w + pad) && py >= (y - pad) && py < (y + h + pad);
    }
};

constexpr size_t kNumHeaderBtns = 6;  // 0..2 tabs, 3 context, 4 ROT, 5 CONSOLE

struct ChromeLayout {
    int headerH{130};
    UiRect tabs[3];
    UiRect ctxBtn;
    UiRect rotBtn;
    UiRect consoleBtn;
    UiRect statusArea;
    UiRect bodyBtns[4];
};

struct AppState {
    AppConfig config;
    ThinClientGfx gfx;
    TerminalBuffer localTerm{1600};
    TerminalEmulator vt;
    KeyboardMapper kbd;
    WifiRuntime wifi;
    SshClient ssh;
    StorageBridge bridge;
    PythonRunner python;
    ImageViewer imageViewer;

    Screen screen{Screen::Terminal};
    bool dirty{true};
    bool headerDirty{true};
    bool keyboardMenuMode{false};
    uint8_t focusedHeaderIdx{0};
    size_t focusedContentIdx{0};
    size_t listScroll{0};
    size_t editIndex{0};
    uint8_t editField{0};
    size_t editCursor{0};
    bool editInsertNew{false};

    String commandLine;
    size_t commandCursor{0};
    std::vector<String> cmdHistory;
    int historyNavIdx{-1};

    String statusBanner{"Ready"};
    String sdCwd{"/"};
    uint32_t lastBlinkMs{0};
    bool cursorVisible{true};
    bool lastVtCursorDrawn{false};
    size_t lastVtCursorCol{0};
    size_t lastVtCursorRow{0};
    size_t lastVtScrollOffset{0};

    bool sntpStarted{false};
    bool sdReady{false};
    bool cliPythonRepl{false};
    SshCliSession sshSession;
    bool touchWasDown{false};
    int touchStartX{0};
    int touchStartY{0};
    int touchLastY{0};
    bool touchDragged{false};
    uint32_t lastClockMinute{0xFFFFFFFFu};

    ChromeLayout layout;
};

AppState* g_app = nullptr;
QueueHandle_t g_keyQueue = nullptr;

uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool usbKeyFilter(uint8_t hidUsage, uint8_t mods, void*) {
    if (!g_keyQueue || hidUsage == 0) return false;
    RawKeyEvent ev{hidUsage, mods};
    xQueueSend(g_keyQueue, &ev, 0);
    return true;
}

bool tab5KeyFilter(uint8_t hidUsage, uint8_t mods) {
    if (!g_keyQueue || hidUsage == 0) return false;
    RawKeyEvent ev{hidUsage, mods};
    xQueueSend(g_keyQueue, &ev, 0);
    return true;
}

String plainLocalPrompt(const AppState& s) {
    if (s.cliPythonRepl) return ">>> ";
    if (s.sshSession.active) return sshSessionPlainPrompt(s.sshSession);
    String dev = s.config.system.deviceName.length() ? s.config.system.deviceName : String("tab5");
    return dev + ":" + s.sdCwd + "$ ";
}

String coloredLocalPrompt(const AppState& s) {
    if (s.cliPythonRepl) return "\x1B[1;36m>>> \x1B[0m";
    if (s.sshSession.active) return sshSessionColoredPrompt(s.sshSession);
    String dev = s.config.system.deviceName.length() ? s.config.system.deviceName : String("tab5");
    return String("\x1B[1;32m") + dev + "\x1B[0m:\x1B[1;34m" + s.sdCwd + "\x1B[0m$ ";
}

void appendLocalLine(AppState& s, const String& line) {
    s.localTerm.append(line + "\n");
    s.localTerm.scrollToBottom();
    if (s.screen == Screen::Terminal && !s.ssh.connected()) {
        s.dirty = true;
    }
}

void setBanner(AppState& s, const String& msg) {
    s.statusBanner = msg;
    s.headerDirty = true;
}

int terminalLineStep(const AppState& s) {
    const auto& spec = ThinClientGfx::fontSpec(s.config.keyboard.terminalFont);
    int minStep = std::max(14, spec.settingsLineHeight + 2);
    int step = static_cast<int>(s.config.keyboard.terminalLineStep);
    if (step < minStep) {
        step = std::max(spec.defaultLineStep, minStep);
    }
    return step;
}

constexpr int kTermPadX = 14;
constexpr int kTermPadY = 10;

void configureTerminalGeometry(AppState& s) {
    s.kbd.configure(s.config.keyboard);
    const auto& spec = ThinClientGfx::fontSpec(s.config.keyboard.terminalFont);
    int headerH = s.gfx.isPortrait() ? 124 : 130;
    s.layout.headerH = headerH;
    int availW = std::max(120, s.gfx.width() - kTermPadX * 2);
    int availH = std::max(80, s.gfx.height() - headerH - kTermPadY * 2);
    int cols = std::max(24, availW / spec.cellW);
    int rows = std::max(6, availH / terminalLineStep(s));
    s.localTerm.setViewport(static_cast<size_t>(cols), static_cast<size_t>(rows));
    s.vt.resize(static_cast<size_t>(cols), static_cast<size_t>(rows));
    s.vt.markAllDirty();
    if (s.ssh.connected()) {
        s.ssh.resizePty(cols, rows);
    }
    s.python.setCanvasViewport(0, headerH, s.gfx.width(), s.gfx.height() - headerH, s.config.keyboard.darkTheme);
}

void syncTimezone(const AppState& s) {
    int offsetMin = s.config.system.utcOffsetMinutes;
    int posixSignOffset = -offsetMin;  // POSIX TZ sign is inverted relative to ISO UTC offset
    int absMin = std::abs(posixSignOffset);
    int hh = absMin / 60;
    int mm = absMin % 60;
    char tz[32];
    snprintf(tz, sizeof(tz), "UTC%c%d:%02d", posixSignOffset >= 0 ? '+' : '-', hh, mm);
    setenv("TZ", tz, 1);
    tzset();
}

void startSntpIfNeeded(AppState& s) {
    if (s.sntpStarted || !s.wifi.hasIp()) return;
    syncTimezone(s);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    const char* srv = s.config.system.ntpServer.length() ? s.config.system.ntpServer.c_str() : "pool.ntp.org";
    esp_sntp_setservername(0, const_cast<char*>(srv));
    esp_sntp_init();
    s.sntpStarted = true;
}

ChromeLayout computeLayout(const AppState& s) {
    ChromeLayout l{};
    const int w = s.gfx.width();
    const bool portrait = s.gfx.isPortrait();
    l.headerH = portrait ? 124 : 130;

    if (!portrait) {
        int x = 16;
        const int y1 = 12;
        const int h1 = 54;
        const int tabWidths[3] = {120, 120, 120};
        for (int i = 0; i < 3; ++i) {
            l.tabs[i] = {x, y1, tabWidths[i], h1};
            x += tabWidths[i] + 10;
        }
        l.consoleBtn = {w - 168, y1, 152, h1};
        l.rotBtn = {l.consoleBtn.x - 98, y1, 88, h1};
        l.ctxBtn = {l.rotBtn.x - 128, y1, 118, h1};
        l.statusArea = {16, 74, w - 32, 46};
    } else {
        int x = 12;
        const int y1 = 10;
        const int h1 = 50;
        const int tabWidths[3] = {90, 90, 90};
        for (int i = 0; i < 3; ++i) {
            l.tabs[i] = {x, y1, tabWidths[i], h1};
            x += tabWidths[i] + 8;
        }
        l.consoleBtn = {w - 138, y1, 126, h1};
        l.rotBtn = {l.consoleBtn.x - 78, y1, 72, h1};
        l.ctxBtn = {l.rotBtn.x - 102, y1, 96, h1};
        l.statusArea = {12, 68, w - 24, 46};
    }

    int by = l.headerH + 54;
    int bw = portrait ? std::min(164, (w - 50) / 4) : 178;
    int gap = portrait ? 6 : 12;
    for (int i = 0; i < 4; ++i) {
        l.bodyBtns[i] = {16 + i * (bw + gap), by, bw, 50};
    }
    return l;
}

uint16_t ansiColor565(uint32_t c, bool fg, const ConsolePalette& pal) {
    if (c == 7 && fg) return pal.termFg;
    if (c == 0 && !fg) return pal.termBg;
    if (c & 0x01000000u) {
        return ThinClientGfx::rgb565(static_cast<uint8_t>((c >> 16) & 0xFF),
                                     static_cast<uint8_t>((c >> 8) & 0xFF),
                                     static_cast<uint8_t>(c & 0xFF));
    }
    static constexpr uint32_t kAnsi16[16] = {
        0x101215, 0xef4444, 0x22c55e, 0xeab308, 0x3b82f6, 0xa855f7, 0x06b6d4, 0xe6edf3,
        0x6b7280, 0xf87171, 0x4ade80, 0xfde047, 0x60a5fa, 0xc084fc, 0x22d3ee, 0xffffff,
    };
    if (c < 16) {
        return ThinClientGfx::rgb565From888(kAnsi16[c]);
    }
    if (c < 232) {
        uint32_t idx = c - 16;
        auto level = [](uint32_t v) -> uint8_t { return v ? static_cast<uint8_t>(55 + v * 40) : 0; };
        return ThinClientGfx::rgb565(level(idx / 36), level((idx / 6) % 6), level(idx % 6));
    }
    uint8_t gray = static_cast<uint8_t>(8 + (c - 232) * 10);
    return ThinClientGfx::rgb565(gray, gray, gray);
}

void drawChromeButton(ThinClientGfx& gfx, const ConsolePalette& pal, const UiRect& r,
                      const char* label, uint16_t fill, uint16_t border, uint16_t textCol,
                      bool focused) {
    gfx.fillRoundRect(r.x, r.y, r.w, r.h, 4, fill);
    gfx.drawRoundRect(r.x, r.y, r.w, r.h, 4, focused ? pal.text : border);
    if (focused) {
        gfx.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3, pal.blue);
    }
    const auto& font = cyd_fonts::pixelfont_11x5_thin;
    int tw = ThinClientGfx::pixelTextWidth(font, label, 2);
    int tx = r.x + std::max(2, (r.w - tw) / 2);
    int ty = r.y + std::max(2, (r.h - font.ascent * 2) / 2);
    gfx.drawPixelText(font, label, tx, ty, textCol, 2, true);
}

const char* contextLabel(const AppState& s) {
    switch (s.screen) {
        case Screen::Terminal: return s.ssh.connected() ? "DISC" : "CONN";
        case Screen::WifiList: return "SCAN";
        case Screen::WifiScan: return "BACK";
        case Screen::SshList: return "CONN";
        case Screen::WifiEdit:
        case Screen::SshEdit:
        case Screen::ConfigEdit: return "SAVE";
    }
    return "OK";
}

void drawHeader(AppState& s) {
    s.layout = computeLayout(s);
    const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
    auto& gfx = s.gfx;
    const int w = gfx.width();
    const int hh = s.layout.headerH;

    gfx.fillRect(0, 0, w, hh - 3, pal.surface);
    gfx.fillRect(0, hh - 3, w, 3, pal.border);

    static const char* kTabLabels[3] = {"TERM", "WIFI", "CONF"};
    int activeTab = 0;
    if (s.screen == Screen::WifiList || s.screen == Screen::WifiScan || s.screen == Screen::WifiEdit) activeTab = 1;
    else if (s.screen == Screen::ConfigEdit) activeTab = 2;
    else if (s.screen == Screen::SshList || s.screen == Screen::SshEdit) activeTab = -1;

    for (int i = 0; i < 3; ++i) {
        bool active = (i == activeTab);
        bool focused = s.keyboardMenuMode && (s.focusedHeaderIdx == i);
        uint16_t fill = active ? pal.blue : pal.surfacePressed;
        uint16_t border = active ? pal.blue : pal.border;
        uint16_t txt = active ? pal.text : pal.muted;
        drawChromeButton(gfx, pal, s.layout.tabs[i], kTabLabels[i], fill, border, txt, focused);
    }

    // Context, Rotation, and CONSOLE return buttons
    drawChromeButton(gfx, pal, s.layout.ctxBtn, contextLabel(s),
                     s.ssh.connected() ? pal.red : pal.green,
                     s.ssh.connected() ? pal.red : pal.green,
                     pal.bg,
                     s.keyboardMenuMode && s.focusedHeaderIdx == 3);

    char rotLabel[8];
    snprintf(rotLabel, sizeof(rotLabel), "R%u", static_cast<unsigned>(s.config.keyboard.displayRotation * 90));
    drawChromeButton(gfx, pal, s.layout.rotBtn, rotLabel,
                     pal.surfacePressed, pal.border, pal.text,
                     s.keyboardMenuMode && s.focusedHeaderIdx == 4);

    drawChromeButton(gfx, pal, s.layout.consoleBtn, "CONSOLE",
                     pal.surfacePressed, pal.orange, pal.orange,
                     s.keyboardMenuMode && s.focusedHeaderIdx == 5);

    // Full-width IP & status telemetry bar (22 px PixelFont height)
    const UiRect& sa = s.layout.statusArea;
    gfx.fillRoundRect(sa.x, sa.y, sa.w, sa.h, 5, pal.bg);
    gfx.drawRoundRect(sa.x, sa.y, sa.w, sa.h, 5, pal.border);

    uint16_t dotColor = s.wifi.hasIp() ? pal.green : (s.wifi.isConnected() ? pal.yellow : pal.red);
    gfx.fillCircle(sa.x + 18, sa.y + sa.h / 2, 6, dotColor);

    String netSummary;
    if (s.wifi.hasIp()) {
        netSummary = String("IP ") + s.wifi.ipAddress();
        if (s.wifi.ssid().length()) {
            netSummary += String(" (") + s.wifi.ssid() + ")";
        }
        if (s.ssh.connected() && s.config.keyboard.activeSsh < s.config.ssh.size()) {
            const auto& p = s.config.ssh[s.config.keyboard.activeSsh];
            netSummary += String(" | SSH ") + p.user + "@" + p.host;
        } else if (s.statusBanner.length() && s.statusBanner != "Ready") {
            netSummary += String(" | ") + s.statusBanner;
        }
    } else if (s.ssh.connected() && s.config.keyboard.activeSsh < s.config.ssh.size()) {
        const auto& p = s.config.ssh[s.config.keyboard.activeSsh];
        netSummary = String("SSH ") + p.user + "@" + p.host;
    } else {
        netSummary = s.statusBanner.length() ? s.statusBanner : String("No Wi-Fi");
    }

    const auto& spec = ThinClientGfx::fontSpec(s.config.keyboard.terminalFont);
    char rightBadge[32];
    time_t now = time(nullptr);
    struct tm tmNow{};
    localtime_r(&now, &tmNow);
    if (tmNow.tm_year + 1900 >= 2024) {
        snprintf(rightBadge, sizeof(rightBadge), "%dx%d  %02d:%02d", spec.cellW, spec.cellH, tmNow.tm_hour, tmNow.tm_min);
    } else {
        snprintf(rightBadge, sizeof(rightBadge), "%dx%d", spec.cellW, spec.cellH);
    }

    const auto& pfont = cyd_fonts::pixelfont_11x5_thin;
    const int textScale = 2;
    int bw = ThinClientGfx::pixelTextWidth(pfont, rightBadge, textScale);
    int leftStart = sa.x + 34;
    int availW = std::max(40, (sa.x + sa.w - bw - 18) - leftStart);
    while (netSummary.length() > 8 &&
           ThinClientGfx::pixelTextWidth(pfont, netSummary.c_str(), textScale) > availW) {
        netSummary = netSummary.substring(0, netSummary.length() - 1);
    }
    int ty = sa.y + std::max(2, (sa.h - pfont.ascent * textScale) / 2);
    gfx.drawPixelText(pfont, netSummary.c_str(), leftStart, ty, pal.text, textScale, true);
    gfx.drawPixelText(pfont, rightBadge, sa.x + sa.w - bw - 12, ty, pal.muted, textScale, true);
    s.headerDirty = false;
}

void drawVtCell(AppState& s, size_t col, size_t row, int cellW, int lineStep, int top, bool asCursor, const ConsolePalette& pal) {
    const auto& cell = s.vt.displayCell(col, row);
    if (cell.continuation) return;
    int x = kTermPadX + static_cast<int>(col) * cellW;
    int y = top + static_cast<int>(row) * lineStep;
    int w = cell.wide ? cellW * 2 : cellW;

    uint32_t cellFg = (cell.bold && cell.fg < 8) ? (cell.fg + 8) : cell.fg;
    uint32_t fgId = cell.inverse ? cell.bg : cellFg;
    uint32_t bgId = cell.inverse ? cellFg : cell.bg;
    if (asCursor) std::swap(fgId, bgId);
    uint16_t fg = ansiColor565(fgId, !cell.inverse ^ asCursor, pal);
    uint16_t bg = ansiColor565(bgId, cell.inverse ^ asCursor, pal);

    s.gfx.fillRect(x, y, w, lineStep, bg);
    const auto& spec = ThinClientGfx::fontSpec(s.config.keyboard.terminalFont);
    if (cell.ch.length() && cell.ch != " ") {
        uint32_t cp = ThinClientGfx::utf8CodepointAt(cell.ch.c_str(), cell.ch.length(), 0);
        if (!s.gfx.drawBlockGlyph(cp, x, y, w, lineStep, fg, bg) &&
            !s.gfx.drawBoxGlyph(cp, x, y, w, lineStep, fg)) {
            s.gfx.drawTerminalText(spec, lineStep, cell.ch, x, y, fg, bg);
        }
    }
}

struct AnsiStyleState {
    uint32_t fg{7};
    uint32_t bg{0};
    bool bold{false};
    bool inverse{false};
};

void applyAnsiSgr(const String& paramsStr, AnsiStyleState& st) {
    std::vector<int> params;
    size_t start = 0;
    while (start <= paramsStr.length()) {
        int semi = paramsStr.indexOf(';', start);
        size_t end = semi < 0 ? paramsStr.length() : static_cast<size_t>(semi);
        String part = paramsStr.substring(start, end);
        params.push_back(part.length() ? atoi(part.c_str()) : 0);
        if (semi < 0) break;
        start = static_cast<size_t>(semi) + 1;
    }
    if (params.empty()) params.push_back(0);
    for (size_t i = 0; i < params.size(); ++i) {
        int p = params[i];
        if (p == 0) {
            st = AnsiStyleState{};
        } else if (p == 1) {
            st.bold = true;
        } else if (p == 7) {
            st.inverse = true;
        } else if (p == 22) {
            st.bold = false;
        } else if (p == 27) {
            st.inverse = false;
        } else if (p >= 30 && p <= 37) {
            st.fg = static_cast<uint32_t>(p - 30);
        } else if (p == 39) {
            st.fg = 7;
        } else if (p >= 40 && p <= 47) {
            st.bg = static_cast<uint32_t>(p - 40);
        } else if (p == 49) {
            st.bg = 0;
        } else if (p >= 90 && p <= 97) {
            st.fg = static_cast<uint32_t>(p - 90 + 8);
        } else if (p >= 100 && p <= 107) {
            st.bg = static_cast<uint32_t>(p - 100 + 8);
        } else if ((p == 38 || p == 48) && i + 2 < params.size() && params[i + 1] == 5) {
            uint32_t c = static_cast<uint32_t>(std::max(0, std::min(255, params[i + 2])));
            if (p == 38) st.fg = c;
            else st.bg = c;
            i += 2;
        } else if ((p == 38 || p == 48) && i + 4 < params.size() && params[i + 1] == 2) {
            uint32_t r = static_cast<uint32_t>(std::max(0, std::min(255, params[i + 2])));
            uint32_t g = static_cast<uint32_t>(std::max(0, std::min(255, params[i + 3])));
            uint32_t b = static_cast<uint32_t>(std::max(0, std::min(255, params[i + 4])));
            uint32_t c = 0x01000000u | (r << 16) | (g << 8) | b;
            if (p == 38) st.fg = c;
            else st.bg = c;
            i += 4;
        }
    }
}

int drawAnsiLine(AppState& s, const TerminalFontSpec& spec, int lineStep, int x, int lineTop,
                 const String& text, const ConsolePalette& pal) {
    AnsiStyleState st;
    int cursorX = x;
    const size_t n = text.length();
    size_t i = 0;
    while (i < n) {
        int esc = text.indexOf('\x1B', i);
        size_t chunkEnd = esc < 0 ? n : static_cast<size_t>(esc);
        if (chunkEnd > i) {
            String run = text.substring(i, chunkEnd);
            uint32_t fgId = (st.bold && st.fg < 8) ? (st.fg + 8) : st.fg;
            uint32_t bgId = st.bg;
            if (st.inverse) std::swap(fgId, bgId);
            uint16_t fg = ansiColor565(fgId, !st.inverse, pal);
            uint16_t bg = ansiColor565(bgId, st.inverse, pal);
            if (bg != pal.termBg) {
                int rw = ThinClientGfx::terminalTextWidth(spec, run);
                if (rw > 0) s.gfx.fillRect(cursorX, lineTop, rw, lineStep, bg);
            }
            cursorX += s.gfx.drawTerminalText(spec, lineStep, run, cursorX, lineTop, fg, bg);
        }
        if (esc < 0) break;
        size_t pos = static_cast<size_t>(esc);
        if (pos + 1 < n && text[pos + 1] == '[') {
            size_t j = pos + 2;
            while (j < n && (static_cast<uint8_t>(text[j]) < 0x40 || static_cast<uint8_t>(text[j]) > 0x7E)) {
                ++j;
            }
            if (j < n) {
                if (text[j] == 'm') {
                    applyAnsiSgr(text.substring(pos + 2, j), st);
                }
                i = j + 1;
            } else {
                i = n;
            }
        } else {
            i = pos + 1;
        }
    }
    return cursorX - x;
}

void drawTerminal(AppState& s) {
    const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
    const auto& spec = ThinClientGfx::fontSpec(s.config.keyboard.terminalFont);
    const int lineStep = terminalLineStep(s);
    const int top = s.layout.headerH + kTermPadY;
    auto& gfx = s.gfx;

    if (s.ssh.connected()) {
        if (s.dirty) {
            const int hh = s.layout.headerH;
            gfx.fillRect(0, hh, gfx.width(), kTermPadY, pal.surface);
            gfx.fillRect(0, gfx.height() - kTermPadY, gfx.width(), kTermPadY, pal.surface);
            gfx.fillRect(0, hh, kTermPadX, gfx.height() - hh, pal.surface);
            gfx.fillRect(gfx.width() - kTermPadX, hh, kTermPadX, gfx.height() - hh, pal.surface);
            gfx.drawRect(kTermPadX - 2, hh + kTermPadY - 2,
                         gfx.width() - (kTermPadX - 2) * 2, gfx.height() - hh - (kTermPadY - 2) * 2,
                         pal.border);
        }
        if (s.lastVtScrollOffset != s.vt.scrollbackOffset()) {
            s.vt.markAllDirty();
            s.lastVtScrollOffset = s.vt.scrollbackOffset();
        }
        const bool viewingHistory = s.vt.scrollbackOffset() > 0;
        const bool drawCur = !viewingHistory && s.cursorVisible && s.vt.cursorVisible();
        const size_t cCol = s.vt.cursorColumn();
        const size_t cRow = s.vt.cursorRow();
        if (s.lastVtCursorDrawn && (s.lastVtCursorCol != cCol || s.lastVtCursorRow != cRow || !drawCur)) {
            if (s.lastVtCursorCol < s.vt.columns() && s.lastVtCursorRow < s.vt.rows()) {
                drawVtCell(s, s.lastVtCursorCol, s.lastVtCursorRow, spec.cellW, lineStep, top, false, pal);
            }
        }
        for (size_t row = 0; row < s.vt.rows(); ++row) {
            for (size_t col = 0; col < s.vt.columns(); ++col) {
                const auto& cell = s.vt.displayCell(col, row);
                bool isCur = drawCur && row == cRow && col == cCol;
                if (cell.dirty || isCur) {
                    drawVtCell(s, col, row, spec.cellW, lineStep, top, isCur, pal);
                }
            }
        }
        s.vt.clearDirty();
        s.lastVtCursorDrawn = drawCur;
        s.lastVtCursorCol = cCol;
        s.lastVtCursorRow = cRow;
        return;
    }

    if (s.python.canvasActive()) {
        // Leave Python canvas pixels intact unless terminal explicitly cleared.
        return;
    }

    gfx.fillRect(0, s.layout.headerH, gfx.width(), gfx.height() - s.layout.headerH, pal.termBg);
    const bool drawEditor = s.localTerm.atBottom();
    const size_t editorRow = drawEditor ? s.localTerm.inputViewportRow() : s.localTerm.viewportRows();
    const bool maskInput = s.sshSession.active && s.sshSession.step == SshMenuStep::AddPassword;

    for (size_t row = 0; row < s.localTerm.viewportRows(); ++row) {
        int lineTop = top + static_cast<int>(row) * lineStep;
        if (drawEditor && row == editorRow) {
            String shownCmd = maskInput ? String(std::string(s.commandLine.length(), '*')) : s.commandLine;
            size_t cur = std::min(s.commandCursor, shownCmd.length());
            String typedBefore = shownCmd.substring(0, cur);
            String curGlyph = " ";
            String suffix;
            if (cur < shownCmd.length()) {
                size_t next = cur + ThinClientGfx::utf8CharLength(static_cast<uint8_t>(shownCmd[cur]));
                curGlyph = shownCmd.substring(cur, next);
                suffix = shownCmd.substring(next);
            }
            int x = kTermPadX;
            int promptW = ThinClientGfx::terminalTextWidth(spec, plainLocalPrompt(s));
            int maxW = std::max(40, gfx.width() - kTermPadX * 2);
            if (promptW < maxW - 32) {
                x += drawAnsiLine(s, spec, lineStep, x, lineTop, coloredLocalPrompt(s), pal);
            }
            int availTypedW = std::max(24, maxW - (x - kTermPadX));
            size_t visibleStart = 0;
            while (visibleStart < typedBefore.length() &&
                   ThinClientGfx::terminalTextWidth(spec, typedBefore.substring(visibleStart)) > availTypedW) {
                ++visibleStart;
                while (visibleStart < typedBefore.length() &&
                       (static_cast<uint8_t>(typedBefore[visibleStart]) & 0xC0) == 0x80) {
                    ++visibleStart;
                }
            }
            x += gfx.drawTerminalText(spec, lineStep, typedBefore.substring(visibleStart), x, lineTop, pal.termFg, pal.termBg);
            int curW = std::max(spec.cellW, ThinClientGfx::terminalTextWidth(spec, curGlyph));
            if (s.cursorVisible) {
                gfx.fillRect(x, lineTop, curW, lineStep, pal.termFg);
                gfx.drawTerminalText(spec, lineStep, curGlyph, x, lineTop, pal.termBg, pal.termFg);
            } else {
                gfx.drawTerminalText(spec, lineStep, curGlyph, x, lineTop, pal.termFg, pal.termBg);
            }
            x += curW;
            if (suffix.length()) {
                gfx.drawTerminalText(spec, lineStep, suffix, x, lineTop, pal.termFg, pal.termBg);
            }
            continue;
        }
        String line = s.localTerm.lineAt(row);
        if (line.length()) {
            drawAnsiLine(s, spec, lineStep, kTermPadX, lineTop, line, pal);
        }
    }
}

void drawSettingsHeaderAndButtons(AppState& s, const char* title, const char* b0, const char* b1, const char* b2, const char* b3) {
    const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
    s.gfx.fillRect(0, s.layout.headerH, s.gfx.width(), s.gfx.height() - s.layout.headerH, pal.bg);
    s.gfx.drawPixelText(cyd_fonts::pixelfont_11x5_thin, title, 16, s.layout.headerH + 14, pal.text, 2, true);
    const char* labels[4] = {b0, b1, b2, b3};
    for (int i = 0; i < 4; ++i) {
        if (!labels[i]) continue;
        uint16_t fill = (i == 2) ? pal.green : pal.surface;
        uint16_t txt = (i == 2) ? pal.bg : pal.text;
        bool focused = !s.keyboardMenuMode && s.focusedContentIdx == static_cast<size_t>(i);
        drawChromeButton(s.gfx, pal, s.layout.bodyBtns[i], labels[i], fill, pal.border, txt, focused);
    }
}

int listTop(const AppState& s) { return s.layout.headerH + 114; }
int rowHeight() { return 50; }
size_t visibleRows(const AppState& s) {
    return static_cast<size_t>(std::max(1, (s.gfx.height() - listTop(s) - 14) / rowHeight()));
}

void drawRowCard(AppState& s, int y, bool selected, bool active, const String& left, const String& right) {
    const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
    uint16_t bg = selected ? pal.selectBg : pal.surface;
    uint16_t bd = selected ? pal.selectBorder : (active ? pal.green : pal.border);
    int w = s.gfx.width() - 32;
    s.gfx.fillRoundRect(16, y, w, rowHeight() - 8, 5, bg);
    s.gfx.drawRoundRect(16, y, w, rowHeight() - 8, 5, bd);
    s.gfx.drawSettingsString(lgfx_font_japan_gothic_20, left, 26, y + 11, pal.text);
    if (right.length()) {
        int rw = s.gfx.settingsTextWidth(lgfx_font_japan_gothic_16, right);
        s.gfx.drawSettingsString(lgfx_font_japan_gothic_16, right, 16 + w - rw - 16, y + 13, active ? pal.green : pal.muted);
    }
}

void drawWifiList(AppState& s) {
    drawSettingsHeaderAndButtons(s, "WI-FI PROFILES", "SCAN", "ADD", "CONNECT", "RETRY");
    int top = listTop(s);
    size_t vis = visibleRows(s);
    if (s.config.wifi.empty()) {
        s.gfx.drawSettingsString(lgfx_font_japan_gothic_20,
                                 String("Console saved Wi-Fi: ") + (s.wifi.ssid().length() ? s.wifi.ssid() : s.wifi.statusMessage()),
                                 14, top + 6, ThinClientGfx::palette(s.config.keyboard.darkTheme).muted);
        return;
    }
    for (size_t r = 0; r < vis && (s.listScroll + r) < s.config.wifi.size(); ++r) {
        size_t i = s.listScroll + r;
        bool act = (i == s.config.keyboard.activeWifi);
        bool sel = !s.keyboardMenuMode && s.focusedContentIdx == i + 4;
        String left = String(act ? "* " : "  ") + s.config.wifi[i].name + " | " + s.config.wifi[i].ssid;
        drawRowCard(s, top + static_cast<int>(r) * rowHeight(), sel, act, left, act ? "ACTIVE" : "EDIT");
    }
}

void drawWifiScan(AppState& s) {
    drawSettingsHeaderAndButtons(s, "WI-FI SCAN RESULTS", "RESCAN", "BACK", nullptr, nullptr);
    int top = listTop(s);
    size_t vis = visibleRows(s);
    const auto& nets = s.wifi.scannedNetworks();
    for (size_t r = 0; r < vis && (s.listScroll + r) < nets.size(); ++r) {
        size_t i = s.listScroll + r;
        bool sel = !s.keyboardMenuMode && s.focusedContentIdx == i + 2;
        String left = nets[i].ssid;
        String right = String(nets[i].rssi) + " dBm  " + (nets[i].secure ? "SEC" : "OPEN");
        drawRowCard(s, top + static_cast<int>(r) * rowHeight(), sel, false, left, right);
    }
}

void drawSshList(AppState& s) {
    drawSettingsHeaderAndButtons(s, "SSH PROFILES (NVS + SD)", "SD SYNC", "ADD", "CONNECT", "EDIT");
    int top = listTop(s);
    size_t vis = visibleRows(s);
    if (s.config.ssh.empty()) {
        s.gfx.drawSettingsString(lgfx_font_japan_gothic_20,
                                 "No SSH profiles. Tap ADD or place /sdcard/private/tab5-terminal.json",
                                 14, top + 6, ThinClientGfx::palette(s.config.keyboard.darkTheme).muted);
        return;
    }
    for (size_t r = 0; r < vis && (s.listScroll + r) < s.config.ssh.size(); ++r) {
        size_t i = s.listScroll + r;
        const auto& p = s.config.ssh[i];
        bool act = (i == s.config.keyboard.activeSsh);
        bool sel = !s.keyboardMenuMode && s.focusedContentIdx == i + 4;
        String left = String(act ? "* " : "  ") + p.name + "  (" + p.user + "@" + p.host + ":" + static_cast<unsigned>(p.port) + ")";
        String right = "PASS";
        if (p.keyPath.length()) {
            struct stat st{};
            right = (s.sdReady && stat(p.keyPath.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) ? "KEY" : "KEY?";
        }
        drawRowCard(s, top + static_cast<int>(r) * rowHeight(), sel, act, left, right);
    }
}

String maskSecret(const String& v) {
    String out;
    for (size_t i = 0; i < v.length(); ++i) out += '*';
    return out;
}

String getFieldValue(const AppState& s, uint8_t field) {
    if (s.screen == Screen::WifiEdit && s.editIndex < s.config.wifi.size()) {
        const auto& p = s.config.wifi[s.editIndex];
        if (field == 0) return p.name;
        if (field == 1) return p.ssid;
        return p.password;
    }
    if (s.screen == Screen::SshEdit && s.editIndex < s.config.ssh.size()) {
        const auto& p = s.config.ssh[s.editIndex];
        switch (field) {
            case 0: return p.name;
            case 1: return p.host;
            case 2: return String(static_cast<unsigned>(p.port));
            case 3: return p.user;
            case 4: return p.password;
            case 5: return p.keyPath;
            default: return p.terminal;
        }
    }
    if (s.screen == Screen::ConfigEdit) {
        switch (field) {
            case 0: return s.config.system.deviceName;
            case 1: return s.config.system.region;
            case 2: return String(s.config.system.utcOffsetMinutes);
            case 3: return s.config.system.ntpServer;
            case 4: return s.config.keyboard.layout;
            case 5: return s.config.keyboard.ctrlCapsSwap ? "on" : "off";
            case 6: return String(static_cast<unsigned>(s.config.keyboard.displayRotation * 90));
            default: return s.config.keyboard.darkTheme ? "console-dark" : "classic";
        }
    }
    return "";
}

void setFieldValue(AppState& s, uint8_t field, const String& val) {
    if (s.screen == Screen::WifiEdit && s.editIndex < s.config.wifi.size()) {
        auto& p = s.config.wifi[s.editIndex];
        if (field == 0) p.name = val;
        else if (field == 1) p.ssid = val;
        else p.password = val;
    } else if (s.screen == Screen::SshEdit && s.editIndex < s.config.ssh.size()) {
        auto& p = s.config.ssh[s.editIndex];
        if (field == 0) p.name = val;
        else if (field == 1) p.host = val;
        else if (field == 2) p.port = static_cast<uint16_t>(std::max(1L, std::min(65535L, val.toInt())));
        else if (field == 3) p.user = val;
        else if (field == 4) p.password = val;
        else if (field == 5) p.keyPath = val;
        else p.terminal = val.length() ? val : String("xterm-256color");
    } else if (s.screen == Screen::ConfigEdit) {
        if (field == 0) s.config.system.deviceName = val;
        else if (field == 1) s.config.system.region = val;
        else if (field == 2) s.config.system.utcOffsetMinutes = static_cast<int32_t>(std::max(-720L, std::min(840L, val.toInt())));
        else if (field == 3) s.config.system.ntpServer = val;
    }
}

uint8_t fieldCountForScreen(Screen sc) {
    if (sc == Screen::WifiEdit) return 3;
    if (sc == Screen::SshEdit) return 7;
    if (sc == Screen::ConfigEdit) return 8;
    return 0;
}

void drawEditScreen(AppState& s) {
    static const char* kWifiLabels[] = {"Name", "SSID", "Password"};
    static const char* kSshLabels[] = {"Name", "Host", "Port", "User", "Password", "Key Path", "Term"};
    static const char* kConfLabels[] = {"Device", "Region", "UTC min", "NTP", "Keymap", "SwapCaps", "Rotation", "Theme"};

    const char* title = s.screen == Screen::WifiEdit ? "EDIT WI-FI PROFILE" :
                        (s.screen == Screen::SshEdit ? "EDIT SSH PROFILE" : "CONFIGURATION");
    const char** labels = s.screen == Screen::WifiEdit ? kWifiLabels :
                          (s.screen == Screen::SshEdit ? kSshLabels : kConfLabels);
    uint8_t count = fieldCountForScreen(s.screen);
    drawSettingsHeaderAndButtons(s, title, "PREV", "NEXT", "SAVE", s.screen == Screen::ConfigEdit ? "DEFAULTS" : "DELETE");

    const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
    int top = listTop(s);
    int labelW = s.gfx.isPortrait() ? 130 : 170;
    for (uint8_t i = 0; i < count; ++i) {
        int y = top + static_cast<int>(i) * rowHeight();
        bool active = (i == s.editField);
        uint16_t bg = active ? pal.selectBg : pal.surface;
        uint16_t bd = active ? pal.selectBorder : pal.border;
        s.gfx.fillRoundRect(16, y, s.gfx.width() - 32, rowHeight() - 8, 5, bg);
        s.gfx.drawRoundRect(16, y, s.gfx.width() - 32, rowHeight() - 8, 5, bd);
        s.gfx.drawSettingsString(lgfx_font_japan_gothic_20, labels[i], 26, y + 11, pal.muted);

        bool secret = (s.screen == Screen::WifiEdit && i == 2) || (s.screen == Screen::SshEdit && i == 4);
        String val = secret ? maskSecret(getFieldValue(s, i)) : getFieldValue(s, i);
        if (active && s.cursorVisible && !(s.screen == Screen::ConfigEdit && i >= 4)) {
            val += "_";
        }
        s.gfx.drawSettingsString(lgfx_font_japan_gothic_20, val, 16 + labelW, y + 11, pal.text);
    }
}

void drawAll(AppState& s) {
    if (s.dirty) {
        if (s.screen == Screen::Terminal) {
            if (s.ssh.connected()) s.vt.markAllDirty();
            drawTerminal(s);
        } else if (s.screen == Screen::WifiList) drawWifiList(s);
        else if (s.screen == Screen::WifiScan) drawWifiScan(s);
        else if (s.screen == Screen::SshList) drawSshList(s);
        else drawEditScreen(s);
        drawHeader(s);
        s.dirty = false;
    } else if (s.headerDirty) {
        drawHeader(s);
    } else if (s.screen == Screen::Terminal) {
        drawTerminal(s);
    }
    s.gfx.flushDirty();
}

void connectActiveSsh(AppState& s);

void triggerReturnToConsole(AppState& s) {
    setBanner(s, "Returning to Console...");
    drawHeader(s);
    s.gfx.flushDirty();
    s.bridge.reset();
    s.ssh.disconnect();
    s.python.endSession();
    tab5_sd_unmount();
    esp_err_t err = tab5_app_boot_return_to_console();
    appendLocalLine(s, String("Console return failed: ") + esp_err_to_name(err));
}

void handleSshOsc(AppState& s, const String& osc) {
    if (!osc.startsWith("777;tab5-image;")) return;
    String pathHex;
    String mode = "fit";
    size_t start = strlen("777;tab5-image;");
    while (start <= osc.length()) {
        int sep = osc.indexOf(';', start);
        size_t end = sep < 0 ? osc.length() : static_cast<size_t>(sep);
        String part = osc.substring(start, end);
        int eq = part.indexOf('=');
        if (eq > 0) {
            String k = part.substring(0, static_cast<size_t>(eq));
            String v = part.substring(static_cast<size_t>(eq + 1));
            if (k == "pathhex") pathHex = v;
            else if (k == "mode") mode = v;
        }
        if (sep < 0) break;
        start = end + 1;
    }
    String remotePath;
    for (size_t i = 0; i + 1 < pathHex.length(); i += 2) {
        char buf[3] = {pathHex[i], pathHex[i + 1], '\0'};
        remotePath += static_cast<char>(strtol(buf, nullptr, 16));
    }
    if (!remotePath.length() || tab5_sd_mount() != ESP_OK) return;
    mkdir("/sdcard/.tab5-cache", 0755);
    String ts(static_cast<unsigned long>(nowMs()));
    String local = String("/sdcard/.tab5-cache/img_") + ts + ".bin";
    int dot = remotePath.lastIndexOf('.');
    if (dot >= 0) local = String("/sdcard/.tab5-cache/img_") + ts + remotePath.substring(static_cast<size_t>(dot));
    String err;
    if (s.ssh.scpDownloadFile(remotePath, local, err)) {
        if (!s.imageViewer.showFile(s.gfx, local, mode, 0, s.layout.headerH, s.gfx.width(),
                                    s.gfx.height() - s.layout.headerH,
                                    ThinClientGfx::palette(s.config.keyboard.darkTheme).bg, err)) {
            setBanner(s, err);
        }
    } else {
        setBanner(s, String("IMG SCP: ") + err);
    }
}

void connectActiveSsh(AppState& s) {
    if (s.config.ssh.empty()) {
        appendLocalLine(s, "No SSH profiles configured. Use SSH tab or /sdcard/private/tab5-terminal.json.");
        s.screen = Screen::SshList;
        s.dirty = true;
        return;
    }
    if (s.config.keyboard.activeSsh >= s.config.ssh.size()) s.config.keyboard.activeSsh = 0;
    const auto& prof = s.config.ssh[s.config.keyboard.activeSsh];

    s.imageViewer.clear();
    s.python.clearCanvasActive();
    s.bridge.reset();
    s.ssh.disconnect();
    s.screen = Screen::Terminal;
    appendLocalLine(s, String("Connecting SSH to ") + prof.user + "@" + prof.host + ":" + static_cast<unsigned>(prof.port) + "...");
    drawAll(s);

    String err;
    if (!s.ssh.connect(prof, static_cast<int>(s.vt.columns()), static_cast<int>(s.vt.rows()), err)) {
        appendLocalLine(s, String("SSH error: ") + err);
        if (err.startsWith("HOST KEY MISMATCH")) {
            appendLocalLine(s, String("\x1B[33mTip:\x1B[0m run \x1B[1;36mssh tofu clear ") + prof.host +
                               "\x1B[0m (or \x1B[1;36mssh sync --replace\x1B[0m) if host changed.");
        }
        setBanner(s, String("SSH fail: ") + prof.host);
        s.dirty = true;
        return;
    }

    s.vt.reset();
    s.vt.markAllDirty();
    setBanner(s, String("SSH: ") + prof.user + "@" + prof.host);
    if (s.ssh.lastHostFirstUse()) {
        appendLocalLine(s, String("TOFU host fingerprint trusted: ") + s.ssh.hostFingerprintHex());
    }

    String homeOut;
    String homeErr;
    if (s.ssh.execCommand("printf %s \"$HOME\"", homeOut, homeErr, 3000)) {
        homeOut.trim();
    }
    s.bridge.start(s.ssh, homeOut, [&](const String& m) { appendLocalLine(s, m); });
    s.dirty = true;
}

void saveCurrentSettings(AppState& s) {
    String err;
    settings_store::clampSelection(s.config);
    if (settings_store::saveConfig(s.config, err)) {
        setBanner(s, "Settings saved");
    } else {
        setBanner(s, err);
    }
}

void switchScreen(AppState& s, Screen next) {
    s.imageViewer.clear();
    s.python.clearCanvasActive();
    s.screen = next;
    s.listScroll = 0;
    s.focusedContentIdx = 0;
    s.dirty = true;
}

void activateContextButton(AppState& s) {
    switch (s.screen) {
        case Screen::Terminal:
            if (s.ssh.connected()) {
                s.bridge.reset();
                s.ssh.disconnect();
                setBanner(s, "SSH disconnected");
                s.dirty = true;
            } else {
                connectActiveSsh(s);
            }
            break;
        case Screen::WifiList:
            s.wifi.scan();
            switchScreen(s, Screen::WifiScan);
            break;
        case Screen::WifiScan:
            switchScreen(s, Screen::WifiList);
            break;
        case Screen::SshList:
            connectActiveSsh(s);
            break;
        case Screen::WifiEdit:
        case Screen::SshEdit:
        case Screen::ConfigEdit:
            saveCurrentSettings(s);
            configureTerminalGeometry(s);
            switchScreen(s, s.screen == Screen::WifiEdit ? Screen::WifiList :
                            (s.screen == Screen::SshEdit ? Screen::SshList : Screen::Terminal));
            break;
    }
}

void handleHeaderButtonTap(AppState& s, int idx) {
    s.keyboardMenuMode = false;
    if (idx == 0) switchScreen(s, Screen::Terminal);
    else if (idx == 1) switchScreen(s, Screen::WifiList);
    else if (idx == 2) { s.editField = 0; switchScreen(s, Screen::ConfigEdit); }
    else if (idx == 3) activateContextButton(s);
    else if (idx == 4) {
        s.config.keyboard.displayRotation = (s.config.keyboard.displayRotation + 1) & 3u;
        s.gfx.setRotation(s.config.keyboard.displayRotation);
        configureTerminalGeometry(s);
        saveCurrentSettings(s);
        s.dirty = true;
    } else if (idx == 5) {
        triggerReturnToConsole(s);
    }
}

void executeLocalLine(AppState& s, const String& line) {
    if (line.length()) {
        s.cmdHistory.push_back(line);
        s.historyNavIdx = -1;
    }
    CliContext cli{
        s.config, s.localTerm, s.wifi, s.ssh, s.python,
        s.sdCwd, s.cliPythonRepl, &s.sshSession,
        [&](const String& m) { appendLocalLine(s, m); },
        [&]() { s.localTerm.clear(); s.dirty = true; },
        [&]() { connectActiveSsh(s); },
        [&]() { s.bridge.reset(); s.ssh.disconnect(); setBanner(s, "SSH disconnected"); s.dirty = true; },
        [&](uint8_t rot) {
            s.config.keyboard.displayRotation = rot & 3u;
            s.gfx.setRotation(s.config.keyboard.displayRotation);
            configureTerminalGeometry(s);
            saveCurrentSettings(s);
            s.dirty = true;
        },
        [&](bool dark) {
            s.config.keyboard.darkTheme = dark;
            saveCurrentSettings(s);
            s.dirty = true;
        },
        [&]() { saveCurrentSettings(s); },
        [&](bool replace) {
            int n = settings_store::syncSdSshProfiles(s.config, replace);
            saveCurrentSettings(s);
            appendLocalLine(s, String("\x1B[1;32mSD SSH sync:\x1B[0m ") +
                               static_cast<unsigned int>(s.config.ssh.size()) + " profile(s) total (+" + n + " new)");
            s.dirty = true;
        },
        [&](const String& path, const String& mode, String& err) {
            const auto& pal = ThinClientGfx::palette(s.config.keyboard.darkTheme);
            return s.imageViewer.showFile(s.gfx, path, mode, 0, s.layout.headerH, s.gfx.width(),
                                          s.gfx.height() - s.layout.headerH, pal.bg, err);
        },
        [&]() { triggerReturnToConsole(s); },
        [&](const String& host, uint16_t port, uint32_t& latencyMs, String& info) {
            return SshClient::probeHost(host, port, 3000, latencyMs, info);
        },
        [&]() { return tab5_sd_mount() == ESP_OK; },
    };
    executeLocalCliCommand(cli, line);
    s.sdCwd = cli.sdCwd;
    s.cliPythonRepl = cli.pythonReplMode;
}

void processKeyAction(AppState& s, const KeyAction& act) {
    if (act.type == KeyActionType::None) return;

    if (s.imageViewer.active()) {
        if (act.type == KeyActionType::Menu || (act.type == KeyActionType::Text && (act.text == "q" || act.text == "\r"))) {
            s.imageViewer.clear();
            s.dirty = true;
        }
        return;
    }

    if (act.type == KeyActionType::Menu) {
        s.keyboardMenuMode = !s.keyboardMenuMode;
        s.headerDirty = true;
        return;
    }

    if (s.keyboardMenuMode) {
        if (act.text == "\x1B[D") {
            s.focusedHeaderIdx = (s.focusedHeaderIdx + kNumHeaderBtns - 1) % kNumHeaderBtns;
            s.headerDirty = true;
        } else if (act.text == "\x1B[C" || act.text == "\t") {
            s.focusedHeaderIdx = (s.focusedHeaderIdx + 1) % kNumHeaderBtns;
            s.headerDirty = true;
        } else if (act.text == "\r") {
            handleHeaderButtonTap(s, s.focusedHeaderIdx);
        }
        return;
    }

    if (act.type == KeyActionType::FontStep) {
        size_t count = ThinClientGfx::fontCount();
        size_t curIdx = 1;
        for (size_t i = 0; i < count; ++i) {
            if (static_cast<uint8_t>(ThinClientGfx::fontByIndex(i).id) == s.config.keyboard.terminalFont) {
                curIdx = i;
                break;
            }
        }
        size_t nextIdx = curIdx;
        if (act.value > 0 && curIdx + 1 < count) nextIdx = curIdx + 1;
        else if (act.value < 0 && curIdx > 0) nextIdx = curIdx - 1;
        if (nextIdx != curIdx) {
            const auto& f = ThinClientGfx::fontByIndex(nextIdx);
            s.config.keyboard.terminalFont = static_cast<uint8_t>(f.id);
            s.config.keyboard.terminalLineStep = static_cast<uint8_t>(f.defaultLineStep);
            configureTerminalGeometry(s);
            saveCurrentSettings(s);
            setBanner(s, String("Font: ") + f.label);
            s.dirty = true;
            s.headerDirty = true;
        }
        return;
    }

    if (act.type == KeyActionType::Scroll) {
        if (s.screen == Screen::Terminal) {
            if (s.ssh.connected()) {
                s.vt.scrollback(act.value);
                s.vt.markAllDirty();
            } else {
                s.localTerm.scroll(act.value);
            }
            s.dirty = true;
        }
        return;
    }

    if (s.screen == Screen::Terminal) {
        if (s.ssh.connected()) {
            if (act.text.length()) {
                s.vt.scrollbackToBottom();
                s.ssh.writeText(act.text);
            }
            return;
        }
        if (act.text == "\x03") {
            s.python.requestInterrupt();
            s.python.clearCanvasActive();
            if (s.sshSession.active) {
                s.sshSession.active = false;
                s.sshSession.step = SshMenuStep::Inactive;
            }
            s.commandLine.clear();
            s.commandCursor = 0;
            appendLocalLine(s, "^C");
            return;
        }
        if (act.text == "\r") {
            String entered = s.commandLine;
            bool maskEntered = s.sshSession.active && s.sshSession.step == SshMenuStep::AddPassword;
            appendLocalLine(s, coloredLocalPrompt(s) + (maskEntered ? String(std::string(entered.length(), '*')) : entered));
            s.commandLine.clear();
            s.commandCursor = 0;
            executeLocalLine(s, entered);
            s.dirty = true;
            return;
        }
        if (act.text == "\x7F" || act.text == "\b") {
            if (s.commandCursor > 0) {
                s.commandLine = s.commandLine.substring(0, s.commandCursor - 1) + s.commandLine.substring(s.commandCursor);
                --s.commandCursor;
                s.dirty = true;
            }
            return;
        }
        if (act.text == "\x1B[D") {
            if (s.commandCursor > 0) --s.commandCursor;
            s.dirty = true;
            return;
        }
        if (act.text == "\x1B[C") {
            if (s.commandCursor < s.commandLine.length()) ++s.commandCursor;
            s.dirty = true;
            return;
        }
        if (act.text == "\x1B[A" && !s.cmdHistory.empty()) {
            if (s.historyNavIdx < 0) s.historyNavIdx = static_cast<int>(s.cmdHistory.size()) - 1;
            else if (s.historyNavIdx > 0) --s.historyNavIdx;
            s.commandLine = s.cmdHistory[static_cast<size_t>(s.historyNavIdx)];
            s.commandCursor = s.commandLine.length();
            s.dirty = true;
            return;
        }
        if (act.text == "\x1B[B" && s.historyNavIdx >= 0) {
            ++s.historyNavIdx;
            if (s.historyNavIdx >= static_cast<int>(s.cmdHistory.size())) {
                s.historyNavIdx = -1;
                s.commandLine.clear();
            } else {
                s.commandLine = s.cmdHistory[static_cast<size_t>(s.historyNavIdx)];
            }
            s.commandCursor = s.commandLine.length();
            s.dirty = true;
            return;
        }
        if (act.text.length() == 1 && static_cast<uint8_t>(act.text[0]) >= 0x20) {
            s.commandLine = s.commandLine.substring(0, s.commandCursor) + act.text + s.commandLine.substring(s.commandCursor);
            ++s.commandCursor;
            s.localTerm.scrollToBottom();
            s.dirty = true;
        }
        return;
    }

    // Inline editor keyboard input on WifiEdit / SshEdit / ConfigEdit
    uint8_t count = fieldCountForScreen(s.screen);
    if (count > 0) {
        if (act.text == "\x1B[A") {
            s.editField = (s.editField + count - 1) % count;
            s.dirty = true;
        } else if (act.text == "\x1B[B" || act.text == "\t") {
            s.editField = (s.editField + 1) % count;
            s.dirty = true;
        } else if (s.screen == Screen::ConfigEdit && s.editField >= 4 && act.text == "\r") {
            if (s.editField == 4) s.config.keyboard.layout = (s.config.keyboard.layout == "jp") ? "us" : "jp";
            else if (s.editField == 5) s.config.keyboard.ctrlCapsSwap = !s.config.keyboard.ctrlCapsSwap;
            else if (s.editField == 6) {
                s.config.keyboard.displayRotation = (s.config.keyboard.displayRotation + 1) & 3u;
                s.gfx.setRotation(s.config.keyboard.displayRotation);
                configureTerminalGeometry(s);
            } else if (s.editField == 7) {
                s.config.keyboard.darkTheme = !s.config.keyboard.darkTheme;
            }
            saveCurrentSettings(s);
            s.dirty = true;
        } else if (act.text == "\x7F" || act.text == "\b") {
            String v = getFieldValue(s, s.editField);
            if (v.length()) {
                v = v.substring(0, v.length() - 1);
                setFieldValue(s, s.editField, v);
                s.dirty = true;
            }
        } else if (act.text == "\r") {
            saveCurrentSettings(s);
            switchScreen(s, s.screen == Screen::WifiEdit ? Screen::WifiList :
                            (s.screen == Screen::SshEdit ? Screen::SshList : Screen::Terminal));
        } else if (act.text.length() == 1 && static_cast<uint8_t>(act.text[0]) >= 0x20) {
            if (!(s.screen == Screen::ConfigEdit && s.editField >= 4)) {
                String v = getFieldValue(s, s.editField) + act.text;
                setFieldValue(s, s.editField, v);
                s.dirty = true;
            }
        }
    }
}

void handleTouchTap(AppState& s, int x, int y) {
    if (s.imageViewer.active()) {
        s.imageViewer.clear();
        s.dirty = true;
        return;
    }

    for (int i = 0; i < 3; ++i) {
        if (s.layout.tabs[i].containsPadded(x, y, 8)) {
            handleHeaderButtonTap(s, i);
            return;
        }
    }
    if (s.layout.ctxBtn.containsPadded(x, y, 8)) { handleHeaderButtonTap(s, 3); return; }
    if (s.layout.rotBtn.containsPadded(x, y, 8)) { handleHeaderButtonTap(s, 4); return; }
    if (s.layout.consoleBtn.containsPadded(x, y, 8)) { handleHeaderButtonTap(s, 5); return; }

    if (s.screen == Screen::Terminal) return;

    int top = listTop(s);
    if (s.screen == Screen::WifiList) {
        if (s.layout.bodyBtns[0].containsPadded(x, y, 4)) { s.wifi.scan(); switchScreen(s, Screen::WifiScan); return; }
        if (s.layout.bodyBtns[1].containsPadded(x, y, 4)) {
            s.config.wifi.push_back(WifiProfile{"New Wi-Fi", "", ""});
            s.editIndex = s.config.wifi.size() - 1;
            s.editField = 1;
            switchScreen(s, Screen::WifiEdit);
            return;
        }
        if (s.layout.bodyBtns[2].containsPadded(x, y, 4) && s.config.keyboard.activeWifi < s.config.wifi.size()) {
            const auto& p = s.config.wifi[s.config.keyboard.activeWifi];
            s.wifi.connect(p.ssid, p.password);
            switchScreen(s, Screen::Terminal);
            return;
        }
        if (s.layout.bodyBtns[3].containsPadded(x, y, 4)) { s.wifi.retry(); return; }
        if (y >= top) {
            size_t row = static_cast<size_t>((y - top) / rowHeight()) + s.listScroll;
            if (row < s.config.wifi.size()) {
                if (x > s.gfx.width() - 130) {
                    s.editIndex = row;
                    s.editField = 0;
                    switchScreen(s, Screen::WifiEdit);
                } else {
                    s.config.keyboard.activeWifi = static_cast<uint8_t>(row);
                    saveCurrentSettings(s);
                    s.dirty = true;
                }
            }
        }
        return;
    }

    if (s.screen == Screen::WifiScan) {
        if (s.layout.bodyBtns[0].containsPadded(x, y, 4)) { s.wifi.scan(); s.dirty = true; return; }
        if (s.layout.bodyBtns[1].containsPadded(x, y, 4)) { switchScreen(s, Screen::WifiList); return; }
        if (y >= top) {
            size_t row = static_cast<size_t>((y - top) / rowHeight()) + s.listScroll;
            const auto& nets = s.wifi.scannedNetworks();
            if (row < nets.size()) {
                WifiProfile p{nets[row].ssid, nets[row].ssid, ""};
                s.config.wifi.push_back(p);
                s.editIndex = s.config.wifi.size() - 1;
                s.editField = 2;
                switchScreen(s, Screen::WifiEdit);
            }
        }
        return;
    }

    if (s.screen == Screen::SshList) {
        if (s.layout.bodyBtns[0].containsPadded(x, y, 4)) {
            int n = settings_store::syncSdSshProfiles(s.config, false);
            saveCurrentSettings(s);
            setBanner(s, String("SD synced +") + n);
            s.dirty = true;
            return;
        }
        if (s.layout.bodyBtns[1].containsPadded(x, y, 4)) {
            s.config.ssh.push_back(SshProfile{"LAN Host", "", 22, "pi", "", "", "", "xterm-256color"});
            s.editIndex = s.config.ssh.size() - 1;
            s.editField = 1;
            switchScreen(s, Screen::SshEdit);
            return;
        }
        if (s.layout.bodyBtns[2].containsPadded(x, y, 4)) { connectActiveSsh(s); return; }
        if (s.layout.bodyBtns[3].containsPadded(x, y, 4) && s.config.keyboard.activeSsh < s.config.ssh.size()) {
            s.editIndex = s.config.keyboard.activeSsh;
            s.editField = 0;
            switchScreen(s, Screen::SshEdit);
            return;
        }
        if (y >= top) {
            size_t row = static_cast<size_t>((y - top) / rowHeight()) + s.listScroll;
            if (row < s.config.ssh.size()) {
                s.config.keyboard.activeSsh = static_cast<uint8_t>(row);
                saveCurrentSettings(s);
                if (x > s.gfx.width() - 150) {
                    s.editIndex = row;
                    s.editField = 0;
                    switchScreen(s, Screen::SshEdit);
                } else {
                    s.dirty = true;
                }
            }
        }
        return;
    }

    // WifiEdit / SshEdit / ConfigEdit
    uint8_t fcount = fieldCountForScreen(s.screen);
    if (s.layout.bodyBtns[0].containsPadded(x, y, 4)) { s.editField = (s.editField + fcount - 1) % fcount; s.dirty = true; return; }
    if (s.layout.bodyBtns[1].containsPadded(x, y, 4)) { s.editField = (s.editField + 1) % fcount; s.dirty = true; return; }
    if (s.layout.bodyBtns[2].containsPadded(x, y, 4)) { activateContextButton(s); return; }
    if (s.layout.bodyBtns[3].containsPadded(x, y, 4)) {
        if (s.screen == Screen::WifiEdit && s.editIndex < s.config.wifi.size()) {
            s.config.wifi.erase(s.config.wifi.begin() + static_cast<long>(s.editIndex));
            settings_store::clampSelection(s.config);
            saveCurrentSettings(s);
            switchScreen(s, Screen::WifiList);
        } else if (s.screen == Screen::SshEdit && s.editIndex < s.config.ssh.size()) {
            s.config.ssh.erase(s.config.ssh.begin() + static_cast<long>(s.editIndex));
            settings_store::clampSelection(s.config);
            saveCurrentSettings(s);
            switchScreen(s, Screen::SshList);
        } else if (s.screen == Screen::ConfigEdit) {
            s.config.keyboard.layout = "us";
            s.config.keyboard.ctrlCapsSwap = false;
            s.config.keyboard.darkTheme = true;
            saveCurrentSettings(s);
            configureTerminalGeometry(s);
            s.dirty = true;
        }
        return;
    }
    if (y >= top) {
        size_t row = static_cast<size_t>((y - top) / rowHeight());
        if (row < fcount) {
            s.editField = static_cast<uint8_t>(row);
            if (s.screen == Screen::ConfigEdit && row >= 4) {
                KeyAction enter{KeyActionType::Text, "\r", 0};
                processKeyAction(s, enter);
            }
            s.dirty = true;
        }
    }
}

void pollTouch(AppState& s) {
    int tx = 0;
    int ty = 0;
    bool down = tab5_touch_poll(&tx, &ty);
    if (down && !s.touchWasDown) {
        s.touchWasDown = true;
        s.touchStartX = tx;
        s.touchStartY = ty;
        s.touchLastY = ty;
        s.touchDragged = false;
    } else if (down && s.touchWasDown) {
        const bool startedInHeader = s.touchStartY <= (s.layout.headerH + 10);
        int dy = ty - s.touchLastY;
        if (!startedInHeader && std::abs(ty - s.touchStartY) > 18) s.touchDragged = true;
        if (!startedInHeader && s.screen == Screen::Terminal && std::abs(dy) >= 24) {
            int scrollLines = dy > 0 ? 3 : -3;
            if (s.ssh.connected()) {
                s.vt.scrollback(scrollLines);
                s.vt.markAllDirty();
            } else {
                s.localTerm.scroll(scrollLines);
            }
            s.touchLastY = ty;
            s.dirty = true;
        }
    } else if (!down && s.touchWasDown) {
        s.touchWasDown = false;
        if (!s.touchDragged) {
            handleTouchTap(s, s.touchStartX, s.touchStartY);
        }
    }
}

}  // namespace

extern "C" void app_main(void) {
    nvs_flash_init();
    tab5_board_status board = tab5_board_init();
    g_app = new AppState();
    auto& app = *g_app;

    app.sdReady = (tab5_sd_mount() == ESP_OK);
    String cfgSource;
    settings_store::loadConfig(app.config, cfgSource);
    const uint32_t prevSyncRev = app.config.keyboard.sdSyncRevision;
    int sdImported = app.sdReady ? settings_store::syncSdSshProfiles(app.config, false) : 0;
    if (sdImported > 0 || app.config.keyboard.sdSyncRevision != prevSyncRev) {
        String saveErr;
        settings_store::saveConfig(app.config, saveErr);
    }

    app.gfx.init(app.config.keyboard.displayRotation);
    app.layout = computeLayout(app);
    configureTerminalGeometry(app);

    app.python.setGfxTarget(&app.gfx);
    app.python.setOutputCallback([](const char* data, size_t len, void* ctx) {
        auto* st = static_cast<AppState*>(ctx);
        st->localTerm.append(data, len);
        st->localTerm.scrollToBottom();
        if (!st->python.canvasActive()) {
            st->dirty = true;
            drawAll(*st);
        }
    }, &app);

    appendLocalLine(app, "\x1B[1;36mTab5ThinClient v0.1.3-dev\x1B[0m \x1B[90m(Console PixelFonts Chrome + Terminus/Gothic)\x1B[0m");
    appendLocalLine(app, String("\x1B[36mConfig:\x1B[0m ") + cfgSource +
                         (app.sdReady ? (String(" | \x1B[1;32mSD mounted\x1B[0m (+") + sdImported + " SSH profile(s))")
                                      : String(" | \x1B[33mSD absent\x1B[0m")));
    appendLocalLine(app, "Type \x1B[1;32mhelp\x1B[0m, \x1B[1;32mssh\x1B[0m (or \x1B[33mssh <user>@<host>\x1B[0m / \x1B[33mssh clear\x1B[0m), or \x1B[1;32mpython /demos/hat.py\x1B[0m.");

    drawAll(app);
    tab5_app_boot_ready(board.display_ready && board.touch_ready);

    g_keyQueue = xQueueCreate(32, sizeof(RawKeyEvent));
    tab5_keyboard_set_key_filter(usbKeyFilter, nullptr);
    tab5::start_tab5_kbd_input([](const uint8_t*, size_t) {}, {}, tab5KeyFilter);

    app.wifi.start();

    for (;;) {
        app.wifi.poll();
        startSntpIfNeeded(app);

        if (app.ssh.connected()) {
            uint8_t rx[4096];
            for (int burst = 0; burst < 4; ++burst) {
                int n = app.ssh.read(rx, sizeof(rx));
                if (n > 0) {
                    app.vt.write(reinterpret_cast<const char*>(rx), static_cast<size_t>(n));
                    while (app.vt.hasOscMessage()) {
                        handleSshOsc(app, app.vt.popOscMessage());
                    }
                    while (app.vt.hasReply()) {
                        app.ssh.writeText(app.vt.popReply());
                    }
                } else {
                    if (n < 0) {
                        app.bridge.reset();
                        String reason = app.ssh.lastError();
                        setBanner(app, reason.length() ? (String("SSH closed: ") + reason) : String("SSH closed"));
                        appendLocalLine(app, reason.length()
                                                 ? (String("SSH session closed (") + reason + ").")
                                                 : String("SSH session closed."));
                        app.dirty = true;
                    }
                    break;
                }
            }
            if (app.ssh.connected()) {
                app.bridge.poll(app.ssh);
            }
        }

        RawKeyEvent ev{};
        while (g_keyQueue && xQueueReceive(g_keyQueue, &ev, 0) == pdTRUE) {
            KeyAction act = app.kbd.mapHid(ev.modifiers, ev.usage);
            processKeyAction(app, act);
        }

        pollTouch(app);

        uint32_t now = nowMs();
        if (now - app.lastBlinkMs >= 500) {
            app.lastBlinkMs = now;
            app.cursorVisible = !app.cursorVisible;
            if (app.screen == Screen::Terminal && !app.imageViewer.active() && !app.python.canvasActive()) {
                drawTerminal(app);
            }
        }

        time_t tnow = time(nullptr);
        uint32_t curMin = static_cast<uint32_t>(tnow / 60);
        if (curMin != app.lastClockMinute) {
            app.lastClockMinute = curMin;
            app.headerDirty = true;
        }

        if (app.dirty || app.headerDirty || app.gfx.hasDirty()) {
            drawAll(app);
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
