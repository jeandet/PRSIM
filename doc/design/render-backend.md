# Render Backend

## Overview

A render backend takes an immutable `SceneSnapshot` and puts pixels on screen. The entire render path — from snapshot interpretation to frame presentation — is owned by the backend. There is no shared intermediate representation between backends.

## Interface

`BackendBase` is an abstract class — runtime-polymorphic so backends can be loaded or switched at runtime:

```cpp
class BackendBase {
public:
    virtual ~BackendBase();
    virtual void run(std::function<void(const InputEvent&)> event_cb) = 0;
    virtual void submit(std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
    virtual void wait_ready() {}
};
```

`Backend` is a move-only RAII wrapper with static factories:
- `Backend::software(cfg)` — built-in, statically linked
- `Backend::load(path, cfg)` — dlopen a backend .so (future)

The backend owns: window, event loop, rendering, presentation. Communication:
- App → Backend: `submit()` + `wake()`
- Backend → App: `event_cb` callback with `InputEvent` values

Each backend .so exports an `extern "C"` factory returning `std::unique_ptr<BackendBase>`. C++ vtable ABI (not C ABI) — same compiler is required for a C++26 project. This matches how Qt, Mesa, and other C++ libraries handle plugins.

## Why Not Split Rasteriser + Presenter?

A split interface (`Rasteriser` + `Presenter` with a `PixelBuffer` in between) forces GPU backends through a CPU-side pixel buffer, defeating the purpose. Instead:

- The **software backend** internally splits into CPU rasteriser + SDL3 presenter — but that's its private implementation detail.
- A **Vulkan backend** goes GPU-native end-to-end: upload draw list → shaders → swapchain present.
- They share no intermediate types except `SceneSnapshot` and `DrawList`.

## Software Backend (Implemented)

`SoftwareBackend : BackendBase` owns the SDL window, event loop, software rasteriser, and surface blit presentation.

**Architecture:**
```
SceneSnapshot
  → iterate z_order back-to-front
  → rasterise each DrawList command (FilledRect only in POC)
  → write to PixelBuffer (std::vector<uint32_t>, ARGB8888)
  → blit to SDL window surface
```

**Headless types (header-only, no SDL):**
- `PixelBuffer` — `std::vector<uint32_t>`, ARGB8888, width × height. Provides `fill_rect(Rect, Color)` with bounds clipping, `clear()`, `resize()`, `pixel(x,y)` accessor.
- `SoftwareRenderer` — headless rasteriser. `render_frame(snap)` clears buffer then rasterises. Testable by inspecting `buffer()`.

**POC limitations:**
- Only `FilledRect` commands are rasterised. `RectOutline`, `TextCmd`, `ClipPush/Pop` are no-ops.
- No tile splitting or parallel rasterisation — sequential full-buffer pass.
- No text rendering.

## Build System

The core library is header-only (no SDL dependency). Each backend is a separate shared library:

| Meson dependency | Links | Purpose |
|---|---|---|
| `prism_core_dep` | nothing (header-only) | Widgets, DrawList, SceneSnapshot, concurrency primitives |
| `prism_software_backend_dep` | `libprism-software-backend.so` + SDL3 | Software backend |
| `prism_dep` | core + software backend | Convenience for apps |

Headless tests link only `prism_core_dep` — no SDL required.

## Window Ownership

The backend creates the SDL window and owns the event pump. `SDL_Init(SDL_INIT_VIDEO)` is called inside `SoftwareBackend::run()` (SDL3 requires event pumping on the init thread). A `wait_ready()` handshake ensures the app thread doesn't call `wake()` before SDL is initialised.

## GPU Backend (Phase 5)

```
SceneSnapshot
  → upload DrawList commands as GPU vertex/index data
  → fragment shader rasterisation (or compute shader for tiles)
  → GPU-side compositing with blending
  → swapchain present
```

Tile strategy differs: CPU wants spatial tiles for parallelism, GPU wants draw-call batching for throughput.

## Packaging

| Package | Contents |
|---|---|
| `libprism-dev` | Headers (header-only core) + `BackendBase` vtable anchor (.a) |
| `libprism-software` | `SoftwareBackend` .so (links SDL3) |
| `libprism-vulkan` | `VulkanBackend` .so (links Vulkan) [future] |

`BackendBase::~BackendBase()` is defined out-of-line in `backend.cpp` to anchor the vtable in a single translation unit. Soname versioning on vtable-breaking changes.

## Open Questions

- Should `render_frame` return frame timing / stats for profiling?
- Multi-window — deferred. One App = one window for now.
- Tile splitting for parallel CPU rasterisation — deferred until profiling shows need.
- `request_redraw()` for animation-driven repaints without input events.
