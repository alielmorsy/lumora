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

TEST(StyleKeyframesTests, OpacityKeyframeMidAndForwards)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@keyframes fade { from { opacity: 1; } to { opacity: 0; } } "
                        ".a { animation: fade 1s linear forwards; }");
    const NodeId root = h.Mount(Box({.className = "a"}), owner);
    h.Frames(30);
    const float mid = h.Style(root).opacity;
    EXPECT_GT(mid, 0.35f);
    EXPECT_LT(mid, 0.65f);
    h.Frames(45);
    EXPECT_FLOAT_EQ(h.Style(root).opacity, 0.f);
}

TEST(StyleKeyframesTests, ColorKeyframeForwards)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@keyframes glow { from { background-color: #000000; } to { background-color: #ffffff; } } "
                        ".g { animation: glow 1s linear forwards; }");
    const NodeId root = h.Mount(Box({.className = "g"}), owner);
    h.Frames(30);
    const Color mid = h.Style(root).fill;
    EXPECT_GT(mid.r, 100);
    EXPECT_LT(mid.r, 160);
    h.Frames(45);
    EXPECT_EQ(h.Style(root).fill, Color(255, 255, 255));
}

TEST(StyleKeyframesTests, PercentKeyframeStops)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@keyframes r { 0% { border-radius: 0; } 50% { border-radius: 20px; } 100% { border-radius: 0; } } "
                        ".r { animation: r 1s linear forwards; }");
    const NodeId root = h.Mount(Box({.className = "r"}), owner);
    h.Frames(30);
    EXPECT_GT(h.Style(root).radius, 16.f);
    EXPECT_LE(h.Style(root).radius, 20.f);
    h.Frames(45);
    EXPECT_FLOAT_EQ(h.Style(root).radius, 0.f);
}

TEST(StyleKeyframesTests, InfiniteKeepsAnimating)
{
    SetReducedMotion(false);
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@keyframes pulse { from { opacity: 1; } to { opacity: 0.4; } } "
                        ".p { animation: pulse 1s linear infinite alternate; }");
    const NodeId root = h.Mount(Box({.className = "p"}), owner);
    h.Frames(90);
    const float v = h.Style(root).opacity;
    EXPECT_GE(v, 0.4f);
    EXPECT_LE(v, 1.0f);
    h.Frames(60);
    const float v2 = h.Style(root).opacity;
    EXPECT_GE(v2, 0.4f);
    EXPECT_LE(v2, 1.0f);
}

TEST(StyleKeyframesTests, ReducedMotionHoldsFinal)
{
    SetReducedMotion(true);
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@keyframes fade { from { opacity: 1; } to { opacity: 0; } } "
                        ".a { animation: fade 1s linear forwards; }");
    const NodeId root = h.Mount(Box({.className = "a"}), owner);
    h.Frames(2);
    EXPECT_LE(h.Style(root).opacity, 0.5f);
    SetReducedMotion(false);
}
