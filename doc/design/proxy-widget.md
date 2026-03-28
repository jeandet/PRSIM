# Proxy Widget

**Status:** Concept

A proxy widget composites content from a foreign renderer (another process, library, or platform element) into the PRISM widget tree. The foreign source renders to a shared surface; PRISM treats it as a regular widget for layout, hit testing, and input routing.

## Architecture

```
Foreign Renderer          PRISM
+-----------------+       +-------------------+
| CEF / VTK / mpv |       | ProxyWidget       |
| renders to      |------>| Delegate:          |
| shared surface  |       |   record() -> blit |
+-----------------+       |   handle_input()   |
       ^                  |     -> forward     |
       |                  +-------------------+
       +--- input events forwarded back ------+
```

The proxy widget participates in the normal PRISM pipeline:
- **Layout** assigns it a rect like any other widget
- **record()** emits a "blit surface" DrawList command
- **handle_input()** serializes input events and forwards them to the foreign renderer
- **SceneSnapshot** carries the composited result to the backend thread

## Transport Mechanisms

| Platform | Zero-copy             | Fallback (memcpy)       |
|----------|-----------------------|-------------------------|
| Linux    | DMA-BUF / EGL image   | Shared memory (shm)     |
| macOS    | IOSurface             | Shared memory (mmap)    |
| Windows  | DXGI shared handle    | Named shared memory     |
| Wasm     | DOM overlay (iframe)  | N/A                     |

The shared-memory fallback works everywhere: the foreign renderer writes pixels to a buffer, PRISM uploads via `SDL_UpdateTexture()`. For a 1080p region (~8 MB/frame), this is well within budget at 60 fps.

Zero-copy paths are platform-specific but SDL3's texture API abstracts most differences.

The Wasm target is a special case: instead of pixel sharing, PRISM position-syncs a DOM element (iframe, video) over its canvas. Embedding a browser engine inside Wasm would be circular.

## Use Cases

### Scientific workbench

Heterogeneous renderers unified under one reactive model:
- **Plotting** -- proxy existing SciQLopPlots or Matplotlib (agg backend) for time series
- **3D viewport** -- VTK/ParaView off-screen render for spacecraft trajectories or magnetosphere visualization
- **Terminal** -- libvterm off-screen for running analysis scripts inline
- **Maps** -- MapLibre GL Native for ground tracks and station positions

All proxies share `Field<T>` state (e.g. a time range). Scrub one, all update via `on_change()`.

### Media

- **Video** -- mpv/GStreamer shared surface for telemetry video playback
- **Camera** -- live webcam feeds for lab monitoring

### Documents

- **PDF** -- poppler/mupdf off-screen render for inline paper/spec viewing
- **LaTeX/Markdown** -- live equation rendering for analysis notes

### Embedding

- **Another PRISM app** -- nested/plugin architecture for free
- **Web content** -- CEF off-screen rendering (desktop platforms)
- **Remote desktop** -- VNC/RDP frame buffer for remote instrument control

## What PRISM Needs

1. A **"blit surface" DrawList command** -- references a shared texture/surface by handle
2. A **`Delegate<ProxyWidget>`** -- records the blit command, forwards input events
3. A **`SharedSurface` abstraction** -- thin wrapper over platform-specific texture sharing, with a shared-memory fallback

The `ProxyWidget` sentinel would look like:

```cpp
struct MyApp {
    prism::Field<prism::ProxyWidget> plots{/* ... */};
    prism::Field<prism::ProxyWidget> viewport_3d{/* ... */};
    prism::Field<prism::ProxyWidget> terminal{/* ... */};
};
```

## Open Questions

- Should input forwarding be synchronous (blocking round-trip) or async (fire-and-forget with ack)?
- How to handle cursor shape changes from the foreign renderer?
- Should the proxy negotiate its preferred size, or does PRISM layout dictate it?
- Damage tracking: can the proxy report dirty rects to avoid full-surface uploads?
