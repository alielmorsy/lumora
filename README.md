# Lumora

A reactive, GPU-rendered C++20 GUI library. Components and signals on top, a CSS subset for styling, flexbox layout, and a Vulkan 1.1 renderer underneath.

- **Reactive core**: `Signal`, `Computed`, `Effect`, `Each`, and `Show` with fine-grained dependency tracking.
- **CSS styling**: selectors, cascade, variables, media queries, transitions, keyframes, and transforms.
- **Flexbox layout**: flexbox containers, items, grid tracks, alignment, and gaps.
- **Widgets**: buttons, switches, checkboxes, inputs, text editor, lists, virtual lists, grids, docking, menus, node graph editor, and SVG.
- **Full text support**: text shaping, bidirectional text reordering, and line breaking.
- **GPU rendering**: batched rendering pipeline targeting Vulkan 1.1 with embedded SPIR-V shaders.

## Build

### Requirements

- CMake 3.28+
- Ninja
- C++20 compiler (tested with MSVC 2022)
- Vulkan SDK (includes `dxc` for compiling HLSL shaders to SPIR-V)

### Building with Presets

```sh
cmake --preset debug
cmake --build --preset debug
```

For release builds:

```sh
cmake --preset release
cmake --build --preset release
```

On Windows, `scripts\build.cmd` and `scripts\release.cmd` set up the MSVC environment first before building. Build output is placed in `build/<preset>`.

### Headless Linux Presets

`CMakePresets.json` defines headless configurations where GPU rendering and samples are disabled (`LUMORA_BUILD_GPU=OFF`, `LUMORA_BUILD_SAMPLES=OFF`):

- `linux-profile`: RelWithDebInfo build with frame pointers (`-fno-omit-frame-pointer`) for profiling.
- `linux-asan`: RelWithDebInfo build configured with AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`).

## Benchmarks

`LumoraBench` under `Benchmarks/` measures frame workloads against a null canvas (Tick, Layout, Paint) and reports allocation statistics.

CLI flags:

- `--scale N`: Scene scale factor (default: 2000).
- `--frames N`: Number of measured frames (default: 200).
- `--warmup N`: Number of warm-up frames to discard before recording (default: 40).
- `--scene substr`: Filter benchmark scenes by substring match.
- `--font path`: Path to a TrueType font file.
- `--csv`: Emit machine-readable CSV output.
- `--alloc-sites`: (Windows) Report heap allocations still held after warm-up, grouped by call site.

Reports provide:
- Frame times per phase (Tick, Layout, Paint, and Total min/median/p95).
- Allocation rates per frame (count and KiB per frame).
- Heap statistics per node (mount duration, blueprint memory, live memory per node, peak mount memory, and steady-state heap growth).

Median CPU time to produce one frame (tick + layout + paint recording) at the default scale of 2000 nodes on a 1920x1080
surface, RelWithDebInfo MSVC build on an AMD Ryzen 7 5700X3D. GPU submission is not included (null canvas).

| Scene | What changes each frame | Frame time (ms) | Heap per node |
|---|---|---|---|
| static-idle | nothing | 0.018 | 2.6 KB |
| one-leaf-write | one text leaf in a 2000-node tree | 0.75 | 2.7 KB |
| repaint-one-leaf | one fill colour | 0.15 | 2.7 KB |
| hover-transitions | pointer moves across CSS `:hover` transitions | 1.15 | 3.9 KB |
| virtual-list | scrolling a 20000-row list | 0.05 | 1.6 KB |
| text-panel | nothing (200 rows of text labels, real font) | 0.006 | 2.9 KB |

## Samples

Executable samples under `Samples/`:

- **Gallery**: Component showcase demonstrating buttons, toggles, text inputs, lists (scrolling and virtualized), layout splitters, and theme switching.
- **NodeStudio**: Interactive visual node graph editor with draggable nodes, pin connections, inspector panel, palette, and canvas navigation.
- **Pulse**: Real-time observability dashboard displaying streaming traffic charts, animated gauges, heatmaps, live metric grids, and event feeds.
- **Vitals**: Mobile health and wellness interface featuring tab navigation (Today, Activity, Sleep, Profile), bottom navigation, cards, bottom sheets, FAB, and accent color themes.

### Sample Flags

All samples accept standard flags via `Samples/Host`:

- `--validation`: Enables Vulkan validation layers and debug labels.
- `--exit-after N`: Runs the interactive window for `N` frames and exits.
- `--headless-frames N`: Executes `N` frames in headless mode without creating a GPU device or window.

`Vitals` also supports startup flags for scripted testing and screenshots:

- `--tab N`: Initial active tab index (0 to 3).
- `--dark`: Start with dark theme enabled.
- `--accent N`: Accent color palette index (0 to 3).
- `--sheet`: Start with the bottom sheet modal opened.

## Example

Lumora pairs reactive C++ components with CSS stylesheets:

```cpp
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Format.h>
#include <lumora/Widgets/Widgets.h>

using namespace lumora;

Elem CounterCard()
{
    Signal<int> count{0};

    return Column({.className = "card"},
        Text("Counter Card", {.className = "title"}),
        Text(Format("Clicked {} times", count), {.className = "subtitle"}),
        Row({.className = "button-row"},
            Button({.label = "Increment", .onClick = [count] { count += 1; }, .className = "btn"}),
            Button({.label = "Reset",     .onClick = [count] { count = 0;  }, .className = "btn reset"}))
    );
}
```

```css
/* theme.css */
:root {
    --bg-card: #18191f;
    --border-color: #2e303b;
    --text-primary: #ffffff;
    --text-muted: #8b8d98;
    --accent: #4c8dff;
    --accent-hover: #6ca0ff;
}

.card {
    background-color: var(--bg-card);
    border: 1px solid var(--border-color);
    border-radius: 8px;
    padding: 16px;
    gap: 12px;
    transition: transform 0.15s ease-out;
}

.card:hover {
    border-color: var(--accent);
    transform: translateY(-2px);
}

.card > .title {
    color: var(--text-primary);
    font-size: 16px;
    font-weight: bold;
}

.card > .subtitle {
    color: var(--text-muted);
    font-size: 13px;
}

.button-row {
    flex-direction: row;
    gap: 8px;
}

.btn {
    background-color: var(--accent);
    color: #ffffff;
    border-radius: 4px;
    padding: 8px 14px;
    transition: background-color 0.15s ease;
}

.btn:hover {
    background-color: var(--accent-hover);
}

.btn.reset {
    background-color: #323541;
}
```

## ThirdParty

| Library | Version | Role | Source |
|---|---|---|---|
| [FreeType](https://github.com/freetype/freetype) | VER-2-13-3 | Glyph rasterisation for text rendering | Fetched |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | 10.1.0 | OpenType text shaping | Fetched |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | v3.0.0 | Unicode Bidirectional Algorithm implementation | Fetched |
| [libunibreak](https://github.com/adah1972/libunibreak) | libunibreak_6_1 | Unicode line breaking algorithm (UAX #14) | Fetched |
| [Masharif](https://github.com/alielmorsy/Masharif) | v1.2 | Flexbox layout solver | Fetched |
| [libtess2](https://github.com/memononen/libtess2) | v1.0.2 | Polygon tessellation for SVG fills | Fetched |
| [spdlog](https://github.com/gabime/spdlog) | v1.15.1 | Logging library | Fetched |
| [GoogleTest](https://github.com/google/googletest) | v1.15.2 | Unit test framework (test targets only) | Fetched |
| [stb](https://github.com/nothings/stb) | Vendored | Image loading (`stb_image.h`) for renderer texture providers | Vendored |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Vendored | GPU memory allocation management | Vendored |
| [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) | 1.1+ | System dependency for Vulkan loader, headers, and `dxc` shader compiler | System |

Fetched libraries are cloned into `ThirdParty/` at configure time by `cmake/FetchThirdParty.cmake`.

## Documentation

- [CSS Reference](docs/CSS.md): Selectors, cascade, properties, custom properties, animations, and transitions.
- [Text Rendering Notes](docs/TextRendering.md): Font discovery, text shaping pipeline, glyph cache, and atlas packing.
- [Rectangle Rendering Notes](docs/RectRendering.md): Batched primitive rendering, borders, rounded corners, and shader pipeline.
