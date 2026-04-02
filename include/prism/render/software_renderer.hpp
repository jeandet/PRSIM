#pragma once

#include <prism/render/pixel_buffer.hpp>
#include <prism/render/scene_snapshot.hpp>

namespace prism::render {

// Rasterises a SceneSnapshot into a PixelBuffer.
// Satisfies RenderBackend concept: render_frame(snap), resize(w, h).
// Headless — no SDL dependency. Testable by inspecting buffer().
class SoftwareRenderer {
public:
    SoftwareRenderer(int w, int h) : buf_(w, h) {}

    void render_frame(const SceneSnapshot& snap) {
        buf_.clear();
        for (uint16_t idx : snap.z_order) {
            rasterise_draw_list(snap.draw_lists[idx]);
        }
    }

    void resize(int w, int h) { buf_.resize(w, h); }

    [[nodiscard]] const PixelBuffer& buffer() const { return buf_; }
    [[nodiscard]] PixelBuffer& buffer() { return buf_; }

private:
    PixelBuffer buf_;

    void rasterise_draw_list(const DrawList& dl) {
        for (const auto& cmd : dl.commands) {
            std::visit([this](const auto& c) { rasterise(c); }, cmd);
        }
    }

    void rasterise(const FilledRect& cmd) { buf_.fill_rect(cmd.rect, cmd.color); }
    void rasterise(const RectOutline&) {} // POC: skip outlines
    void rasterise(const TextCmd&) {}     // POC: skip text
    void rasterise(const ClipPush&) {}    // POC: skip clipping
    void rasterise(const ClipPop&) {}     // POC: skip clipping
    void rasterise(const RoundedRect&) {} // POC: skip rounded rects
    void rasterise(const Line&) {}        // POC: skip lines
    void rasterise(const Polyline&) {}    // POC: skip polylines
    void rasterise(const Circle&) {}      // POC: skip circles
};

} // namespace prism::render
