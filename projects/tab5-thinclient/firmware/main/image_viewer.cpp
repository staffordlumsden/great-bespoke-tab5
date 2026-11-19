#include "image_viewer.hpp"

#include <algorithm>
#include <cstdio>

#include "esp_heap_caps.h"
#include "imgf_alloc.h"
#include "imgf_bmpd.h"
#include "imgf_decoder.h"
#include "imgf_jpegd.h"
#include "imgf_pngd.h"
#include "imgf_resize.h"
#include "imgf_stream.h"

namespace {

String lowerExtension(const String& path) {
    int dot = path.lastIndexOf('.');
    if (dot < 0) return "";
    String ext = path.substring(static_cast<size_t>(dot + 1));
    ext.toLowerCase();
    return ext;
}

void computeTargetBox(const String& mode, int areaW, int areaH, int& maxW, int& maxH) {
    if (mode == "half") {
        maxW = std::max(16, areaW / 2);
        maxH = std::max(16, areaH / 2);
    } else if (mode == "quarter") {
        maxW = std::max(16, areaW / 4);
        maxH = std::max(16, areaH / 4);
    } else {
        // Both "fit" and "center" bound within the content area while preserving aspect ratio.
        maxW = std::max(16, areaW - 16);
        maxH = std::max(16, areaH - 16);
    }
}

}  // namespace

ImageViewer::~ImageViewer() {
    clear();
}

void ImageViewer::clear() {
    if (pixels_) {
        imgf_free(pixels_);
        pixels_ = nullptr;
    }
    imgW_ = 0;
    imgH_ = 0;
    lastAreaW_ = 0;
    lastAreaH_ = 0;
    currentPath_.clear();
    currentMode_.clear();
    active_ = false;
}

bool ImageViewer::showFile(ThinClientGfx& gfx, const String& sdFullPath, const String& mode,
                           int areaX, int areaY, int areaW, int areaH,
                           uint16_t backdropColor, String& error) {
    if (areaW <= 0 || areaH <= 0) {
        error = "invalid image viewport";
        return false;
    }
    String normMode = mode.length() ? mode : String("fit");
    normMode.toLowerCase();
    if (normMode != "fit" && normMode != "center" && normMode != "half" && normMode != "quarter") {
        normMode = "fit";
    }

    bool needDecode = !pixels_ || currentPath_ != sdFullPath || currentMode_ != normMode ||
                      lastAreaW_ != areaW || lastAreaH_ != areaH;

    if (needDecode) {
        if (pixels_) {
            imgf_free(pixels_);
            pixels_ = nullptr;
        }
        String ext = lowerExtension(sdFullPath);
        imgf_decoder_t* dec = nullptr;
        if (ext == "jpg" || ext == "jpeg") dec = imgf_jpegd_create();
        else if (ext == "png") dec = imgf_pngd_create();
        else if (ext == "bmp") dec = imgf_bmpd_create();
        else {
            error = "unsupported image extension (need .jpg/.jpeg/.png/.bmp)";
            return false;
        }
        if (!dec) {
            error = "decoder allocation failed";
            return false;
        }

        FILE* fp = fopen(sdFullPath.c_str(), "rb");
        if (!fp) {
            imgf_decoder_destroy(dec);
            error = String("cannot open ") + sdFullPath;
            return false;
        }

        int maxW = 0;
        int maxH = 0;
        computeTargetBox(normMode, areaW, areaH, maxW, maxH);

        imgf_file_source_t fsrc{};
        imgf_stream_t stream = imgf_stream_from_file(&fsrc, fp, 0, 0);
        imgf_decode_opts_t dopts{};
        dopts.target_w = static_cast<uint16_t>(maxW);
        dopts.target_h = static_cast<uint16_t>(maxH);
        dopts.max_src_pixels = 4096u * 4096u;
        dopts.alloc_caps = MALLOC_CAP_SPIRAM;

        imgf_err_t err = imgf_decoder_open(dec, stream, &dopts);
        if (err != IMGF_OK) {
            fclose(fp);
            imgf_decoder_destroy(dec);
            error = String("image open failed: ") + imgf_err_to_str(err);
            return false;
        }

        uint16_t srcW = imgf_decoder_width(dec);
        uint16_t srcH = imgf_decoder_height(dec);
        imgf_resize_opts_t ropts{};
        ropts.fit = IMGF_FIT_CONTAIN;
        ropts.dst_pixfmt = IMGF_PIX_RGB565;
        ropts.alloc_caps = MALLOC_CAP_SPIRAM;
        ropts.orientation = IMGF_ORIENT_NONE;
        if (normMode == "center" && srcW <= static_cast<uint16_t>(maxW) && srcH <= static_cast<uint16_t>(maxH)) {
            ropts.target_w = srcW;
            ropts.target_h = srcH;
        } else {
            ropts.target_w = static_cast<uint16_t>(maxW);
            ropts.target_h = static_cast<uint16_t>(maxH);
        }

        uint8_t* decoded = nullptr;
        uint16_t outW = 0;
        uint16_t outH = 0;
        err = imgf_resize_decoder(dec, &ropts, &decoded, &outW, &outH);
        fclose(fp);
        imgf_decoder_destroy(dec);
        if (err != IMGF_OK || !decoded) {
            if (decoded) imgf_free(decoded);
            error = String("image decode failed: ") + imgf_err_to_str(err);
            return false;
        }

        pixels_ = reinterpret_cast<uint16_t*>(decoded);
        imgW_ = outW;
        imgH_ = outH;
        lastAreaW_ = areaW;
        lastAreaH_ = areaH;
        currentPath_ = sdFullPath;
        currentMode_ = normMode;
    }

    active_ = true;
    gfx.fillRect(areaX, areaY, areaW, areaH, backdropColor);
    int dstX = areaX + (areaW - static_cast<int>(imgW_)) / 2;
    int dstY = areaY + (areaH - static_cast<int>(imgH_)) / 2;
    gfx.blitRgb565(dstX, dstY, imgW_, imgH_, pixels_);
    return true;
}
