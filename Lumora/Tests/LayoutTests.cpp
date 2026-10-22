#include <gtest/gtest.h>

#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Text/Glyphs.h>
#include <lumora/Text/TextMeasurer.h>
#include <lumora/Widgets/Text.h>
#include <lumora/Widgets/Widgets.h>

#include "Layout/AspectRatio.h"
#include "Layout/LayoutBridge.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

class FakeMeasurer final : public TextMeasurer {
public:
    static constexpr float kCharW = 10.f;
    static constexpr float kLineH = 20.f;

    [[nodiscard]] Vec2 Measure(std::string_view text, const FontProps&, float, const TextFlowProps& flow,
                               AvailableSpace width) const override {
        if (text.empty()) {
            return {0.f, kLineH};
        }
        if (!flow.wrap || !width.IsDefinite()) {
            return {static_cast<float>(text.size()) * kCharW, kLineH};
        }
        const float maxW    = width.value;
        float       lineW    = 0.f;
        float       longest  = 0.f;
        int         lines    = 1;
        std::size_t i        = 0;
        while (i < text.size()) {
            std::size_t j = i;
            while (j < text.size() && text[j] != ' ') {
                ++j;
            }
            const float wordW     = static_cast<float>(j - i) * kCharW;
            const float withLead  = (lineW > 0.f ? kCharW : 0.f) + wordW;
            if (lineW > 0.f && lineW + withLead > maxW) {
                longest = std::max(longest, lineW);
                ++lines;
                lineW = wordW;
            } else {
                lineW += withLead;
            }
            i = j;
            while (i < text.size() && text[i] == ' ') {
                ++i;
            }
        }
        longest = std::max(longest, lineW);
        return {longest, static_cast<float>(lines) * kLineH};
    }

    void BuildGlyphCache(std::string_view, const FontProps&, float, const TextFlowProps&, AvailableSpace,
                         std::vector<CachedGlyph>& coverage, std::vector<CachedGlyph>& color) const override {
        coverage.clear();
        color.clear();
    }

    [[nodiscard]] AtlasView Atlas(AtlasKind) const override { return {}; }
};

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope         scope{*graph};
    NodeArena          arena;
    LayoutTree         layout;
    FakeMeasurer       measurer;
    AppCtx             ctx{arena, *graph, &layout, &measurer};

    NodeId Mount(const Elem& tree, Owner& owner) {
        return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
    }
    void                Solve(NodeId root, float w, float h) { Layout(ctx, root, w, h); }
    [[nodiscard]] const Rect& Solved(NodeId id) { return arena.Resolve(id).solved; }
    [[nodiscard]] std::uint32_t SolveCount(NodeId id) { return layout.SolveCount(arena.Resolve(id).layout); }
    [[nodiscard]] NodeId Child(NodeId id) { return arena.Resolve(id).firstChild; }
    [[nodiscard]] NodeId Next(NodeId id) { return arena.Resolve(id).nextSibling; }
};

TEST(LayoutTests, ColumnStacksChildrenWithGap) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({.layout = {.gap = 10.f}},
               Box({.layout = {.width = Fixed(100), .height = Fixed(20)}}),
               Box({.layout = {.width = Fixed(100), .height = Fixed(30)}})),
        owner);
    h.Solve(root, 200.f, 200.f);

    const NodeId c0 = h.Child(root);
    const NodeId c1 = h.Next(c0);

    const Rect& r0 = h.Solved(c0);
    const Rect& r1 = h.Solved(c1);
    EXPECT_NEAR(r0.x, 0.f, 0.05f);
    EXPECT_NEAR(r0.y, 0.f, 0.05f);
    EXPECT_NEAR(r0.width, 100.f, 0.05f);
    EXPECT_NEAR(r0.height, 20.f, 0.05f);
    EXPECT_NEAR(r1.x, 0.f, 0.05f);
    EXPECT_NEAR(r1.y, 30.f, 0.05f);
    EXPECT_NEAR(r1.width, 100.f, 0.05f);
    EXPECT_NEAR(r1.height, 30.f, 0.05f);

    EXPECT_NEAR(h.Solved(root).width, 200.f, 0.05f);
    EXPECT_NEAR(h.Solved(root).height, 200.f, 0.05f);
}

TEST(LayoutTests, RowExpandSplitsByWeight) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Row({.layout = {.width = Fixed(300), .height = Fixed(50)}},
                   Expanded(1.f), Expanded(2.f))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId row = h.Child(root);
    const NodeId e0  = h.Child(row);
    const NodeId e1  = h.Next(e0);

    EXPECT_NEAR(h.Solved(e0).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(e0).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(e1).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(e1).width, 200.f, 0.05f);
}

TEST(LayoutTests, PercentResolvesAgainstParent) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Box({.layout = {.width = Fixed(200), .height = Fixed(100)}},
                   Box({.layout = {.width = Percent(50), .height = Fixed(20)}}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId outer = h.Child(root);
    const NodeId inner = h.Child(outer);
    EXPECT_NEAR(h.Solved(outer).width, 200.f, 0.05f);
    EXPECT_NEAR(h.Solved(inner).width, 100.f, 0.05f);
}

TEST(LayoutTests, AbsoluteOffsetPositionsChild) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Box({.layout = {.width = Fixed(300), .height = Fixed(300)}, .anchor = true},
            Box({.layout = {.width = Fixed(50), .height = Fixed(50)},
                 .position = {.position = Position::Absolute, .offset = {20.f, 30.f}}})),
        owner);
    h.Solve(root, 300.f, 300.f);

    const Rect& r = h.Solved(h.Child(root));
    EXPECT_NEAR(r.x, 20.f, 0.05f);
    EXPECT_NEAR(r.y, 30.f, 0.05f);
    EXPECT_NEAR(r.width, 50.f, 0.05f);
    EXPECT_NEAR(r.height, 50.f, 0.05f);
}

TEST(LayoutTests, IncrementalReSolvesOnlyDirtySubtree) {
    Harness h;
    Owner   owner;

    auto gapA = Signal<float>(0.f);
    const NodeId root = h.Mount(
        Column({},
               Row({.layout = {.height = Fixed(100), .gap = gapA}},
                    Box({.layout = {.width = Fixed(20), .height = Fixed(20)}}),
                    Box({.layout = {.width = Fixed(20), .height = Fixed(20)}})),
               Row({.layout = {.height = Fixed(100), .gap = 10.f}},
                    Box({.layout = {.width = Fixed(20), .height = Fixed(20)}}),
                    Box({.layout = {.width = Fixed(20), .height = Fixed(20)}}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId rowA = h.Child(root);
    const NodeId rowB = h.Next(rowA);
    const std::uint32_t a0 = h.SolveCount(rowA);
    const std::uint32_t b0 = h.SolveCount(rowB);

    gapA = 25.f;
    h.Solve(root, 400.f, 400.f);

    EXPECT_GT(h.SolveCount(rowA), a0);
    EXPECT_EQ(h.SolveCount(rowB), b0);

    const NodeId a1 = h.Next(h.Child(rowA));
    EXPECT_NEAR(h.Solved(a1).x, 45.f, 0.05f);
}

TEST(LayoutTests, GrowChildPositionsAfterFixedSibling) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({.layout = {.width = Grow(1), .height = Grow(1), .cross = Align::Stretch}},
               Box({.layout = {.width = Grow(1), .height = Fixed(80)}}),
               Box({.layout = {.width = Grow(1), .height = Grow(1)}})),
        owner);
    h.Solve(root, 1000.f, 600.f);

    const NodeId header = h.Child(root);
    const NodeId host   = h.Next(header);

    EXPECT_NEAR(h.Solved(header).y, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(header).height, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(host).y, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(host).height, 520.f, 0.05f);
}

TEST(LayoutTests, NestedGrowMainAreaPositions) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Row({.layout = {.width = Grow(1), .height = Grow(1), .cross = Align::Stretch}},
            Box({.layout = {.width = Fixed(236), .height = Grow(1)}}),
            Column({.layout = {.width = Grow(1), .cross = Align::Stretch}},
                   Box({.layout = {.width = Grow(1), .height = Fixed(80)}}),
                   Box({.layout = {.width = Grow(1), .height = Grow(1)}},
                       Show(Signal(true), [] {
                           return Scroll({.axis = ScrollAxis::Vertical,
                                          .layout = {.width = Grow(1), .height = Grow(1)}},
                                         Box({.layout = {.width = Fixed(50), .height = Fixed(50)}}));
                       })))),
        owner);
    h.Solve(root, 1000.f, 600.f);

    const NodeId sidebar  = h.Child(root);
    const NodeId mainArea = h.Next(sidebar);
    const NodeId header   = h.Child(mainArea);
    const NodeId host     = h.Next(header);
    const NodeId show     = h.Child(host);
    const NodeId scroll   = h.Child(show);

    EXPECT_NEAR(h.Solved(mainArea).x, 236.f, 0.05f);
    EXPECT_NEAR(h.Solved(mainArea).height, 600.f, 0.05f);
    EXPECT_NEAR(h.Solved(host).x, 236.f, 0.05f);
    EXPECT_NEAR(h.Solved(host).y, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).x, 236.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).y, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).width, 764.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).height, 520.f, 0.05f);
}

TEST(LayoutTests, ShowMountedScrollPositionsBelowHeader) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({.layout = {.width = Grow(1), .height = Grow(1), .cross = Align::Stretch}},
               Box({.layout = {.width = Grow(1), .height = Fixed(80)}}),
               Box({.layout = {.width = Grow(1), .height = Grow(1)}},
                   Show(Signal(true), [] {
                       return Scroll({.axis = ScrollAxis::Vertical,
                                      .layout = {.width = Grow(1), .height = Grow(1)}},
                                     Box({.layout = {.width = Fixed(50), .height = Fixed(50)}}));
                   }))),
        owner);
    h.Solve(root, 1000.f, 600.f);

    const NodeId header = h.Child(root);
    const NodeId host   = h.Next(header);
    const NodeId show   = h.Child(host);
    const NodeId scroll = h.Child(show);

    EXPECT_EQ(h.arena.Resolve(show).kind, LiveKind::Show);
    EXPECT_EQ(h.arena.Resolve(scroll).kind, LiveKind::Scroll);

    EXPECT_NEAR(h.Solved(host).y, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(host).height, 520.f, 0.05f);

    EXPECT_NEAR(h.Solved(scroll).y, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).height, 520.f, 0.05f);
    EXPECT_NEAR(h.Solved(scroll).width, 1000.f, 0.05f);
}

TEST(LayoutTests, AbsoluteStaysInAnchorContentBox) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Row({.layout = {.width = Grow(1), .height = Grow(1), .cross = Align::Stretch}},
            Box({.layout = {.width = Fixed(236), .height = Grow(1)}}),
            Box({.layout = {.width = Grow(1), .height = Grow(1)}, .anchor = true},
                Box({.layout = {.width = Fixed(100), .height = Fixed(50)},
                     .position = {.position = Position::Absolute, .offset = {0.f, 10.f}}}))),
        owner);
    h.Solve(root, 1000.f, 600.f);

    const NodeId hostBox = h.Next(h.Child(root));
    const NodeId offset  = h.Child(hostBox);

    EXPECT_NEAR(h.Solved(offset).x, 236.f, 0.05f);
    EXPECT_NEAR(h.Solved(offset).y, 10.f, 0.05f);
    EXPECT_NEAR(h.Solved(offset).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(offset).height, 50.f, 0.05f);
}

TEST(LayoutTests, TransformDoesNotMoveSolvedRect) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({.layout = {.width = Fixed(200), .height = Fixed(200)}},
               Box({.layout = {.width = Fixed(40), .height = Fixed(40)},
                    .transform = {.x = 25.f, .y = 15.f}})),
        owner);
    h.Solve(root, 200.f, 200.f);

    const Rect& r = h.Solved(h.Child(root));
    EXPECT_NEAR(r.x, 0.f, 0.05f);
    EXPECT_NEAR(r.y, 0.f, 0.05f);
    EXPECT_NEAR(r.width, 40.f, 0.05f);
    EXPECT_NEAR(r.height, 40.f, 0.05f);
}

TEST(LayoutTests, FixedPinsToViewport) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({.layout = {.width = Grow(1), .height = Grow(1)}},
               Box({.layout = {.width = Grow(1), .height = Grow(1), .padding = 20.f}},
                   Box({.layout = {.width = Fixed(80), .height = Fixed(60)},
                        .position = {.position = Position::Fixed, .offset = {12.f, 12.f}}}))),
        owner);
    h.Solve(root, 1000.f, 600.f);

    const NodeId inner = h.Child(root);
    const NodeId fixed = h.Child(inner);
    EXPECT_NEAR(h.Solved(fixed).x, 12.f, 0.05f);
    EXPECT_NEAR(h.Solved(fixed).y, 12.f, 0.05f);
    EXPECT_NEAR(h.Solved(fixed).width, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(fixed).height, 60.f, 0.05f);
}

TEST(LayoutTests, IdleFrameSolvesNothing) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({}, Box({.layout = {.width = Fixed(40), .height = Fixed(40)}})), owner);
    h.Solve(root, 200.f, 200.f);

    const NodeId child = h.Child(root);
    const std::uint32_t before = h.SolveCount(child);
    h.Solve(root, 200.f, 200.f);
    EXPECT_EQ(h.SolveCount(child), before);
}

TEST(LayoutTests, WrapTextInDefiniteParentWraps) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Column({.layout = {.width = Fixed(100)}},
                      Text("aaaa bbbb cccc"))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId inner = h.Child(root);
    const NodeId text  = h.Child(inner);

    EXPECT_TRUE(h.arena.Resolve(text).wrapInBox);
    EXPECT_NEAR(h.Solved(text).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(text).height, 40.f, 0.05f);
    EXPECT_NEAR(h.Solved(inner).height, 40.f, 0.05f);
}

TEST(LayoutTests, WrapHeadingInShrinkToFitParentStaysOneLine) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Row({.layout = {.width = Grow(1), .main = Align::Between, .cross = Align::Center}},
            Column({.layout = {.gap = 4.f}},
                   Text("aaaa bbbb cccc dddd")),
            Box({.layout = {.width = Fixed(120), .height = Fixed(40)}})),
        owner);
    h.Solve(root, 1000.f, 400.f);

    const NodeId column = h.Child(root);
    const NodeId text   = h.Child(column);

    EXPECT_FALSE(h.arena.Resolve(text).wrapInBox);
    EXPECT_NEAR(h.Solved(text).height, 20.f, 0.05f);
}

TEST(LayoutTests, OverfullColumnOverflowsInsteadOfShrinking) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Column({.layout = {.height = Fixed(30), .cross = Align::Stretch}},
                      Text("Title"),
                      Box({.layout = {.height = Fixed(40)}}))),
        owner);
    h.Solve(root, 200.f, 200.f);

    const NodeId inner = h.Child(root);
    const NodeId title = h.Child(inner);
    const NodeId box   = h.Next(title);

    EXPECT_NEAR(h.Solved(title).height, 20.f, 0.05f);
    EXPECT_NEAR(h.Solved(box).height, 40.f, 0.05f);
    EXPECT_NEAR(h.Solved(box).y, h.Solved(title).y + 20.f, 0.05f);
}

TEST(LayoutTests, WrapContainerHeightIncludesGapsAndMargins) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Column({.layout = {.cross = Align::Stretch, .gap = 10.f}},
                      Box({.layout = {.height = Fixed(20)}}),
                      Box({.layout = {.height = Fixed(30), .margin = Spacing::Only({.bottom = 5})}}),
                      Box({.layout = {.height = Fixed(40)}}))),
        owner);
    h.Solve(root, 200.f, 200.f);

    const NodeId col   = h.Child(root);
    const NodeId last  = h.Next(h.Next(h.Child(col)));
    EXPECT_NEAR(h.Solved(col).height, 115.f, 0.05f);
    EXPECT_NEAR(h.Solved(last).y + h.Solved(last).height, 115.f, 0.05f);
}

TEST(LayoutTests, GridUniformColumnsAutoFlow) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = 3, .autoRows = Px(50.f), .layout = {.width = Fixed(300)}},
                    Box({}), Box({}), Box({}), Box({}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid = h.Child(root);
    const NodeId c0   = h.Child(grid);
    const NodeId c1   = h.Next(c0);
    const NodeId c2   = h.Next(c1);
    const NodeId c3   = h.Next(c2);

    EXPECT_NEAR(h.Solved(c0).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(c0).y, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(c0).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(c0).height, 50.f, 0.05f);
    EXPECT_NEAR(h.Solved(c1).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(c2).x, 200.f, 0.05f);
    EXPECT_NEAR(h.Solved(c3).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(c3).y, 50.f, 0.05f);
    EXPECT_NEAR(h.Solved(grid).width, 300.f, 0.05f);
    EXPECT_NEAR(h.Solved(grid).height, 100.f, 0.05f);
}

TEST(LayoutTests, GridMixedPxFrTracks) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = {Px(100.f), 1_fr, 2_fr}, .autoRows = Px(40.f),
                     .layout = {.width = Fixed(300)}},
                    Box({}), Box({}), Box({}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid = h.Child(root);
    const NodeId c0   = h.Child(grid);
    const NodeId c1   = h.Next(c0);
    const NodeId c2   = h.Next(c1);

    EXPECT_NEAR(h.Solved(c0).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(c1).width, 66.6667f, 0.05f);
    EXPECT_NEAR(h.Solved(c2).width, 133.3333f, 0.05f);
    EXPECT_NEAR(h.Solved(c1).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(c2).x, 166.6667f, 0.05f);
}

TEST(LayoutTests, GridGapsOffsetTracks) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = 2, .colGap = 20.f, .rowGap = 10.f, .autoRows = Px(50.f),
                     .layout = {.width = Fixed(220)}},
                    Box({}), Box({}), Box({}), Box({}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid = h.Child(root);
    const NodeId c0   = h.Child(grid);
    const NodeId c1   = h.Next(c0);
    const NodeId c2   = h.Next(c1);

    EXPECT_NEAR(h.Solved(c0).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(c1).x, 120.f, 0.05f);
    EXPECT_NEAR(h.Solved(c2).y, 60.f, 0.05f);
}

TEST(LayoutTests, GridExplicitPlacementAndSpan) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = {1_fr, 1_fr}, .rows = {Px(40.f), Px(40.f)},
                     .layout = {.width = Fixed(200), .height = Fixed(80)}},
                    Cell({.col = 0, .row = 0, .colSpan = 2}, Box({})),
                    Cell({.col = 0, .row = 1}, Box({})),
                    Cell({.col = 1, .row = 1}, Box({})))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid   = h.Child(root);
    const NodeId header = h.Child(grid);
    const NodeId left   = h.Next(header);
    const NodeId right  = h.Next(left);

    EXPECT_NEAR(h.Solved(header).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(header).y, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(header).width, 200.f, 0.05f);
    EXPECT_NEAR(h.Solved(header).height, 40.f, 0.05f);
    EXPECT_NEAR(h.Solved(left).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(left).y, 40.f, 0.05f);
    EXPECT_NEAR(h.Solved(left).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(right).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(right).y, 40.f, 0.05f);
}

TEST(LayoutTests, GridNestedResolvesInOneSweep) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = 2, .autoRows = Px(100.f), .layout = {.width = Fixed(200)}},
                    Box({}),
                    Grid({.columns = 2, .rows = {1_fr, 1_fr}},
                         Box({}), Box({}), Box({}), Box({})))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId outer  = h.Child(root);
    const NodeId nested = h.Next(h.Child(outer));
    const NodeId nA     = h.Child(nested);
    const NodeId nC     = h.Next(h.Next(nA));

    EXPECT_NEAR(h.Solved(nested).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(nested).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(nested).height, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(nA).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(nA).y, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(nA).width, 50.f, 0.05f);
    EXPECT_NEAR(h.Solved(nA).height, 50.f, 0.05f);
    EXPECT_NEAR(h.Solved(nC).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(nC).y, 50.f, 0.05f);
}

TEST(LayoutTests, GridFrUnderWrapClamps) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = {Px(80.f), 1_fr}, .autoRows = Px(30.f), .layout = {.width = Wrap}},
                    Box({}), Box({}))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid = h.Child(root);
    const NodeId c0   = h.Child(grid);
    const NodeId c1   = h.Next(c0);

    EXPECT_NEAR(h.Solved(c0).width, 80.f, 0.05f);
    EXPECT_NEAR(h.Solved(c1).width, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(grid).width, 80.f, 0.05f);
}

TEST(LayoutTests, GridHoistsTransparentChildren) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(
        Column({},
               Grid({.columns = 2, .autoRows = Px(40.f), .layout = {.width = Fixed(200)}},
                    Box({}),
                    Show(Signal(true), [] { return Box({}); }))),
        owner);
    h.Solve(root, 400.f, 400.f);

    const NodeId grid = h.Child(root);
    const NodeId box0 = h.Child(grid);
    const NodeId show = h.Next(box0);
    const NodeId box1 = h.Child(show);

    EXPECT_EQ(h.arena.Resolve(show).kind, LiveKind::Show);
    EXPECT_NEAR(h.Solved(box0).x, 0.f, 0.05f);
    EXPECT_NEAR(h.Solved(box0).width, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(box1).x, 100.f, 0.05f);
    EXPECT_NEAR(h.Solved(box1).width, 100.f, 0.05f);
}

TEST(LayoutTests, AspectRatioPinsTheAutoAxis) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(Row({}, Box({.layout = {.width = Expand}})), owner);
    const NodeId tile = h.Child(root);
    h.Solve(root, 300.f, 300.f);
    EXPECT_NEAR(h.Solved(tile).width, 300.f, 0.05f);

    h.arena.Resolve(tile).style.aspectRatio = 2.f;
    h.arena.Resolve(tile).style.aspectPin   = AspectPin::Height;
    h.arena.TrackAspect(tile);
    EXPECT_TRUE(RunAspectRatios(h.ctx));
    h.Solve(root, 300.f, 300.f);
    EXPECT_NEAR(h.Solved(tile).height, 150.f, 0.05f);

    EXPECT_FALSE(RunAspectRatios(h.ctx));

    h.Solve(root, 200.f, 200.f);
    EXPECT_TRUE(RunAspectRatios(h.ctx));
    h.Solve(root, 200.f, 200.f);
    EXPECT_NEAR(h.Solved(tile).height, 100.f, 0.05f);
}

TEST(LayoutTests, AspectRatioIgnoresBothAxesStated) {
    Harness h;
    Owner   owner;

    const NodeId root = h.Mount(Row({.layout = {.width = Fixed(300.f), .height = Fixed(300.f)}},
                                    Box({.layout = {.width = Fixed(120.f), .height = Fixed(80.f)}})),
                                owner);
    const NodeId tile = h.Child(root);
    h.Solve(root, 400.f, 400.f);

    h.arena.Resolve(tile).style.aspectRatio = 1.f;
    h.arena.Resolve(tile).style.aspectPin   = AspectPin::None;
    h.arena.TrackAspect(tile);
    EXPECT_FALSE(RunAspectRatios(h.ctx));
    h.Solve(root, 400.f, 400.f);
    EXPECT_NEAR(h.Solved(tile).height, 80.f, 0.05f);
}

TEST(LayoutTests, AspectRatioDropsStaleRegistrations) {
    Harness h;

    NodeId tile;
    {
        Owner owner;
        const NodeId root = h.Mount(Row({.layout = {.width = Fixed(300.f), .height = Fixed(300.f)}},
                                        Box({.layout = {.width = Expand}})),
                                    owner);
        tile = h.Child(root);
        h.Solve(root, 400.f, 400.f);
        h.arena.Resolve(tile).style.aspectRatio = 1.f;
        h.arena.Resolve(tile).style.aspectPin   = AspectPin::Height;
        h.arena.TrackAspect(tile);
        EXPECT_EQ(h.arena.AspectNodes().size(), 1);
    }
    EXPECT_FALSE(RunAspectRatios(h.ctx));
    EXPECT_TRUE(h.arena.AspectNodes().empty());
}

} // namespace
