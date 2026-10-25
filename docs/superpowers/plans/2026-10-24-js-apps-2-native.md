# Lumora JS Apps, Phase 2 (Native Modules + Android) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** npm packages (and the app itself) ship C++ and Kotlin native modules that autolink on `npm run dev` (Windows) and `npm run android` (device reload loop).

**Architecture:** A module declares itself in its `package.json` `"lumora"` field. The CLI discovers modules and generates a CMake include plus a registry source, then configures the Lumora source tree (`LUMORA_SOURCE`) with `-DLUMORA_JS_MODULES_CMAKE=...` into a project-local custom host. C++ modules install into JSI through `LUMORA_NATIVE_MODULE`. Kotlin modules use a small DSL. C++ forwards JSON strings over JNI, and the JS runtime builds the module proxy (promises, events). Android apps are generated Gradle projects that reuse `lumora.android.app`.

**Tech Stack:** Phase 1 stack, plus JNI, Kotlin, AGP 9.2.1 / Gradle (existing `Android/`), adb.

**Spec:** `docs/superpowers/specs/2026-10-24-js-apps-design.md`. Phase 1 plan `docs/superpowers/plans/2026-10-24-js-apps-1-core.md` must be complete. Its Interfaces blocks define `JsApp`, `HostTree`, `bundle()`, `dev()`, `resolveHost()`, `RunJsHost`.

## Global Constraints

- Phase 1 constraints apply unchanged.
- Manifest field, exactly:
  `"lumora": { "module": "<PascalName>", "cpp": { "dir": "<rel>", "target": "<cmake target>" }, "android": { "dir": "<rel>", "class": "<fq Kotlin class>" } }`. `cpp` and `android` are each optional, but at least one is required. The app's own package.json may carry the same field.
- Platform choice per module: Android uses `android` if present, else `cpp`. Desktop uses `cpp` only. Missing implementation: `requireNativeModule` throws `Error: Native module "<Name>" has no <platform> implementation`.
- Values crossing JNI are JSON strings. Mark the bridge with `ponytail: JSON over JNI, typed jni calls if call rate matters`.
- `LUMORA_SOURCE` (absolute path to a Lumora checkout) is required for custom hosts and Android until Phase 3. A missing value fails with a message naming the variable.
- Ownership: `Android/**` belongs to the `android-app` peer (offline). Tasks 6, 7 and 8 edit it: post `lead.jsapps.android` on the board first. `Lumora/Core/Platform/**` Android files belong to `android-platform` (offline): Task 5 only reads them.
- Generated files are rewritten only when their content changes, so CMake/Gradle do not rebuild needlessly.

## Review Focus

1. **Duplicate module names across packages** (two packages both declare `Camera`). Expect a hard error naming both packages, not a silent pick. Test in Task 2.
2. **Workspace or symlinked packages** (`npm link`, workspaces). Discovery must follow symlinks (realpath) and dedupe, or the module is missed or linked twice. Test in Task 2.
3. **Async results after reload.** A Kotlin `AsyncFunction` or a C++ invoker job finishing after `JsApp::Unload` must be dropped, not delivered into the new runtime or a dead one. Tests in Tasks 1 and 7.
4. **Kotlin argument type mismatch** (JS passes `"3"` to an `Int` parameter). Expect a JS `TypeError` with module, function, index and types, not a JVM crash. Test in Task 7.
5. **MSVC environment missing** when `lumora dev` builds a custom host from a plain terminal. Expect vcvars found via vswhere, or a clear message. Test in Task 3 with an injected runner.

---

### Task 1: C++ native module API

**Files:**
- Create: `Lumora/Script/Include/lumora/script/NativeModule.h`, `Lumora/Script/Source/NativeModule.cpp`, `Samples/JsHost/NoModules.cpp`
- Modify: `Lumora/Script/Include/lumora/script/JsApp.h`, `Lumora/Script/Source/JsApp.cpp`, `Lumora/Script/Source/Bridge.cpp` (`requireModule`), `Samples/JsHost/JsHost.cpp` (build a registry, call `RegisterLinkedModules`), `Samples/JsHost/CMakeLists.txt`
- Modify: `js/packages/runtime/src/index.js`, `index.d.ts` (`requireNativeModule`)
- Test: `Tests/Script/NativeModuleTests.cpp`, `js/packages/runtime/test/modules.test.js`

**Interfaces:**
- Produces:

```cpp
namespace lumora::script {
using Invoker = std::function<void(std::function<void(facebook::jsi::Runtime&)>)>; // thread-safe; job runs on the JS thread in the next Tick, dropped if the runtime it was made for is gone
struct ModuleContext { facebook::jsi::Runtime& runtime; facebook::jsi::Object& exports; Invoker invoker; };
using InstallFn = void (*)(ModuleContext&);
class LUMORA_API ModuleRegistry {
public:
    void Add(std::string name, InstallFn install); // duplicate: LOG_WARN, first wins
    [[nodiscard]] InstallFn Find(std::string_view name) const;
};
void RegisterLinkedModules(ModuleRegistry& registry); // defined exactly once per host executable
}
#define LUMORA_NATIVE_MODULE(Name) void LumoraModule_##Name##_Install(::lumora::script::ModuleContext& ctx)
```

- `JsApp(MountedApp& app, const ModuleRegistry* modules = nullptr)`. `__lumora.requireModule(name)` installs on first call, caches the exports object per runtime, and returns `undefined` when the name is unknown.
- `@lumora/runtime`: `export function requireNativeModule<T = any>(name: string): T`. Order: `__lumora.requireModule(name)`, then the Kotlin proxy (Task 7, through `__lumora.kotlin`), then throw `Error('Native module "<name>" is not linked into this host. Install the package that provides it and restart the dev command.')`.
- `NoModules.cpp` defines an empty `RegisterLinkedModules`. It is used when `LUMORA_JS_MODULES_CMAKE` is unset.

- [ ] **Step 1: Failing tests.** `NativeModuleTests.cpp` defines a test module with `LUMORA_NATIVE_MODULE(Echo)` (sync `echo(s)`, and `later(s)` returning a Promise resolved from a `std::thread` through `ctx.invoker`):

```cpp
TEST(NativeModule, RequireReturnsExports)      // JS: __lumora.requireModule('Echo').echo('hi') shown in a text -> "hi"
TEST(NativeModule, RequireCaches)              // requireModule twice returns the same object (===) -> text "same"
TEST(NativeModule, UnknownIsUndefined)         // typeof __lumora.requireModule('Nope') === 'undefined'
TEST(NativeModule, InvokerDeliversOnTick)      // later('x') then Tick until resolved (bounded 100 ticks) -> "x"
TEST(NativeModule, InvokerAfterUnloadIsDropped) // call later, Unload, Load other script, Tick -> no crash, new script's text unchanged
```

`modules.test.js`: `requireNativeModule` returns the fake's object; unknown name throws the exact message above.
- [ ] **Step 2: Run to see failure.** `cmake --build --preset debug --target lumora_script_tests` → FAIL.
- [ ] **Step 3: Implement.** The invoker queue is a mutex-guarded vector on `JsApp`, tagged with a runtime generation counter that `Load` bumps.
- [ ] **Step 4: Run.** `build\debug\bin\lumora_script_tests.exe --gtest_filter=NativeModule*` → 5 PASS. `cd js && npm test -w @lumora/runtime` → PASS. `ctest --preset debug -R LumoraJsHost` still PASS.
- [ ] **Step 5: Commit** `git commit -m "Add C++ native module API and requireNativeModule"`

---

### Task 2: Module discovery and desktop link files

**Files:**
- Create: `js/packages/lumora/src/link.js`
- Test: `js/packages/lumora/test/link.test.js`, fixtures `test/fixtures/link/{app,node_modules/...}`

**Interfaces:**
- Produces:
  - `export async function discoverModules(root: string): Promise<ModuleInfo[]>`, where `ModuleInfo = { name, packageName, packageDir, cpp?: { dir, target }, android?: { dir, className } }` with absolute dirs, sorted by `name`. It scans the app's package.json and `root/node_modules/*/package.json` and `root/node_modules/@*/*/package.json`, follows symlinks with `realpath`, and dedupes by real dir. It throws on an invalid name (`^[A-Z][A-Za-z0-9]*$`), a missing dir, a `cpp.dir` without `CMakeLists.txt`, or a duplicate `module` (the message names both packages).
  - `export async function writeDesktopLinkFiles(root: string, modules: ModuleInfo[]): Promise<{ cmakeFile: string, changed: boolean }>`, which writes `<root>/.lumora/native/modules.cmake` and `LinkedModules.gen.cpp`.

`modules.cmake` sets, for each module with `cpp`: `add_subdirectory("<cpp.dir>" "${CMAKE_BINARY_DIR}/lumora_modules/<name>")`. It also sets `LUMORA_JS_MODULE_TARGETS` (list of targets) and `LUMORA_JS_MODULES_SOURCE` (abs path of the `.gen.cpp`). The `.gen.cpp` forward-declares `LUMORA_NATIVE_MODULE(<Name>);` for each module and defines `RegisterLinkedModules` adding each one. Paths use forward slashes.

- [ ] **Step 1: Failing tests:**

```js
test("finds app-local, plain and scoped packages");     // fixture with app lumora field, node_modules/a, node_modules/@x/b -> 3 modules sorted
test("follows symlinks and dedupes");                     // symlink node_modules/c -> ../packages/c twice reachable -> 1 entry with realpath dir
test("duplicate module names fail naming both packages");
test("cpp dir without CMakeLists fails");
test("writes cmake and registry, unchanged second time"); // changed true then false; .gen.cpp contains LUMORA_NATIVE_MODULE(A); and r.Add("A", ...)
test("no modules writes an empty target list");
```

Make the symlinks with `fs.symlink(..., "junction")` so the test runs on Windows without admin rights.
- [ ] **Step 2: Run to see failure.** `npm test -w lumora` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w lumora` → PASS.
- [ ] **Step 5: Commit** `git commit -m "Add native module discovery and desktop link files"`

---

### Task 3: Custom desktop host build

**Files:**
- Modify: `Samples/JsHost/CMakeLists.txt` (if `LUMORA_JS_MODULES_CMAKE`: `include()` it, add `${LUMORA_JS_MODULES_SOURCE}`, link `${LUMORA_JS_MODULE_TARGETS}`, else add `NoModules.cpp`)
- Create: `js/packages/lumora/src/build-host.js`, `js/packages/lumora/src/msvc.js`
- Modify: `js/packages/lumora/src/host.js`, `src/dev.js`
- Create: `js/examples/echo-module/` (package `@lumora-examples/echo-module`, C++ module `Echo` with `echo(s)` and an async `later(s, ms)` through the invoker, `index.js` re-exporting `requireNativeModule("Echo")` with types), `js/examples/native-app/` (depends on it and shows `echo: hi` then `later: hi`)
- Test: `js/packages/lumora/test/build-host.test.js`, extend `test/e2e.test.js`

**Interfaces:**
- Consumes: `discoverModules`, `writeDesktopLinkFiles` (Task 2).
- Produces:
  - `export async function buildHost({ root, lumoraSource, config = "RelWithDebInfo", run = defaultRun }): Promise<string>` returns the `LumoraJsHost` exe path. Binary dir: `<root>/.lumora/host/<platform>-<arch>`. Configure: `cmake -S <lumoraSource> -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=<config> -DLUMORA_BUILD_TESTS=OFF -DLUMORA_BUILD_BENCHMARKS=OFF -DLUMORA_JS_MODULES_CMAKE=<cmakeFile>`. Configure runs only when the binary dir has no `CMakeCache.txt` or the link files `changed`. Build: `cmake --build <dir> --target LumoraJsHost`. `run(cmd, args, { cwd, env })` is injectable for tests.
  - `msvc.js`: `export async function msvcEnv({ run }): Promise<NodeJS.ProcessEnv | null>`. It returns `process.env` if `cl.exe` is on PATH. Otherwise it locates `vswhere.exe` under `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer`, then `VC\Auxiliary\Build\vcvars64.bat`, and captures `cmd /d /s /c "call <vcvars> >nul && set"`. It returns `null` when VS is absent.
  - `resolveHost` new order: `--host`, then `LUMORA_HOST` (when modules with `cpp` exist this logs a warning that they are not in that host), then `buildHost` when `LUMORA_SOURCE` is set, then throw (message names `LUMORA_HOST` and `LUMORA_SOURCE`). `resolveHost` becomes `async`, and `dev` awaits it.

- [ ] **Step 1: Failing tests:**

```js
test("buildHost configures once and rebuilds on link change"); // fake run records calls: first call -> configure+build; second with same modules -> build only; after adding a module -> configure+build
test("buildHost passes the modules cmake path");                // configure args contain -DLUMORA_JS_MODULES_CMAKE=<root>/.lumora/native/modules.cmake (forward slashes)
test("msvcEnv uses PATH cl.exe, then vswhere, else null");      // injected run/fs stubs for all three cases
test("resolveHost prefers flag, then LUMORA_HOST, then source build");
```

e2e (skipped unless `LUMORA_SOURCE` is set): `buildHost` for `js/examples/native-app`, run the host headless with `--dump-text`, and assert lines `echo: hi` and `later: hi`.
- [ ] **Step 2: Run to see failure.** `npm test -w lumora` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w lumora` → PASS. `set LUMORA_SOURCE=D:\works\Lumora && npm test -w lumora` → e2e PASS (first run builds Lumora + Hermes, so allow 30 min).
- [ ] **Step 5: Manual.** In `js/examples/native-app`, `npm run dev` with only `LUMORA_SOURCE` set builds the host, opens the window, and shows both lines. Edit `Echo.cpp` and restart `npm run dev`: the change shows after the incremental build.
- [ ] **Step 6: Commit** `git commit -m "Build custom JS hosts with linked C++ modules"`

---

### Task 4: `lumora create-module`

**Files:**
- Create: `js/packages/lumora/templates/module/{package.json,index.js,index.d.ts,README.md,native/cpp/CMakeLists.txt,native/cpp/{{Name}}.cpp,native/android/build.gradle.kts,native/android/src/main/kotlin/{{pkgPath}}/{{Name}}Module.kt}`
- Modify: `js/packages/lumora/src/cli.js`, `src/create.js` (`createModule`)
- Test: `js/packages/lumora/test/create-module.test.js`

**Interfaces:**
- Produces: `lumora create-module <npm-name> [--module <PascalName>] [--android-package <pkg>]` and `export async function createModule({ name, cwd, module, androidPackage }): Promise<string>`. The defaults derive `module` from the npm name (`@acme/haptics` → `Haptics`) and `androidPackage` = `com.lumora.modules.<lowercase>`. The template's C++ `hello()` returns `"Hello from C++"`, and the Kotlin `Function("hello")` returns `"Hello from Kotlin"`. The `cpp.target` is `lumora_<lowercase>`. `native/cpp/CMakeLists.txt` creates a STATIC target linking `lumora::script`.

- [ ] **Step 1: Failing tests:** create a module in a tmp dir, then `discoverModules` (with the module copied into a fixture app's node_modules) returns it with both `cpp` and `android`. No `{{` remains in any file. Kotlin file path matches the package.
- [ ] **Step 2: Run to see failure.** `npm test -w lumora` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w lumora` → PASS. Manual: add the generated module to `js/examples/native-app` and `npm run dev` shows `Hello from C++`.
- [ ] **Step 5: Commit** `git commit -m "Add lumora create-module template"`

---

### Task 5: Hermes, lumora_script and LumoraJsHost on Android

**Files:**
- Modify: `cmake/deps/hermes.cmake` (Android branch), `Lumora/Script/CMakeLists.txt`, `Samples/JsHost/JsHost.cpp` (Android bundle source)
- Create: `Samples/JsHost/test/android-smoke.md` (the manual check, 10 lines)

**Interfaces:**
- Produces: `LumoraJsHost` builds as the Android `.so` through `lumora_add_sample`. On Android, the bundle is read from `<internalDataPath>/lumora-dev/bundle.js` when that file exists and the build is not `NDEBUG`. Otherwise it is the packaged asset `bundle.js` (`os::ReadAsset`). Only the dev file is polled for changes.

Hermes for Android, in order of preference (record the choice in `hermes.cmake`):
1. The prefab AAR `com.facebook.hermes:hermes-android:250829098.0.16` (check Maven Central for these exact coordinates and that the AAR ships prefab headers including JSI). It is consumed like `game-activity` (`find_package(... CONFIG)`), and the Gradle dependency goes in Task 6's plugin change.
2. A source build of the same ref, with the host `hermesc` taken from the `hermes-compiler@250829098.0.16` npm package through Hermes's import-host-compilers CMake option (check the option name in the pinned source).

- [ ] **Step 1: Failing check.** Configure an Android build of `LumoraJsHost` the way Gradle does (`cmake` with the NDK toolchain, `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=30`). It fails: Hermes not found or not cross-buildable.
- [ ] **Step 2: Implement** the chosen route and the Android bundle source in JsHost.
- [ ] **Step 3: Verify.** The Android configure + `--target LumoraJsHost` builds for `arm64-v8a` and `x86_64`. The desktop `lumora_script_tests` still pass.
- [ ] **Step 4: Commit** `git commit -m "Build Hermes and LumoraJsHost for Android"`

---

### Task 6: Gradle plugin supports JS apps

**Files:**
- Modify: `Android/build-logic/src/main/kotlin/com/lumora/buildlogic/LumoraAndroidAppPlugin.kt`, `Android/lumora-android/build.gradle.kts` (Hermes dependency if Task 5 chose prefab)

**Interfaces:**
- Produces, in `lumora.json`:
  - `entry.js` (path relative to `lumora.json`). It replaces the current hard error. The file is copied to `build/generated/lumora/assets/bundle.js` (a registered assets srcDir) by a task the merge-assets task depends on. When `entry.js` is set, `entry.native` defaults to `"LumoraJsHost"`.
- Produces, Gradle properties:
  - `lumora.root`: absolute path of the Lumora checkout. It is used for the CMake path. When unset, today's repo-relative path stays.
  - `lumora.jsModulesCmake`: when set, adds `-DLUMORA_JS_MODULES_CMAKE=<value>` to the CMake arguments.

- [ ] **Step 1: Failing check.** Add `Samples/JsCheck/lumora.json` with `{"id":"com.lumora.jscheck","label":"JS Check","entry":{"js":"../JsHost/test/hello.js"}}` and `Samples/JsCheck/android/build.gradle.kts` (`plugins { id("lumora.android.app") }`). There is no CMakeLists, so CMake's sample glob skips it, while the Gradle settings scan picks it up. Then `Android\gradlew.bat -p Android :JsCheck:assembleDebug` fails with "JS entry not supported yet". Keep it as the JS packaging check.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Verify.** `assembleDebug` succeeds. `unzip -l` on the APK lists `assets/bundle.js` and `lib/arm64-v8a/libLumoraJsHost.so`. Installing on a device or emulator shows `Count: 1` (hello.js). Gallery and Vitals still assemble.
- [ ] **Step 4: Commit** `git commit -m "Support JS entry apps in the Lumora Android plugin"`

---

### Task 7: Kotlin module DSL and JNI bridge

**Files:**
- Create: `Android/lumora-android/src/main/java/com/lumora/android/modules/{LumoraModule.kt,ModuleDefinition.kt,NativeBridge.kt,Json.kt}`
- Modify: `Android/lumora-android/src/main/java/com/lumora/android/LumoraActivity.java` (`NativeBridge.install(this)` before `super.onCreate`), `Android/lumora-android/build.gradle.kts` (Kotlin plugin, `testImplementation("junit:junit:4.13.2")`, `testImplementation("org.json:json:20250517")`)
- Create: `Lumora/Script/Source/Android/KotlinBridge.cpp` (JNI both ways, result/event queue drained by `JsApp::Tick`)
- Modify: `js/packages/runtime/src/index.js` (Kotlin proxy in `requireNativeModule`)
- Test: `Android/lumora-android/src/test/java/com/lumora/android/modules/ModuleDefinitionTest.kt`, `js/packages/runtime/test/kotlin-proxy.test.js`

**Interfaces:**
- Produces (Kotlin, package `com.lumora.android.modules`):

```kotlin
abstract class LumoraModule {
    lateinit var appContext: android.content.Context
        internal set
    abstract fun definition(): ModuleDefinition
    fun sendEvent(name: String, payload: Any? = null) // name must be declared in Events(...)
}
fun ModuleDefinition(block: ModuleDefinitionBuilder.() -> Unit): ModuleDefinition
class ModuleDefinitionBuilder {
    fun Name(name: String)
    fun Events(vararg names: String)
    // Function / AsyncFunction overloads for 0..4 reified parameters; AsyncFunction bodies run on a shared cached thread pool
}
object NativeBridge {
    @JvmStatic fun install(activity: android.app.Activity)                 // loads com.lumora.generated.LumoraGeneratedModules.all by reflection; absent class = no modules
    @JvmStatic fun describe(module: String): String?                       // {"functions":[{"name":"x","async":false}],"events":["e"]} or null
    @JvmStatic fun call(module: String, fn: String, argsJson: String): String // {"ok":true,"value":...} | {"ok":false,"error":"...","type":"TypeError"|"Error"}
    @JvmStatic fun callAsync(module: String, fn: String, argsJson: String, callId: Int)
    @JvmStatic external fun resolve(callId: Int, resultJson: String)       // same envelope as call
    @JvmStatic external fun emit(module: String, event: String, payloadJson: String)
}
```

- Supported parameter types: `String Int Long Double Float Boolean List<*> Map<String, *>` and their nullable forms. A mismatch gives `{"ok":false,"type":"TypeError","error":"<Module>.<fn>: argument <n> expected <Type>, got <json type>"}`. Return values: `null Unit Boolean Number String List Map JSONObject JSONArray`. Anything else is an `Error`.
- C++ (`__lumora.kotlin`, installed only on Android): `describe(name)`, `call(m, f, argsJson)`, `callAsync(m, f, argsJson, id)` forward to `NativeBridge` with JNI. `resolve` and `emit` push onto a mutex queue tagged with the runtime generation. `Tick` drains it into `globalThis.__lumoraNativeResult(id, json)` / `__lumoraNativeEvent(module, event, json)` and drops stale generations.
- JS proxy: `requireNativeModule(name)` builds `{ [fn]: (...args) => value | Promise, addListener(event, cb): { remove() } }` from `describe`, using `JSON.stringify` for args and `JSON.parse` for results. An error envelope becomes a thrown or rejected `TypeError`/`Error` with that message.

- [ ] **Step 1: Failing tests.** JVM unit tests (no device), plus JS proxy tests against a fake `__lumora.kotlin`:

```kotlin
@Test fun describesFunctionsAndEvents()     // Name("Dev"); Function("a"){1}; AsyncFunction("b"){x: Int -> x}; Events("e") -> describe JSON matches
@Test fun convertsArgumentsAndReturn()      // Function("add"){a: Int, b: Double -> a + b}; call("[1, 2.5]") -> {"ok":true,"value":3.5}
@Test fun typeMismatchIsTypeError()         // call add with ["3", 1] -> type TypeError, message "Dev.add: argument 1 expected Int, got string"
@Test fun nullableAndCollections()          // (String?, List<*>, Map<String,*>) round trip
@Test fun asyncResolvesThroughCallback()    // inject a resolver instead of the external fun; callAsync -> resolver gets envelope with value
@Test fun undeclaredEventThrows()
```

```js
test("sync call parses value");  test("async call resolves and rejects");  test("type error surfaces as TypeError");
test("addListener receives events and remove() stops them");  test("results for unknown call ids are ignored");
```

- [ ] **Step 2: Run to see failure.** `Android\gradlew.bat -p Android :lumora-android:testDebugUnitTest` → FAIL. `npm test -w @lumora/runtime` → FAIL.
- [ ] **Step 3: Implement.** For JNI from the UI thread, use `lumora::platform::android::Env()` (it exists in `AndroidApp.h`). The `external fun`s are `JNIEXPORT` functions named `Java_com_lumora_android_modules_NativeBridge_resolve/emit`.
- [ ] **Step 4: Run** both commands → PASS.
- [ ] **Step 5: Commit** `git commit -m "Add Kotlin native modules and JNI bridge"`

---

### Task 8: `lumora android` with device reload

**Files:**
- Create: `js/packages/lumora/src/android.js`, `js/packages/lumora/src/adb.js`, `js/packages/lumora/templates/android/{settings.gradle.kts,gradle.properties,app/android/build.gradle.kts}`
- Modify: `src/link.js` (`writeAndroidLinkFiles`), `src/cli.js`, `templates/app/package.json` (add `"android": "lumora android"`)
- Create: `js/examples/native-app` gets a Kotlin module `Device` (`model()` returns `Build.MODEL`, async `wait(ms)`)
- Test: `js/packages/lumora/test/android.test.js`

**Interfaces:**
- Consumes: `bundle`, `discoverModules`, the Task 6 plugin properties, Task 7 `LumoraGeneratedModules` contract.
- Produces:
  - `export async function writeAndroidLinkFiles(root, modules, outDir): Promise<{ changed: boolean }>` writes `<outDir>/app/gen/kotlin/com/lumora/generated/LumoraGeneratedModules.kt` (`object LumoraGeneratedModules { @JvmField val all: List<LumoraModule> = listOf(<className>(), ...) }`) and the module `include`s for settings.
  - `export async function android({ root, lumoraSource, device, run = defaultRun, signal }): Promise<number>`. Project dir: `<root>/.lumora/android/`.
    - It generates `settings.gradle.kts`, which mirrors `Android/settings.gradle.kts`: build-logic via `includeBuild("<lumoraSource>/Android/build-logic")`, `:lumora-android` from the source tree, one `:lumora-module-<name>` per module with `android`, and `:app` at `app/`. It copies the version catalog and the Gradle wrapper from `<lumoraSource>/Android`.
    - `gradle.properties` holds `lumora.root` and `lumora.jsModulesCmake`.
    - `app/lumora.json` is built from the app package.json's optional `"lumora": { "app": { id, label, orientation, permissions } }`. Defaults: `id = com.lumora.app.<sanitized name>`, `label = name`. `entry.js` is `bundle.js`.
    - The steps, in order: dev bundle to `app/bundle.js`, `gradlew :app:installDebug` (`ANDROID_HOME`/`JAVA_HOME` checked first, with a clear error), then `adb shell am start -n <id>/com.lumora.android.LumoraActivity`.
    - It then watches. Each good rebuild runs `adb push app/bundle.js /data/local/tmp/lumora-<id>.js` and `adb shell run-as <id> sh -c "mkdir -p files/lumora-dev && cp /data/local/tmp/lumora-<id>.js files/lumora-dev/bundle.js"`.
    - It streams `adb logcat` filtered to the app pid. `--device <serial>` adds `-s <serial>` to every adb call.
- [ ] **Step 1: Failing tests** with the injected `run`:

```js
test("generates gradle project with modules and properties"); // settings includes :lumora-module-device with its dir; gradle.properties has lumora.root; app/lumora.json id default
test("LumoraGeneratedModules lists android classes only");
test("runs install, start, then pushes on rebuild");             // recorded commands in order; after editing App.tsx, push + run-as cp recorded
test("missing ANDROID_HOME fails with a clear message");
test("--device adds -s to every adb call");
```

- [ ] **Step 2: Run to see failure.** `npm test -w lumora` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w lumora` → PASS.
- [ ] **Step 5: Manual acceptance (the Phase 2 goal):**
  1. With a device attached and `LUMORA_SOURCE`, `ANDROID_HOME`, `JAVA_HOME` set: in `js/examples/native-app`, `npm run android` installs and opens the app. It shows `echo: hi` (C++ module on Android) and the device model (Kotlin module).
  2. Edit `App.tsx` and save. The app reloads on the device within about 2 s with no reinstall.
  3. A Kotlin `AsyncFunction` that sleeps 3 s, then save during the sleep: no crash, and no stale result shows up in the new load.
- [ ] **Step 6: Commit** `git commit -m "Add lumora android with device reload"`
