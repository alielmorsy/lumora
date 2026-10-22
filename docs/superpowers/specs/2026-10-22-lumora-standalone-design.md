# Lumora Standalone - Design

Date: 2026-10-22
Source: `D:\works\DefineEngine\Engine` (read-only - never modified)
Target: `D:\works\Lumora`

## Intent

Lift the Lumora UI library out of DefineEngine into its own project. It keeps
running on Windows (Vulkan) and also runs on Android. It owns its own small
foundation (log, platform, RHI) in `Core/` instead of depending on the engine.

## Goals

- Lumora (UI lib, Text, RHI renderer backend, shaders) builds standalone.
- No `define::` / `Define::` / `define/` anywhere: namespaces, includes, CMake targets.
- `Core/` holds the foundation:
  - `Core/Log` - our own logger.
  - `Core/Platform` - copied from engine `Platform` + `Platform/OSCore`, plus a new Android backend.
  - `Core/RHI` - a slimmed-down Vulkan RHI. Same type names Lumora already uses, none of the engine boilerplate.
- Android path: Lumora app runs on an Android device.
- Tests use GoogleTest instead of the engine's built-in test harness.

## Non-goals

- Engine `Core` (ECS, Reflection, Capsules, ktx, zstd), ImGui, RenderCapture, other RHI backends.
- `gpumem` (GpuAllocator/arenas/RangeAllocator) - replaced by VMA.
- volk / bundled Vulkan headers.
- macOS, Linux, iOS (not requested).

## Anti-goals (failure even if it builds)

- Any edit to `D:\works\DefineEngine`.
- The slim RHI regrowing engine boilerplate (retire queues, submit trackers, arena devices, backend contracts, execution contexts) without a concrete Lumora need.
- Leftover `define` naming.

## Decisions made by the user

- Own log and own platform, both under `Core/`.
- Platform and RHI live in a folder called `Core`.
- VMA instead of gpumem.
- Simplified RHI, Vulkan only.
- Link Vulkan directly: `find_package(Vulkan)` on desktop, NDK `libvulkan` on Android. No volk, no vendored Vulkan headers.
- Tests switched to GoogleTest.
- Team: a Sonnet subagent does the de-`define` pass; two agy workers: one for the gtest migration, one for the Android platform. Both workers also help with copying. Claude leads: structure, slim RHI, integration, review.

- DefineEngine will later depend on standalone Lumora (it replaces the engine copy; no syncing back).
- Android 11 (API 30) and up.
- Full touch scrolling (see Touch).
- Full Android keyboard support, IME included (see Android).
- Samples: new, better examples, not a Sandbox port and not named "sandbox". Done last.

## Consumable by DefineEngine

- Recon: `Lumora/Include`, `Source`, `Text` have zero RHI references; only `Renderer` uses RHI.
  Keep it that way - `lumora::lumora` and `lumora::text` must never depend on `lumora::rhi`.
- The engine will link `lumora::lumora` + `lumora::text` and either keep its own RHI backend or adopt
  `lumora::renderer`. So the slim RHI is Lumora's, not a constraint on the engine.
- Project must work both via `add_subdirectory` (options prefixed `LUMORA_`, no global flags/output dirs
  forced when not top-level, `LUMORA_BUILD_TESTS`/`LUMORA_BUILD_SAMPLES` default ON only when top-level)
  and via `install` + `find_package(Lumora)` export.
- Engine switch-over itself (editing DefineEngine) is a later, separate task.

## Layout

```
D:\works\Lumora\
  CMakeLists.txt              top-level: options, ThirdParty, add_subdirectory(Core, Lumora, Samples)
  cmake\                      small helpers only if needed (no define_add_target port)
  Core\
    Log\       Include\lumora\log\Log.h, Source\           -> target lumora::log
    Platform\  Include\lumora\platform\..., Source\{Common,Windows,Android}\  -> lumora::platform
               (OSCore folded in: Include\lumora\os\...)
    RHI\       Include\lumora\rhi\..., Source\Vulkan\       -> lumora::rhi
  Lumora\
    Include\lumora\...        (was Include\define\lumora\...)
    Source\                   -> lumora::lumora
    Text\                     -> lumora::text
    Renderer\                 (was RHIBackend) -> lumora::renderer
    Shaders\                  HLSL
    Tests\                    gtest
  ThirdParty\                 VMA, masharif, freetype, harfbuzz, SheenBidi, unibreak, libtess2, stb, googletest
  Samples\                    built last; new showcase examples (no "sandbox" naming)
    Desktop\                  Windows host
    Android\                  Gradle project, GameActivity, calls the same CMake
  docs\
```

Namespaces: `lumora`, `lumora::rhi`, `lumora::platform`, `lumora::os`, `lumora::log`
(existing sub-namespaces like `lumora::style`, `lumora::nodegraph` keep their tail).

## What Lumora actually needs (recon)

- From log: `define/log/Log.h` (15 includes).
- From platform: `InputEvents.h`, `SystemMetrics.h` directly; Renderer uses Platform privately.
- From RHI (~30 types): `RHI`, `RHIResources`, `CommandBuffer`, handles (Texture, TextureView, Sampler,
  Buffer, Pipeline, DescriptorLayout), `Format`, `TextureUsage`, `TextureType`, `BufferUsage`,
  `ImageLayout`, `PipelineStage`, `AccessMask`, `TextureBarrier`, `ShaderStageFlags`, `AddressMode`,
  `Filter`, `MipmapMode`, `DescriptorType`, `DescriptorWrite`, `UploadRequest`, `MemoryPattern`,
  `RenderingInfo`, `ColorAttachment`, `LoadOp`, `StoreOp`, `Color`, `Extent`, `Rect`, `IndexType`,
  `EnumerateAdapters`. Headers: `RHI.h`, `Handles.h`, `Formats.h`, `Common.h`, `Flags.h`, `CommandBuffer.h`.
- Engine RHI: Vulkan only, depends on log, OSCore, gpumem.

## Slim RHI

- Public API = the list above, plus device/instance creation, surface + swapchain, frame
  begin/end, and present. Type and function names match the engine so `Lumora/Renderer` ports with
  a namespace rename and few edits.
- Memory via VMA. Handles stay opaque.
- Dynamic rendering (Lumora uses `RenderingInfo`). Target Vulkan 1.1 + `VK_KHR_dynamic_rendering`
  (promoted in 1.3, use core when available) so Android 11 devices qualify.
- Push descriptors: Renderer uses `cmd.PushDescriptors` (`VK_KHR_push_descriptor`). Use it when present;
  otherwise fall back to per-frame descriptor-set allocation from a pool reset each frame. Required
  because many Android 11 Mali/Adreno drivers lack it.
- Swapchain format: pick from what the surface reports (Android usually `R8G8B8A8`, Windows `B8G8R8A8`);
  Renderer must not hardcode `B8G8R8A8`.
- Surface creation is per-platform (Win32 / Android) and takes the native window from `lumora::platform`.

## Android

Note: "no extra dependency" no longer holds - androidx.games (GameActivity, GameTextInput) is pulled
via Gradle, because full keyboard support needs it.

- Min SDK 30 (Android 11).
- Standalone app that owns its window. Uses **GameActivity + GameTextInput** (androidx.games) - required
  by the user's "full Android keyboard support": composition/preedit (CJK), autocorrect, swipe typing,
  IME actions (Done/Next), keyboard show/hide, insets. Embedding in a host app is out of scope.
- Lumora needs a composition (preedit) text event in `InputEvents` and Input/TextEditor support for
  showing/committing preedit text, plus telling the platform caret rect and input type
  (text/number/password/multiline). Desktop Windows gets IME (IMM32) through the same events.
- Platform Android backend: window from `ANativeWindow`, touch → pointer `InputEvents` (multi-touch ids kept), lifecycle (surface lost/recreated → swapchain recreate), `SystemMetrics` (DPI from `AConfiguration`), `UserPaths` (internal data dir), clipboard via JNI.
- Platform features with no Android meaning (CursorLock, FileDialog, ProgressDialog, multi-window) get stubs that return "unsupported".
- Log backend: `__android_log_print`.
- Build: Gradle + AGP `externalNativeBuild` pointing at the same CMake.

- Fonts (Claude's call): bundle an OFL font (Inter + a Noto fallback for CJK) in APK assets, loaded via
  `AAssetManager`; the default file loader gets an asset-backed path on Android.

## Touch (library work in `Lumora/`)

Today scrolling is mouse-wheel only (`Source/Widgets/ScrollCore.h`, `Source/Graph/Input.h`). Full scroll means:
- Drag-to-scroll with touch-slop threshold so taps on children still work (gesture arbitration: a drag
  past slop cancels the child press and captures to the scroller).
- Fling with velocity tracking and decay; overscroll clamp/bounce.
- Applies to Scroll, VirtualList, Grid, Graph (pan); pinch-to-zoom on Graph.
- Long-press → context menu (replaces right-click on touch); no hover dependence for core actions.
- Mouse-wheel and desktop scrollbar behavior unchanged. Gesture logic covered by gtest with synthetic events.

## Shaders

Claude's call: HLSL compiled to SPIR-V with `dxc -spirv` (from the Vulkan SDK) at build time on the
host, embedded as generated byte-array headers. Same binaries for Windows and Android, no runtime
compiler, no asset packaging.

## Tests

- GoogleTest via `FetchContent` (or vendored in ThirdParty), run with `ctest` on desktop.
- Each engine test executable becomes a gtest executable; assertions mapped 1:1 to `EXPECT_*`/`ASSERT_*`.
- Bench executables stay plain executables (not tests).
- Android: tests are not run on device (desktop only).

## Work split and order

1. Claude: create the skeleton (folders, top-level CMake, ThirdParty adds). Plain `add_library`, no `define_add_target`.
2. Copy (both agy workers help): Lumora → `Lumora/`, Platform+OSCore → `Core/Platform`, DefineUtils log → `Core/Log` basis.
3. Sonnet subagent: de-`define` pass over everything copied (namespaces, include paths, CMake targets, macros like `DEFINE_API`).
4. In parallel:
   - Claude: slim RHI in `Core/RHI`, then port `Lumora/Renderer` to it.
   - agy worker A: gtest migration of all tests.
   - agy worker B: Android platform backend (GameActivity, GameTextInput, touch, lifecycle, assets).
5. Claude: touch scrolling/gestures + IME composition events in `Lumora/` (worker A adds their gtests).
6. Claude: integration, review of both workers.
7. Last: new examples (Desktop + Android) showing off widgets, text/IME, scrolling, graph.

## Success

- Desktop: `cmake --build` succeeds on Windows; Desktop sample shows a Lumora UI; all gtest tests pass.
- Android: example APK builds, runs on an Android 11+ device, finger-scrolls lists, and types through
  the system keyboard including composition (e.g. Japanese IME).
- `find_package(Lumora)` from a scratch consumer project works.
- `grep -ri "define" ` finds no `define::`, `Define::`, or `define/` references.

## Left to the builder

- Exact slim RHI header split and internals.
- Logger API shape (keep the engine's macro names under a `LUMORA_` prefix is the expected default).
- Whether OSCore stays a separate target or merges into platform.
