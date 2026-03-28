# SDL_Renderer Backend Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the PixelBuffer + surface-blit render path with SDL_Renderer + SDL3_ttf so all DrawList commands (text, outlines, clipping) render correctly.

**Architecture:** The `SoftwareBackend` drops its `SoftwareRenderer` member and instead creates an `SDL_Renderer` at startup. Each frame, it iterates the snapshot's draw lists and translates each `DrawCmd` to the corresponding SDL render call. Text is rendered via SDL3_ttf with a bundled JetBrains Mono Nerd Font. The headless `SoftwareRenderer` + `PixelBuffer` stay untouched for tests.

**Tech Stack:** SDL3, SDL3_ttf 3.2.x, Meson wraps, C++26

**Spec:** `docs/superpowers/specs/2026-03-28-sdl-renderer-migration-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `subprojects/sdl3_ttf.wrap` | Create | Meson wrap for SDL3_ttf dependency |
| `assets/fonts/JetBrainsMonoNerdFont-Regular.ttf` | Create | Bundled font file (~1MB) |
| `assets/fonts/OFL.txt` | Create | SIL Open Font License |
| `src/meson.build` | Modify | Add sdl3_ttf dep, PRISM_FONT_PATH define |
| `include/prism/core/backend.hpp` | Modify | Add `font_path` to BackendConfig |
| `include/prism/backends/software_backend.hpp` | Modify | Replace SoftwareRenderer with SDL_Renderer/TTF members |
| `src/backends/software_backend.cpp` | Modify | Rewrite render path using SDL_Renderer + SDL_ttf |

---

### Task 1: Add SDL3_ttf dependency and bundled font

**Files:**
- Create: `subprojects/sdl3_ttf.wrap`
- Create: `assets/fonts/JetBrainsMonoNerdFont-Regular.ttf`
- Create: `assets/fonts/OFL.txt`
- Modify: `src/meson.build`

- [ ] **Step 1: Install the SDL3_ttf meson wrap**

Run:
```bash
cd /var/home/jeandet/Documents/prog/PRSIM
meson wrap install sdl3_ttf
```

Expected: `subprojects/sdl3_ttf.wrap` created with version 3.2.2-2.

- [ ] **Step 2: Download and place JetBrains Mono Nerd Font**

Download the Regular weight from the Nerd Fonts release:
```bash
mkdir -p assets/fonts
curl -L -o /tmp/JetBrainsMono.tar.xz \
  "https://github.com/ryanoasis/nerd-fonts/releases/latest/download/JetBrainsMono.tar.xz"
tar xf /tmp/JetBrainsMono.tar.xz -C /tmp/JetBrainsMono --one-top-level
cp /tmp/JetBrainsMono/JetBrainsMonoNerdFont-Regular.ttf assets/fonts/
```

Expected: `assets/fonts/JetBrainsMonoNerdFont-Regular.ttf` exists (~1MB).

- [ ] **Step 3: Add the OFL license file**

Create `assets/fonts/OFL.txt` with the SIL Open Font License text. Copy it from the Nerd Fonts release (it's included in the archive) or from the JetBrains Mono repo.

```bash
cp /tmp/JetBrainsMono/OFL.txt assets/fonts/OFL.txt
```

- [ ] **Step 4: Add sdl3_ttf dependency and font path define to meson build**

Modify `src/meson.build`. Add the `sdl3_ttf` dependency next to `sdl3_dep`. Add a `-DPRISM_FONT_PATH` compile define pointing to the bundled font's absolute source path.

```meson
# Core: header-only, no external dependencies
prism_core_dep = declare_dependency(
  include_directories : prism_inc,
)

# Software backend: links SDL3 + SDL3_ttf
sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
sdl3_ttf_dep = dependency('sdl3_ttf', fallback : ['sdl3_ttf', 'sdl3_ttf_dep'])

font_path = meson.project_source_root() / 'assets' / 'fonts' / 'JetBrainsMonoNerdFont-Regular.ttf'

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  dependencies : [prism_core_dep, sdl3_dep, sdl3_ttf_dep],
  cpp_args : ['-DPRISM_FONT_PATH="' + font_path + '"'],
  install : true,
)

prism_software_backend_dep = declare_dependency(
  link_with : prism_software_backend_lib,
  dependencies : [prism_core_dep],
)

# Convenience: core + default software backend (SDL3 headers exposed for keycodes etc.)
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep, sdl3_dep],
)
```

- [ ] **Step 5: Verify the build still compiles**

Run:
```bash
meson setup builddir --wipe
ninja -C builddir
```

Expected: Build succeeds. SDL3_ttf is fetched and compiled. No link errors. All existing tests still pass.

- [ ] **Step 6: Run tests to confirm nothing is broken**

Run:
```bash
meson test -C builddir
```

Expected: All existing tests pass.

- [ ] **Step 7: Commit**

```bash
git add subprojects/sdl3_ttf.wrap assets/fonts/ src/meson.build
git commit -m "build: add SDL3_ttf dependency and bundle JetBrains Mono Nerd Font"
```

---

### Task 2: Add font_path to BackendConfig

**Files:**
- Modify: `include/prism/core/backend.hpp`

- [ ] **Step 1: Add font_path field to BackendConfig**

In `include/prism/core/backend.hpp`, add the field after the existing fields:

```cpp
struct BackendConfig {
    const char* title     = "PRISM";
    int         width     = 800;
    int         height    = 600;
    const char* font_path = nullptr;  // nullptr = use bundled default
};
```

- [ ] **Step 2: Verify the build still compiles**

Run:
```bash
ninja -C builddir
```

Expected: Build succeeds. The new field has a default value so all existing call sites are unchanged.

- [ ] **Step 3: Run tests**

Run:
```bash
meson test -C builddir
```

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/backend.hpp
git commit -m "feat: add font_path to BackendConfig for custom font loading"
```

---

### Task 3: Rewrite SoftwareBackend header

**Files:**
- Modify: `include/prism/backends/software_backend.hpp`

- [ ] **Step 1: Replace SoftwareRenderer with SDL_Renderer/TTF members**

Replace the entire content of `include/prism/backends/software_backend.hpp`:

```cpp
#pragma once

#include <prism/core/backend.hpp>

#include <atomic>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_FRect;
typedef struct _TTF_Font TTF_Font;

namespace prism {

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(BackendConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    void run(std::function<void(const InputEvent&)> event_cb) override;
    void submit(std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;

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
    void render_cmd(const FilledRect& cmd);
    void render_cmd(const RectOutline& cmd);
    void render_cmd(const TextCmd& cmd);
    void render_cmd(const ClipPush& cmd);
    void render_cmd(const ClipPop& cmd);
};

} // namespace prism
```

Key changes vs old header:
- Removed: `#include <prism/core/software_renderer.hpp>`, `SoftwareRenderer renderer_` member, `void present()`
- Added: forward declarations for `SDL_Renderer`, `SDL_FRect`, `TTF_Font`
- Added: `SDL_Renderer* renderer_`, `TTF_Font* font_`, `std::vector<SDL_FRect> clip_stack_`
- Added: `render_snapshot()`, `render_draw_list()`, per-command `render_cmd()` overloads

- [ ] **Step 2: Verify it compiles (will fail at link — implementation not yet updated)**

Run:
```bash
ninja -C builddir 2>&1 | head -20
```

Expected: Header compiles, but `software_backend.cpp` will have errors because it still references the old `SoftwareRenderer` member and `present()`. That's expected — we fix it in Task 4.

Note: If you get header-level errors (e.g., SDL_FRect not a valid forward declaration), fix the forward declaration syntax. SDL3 uses plain `struct SDL_FRect` so `struct SDL_FRect;` should work. For TTF_Font, SDL3_ttf defines it as a typedef so use `typedef struct _TTF_Font TTF_Font;` or just include the header — in which case replace the forward declaration with `#include <SDL3_ttf/SDL_ttf.h>`.

- [ ] **Step 3: Commit (partial — header only)**

```bash
git add include/prism/backends/software_backend.hpp
git commit -m "refactor: update SoftwareBackend header for SDL_Renderer migration"
```

---

### Task 4: Rewrite SoftwareBackend implementation

**Files:**
- Modify: `src/backends/software_backend.cpp`

- [ ] **Step 1: Rewrite the entire implementation**

Replace the content of `src/backends/software_backend.cpp`:

```cpp
#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>

namespace prism {

namespace {

SDL_FRect to_sdl(Rect r) {
    return {r.x, r.y, r.w, r.h};
}

SDL_Color to_sdl(Color c) {
    return {c.r, c.g, c.b, c.a};
}

const char* resolve_font_path(const BackendConfig& cfg) {
    if (cfg.font_path) return cfg.font_path;
#ifdef PRISM_FONT_PATH
    return PRISM_FONT_PATH;
#else
    return nullptr;
#endif
}

} // namespace

SoftwareBackend::SoftwareBackend(BackendConfig cfg)
    : config_(cfg)
{}

SoftwareBackend::~SoftwareBackend() {
    if (font_) TTF_CloseFont(font_);
    TTF_Quit();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SoftwareBackend::run(std::function<void(const InputEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(config_.title, config_.width, config_.height, 0);
    renderer_ = SDL_CreateRenderer(window_, nullptr);

    TTF_Init();
    const char* fpath = resolve_font_path(config_);
    if (fpath) {
        font_ = TTF_OpenFont(fpath, 16.0f);
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                event_cb(WindowResize{ev.window.data1, ev.window.data2});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event_cb(MouseMove{{ev.motion.x, ev.motion.y}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, true});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, false});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event_cb(MouseScroll{
                    {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
                break;
            case SDL_EVENT_KEY_DOWN:
                event_cb(KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod});
                break;
            case SDL_EVENT_KEY_UP:
                event_cb(KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod});
                break;
            case SDL_EVENT_USER:
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (!running_.load(std::memory_order_relaxed)) break;

        auto snap = snapshot_.load(std::memory_order_acquire);
        if (snap) {
            render_snapshot(*snap);
        }
    }
}

void SoftwareBackend::submit(std::shared_ptr<const SceneSnapshot> snap) {
    snapshot_.store(std::move(snap), std::memory_order_release);
}

void SoftwareBackend::wake() {
    SDL_Event wake_ev{};
    wake_ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake_ev);
}

void SoftwareBackend::wait_ready() {
    ready_.wait(false, std::memory_order_acquire);
}

void SoftwareBackend::quit() {
    running_.store(false, std::memory_order_relaxed);
    wake();
}

void SoftwareBackend::render_snapshot(const SceneSnapshot& snap) {
    SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
    SDL_RenderClear(renderer_);
    for (uint16_t idx : snap.z_order) {
        render_draw_list(snap.draw_lists[idx]);
    }
    SDL_RenderPresent(renderer_);
}

void SoftwareBackend::render_draw_list(const DrawList& dl) {
    for (const auto& cmd : dl.commands) {
        std::visit([this](const auto& c) { render_cmd(c); }, cmd);
    }
}

void SoftwareBackend::render_cmd(const FilledRect& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderFillRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const RectOutline& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const TextCmd& cmd) {
    if (!font_ || cmd.text.empty()) return;

    // Adjust font size if needed
    float current_size = TTF_GetFontSize(font_);
    if (std::abs(current_size - cmd.size) > 0.5f) {
        TTF_SetFontSize(font_, cmd.size);
    }

    SDL_Color color = to_sdl(cmd.color);
    SDL_Surface* surface = TTF_RenderText_Blended(font_, cmd.text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (texture) {
        SDL_FRect dst = {cmd.origin.x, cmd.origin.y,
                         static_cast<float>(surface->w),
                         static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

void SoftwareBackend::render_cmd(const ClipPush& cmd) {
    SDL_FRect r = to_sdl(cmd.rect);
    clip_stack_.push_back(r);
    SDL_SetRenderClipRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const ClipPop&) {
    if (!clip_stack_.empty()) clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        SDL_SetRenderClipRect(renderer_, &clip_stack_.back());
    }
}

Backend Backend::software(BackendConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
```

- [ ] **Step 2: Build and fix any compile errors**

Run:
```bash
ninja -C builddir
```

Expected: Build succeeds. If there are SDL3_ttf API mismatches (e.g., `TTF_GetFontSize` signature, `TTF_RenderText_Blended` parameter count), check the installed SDL3_ttf header at `builddir/subprojects/sdl3_ttf-*/include/SDL3_ttf/SDL_ttf.h` and adjust accordingly.

Common issues to watch for:
- `TTF_GetFontSize` may return `float` directly or take a pointer — check the header
- `TTF_RenderText_Blended` takes `(font, text, length, color)` — the `length` param (pass `0` for null-terminated) is new in SDL3_ttf
- Forward declaration of `TTF_Font` might need adjustment — if `typedef struct _TTF_Font TTF_Font` doesn't work, include the header directly in the `.hpp`

- [ ] **Step 3: Run all tests**

Run:
```bash
meson test -C builddir
```

Expected: All existing tests pass. No test touches the SDL_Renderer path — they all use headless `SoftwareRenderer` or `NullBackend`/`TestBackend`.

- [ ] **Step 4: Commit**

```bash
git add src/backends/software_backend.cpp
git commit -m "feat: rewrite SoftwareBackend to use SDL_Renderer + SDL3_ttf"
```

---

### Task 5: Visual verification

**Files:** None modified — this task verifies the migration works.

- [ ] **Step 1: Run the model_dashboard example**

Run:
```bash
./builddir/examples/model_dashboard
```

Expected: A window opens showing the Dashboard with:
- Text labels visible for each field ("Username", "Dark Mode", "Volume", "Status", "Counter")
- Bool toggle for dark_mode still clickable
- Slider for volume still draggable
- Label showing "All systems go"
- Outlined rects visible (if any delegates emit `RectOutline`)

If text appears but is mispositioned or sized wrong, adjust delegate draw code in a follow-up — the backend migration is complete.

- [ ] **Step 2: Run the hello_rect example**

Run:
```bash
./builddir/examples/hello_rect
```

Expected: The layout with header/sidebar/content/footer renders. Keyboard input (1/2/3) still changes panel selection. Colors render correctly via `SDL_RenderFillRect`.

- [ ] **Step 3: Check for rendering regressions**

Compare the visual output against what the old PixelBuffer backend produced:
- Filled rectangles should look identical (same colors, same positions)
- New: text should now be visible
- New: rect outlines should now be visible

If everything looks correct, the migration is done.

- [ ] **Step 4: Final commit (if any fixups were needed)**

If any small fixes were made during visual verification:
```bash
git add -u
git commit -m "fix: adjust rendering after SDL_Renderer migration"
```
