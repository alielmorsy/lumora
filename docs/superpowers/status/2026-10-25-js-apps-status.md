# Lumora JS apps: status (2026-10-25)

Spec: `docs/superpowers/specs/2026-10-24-js-apps-design.md`.
Plans: `docs/superpowers/plans/2026-10-24-js-apps-1-core.md` and `docs/superpowers/plans/2026-10-24-js-apps-2-native.md`.
Everything lives on branch `feat/js-native`, which contains `feat/js-apps` (Phase 1) and `feat/js-cli`. Nothing is on `main` or pushed yet.

## Done

### Phase 1: core (Windows desktop), all 9 tasks

| # | Task | Commits | Evidence |
|---|---|---|---|
| 1 | Inline CSS (`inlineCss` on Box/Text/Input/Scroll/Image, `className` on Image) | 6d3b7fc | style tests 88/88, reviewed |
| 2 | `HostTree`: imperative node API, `MountedApp::Host()` | be5e041 | 14 HostTree tests, lumora_tests 227/227, reviewed |
| 3 | Hermes `hermes-v250829098.0.16` (ExternalProject) + `lumora_script` | 907af18, f532517 | smoke 5/5 Debug + Release, no CRT mismatch, reviewed |
| 4 | `JsApp` + `__lumora` bridge | 5634ad7 | lumora_script_tests 17/17 |
| 5 | `LumoraJsHost` exe with bundle file-watch reload, `SampleDesc::onFrame` | e66926e | ctest LumoraJsHost 3/3, all smoke 5/5 |
| 6 | npm workspace `js/` + esbuild/babel-preset-solid bundler | e264e84 | 5 tests, reviewed |
| 7 | `@lumora/runtime`: Solid universal renderer, props, text, timers, JSX types | ae075cb, 7da85ac | 14+ tests, tsc type check, reviewed |
| 8 | CLI `lumora create` / `lumora dev` | 6b33bb7 | lumora 21/21, real Node 20 run |
| 9 | `js/examples/hello` + end-to-end test on the real host | 48c192a | e2e 2/2 with `LUMORA_HOST` |

Try it: build `LumoraJsHost` (`scripts\build.cmd --target LumoraJsHost`), set `LUMORA_HOST` to
`build\debug\bin\LumoraJsHost.exe`, then run `npm run dev` in `js/examples/hello` (run `npm install` in `js/` first).

### Phase 2: native modules (desktop + JS parts)

| # | Task | Commits | Evidence |
|---|---|---|---|
| 1 | C++ module API (`LUMORA_NATIVE_MODULE`, `ModuleRegistry`, invoker, `requireNativeModule`) | 9474867 | lumora_script_tests 22/22, runtime 16/16 |
| 2 | Module discovery + desktop link files (`link.js`) | 804d66b | lumora 28/28 |
| 3 | Custom desktop host build: `buildHost`, vswhere/vcvars `msvcEnv`, async `resolveHost` (`--host`, then `LUMORA_HOST`, then `LUMORA_SOURCE` build), `LUMORA_JS_MODULES_CMAKE`, examples `echo-module` + `native-app` | see git log | lumora 49/49 (+3 e2e with real host: `echo: hi`, `later: hi`) |
| 4 | `lumora create-module` template (C++ + Kotlin + TS) | 16d407d | lumora 34/34; templates not compiled yet |
| 8 | `lumora android`: Gradle project generation, install/launch, adb push loop (JS side) | f36ce9e | lumora 42/42, fake runner only |

Per-task reviews ran for Phase 1 Tasks 1, 2, 3, 6 and 7. The user stopped reviews after that, so the other tasks are
accepted on the implementers' own test evidence.

## Left

Try native modules: `set LUMORA_SOURCE=<this checkout>` and run `npm run dev` in `js/examples/native-app`. The first
run builds Lumora + Hermes into `.lumora/host/`; later runs are incremental.

### Dropped for now: Android

- Phase 2 Task 5: Hermes + `lumora_script` + LumoraJsHost cross-built for Android (prefab AAR or source build with
  host `hermesc`). `cmake/deps/hermes.cmake` does not forward the toolchain yet (`ponytail:` note).
- Phase 2 Task 6: Gradle plugin `entry.js`, `lumora.root` / `lumora.source` / `lumora.jsModulesCmake` properties.
- Phase 2 Task 7: Kotlin `LumoraModule` DSL, `NativeBridge`, JNI glue, Kotlin proxy in `requireNativeModule`.
- Device acceptance of `lumora android` (Task 8 has only been run against a fake command runner).
- Known follow-ups when resuming:
  - Android reuses the desktop `modules.cmake`, so a module with both `cpp` and `android` links its C++ too. Android must use Kotlin when it is present.
  - The generated `gradle.properties` carries `lumora.source`. The plugin must read it instead of `rootDir.parentFile`.
  - The generated app `build.gradle.kts` (AGP 9 DSL) is unverified.
  - The module template hard-codes compileSdk 34 / minSdk 26. The repo uses 36.1 / 30.

### Needs a human

- Native-app window check: `npm run dev` in `js/examples/native-app` with `LUMORA_SOURCE` set shows both lines, and editing `Echo.cpp` then restarting rebuilds incrementally.
- Phase 1 window checks:
  - The counter renders and clicks.
  - Save reloads in under 2 s.
  - A syntax error keeps the last good app.
  - A throwing click handler shows red error text and recovers after a fix.

### Recommended before merging to main

- One whole-branch code review. Phase 1 Tasks 4, 5, 8, 9 and all of Phase 2 were not independently reviewed.
- Merging to `main` is blocked by 3 untracked, byte-identical copies of the spec and plans in `D:\works\Lumora`. They
  must be removed (user's call) before git will merge.

### Phase 3 (outlined in the spec, no plan yet)

- Prebuilt hosts on npm (`@lumora/host-win32-x64`) so JS-only apps need no C++ toolchain, and `@lumora/sdk`
  replacing `LUMORA_SOURCE`.
- `lumora build`: Hermes bytecode via the `hermes-compiler` npm package, Windows folder/zip, Android AAB.
- Web target with an Embind `__lumora`.
- npm publishing: `files` fields, template `.gitignore` survives `npm pack`, publish CI. Only after this does
  `npx lumora@latest` work for outside users.

### Other known gaps

- `JsApp` is deliberately leaked on window close, and `--dump-text` needs a frame count. The fix is a
  `SampleDesc::onExit` hook once `feat/render-thread-host` merges (Samples/Host is owned by that branch).
- Linux: `LUMORA_BUILD_SCRIPT` is OFF in the Linux presets, and `hermes.cmake` stops configure on non-Windows hosts.
- `<input>` only supports the `input`, `submit` and `blur` events (keydown and focus warn and are ignored).
- Releasing a handle that `Clear()` already freed logs "invalid handle", once per node on each reload.
- The tsc type check for `@lumora/runtime` is not wired to an npm script.
- A stale `C:\Users\Ali Elmorsy\.pnp.cjs` breaks esbuild resolution for projects under the home directory.
- Minor items deferred from reviews:
  - Task 1: the ApplyLayout min/max fallback changed with no test.
  - Task 6: the `.tsx` babel error mapping is untested, and so is prod mode.
  - Task 7: several edge-case tests are missing.
  - Task 2: `KeyName` lives in HostTree.cpp, and `HostTreeTestAccess` ships in the library.
  - Task 3: `CMAKE_CXX_FLAGS` are not forwarded to Hermes.

## Decisions made on the user's behalf

- Parallel tracks in separate worktrees; plain commits with no Claude trailers.
- Hermes pinned at `250829098.0.16`, although RN stable now names `.0.17`.
- Hermes is built as an ExternalProject, because its bundled llvh creates `gtest` targets that clash. A forced-include
  shim fixes MSVC's `__builtin_expect`, and no Hermes source is patched.
- The JS timer clock is load-relative: `JsApp` passes ms since the current load.
- Creation-only props (`multiline`, `axis`) work because the runtime links nodes natively only once their subtree is
  attached to the root.
- `JsApp` clears all handlers before destroying the runtime, and catches every `jsi::JSIException` (syntax errors
  included).
- The Linux script build is off rather than untested link code.
- `SampleDesc::onExit` is deferred (see Other known gaps).
