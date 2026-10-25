# Lumora on the web (WebAssembly + WebGPU)

## Intent

Lumora itself compiles to WebAssembly, so any Lumora app (Gallery, NodeStudio, Pulse, Vitals, user apps) builds for the
browser with the same `lumora_add_sample` / `Main` entry it uses on Windows and Android. Rendering goes through WebGPU.

## Goals

- `cmake --preset web` + `cmake --build --preset web` produces `<Sample>.html/.js/.wasm` for every sample.
- Samples render and are interactive in desktop Chrome/Edge (WebGPU on): mouse, wheel, keyboard, text input, resize,
  devicePixelRatio, clipboard, cursor capture (pointer lock).
- Touch + virtual keyboard on mobile browsers through the same paths Android uses.
- Lumora unit tests (Tests/UI, Tests/Renderer, Platform tests that make sense) build with Emscripten and run under node.
- Desktop and Android behaviour unchanged.

## Non-goals

- WebGL2 fallback.
- Multi-threading on web (no pthreads, no SharedArrayBuffer, no COOP/COEP requirement). The web build is single-threaded.
- Native-only OS services on web: child processes, dynamic libraries, directory watching, crash handler, progress dialog.
  They compile as stubs that report failure.
- System font discovery, emoji/CJK fallback fonts.
- Hosting/deploy pipeline beyond a local static server.

## Anti-goals

- Forking the renderer or UI per platform. RHIPainter, BatchBuilder, Lumora.cpp stay one code path.
- Making the native RHI API worse to fit WebGPU. API additions are allowed only where they are correct on Vulkan too.

## Decided

- Toolchain: emsdk 4.x (installed at `D:\tools\emsdk`), `--use-port=emdawnwebgpu` (webgpu.h C API). CMake preset `web`
  uses `$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`.
- Graphics: new RHI backend `Lumora/Core/RHI/Source/WebGPU/*.cpp` behind the unchanged public RHI headers. CMake picks
  `Source/Vulkan` or `Source/WebGPU` by `EMSCRIPTEN`.
- Shaders: hand-written WGSL next to each HLSL (`Lumora/Renderer/Shaders/<Name>.wgsl`, `VSMain`/`PSMain` entries), embedded
  with the existing `EmbedFiles.cmake` flow (generalised to accept a glob). The web build embeds `.wgsl` instead of `.spv`.
  No tint/naga dependency.
- Threads: web host runs UI update, layout, record and render on the browser main thread from a
  `requestAnimationFrame` loop. `RenderThread` is not used on web.
- Fonts: web builds bundle `ThirdParty/harfbuzz/perf/fonts/Roboto-Regular.ttf` into the Emscripten virtual FS;
  `GetDefaultUiFont()` on web returns its path.

## Design

### RHI public API changes (all backends)

1. `ShaderStageDesc::spirv` becomes `code`: SPIR-V bytes on Vulkan, UTF-8 WGSL on WebGPU. `SpirvLoader`/`spirvLoader`
   keep their names (out of scope to rename).
2. `NativeWindowKind::Web` and `platform::WindowBackend::Web`. On web, `window` is a `const char*` CSS selector of the
   canvas (`"#canvas"`), `instance` null. SampleHost's two-way kind mapping becomes a small helper that handles all three.
3. `RHIResources::FlushMapped(BufferHandle, uint64_t offset, uint64_t size)`. Callers call it after writing through
   `GetMappedPointer`. Vulkan: `vmaFlushAllocation` (correct for non-coherent memory, no-op otherwise). WebGPU: copies
   that range of the CPU shadow into the GPU buffer with `wgpuQueueWriteBuffer`. Call sites: `FrameRings` users in
   `RHIPainter.cpp` (vertex ring, after the two memcpys) and the static quad index buffer.

### WebGPU backend mapping

| RHI concept | WebGPU |
|---|---|
| Init / adapter / device | `wgpuInstanceRequestAdapter`, `RequestDevice`, waited with `wgpuInstanceWaitAny` (needs `-sASYNCIFY`, or JSPI if it builds cleanly; builder's call) so `RHI::Init` stays synchronous. `EnumerateAdapters` returns one adapter when `navigator.gpu` exists. |
| Swapchain | `wgpuInstanceCreateSurface` from `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`, `wgpuSurfaceConfigure` with `wgpuSurfaceGetCapabilities` format (bgra8unorm or rgba8unorm). Recreate = reconfigure. |
| BeginFrame | `wgpuSurfaceGetCurrentTexture`; status other than success sets `swapchainOutOfDate` and returns an invalid image. Opens a `WGPUCommandEncoder`. No CPU frame wait: the browser paces frames. `framesInFlight` still sizes per-frame rings. |
| EndFrame | finish encoder, `wgpuQueueSubmit`. Present happens when control returns to the browser. |
| Barrier / TransitionTexture / layouts | no-ops. |
| BeginRendering/EndRendering | `wgpuCommandEncoderBeginRenderPass` / `End`. |
| Push constants | one uniform buffer ring per frame slot, 256-byte aligned slices, bound as bind group 1 binding 0 with a dynamic offset. Each `PushConstants` call writes the bytes into the CPU shadow of the ring and sets the dynamic offset for the next draw; the ring is uploaded once with `wgpuQueueWriteBuffer` before submit. WGSL declares the block as `@group(1) @binding(0) var<uniform> gPush : PushConstants;`. |
| Push descriptors / descriptor layouts | `DescriptorLayout` → `WGPUBindGroupLayout` at group 0. `CombinedImageSampler` at binding N maps to texture at `2N`, sampler at `2N+1`. `PushDescriptors` builds (and caches by resource handles) a `WGPUBindGroup`. |
| Pipelines | `WGPURenderPipeline` from WGSL modules; pipeline layout = [descriptor layouts..., push-constant layout] (push layout only when `pushConstants` non-empty, at the group index after the sets). Vertex/blend/topology translate 1:1. |
| Buffers | `GpuOnly` → GPU buffer + `CopyDst`. `CpuToGpuMapped` → GPU buffer + CPU shadow; `GetMappedPointer` returns the shadow; `FlushMapped` uploads. `GpuToCpu` unsupported (log, return invalid); nothing in Lumora uses it today. |
| Uploads (Upload/UpdateBlocking) | `wgpuQueueWriteBuffer` / `wgpuQueueWriteTexture` (row pitch padded to 256 where required). |
| CopyBufferToImage | `wgpuCommandEncoderCopyBufferToTexture`. |
| Deferred destroy | release after `framesInFlight` frames, same contract as Vulkan. |
| Debug labels | push/pop debug group. |

### Platform layer (`Lumora/Core/Platform/Source/Web/**`, `Source/OS/Web/**`)

- `WebWindow : Window` over one canvas. Events via `emscripten/html5.h`: mouse, wheel, key down/up, touch, focus/blur,
  resize (window resize + canvas CSS size × `devicePixelRatio`), `DpiChanged` when DPR changes. Key codes map to the same
  `PlatformKeys` values Windows produces (use `KeyboardEvent.code`).
- Text input: a hidden `<input>`/`<textarea>` overlaid on the canvas, focused by `SetTextInputActive(true)`; `input` and
  composition events produce the same `InputEvent` text/IME events Android emits. Gives virtual keyboard on mobile.
- `WaitEvents` / `Wake` no-ops; `PumpEvents` flushes queued events. `ShouldClose` false until `RequestClose`.
- Cursor capture: Pointer Lock API; relative motion from `movementX/Y`.
- Clipboard: `SetClipboardText` → `navigator.clipboard.writeText` (fire and forget). `GetClipboardText` returns the text
  of the most recent `paste` event (the paste event also injects a Ctrl+V so the UI pulls it). Builder may refine.
- File dialogs: return `nullopt` (async browser pickers do not fit the sync API; non-goal for now).
- `GetDefaultUiFont` → bundled Roboto path. `GetUserConfigDir` → `/home/web_user/.config/<app>` in MEMFS.
- OS: `Thread` uses the Posix file (start fails without pthreads; fine). Process, DynamicLibrary, DirectoryWatcher,
  CrashHandler, Environment, Shell get web stubs (`Shell` opens URLs with `window.open`). `Asset` reads from
  `/assets/` in the preloaded FS.
- `EntryPoint.h`: web uses the plain `main` branch. `Main` returns while the rAF loop keeps running
  (`emscripten_set_main_loop` with `simulate_infinite_loop` or `EXIT_RUNTIME=0`).

### Sample host (web)

`SampleHost.cpp` gets a `RunWeb` path under `__EMSCRIPTEN__`: create window + RHI + swapchain + `MakeLumora`, mount, then
a main-loop callback per frame: pump events, dispatch input, `Update`, `Layout`, `RecordFrame` into one `FramePacket`,
`BeginFrame`, transitions (no-ops), `RenderFrame`, `EndFrame`, handle out-of-date by `RecreateSwapchain` with the canvas
size. Same rendering calls `RenderThread` makes, inline. `--exit-after` / headless flags read from `Module.arguments`.

### Build

- `CMakePresets.json`: `web` configure/build presets (Ninja, Release, Emscripten toolchain, `LUMORA_BUILD_BENCHMARKS=OFF`).
  `web-tests` preset or the same preset with tests on, run via `ctest` using node (`CMAKE_CROSSCOMPILING_EMULATOR`).
- `lumora_add_sample` on `EMSCRIPTEN`: executable with `SUFFIX .html`, `--use-port=emdawnwebgpu`, `-sALLOW_MEMORY_GROWTH`,
  `--preload-file` for the font (and the sample's `assets/` if present), a minimal shell HTML
  (`Samples/Host/web/shell.html`: full-window canvas, error text when `navigator.gpu` is missing).
- Third-party (freetype, harfbuzz, spdlog, SheenBidi, unibreak, libtess2, Masharif, stb) must build with emcc; fix flags
  only where they break.
- `scripts/serve-web.py` not needed: `python -m http.server` from the build dir works (no COOP/COEP).

## Verification

1. `cmake --preset web && cmake --build --preset web` builds all samples and tests with no errors.
2. Unit tests pass under node via ctest.
3. Gallery loaded in Chrome via a local server renders the same as the Windows build (screenshot compare by eye), and
   clicking, scrolling, typing in an input, and resizing the window all work. No WebGPU validation errors in the console.
4. Windows `debug` preset still builds and Gallery still runs (Vulkan path unaffected by API changes).

## Left to the builder

- ASYNCIFY vs JSPI for device acquisition.
- Bind group caching policy details, ring sizes.
- Exact key mapping table, IME event plumbing details.
- Whether WGSL lives as one file per shader or per stage (one per shader, two entry points, is the default).
