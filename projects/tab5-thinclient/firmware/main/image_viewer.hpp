#pragma once

#include <cstdint>
#include "arduino_compat.hpp"
#include "thinclient_gfx.hpp"

class ImageViewer {
public:
    ~ImageViewer();

    void clear();
    bool showFile(ThinClientGfx& gfx, const String& sdFullPath, const String& mode,
                  int areaX, int areaY, int areaW, int areaH,
                  uint16_t backdropColor, String& error);
    bool active() const { return active_; }
    const String& currentPath() const { return currentPath_; }
    const String& currentMode() const { return currentMode_; }

private:
    bool active_{false};
    String currentPath_;
    String currentMode_;
    uint16_t* pixels_{nullptr};
    uint16_t imgW_{0};
    uint16_t imgH_{0};
    int lastAreaW_{0};
    int lastAreaH_{0};
};
