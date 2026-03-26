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

## Software Backend (Implemented — POC)

**Architecture:**
```
SceneSnapshot
  → iterate z_order back-to-front
  → rasterise each DrawList command (FilledRect only in POC)
  → write to PixelBuffer (std::vector<uint32_t>, ARGB8888)
```

**Implemented types:**
- `PixelBuffer` — `std::vector<uint32_t>`, ARGB8888, width × height. Provides `fill_rect(Rect, Color)` with bounds clipping, `clear()`, `resize()`, `pixel(x,y)` accessor.
- `SoftwareRenderer` — headless, no SDL dependency. `render_frame(snap)` clears buffer then rasterises. Testable by inspecting `buffer()`.

**POC limitations:**
- Only `FilledRect` commands are rasterised. `RectOutline`, `TextCmd`, `ClipPush/Pop` are no-ops.
- No tile splitting or parallel rasterisation — sequential full-buffer pass.
- No text rendering.

**Presentation** is handled by `RenderLoop`, not the renderer itself. `RenderLoop` copies the `PixelBuffer` to the SDL window surface via `SDL_GetWindowSurface` / `SDL_LockSurface` / `memcpy` / `SDL_UpdateWindowSurface`.

## Window Ownership

The render thread creates the SDL window (via `RenderLoop`) and owns the event pump. `SDL_Init(SDL_INIT_VIDEO)` is called on the render thread (SDL3 requires event pumping on the init thread). The software renderer is headless and never touches SDL — it just writes to a `PixelBuffer`.

## GPU Backend (Phase 5)

```
SceneSnapshot
  → upload DrawList commands as GPU vertex/index data
  → fragment shader rasterisation (or compute shader for tiles)
  → GPU-side compositing with blending
  → swapchain present
```

Tile strategy differs: CPU wants spatial tiles for parallelism, GPU wants draw-call batching for throughput.

## Open Questions

- Should `render_frame` return frame timing / stats for profiling?
- Backend discovery — compile-time selection is fine for now. Runtime fallback chain (Vulkan → software) deferred.
- Multi-window — deferred. One App = one window for now.
- Tile splitting for parallel CPU rasterisation — deferred until profiling shows need.
