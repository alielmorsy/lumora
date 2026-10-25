#include <gtest/gtest.h>

#include <lumora/Host/HostTree.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Widgets.h>
#include <lumora/script/JsApp.h>
#include <lumora/script/NativeModule.h>

#include "../UI/FakeFonts.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace lumora;
using namespace lumora::script;
namespace jsi = facebook::jsi;

namespace {

std::atomic<int> g_laterPosted{0};

using Texts = std::vector<std::string>;

} // namespace

// echo(s) returns s; later(s) returns a Promise resolved with s from another thread through the invoker.
// Pending resolvers live on the runtime's global, so jobs carry only plain data.
LUMORA_NATIVE_MODULE(Echo)
{
    jsi::Runtime& rt = ctx.runtime;
    ctx.exports.setProperty(rt, "echo", jsi::Function::createFromHostFunction(rt, jsi::PropNameID::forAscii(rt, "echo"), 1,
        [](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t) { return jsi::Value(r, args[0]); }));

    ctx.exports.setProperty(rt, "later", jsi::Function::createFromHostFunction(rt, jsi::PropNameID::forAscii(rt, "later"), 1,
        [invoker = ctx.invoker](jsi::Runtime& r, const jsi::Value&, const jsi::Value* args, std::size_t) -> jsi::Value
        {
            std::string s = args[0].getString(r).utf8(r);
            const jsi::Function executor = jsi::Function::createFromHostFunction(r, jsi::PropNameID::forAscii(r, "executor"), 2,
                [](jsi::Runtime& r2, const jsi::Value&, const jsi::Value* a, std::size_t) {
                    r2.global().setProperty(r2, "__echoResolve", jsi::Value(r2, a[0]));
                    return jsi::Value::undefined();
                });
            jsi::Value promise = r.global().getPropertyAsFunction(r, "Promise").callAsConstructor(r, executor);
            std::thread([invoker, s = std::move(s)] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                invoker([s](jsi::Runtime& r3) {
                    r3.global().getPropertyAsFunction(r3, "__echoResolve").call(r3, jsi::String::createFromUtf8(r3, s));
                });
                ++g_laterPosted;
            }).detach();
            return promise;
        }));
}

namespace {

struct Fixture {
    FakeFonts      fonts;
    MountedApp     app{Box({}), &fonts};
    HostTree&      host = app.Host();
    ModuleRegistry modules;
    JsApp          js{app, &modules};

    Fixture()
    {
        app.SetInteractionEnv(&fonts, nullptr);
        modules.Add("Echo", &LumoraModule_Echo_Install);
    }

    void Frame()
    {
        app.Tick(1.f / 60.f);
        app.Layout(400.f, 300.f);
    }

    Texts Show(const std::string& expr)
    {
        const std::string src = "const t=__lumora.createNode('text'); __lumora.insert(__lumora.root(), t, null);"
                                "globalThis.show=(s)=>__lumora.setProp(t,'text',String(s)); show(" + expr + ");";
        EXPECT_TRUE(js.Load(src, "app.js")) << js.LastError();
        Frame();
        return host.TextContents();
    }
};

constexpr const char* kText = "const t=__lumora.createNode('text'); __lumora.insert(__lumora.root(), t, null);"
                              "__lumora.setProp(t,'text','start');";

} // namespace

TEST(NativeModule, RequireReturnsExports)
{
    Fixture f;
    EXPECT_EQ(f.Show("__lumora.requireModule('Echo').echo('hi')"), (Texts{"hi"}));
}

TEST(NativeModule, RequireCaches)
{
    Fixture f;
    EXPECT_EQ(f.Show("__lumora.requireModule('Echo') === __lumora.requireModule('Echo') ? 'same' : 'diff'"), (Texts{"same"}));
}

TEST(NativeModule, UnknownIsUndefined)
{
    Fixture f;
    EXPECT_EQ(f.Show("typeof __lumora.requireModule('Nope')"), (Texts{"undefined"}));
}

TEST(NativeModule, InvokerDeliversOnTick)
{
    Fixture f;
    ASSERT_TRUE(f.js.Load(std::string(kText) + "__lumora.requireModule('Echo').later('x').then((s)=>__lumora.setProp(t,'text',s));",
                          "app.js"))
        << f.js.LastError();
    for (int i = 0; i < 100 && f.host.TextContents() != Texts{"x"}; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        f.js.Tick(i * 16.0);
        f.Frame();
    }
    EXPECT_EQ(f.host.TextContents(), (Texts{"x"})) << f.js.LastError();
    EXPECT_TRUE(f.js.Loaded());
}

TEST(NativeModule, InvokerAfterUnloadIsDropped)
{
    Fixture f;
    const int before = g_laterPosted;
    ASSERT_TRUE(f.js.Load(std::string(kText) + "__lumora.requireModule('Echo').later('x').then((s)=>__lumora.setProp(t,'text',s));",
                          "app.js"))
        << f.js.LastError();
    f.js.Unload();
    ASSERT_TRUE(f.js.Load(kText, "other.js")) << f.js.LastError();
    while (g_laterPosted == before) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    for (int i = 0; i < 5; ++i) f.js.Tick(i * 16.0);
    f.Frame();
    EXPECT_EQ(f.host.TextContents(), (Texts{"start"}));
    EXPECT_TRUE(f.js.Loaded()) << f.js.LastError();
}
