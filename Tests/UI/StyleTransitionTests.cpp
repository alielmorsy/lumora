#include <gtest/gtest.h>
#include <lumora/Animation/Animation.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Style/StyleEngine.h"
#include "Style/Transition.h"

#include <cmath>
#include <string>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace
{
    struct StyleHarness
    {
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        GraphScope scope{*graph};
        NodeArena arena;
        style::StyleEngine engine;
        AppCtx ctx{arena, *graph};
        Owner driverOwner;

        StyleHarness()
        {
            ctx.style = &engine;
            engine.AttachDriver(*graph, driverOwner, arena);
        }
        NodeId Mount(const Elem& tree, Owner& owner) { return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner); }
        [[nodiscard]] const StyleState& Style(NodeId id) { return arena.PayloadOf<ContainerPayload>(id).style; }
        void Frames(int n) { for (int i = 0; i < n; ++i) graph->Tick(1.f / 60.f); }
    };
} // namespace

TEST(StyleTransitionTests, TimingFunctionEval)
{
    style::TimingFn linear;
    linear.kind = style::TimingFn::Kind::Fn;
    linear.fn = ease::Linear;
    EXPECT_LT(std::fabs(linear.Eval(0.5f) - 0.5f), 1e-3f);

    style::TimingFn bez;
    bez.kind = style::TimingFn::Kind::CubicBezier;
    bez.bez[0] = 0.f; bez.bez[1] = 0.f; bez.bez[2] = 1.f; bez.bez[3] = 1.f;
    EXPECT_LT(std::fabs(bez.Eval(0.5f) - 0.5f), 2e-2f);

    style::TimingFn steps;
    steps.kind = style::TimingFn::Kind::Steps;
    steps.steps = 2;
    steps.stepPos = style::TimingFn::StepPos::End;
    EXPECT_FLOAT_EQ(steps.Eval(0.4f), 0.f);
    EXPECT_FLOAT_EQ(steps.Eval(0.6f), 0.5f);
    EXPECT_FLOAT_EQ(steps.Eval(1.0f), 1.f);
}

TEST(StyleTransitionTests, FillTransitionInterpolates)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #000000; transition: background-color 1s linear; } "
                        ".card.on { background-color: #ffffff; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 0));

    cls = std::string("card on");
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 0));

    h.Frames(30);
    const Color mid = h.Style(root).fill;
    EXPECT_GT(mid.r, 100);
    EXPECT_LT(mid.r, 160);
    EXPECT_FALSE(mid == Color(0, 0, 0));
    EXPECT_FALSE(mid == Color(255, 255, 255));

    h.Frames(40);
    EXPECT_EQ(h.Style(root).fill, Color(255, 255, 255));
}

// The transition is declared only by the toggled-in class, so the node had no transition state before the toggle: the run must
// still start from what was on screen, not snap.
TEST(StyleTransitionTests, TransitionAddedByToggleAnimatesFromThePreviousValue)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #000000; } "
                        ".card.on { background-color: #ffffff; transition: background-color 1s linear; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 0));

    cls = std::string("card on");
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 0));
    h.Frames(30);
    const Color mid = h.Style(root).fill;
    EXPECT_GT(mid.r, 100);
    EXPECT_LT(mid.r, 160);

    h.Frames(40);
    EXPECT_EQ(h.Style(root).fill, Color(255, 255, 255));
}

TEST(StyleTransitionTests, RetargetMidFlight)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #000000; transition: background-color 1s linear; } "
                        ".card.on { background-color: #ffffff; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    cls = std::string("card on");
    h.Frames(30);
    cls = std::string("card");
    h.Frames(70);
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 0));
}

TEST(StyleTransitionTests, ReducedMotionSnaps)
{
    SetReducedMotion(true);
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #000000; transition: background-color 1s linear; } "
                        ".card.on { background-color: #ffffff; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    cls = std::string("card on");
    h.Frames(1);
    EXPECT_EQ(h.Style(root).fill, Color(255, 255, 255));
    SetReducedMotion(false);
}

TEST(StyleTransitionTests, OpacityTransition)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("c");
    h.engine.LoadSource(".c { opacity: 1; transition: opacity 1s linear; } .c.dim { opacity: 0; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_FLOAT_EQ(h.Style(root).opacity, 1.f);
    cls = std::string("c dim");
    h.Frames(30);
    const float mid = h.Style(root).opacity;
    EXPECT_GT(mid, 0.3f);
    EXPECT_LT(mid, 0.7f);
    h.Frames(40);
    EXPECT_FLOAT_EQ(h.Style(root).opacity, 0.f);
}
