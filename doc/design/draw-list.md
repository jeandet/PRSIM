# Draw List

## Overview

A `DrawList` is a flat array of typed draw commands recorded by a widget. It is the immediate-style capture mechanism — widgets describe *what* to draw, not *how* to draw it. The backend interprets draw commands into actual pixels.

## Command Set

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop>;
```

Current commands:

| Command | Fields | Purpose |
|---|---|---|
| `FilledRect` | rect, color | Solid rectangle fill |
| `RectOutline` | rect, color, thickness | Rectangle stroke |
| `TextCmd` | text, origin, size, color | Text at a position |
| `ClipPush` | rect | Push a clip rectangle onto the clip stack |
| `ClipPop` | — | Pop the clip stack |

## Design Principles

- **Flat array**: contiguous in memory, cache-friendly, trivially copyable across threads.
- **Discriminated union**: `std::variant` — no vtables, no heap allocation per command.
- **Extensible**: adding a new command = add a struct + add it to the variant. No interface changes.
- **Backend-agnostic**: commands describe geometry and color, not GPU state or API calls.

## Future Commands

- `ImageBlit` — texture handle, source rect, destination rect.
- `Line` / `Polyline` — for plot data (critical for scientific visualisation use case).
- `RoundedRect` — common UI primitive.
- `Path` — general vector path for complex shapes.
- `Gradient` — linear/radial gradient fills.

## Serialisation

DrawList should be serialisable for:

- Deterministic replay (record a session, replay for debugging).
- Visual regression tests (compare serialised draw lists across runs).
- Network transparency (remote rendering).

Since all commands are plain data with no pointers, serialisation is straightforward.

## Open Questions

- Should text commands carry a font handle or a style enum? (Font resolution is a backend concern.)
- Batching strategy — should the draw list pre-sort by command type for the backend?
- Anti-aliasing hints — per-command or global render setting?
