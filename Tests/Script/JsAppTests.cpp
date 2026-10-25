#include <gtest/gtest.h>

#include <lumora/Host/HostTree.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Widgets.h>
#include <lumora/log/Log.h>
#include <lumora/script/JsApp.h>

#include "../UI/FakeFonts.h"
#include "Host/HostTreeAccess.h"

#include <atomic>
#include <string>
#include <vector>

using namespace lumora;
using lumora::script::JsApp;

namespace {

using Texts = std::vector<std::string>;

constexpr const char* kHi =
    "const t=__lumora.createNode('text'); __lumora.setProp(t,'text','hi'); __lumora.insert(__lumora.root(), t, null);";

[[nodiscard]] Event Mouse(EventType type, Vec2 at) {
    Event ev{};
    ev.Type     = type;
    ev.Position = at;
    ev.Button   = PointerButton::Left;
    return ev;
}

struct Fixture {
    FakeFonts  fonts;
    MountedApp app{Box({}), &fonts};
    HostTree&  host = app.Host();
    JsApp      js{app};

    Fixture() { app.SetInteractionEnv(&fonts, nullptr); }

    void Frame() {
        app.Tick(1.f / 60.f);
        app.Layout(400.f, 300.f);
    }

    /// Script buttons below are 100x40 and first under the root.
    void ClickButton() {
        app.Dispatch(Mouse(EventType::MouseDown, {50.f, 20.f}));
        app.Dispatch(Mouse(EventType::MouseUp, {50.f, 20.f}));
        Frame();
    }
};

constexpr const char* kButton =
    "const b=__lumora.createNode('button'); __lumora.setProp(b,'style','width:100px;height:40px');"
    "const t=__lumora.createNode('text'); __lumora.setProp(t,'text','hi');"
    "__lumora.insert(__lumora.root(), b, null); __lumora.insert(__lumora.root(), t, null);";

} // namespace

TEST(JsApp, BuildsTreeFromScript) {
    Fixture f;
    ASSERT_TRUE(f.js.Load(kHi, "app.js")) << f.js.LastError();
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"hi"}));
    EXPECT_TRUE(f.js.Loaded());
}

TEST(JsApp, TickDrivesTimersAndPromises) {
    Fixture f;
    ASSERT_TRUE(f.js.Load(std::string(kHi) +
                              "globalThis.__lumoraTick=(n)=>{ if(n>=100) Promise.resolve().then(()=>__lumora.setProp(t,'text','late')) };",
                          "app.js"))
        << f.js.LastError();
    f.js.Tick(1000); // first tick after a load delivers 0
    f.js.Tick(1050);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"hi"}));
    f.js.Tick(1100);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"late"}));
}

TEST(JsApp, TickClockStartsAtZeroPerLoad) {
    Fixture f;
    const char* script = "const t=__lumora.createNode('text'); __lumora.insert(__lumora.root(), t, null); const seen=[];"
                         "globalThis.__lumoraTick=(n)=>{ seen.push(n); __lumora.setProp(t,'text',seen.join(',')); };";
    ASSERT_TRUE(f.js.Load(script, "app.js")) << f.js.LastError();
    f.js.Tick(5000);
    f.js.Tick(5100);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"0,100"}));

    ASSERT_TRUE(f.js.Load(script, "app.js")) << f.js.LastError();
    f.js.Tick(9000);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"0"}));
}

TEST(JsApp, HandlerCallsIntoJs) {
    Fixture f;
    ASSERT_TRUE(f.js.Load(std::string(kButton) + "__lumora.setHandler(b,'click',()=>__lumora.setProp(t,'text','clicked'));", "app.js"))
        << f.js.LastError();
    f.Frame();
    f.ClickButton();
    EXPECT_EQ(f.host.TextContents(), (Texts{"clicked"}));
}

TEST(JsApp, LoadErrorShowsOverlay) {
    Fixture f;
    EXPECT_FALSE(f.js.Load("throw new Error('boom')", "app.js"));
    EXPECT_NE(f.js.LastError().find("boom"), std::string::npos);
    EXPECT_FALSE(f.js.Loaded());
    f.Frame();
    const Texts texts = f.host.TextContents();
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_NE(texts[0].find("boom"), std::string::npos);
}

TEST(JsApp, SyntaxErrorShowsOverlay) {
    Fixture f;
    EXPECT_FALSE(f.js.Load("let =", "app.js"));
    EXPECT_FALSE(f.js.LastError().empty());
    f.Frame();
    EXPECT_EQ(f.host.TextContents().size(), 1u);
}

TEST(JsApp, HandlerErrorDoesNotCrash) {
    Fixture f;
    ASSERT_TRUE(f.js.Load(std::string(kButton) +
                              "__lumora.setHandler(b,'click',()=>{ throw new Error('handler blew up'); });"
                              "globalThis.__lumoraTick=()=>__lumora.setProp(t,'text','ticked');",
                          "app.js"))
        << f.js.LastError();
    f.Frame();
    f.ClickButton();
    EXPECT_NE(f.js.LastError().find("handler blew up"), std::string::npos);
    f.js.Tick(16); // no-op after an error
    f.Frame();
    const Texts texts = f.host.TextContents();
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_NE(texts[0].find("handler blew up"), std::string::npos);

    ASSERT_TRUE(f.js.Load(kHi, "app.js")) << f.js.LastError();
    EXPECT_TRUE(f.js.LastError().empty());
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"hi"}));
}

TEST(JsApp, UnknownTagThrowsTypeError) {
    Fixture f;
    EXPECT_FALSE(f.js.Load("__lumora.createNode('div')", "app.js"));
    EXPECT_NE(f.js.LastError().find("TypeError"), std::string::npos) << f.js.LastError();
}

TEST(JsApp, Utf8Text) {
    Fixture f;
    ASSERT_TRUE(f.js.Load("const t=__lumora.createNode('text'); __lumora.setProp(t,'text','héllo 世界 👋');"
                          "__lumora.insert(__lumora.root(), t, null);",
                          "app.js"))
        << f.js.LastError();
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"héllo 世界 👋"}));
}

TEST(JsApp, GcFreesDetachedNodes) {
    Fixture f;
    ASSERT_TRUE(f.js.Load("(function(){ for (let i=0;i<100;i++) __lumora.createNode('view'); })();", "app.js"))
        << f.js.LastError();
    f.Frame();
    EXPECT_EQ(f.host.HandleCount(), 100u);
    f.js.CollectGarbageForTesting();
    f.Frame();
    EXPECT_EQ(f.host.HandleCount(), 0u);
}

TEST(JsApp, ReloadDoesNotLeak) {
    std::atomic<int> warnings{0};
    const log::SinkHandle sink = log::AddSink([&warnings](log::Level level, const char*, const char*)
    {
        if (level >= log::Level::Warn) ++warnings;
    });

    Fixture f;
    // A held detached node with a handler, an attached tree with handlers, and a pending timer.
    const std::string script = std::string(kButton) +
                               "__lumora.setHandler(b,'click',()=>{});"
                               "globalThis.held=__lumora.createNode('button'); __lumora.setHandler(held,'click',()=>{});"
                               "const v=__lumora.createNode('view'); __lumora.insert(b, v, null);"
                               "globalThis.__lumoraTick=()=>{};";
    ASSERT_TRUE(f.js.Load(script, "app.js")) << f.js.LastError();
    f.Frame();
    const std::uint32_t live = HostTreeTestAccess::LiveNodes(f.host);
    const std::size_t handles = f.host.HandleCount();

    for (int i = 0; i < 20; ++i) {
        f.js.Unload();
        ASSERT_TRUE(f.js.Load(script, "app.js")) << f.js.LastError();
        f.js.Tick(i * 16.0);
        f.Frame();
    }
    EXPECT_EQ(HostTreeTestAccess::LiveNodes(f.host), live);
    EXPECT_EQ(f.host.HandleCount(), handles);

    f.js.Unload();
    f.Frame();
    EXPECT_EQ(f.host.HandleCount(), 0u);
    log::RemoveSink(sink);
    EXPECT_EQ(warnings.load(), 0);
}

TEST(JsApp, OldTimersDieOnReload) {
    Fixture f;
    ASSERT_TRUE(f.js.Load(std::string(kHi) + "globalThis.__lumoraTick=()=>__lumora.setProp(t,'text','old');", "app.js"))
        << f.js.LastError();
    ASSERT_TRUE(f.js.Load("const t=__lumora.createNode('text'); __lumora.setProp(t,'text','new'); __lumora.insert(__lumora.root(), t, null);",
                          "app.js"))
        << f.js.LastError();
    f.js.Tick(16);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"new"}));
}
