#pragma once

#include <cstdint>
#include <vector>
#include "arduino_compat.hpp"
#include "thinclient_gfx.hpp"

class PythonRunner {
public:
    using OutputCallback = void (*)(const char* data, size_t len, void* ctx);

    void setCanvasViewport(int x, int y, int w, int h, bool darkTheme);
    void setGfxTarget(ThinClientGfx* gfx);
    void setOutputCallback(OutputCallback cb, void* ctx);

    bool beginSession();
    void endSession();
    bool sessionActive() const { return vmReady_; }

    void requestInterrupt();
    bool interruptRequested() const { return interruptFlag_; }

    bool execInteractiveLine(const String& line, bool& needsMoreInput);
    void resetRepl();
    bool waitingContinuation() const { return !pendingBlock_.isEmpty(); }

    bool runScriptText(const String& source, const std::vector<String>& argv, String& error);
    bool runScriptFile(const String& fullPath, const std::vector<String>& argv, String& error);

    bool canvasActive() const { return canvasActive_; }
    void clearCanvasActive() { canvasActive_ = false; }
    int viewportWidth() const { return viewW_; }
    int viewportHeight() const { return viewH_; }

    // Invoked internally from MicroPython stdout/graphics hooks
    void handleStdout(const char* data, size_t len);
    void handleGfxOp(const char* op, int a, int b, int c, int d, int e, uint32_t col1, uint32_t col2, const char* text, size_t textLen);

private:
    bool vmReady_{false};
    bool canvasActive_{false};
    volatile bool interruptFlag_{false};
    uint8_t* heap_{nullptr};
    String pendingBlock_;

    ThinClientGfx* gfx_{nullptr};
    int viewX_{0};
    int viewY_{60};
    int viewW_{1280};
    int viewH_{660};
    bool darkTheme_{true};

    OutputCallback outCb_{nullptr};
    void* outCtx_{nullptr};

    bool ensureVm();
    void registerGfxAndPrelude();
    void setArgv(const std::vector<String>& argv);
    void emitText(const char* s);
};
