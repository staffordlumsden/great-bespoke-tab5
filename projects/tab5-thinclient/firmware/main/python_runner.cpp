#include "python_runner.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tab5_gpio_bridge.h"

extern "C" {
#include "port/micropython_embed.h"
#include "port/micropython_host_io.h"
#include "py/mpstate.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
}

namespace {

constexpr size_t kPythonHeapBytes = 256u * 1024u;
PythonRunner* s_activeRunner = nullptr;

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

uint16_t colorFrom24Bit(mp_int_t rgb) {
    uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return ThinClientGfx::rgb565(r, g, b);
}

void checkInterruptOrRaise() {
    if (s_activeRunner && s_activeRunner->interruptRequested()) {
        mp_raise_msg(&mp_type_KeyboardInterrupt, MP_ERROR_TEXT("interrupted"));
    }
}

// MicroPython C primitives for gfx and timing (stored via runtime QSTRs).
static mp_obj_t py_gfx_size(void) {
    int w = s_activeRunner ? s_activeRunner->viewportWidth() : 1280;
    int h = s_activeRunner ? s_activeRunner->viewportHeight() : 660;
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(w),
        MP_OBJ_NEW_SMALL_INT(h),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_gfx_size_obj, py_gfx_size);

static mp_obj_t py_gfx_clear(mp_obj_t col_in) {
    checkInterruptOrRaise();
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("clear", 0, 0, 0, 0, 0, static_cast<uint32_t>(mp_obj_get_int(col_in)), 0, nullptr, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_gfx_clear_obj, py_gfx_clear);

static mp_obj_t py_gfx_present(void) {
    checkInterruptOrRaise();
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("present", 0, 0, 0, 0, 0, 0, 0, nullptr, 0);
    }
    vTaskDelay(1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_gfx_present_obj, py_gfx_present);

static mp_obj_t py_gfx_pixel(mp_obj_t x_in, mp_obj_t y_in, mp_obj_t col_in) {
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("pixel", mp_obj_get_int(x_in), mp_obj_get_int(y_in),
                                    0, 0, 0, static_cast<uint32_t>(mp_obj_get_int(col_in)), 0, nullptr, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_gfx_pixel_obj, py_gfx_pixel);

static mp_obj_t py_gfx_line(size_t n_args, const mp_obj_t* args) {
    (void)n_args;
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("line",
                                    mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                                    mp_obj_get_int(args[2]), mp_obj_get_int(args[3]),
                                    0, static_cast<uint32_t>(mp_obj_get_int(args[4])), 0, nullptr, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_gfx_line_obj, 5, 5, py_gfx_line);

static mp_obj_t py_gfx_rect(size_t n_args, const mp_obj_t* args) {
    int fill = (n_args >= 6) ? (mp_obj_is_true(args[5]) ? 1 : 0) : 1;
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("rect",
                                    mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                                    mp_obj_get_int(args[2]), mp_obj_get_int(args[3]),
                                    fill, static_cast<uint32_t>(mp_obj_get_int(args[4])), 0, nullptr, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_gfx_rect_obj, 5, 6, py_gfx_rect);

static mp_obj_t py_gfx_circle(size_t n_args, const mp_obj_t* args) {
    int fill = (n_args >= 5) ? (mp_obj_is_true(args[4]) ? 1 : 0) : 1;
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("circle",
                                    mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                                    mp_obj_get_int(args[2]), 0,
                                    fill, static_cast<uint32_t>(mp_obj_get_int(args[3])), 0, nullptr, 0);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_gfx_circle_obj, 4, 5, py_gfx_circle);

static mp_obj_t py_gfx_text(size_t n_args, const mp_obj_t* args) {
    (void)n_args;
    size_t len = 0;
    const char* s = mp_obj_str_get_data(args[2], &len);
    uint32_t col = n_args >= 4 ? static_cast<uint32_t>(mp_obj_get_int(args[3])) : 0xFFFFFFu;
    int scale = n_args >= 5 ? mp_obj_get_int(args[4]) : 1;
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("text", mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                                    scale, 0, 0, col, 0, s, len);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_gfx_text_obj, 3, 5, py_gfx_text);

static mp_obj_t py_gfx_mono(size_t n_args, const mp_obj_t* args) {
    (void)n_args;
    size_t len = 0;
    const char* bits = mp_obj_str_get_data(args[7], &len);
    if (s_activeRunner) {
        s_activeRunner->handleGfxOp("mono",
                                    mp_obj_get_int(args[0]), mp_obj_get_int(args[1]),
                                    mp_obj_get_int(args[2]), mp_obj_get_int(args[3]),
                                    mp_obj_get_int(args[4]),
                                    static_cast<uint32_t>(mp_obj_get_int(args[5])),
                                    static_cast<uint32_t>(mp_obj_get_int(args[6])),
                                    bits, len);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_gfx_mono_obj, 8, 8, py_gfx_mono);

static mp_obj_t py_sleep_ms(mp_obj_t ms_in) {
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 0) ms = 0;
    mp_int_t elapsed = 0;
    while (elapsed < ms) {
        checkInterruptOrRaise();
        mp_int_t slice = std::min<mp_int_t>(20, ms - elapsed);
        vTaskDelay(pdMS_TO_TICKS(slice));
        elapsed += slice;
    }
    checkInterruptOrRaise();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sleep_ms_obj, py_sleep_ms);

static mp_obj_t py_ticks_ms(void) {
    return mp_obj_new_int_from_ll(static_cast<long long>(esp_timer_get_time() / 1000LL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_ticks_ms_obj, py_ticks_ms);

const char* kPythonPrelude = R"PY(
import sys

class _Gfx:
    def size(self):
        return _tab5_gfx_size()
    def clear(self, color=0):
        _tab5_gfx_clear(int(color))
    def present(self):
        _tab5_gfx_present()
    def pixel(self, x, y, color=0xffffff):
        _tab5_gfx_pixel(int(x), int(y), int(color))
    def line(self, x0, y0, x1, y1, color=0xffffff):
        _tab5_gfx_line(int(x0), int(y0), int(x1), int(y1), int(color))
    def rect(self, x, y, w, h, color=0xffffff, fill=1):
        _tab5_gfx_rect(int(x), int(y), int(w), int(h), int(color), 1 if fill else 0)
    def fill_rect(self, x, y, w, h, color=0xffffff):
        _tab5_gfx_rect(int(x), int(y), int(w), int(h), int(color), 1)
    def circle(self, x, y, r, color=0xffffff, fill=1):
        _tab5_gfx_circle(int(x), int(y), int(r), int(color), 1 if fill else 0)
    def fill_circle(self, x, y, r, color=0xffffff):
        _tab5_gfx_circle(int(x), int(y), int(r), int(color), 1)
    def text(self, x, y, s, color=0xffffff, scale=1):
        _tab5_gfx_text(int(x), int(y), str(s), int(color), int(scale))
    def mono(self, x, y, cols, rows, cell, fg, bg, bits):
        _tab5_gfx_mono(int(x), int(y), int(cols), int(rows), int(cell), int(fg), int(bg), str(bits))

gfx = _Gfx()

class _TimeMod:
    def sleep_ms(self, ms):
        _tab5_sleep_ms(int(ms))
    def sleep(self, sec):
        _tab5_sleep_ms(int(float(sec) * 1000))
    def ticks_ms(self):
        return _tab5_ticks_ms()
    def ticks_diff(self, a, b):
        return int(a) - int(b)

sys.modules["time"] = _TimeMod()
sys.modules["gfx"] = gfx

def pins():
    return [int(x) for x in _tab5_pin_list().split(",") if x]

class Pin:
    IN = 0
    OUT = 1
    IN_PULLUP = 2
    IN_PULLDOWN = 3
    PULL_UP = 2
    PULL_DOWN = 3
    def __init__(self, pin, mode=0, pull=None):
        self._pin = int(pin)
        m = int(mode)
        if pull == Pin.PULL_UP:
            m = Pin.IN_PULLUP
        elif pull == Pin.PULL_DOWN:
            m = Pin.IN_PULLDOWN
        self._val = 0
        _tab5_pin_mode(self._pin, m)
    def value(self, v=None):
        if v is None:
            return _tab5_pin_read(self._pin)
        self._val = 1 if v else 0
        _tab5_pin_write(self._pin, self._val)
        return self._val
    def on(self):
        self.value(1)
    def off(self):
        self.value(0)
    def toggle(self):
        self.value(0 if self._val else 1)
    def pwm(self, duty, freq=1000):
        _tab5_pwm_write(self._pin, int(duty), int(freq))
    def release(self):
        _tab5_pwm_release(self._pin)

class ADC:
    def __init__(self, pin):
        self._pin = int(pin)
        _ = _tab5_adc_read(self._pin)
    def read(self):
        return _tab5_adc_read(self._pin)
    def read_mv(self):
        return _tab5_adc_mv(self._pin)

class PWM:
    def __init__(self, pin, freq=1000, duty=0):
        self._pin = int(pin)
        self._freq = int(freq)
        _tab5_pwm_write(self._pin, int(duty), self._freq)
    def duty(self, d):
        _tab5_pwm_write(self._pin, int(d), self._freq)
    def freq(self, f):
        self._freq = int(f)
    def deinit(self):
        _tab5_pwm_release(self._pin)

class I2C:
    def __init__(self, sda=53, scl=54, freq=100000):
        _tab5_i2c_init(int(sda), int(scl), int(freq))
    def deinit(self):
        _tab5_i2c_deinit()
    def scan(self):
        return _tab5_i2c_scan()
    def write(self, addr, data, stop=True):
        return _tab5_i2c_write(int(addr), data, bool(stop))
    def read(self, addr, n):
        return _tab5_i2c_read(int(addr), int(n))
    def write_read(self, addr, tx, n):
        return _tab5_i2c_write_read(int(addr), tx, int(n))

class SPI:
    def __init__(self, sck, mosi, miso=-1, freq=1000000, mode=0):
        _tab5_spi_init(int(sck), int(miso), int(mosi), int(freq), int(mode))
    def deinit(self):
        _tab5_spi_deinit()
    def transfer(self, cs, tx):
        return _tab5_spi_transfer(int(cs), tx)

class UART:
    def __init__(self, port=1, baud=115200, rx=-1, tx=-1):
        self._port = int(port)
        _tab5_uart_init(self._port, int(baud), int(rx), int(tx))
    def deinit(self):
        _tab5_uart_deinit(self._port)
    def write(self, data):
        return _tab5_uart_write(self._port, data)
    def read(self, n, timeout_ms=100):
        return _tab5_uart_read(self._port, int(n), int(timeout_ms))
    def any(self):
        return _tab5_uart_any(self._port)

argv = []
sys.argv = argv
)PY";

}  // namespace

extern "C" void micropython_host_stdout(const char* str, size_t len) {
    if (s_activeRunner && str && len > 0) {
        s_activeRunner->handleStdout(str, len);
    }
}

void PythonRunner::setCanvasViewport(int x, int y, int w, int h, bool darkTheme) {
    viewX_ = x;
    viewY_ = y;
    viewW_ = std::max(16, w);
    viewH_ = std::max(16, h);
    darkTheme_ = darkTheme;
}

void PythonRunner::setGfxTarget(ThinClientGfx* gfx) {
    gfx_ = gfx;
}

void PythonRunner::setOutputCallback(OutputCallback cb, void* ctx) {
    outCb_ = cb;
    outCtx_ = ctx;
}

void PythonRunner::requestInterrupt() {
    interruptFlag_ = true;
}

void PythonRunner::handleStdout(const char* data, size_t len) {
    if (outCb_) {
        outCb_(data, len, outCtx_);
    }
}

void PythonRunner::emitText(const char* s) {
    if (s && *s && outCb_) {
        outCb_(s, strlen(s), outCtx_);
    }
}

void PythonRunner::handleGfxOp(const char* op, int a, int b, int c, int d, int e,
                               uint32_t col1, uint32_t col2, const char* text, size_t textLen) {
    if (!op || !gfx_) return;

    canvasActive_ = true;
    gfx_->setClipRect(viewX_, viewY_, viewW_, viewH_);
    const int ox = viewX_;
    const int oy = viewY_;

    if (strcmp(op, "clear") == 0) {
        gfx_->fillRect(viewX_, viewY_, viewW_, viewH_, colorFrom24Bit(col1));
    } else if (strcmp(op, "present") == 0) {
        gfx_->clearClipRect();
        gfx_->flushDirty();
        return;
    } else if (strcmp(op, "pixel") == 0) {
        gfx_->drawPixel(ox + a, oy + b, colorFrom24Bit(col1));
    } else if (strcmp(op, "line") == 0) {
        gfx_->drawLine(ox + a, oy + b, ox + c, oy + d, colorFrom24Bit(col1));
    } else if (strcmp(op, "rect") == 0) {
        uint16_t color = colorFrom24Bit(col1);
        if (e) gfx_->fillRect(ox + a, oy + b, c, d, color);
        else gfx_->drawRect(ox + a, oy + b, c, d, color);
    } else if (strcmp(op, "circle") == 0) {
        uint16_t color = colorFrom24Bit(col1);
        if (e) gfx_->fillCircle(ox + a, oy + b, c, color);
        else gfx_->drawCircle(ox + a, oy + b, c, color);
    } else if (strcmp(op, "text") == 0 && text && textLen > 0) {
        uint16_t color = colorFrom24Bit(col1);
        int scale = std::max(1, std::min(4, c));
        int cx = ox + a;
        int cy = oy + b;
        for (size_t i = 0; i < textLen; ++i) {
            uint8_t ch = static_cast<uint8_t>(text[i]);
            gfx_->drawAscii8x16Char(ch, cx, cy, color, scale, scale);
            cx += 8 * scale;
        }
    } else if (strcmp(op, "mono") == 0 && text && textLen > 0) {
        int cols = c;
        int rows = d;
        int cell = std::max(1, e);
        uint16_t fg = colorFrom24Bit(col1);
        uint16_t bg = colorFrom24Bit(col2);
        size_t totalCells = static_cast<size_t>(std::max(0, cols)) * static_cast<size_t>(std::max(0, rows));
        for (size_t idx = 0; idx < totalCells; ++idx) {
            size_t hexIdx = idx / 4;
            if (hexIdx >= textLen) break;
            int nib = hexNibble(text[hexIdx]);
            int bit = 3 - static_cast<int>(idx % 4);
            bool on = (nib & (1 << bit)) != 0;
            int px = ox + a + static_cast<int>(idx % cols) * cell;
            int py = oy + b + static_cast<int>(idx / cols) * cell;
            gfx_->fillRect(px, py, cell, cell, on ? fg : bg);
        }
    }
    gfx_->clearClipRect();
}

void PythonRunner::registerGfxAndPrelude() {
    tab5_gpio_register_globals();
    auto store = [](const char* name, const void* obj) {
        mp_store_global(qstr_from_str(name), MP_OBJ_FROM_PTR(obj));
    };
    store("_tab5_gfx_size", &py_gfx_size_obj);
    store("_tab5_gfx_clear", &py_gfx_clear_obj);
    store("_tab5_gfx_present", &py_gfx_present_obj);
    store("_tab5_gfx_pixel", &py_gfx_pixel_obj);
    store("_tab5_gfx_line", &py_gfx_line_obj);
    store("_tab5_gfx_rect", &py_gfx_rect_obj);
    store("_tab5_gfx_circle", &py_gfx_circle_obj);
    store("_tab5_gfx_text", &py_gfx_text_obj);
    store("_tab5_gfx_mono", &py_gfx_mono_obj);
    store("_tab5_sleep_ms", &py_sleep_ms_obj);
    store("_tab5_ticks_ms", &py_ticks_ms_obj);

    mp_embed_exec_str(kPythonPrelude);
}

bool PythonRunner::ensureVm() {
    if (vmReady_) return true;
    if (!heap_) {
        heap_ = static_cast<uint8_t*>(heap_caps_malloc(kPythonHeapBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!heap_) {
            heap_ = static_cast<uint8_t*>(malloc(kPythonHeapBytes));
        }
        if (!heap_) {
            return false;
        }
    }
    uint8_t stackAnchor = 0;
    s_activeRunner = this;
    mp_embed_init(heap_, kPythonHeapBytes, &stackAnchor);
    vmReady_ = true;
    registerGfxAndPrelude();
    return true;
}

bool PythonRunner::beginSession() {
    interruptFlag_ = false;
    pendingBlock_.clear();
    return ensureVm();
}

void PythonRunner::endSession() {
    if (vmReady_) {
        mp_embed_deinit();
        vmReady_ = false;
    }
    if (heap_) {
        free(heap_);
        heap_ = nullptr;
    }
    pendingBlock_.clear();
    canvasActive_ = false;
    s_activeRunner = nullptr;
}

void PythonRunner::resetRepl() {
    pendingBlock_.clear();
}

void PythonRunner::setArgv(const std::vector<String>& argv) {
    String py = "argv = [";
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) py += ", ";
        py += '"';
        for (size_t k = 0; k < argv[i].length(); ++k) {
            char c = argv[i][k];
            if (c == '\\' || c == '"') py += '\\';
            py += c;
        }
        py += '"';
    }
    py += "]\nsys.argv = argv\n";
    mp_embed_exec_str(py.c_str());
}

bool PythonRunner::execInteractiveLine(const String& line, bool& needsMoreInput) {
    needsMoreInput = false;
    interruptFlag_ = false;
    if (!ensureVm()) {
        emitText("python: failed to initialize VM\n");
        return false;
    }
    s_activeRunner = this;
    String trimmed = line;
    trimmed.trim();

    if (pendingBlock_.length()) {
        if (!trimmed.length()) {
            String block = pendingBlock_;
            pendingBlock_.clear();
            return mp_embed_exec_str_status(block.c_str()) == 0;
        }
        pendingBlock_ += line;
        pendingBlock_ += "\n";
        needsMoreInput = true;
        return true;
    }

    if (trimmed.endsWith(":")) {
        pendingBlock_ = line + "\n";
        needsMoreInput = true;
        return true;
    }

    if (!trimmed.length()) return true;
    int status = mp_embed_exec_str_status(line.c_str());
    return status == 0;
}

bool PythonRunner::runScriptText(const String& source, const std::vector<String>& argv, String& error) {
    interruptFlag_ = false;
    canvasActive_ = false;
    if (!ensureVm()) {
        error = "failed to allocate MicroPython heap";
        return false;
    }
    s_activeRunner = this;
    setArgv(argv);
    int rc = mp_embed_exec_str_status(source.c_str());
    if (rc != 0) {
        error = interruptFlag_ ? "interrupted" : "python script error";
        return false;
    }
    return true;
}

bool PythonRunner::runScriptFile(const String& fullPath, const std::vector<String>& argv, String& error) {
    FILE* fp = fopen(fullPath.c_str(), "rb");
    if (!fp) {
        error = String("cannot open ") + fullPath;
        return false;
    }
    std::string src;
    char buf[1024];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        src.append(buf, n);
        if (src.size() > 512u * 1024u) break;
    }
    fclose(fp);
    return runScriptText(String(std::move(src)), argv, error);
}
