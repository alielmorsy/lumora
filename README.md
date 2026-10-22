# Lumora

A reactive, GPU-rendered C++20 GUI library. Components and signals on top, a CSS subset for styling,
flexbox layout, and a slim Vulkan renderer underneath.

- **Reactive**: `Signal`, `Computed`, `Effect`, `Each`, `Show` with fine-grained updates.
- **Styled with CSS**: selectors, cascade, variables, media queries, transitions, keyframes, transforms.
- **Widgets**: buttons, inputs, text editor, lists, virtual lists, grids, docking, menus, node graph editor, SVG.
- **Text**: FreeType + HarfBuzz shaping, bidi, line breaking.
- **Renderer**: batched Vulkan 1.1 painter, embedded SPIR-V, single static exe in Release.

## Layout

```
Lumora/
  Core/Base       lumora::base      Ref, StringId, EventBus (header only)
  Core/Log        lumora::log       spdlog-backed logging
  Core/Platform   lumora::platform  windowing, input, clipboard, dialogs, OS services
  Core/RHI        lumora::rhi       slim Vulkan RHI
  UI              lumora::lumora    reactive core, widgets, style, layout, graph, svg
  Text            lumora::text      fonts and shaping
  Renderer        lumora::renderer  RHI painter and shaders
Tests/            GoogleTest suites, mirrors Lumora/
Benchmarks/       LumoraBench
Samples/          Gallery, NodeStudio, Pulse, Vitals
cmake/            build helpers; cmake/deps holds one file per third-party library
ThirdParty/       vendored and fetched third-party sources
scripts/          build.cmd, release.cmd
docs/             CSS reference, text and rect rendering notes
```

## Build

Requires CMake 3.28+, Ninja, a C++20 compiler and the Vulkan SDK (dxc compiles the HLSL shaders).

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

On Windows, `scripts\build.cmd` and `scripts\release.cmd` set up the MSVC environment first.
Output goes to `build/<preset>`.

## Using it from another project

```cmake
add_subdirectory(path/to/Lumora)
target_link_libraries(app PRIVATE lumora::lumora lumora::text lumora::renderer)
```

Tests, samples and benchmarks are off when Lumora is not the top-level project
(`LUMORA_BUILD_TESTS`, `LUMORA_BUILD_SAMPLES`, `LUMORA_BUILD_BENCHMARKS`).
