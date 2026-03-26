# Render Backend

## Overview

A render backend takes an immutable `SceneSnapshot` and puts pixels on screen. The entire render path — from snapshot interpretation to frame presentation — is owned by the backend. There is no shared intermediate representation between backends.

## Interface

```cpp
template <typename T>
concept RenderBackend = requires(T b, const SceneSnapshot& snap) {
    { b.render_frame(snap) } -> std::same_as<void>;
    { b.resize(int, int) } -> std::same_as<void>;
};
```

One function. One concept. Adding a backend = implement one type.

## Why Not Split Rasteriser + Presenter?

A split interface (`Rasteriser` + `Presenter` with a `PixelBuffer` in between) forces GPU backends through a CPU-side pixel buffer, defeating the purpose. Instead:

- The **software backend** internally splits into CPU rasteriser + SDL3 presenter — but that's its private implementation detail.
- A **Vulkan backend** goes GPU-native end-to-end: upload draw list → shaders → swapchain present.
- They share no intermediate types except `SceneSnapshot` and `DrawList`.

## Software Backend (Phase 1)

```
SceneSnapshot
  → tile split (spatial partitioning of widget rects)
  → per-tile CPU rasterisation (parallel via thread pool)
  → composite tiles into framebuffer (PixelBuffer)
  → SDL3 blit to window
```

Internal types:
- `PixelBuffer` — `std::vector<uint32_t>`, RGBA8, width × height.
- `Tile` — region rect + local pixel buffer.

Testable headless: skip SDL3 presenter, inspect pixel buffer directly.

## GPU Backend (Phase 5)

```
SceneSnapshot
  → upload DrawList commands as GPU vertex/index data
  → fragment shader rasterisation (or compute shader for tiles)
  → GPU-side compositing with blending
  → swapchain present
```

Tile strategy differs: CPU wants spatial tiles for parallelism, GPU wants draw-call batching for throughput.

## Pipeline Integration

```cpp
template <RenderBackend Backend>
class RenderPipeline {
    Backend backend_;
    atomic_cell<SceneSnapshot> snapshot_cell_;
    mpsc_queue<SceneDiff> diff_queue_;

    void frame_loop() {
        auto snap = snapshot_cell_.load();
        if (!snap) return;
        // drain diffs, patch snapshot
        backend_.render_frame(*snap);
    }
};
```

## Open Questions

- Should `render_frame` return frame timing / stats for profiling?
- Backend discovery — compile-time selection is fine, but should there be a runtime fallback chain (Vulkan → OpenGL → software)?
- Multi-monitor / multi-window — one backend per window, or one backend managing multiple surfaces?
