# SDL_Renderer Backend Migration

## Summary

Replace the manual `PixelBuffer` + surface-blit rendering path in `SoftwareBackend` with SDL3's GPU-accelerated `SDL_Renderer`, and add text rendering via `SDL3_ttf` with a bundled JetBrains Mono Nerd Font.

## Motivation

Every delegate emits `TextCmd` draw commands (field labels, slider values, label text), but the software rasteriser silently drops them. Without text, the UI is a grid of colored rectangles with no visible content. Text rendering unblocks visual validation of every existing widget.

Rather than implementing a custom glyph rasteriser, SDL3 already provides everything the `DrawList` command set needs: filled rects, outlined rects, texture-based text via SDL_ttf, and clip rects. Moving to `SDL_Renderer` gets all four in one change.

## Scope

### Changes

| File | Change |
|---|---|
| `src/backends/software_backend.cpp` | Rewrite render path: SDL_Renderer + SDL_ttf |
| `include/prism/backends/software_backend.hpp` | Replace `SoftwareRenderer` member with SDL_Renderer*, TTF_Font*, clip stack |
| `include/prism/core/backend.hpp` | Add optional `font_path` field to `BackendConfig` |
| `src/meson.build` | Add `sdl3_ttf` dependency |
| `subprojects/sdl3_ttf.wrap` | New meson wrap file |
| `assets/fonts/JetBrainsMonoNerdFont-Regular.ttf` | Bundled font (~1MB, OFL license) |
| `assets/fonts/OFL.txt` | SIL Open Font License text |

### Unchanged

- `BackendBase` interface -- no API change
- `DrawList`, `SceneSnapshot`, all core types -- untouched
- `SoftwareRenderer` + `PixelBuffer` -- stay in core for headless tests
- All existing tests -- still pass, still headless (no SDL dependency)
- `model_app`, `app<State>`, `App` entry points -- no change
- Examples -- no code change, they just start rendering text

## Design

### Command Mapping

| DrawCmd | SDL call |
|---|---|
| `FilledRect` | `SDL_SetRenderDrawColor` + `SDL_RenderFillRect` |
| `RectOutline` | `SDL_SetRenderDrawColor` + `SDL_RenderRect` |
| `TextCmd` | `TTF_RenderText_Blended` -> `SDL_CreateTextureFromSurface` -> `SDL_RenderTexture` |
| `ClipPush` | `SDL_SetRenderClipRect` (push onto stack) |
| `ClipPop` | Restore previous clip from stack (or clear if empty) |

### Backend Lifecycle

```
run():
    SDL_Init(SDL_INIT_VIDEO)
    SDL_CreateWindow(title, w, h, 0)
    SDL_CreateRenderer(window, NULL)      // SDL picks best GPU driver
    TTF_Init()
    TTF_OpenFont(font_path, default_pt)
    signal ready

    event loop:
        SDL_WaitEvent -> dispatch InputEvents via event_cb
        snap = snapshot_.load(acquire)
        if snap:
            render_snapshot(*snap)

    cleanup:
        TTF_CloseFont(font_)
        TTF_Quit()
        SDL_DestroyRenderer(renderer_)
        SDL_DestroyWindow(window_)
        SDL_Quit()
```

### render_snapshot()

Replaces the old `render_frame()` + `present()` pair:

```
render_snapshot(snap):
    SDL_SetRenderDrawColor(renderer_, bg)
    SDL_RenderClear(renderer_)
    for idx in snap.z_order:
        render_draw_list(snap.draw_lists[idx])
    SDL_RenderPresent(renderer_)
```

Each draw command is dispatched via `std::visit`:

```
render_cmd(FilledRect):
    SDL_SetRenderDrawColor(r, g, b, a)
    SDL_RenderFillRect(&sdl_rect)

render_cmd(RectOutline):
    SDL_SetRenderDrawColor(r, g, b, a)
    SDL_RenderRect(&sdl_rect)

render_cmd(TextCmd):
    SDL_Surface* surf = TTF_RenderText_Blended(font_, text, 0, sdl_color)
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf)
    SDL_RenderTexture(renderer_, tex, NULL, &dst_rect)
    SDL_DestroyTexture(tex)
    SDL_DestroySurface(surf)

render_cmd(ClipPush):
    clip_stack_.push_back(rect)
    SDL_SetRenderClipRect(renderer_, &sdl_rect)

render_cmd(ClipPop):
    clip_stack_.pop_back()
    if clip_stack_.empty():
        SDL_SetRenderClipRect(renderer_, NULL)
    else:
        SDL_SetRenderClipRect(renderer_, &clip_stack_.back())
```

### Text Rendering Strategy

**POC approach:** Render text fresh each frame. `TTF_RenderText_Blended` produces an `SDL_Surface`, converted to an `SDL_Texture`, rendered, then both destroyed. Simple, correct, no cache invalidation.

**Future optimisation:** A texture cache keyed on `(text, size, color)` to avoid per-frame surface/texture creation. Add when profiling shows text rendering is a bottleneck.

### Font Size

`TextCmd` carries a `float size` field. For the POC, open the font at one default point size (16pt). If the requested size differs from the loaded size, use `TTF_SetFontSize` to change it before rendering. SDL_ttf supports this without re-opening the font.

### Font Path Resolution

`BackendConfig` gains an optional `std::string font_path` field (default: empty). Resolution order:

1. If `font_path` is set, use it directly
2. Otherwise, use a compile-time `PRISM_FONT_PATH` define set by meson to the absolute path of the bundled font in the build/source tree

The meson define approach works for both `ninja -C builddir` and installed layouts. The `PRISM_FONT_PATH` default is set via `configure_file()` or `-D` flag pointing to `assets/fonts/JetBrainsMonoNerdFont-Regular.ttf`.

### Clip Stack

A `std::vector<SDL_FRect>` member on the backend. `ClipPush` appends, `ClipPop` pops. The stack should not intersect nested clips for now (each clip replaces the previous one). Intersecting nested clips can be added later if needed.

### Window Resize

`SDL_Renderer` handles resize automatically -- no manual buffer resize needed. The old `renderer_.resize()` call is removed. The `WindowResize` event still fires to the app thread for layout recalculation.

### Header Changes

```cpp
// software_backend.hpp -- after migration
class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(BackendConfig cfg);
    ~SoftwareBackend() override;
    // ... same interface ...

private:
    BackendConfig config_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    std::vector<SDL_FRect> clip_stack_;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot_;

    void render_snapshot(const SceneSnapshot& snap);
    void render_draw_list(const DrawList& dl);
};
```

The `SoftwareRenderer renderer_` member and `present()` method are removed.

## Dependencies

- **SDL3_ttf**: Added via `subprojects/sdl3_ttf.wrap`, declared as `dependency('sdl3_ttf', fallback: ...)` in `src/meson.build`
- **JetBrains Mono Nerd Font**: Regular weight, OFL licensed, bundled in `assets/fonts/`

## Testing

No new tests required for this change. Existing headless tests continue using `SoftwareRenderer` + `PixelBuffer`. The SDL_Renderer path is validated visually by running `model_dashboard` and `hello_rect` examples.

Future: screenshot comparison tests using the SDL_Renderer backend (deferred).

## What This Enables

- Visible text in all existing delegates (labels, field names, slider values)
- Outlined rects (widget borders, focus rings)
- Clip regions (scrollable areas, overflow)
- GPU-accelerated 2D rendering (SDL picks the best available driver)
- Foundation for text input widget (Phase 3)
