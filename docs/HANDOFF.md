# Lumora Standalone: Handoff

Date: 2026-10-22
Design: `docs/superpowers/specs/2026-10-22-lumora-standalone-design.md`
Source it came from: `D:\works\DefineEngine\Engine` (never modified)

## State in one paragraph

Lumora builds and runs standalone on Windows with its own `Core` (Base, Log, Platform, slim Vulkan RHI).
No `define` naming remains. All tests are GoogleTest: **512/512 pass** (`ctest -j 8`), the DirectoryWatcher
flake is fixed (20 parallel repeats clean). Four samples run in a real window with Vulkan validation and log
zero errors/warnings. A Release build produces a single shippable exe (static CRT, embedded SPIR-V, needs only
`vulkan-1.dll` from the GPU driver). Android is designed but **not started** (deferred by the user).

## Layout

```
CMakeLists.txt            top level; works as top-level or add_subdirectory (LUMORA_* options)
build.cmd / release.cmd   vcvars64 + Ninja; LUMORA_BUILD_DIR picks the build folder
cmake/                    FetchThirdParty, AddThirdParty, EmbedFiles (SPIR-V -> C++ arrays)
Core/Base                 lumora::base     Api.h, Ref.h, StringId.h, EventBus.h (header only)
Core/Log                  lumora::log      spdlog-backed logger, LOG_* macros, LUMORA_LOG_CATEGORY
Core/Platform             lumora::platform Win32 window/input/clipboard/dialogs + OS (folded OSCore)
Core/RHI                  lumora::rhi      slim Vulkan RHI (see below) + gtest suite
Lumora/                   lumora::lumora   UI library (reactive, widgets, style, layout, graph, svg)
Lumora/Text               lumora::text     FontSystem (freetype, harfbuzz, sheenbidi, unibreak)
Lumora/Renderer           lumora::renderer RHI painter, pipelines, HLSL shaders, embedded SPIR-V
Lumora/Tests              gtest targets (see Tests)
Samples/Host              RunSample(desc, argc, argv): window + RHI + Lumora loop, or headless
Samples/{Gallery,NodeStudio,Pulse,Vitals}
ThirdParty/               vma, stb (vendored), googletest (FetchContent zip)
_engine_ref/              read-only copy of engine RHI + DefineTarget.cmake, reference only, not built
```

Namespaces: `lumora`, `lumora::rhi`, `lumora::platform`, `lumora::os`, `lumora::log`, `lumora::nodegraph`.
Macros: `LUMORA_*`. Graph node ids are `nodegraph::GraphNodeId` (renamed to avoid clashing with `lumora::NodeId`).

## Build and run

```
build.cmd                         Debug into build\ (set LUMORA_BUILD_DIR to change)
build.cmd --target Vitals         one target
release.cmd --target Vitals       Release into build-release\
ctest --test-dir build -j 8       run from an MSVC env; set LUMORA_NO_DIALOGS=1 for unattended runs
```

Sample flags: `--validation` (Vulkan validation), `--exit-after N` (quit after N frames),
`--headless-frames N` (no window; used by the `<Name>.smoke` ctest entries). Vitals also takes
`--tab N --dark --accent N --sheet`.

Toolchain used: VS 2022 Community, Ninja, Vulkan SDK at `E:\unknown\programs\vulkan1` (dxc from it compiles
the HLSL). git is not on PATH inside the vcvars shell, so third party code is vendored or fetched by zip URL.

## Slim RHI (Core/RHI)

- Vulkan 1.1 minimum, linked directly against the loader (no volk), VMA for memory.
- Public headers: `Common.h Flags.h Formats.h Handles.h Descriptions.h CommandBuffer.h RHI.h`. Type names
  match the engine RHI so `Lumora/Renderer` ported with only a namespace change.
- Backend: `Source/Vulkan/{RHI,Resources,Uploads,Descriptors,Pipelines,CommandBuffer,Swapchain}.cpp`,
  shared internals in `VulkanRHI.h` (NativeRHI, generational `Pool<>`), enum mapping in `Translate.h`.
- Fallbacks for Android-class GPUs, both implemented, only the native paths exercised so far (desktop
  RTX 3070 Ti has both features):
  - no `VK_KHR_dynamic_rendering` -> cached VkRenderPass + per-frame framebuffer
  - no `VK_KHR_push_descriptor` -> per-frame descriptor pool, sets allocated in `PushDescriptors`
- Deferred destroy: `NativeRHI::Defer` tags work with the last begun frame number; runs once all frames in
  flight have retired.
- Frame contract: `BeginFrame(swapchain)` always opens the command buffer; always call `EndFrame`, even when
  `swapchainImage` is invalid. Recreate on `swapchainOutOfDate`. Minimized windows keep the old swapchain.
- Fatal Vulkan errors show a message box (suppressed by `LUMORA_NO_DIALOGS`) and log.

## Tests

| Target | Covers |
|---|---|
| lumora_tests | reactive, nodes, widgets, layout, input, text selection, animation, paint, focus, dock |
| lumora_style_tests | parser, matcher, cascade, transitions, keyframes, transforms, borders |
| lumora_graph_tests | node graph model, layout, history, serialize, widget |
| lumora_batch_tests | renderer batch builder, paint geometry (arcs, concave polygons) |
| lumora_platform_tests | window geometry, directory watcher |
| lumora_rhi_tests | RHI init, buffers, textures + uploads, samplers, layouts, pipelines, deferred destroy (GPU, skips without one) |
| lumora_shared_image_tests | renderer image sharing (GPU) |
| `<Sample>.smoke` | each sample headless for 3 frames |
| LumoraBench | benchmark exe, not in ctest |

## What changed vs the engine (beyond renames)

Library fixes made during the port, each with a test:
- **Hidden nodes swallowed clicks**: `view.visible = false` only skipped paint; hit testing ignored it, so a
  hidden full-screen overlay ate every click (Vitals). Fixed in `Input/Dispatch.cpp` `HitChain`.
- **DirectoryWatcher** re-armed `ReadDirectoryChangesW` before parsing its buffer and lost "Added" during
  coalescing. Double-buffered, flake gone.
- **NumberField** ran scrub gestures while disabled.
- **Uploads**: "full mip" region detection (`Extent3D` depth defaults to 1).
- **CommandBuffer**: color attachment given only as a view now resolves; fallback framebuffer sizing; balanced
  debug labels.
- **Swapchain recreate** on a zero-size surface destroyed the old swapchain and left it dangling (crash after
  minimize/restore). Now bails out before teardown.
- **Paint2D::FillPolygon** fanned concave polygons; concave input now goes through libtess2.
- Arc angle docs said CCW; they run clockwise on screen (y-down, like HTML canvas). Docs fixed, behavior kept.

New features:
- CSS per-side borders (`border-top/right/bottom/left` + `-width/-color/-style`, 1-4 value forms).
- CSS `transform: scale()/rotate()` + `transform-origin`, applied as a paint layer (layout unchanged), hit
  testing follows the transform, transitions and keyframes animate it.
- `Tween<T>(target, from, config)` overload that animates on mount.

Six inherited engine test failures were stale tests, not bugs (fixed-step reactive clock clamp, tests not
providing fonts to text selection, scroll content not overflowing); each was updated with a stated reason.

## Samples

| Sample | What | Status |
|---|---|---|
| Gallery | widget + theming showcase, 5 sections | runs clean; widget color/switch fixes **in progress** (see Open work) |
| NodeStudio | node graph editor (palette, canvas, inspector, undo/redo) | runs clean |
| Pulse | heavy real-time dashboard (charts, gauges, feed) | **unfinished** (agent was stopped); builds and runs clean |
| Vitals | Android-style health tracker in a phone-size window | runs clean, clicks verified |

## Open work

1. **In progress when this was written** (agy worker `nodestudio`, task #243): Gallery Switch / Checkbox /
   dark-mode switch rendering, stylable colors for ComboBox, Breadcrumb, Badge, Splitter, Checkbox, and the
   VirtualList doc comment. Review its result: run Gallery light + dark, `ctest`.
2. **Android** (designed, not started): `Core/Platform/Source/Android` with GameActivity + GameTextInput
   (full IME), touch -> pointer events, lifecycle and surface recreation, AAssetManager loader, bundled fonts,
   Gradle sample. Library work: drag-to-scroll with slop + fling, long-press menus, pinch on Graph, IME
   composition events. NDK 28.2 is at `D:\Android\SDK`. Both RHI fallbacks need a real Android 11 device run.
3. Sample exes are console-subsystem (a console window opens). Switch to `WIN32_EXECUTABLE` for shipping.
4. Known limits: CSS transform components always compose translate, rotate, scale (not CSS order);
   `transform-origin` takes keywords and % only; focus rings and anchored popups follow translate only;
   `vkAcquireNextImageKHR` uses an infinite timeout.
5. The always-on-top "white window" report did not reproduce here; the swapchain recreate fix is the likely
   cause. Recheck on the machine that showed it.
6. DefineEngine switch-over to this standalone Lumora (separate task; engine was never edited).
7. Disk: D: is nearly full (~2 GB free). Build folders are large (about 3.5 GB each for a full Debug build).

## Housekeeping (not cleaned up, by design: nothing was deleted without the user asking)

- Worker build folders: `build-gtest`, `build-nodestudio`, `build-rhi-cmd`, `build-rhi-res`, `build-vitals`.
- Strays in the repo root from worker runs: `Gallery.log`, `NumberField.obj`, `vc140.pdb`,
  `cmake-build-relwithdebinfo/`.
- Off-repo build folders left by the Vitals agent: `E:\lumora-build-vitals` and one under the session scratchpad.
- `_engine_ref/` can go once nobody needs the engine RHI for reference.
- agy workers still registered in let-them-talk workspace `lumora`: rhi-res, rhi-cmd, nodestudio, gtest.

## Conventions the user asked for

- Delete over-detailed comments in any file you touch; keep only short ones.
- Never use em dashes, anywhere.
- Never revert, delete or clean up anything without being asked.
- agy workers may build and run tests in their own build folders.
