#include <gtest/gtest.h>

#include <lumora/Host/HostTree.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"
#include "Host/HostTreeAccess.h"

#include <cstring>
#include <string>
#include <vector>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

using Texts = std::vector<std::string>;

[[nodiscard]] Event Mouse(EventType type, Vec2 at) {
    Event ev{};
    ev.Type     = type;
    ev.Position = at;
    ev.Button   = PointerButton::Left;
    return ev;
}

[[nodiscard]] Event Typed(const char* text) {
    Event ev{};
    ev.Type = EventType::TextInput;
    std::strncpy(ev.Text, text, sizeof(ev.Text) - 1);
    return ev;
}

void Frame(MountedApp& app) {
    app.Tick(1.f / 60.f);
    app.Layout(kSurfaceW, kSurfaceH);
}

void Click(MountedApp& app, const HostTree& host, HostHandle node) {
    const Rect r = HostTreeTestAccess::Solved(host, node);
    const Vec2 at{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    app.Dispatch(Mouse(EventType::MouseDown, at));
    app.Dispatch(Mouse(EventType::MouseUp, at));
}

struct Fixture {
    FakeFonts  fonts;
    MountedApp app{Box({}), &fonts};
    HostTree&  host = app.Host();

    Fixture() { app.SetInteractionEnv(&fonts, nullptr); }

    HostHandle MakeText(const char* text) {
        const HostHandle h = host.Create(HostTag::Text);
        host.SetProp(h, "text", std::string{text});
        return h;
    }
};

TEST(HostTree, CreateInsertBuildsLiveText) {
    Fixture f;
    const HostHandle t = f.MakeText("hi");
    EXPECT_TRUE(f.host.TextContents().empty());
    f.host.Insert(f.host.Root(), t);
    EXPECT_EQ(f.host.TextContents(), (Texts{"hi"}));
    EXPECT_EQ(f.host.HandleCount(), 1u);
}

TEST(HostTree, TextPropUpdatesAfterInsert) {
    Fixture f;
    const HostHandle t = f.host.Create(HostTag::Text);
    f.host.Insert(f.host.Root(), t);
    f.host.SetProp(t, "text", std::string{"a"});
    f.host.SetProp(t, "text", std::string{"b"});
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{"b"}));
}

TEST(HostTree, InsertBeforeOrders) {
    Fixture f;
    const HostHandle a = f.MakeText("a");
    const HostHandle b = f.MakeText("b");
    const HostHandle c = f.MakeText("c");
    f.host.Insert(f.host.Root(), c);
    f.host.Insert(f.host.Root(), a, c);
    f.host.Insert(f.host.Root(), b, c);
    EXPECT_EQ(f.host.TextContents(), (Texts{"a", "b", "c"}));
}

TEST(HostTree, InsertAttachedNodeMoves) {
    Fixture f;
    const HostHandle a = f.MakeText("a");
    const HostHandle b = f.MakeText("b");
    f.host.Insert(f.host.Root(), a);
    f.host.Insert(f.host.Root(), b);
    const std::size_t count = f.host.HandleCount();
    f.host.Insert(f.host.Root(), a);
    EXPECT_EQ(f.host.TextContents(), (Texts{"b", "a"}));
    EXPECT_EQ(f.host.HandleCount(), count);
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{"b", "a"}));
}

TEST(HostTree, RemoveThenReinsertWorks) {
    Fixture f;
    const HostHandle view2 = f.host.Create(HostTag::View);
    const HostHandle z     = f.MakeText("z");
    const HostHandle a     = f.MakeText("a");
    f.host.Insert(f.host.Root(), view2);
    f.host.Insert(f.host.Root(), z);
    f.host.Insert(f.host.Root(), a);
    Frame(f.app);

    f.host.Remove(f.host.Root(), a);
    EXPECT_EQ(f.host.TextContents(), (Texts{"z"}));
    Frame(f.app);

    f.host.Insert(view2, a);
    EXPECT_EQ(f.host.TextContents(), (Texts{"a", "z"}));
    Frame(f.app);
    EXPECT_EQ(f.host.HandleCount(), 3u);
}

TEST(HostTree, ReleaseWhileAttachedKeepsNode) {
    Fixture f;
    const HostHandle a = f.MakeText("a");
    f.host.Insert(f.host.Root(), a);
    f.host.Release(a);
    EXPECT_EQ(f.host.TextContents(), (Texts{"a"}));
    EXPECT_EQ(f.host.HandleCount(), 1u);
    f.host.Remove(f.host.Root(), a);
    EXPECT_EQ(f.host.HandleCount(), 0u);

    // Never inserted: released means freed at once.
    f.host.Release(f.host.Create(HostTag::View));
    EXPECT_EQ(f.host.HandleCount(), 0u);

    // A freed parent leaves its still-held child alive and detached.
    const HostHandle p = f.host.Create(HostTag::View);
    const HostHandle k = f.MakeText("k");
    f.host.Insert(p, k);
    f.host.Insert(f.host.Root(), p);
    Frame(f.app);
    f.host.Release(p);
    f.host.Remove(f.host.Root(), p);
    EXPECT_EQ(f.host.HandleCount(), 1u);
    EXPECT_TRUE(f.host.TextContents().empty());
    f.host.SetProp(k, "text", std::string{"k2"});
    f.host.Insert(f.host.Root(), k);
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{"k2"}));
}

TEST(HostTree, ClearFreesEverything) {
    Fixture f;
    Frame(f.app);
    const std::uint32_t baseline = HostTreeTestAccess::LiveNodes(f.host);

    const HostHandle a = f.host.Create(HostTag::View);
    const HostHandle b = f.host.Create(HostTag::Scroll);
    const HostHandle c = f.MakeText("c");
    const HostHandle d = f.host.Create(HostTag::Button);
    f.host.Insert(b, c);
    f.host.Insert(a, b);
    f.host.Insert(f.host.Root(), a);
    f.host.Insert(f.host.Root(), d);
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{"c"}));
    EXPECT_GT(HostTreeTestAccess::LiveNodes(f.host), baseline);

    for (const HostHandle h : {a, b, c, d}) {
        f.host.Release(h);
    }
    EXPECT_EQ(f.host.HandleCount(), 4u);
    f.host.Clear();
    EXPECT_EQ(f.host.HandleCount(), 0u);
    EXPECT_EQ(HostTreeTestAccess::LiveNodes(f.host), baseline);
    EXPECT_TRUE(f.host.TextContents().empty());
    Frame(f.app);
}

TEST(HostTree, ClickHandlerFires) {
    Fixture f;
    const HostHandle button = f.host.Create(HostTag::Button);
    f.host.SetProp(button, "style", std::string{"width: 100px; height: 40px"});
    int clicks = 0;
    f.host.SetHandler(button, HostEvent::Click, [&clicks](const HostEventArgs&) {
        ++clicks;
        return true;
    });
    f.host.Insert(f.host.Root(), button);
    f.app.Layout(kSurfaceW, kSurfaceH);

    Click(f.app, f.host, button);
    EXPECT_EQ(clicks, 1);
}

TEST(HostTree, SwapHandlerAfterBuild) {
    Fixture f;
    const HostHandle button = f.host.Create(HostTag::Button);
    f.host.SetProp(button, "style", std::string{"width: 100px; height: 40px"});
    int first = 0;
    int second = 0;
    f.host.SetHandler(button, HostEvent::Click, [&first](const HostEventArgs&) { return ++first, true; });
    f.host.Insert(f.host.Root(), button);
    f.host.SetHandler(button, HostEvent::Click, [&second](const HostEventArgs&) { return ++second, true; });
    f.app.Layout(kSurfaceW, kSurfaceH);

    Click(f.app, f.host, button);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);

    f.host.SetHandler(button, HostEvent::Click, nullptr);
    Click(f.app, f.host, button);
    EXPECT_EQ(second, 1);

    // A handler removing and releasing its own node mid-dispatch: applied at the next Tick.
    HostTree& host = f.host;
    f.host.SetHandler(button, HostEvent::Click, [&host, button](const HostEventArgs&) {
        host.Remove(host.Root(), button);
        host.Release(button);
        return true;
    });
    Click(f.app, f.host, button);
    EXPECT_EQ(f.host.HandleCount(), 1u);
    Frame(f.app);
    EXPECT_EQ(f.host.HandleCount(), 0u);
}

TEST(HostTree, InputValueAndInputEvent) {
    Fixture f;
    const HostHandle input = f.host.Create(HostTag::Input);
    f.host.SetProp(input, "value", std::string{"x"});
    std::string seen;
    f.host.SetHandler(input, HostEvent::Input, [&seen](const HostEventArgs& e) {
        seen = e.text;
        return true;
    });
    f.host.Insert(f.host.Root(), input);
    Frame(f.app);

    f.app.FocusNext();
    Event end{};
    end.Type    = EventType::KeyDown;
    end.KeyCode = Key::End;
    f.app.Dispatch(end);
    f.app.Dispatch(Typed("y"));
    Frame(f.app);
    EXPECT_EQ(seen, "xy");
}

TEST(HostTree, UnknownPropReturnsFalse) {
    Fixture f;
    const HostHandle view = f.host.Create(HostTag::View);
    const HostHandle text = f.host.Create(HostTag::Text);
    EXPECT_FALSE(f.host.SetProp(view, "bogus", 1.0));
    EXPECT_FALSE(f.host.SetProp(view, "text", std::string{"x"}));
    EXPECT_TRUE(f.host.SetProp(view, "class", std::string{"a b"}));
    EXPECT_TRUE(f.host.SetProp(text, "text", 3.0));
    f.host.Insert(f.host.Root(), view);
    f.host.Insert(view, text);
    EXPECT_EQ(f.host.TextContents(), (Texts{"3"}));
    EXPECT_TRUE(f.host.SetProp(text, "text", HostValue{}));
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{""}));
}

TEST(HostTree, CycleInsertIsRejected) {
    Fixture f;
    const HostHandle parent = f.host.Create(HostTag::View);
    const HostHandle child  = f.host.Create(HostTag::View);
    const HostHandle t      = f.MakeText("t");
    f.host.Insert(f.host.Root(), parent);
    f.host.Insert(parent, child);
    f.host.Insert(child, t);

    f.host.Insert(child, parent);
    f.host.Insert(parent, parent);
    f.host.Insert(t, f.host.Create(HostTag::Text));
    EXPECT_EQ(f.host.TextContents(), (Texts{"t"}));
    Frame(f.app);
    EXPECT_EQ(f.host.TextContents(), (Texts{"t"}));
}

TEST(HostTree, ClassAndStyleApply) {
    Fixture f;
    f.app.SetStylesheetSource(".big{width:50px}");
    const HostHandle view = f.host.Create(HostTag::View);
    f.host.SetProp(view, "class", std::string{"big"});
    f.host.SetProp(view, "style", std::string{"height:20px"});
    f.host.Insert(f.host.Root(), view);
    Frame(f.app);

    const Rect r = HostTreeTestAccess::Solved(f.host, view);
    EXPECT_FLOAT_EQ(r.width, 50.f);
    EXPECT_FLOAT_EQ(r.height, 20.f);

    f.host.SetProp(view, "style", std::string{"height:30px"});
    Frame(f.app);
    EXPECT_FLOAT_EQ(HostTreeTestAccess::Solved(f.host, view).height, 30.f);
}

TEST(HostTree, ParseNames) {
    EXPECT_EQ(ParseHostTag("scroll"), HostTag::Scroll);
    EXPECT_EQ(ParseHostTag("view"), HostTag::View);
    EXPECT_EQ(ParseHostTag("div"), std::nullopt);
    EXPECT_EQ(ParseHostEvent("dblclick"), HostEvent::DoubleClick);
    EXPECT_EQ(ParseHostEvent("submit"), HostEvent::Submit);
    EXPECT_EQ(ParseHostEvent("doubleclick"), std::nullopt);
    EXPECT_EQ(KeyName(Key::Enter), "Enter");
    EXPECT_EQ(KeyName(Key::A), "a");
}

} // namespace
