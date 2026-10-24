# Lumora JS apps (Hermes + Solid-style TSX + npm native modules)

## Intent

People build Lumora apps the way they build web or React Native apps: `npx lumora@latest` scaffolds a project,
`npm run dev` opens it in a window that reloads on save, the UI is TSX, and libraries come from npm. A library can ship
native code: C++ (any platform) and Kotlin (Android). An app can carry its own native code the same way. Lumora's C++
engine (layout, CSS, text, GPU renderer) stays the thing that draws; JS only decides what to show.

## Goals

- `npx lumora@latest my-app` creates a runnable project. `cd my-app && npm run dev` opens a desktop window on Windows
  and reloads the JS on every save, keeping the window open.
- UI is TSX with a React-like look: function components, props, `class`, `style`, `onClick`. Reactivity is Solid's
  model (`createSignal`, `<Show>`, `<For>`). Small learning curve for React developers.
- Signal changes reach native properties directly. No virtual DOM diff in JS, no second reactive tree in C++.
- npm packages declare native code in `package.json` (`"lumora"` field). Installing one and re-running `npm run dev`
  links it with no manual steps (autolinking).
- C++ native modules use JSI directly. Kotlin modules use a small Expo-style DSL.
- Same app runs on Android (`npm run android`) with the same reload loop.

## Non-goals

- Running real React or React-ecosystem libraries (react-query, MUI, zustand-react).
- iOS, macOS, Linux desktop (Lumora has no GPU backend there today).
- State-preserving HMR. Reload restarts the JS (full reload, same window).
- C++ native modules on the web build (web gets JS-only modules).
- Codegen of typed native bindings. Module authors write their own `.d.ts`.
- Static Hermes / AOT native compilation of JS.

## Anti-goals

- A JS-side diffing layer or a mirror of Lumora's reactive graph in JS.
- Forking Lumora's UI per JS or per platform. The JS path builds on the existing widgets, CSS engine and runtime.
- Requiring a C++ toolchain for apps that have no native code, once Phase 3 ships prebuilt hosts.

## Decided (all are the builder's calls; the user delegated the how)

- **Engine:** Hermes V1, the version React Native 0.86/0.87 ship (`250829098.0.16` per reactnative.dev/releases/branches),
  built from source with `lumora_fetch_thirdparty`, consumed through JSI (`jsi/jsi.h`). Desktop + Android. The web build
  runs the same bundle on the browser's own JS engine (Phase 3).
- **UI layer:** `solid-js/universal` `createRenderer`, with JSX compiled by `babel-preset-solid`
  `{ generate: "universal", moduleName: "@lumora/runtime" }`.
- **Bridge:** one global object `__lumora` installed into the JS runtime. JS calls it; C++ never walks JS objects.
- **Imperative tree in C++:** new public `lumora::HostTree` in the UI library. JS nodes map 1:1 onto live Lumora
  nodes built from the existing widgets. Each prop JS can change is a `Signal` feeding the existing widget `Reactive<T>` props, so
  the existing bind/patch code does the work.
- **Styling:** CSS. `class` plus a new per-node inline style (`style` attribute, string or object). App CSS is
  imported from TSX (`import "./app.css"`). The bundler collects the files, and `render()` passes them to
  `__lumora.setStylesheet` before the first node is created.
- **Elements (intrinsics):** `view`, `text`, `button`, `input`, `image`, `scroll`.
- **Bundler:** esbuild, with a small onLoad plugin that runs Babel (`babel-preset-solid` + `@babel/preset-typescript`)
  on `.tsx/.jsx`. Output is one IIFE file. solid-js resolves with the `browser` condition (the node condition loads the
  non-reactive SSR build).
- **Dev loop:** the CLI bundles in watch mode and launches the host with `--bundle <file>`. The host polls the
  file's mtime and fully reloads the JS when it changes. Android: the CLI pushes the bundle into the app's files dir with
  `adb` + `run-as`; the debug host prefers that file over the packaged asset.
- **Host:** `Samples/JsHost` (`LumoraJsHost` target), built with `lumora_add_sample` so desktop, Android and web
  packaging come free. Its logic lives in `lumora_js_host` (static) so custom hosts with native modules relink it.
- **JS packages** live in `js/` as npm workspaces, plain ESM JavaScript with hand-written `.d.ts` (no TS build step):
  `lumora` (CLI, `bin: lumora`, templates) and `@lumora/runtime` (renderer, JSX types, Solid re-exports). Tests use
  `node:test`. Node >= 20.
- **Native module manifest** in a package's (or the app's own) `package.json`:
  ```json
  "lumora": {
    "module": "Haptics",
    "cpp": { "dir": "native/cpp", "target": "lumora_haptics" },
    "android": { "dir": "native/android", "class": "com.acme.haptics.HapticsModule" }
  }
  ```
  On Android a module with `android` uses Kotlin; otherwise `cpp` is used. JS: `requireNativeModule("Haptics")`.
- **Custom host builds** (an app with C++ modules) configure the Lumora source tree with
  `-DLUMORA_JS_MODULES_CMAKE=<generated file>` into the project's `.lumora/host/` dir. Until Phase 3 the Lumora
  source comes from `LUMORA_SOURCE`.
- **Timers:** `setTimeout/setInterval/clearTimeout/clearInterval/queueMicrotask/requestAnimationFrame` are
  implemented in the runtime JS and driven by one native call per frame (`__lumoraTick(nowMs)`). Resolution is one
  frame.

## Phases

1. **Core (plan `2026-10-24-js-apps-1-core.md`):** HostTree, inline CSS, Hermes, `lumora_script`, JsHost, `@lumora/runtime`,
   bundler, `lumora create`, `lumora dev` on Windows. JsHost found via `--host` / `LUMORA_HOST`.
2. **Native modules + Android (plan `2026-10-24-js-apps-2-native.md`):** C++ module API, autolinking, custom
   desktop host build, `lumora create-module`, Hermes on Android, Gradle `entry.js`, Kotlin module DSL + JNI bridge,
   `lumora android` with device reload.
3. **Ship (plan to be written after Phase 2):** prebuilt hosts on npm (`@lumora/host-win32-x64`, Android AAR), an
   `@lumora/sdk` source package replacing `LUMORA_SOURCE`, `lumora build` (Hermes bytecode via the `hermes-compiler`
   npm package, Windows folder/zip, Android AAB), web target with an Embind `__lumora`, npm publish CI.

## The `__lumora` contract (JS <-> C++)

| Call | Meaning |
|---|---|
| `root(): Node` | the app's root node |
| `createNode(tag: string): Node` | new detached node; unknown tag throws `TypeError` |
| `insert(parent, child, before: Node \| null)` | link or move `child` before `before` (append when null) |
| `remove(parent, child)` | unlink; the node stays alive while JS holds it |
| `setProp(node, name: string, value: string \| number \| boolean \| null)` | `null` resets to default |
| `setHandler(node, event: string, fn \| null)` | event names: `click dblclick press release move enter leave scroll focus blur keydown keyup input submit` |
| `setStylesheet(css: string)` | replaces the app stylesheet |
| `log(level: 0..3, msg: string)` | console backend (debug, info, warn, error) |
| `requireModule(name: string): object \| undefined` | Phase 2 |
| global `__lumoraTick(nowMs)` | defined by the runtime, called by the host once per frame |

`Node` is an opaque host object. When JS drops the last reference and the node is detached, C++ frees it.

Props per tag: all tags take `class`, `style`. `text`: `text`. `button`: `disabled`. `input`: `value`, `placeholder`,
`disabled`, `multiline` (creation-only). `image`: `src`, `fit` (`fill|contain|cover|none`). `scroll`: `axis`
(`vertical|horizontal|both`, creation-only). Creation-only props are read when the node is first inserted; later writes
log a warning and are ignored.

## Left to the builder

- How `MountedApp` exposes its `AppCtx` to `HostTree` (a `MountedApp::Host()` accessor is the suggested shape).
- Error overlay look (red text on the root is enough).
- Exact Hermes CMake target names and options for the pinned tag.
