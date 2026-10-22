#include <gtest/gtest.h>

#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <lumora/log/Log.h>

#include "Layout/LayoutBridge.h"
#include "Node/ControlState.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/VirtualListRuntime.h"

#include <cmath>
#include <string>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

constexpr float kRow = 20.f;
constexpr float kViewport = 200.f;

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope scope{*graph};
    NodeArena arena;
    LayoutTree layout;
    AppCtx ctx{arena, *graph, &layout};

    NodeId Mount(const Elem& tree, Owner& owner) {
        return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
    }

    void Frame(NodeId root, float w = 300.f, float h = kViewport) {
        Layout(ctx, root, w, h);
        if (RunVirtualLists(ctx)) {
            Layout(ctx, root, w, h);
        }
    }

    [[nodiscard]] VirtualListState& State(NodeId list) { return arena.PayloadOf<VirtualListState>(list); }

    [[nodiscard]] std::uint32_t MaterializedRows(NodeId list) {
        const NodeId content = State(list).content;
        return content.Valid() ? arena.Resolve(content).childCount : 0u;
    }
};

[[nodiscard]] VirtualListCfg Rows(Signal<int>& count, int overscan = 2) {
    VirtualListCfg cfg{
        .count = count,
        .itemExtent = kRow,
        .overscan = overscan,
        .row = [](int index) {
            return Box({.layout = {.height = Fixed(kRow)}},
                       Text(std::to_string(index)));
        },
        .key = [](int index) { return static_cast<std::uint64_t>(index); },
        .layout = {.width = Expand, .height = Expand},
    };
    return cfg;
}

TEST(VirtualListTests, HugeListMaterializesOnlyItsWindow) {
    Harness h;
    Owner owner;
    auto count = Signal(100'000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(Rows(count))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;
    EXPECT_EQ(h.arena.Resolve(list).kind, LiveKind::VirtualList);

    EXPECT_LE(h.MaterializedRows(list), 16u);
    EXPECT_GE(h.MaterializedRows(list), 11u);
    EXPECT_EQ(h.State(list).first, 0);
}

TEST(VirtualListTests, ScrollingMovesTheWindowAndDisposesWhatLeftIt) {
    Harness h;
    Owner owner;
    auto count = Signal(1000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(Rows(count))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;

    h.State(list).offsetY = 100.f * kRow;
    h.Frame(root);

    EXPECT_EQ(h.State(list).first, 98);

    const VirtualListState& st = h.State(list);
    EXPECT_EQ(h.MaterializedRows(list), static_cast<std::uint32_t>(st.last - st.first + 1));
    EXPECT_LE(h.MaterializedRows(list), 16u);

    bool anyStale = false;
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index < 98) anyStale = true;
    }
    EXPECT_FALSE(anyStale);
}

TEST(VirtualListTests, ARowThatStaysInTheWindowKeepsItsNode) {
    Harness h;
    Owner owner;
    auto count = Signal(1000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(Rows(count))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;

    NodeId nodeOfRow5{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 5) nodeOfRow5 = row.root;
    }
    EXPECT_TRUE(nodeOfRow5.Valid());

    h.State(list).offsetY = 2.f * kRow;
    h.Frame(root);

    NodeId after{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 5) after = row.root;
    }
    EXPECT_EQ(after, nodeOfRow5);
}

TEST(VirtualListTests, ShrinkingTheCountClampsTheWindow) {
    Harness h;
    Owner owner;
    auto count = Signal(1000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(Rows(count))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;
    h.State(list).offsetY = 500.f * kRow;
    h.Frame(root);
    EXPECT_GT(h.State(list).first, 0);

    count = 3;
    h.graph->Tick(0.f);
    h.Frame(root);

    EXPECT_EQ(h.State(list).first, 0);
    EXPECT_EQ(h.State(list).last, 2);
    EXPECT_EQ(h.MaterializedRows(list), 3u);
    EXPECT_EQ(h.State(list).offsetY, 0.f);
}

TEST(VirtualListTests, AnEmptyListMaterializesNothing) {
    Harness h;
    Owner owner;
    auto count = Signal(0);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(Rows(count))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;
    EXPECT_EQ(h.MaterializedRows(list), 0u);
    EXPECT_EQ(h.State(list).last, -1);
}

TEST(VirtualListTests, ABumpedVersionRebuildsTheLiveRows) {
    Harness h;
    Owner owner;
    auto count = Signal(50);
    auto version = Signal<std::uint64_t>(0);

    VirtualListCfg cfg = Rows(count);
    cfg.version = version;
    cfg.key = {};

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(std::move(cfg))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;
    NodeId first{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 0) first = row.root;
    }
    EXPECT_TRUE(first.Valid());

    version = 1;
    h.graph->Tick(0.f);
    h.Frame(root);

    NodeId rebuilt{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 0) rebuilt = row.root;
    }
    EXPECT_TRUE(rebuilt.Valid());
    EXPECT_NE(rebuilt, first);
}

TEST(VirtualListTests, AKeyedRowWithSameKeySurvivesVersionBump) {
    Harness h;
    Owner owner;
    auto count = Signal(50);
    auto version = Signal<std::uint64_t>(0);

    VirtualListCfg cfg = Rows(count);
    cfg.version = version;
    cfg.key = [](int index) { return static_cast<std::uint64_t>(index == 0 ? 1000 : index); };

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(std::move(cfg))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;
    NodeId first{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 0) first = row.root;
    }
    EXPECT_TRUE(first.Valid());

    version = 1;
    h.graph->Tick(0.f);
    h.Frame(root);

    NodeId after{};
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == 0) after = row.root;
    }
    EXPECT_TRUE(after.Valid());
    EXPECT_EQ(after, first);
}

int g_warns = 0;

void CountWarns(lumora::log::Level level, const char*, const char*) noexcept {
    if (level == lumora::log::Level::Warn) ++g_warns;
}

TEST(VirtualListTests, AZeroItemExtentWarnsAndMaterializesNothing) {
    g_warns = 0;
    lumora::log::SetSink(&CountWarns);
    {
        Harness h;
        Owner owner;
        auto count = Signal(100);

        VirtualListCfg cfg = Rows(count);
        cfg.itemExtent = 0.f;

        const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                           VirtualList(std::move(cfg))),
                                    owner);
        h.Frame(root);

        const NodeId list = h.arena.Resolve(root).firstChild;
        EXPECT_EQ(h.MaterializedRows(list), 0u);
    }
    lumora::log::SetSink(nullptr);
    EXPECT_GE(g_warns, 1);
}

TEST(VirtualListTests, PaddingBelongsToTheScrollableReach) {
    Harness h;
    Owner owner;
    auto count = Signal(50);

    VirtualListCfg cfg = Rows(count);
    cfg.layout.padding = Spacing{6.f};

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(std::move(cfg))),
                                owner);
    h.Frame(root);

    const NodeId list = h.arena.Resolve(root).firstChild;

    h.State(list).offsetY = 1e6f;
    h.Frame(root);

    EXPECT_NEAR(h.State(list).offsetY, 50.f * kRow + 12.f - kViewport, 0.01f);
    EXPECT_EQ(h.State(list).last, 49);

    const VirtualListState& st = h.State(list);
    const NodeId lastRow = st.rows.empty() ? NodeId{} : st.rows.back().root;
    ASSERT_TRUE(lastRow.Valid());
    const float shift = h.arena.PayloadOf<ContainerPayload>(st.content).transform.y;
    const float bottom = h.arena.Resolve(lastRow).solved.Bottom() + shift;
    EXPECT_NEAR(bottom, h.arena.Resolve(list).solved.Bottom() - 6.f, 0.01f);
}

constexpr float kSurfaceWidth = 300.f;
constexpr float kNarrow = 50.f;

[[nodiscard]] VirtualListCfg WideRows(Signal<int>& count, ScrollAxis axis, int wideIndex, float wideWidth) {
    return VirtualListCfg{
        .count = count,
        .itemExtent = kRow,
        .overscan = 2,
        .axis = axis,
        .row = [wideIndex, wideWidth](int index) {
            const float width = index == wideIndex ? wideWidth : kNarrow;
            return Row({.layout = {.height = Fixed(kRow)}},
                       Box({.layout = {.width = Fixed(width), .height = Fixed(kRow)}}));
        },
        .key = [](int index) { return static_cast<std::uint64_t>(index); },
        .layout = {.width = Expand, .height = Expand},
    };
}

[[nodiscard]] float RowWidth(Harness& h, NodeId list, int index) {
    for (const VirtualRow& row : h.State(list).rows) {
        if (row.index == index) return h.arena.Resolve(row.root).solved.width;
    }
    return -1.f;
}

[[nodiscard]] float ContentShiftX(Harness& h, NodeId list) {
    return h.arena.PayloadOf<ContainerPayload>(h.State(list).content).transform.x;
}

TEST(VirtualListTests, BothScrollsAcrossTheWidestRow) {
    Harness h;
    Owner owner;
    auto count = Signal(1000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(WideRows(count, ScrollAxis::Both, 3, 700.f))),
                                owner);
    h.Frame(root);
    const NodeId list = h.arena.Resolve(root).firstChild;

    EXPECT_NEAR(RowWidth(h, list, 3), 700.f, 0.01f);
    EXPECT_NEAR(RowWidth(h, list, 0), 700.f, 0.01f);

    h.State(list).offsetX = 1e6f;
    h.Frame(root);
    EXPECT_NEAR(h.State(list).offsetX, 700.f - kSurfaceWidth, 0.01f);
    EXPECT_NEAR(ContentShiftX(h, list), -(700.f - kSurfaceWidth), 0.01f);

    h.State(list).offsetY = 100.f * kRow;
    h.Frame(root);
    h.Frame(root);
    EXPECT_EQ(h.State(list).first, 98);
    EXPECT_LE(h.MaterializedRows(list), 16u);
    EXPECT_NEAR(h.State(list).offsetX, 700.f - kSurfaceWidth, 0.01f);
    EXPECT_NEAR(ContentShiftX(h, list), -(700.f - kSurfaceWidth), 0.01f);
}

TEST(VirtualListTests, BothRowsFillANarrowerViewport) {
    Harness h;
    Owner owner;
    auto count = Signal(100);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(WideRows(count, ScrollAxis::Both, -1, 0.f))),
                                owner);
    h.Frame(root);
    const NodeId list = h.arena.Resolve(root).firstChild;

    EXPECT_NEAR(RowWidth(h, list, 0), kSurfaceWidth, 0.01f);

    h.State(list).offsetX = 1e6f;
    h.Frame(root);
    EXPECT_EQ(h.State(list).offsetX, 0.f);
    EXPECT_EQ(ContentShiftX(h, list), 0.f);
}

TEST(VirtualListTests, BothForgetsTheReachWhenTheRowsShrink) {
    Harness h;
    Owner owner;
    auto count = Signal(1000);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(WideRows(count, ScrollAxis::Both, 3, 700.f))),
                                owner);
    h.Frame(root);
    const NodeId list = h.arena.Resolve(root).firstChild;

    h.State(list).offsetX = 1e6f;
    h.Frame(root);
    EXPECT_GT(h.State(list).offsetX, 0.f);

    count = 3;
    h.graph->Tick(0.f);
    h.Frame(root);
    EXPECT_EQ(h.State(list).offsetX, 0.f);
    EXPECT_EQ(ContentShiftX(h, list), 0.f);

    h.State(list).offsetX = 1e6f;
    h.Frame(root);
    EXPECT_EQ(h.State(list).offsetX, 0.f);
}

TEST(VirtualListTests, VerticalNeverScrollsSideways) {
    Harness h;
    Owner owner;
    auto count = Signal(100);

    const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                       VirtualList(WideRows(count, ScrollAxis::Vertical, 3, 700.f))),
                                owner);
    h.Frame(root);
    const NodeId list = h.arena.Resolve(root).firstChild;

    EXPECT_NEAR(RowWidth(h, list, 3), kSurfaceWidth, 0.01f);

    h.State(list).offsetX = 1e6f;
    h.Frame(root);
    EXPECT_EQ(h.State(list).offsetX, 0.f);
    EXPECT_EQ(ContentShiftX(h, list), 0.f);
}

TEST(VirtualListTests, UnmountingDisposesEveryRow) {
    Harness h;
    auto count = Signal(100);

    std::uint32_t liveAfterMount = 0;
    {
        Owner owner;
        const NodeId root = h.Mount(Column({.layout = {.width = Expand, .height = Expand}},
                                           VirtualList(Rows(count))),
                                    owner);
        h.Frame(root);
        liveAfterMount = h.arena.LiveCount();
        EXPECT_GT(liveAfterMount, 10u);
    }
    EXPECT_EQ(h.arena.LiveCount(), 0u);
}

} // namespace
