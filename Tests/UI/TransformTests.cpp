#include <gtest/gtest.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"
#include "Layout/LayoutBridge.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "RecordingCanvas.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleEngine.h"

#include <numbers>
#include <string>

using namespace lumora;

namespace
{
    struct Harness
    {
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        GraphScope scope{*graph};
        NodeArena arena;
        LayoutTree layout;
        style::StyleEngine engine;
        AppCtx ctx{arena, *graph, &layout};
        Owner owner;

        Harness() { ctx.style = &engine; }

        NodeId Mount(const Elem& tree) { return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner); }
        [[nodiscard]] const TransformState& Xf(NodeId id) { return arena.PayloadOf<ContainerPayload>(id).transform; }
    };

    NodeId MountBox(Harness& h, const char* css, Reactive<std::string> cls)
    {
        h.engine.LoadSource(css);
        const NodeId root =
            h.Mount(Column({}, Box({.layout = {.width = Fixed(100.f), .height = Fixed(40.f)}, .className = cls})));
        Layout(h.ctx, root, 200.f, 200.f);
        return h.arena.Resolve(root).firstChild;
    }
} // namespace

TEST(TransformTests, ParsesScaleRotateTranslate)
{
    Harness h;
    h.engine.LoadSource(".a { transform: scale(0.97); }"
                        ".b { transform: scaleX(2) scaleY(50%); }"
                        ".c { transform: translate(10px, 5px) rotate(90deg); }"
                        ".d { transform: scale(2, 3) rotate(0.5turn); }");
    const NodeId a = h.Mount(Box({.className = "a"}));
    const NodeId b = h.Mount(Box({.className = "b"}));
    const NodeId c = h.Mount(Box({.className = "c"}));
    const NodeId d = h.Mount(Box({.className = "d"}));

    EXPECT_FLOAT_EQ(h.Xf(a).scaleX, 0.97f);
    EXPECT_FLOAT_EQ(h.Xf(a).scaleY, 0.97f);
    EXPECT_FLOAT_EQ(h.Xf(b).scaleX, 2.f);
    EXPECT_FLOAT_EQ(h.Xf(b).scaleY, 0.5f);
    EXPECT_FLOAT_EQ(h.Xf(c).x, 10.f);
    EXPECT_FLOAT_EQ(h.Xf(c).y, 5.f);
    EXPECT_NEAR(h.Xf(c).rotate, std::numbers::pi_v<float> / 2.f, 1e-5f);
    EXPECT_FLOAT_EQ(h.Xf(d).scaleX, 2.f);
    EXPECT_FLOAT_EQ(h.Xf(d).scaleY, 3.f);
    EXPECT_NEAR(h.Xf(d).rotate, std::numbers::pi_v<float>, 1e-5f);
}

TEST(TransformTests, OriginAndRevert)
{
    Harness h;
    auto cls = Signal<std::string>("s");
    h.engine.LoadSource(".s { transform: scale(2); transform-origin: left top; } .p { transform-origin: 25% bottom; }");
    const NodeId n = h.Mount(Box({.className = cls}));
    EXPECT_FLOAT_EQ(h.Xf(n).originX, 0.f);
    EXPECT_FLOAT_EQ(h.Xf(n).originY, 0.f);

    cls = std::string("p");
    EXPECT_TRUE(h.Xf(n).IsIdentity());
    EXPECT_FLOAT_EQ(h.Xf(n).originX, 0.25f);
    EXPECT_FLOAT_EQ(h.Xf(n).originY, 1.f);

    cls = std::string("");
    EXPECT_FLOAT_EQ(h.Xf(n).originX, 0.5f);
    EXPECT_FLOAT_EQ(h.Xf(n).originY, 0.5f);
}

TEST(TransformTests, ScaleTransitions)
{
    Harness h;
    auto cls = Signal<std::string>("btn");
    const NodeId n = MountBox(h, ".btn { transition: transform 100ms linear; } .btn.down { transform: scale(0.9); }", cls);
    EXPECT_FLOAT_EQ(h.Xf(n).scaleX, 1.f);

    cls = std::string("btn down");
    EXPECT_FLOAT_EQ(h.Xf(n).scaleX, 1.f); // displayed value holds until the driver advances
    h.engine.AdvanceAnimations(h.arena, 0.05f);
    EXPECT_NEAR(h.Xf(n).scaleX, 0.95f, 1e-4f);
    EXPECT_NEAR(h.Xf(n).scaleY, 0.95f, 1e-4f);
    h.engine.AdvanceAnimations(h.arena, 0.06f);
    EXPECT_FLOAT_EQ(h.Xf(n).scaleX, 0.9f);
}

TEST(TransformTests, ScalePaintsLayerAboutCenterWithoutTouchingLayout)
{
    Harness h;
    const NodeId n = MountBox(h, ".s { background-color: #ff0000; transform: scale(0.5); }", std::string("s"));

    const Rect solved = h.arena.Resolve(n).solved;
    EXPECT_FLOAT_EQ(solved.width, 100.f);
    EXPECT_FLOAT_EQ(solved.height, 40.f);

    RecordingCanvas rc;
    Paint(h.ctx, h.arena.Resolve(n).parent, rc);
    const CanvasCall* layer = nullptr;
    for (const CanvasCall& c : rc.calls)
        if (c.op == CanvasCall::Op::PushLayer) { layer = &c; break; }
    ASSERT_NE(layer, nullptr);
    EXPECT_FLOAT_EQ(layer->transform[0], 0.5f);
    EXPECT_FLOAT_EQ(layer->transform[4], 0.5f);
    EXPECT_FLOAT_EQ(layer->transform[6], solved.x + 25.f); // origin (center) stays fixed
    EXPECT_FLOAT_EQ(layer->transform[7], solved.y + 10.f);
    EXPECT_TRUE(rc.Balanced());
}

TEST(TransformTests, PointerHitsTheScaledBoxNotTheLayoutBox)
{
    FakeFonts fonts;
    int presses = 0;
    BoxCfg cfg{.layout = {.width = Fixed(100.f), .height = Fixed(40.f)},
               .position = {.position = Position::Absolute, .inset = {.left = 0.f, .top = 0.f}},
               .className = "s"};
    cfg.on.onPress = [&presses](PointerInfo) { ++presses; };
    MountedApp app(Stack(StackCfg{.layout = {.width = Expand, .height = Expand}, .anchor = true}, Box(std::move(cfg))),
                   &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.SetStylesheetSource(".s { transform: scale(0.5); }");
    app.Layout(400.f, 300.f);

    const auto press = [&](float x, float y)
    {
        Event ev{};
        ev.Type = EventType::MouseDown;
        ev.Button = PointerButton::Left;
        ev.Position = {x, y};
        app.Dispatch(ev);
        ev.Type = EventType::MouseUp;
        app.Dispatch(ev);
    };
    press(5.f, 5.f); // inside the layout box, outside the painted (scaled) one
    EXPECT_EQ(presses, 0);
    press(50.f, 20.f);
    EXPECT_EQ(presses, 1);
}

TEST(TransformTests, RotateMatrixAndHitInverse)
{
    Harness h;
    const NodeId n = MountBox(h, ".r { transform: rotate(90deg); } .s { transform: scale(0.5); }", std::string("r"));
    const LiveNode& node = h.arena.Resolve(n);

    const svg::Affine m = detail::NodeTransform(h.arena, node);
    const Vec2 center{node.solved.x + 50.f, node.solved.y + 20.f};
    const Vec2 c = m.Apply(center);
    EXPECT_NEAR(c.x, center.x, 1e-3f);
    EXPECT_NEAR(c.y, center.y, 1e-3f);
    // The top-left corner swings to the top-right of the rotated box (clockwise, y down).
    const Vec2 tl = m.Apply({node.solved.x, node.solved.y});
    EXPECT_NEAR(tl.x, center.x + 20.f, 1e-3f);
    EXPECT_NEAR(tl.y, center.y - 50.f, 1e-3f);

    // Hit-test maps through the inverse: a point inside the layout box but outside the scaled-down box misses.
    h.engine.LoadSource(".r { transform: scale(0.5); }");
    svg::Affine inv;
    ASSERT_TRUE(detail::NodeTransform(h.arena, node).Invert(inv));
    EXPECT_FALSE(node.solved.Contains(inv.Apply({node.solved.x + 5.f, node.solved.y + 5.f})));
    EXPECT_TRUE(node.solved.Contains(inv.Apply(center)));
}
