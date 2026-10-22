#include <gtest/gtest.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Widgets/Widgets.h>
#include <lumora/Paint/Canvas.h>

#include "FakeFonts.h"
#include "Graph/Paint.h"
#include "Graph/State.h"
#include "Layout/LayoutBridge.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "RecordingCanvas.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"

#include <functional>

using namespace lumora;
using namespace lumora::nodegraph;
using lumora::MakeRef;
using lumora::Ref;

namespace
{
    constexpr float kSurfaceW = 800.f;
    constexpr float kSurfaceH = 600.f;

    struct Harness
    {
        FakeFonts          fonts;
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        GraphScope         scope{*graph};
        NodeArena          arena;
        LayoutTree         layout;
        AppCtx             ctx{arena, *graph, &layout};

        Harness() { ctx.measurer = &fonts; }

        lumora::NodeId Mount(const Elem& tree, Owner& owner)
        {
            return Instantiate(ctx, *tree.RawNode(), lumora::NodeId{}, owner);
        }
    };

    struct PaintRun
    {
        Harness         h;
        Owner           owner;
        RecordingCanvas rc;

        void Run(State& state, const ViewCfg& cfg)
        {
            const lumora::NodeId root = h.Mount(
                Painter({.onPaint = [&state, &cfg](Paint2D& p) { PaintGraph(state, p, cfg); }}),
                owner);
            lumora::Layout(h.ctx, root, kSurfaceW, kSurfaceH);
            Paint(h.ctx, root, rc);
        }
    };

    struct Fixture
    {
        Controller ctrl;
        ViewCfg    cfg;
        NodeTypeId plain;
        NodeTypeId preview;

        Fixture()
        {
            NodeTypeDesc p;
            p.title = "Op";
            p.minWidth = 150.f;
            p.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
            p.body = {PinRowCell{.input = 0, .output = 1}, LabelCell{.text = "label"}};
            plain = ctrl.Type("plain", std::move(p));

            NodeTypeDesc q;
            q.title = "Tex";
            q.minWidth = 150.f;
            q.pins = {PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
            q.body = {PinRowCell{.output = 0},
                      GroupCell{.title = "Preview",
                                .resizable = true,
                                .children = {ImageCell{.image = [](nodegraph::GraphNodeId)
                                                       { return ImageHandle{.id = 42, .width = 64.f,
                                                                            .height = 64.f}; }}}}};
            preview = ctrl.Type("tex", std::move(q));
        }

        [[nodiscard]] State& S() { return nodegraph::detail::Access(ctrl); }
    };
} // namespace

TEST(GraphPaintTests, AnEmptyGraphStillPaintsItsBackgroundAndGrid)
{
    Fixture  f;
    PaintRun frame;
    frame.Run(f.S(), f.cfg);

    EXPECT_TRUE(frame.rc.Balanced());
    EXPECT_GT(frame.rc.CountOp(CanvasCall::Op::Rect), 1);
}

TEST(GraphPaintTests, TurningTheGridOffLeavesOnlyTheBackground)
{
    Fixture f;
    f.cfg.showGrid = false;
    PaintRun frame;
    frame.Run(f.S(), f.cfg);

    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Rect), 1);
    const CanvasCall* background = nullptr;
    for (const CanvasCall& c : frame.rc.calls)
    {
        if (c.op == CanvasCall::Op::Rect)
        {
            background = &c;
            break;
        }
    }
    ASSERT_NE(background, nullptr);
    EXPECT_EQ(background->rect.fill, f.ctrl.Visuals().Background);
}

TEST(GraphPaintTests, TheMinorGridIsDroppedWhenZoomedOut)
{
    Fixture  f;
    PaintRun inside;
    f.ctrl.View().SetZoom(0.6f);
    inside.Run(f.S(), f.cfg);
    const int withMinor = inside.rc.CountOp(CanvasCall::Op::Rect);

    Fixture  g;
    PaintRun outside;
    g.ctrl.View().SetZoom(0.4f);
    outside.Run(g.S(), g.cfg);
    const int withoutMinor = outside.rc.CountOp(CanvasCall::Op::Rect);

    EXPECT_LT(withoutMinor, withMinor);
}

TEST(GraphPaintTests, ANodePaintsItsChromeItsTitleAndItsPins)
{
    Fixture f;
    f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});
    f.cfg.showGrid = false;

    PaintRun frame;
    frame.Run(f.S(), f.cfg);

    EXPECT_TRUE(frame.rc.Balanced());
    EXPECT_GE(frame.rc.CountOp(CanvasCall::Op::Rect), 3);
    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Shape), 2);
    EXPECT_GE(frame.rc.CountOp(CanvasCall::Op::Glyphs), 1);
}

TEST(GraphPaintTests, ACulledNodePaintsNothing)
{
    Fixture f;
    f.ctrl.AddNode(f.plain, Vec2{50000.f, 50000.f});
    f.cfg.showGrid = false;

    PaintRun frame;
    frame.Run(f.S(), f.cfg);

    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Rect), 1);
    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Shape), 0);
}

TEST(GraphPaintTests, ACollapsedNodePaintsNoCells)
{
    Fixture f;
    const nodegraph::GraphNodeId a = f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});
    f.cfg.showGrid = false;

    PaintRun open;
    open.Run(f.S(), f.cfg);
    const int openGlyphs = open.rc.CountOp(CanvasCall::Op::Glyphs);

    f.ctrl.SetCollapsed(a, true);
    PaintRun shut;
    shut.Run(f.S(), f.cfg);

    EXPECT_LT(shut.rc.CountOp(CanvasCall::Op::Glyphs), openGlyphs);
    EXPECT_EQ(shut.rc.CountOp(CanvasCall::Op::Shape), 2);
}

TEST(GraphPaintTests, AnImageCellDrawsTheHandleItsProviderReturns)
{
    Fixture f;
    f.ctrl.AddNode(f.preview, Vec2{40.f, 40.f});
    f.cfg.showGrid = false;

    PaintRun frame;
    frame.Run(f.S(), f.cfg);

    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Image), 1);
    const CanvasCall* image = nullptr;
    for (const CanvasCall& c : frame.rc.calls)
    {
        if (c.op == CanvasCall::Op::Image)
        {
            image = &c;
        }
    }
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->image.id, 42u);
}

TEST(GraphPaintTests, AClosedPreviewGroupDrawsNoImage)
{
    Fixture f;
    const nodegraph::GraphNodeId a = f.ctrl.AddNode(f.preview, Vec2{40.f, 40.f});
    f.ctrl.SetGroupOpen(a, 0, false);
    f.cfg.showGrid = false;

    PaintRun frame;
    frame.Run(f.S(), f.cfg);
    EXPECT_EQ(frame.rc.CountOp(CanvasCall::Op::Image), 0);
}

TEST(GraphPaintTests, ALinkPaintsAsTessellatedGeometry)
{
    Fixture f;
    const nodegraph::GraphNodeId a = f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});
    const nodegraph::GraphNodeId b = f.ctrl.AddNode(f.plain, Vec2{400.f, 40.f});
    f.ctrl.Connect(PinRef{a, 1, PinDir::Output}, PinRef{b, 0, PinDir::Input});
    f.cfg.showGrid = false;

    PaintRun with;
    with.Run(f.S(), f.cfg);
    EXPECT_GE(with.rc.CountOp(CanvasCall::Op::Triangles), 1);
}

TEST(GraphPaintTests, ASelectedNodeGetsAnExtraOutline)
{
    Fixture f;
    const nodegraph::GraphNodeId a = f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});
    f.cfg.showGrid = false;

    PaintRun plain;
    plain.Run(f.S(), f.cfg);
    const int plainRects = plain.rc.CountOp(CanvasCall::Op::Rect);

    f.ctrl.Select(std::vector<nodegraph::GraphNodeId>{a});
    PaintRun selected;
    selected.Run(f.S(), f.cfg);

    EXPECT_GT(selected.rc.CountOp(CanvasCall::Op::Rect), plainRects);
}

TEST(GraphPaintTests, AnInFlightLinkAndABoxSelectionPaintOnTop)
{
    Fixture f;
    const nodegraph::GraphNodeId a = f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});
    f.cfg.showGrid = false;

    State& s = f.S();

    PaintRun idle;
    idle.Run(s, f.cfg);
    const int quiet = idle.rc.CountOp(CanvasCall::Op::Rect);

    s.tool = Tool::DragLink;
    s.pendingFrom = PinRef{a, 1, PinDir::Output};
    s.pendingToCanvas = Vec2{400.f, 200.f};
    s.pendingLegal = true;

    PaintRun link;
    link.Run(s, f.cfg);
    const int withLink = link.rc.CountOp(CanvasCall::Op::Triangles);

    s.tool = Tool::BoxSelect;
    s.boxSelect = Rect{10.f, 10.f, 200.f, 150.f};
    PaintRun box;
    box.Run(s, f.cfg);

    EXPECT_GE(withLink, 1);
    EXPECT_TRUE(box.rc.Balanced());
    EXPECT_GT(box.rc.CountOp(CanvasCall::Op::Rect), quiet);
}

TEST(GraphPaintTests, RepaintingAnUnchangedGraphIsDeterministic)
{
    Fixture f;
    f.ctrl.AddNode(f.plain, Vec2{40.f, 40.f});

    PaintRun first;
    first.Run(f.S(), f.cfg);
    PaintRun second;
    second.Run(f.S(), f.cfg);

    EXPECT_EQ(first.rc.calls.size(), second.rc.calls.size());
}

TEST(GraphPaintTests, TheDebugOverlayIsOffByDefault)
{
    Fixture f;
    f.cfg.showGrid = false;
    PaintRun off;
    off.Run(f.S(), f.cfg);
    const int quiet = off.rc.CountOp(CanvasCall::Op::Glyphs);

    f.cfg.showDebug = true;
    PaintRun on;
    on.Run(f.S(), f.cfg);
    EXPECT_GT(on.rc.CountOp(CanvasCall::Op::Glyphs), quiet);
}
