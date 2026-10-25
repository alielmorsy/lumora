# Lumora JS Apps, Phase 1 (Core) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A TSX app created with `npx lumora create hello` runs in a Lumora window on Windows through `npm run dev` and reloads on save.

**Architecture:** Solid's universal renderer (JS) issues imperative node ops to a global `__lumora` object. C++ `lumora_script` implements it on Hermes/JSI on top of a new public `lumora::HostTree`, which maps each JS node onto a live Lumora widget whose changeable props are `Signal`s. `LumoraJsHost` (a `lumora_add_sample` exe) loads the bundle and reloads it when the file changes. The `lumora` npm CLI scaffolds, bundles (esbuild + babel-preset-solid) and launches the host.

**Tech Stack:** C++20, CMake/Ninja, GoogleTest, Hermes V1 + JSI, Node >= 20, esbuild, Babel, solid-js, node:test.

**Spec:** `docs/superpowers/specs/2026-10-24-js-apps-design.md` (read it first, especially "The `__lumora` contract").

## Global Constraints

- Hermes V1 `250829098.0.16`, fetched via `lumora_fetch_thirdparty` (git tag or branch for that version).
- `solid-js@^1.9.9`, `babel-preset-solid@^1.9.9`, `esbuild@^0.25.10`, `@babel/core@^7.28.4`, `@babel/preset-typescript@^7.27.1`, `typescript@^5.9` (template devDependency only).
- JS packages are plain ESM `.js` + hand-written `.d.ts`. No TS build step, no test framework beyond `node:test` + `node:assert/strict`.
- New C++ public symbols carry `LUMORA_API`. Libraries stay STATIC. Match surrounding naming (`PascalCase` methods, `m_` members) and comment density.
- No em dashes in code comments or docs.
- Commits: plain messages, no `Co-Authored-By` or `Claude-Session` trailers.
- `LUMORA_BUILD_SCRIPT` option, default `ON` unless `EMSCRIPTEN`. Everything Hermes-related is behind it.
- Ownership: `Samples/Host/**` belongs to the `render-thread-host` peer (active on `feat/render-thread-host`). Task 5 touches `SampleHost.h/.cpp`; post the change on the board as `lead.jsapps.samplehost` before starting and rebase onto that branch if it merges first.
- C++ build/test commands run from a VS 2022 x64 developer shell at repo root (`scripts/build.cmd` shows the vcvars call). Binaries land in `build/debug/bin/`.

## Review Focus

1. **solid-js resolving to its SSR build.** With the `node` condition esbuild picks `solid-js/dist/server.js`, which never re-runs effects: the UI renders once and never updates. Task 6 pins `conditions: ["browser"]` and tests that a signal write reaches `setProp`.
2. **Node lifetime vs JS GC.** Solid moves nodes with `insert` without a `remove` first, and can re-insert a removed node. The node must survive while JS holds it, even when detached, and must be freed once both are gone. Tests in Tasks 2 and 4.
3. **JS exceptions at load, in a handler, or in a timer.** The host must not crash. It shows the error and recovers on the next save. Tests in Tasks 4 and 5.
4. **Reload leaks.** 20 reloads must leave arena `LiveCount()` and `HostTree::HandleCount()` where one load leaves them. Test in Task 4.
5. **Non-ASCII text across JSI** (`"héllo 世界 👋"`) and **timers from a previous load** firing after reload. Tests in Task 4.

---

### Task 1: Inline CSS on widgets

Per-node CSS declarations that beat stylesheet rules (except `!important`), so JS `style` works. Image joins CSS.

**Files:**
- Modify: `Lumora/UI/Include/lumora/Widgets/Box.h` (`BoxCfg`), `Text.h` (`TextCfg`), `Input.h` (`InputCfg`), `Scroll.h` (`ScrollCfg`), `Image.h` (`ImageCfg`)
- Modify: `Lumora/UI/Source/Style/StyleResolver.h` (`BindClass`), `Lumora/UI/Source/Style/CssNodeState.h`, `Lumora/UI/Source/Style/Cascade.cpp/.h`
- Modify: `Lumora/UI/Source/Widgets/Image.cpp` (call `style::BindClass`, apply layout/opacity/radius like Container does)
- Test: `Tests/UI/StyleApplyTests.cpp`

**Interfaces:**
- Produces: `Str inlineCss{};` on `BoxCfg`, `TextCfg`, `InputCfg`, `ScrollCfg`, `ImageCfg`; `Str className{};` on `ImageCfg`. Reactive like `className`.
- Produces (internal): `std::vector<Declaration> style::ParseInlineDeclarations(std::string_view css)` and `void style::ApplyDeclarations(ComputedStyle&, std::span<const Declaration>)` in `Cascade.h`.

- [ ] **Step 1: Write failing tests** in `StyleApplyTests.cpp` using the existing `StyleHarness`:

```cpp
TEST(StyleApplyTests, InlineCssBeatsClassRule) {
    StyleHarness h;
    h.engine.LoadSource(".card { padding: 4px; } ");
    Owner o;
    const NodeId id = h.Mount(Box({.className = "card", .inlineCss = "padding: 10px; border-radius: 6px"}), o);
    EXPECT_EQ(h.Node(id).style.padding.left, 10.f);
    EXPECT_EQ(h.Style(id).radius, 6.f);
}
TEST(StyleApplyTests, ImportantRuleBeatsInlineCss) {
    StyleHarness h;
    h.engine.LoadSource(".card { padding: 4px !important; }");
    Owner o;
    const NodeId id = h.Mount(Box({.className = "card", .inlineCss = "padding: 10px"}), o);
    EXPECT_EQ(h.Node(id).style.padding.left, 4.f);
}
TEST(StyleApplyTests, InlineCssIsReactive) {
    StyleHarness h;
    h.engine.LoadSource("");
    Signal<std::string> css{std::string("width: 20px")};
    Owner o;
    const NodeId id = h.Mount(Box({.inlineCss = css}), o);
    css = std::string("width: 40px");
    h.graph->Flush(); // use whatever the harness's existing reactive tests call to flush
    EXPECT_EQ(std::get<float>(h.Node(id).style.width.value), 40.f); // adapt to SizeHint's accessor
}
TEST(StyleApplyTests, MalformedInlineCssIsIgnored) {
    StyleHarness h;
    h.engine.LoadSource("");
    Owner o;
    const NodeId id = h.Mount(Box({.inlineCss = "padding: ; ;; color red"}), o);
    EXPECT_EQ(h.Node(id).style.padding.left, 0.f);
}
TEST(StyleApplyTests, ImageTakesClassAndInlineSize) {
    StyleHarness h;
    h.engine.LoadSource(".pic { width: 64px; }");
    Owner o;
    const NodeId id = h.Mount(Image({.className = "pic", .inlineCss = "height: 32px"}), o);
    // assert width 64 and height 32 on h.Node(id).style via the SizeHint accessor used elsewhere in this file
}
```

Adapt the two accessor lines (`SizeHint`, flush) to what the file already uses. Keep the assertions' values.

- [ ] **Step 2: Run and see them fail (compile error on `inlineCss`)**

Run: `cmake --build --preset debug --target lumora_style_tests`
Expected: FAIL, `'inlineCss': is not a member of 'lumora::BoxCfg'`

- [ ] **Step 3: Implement**

`ParseInlineDeclarations`: run `ParseStylesheet(".x{" + css + "}")` and return the first rule's declarations (empty on anything else). Cache the parse on `CssNodeState` (`std::string inlineSource; std::vector<Declaration> inlineDecls;`), re-parsing only when the string changes. In `BindClass`, inside the effect, read `cfg.inlineCss.Get()` under `if constexpr (requires { cfg.inlineCss.Get(); })`, then call `ApplyDeclarations(cs, inlineDecls)` right after `Cascade(...)` and before vars resolve. `ApplyDeclarations` must write only properties that no `!important` rule set: extract Cascade's per-declaration write into it if Cascade does not already have one. Image: add `className`/`inlineCss` to `ImageCfg` and bind like `Container.cpp` (layout size, radius, opacity).

- [ ] **Step 4: Run tests**

Run: `cmake --build --preset debug --target lumora_style_tests lumora_tests && build\debug\bin\lumora_style_tests.exe && build\debug\bin\lumora_tests.exe`
Expected: all PASS (new and existing)

- [ ] **Step 5: Commit** `git commit -m "Add per-node inline CSS and CSS classes on Image"`

---

### Task 2: HostTree, an imperative node API

**Files:**
- Create: `Lumora/UI/Include/lumora/Host/HostTree.h`, `Lumora/UI/Source/Host/HostTree.cpp`
- Modify: `Lumora/UI/Include/lumora/MountedApp.h`, `Lumora/UI/Source/Runtime/MountedApp.cpp` (add `Host()`), `Lumora/UI/Source/Runtime/ControlRuntime.h/.cpp` (add `InsertChildBefore`), `Lumora/UI/CMakeLists.txt`
- Modify (if missing): `Lumora/UI/Include/lumora/Input/Event.h` add `LUMORA_API std::string_view KeyName(Key)`
- Test: `Tests/UI/HostTreeTests.cpp` (add to `lumora_tests` in `Tests/UI/CMakeLists.txt`)

**Interfaces:**
- Consumes: `MountBody`, `UnlinkChild` (ControlRuntime.h), widget builders `Box/Text/Input/Image/Scroll`, Task 1 `inlineCss`.
- Produces (`lumora/Host/HostTree.h`):

```cpp
namespace lumora {
enum class HostTag : std::uint8_t { View, Text, Button, Input, Image, Scroll };
enum class HostEvent : std::uint8_t { Click, DoubleClick, Press, Release, Move, Enter, Leave, Scroll, Focus, Blur, KeyDown, KeyUp, Input, Submit };
[[nodiscard]] LUMORA_API std::optional<HostTag>   ParseHostTag(std::string_view);   // "view","text","button","input","image","scroll"
[[nodiscard]] LUMORA_API std::optional<HostEvent> ParseHostEvent(std::string_view); // "click","dblclick","press","release","move","enter","leave","scroll","focus","blur","keydown","keyup","input","submit"
using HostValue = std::variant<std::monostate, bool, double, std::string>;
struct HostEventArgs { Vec2 position{}; Vec2 delta{}; int button = 0; std::string key; std::string text; KeyMods mods{}; };
using HostHandler = std::function<bool(const HostEventArgs&)>; // true = consumed (only key events read it)
using HostHandle  = std::uint32_t;
inline constexpr HostHandle kNoHost = 0;

class LUMORA_API HostTree {
public:
    [[nodiscard]] HostHandle Root() const noexcept;
    [[nodiscard]] HostHandle Create(HostTag tag);
    void Insert(HostHandle parent, HostHandle child, HostHandle before = kNoHost); // moves if already attached
    void Remove(HostHandle parent, HostHandle child);
    bool SetProp(HostHandle node, std::string_view name, const HostValue& value); // false: unknown prop for tag
    void SetHandler(HostHandle node, HostEvent event, HostHandler fn);           // empty fn clears
    void Release(HostHandle node);   // the script dropped its reference
    void Clear();                    // detach and release every child of Root()
    [[nodiscard]] std::size_t HandleCount() const noexcept;      // handles not yet freed, Root excluded
    [[nodiscard]] std::vector<std::string> TextContents() const; // Text contents under Root, tree order
};
}
```

`MountedApp`: `[[nodiscard]] HostTree& Host();` (created on first call, lives in `Impl`, root = the app's root node).

`ControlRuntime.h`: `void InsertChildBefore(NodeArena&, NodeId parent, NodeId child, NodeId before);` (invalid `before` = append).

Behavior the implementer must keep:
- `Create` records tag + props only. The live node is built at the first `Insert` (`MountBody` under the parent with a builder that reads the accumulated props), so creation-only props (`multiline`, `axis`) and initial values are static. Later writes to creation-only props: `LOG_WARN`, return true, ignore.
- Every changeable prop is a `Signal` in the handle's state, passed as the widget's `Reactive`/`Str`: `class`->`className`, `style`->`inlineCss`, `text`, `value` (Input `Bind`), `placeholder`, `disabled`, `src`, `fit`. `null` (`monostate`) resets to the widget default.
- `button` is a `Box` with `focusPolicy = Tab` and `onClick`.
- Handlers: the widget's `Fn`s are wired at build time for each event that has a handler then. They call through a shared per-handle slot so `SetHandler` can swap or clear them later. Adding a new event after build: `LOG_WARN`, ignored.
- Lifetime: a handle is freed when it is both released and detached. Freeing a node first unlinks its children (each has its own scope) and frees those that are released too.
- Invalid handle, wrong parent, inserting a node under itself or its descendant, children under `text`/`input`/`image`: `LOG_WARN`, no-op.
- `InsertChildBefore` is O(1) on the sibling list. Cross-parent moves keep the subtree's inherited CSS from the first parent until its next restyle. Mark that with a `ponytail:` comment.

- [ ] **Step 1: Write failing tests** in `HostTreeTests.cpp`. Build a `MountedApp` the way `Tests/UI/CountersTests.cpp` or `FocusTests.cpp` do (root `Box({})`, `FakeFonts.h` measurer), then use `app.Host()`:

```cpp
TEST(HostTree, CreateInsertBuildsLiveText) {          // Create(Text), SetProp text "hi", Insert(Root) -> TextContents()=={"hi"}
TEST(HostTree, TextPropUpdatesAfterInsert) {          // SetProp text "a" then "b" after insert, Tick -> {"b"}
TEST(HostTree, InsertBeforeOrders) {                  // insert c, then a before c, then b before c -> {"a","b","c"}
TEST(HostTree, InsertAttachedNodeMoves) {             // insert a,b; Insert(root,a) again -> {"b","a"}, HandleCount unchanged
TEST(HostTree, RemoveThenReinsertWorks) {             // Remove(root,a); Insert(view2,a) -> a's text visible under view2
TEST(HostTree, ReleaseWhileAttachedKeepsNode) {       // Release(a) while attached -> still in TextContents; Remove -> HandleCount drops by 1
TEST(HostTree, ClearFreesEverything) {                // build 3-level tree, release all, Clear() -> HandleCount()==0 and arena LiveCount back to baseline
TEST(HostTree, ClickHandlerFires) {                   // button with click handler, Layout(400,300), Dispatch a press+release at its solved rect -> handler ran once
TEST(HostTree, SwapHandlerAfterBuild) {               // replace click handler after insert -> new one runs, old does not
TEST(HostTree, InputValueAndInputEvent) {             // input with value "x"; handler for input receives text after typed char
TEST(HostTree, UnknownPropReturnsFalse) {             // SetProp(view, "bogus", 1.0) == false; no crash
TEST(HostTree, CycleInsertIsRejected) {               // Insert(child, parent) where parent contains child -> tree unchanged
TEST(HostTree, ClassAndStyleApply) {                  // app stylesheet ".big{width:50px}", class "big", style "height:20px" -> solved rect 50x20
TEST(HostTree, ParseNames) {                          // ParseHostTag("scroll")==Scroll, ParseHostTag("div")==nullopt, ParseHostEvent("dblclick")==DoubleClick
```

Write each body from its comment: the comment names the assertions and values.

- [ ] **Step 2: Run to see them fail**

Run: `cmake --build --preset debug --target lumora_tests`
Expected: FAIL, `lumora/Host/HostTree.h: No such file`

- [ ] **Step 3: Implement** `HostTree`, `MountedApp::Host()`, `InsertChildBefore`, `KeyName` per the Interfaces block. Handle state lives in a `std::vector<Slot>` indexed by handle (generation-free: handles are never reused within one `HostTree`; `std::uint32_t` gives 4 billion creations, so mark that with a `ponytail:` comment).

- [ ] **Step 4: Run tests**

Run: `cmake --build --preset debug --target lumora_tests && build\debug\bin\lumora_tests.exe --gtest_filter=HostTree*`
Expected: 14 PASS. Then the whole `lumora_tests` binary still passes.

- [ ] **Step 5: Commit** `git commit -m "Add HostTree imperative node API"`

---

### Task 3: Hermes dependency and the lumora_script target

**Files:**
- Create: `cmake/deps/hermes.cmake`, `Lumora/Script/CMakeLists.txt`, `Lumora/Script/Include/lumora/script/Script.h`, `Lumora/Script/Source/Script.cpp`
- Modify: `CMakeLists.txt` (option `LUMORA_BUILD_SCRIPT`), `Lumora/CMakeLists.txt` (`add_subdirectory(Script)` when ON), `Tests/CMakeLists.txt` (`add_subdirectory(Script)` when `TARGET lumora_script`), `.gitignore` (`ThirdParty/hermes/`)
- Test: `Tests/Script/CMakeLists.txt`, `Tests/Script/HermesSmokeTests.cpp`

**Interfaces:**
- Produces: target `lumora_script` (STATIC, alias `lumora::script`), PUBLIC-linked to Hermes + `jsi` so module code can include `<jsi/jsi.h>`. `std::string lumora::script::EvalToString(std::string_view js)`, a test seam that runs `js` in a fresh Hermes runtime and returns `String(result)` (or `"Error: <message>"`).

- [ ] **Step 1: Pin Hermes.** Find the facebook/hermes tag or branch for Hermes V1 `250829098.0.16` (reactnative.dev/releases/branches lists the version; the RN 0.87 `sdks/.hermesversion` or `sdks/hermes-engine/version.properties` names the ref). Write `cmake/deps/hermes.cmake` with `lumora_fetch_thirdparty(hermes REPO facebook/hermes TAG <ref> CMAKE OPTIONS ...)`, turning off test suite, tools other than what the VM build needs, Apple framework, ICU-requiring Intl if it needs system ICU on Windows. Record the ref in a comment.

- [ ] **Step 2: Write the failing smoke test**

```cpp
TEST(HermesSmoke, EvaluatesArithmetic) { EXPECT_EQ(lumora::script::EvalToString("1 + 2"), "3"); }
TEST(HermesSmoke, SupportsModernSyntax) {
    EXPECT_EQ(lumora::script::EvalToString("class A { #x = 2; get x() { return this.#x; } } const f = async () => 1; `${new A().x}`"), "2");
}
TEST(HermesSmoke, ReportsSyntaxErrors) { EXPECT_TRUE(lumora::script::EvalToString("let =").starts_with("Error:")); }
TEST(HermesSmoke, Utf8RoundTrip) { EXPECT_EQ(lumora::script::EvalToString("'héllo 世界 👋'"), "héllo 世界 👋"); }
```

If `SupportsModernSyntax` fails on the pinned Hermes, note which feature failed. Task 6 sets esbuild's `target` to lower that syntax, and this test then covers only what Hermes runs natively.

- [ ] **Step 3: Run to see it fail.** Run: `cmake --preset debug && cmake --build --preset debug --target lumora_script_tests`. Expected: FAIL, missing `lumora/script/Script.h`.

- [ ] **Step 4: Implement** `EvalToString` with `facebook::hermes::makeHermesRuntime()` and `evaluateJavaScript(std::make_shared<jsi::StringBuffer>(...), "eval.js")`, catching `jsi::JSIException`. If Hermes builds as a DLL, call `lumora_copy_runtime_dlls` on the test and later on `LumoraJsHost`. Make Hermes inherit the repo's `/MT` runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`). If Hermes overrides it, set it per Hermes target after fetch.

- [ ] **Step 5: Run.** `build\debug\bin\lumora_script_tests.exe` → 4 PASS (or 3 + documented feature gap). A configure with `-DLUMORA_BUILD_SCRIPT=OFF` still builds `lumora_tests`.

- [ ] **Step 6: Commit** `git commit -m "Add Hermes dependency and lumora_script target"`

---

### Task 4: JsApp and the `__lumora` bridge

**Files:**
- Create: `Lumora/Script/Include/lumora/script/JsApp.h`, `Lumora/Script/Source/JsApp.cpp`, `Lumora/Script/Source/Bridge.cpp/.h` (host functions + `NodeRef` host object)
- Test: `Tests/Script/JsAppTests.cpp`

**Interfaces:**
- Consumes: `MountedApp::Host()`, `HostTree` (Task 2), `MountedApp::RunScoped`.
- Produces:

```cpp
namespace lumora::script {
class LUMORA_API JsApp {
public:
    explicit JsApp(MountedApp& app);
    ~JsApp();                                                       // Unload()
    bool Load(std::string_view source, std::string_view sourceUrl); // fresh runtime, installs __lumora, evaluates; false on error
    void Tick(double nowMs);                                        // calls global __lumoraTick(nowMs) if defined, drains microtasks
    void Unload();                                                  // destroys the runtime, then HostTree::Clear()
    [[nodiscard]] bool Loaded() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;    // message + JS stack of the last failure, empty if none
};
}
```

- `__lumora` exactly as the spec's contract table (no `requireModule` yet). Node refs are a `jsi::HostObject` holding a `HostHandle`. Its destructor calls `HostTree::Release` unless the `JsApp` is mid-`Unload` (then `Clear()` frees all).
- Every entry into JS (`Load`, `Tick`, a `HostHandler` firing) runs under `app.RunScoped` when not already inside dispatch, catches `jsi::JSError`, stores `LastError`, logs it with `LOG_ERROR`, and shows it: `HostTree::Clear()` then a `text` node with class `lumora-error` and inline style `color: #ff5555; padding: 12px` holding the message. After an error, `Tick` is a no-op until the next `Load`.
- `log(level, msg)` maps to `LOG_DEBUG/INFO/WARN/ERROR`.
- Hermes runtime config enables the microtask queue. `Tick` and every handler call drain it.

- [ ] **Step 1: Write failing tests.** Headless `MountedApp` as in Task 2. JS is inline strings against the raw `__lumora` API (no bundler yet):

```cpp
TEST(JsApp, BuildsTreeFromScript)      // Load("const t=__lumora.createNode('text'); __lumora.setProp(t,'text','hi'); __lumora.insert(__lumora.root(), t, null);") -> Host().TextContents()=={"hi"}
TEST(JsApp, TickDrivesTimersAndPromises) // script defines globalThis.__lumoraTick=(n)=>{ if(n>=100) Promise.resolve().then(()=>__lumora.setProp(t,'text','late')) }; Tick(50) -> "hi"; Tick(100) -> "late"
TEST(JsApp, HandlerCallsIntoJs)         // button with setHandler(b,'click',()=>__lumora.setProp(t,'text','clicked')); dispatch click -> "clicked"
TEST(JsApp, LoadErrorShowsOverlay)      // Load("throw new Error('boom')") == false; LastError() contains "boom"; TextContents()[0] contains "boom"
TEST(JsApp, HandlerErrorDoesNotCrash)   // click handler throws -> LastError contains message; app still ticks and lays out
TEST(JsApp, UnknownTagThrowsTypeError)  // Load("__lumora.createNode('div')") == false; LastError contains "TypeError"
TEST(JsApp, Utf8Text)                   // setProp text 'héllo 世界 👋' -> TextContents()=={"héllo 世界 👋"}
TEST(JsApp, GcFreesDetachedNodes)       // create 100 detached nodes in a function scope, run HermesRuntime GC through a test-only JsApp hook, HandleCount()==0
TEST(JsApp, ReloadDoesNotLeak)          // 20x Load(sameTreeScript) with Unload between -> arena LiveCount and HandleCount equal to after first Load
TEST(JsApp, OldTimersDieOnReload)       // load 1 sets __lumoraTick to write 'old'; Load(script2) that writes 'new'; Tick -> {"new"}
```

Add the GC hook as `void JsApp::CollectGarbageForTesting();` (calls `HermesRuntime::instrumentation().collectGarbage("test")`). The arena count is reached through `MountedApp::Counters()` or a test-only accessor. Pick whichever exists, else add `[[nodiscard]] std::uint32_t MountedApp::LiveNodeCount() const`.

- [ ] **Step 2: Run to see them fail.** `cmake --build --preset debug --target lumora_script_tests` → FAIL (missing JsApp.h).
- [ ] **Step 3: Implement** per Interfaces.
- [ ] **Step 4: Run** `build\debug\bin\lumora_script_tests.exe --gtest_filter=JsApp*` → 10 PASS.
- [ ] **Step 5: Commit** `git commit -m "Add JsApp: Hermes runtime bound to HostTree via __lumora"`

---

### Task 5: LumoraJsHost executable with file-watch reload

**Files:**
- Modify: `Samples/Host/SampleHost.h` (`SampleDesc::onFrame`), `Samples/Host/SampleHost.cpp` (call it in headless, windowed and web loops)
- Create: `Samples/JsHost/CMakeLists.txt`, `Samples/JsHost/JsHost.h`, `Samples/JsHost/JsHost.cpp`, `Samples/JsHost/main.cpp`, `Samples/JsHost/test/hello.js` (hand-written raw-API bundle for the smoke test), `Samples/JsHost/test/throws.js`

**Interfaces:**
- Consumes: `JsApp` (Task 4).
- Produces: `SampleDesc` gains `std::function<void(MountedApp&, float dt)> onFrame;`, called once per frame on the thread that ticks the app, after input dispatch and before `Update`/`Tick`. Target `lumora_js_host` (STATIC: `JsHost.cpp`) exposing `int lumora::jshost::RunJsHost(int argc, char** argv);`, and sample `LumoraJsHost` (`main.cpp` defines `lumora::platform::Main` calling it). CLI flags: `--bundle <path>` (required except for the smoke default), `--title <s>` (default "Lumora"), `--dump-text <path>` (write `HostTree::TextContents()` one per line at exit), and every `RunSample` flag.

Behavior:
- Root element: `Box({.className = "lumora-root", .inlineCss = "width: 100%; height: 100%; flex-direction: column"})`.
- First `onFrame`: read the bundle (`std::ifstream`, binary), `JsApp::Load`. No `--bundle`: show a text node "LumoraJsHost: pass --bundle <file>".
- Clock: `nowMs` accumulates `dt * 1000` so headless runs are deterministic.
- Every 250 ms of clock: `std::filesystem::last_write_time(bundle)`. On change, re-read and `Load` (JsApp unloads first). A read that fails (editor mid-write) retries next poll. A missing file keeps the old app.
- Errors are already shown by JsApp. The host also prints them to stderr so `lumora dev` shows them in the terminal.

- [ ] **Step 1: Failing tests as ctest entries** in `Samples/JsHost/CMakeLists.txt`, guarded by `LUMORA_BUILD_TESTS`:
  - `LumoraJsHost.hello`: `LumoraJsHost --headless-frames 30 --bundle test/hello.js --dump-text ${CMAKE_CURRENT_BINARY_DIR}/hello.txt`, then a `cmake -P` check script that asserts the file's lines are exactly `Count: 1` (hello.js shows `Count: 0` and a timer at 100 ms bumps it to 1, through raw `__lumora` calls and its own `__lumoraTick`).
  - `LumoraJsHost.throws`: same with `test/throws.js`. Exit code 0 and the dump's first line contains `boom`.
  - The auto `LumoraJsHost.smoke` (from `lumora_add_sample`) runs with no bundle and must pass.
- [ ] **Step 2: Run to see them fail.** `cmake --build --preset debug --target LumoraJsHost` → FAIL (no target).
- [ ] **Step 3: Implement** `onFrame` plumbing and the host.
- [ ] **Step 4: Run.** `ctest --preset debug -R LumoraJsHost` → 3 PASS. `ctest --preset debug -R smoke` → every sample's smoke still passes.
- [ ] **Step 5: Manual windowed check.** `build\debug\bin\LumoraJsHost.exe --bundle Samples\JsHost\test\hello.js` shows "Count: 0" then "Count: 1". While it runs, edit `hello.js` to change the label. The window updates within about 1 s without closing.
- [ ] **Step 6: Commit** `git commit -m "Add LumoraJsHost with bundle reload"`

---

### Task 6: JS workspace and the bundler

**Files:**
- Create: `js/package.json` (private, `"workspaces": ["packages/*", "examples/*"]`, script `"test": "npm test --workspaces --if-present"`), `js/.gitignore` (`node_modules/`, `.lumora/`)
- Create: `js/packages/lumora/package.json` (`name: lumora`, `type: module`, `bin: { "lumora": "bin/lumora.js" }`, deps esbuild, @babel/core, babel-preset-solid, @babel/preset-typescript; `"test": "node --test"`), `js/packages/lumora/src/bundle.js`
- Test: `js/packages/lumora/test/bundle.test.js`, fixtures `js/packages/lumora/test/fixtures/basic/{main.tsx,app.css,util.ts}`

**Interfaces:**
- Produces: `export async function bundle({ entry, outfile, dev = true, watch = false, external = [], onRebuild }): Promise<{ ok: boolean, errors: string[], stop: () => Promise<void> }>`. `onRebuild({ ok, errors })` runs after each watch rebuild. On failure the previous `outfile` is left untouched.
- Produces: CSS convention used by Task 7: each imported `.css` file becomes the module `(globalThis.__lumoraCss ??= []).push(<css text>);`. Import order means a file's CSS is pushed before code below the import runs.

esbuild settings: `bundle: true, format: "iife", platform: "neutral", mainFields: ["module", "main"], conditions: ["browser"], target: "es2020"` (lower it if Task 3 found gaps), `sourcemap: dev ? "inline" : false`, `minify: !dev`, `define: { "process.env.NODE_ENV": dev ? '"development"' : '"production"' }`. Plugin 1: `.tsx/.jsx` onLoad runs `@babel/core` `transformAsync` with `[["babel-preset-solid", { generate: "universal", moduleName: "@lumora/runtime" }], ["@babel/preset-typescript", { isTSX: true, allExtensions: true }]]`. Plain `.ts` goes through esbuild's own loader. Plugin 2: `.css` onLoad emits the module above (`JSON.stringify` the text).

- [ ] **Step 1: Failing tests** (`node:test`). Fixtures import `solid-js` directly and `@lumora/runtime` as `external`, since the runtime arrives in Task 7:

```js
test("bundles tsx through solid universal", async () => { /* external:["@lumora/runtime"]; output contains `createElement(` and `insertNode(`, no `document.` and no `template(` */ });
test("css import is collected before importing module code", async () => { /* output contains `__lumoraCss` push with ".card", at a smaller index than the App component's code */ });
test("solid resolves to the reactive build", async () => { /* fixture uses createSignal+createEffect from solid-js, run output in vm.runInNewContext: effect re-runs after a signal write (records 2 runs); output does not contain "dist/server" */ });
test("type errors do not fail the bundle but syntax errors do", async () => { /* TS type mismatch file -> ok:true; `const = ` file -> ok:false, errors[0] mentions file and line */ });
test("watch rebuild keeps last good output on error", async () => { /* watch:true on a fixture copy in a tmp dir; write a syntax error; onRebuild gets ok:false; outfile content unchanged */ });
```

- [ ] **Step 2: Run to see failure.** `cd js && npm install && npm test -w lumora` → FAIL (`bundle.js` missing).
- [ ] **Step 3: Implement `bundle`.**
- [ ] **Step 4: Run.** `npm test -w lumora` → 5 PASS.
- [ ] **Step 5: Commit** `git commit -m "Add js workspace and esbuild+solid bundler"` (commit `js/package-lock.json`).

---

### Task 7: @lumora/runtime (renderer, props, text, timers, types)

**Files:**
- Create: `js/packages/runtime/package.json` (`name: @lumora/runtime`, `type: module`, `exports: { ".": { "types": "./index.d.ts", "default": "./src/index.js" }, "./jsx-runtime": { "types": "./jsx-runtime.d.ts", "default": "./src/jsx-runtime.js" } }`, peerDependency `solid-js`, devDependency `lumora: "*"`)
- Create: `src/index.js` (renderer + re-exports + `render`), `src/native.js` (wraps `globalThis.__lumora`), `src/props.js` (event table, style serialization), `src/timers.js` (installs timer globals + `__lumoraTick`), `src/console.js`, `src/jsx-runtime.js` (empty module, exists for TS resolution), `index.d.ts`, `jsx-runtime.d.ts`
- Test: `js/packages/runtime/test/fake-native.js`, `test/render.test.js`, `test/fixtures/*.tsx`

**Interfaces:**
- Consumes: `bundle()` (Task 6) in tests. The `__lumora` contract from the spec.
- Produces: `export function render(code: () => JSX.Element): () => void` (mounts into `__lumora.root()`, returns dispose). Re-exports from `solid-js`: `createSignal, createEffect, createMemo, createResource, createContext, useContext, batch, untrack, on, onMount, onCleanup, Show, For, Index, Switch, Match, ErrorBoundary, Suspense`. Renderer exports required by babel universal output: `effect, memo, createComponent, createElement, createTextNode, insertNode, insert, spread, setProp, mergeProps, use`.
- `fake-native.js`: `export function createFakeNative(): { api: LumoraNative, calls: Array<[string, ...unknown[]]>, tree(): FakeNode }` with `FakeNode = { tag, props: Record<string, unknown>, handlers: Record<string, Function>, children: FakeNode[] }`.
- `jsx-runtime.d.ts`: `export namespace JSX` with `IntrinsicElements { view: ViewProps; text: TextProps; button: ButtonProps; input: InputProps; image: ImageProps; scroll: ScrollProps }`, all with `class?: string; style?: string | CSSProperties; ref?: ...; children?: JSX.Element`, plus the `on*` handlers. `InputProps` has `value?: string; placeholder?: string; multiline?: boolean; onInput?: (e: { value: string }) => void; onSubmit?: (e: { value: string }) => void`.

Rules:
- JS keeps a light mirror `{ tag, native, parent, children[] }` for `getParentNode/getFirstChild/getNextSibling`. No diffing.
- Text nodes are JS-only `{ text, parent }`. Inside a `text` element, the element's `text` prop is the concatenation of its text children, recomputed on change. A text node inserted anywhere else gets a native `text` node created for it.
- `setProperty`: `class`/`className` → `class`. `style` object → `kebab-case: value` joined by `; `, numbers get `px` except `opacity flex flexGrow flexShrink zIndex order fontWeight lineHeight aspectRatio`. Event props map `onClick click, onDoubleClick dblclick, onPress press, onRelease release, onPointerMove move, onPointerEnter enter, onPointerLeave leave, onScroll scroll, onFocus focus, onBlur blur, onKeyDown keydown, onKeyUp keyup, onInput input, onSubmit submit`. Unknown `on*` → `console.warn` once per name.
- Timers: `setTimeout/setInterval/clearTimeout/clearInterval/requestAnimationFrame/cancelAnimationFrame/queueMicrotask` on `globalThis` only if not already defined (the web build later brings real ones). `__lumoraTick(now)` runs due timers in due order, then rAF callbacks with `now`. Errors inside a timer propagate (JsApp shows them).
- `console.log/info/warn/error/debug` → `__lumora.log`, args joined with spaces, objects via `JSON.stringify` guarded for cycles.

- [ ] **Step 1: Failing tests** (`render.test.js`; each bundles a fixture with `bundle()` and runs it in `vm.runInNewContext` with `createFakeNative().api`):

```js
test("renders intrinsic tree");            // <view class="a"><text>Hi</text></view> -> tree() == {tag:"root",children:[{tag:"view",props:{class:"a"},children:[{tag:"text",props:{text:"Hi"}}]}]}
test("signal update patches one prop");    // count signal in <text>Count: {count()}</text>; click handler increments; calls after click == [["setProp", <text>, "text", "Count: 1"]]
test("Show swaps nodes");                  // <Show when={on()}> toggled -> child inserted then removed
test("For reorders by moving");            // For over [a,b,c] -> [c,a,b]: no createNode calls after the initial ones
test("style object serializes");           // style={{ fontSize: 12, opacity: 0.5, padding: "4px 8px" }} -> "font-size: 12px; opacity: 0.5; padding: 4px 8px"
test("timers run from __lumoraTick");      // setTimeout(f,100); tick(50) not run; tick(100) run once; setInterval runs on each 100ms boundary; clearInterval stops it
test("bare text outside <text> becomes a native text node");
test("console routes to __lumora.log");    // console.warn("x", {a:1}) -> log(2, 'x {"a":1}')
test("render applies collected css first"); // fixture imports two .css files; calls[0] is ["setStylesheet", <both texts joined by "\n" in import order>], before any createNode
```

`render()` calls `__lumora.setStylesheet((globalThis.__lumoraCss ?? []).join("\n"))` before mounting, and skips the call when the list is empty.

- [ ] **Step 2: Run to see failure.** `cd js && npm test -w @lumora/runtime` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w @lumora/runtime` → 9 PASS. Also `npx tsc -p packages/runtime/test/types` with a `tsconfig.json` (`jsx: preserve`, `jsxImportSource: @lumora/runtime`, `noEmit`) over a fixture that uses every intrinsic. It must have zero errors, and a fixture line with `<view onClik={...}>` marked `// @ts-expect-error` must be flagged.
- [ ] **Step 5: Commit** `git commit -m "Add @lumora/runtime solid universal renderer"`

---

### Task 8: CLI `create` and `dev`

**Files:**
- Create: `js/packages/lumora/bin/lumora.js`, `src/cli.js` (arg parsing with `node:util` `parseArgs`), `src/create.js`, `src/dev.js`, `src/host.js`
- Create: `js/packages/lumora/templates/app/{package.json,tsconfig.json,.gitignore,src/main.tsx,src/App.tsx,src/app.css,README.md}`
- Test: `js/packages/lumora/test/create.test.js`, `test/dev.test.js`, `test/fake-host.js`

**Interfaces:**
- Consumes: `bundle()` (Task 6). LumoraJsHost flags (Task 5).
- Produces:
  - `lumora [name]` or `lumora create [name] [--no-install]` scaffolds. A first positional that isn't a command is the project name, so `npx lumora@latest my-app` works once published (Phase 3). A missing name prompts with `node:readline` (default `my-lumora-app`). Bare `lumora` inside a dir whose package.json lists `lumora` prints help instead.
  - `lumora dev [--host <exe>] [-- <host args>]`
  - `export async function create({ name, cwd, install = true }): Promise<string>` returns the project dir. It refuses an existing non-empty dir with exit code 1 and a message. `name` must match `^[a-z0-9][a-z0-9-_]*$`. Placeholders `{{name}}` and `{{runtimeVersion}}` are substituted. `install` runs `npm install` in the new dir.
  - `export function resolveHost({ flag, env = process.env }): string` uses the `--host` flag, then `env.LUMORA_HOST`. Otherwise it throws an `Error` whose message says to build `LumoraJsHost` and set `LUMORA_HOST` (Phase 3 adds the prebuilt package to this order).
  - `export async function dev({ root, host, hostArgs = [], signal }): Promise<number>` bundles `src/main.tsx` to `<root>/.lumora/dev/bundle.js` with `watch: true`. It spawns the host as `[--bundle <abs>, --title <package name>, ...hostArgs]` with stdio inherited and prints `[lumora] rebuilt in N ms` or the formatted errors on each rebuild. It returns the host's exit code and stops the watcher on exit or `signal` abort. Ctrl+C kills the host.
- Template `package.json`: `{"name":"{{name}}","private":true,"type":"module","scripts":{"dev":"lumora dev"},"dependencies":{"@lumora/runtime":"^{{runtimeVersion}}","solid-js":"^1.9.9"},"devDependencies":{"lumora":"^{{runtimeVersion}}","typescript":"^5.9.3"}}`. `tsconfig.json`: `{"compilerOptions":{"jsx":"preserve","jsxImportSource":"@lumora/runtime","strict":true,"module":"ESNext","moduleResolution":"Bundler","target":"ES2020","noEmit":true}}`. `App.tsx`: a counter (`createSignal`, `<button onClick>`, `<text>`) with a `class` from `app.css`.

- [ ] **Step 1: Failing tests:**

```js
test("create scaffolds a project");         // create({name:"demo", cwd:tmp, install:false}) -> files exist; package.json name "demo"; no "{{" left in any file
test("create rejects bad names and non-empty dirs");
test("resolveHost order");                  // flag beats env; env used when no flag; neither -> throws mentioning LUMORA_HOST
test("dev bundles and launches host");      // fake-host.js (node script) writes its argv + bundle contents to a file and exits 0; dev({root: scaffolded tmp project with deps symlinked from the workspace, host: `node fake-host.js`}) -> argv has --bundle <abs path>, bundle file exists, return 0
test("dev rebuild rewrites bundle while host runs"); // fake host waits for bundle mtime change then exits 0; test edits src/App.tsx after first build -> dev resolves 0
```

`fake-host.js` gets the host command as `process.execPath` plus script. Let `host` be `string | string[]` so tests can pass `[process.execPath, fakeHostPath]`.

- [ ] **Step 2: Run to see failure.** `npm test -w lumora` → new tests FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** `npm test -w lumora` → all PASS (Task 6's included).
- [ ] **Step 5: Commit** `git commit -m "Add lumora create and dev commands"`

---

### Task 9: End-to-end on the real host

**Files:**
- Create: `js/examples/hello/` (made with `node js/packages/lumora/bin/lumora.js create hello --no-install` inside `js/examples/`, then deps resolve through the workspace), plus `js/examples/hello/src/App.tsx` adds `setTimeout(() => setCount(1), 100)` under `onMount` for the test
- Create: `js/packages/lumora/test/e2e.test.js` (skipped unless `LUMORA_HOST` is set)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Failing e2e test:** bundle `js/examples/hello/src/main.tsx` with `dev: true`, run `LUMORA_HOST --headless-frames 30 --bundle <out> --dump-text <tmp>`, and assert the dump contains `Count: 1`. Then bundle a variant whose App throws during render, run it, and assert exit code 0 with a dump line containing the error message.
- [ ] **Step 2: Run.** `set LUMORA_HOST=%CD%\build\debug\bin\LumoraJsHost.exe && cd js && npm test -w lumora` → e2e PASS. Without `LUMORA_HOST` the test reports skipped.
- [ ] **Step 3: Manual acceptance (the Phase 1 goal):**
  1. `cd %TEMP% && node D:\works\Lumora\js\packages\lumora\bin\lumora.js create demo --no-install`, then in `demo/package.json` point `@lumora/runtime` and `lumora` at `file:D:/works/Lumora/js/packages/...`, then `npm install`.
  2. `set LUMORA_HOST=D:\works\Lumora\build\debug\bin\LumoraJsHost.exe && npm run dev` opens a window with the counter, and clicking increments it.
  3. Edit `src/App.tsx` and save. The window reloads in under 2 s. Introduce a syntax error: the terminal shows it and the window keeps the last good app. Fix it and the window reloads.
  4. Throw inside a click handler: the window shows the red error text. Fix and save: the app comes back.
- [ ] **Step 4: Commit** `git commit -m "Add hello example and end-to-end host test"`
