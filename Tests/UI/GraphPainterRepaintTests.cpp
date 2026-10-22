#include <gtest/gtest.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Layout/LayoutBridge.h"

#include <cstdint>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace
{
    struct Harness
    {
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        GraphScope scope{*graph};
        NodeArena arena;
        LayoutTree layout;
        AppCtx ctx{arena, *graph, &layout};

        NodeId Mount(const Elem& tree, Owner& owner)
        {
            return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
        }
    };
} // namespace

TEST(GraphPainterRepaintTests, BumpingTheRepaintTokenMarksThePainterDirty)
{
    Harness h;
    Owner owner;
    Signal<std::uint64_t> revision{0};

    const NodeId root = h.Mount(Painter({.onPaint = [](Paint2D&) {}, .repaint = revision}), owner);

    h.arena.Resolve(root).dirty = Dirty::None;
    EXPECT_FALSE(HasFlag(h.arena.Resolve(root).dirty, Dirty::Repaint));

    revision = 1;
    EXPECT_TRUE(HasFlag(h.arena.Resolve(root).dirty, Dirty::Repaint));
}

TEST(GraphPainterRepaintTests, AStaticRepaintTokenBindsNoEffect)
{
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Painter({.onPaint = [](Paint2D&) {}}), owner);
    h.arena.Resolve(root).dirty = Dirty::None;
    EXPECT_FALSE(HasFlag(h.arena.Resolve(root).dirty, Dirty::Repaint));
}
