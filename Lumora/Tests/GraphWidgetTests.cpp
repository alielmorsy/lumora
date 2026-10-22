#include <gtest/gtest.h>
#include <lumora/Graph/Graph.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Paint/NullCanvas.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"
#include "Graph/State.h"
#include "RecordingCanvas.h"

#include <vector>

using namespace lumora;
using namespace lumora::nodegraph;

namespace
{
    constexpr float kSurfaceW = 800.f;
    constexpr float kSurfaceH = 600.f;

    [[nodiscard]] Event Mouse(EventType type, Vec2 at, KeyMods mods = KeyMods::None)
    {
        Event ev{};
        ev.Type = type;
        ev.Position = at;
        ev.Button = PointerButton::Left;
        ev.Mods = mods;
        return ev;
    }

    [[nodiscard]] Event Wheel(Vec2 at, float notches)
    {
        Event ev{};
        ev.Type = EventType::Scroll;
        ev.Position = at;
        ev.ScrollDelta = Vec2{0.f, notches};
        return ev;
    }

    struct Fixture
    {
        Controller  ctrl;
        FakeFonts   fonts;
        NodeTypeId  type;

        Fixture()
        {
            NodeTypeDesc d;
            d.title = "Op";
            d.minWidth = 150.f;
            d.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output,
                              .multi = true}};
            d.body = {PinRowCell{.input = 0, .output = 1}};
            type = ctrl.Type("op", std::move(d));
        }

        [[nodiscard]] State& S() { return nodegraph::detail::Access(ctrl); }
    };

    void StepFrame(MountedApp& app, Canvas& canvas)
    {
        app.Tick(1.f / 60.f);
        app.Layout(kSurfaceW, kSurfaceH);
        app.Paint(canvas);
    }
} // namespace

TEST(GraphWidgetTests, PaintingPublishesTheViewportIntoState)
{
    Fixture     f;
    MountedApp  app(NodeGraph(f.ctrl), &f.fonts);
    NullCanvas  canvas;
    StepFrame(app, canvas);

    EXPECT_GT(f.ctrl.Viewport().width, 0.f);
    EXPECT_GT(f.ctrl.Viewport().height, 0.f);
}

TEST(GraphWidgetTests, TheCfgThemeSeedsTheLiveThemeOnceAtMount)
{
    Fixture f;
    ViewCfg cfg;
    cfg.theme.HeaderHeight = 44.f;

    MountedApp app(NodeGraph(f.ctrl, cfg), &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    EXPECT_FLOAT_EQ(f.ctrl.Visuals().HeaderHeight, 44.f);
}

TEST(GraphWidgetTests, PressingANodeThroughTheWidgetSelectsIt)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{100.f, 100.f});

    MountedApp app(NodeGraph(f.ctrl), &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    f.S().EnsureLayout();
    const NodeGeom* geom = f.S().layout.Of(a);
    const Vec2      header =
        f.S().camera.ToLayer(geom->header.Center(), f.ctrl.Viewport());

    app.Dispatch(Mouse(EventType::MouseDown, header));
    app.Dispatch(Mouse(EventType::MouseUp, header));
    StepFrame(app, canvas);

    EXPECT_TRUE(f.ctrl.IsSelected(a));
}

TEST(GraphWidgetTests, DraggingANodeThroughTheWidgetMovesIt)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{100.f, 100.f});

    MountedApp app(NodeGraph(f.ctrl), &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    f.S().EnsureLayout();
    const NodeGeom* geom = f.S().layout.Of(a);
    const Vec2      header = f.S().camera.ToLayer(geom->header.Center(), f.ctrl.Viewport());

    app.Dispatch(Mouse(EventType::MouseDown, header));
    app.Dispatch(Mouse(EventType::MouseMove, header + Vec2{40.f, 25.f}));
    app.Dispatch(Mouse(EventType::MouseUp, header + Vec2{40.f, 25.f}));
    StepFrame(app, canvas);

    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{140.f, 125.f}));
}

TEST(GraphWidgetTests, AMoveThroughTheWidgetRepaintsTheNode)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{100.f, 100.f});

    MountedApp      app(NodeGraph(f.ctrl), &f.fonts);
    RecordingCanvas canvas;
    StepFrame(app, canvas);

    f.S().EnsureLayout();
    const NodeGeom* geom = f.S().layout.Of(a);
    const Vec2      header = f.S().camera.ToLayer(geom->header.Center(), f.ctrl.Viewport());

    app.Dispatch(Mouse(EventType::MouseDown, header));
    app.Dispatch(Mouse(EventType::MouseMove, header + Vec2{40.f, 0.f}));

    canvas.Clear();
    StepFrame(app, canvas);
    EXPECT_FALSE(canvas.calls.empty());
}

TEST(GraphWidgetTests, ScrollingThroughTheWidgetZooms)
{
    Fixture    f;
    MountedApp app(NodeGraph(f.ctrl), &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    app.Dispatch(Mouse(EventType::MouseMove, Vec2{400.f, 300.f}));
    app.Dispatch(Wheel(Vec2{400.f, 300.f}, 1.f));
    StepFrame(app, canvas);

    EXPECT_GT(f.ctrl.View().Zoom(), 1.f);
}

TEST(GraphWidgetTests, TheGraphTakesFocusAndConsumesDelete)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{100.f, 100.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a});

    MountedApp app(NodeGraph(f.ctrl), &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    f.S().EnsureLayout();
    const NodeGeom* geom = f.S().layout.Of(a);
    const Vec2      header = f.S().camera.ToLayer(geom->header.Center(), f.ctrl.Viewport());
    app.Dispatch(Mouse(EventType::MouseDown, header));
    app.Dispatch(Mouse(EventType::MouseUp, header));
    StepFrame(app, canvas);

    Event del{};
    del.Type = EventType::KeyDown;
    del.KeyCode = Key::Delete;
    EXPECT_TRUE(app.Dispatch(del));
    StepFrame(app, canvas);

    EXPECT_EQ(f.ctrl.Data().NodeCount(), 0u);
}

TEST(GraphWidgetTests, PointerMathsSurvivesAnAncestorOffset)
{
    Fixture           f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    MountedApp app(Box({.layout = {.width = Expand, .height = Expand,
                                   .padding = {60.f, 0.f, 0.f, 120.f}}},
                       NodeGraph(f.ctrl)),
                   &f.fonts);
    NullCanvas canvas;
    StepFrame(app, canvas);

    f.S().EnsureLayout();
    const NodeGeom* geom = f.S().layout.Of(a);
    const Vec2      layerHeader =
        f.S().camera.ToLayer(geom->header.Center(), f.ctrl.Viewport());
    const Vec2      surfaceHeader = layerHeader - f.S().surfaceToLayer;

    app.Dispatch(Mouse(EventType::MouseDown, surfaceHeader));
    app.Dispatch(Mouse(EventType::MouseUp, surfaceHeader));
    StepFrame(app, canvas);

    EXPECT_TRUE(f.ctrl.IsSelected(a));
}
