#include <gtest/gtest.h>

#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

constexpr float kAX = 20.f, kAY = 20.f;
constexpr float kBX = 220.f, kBY = 20.f;

[[nodiscard]] Event Mouse(EventType type, float x, float y) {
    Event ev{};
    ev.Type     = type;
    ev.Position = {x, y};
    ev.Button   = PointerButton::Left;
    return ev;
}

void Frame(MountedApp& app) {
    app.Tick(1.f / 60.f);
    app.Layout(kSurfaceW, kSurfaceH);
}

struct Probe {
    int clicks  = 0;
    int doubles = 0;
};

[[nodiscard]] Elem Target(Probe& p, float x, float y) {
    BoxCfg cfg{.layout   = {.width = Fixed(100.f), .height = Fixed(60.f)},
               .position = {.position = Position::Absolute, .inset = {.left = x, .top = y}}};
    cfg.on.onClick       = [&p] { ++p.clicks; };
    cfg.on.onDoubleClick = [&p] { ++p.doubles; };
    return Box(std::move(cfg));
}

[[nodiscard]] Elem Root(Elem a, Elem b) {
    return Stack(StackCfg{.layout = {.width = Expand, .height = Expand}, .anchor = true},
                 std::move(a), std::move(b));
}

void Click(MountedApp& app, float x, float y) {
    app.Dispatch(Mouse(EventType::MouseDown, x, y));
    app.Dispatch(Mouse(EventType::MouseUp, x, y));
}

TEST(DoubleClickTests, TwoClicksInsideTheWindowFireOnce) {
    Probe p;
    FakeFonts fonts;
    Probe unused;
    MountedApp app(Root(Target(p, kAX, kAY), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 10.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(p.doubles, 1);
    EXPECT_EQ(p.clicks, 2);
}

TEST(DoubleClickTests, TwoClicksOutsideTheWindowFireNothing) {
    Probe p;
    FakeFonts fonts;
    Probe unused;
    MountedApp app(Root(Target(p, kAX, kAY), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    for (int i = 0; i < 40; ++i) Frame(app);
    Click(app, kAX + 10.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(p.doubles, 0);
    EXPECT_EQ(p.clicks, 2);
}

TEST(DoubleClickTests, ClicksOnDifferentNodesFireNothing) {
    Probe a, b;
    FakeFonts fonts;
    MountedApp app(Root(Target(a, kAX, kAY), Target(b, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kBX + 10.f, kBY + 10.f);
    Frame(app);

    EXPECT_EQ(a.doubles, 0);
    EXPECT_EQ(b.doubles, 0);
    EXPECT_EQ(a.clicks, 1);
    EXPECT_EQ(b.clicks, 1);
}

TEST(DoubleClickTests, ThreeClicksFireOnceNotTwice) {
    Probe p;
    FakeFonts fonts;
    Probe unused;
    MountedApp app(Root(Target(p, kAX, kAY), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 10.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(p.doubles, 1);
    EXPECT_EQ(p.clicks, 3);
}

TEST(DoubleClickTests, AMovedSecondClickFiresNothing) {
    Probe p;
    FakeFonts fonts;
    Probe unused;
    MountedApp app(Root(Target(p, kAX, kAY), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 30.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(p.doubles, 0);
    EXPECT_EQ(p.clicks, 2);
}

TEST(DoubleClickTests, ANodeWithOnlyDoubleClickStillFires) {
    int doubles = 0;
    FakeFonts fonts;
    BoxCfg cfg{.layout   = {.width = Fixed(100.f), .height = Fixed(60.f)},
               .position = {.position = Position::Absolute,
                            .inset    = {.left = kAX, .top = kAY}}};
    cfg.on.onDoubleClick = [&doubles] { ++doubles; };

    Probe unused;
    MountedApp app(Root(Box(std::move(cfg)), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 10.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(doubles, 1);
}

TEST(DoubleClickTests, ADisabledNodeFiresNothing) {
    Probe p;
    FakeFonts fonts;
    BoxCfg cfg{.layout   = {.width = Fixed(100.f), .height = Fixed(60.f)},
               .position = {.position = Position::Absolute,
                            .inset    = {.left = kAX, .top = kAY}}};
    cfg.on.onClick       = [&p] { ++p.clicks; };
    cfg.on.onDoubleClick = [&p] { ++p.doubles; };
    cfg.on.disabled      = true;

    Probe unused;
    MountedApp app(Root(Box(std::move(cfg)), Target(unused, kBX, kBY)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    Click(app, kAX + 10.f, kAY + 10.f);
    Click(app, kAX + 10.f, kAY + 10.f);
    Frame(app);

    EXPECT_EQ(p.doubles, 0);
    EXPECT_EQ(p.clicks, 0);
}

} // namespace
