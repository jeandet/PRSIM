#pragma once

#include <prism/core/draw_list.hpp> // Rect, Color

#include <algorithm>
#include <cstdint>
#include <vector>

namespace prism {

// ARGB8888 pixel buffer for CPU rasterisation.
// Pixel layout matches SDL_PIXELFORMAT_ARGB8888.
class PixelBuffer {
public:
    PixelBuffer(int w, int h)
        : w_(w), h_(h), pixels_(static_cast<std::size_t>(w * h), 0xFF000000)
    {}

    [[nodiscard]] int width() const { return w_; }
    [[nodiscard]] int height() const { return h_; }
    [[nodiscard]] uint32_t* data() { return pixels_.data(); }
    [[nodiscard]] const uint32_t* data() const { return pixels_.data(); }
    [[nodiscard]] std::size_t pitch() const { return static_cast<std::size_t>(w_) * sizeof(uint32_t); }

    [[nodiscard]] uint32_t pixel(int x, int y) const {
        return pixels_[static_cast<std::size_t>(y * w_ + x)];
    }

    void clear() {
        std::fill(pixels_.begin(), pixels_.end(), 0xFF000000);
    }

    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        pixels_.assign(static_cast<std::size_t>(w * h), 0xFF000000);
    }

    void fill_rect(Rect r, Color c) {
        int x0 = std::max(0, static_cast<int>(r.x));
        int y0 = std::max(0, static_cast<int>(r.y));
        int x1 = std::min(w_, static_cast<int>(r.x + r.w));
        int y1 = std::min(h_, static_cast<int>(r.y + r.h));

        uint32_t packed = pack(c);
        for (int y = y0; y < y1; ++y) {
            auto row = pixels_.begin() + y * w_;
            std::fill(row + x0, row + x1, packed);
        }
    }

private:
    int w_, h_;
    std::vector<uint32_t> pixels_;

    static constexpr uint32_t pack(Color c) {
        return (uint32_t{c.a} << 24) | (uint32_t{c.r} << 16)
             | (uint32_t{c.g} << 8)  | uint32_t{c.b};
    }
};

} // namespace prism
