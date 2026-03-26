# PRISM — Parallel Rendering & Immediate Scene Model

A next-generation 2D UI toolkit in C++26, built from scratch on modern language features with no legacy constraints.

> Take Qt's reactive topology, ImGui's draw-list simplicity, Flutter's pipeline stages, WebRender's tile compositing, SwiftUI's declarative ergonomics, and game engine threading discipline — and put it all together.

## The Problem

Every major UI toolkit — Qt, GTK, wxWidgets, WinForms — couples the application and the renderer on a single thread. Rendering competes with business logic for CPU time, and if any step exceeds the frame budget, the UI freezes.

```
Main Thread (event loop)
├── Process input events
├── Fire signal/slot callbacks         ← your model logic runs here
├── Execute layout pass                ← sequential, blocks frame
├── Call paintEvent() on each widget   ← rendering happens here
└── Present frame
     ↑
     If ANY of these steps takes > 16ms → frame drop, frozen UI
```

This is an **architectural problem**, not a tuning problem.

## Architecture

PRISM decouples the application from the renderer through a versioned, immutable scene snapshot exchanged via lock-free primitives.

```
╔═══════════════════════════════════════════════════════════════╗
║  APPLICATION LAYER — backend thread(s), any number, any cadence  ║
║  [ Model A ]  [ Model B ]  [ Time-series ]  [ Network ]         ║
╚═════════════════╤═════════════════════════════════════════════╝
                  │  publish via lock-free MPSC queue
                  │  (versioned immutable snapshot or diff)
                  ▼
╔═══════════════════════════════════════════════════════════════╗
║  SCENE DESCRIPTION — versioned, immutable, plain data            ║
║  { widget_id → DrawList, rect, clip, z_order, opacity, ... }     ║
╚═════════════════╤═════════════════════════════════════════════╝
                  │  atomic pointer swap at vsync
                  ▼
╔═══════════════════════════════════════════════════════════════╗
║  RENDER GRAPH → tile rasterisation (thread pool) → compositor    ║
║  Guaranteed frame delivery independent of application state      ║
╚═══════════════════════════════════════════════════════════════╝
```

**The frame contract:** the renderer guarantees a frame every N ms regardless of what the application threads are doing. The application never blocks the renderer. The renderer never calls into application code.

## Render Backend

Each backend is a single concept implementation — one type, one function: `render_frame(snapshot)`. The backend owns the entire path from scene snapshot to pixels on screen.

```
SceneSnapshot (immutable plain data)
       │
       ▼
┌──────────────────────────────────────────────────┐
│ RenderBackend::render_frame(snapshot)             │
│                                                   │
│  Software:              Vulkan (future):          │
│    tile split             upload draw list        │
│    CPU rasterise          GPU shaders             │
│    SDL3 present           swapchain present       │
└──────────────────────────────────────────────────┘
```

No abstract base class with dozens of virtual methods. The draw list *is* the abstraction — the backend just interprets it. Adding a backend = implement one function in one file.

## Key Design Choices

| | Qt | Flutter | SwiftUI | **PRISM** |
|---|---|---|---|---|
| Multi-threaded rendering | — | Partial | — | Yes |
| Frame contract independent of app | — | Partial | — | Yes |
| Declarative value-based UI | — | Yes | Yes | Yes |
| RAII / no raw pointers | — | n/a | n/a | Yes |
| Parallel layout | — | — | — | Yes |
| No preprocessor / moc | — | n/a | n/a | Yes |
| Cross-platform C++ | Yes | — | — | Yes |

## API Taste

```cpp
// Declarative composition — no heap allocations, no pointers, no macros
auto login_form = VStack {
    .gap      = 16,
    .padding  = Insets::all(24),
    .children = {
        Label { .text = "Sign in", .style = TextStyle::Heading1 },
        TextField {
            .value       = bind(model.username),
            .placeholder = "Username",
        },
        TextField {
            .value       = bind(model.password),
            .placeholder = "Password",
            .secure      = true,
        },
        HStack {
            .gap = 8,
            .children = {
                Button { .label = "Cancel",  .on_click = [&] { nav.pop(); } },
                Button { .label = "Sign in", .on_click = [&] { model.submit(); } },
            }
        }
    }
};
```

## C++26 Features

| Feature | Used for |
|---|---|
| `std::execution` (P2300) | Signal/slot scheduling, async pipeline, render job submission |
| Static Reflection | Observable properties, binding derivation, serialisation |
| `std::expected` | All fallible API operations — no exceptions at API boundary |
| Designated initialisers | Named-parameter widget construction |
| Concepts & Constraints | Composability rules with clean error messages |
| `std::mdspan` | Tile buffer management, texture atlases |
| `std::flat_map` | Cache-friendly widget property storage |

## Building

Requires GCC 15+ (or any compiler with C++26 support) and Meson >= 1.5.

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

## Roadmap

- **Phase 1** — Core threading infrastructure (MPSC queue, double-buffered scene snapshot, draw list, tile rasteriser stub)
- **Phase 2** — Layout engine (constraint regions, parallel solving, pure functional layout with memoisation)
- **Phase 3** — Reactivity & bindings (`observable<T>`, `bind()`, `std::execution`-based signals)
- **Phase 4** — Widget library (Label, Button, TextField, VStack/HStack/Grid, Plot, VirtualList)
- **Phase 5** — GPU backend & compositing (Vulkan/Metal, SDF text, tile compositing, 10M-point benchmark)

## License

MIT
