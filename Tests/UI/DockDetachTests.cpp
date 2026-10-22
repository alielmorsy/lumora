#include <gtest/gtest.h>

#include <lumora/Dock/Dock.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Paint/NullCanvas.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"

#include <algorithm>
#include <optional>
#include <vector>

using namespace lumora;

namespace {

constexpr float kW = 400.f;
constexpr float kH = 300.f;

[[nodiscard]] Event Mouse(EventType type, float x, float y) {
    Event ev{};
    ev.Type     = type;
    ev.Position = {x, y};
    ev.Button   = PointerButton::Left;
    return ev;
}

void Frame(MountedApp& app) {
    app.Tick(1.f / 60.f);
    app.Layout(kW, kH);
    NullCanvas canvas;
    app.Paint(canvas);
}

DockController MakeDock(std::vector<const char*> ids) {
    std::vector<PanelId> panels;
    for (const char* id : ids) panels.push_back(PanelId{id});
    DockController dock{DockTree{.root = DockNode::Tabs(panels)}};
    for (const char* id : ids) dock.Panel(id, id, [] { return Box({}); });
    return dock;
}

void DragFirstTabTo(MountedApp& app, float x, float y) {
    app.Dispatch(Mouse(EventType::MouseDown, 20.f, 15.f));
    Frame(app);
    app.Dispatch(Mouse(EventType::MouseMove, 40.f, 15.f));
    Frame(app);
    app.Dispatch(Mouse(EventType::MouseMove, x, y));
    Frame(app);
    app.Dispatch(Mouse(EventType::MouseUp, x, y));
    Frame(app);
}

constexpr Vec2 kCentreGuide{200.f, 150.f};
constexpr Vec2 kRightGuide{238.f, 150.f};
constexpr Vec2 kOffGuide{100.f, 250.f};

TEST(DockDetachTests, ReleaseOutsideTakenRemovesThePanel) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    std::optional<PanelId> taken;
    int hovers = 0;
    dock.SetDetachHooks({
        .onDragOutside = [&](PanelId, std::optional<Vec2> at) { if (at) ++hovers; },
        .onDetach = [&](PanelId p, Vec2, Vec2) { taken = p; return true; },
    });
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, 600.f, 15.f);

    EXPECT_TRUE(taken.has_value() && *taken == PanelId{"a"});
    EXPECT_GE(hovers, 1);
    const std::vector<PanelId> left = CollectPanels(dock.Model().Peek());
    EXPECT_TRUE(left.size() == 1 && left[0] == PanelId{"b"});
    EXPECT_TRUE(dock.Model().Peek().floats.empty());
}

TEST(DockDetachTests, ReleaseOutsideRefusedFloatsAsBefore) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    dock.SetDetachHooks({.onDetach = [](PanelId, Vec2, Vec2) { return false; }});
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, 600.f, 15.f);

    EXPECT_EQ(dock.Model().Peek().floats.size(), 1u);
    EXPECT_EQ(CollectPanels(dock.Model().Peek()).size(), 2u);
}

TEST(DockDetachTests, ReleaseInsideNeverAsksTheHost) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    int asked = 0;
    dock.SetDetachHooks({.onDetach = [&](PanelId, Vec2, Vec2) { ++asked; return true; }});
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, kOffGuide.x, kOffGuide.y);

    EXPECT_EQ(asked, 0);
    EXPECT_EQ(CollectPanels(dock.Model().Peek()).size(), 2u);
    EXPECT_EQ(dock.Model().Peek().floats.size(), 1u);
}

TEST(DockDetachTests, DetachPolicyInsideReleaseAsksHost) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    dock.SetBehavior({.floats = DockFloatPolicy::Detach});
    int asked = 0;
    dock.SetDetachHooks({.onDetach = [&](PanelId, Vec2, Vec2) { ++asked; return true; }});
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, kOffGuide.x, kOffGuide.y);

    EXPECT_EQ(asked, 1);
    const std::vector<PanelId> left = CollectPanels(dock.Model().Peek());
    EXPECT_TRUE(left.size() == 1 && left[0] == PanelId{"b"});
    EXPECT_TRUE(dock.Model().Peek().floats.empty());
}

TEST(DockDetachTests, DetachPolicyRefusedCancels) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    dock.SetBehavior({.floats = DockFloatPolicy::Detach});
    dock.SetDetachHooks({.onDetach = [](PanelId, Vec2, Vec2) { return false; }});
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, kOffGuide.x, kOffGuide.y);

    const DockTree& t = dock.Model().Peek();
    EXPECT_TRUE(t.floats.empty());
    EXPECT_TRUE(t.root.IsTabs() && t.root.panels.size() == 2 && t.root.panels[0] == PanelId{"a"});
}

TEST(DockDetachTests, LockedDockIgnoresGuides) {
    FakeFonts fonts;
    DockController dock = MakeDock({"a", "b"});
    dock.SetBehavior({.acceptsDrops = false});
    int asked = 0;
    dock.SetDetachHooks({.onDetach = [&](PanelId, Vec2, Vec2) { ++asked; return false; }});
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    DragFirstTabTo(app, kCentreGuide.x, kCentreGuide.y);

    EXPECT_EQ(asked, 1);
    const DockTree& t = dock.Model().Peek();
    EXPECT_TRUE(t.floats.empty());
    EXPECT_TRUE(t.root.IsTabs() && t.root.panels.size() == 2 && t.root.panels[0] == PanelId{"a"});

    dock.Panel("c", "c", [] { return Box({}); });
    EXPECT_FALSE(dock.CanAcceptExternal(kCentreGuide));
    EXPECT_FALSE(dock.CanAcceptExternal(kRightGuide));
    EXPECT_FALSE(dock.CanAcceptExternal(kOffGuide));
    EXPECT_FALSE(dock.AcceptExternal(PanelId{"c"}, kCentreGuide));
    EXPECT_EQ(CollectPanels(dock.Model().Peek()).size(), 2u);
}

TEST(DockDetachTests, AcceptExternalUsesGuides) {
    FakeFonts fonts;
    DockController dock = MakeDock({"c"});
    dock.Panel("a", "a", [] { return Box({}); });
    dock.Panel("d", "d", [] { return Box({}); });
    MountedApp app(Dock(dock), &fonts);
    Frame(app);

    EXPECT_FALSE(dock.CanAcceptExternal({900.f, 900.f}));
    EXPECT_FALSE(dock.AcceptExternal(PanelId{"a"}, {900.f, 900.f}));
    EXPECT_FALSE(dock.CanAcceptExternal(kOffGuide));
    EXPECT_FALSE(dock.AcceptExternal(PanelId{"a"}, kOffGuide));
    EXPECT_EQ(CollectPanels(dock.Model().Peek()).size(), 1u);

    EXPECT_TRUE(dock.CanAcceptExternal(kCentreGuide));
    EXPECT_TRUE(dock.AcceptExternal(PanelId{"a"}, kCentreGuide));
    Frame(app);
    EXPECT_TRUE(dock.Model().Peek().root.IsTabs());
    EXPECT_EQ(dock.Model().Peek().root.panels.size(), 2u);

    EXPECT_TRUE(dock.CanAcceptExternal(kRightGuide));
    EXPECT_TRUE(dock.AcceptExternal(PanelId{"d"}, kRightGuide));
    Frame(app);
    const DockNode& root = dock.Model().Peek().root;
    EXPECT_TRUE(root.IsSplit() && root.axis == DockAxis::Horizontal);
    EXPECT_EQ(CollectPanels(dock.Model().Peek()).size(), 3u);
}

TEST(DockDetachTests, CollectPanelsIncludesFloats) {
    DockTree t{.root = DockNode::Tabs({PanelId{"a"}})};
    t.floats.push_back({DockNode::Tabs({PanelId{"b"}}), {}});
    const std::vector<PanelId> all = CollectPanels(t);
    EXPECT_EQ(all.size(), 2u);
    EXPECT_TRUE(std::ranges::find(all, PanelId{"b"}) != all.end());
}

} // namespace
