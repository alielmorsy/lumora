#include <gtest/gtest.h>

#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/NullCanvas.h>

#include "Layout/LayoutBridge.h"
#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "RecordingCanvas.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

struct FakeFonts final : TextMeasurer {
    [[nodiscard]] Vec2 Measure(std::string_view text, const FontProps&, float size,
                               const TextFlowProps&, AvailableSpace) const override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }

    void BuildGlyphCache(std::string_view text, const FontProps&, float size, const TextFlowProps&,
                         AvailableSpace, std::vector<CachedGlyph>& coverage,
                         std::vector<CachedGlyph>& color) const override {
        coverage.clear();
        color.clear();
        const float w = size * 0.5f;
        float x = 0.f;
        for (std::size_t i = 0; i < text.size(); ++i) {
            coverage.push_back(CachedGlyph{Rect{x, 0.f, w, size}, 0, 0, 1, 1});
            x += w;
        }
    }

    [[nodiscard]] AtlasView Atlas(AtlasKind kind) const override {
        AtlasView v;
        v.width = 1;
        v.height = 1;
        v.version = kind == AtlasKind::Coverage ? 7 : 0;
        return v;
    }

    [[nodiscard]] Vec2 AtlasSize(AtlasKind) const override { return {1.f, 1.f}; }
};

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope scope{*graph};
    NodeArena arena;
    LayoutTree layout;
    AppCtx ctx{arena, *graph, &layout};

    explicit Harness(const TextMeasurer* fonts = nullptr) { ctx.measurer = fonts; }

    NodeId Mount(const Elem& tree, Owner& owner) { return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner); }
    void Solve(NodeId root, float w, float h) { Layout(ctx, root, w, h); }

    [[nodiscard]] NodeId Child(NodeId id) { return arena.Resolve(id).firstChild; }
};

[[nodiscard]] const CanvasCall* NthOp(const RecordingCanvas& rc, CanvasCall::Op op, int n) {
    for (const CanvasCall& c : rc.calls) {
        if (c.op == op && n-- == 0) {
            return &c;
        }
    }
    return nullptr;
}

TEST(PaintTests, EmitRecordsBackgroundThenChildrenInOrder) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Column({.style = {.fill = Color::DarkGray}},
                                       Box({.style = {.fill = Color::Red}}),
                                       Box({.style = {.fill = Color::Green}})),
                                owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    EXPECT_EQ(rc.CountOp(CanvasCall::Op::Rect), 3);
    ASSERT_EQ(rc.calls.size(), 3u);
    EXPECT_EQ(rc.calls[0].rect.fill, Color::DarkGray);
    EXPECT_EQ(rc.calls[1].rect.fill, Color::Red);
    EXPECT_EQ(rc.calls[2].rect.fill, Color::Green);
}

TEST(PaintTests, TransparentFillAndNoBorderEmitsNothing) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Box({.style = {.fill = Color::Transparent}}), owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);
    EXPECT_TRUE(rc.calls.empty());
}

TEST(PaintTests, ClipWrapsChildrenWithBalancedPushPop) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Box({.style = {.fill = Color::Red}, .view = {.clip = true}},
                                    Box({.style = {.fill = Color::Green}})),
                                owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    ASSERT_EQ(rc.calls.size(), 4u);
    EXPECT_EQ(rc.calls[0].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[1].op, CanvasCall::Op::PushClip);
    EXPECT_EQ(rc.calls[2].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[3].op, CanvasCall::Op::PopClip);
    EXPECT_TRUE(rc.Balanced());
}

TEST(PaintTests, ScrollEmitsClipAndTranslateLayer) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Scroll({.style = {.fill = Color::Blue}},
                                       Box({.layout = {.height = Fixed(300.f)}, .style = {.fill = Color::Red}})),
                                owner);
    h.arena.PayloadOf<ScrollPayload>(root).offsetY = 30.f;
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    ASSERT_EQ(rc.calls.size(), 7u);
    EXPECT_EQ(rc.calls[0].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[1].op, CanvasCall::Op::PushClip);
    EXPECT_EQ(rc.calls[2].op, CanvasCall::Op::PushLayer);
    EXPECT_EQ(rc.calls[2].transform[6], 0.f);
    EXPECT_EQ(rc.calls[2].transform[7], -30.f);
    EXPECT_EQ(rc.calls[3].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[4].op, CanvasCall::Op::PopLayer);
    EXPECT_EQ(rc.calls[5].op, CanvasCall::Op::PopClip);
    EXPECT_EQ(rc.calls[6].op, CanvasCall::Op::Rect);
    EXPECT_TRUE(rc.Balanced());
}

TEST(PaintTests, HiddenSubtreePaintsNothing) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(
        Column({.style = {.fill = Color::Transparent}},
               Box({.style = {.fill = Color::Red}, .view = {.visible = false}},
                   Box({.style = {.fill = Color::Green}}))),
        owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);
    EXPECT_EQ(rc.CountOp(CanvasCall::Op::Rect), 0);
}

TEST(PaintTests, TextEmitsGlyphRunWhenFontsInjected) {
    FakeFonts fonts;
    Harness h(&fonts);
    Owner owner;

    const NodeId root = h.Mount(Column({.style = {.fill = Color::Transparent}}, Text("Hi")), owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    EXPECT_EQ(rc.CountOp(CanvasCall::Op::Glyphs), 1);
    const CanvasCall* glyphs = NthOp(rc, CanvasCall::Op::Glyphs, 0);
    ASSERT_NE(glyphs, nullptr);
    EXPECT_EQ(glyphs->glyphCount, 2);
    EXPECT_EQ(glyphs->atlas, AtlasKind::Coverage);
}

TEST(PaintTests, TextEmitsNothingWithoutFonts) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Column({.style = {.fill = Color::Transparent}}, Text("Hi")), owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);
    EXPECT_EQ(rc.CountOp(CanvasCall::Op::Glyphs), 0);
}

TEST(PaintTests, CoverageGlyphsSnapToWholePixels) {
    FakeFonts fonts;
    Harness h(&fonts);
    Owner owner;

    const NodeId root = h.Mount(Button({.label = "Hi", .layout = {.width = Fixed(101.f)}}), owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    const CanvasCall* glyphs = NthOp(rc, CanvasCall::Op::Glyphs, 0);
    ASSERT_NE(glyphs, nullptr);
    EXPECT_EQ(glyphs->atlas, AtlasKind::Coverage);
    EXPECT_EQ(glyphs->glyphDst.x, std::floor(glyphs->glyphDst.x));
    EXPECT_EQ(glyphs->glyphDst.y, std::floor(glyphs->glyphDst.y));
}

TEST(PaintTests, NestedClipAndScrollStaysBalanced) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(
        Box({.style = {.fill = Color::Red}, .view = {.clip = true}},
            Scroll({}, Box({.style = {.fill = Color::Green}}))),
        owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    EXPECT_EQ(rc.CountOp(CanvasCall::Op::Glyphs), 0);
    EXPECT_TRUE(rc.Balanced());
}

TEST(PaintTests, ATranslatedSubtreeCullsAgainstTheShiftedWindow) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(
        Box({.layout = {.width = Fixed(100.f), .height = Fixed(100.f)}, .view = {.clip = true}},
            Column({.layout = {.width = Fixed(100.f), .height = Fixed(120.f)},
                    .transform = {.y = -85.f}},
                   Box({.layout = {.width = Fixed(100.f), .height = Fixed(40.f)},
                        .style = {.fill = Color::Red}}),
                   Box({.layout = {.width = Fixed(100.f), .height = Fixed(40.f)},
                        .style = {.fill = Color::Blue}}),
                   Box({.layout = {.width = Fixed(100.f), .height = Fixed(40.f)},
                        .style = {.fill = Color::Green}}))),
        owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    bool red = false, blue = false, green = false;
    for (const CanvasCall& c : rc.calls) {
        if (c.op != CanvasCall::Op::Rect) {
            continue;
        }
        red = red || c.rect.fill == Color::Red;
        blue = blue || c.rect.fill == Color::Blue;
        green = green || c.rect.fill == Color::Green;
    }
    EXPECT_TRUE(green);
    EXPECT_FALSE(red);
    EXPECT_FALSE(blue);
}

TEST(PaintTests, NullCanvasAbsorbsAFrame) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Column({.style = {.fill = Color::DarkGray}},
                                       Box({.style = {.fill = Color::Red}})),
                                owner);
    h.Solve(root, 200.f, 200.f);

    NullCanvas canvas;
    Paint(h.ctx, root, canvas);
    EXPECT_TRUE(true);
}

TEST(PaintTests, RePaintIsDeterministic) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Column({.style = {.fill = Color::DarkGray}},
                                       Box({.style = {.fill = Color::Red}}),
                                       Box({.style = {.fill = Color::Green}})),
                                owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);
    const std::size_t first = rc.calls.size();
    rc.Clear();
    Paint(h.ctx, root, rc);
    const std::size_t second = rc.calls.size();
    EXPECT_EQ(first, second);
    EXPECT_EQ(second, 3u);
}

TEST(PaintTests, PaintOffsetTranslatesSubtreeWithoutDirtyingLayout) {
    Harness h;
    Owner owner;

    Signal<Vec2> offset = Signal(Vec2{10.f, 20.f});
    const NodeId root = h.Mount(Box({.style = {.fill = Color::Red},
                                     .paintOffset = offset},
                                    Box({.style = {.fill = Color::Green}})),
                                owner);
    h.Solve(root, 200.f, 200.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    const CanvasCall* layer = NthOp(rc, CanvasCall::Op::PushLayer, 0);
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->transform[6], 10.f);
    EXPECT_EQ(layer->transform[7], 20.f);

    h.arena.Resolve(root).ClearDirty(Dirty::Layout);
    offset = Vec2{30.f, 40.f};

    const LiveNode& node = h.arena.Resolve(root);
    EXPECT_TRUE(node.IsDirty(Dirty::Repaint));
    EXPECT_FALSE(node.IsDirty(Dirty::Layout));

    rc.Clear();
    Paint(h.ctx, root, rc);

    const CanvasCall* layer2 = NthOp(rc, CanvasCall::Op::PushLayer, 0);
    ASSERT_NE(layer2, nullptr);
    EXPECT_EQ(layer2->transform[6], 30.f);
    EXPECT_EQ(layer2->transform[7], 40.f);
}

TEST(PaintTests, ContentScaleAppliesTransformLayer) {
    Harness h;
    Owner owner;

    Signal<float> scale = Signal(2.f);
    const NodeId root = h.Mount(Box({.style = {.fill = Color::Blue},
                                     .contentScale = scale},
                                    Box({.style = {.fill = Color::Green}})),
                                owner);
    h.Solve(root, 100.f, 100.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    const CanvasCall* layer = NthOp(rc, CanvasCall::Op::PushLayer, 0);
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->transform[0], 2.f);
    EXPECT_EQ(layer->transform[4], 2.f);
}

TEST(PaintTests, DirectionalBorderDrawsIndividualSides) {
    Harness h;
    Owner owner;

    Border b;
    b.bottom = 2.f;
    b.bottomColor = Color::Red;
    b.hasBottomColor = true;

    const NodeId root = h.Mount(Box({.style = {.fill = Color::Blue, .border = b}}), owner);
    h.Solve(root, 100.f, 100.f);

    RecordingCanvas rc;
    Paint(h.ctx, root, rc);

    ASSERT_EQ(rc.calls.size(), 2u);
    EXPECT_EQ(rc.calls[0].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[0].rect.fill, Color::Blue);
    EXPECT_EQ(rc.calls[1].op, CanvasCall::Op::Rect);
    EXPECT_EQ(rc.calls[1].rect.fill, Color::Red);
    EXPECT_EQ(rc.calls[1].rect.bounds, (Rect{0.f, 98.f, 100.f, 2.f}));
}

} // namespace
