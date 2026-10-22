#include <gtest/gtest.h>

#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <lumora/log/Log.h>

#include "Layout/LayoutBridge.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ControlRuntime.h"
#include "Runtime/Instantiate.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope scope{*graph};
    NodeArena arena;
    LayoutTree layout;
    AppCtx ctx{arena, *graph, &layout};

    NodeId Mount(const Elem& tree, Owner& owner) { return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner); }
    void Solve(NodeId root, float w, float h) { Layout(ctx, root, w, h); }

    [[nodiscard]] LiveKind Kind(NodeId id) { return arena.Resolve(id).kind; }
    [[nodiscard]] std::uint32_t Children(NodeId id) { return arena.Resolve(id).childCount; }
    [[nodiscard]] NodeId Child(NodeId id) { return arena.Resolve(id).firstChild; }
    [[nodiscard]] NodeId Next(NodeId id) { return arena.Resolve(id).nextSibling; }
};

TEST(ControlFlowTests, WhenContributesOrNothing) {
    Harness h;
    Owner on;
    Owner off;

    const NodeId withPanel = h.Mount(Column({}, When(true, Box({}))), on);
    const NodeId without = h.Mount(Column({}, When(false, Box({}))), off);

    EXPECT_EQ(h.Children(withPanel), 1);
    EXPECT_EQ(h.Children(without), 0);
}

TEST(ControlFlowTests, ShowMountsDisposesAndRunsCleanupsLifo) {
    Harness h;
    std::vector<int> order;
    auto open = Signal(false);

    {
        Owner owner;
        Elem tree = Column({}, Show(open, [&] {
            OnCleanup([&] { order.push_back(1); });
            OnCleanup([&] { order.push_back(2); });
            return Box({});
        }));
        const NodeId root = h.Mount(tree, owner);
        const NodeId show = h.Child(root);

        EXPECT_EQ(h.Kind(show), LiveKind::Show);
        EXPECT_EQ(h.Children(show), 0);
        EXPECT_EQ(h.arena.LiveCount(), 2);

        open = true;
        EXPECT_EQ(h.Children(show), 1);
        EXPECT_EQ(h.Kind(h.Child(show)), LiveKind::Box);
        EXPECT_EQ(h.arena.LiveCount(), 3);
        EXPECT_TRUE(order.empty());

        open = false;
        EXPECT_EQ(h.Children(show), 0);
        EXPECT_EQ(h.arena.LiveCount(), 2);
        EXPECT_EQ(order, (std::vector<int>{2, 1}));
    }
}

TEST(ControlFlowTests, ShowSwapsBetweenBodyAndFallback) {
    Harness h;
    Owner owner;
    auto open = Signal(true);

    const NodeId root = h.Mount(
        Column({}, Show(open, [] { return Box({}); }, [] { return Text("fallback"); })), owner);
    const NodeId show = h.Child(root);

    EXPECT_EQ(h.Children(show), 1);
    EXPECT_EQ(h.Kind(h.Child(show)), LiveKind::Box);

    open = false;
    EXPECT_EQ(h.Children(show), 1);
    EXPECT_EQ(h.Kind(h.Child(show)), LiveKind::Text);

    open = true;
    EXPECT_EQ(h.Kind(h.Child(show)), LiveKind::Box);
}

struct CountingComponent {
    int* renders = nullptr;

    Elem Render() {
        ++*renders;
        return Box({});
    }
};

TEST(ControlFlowTests, ShowReplaysCapturedClassComponent) {
    Harness h;
    Owner owner;
    int renders = 0;
    auto open = Signal(true);

    Elem captured = CountingComponent{&renders};
    const NodeId root = h.Mount(Column({}, Show(open, [captured] { return Column({}, captured); })), owner);
    const NodeId show = h.Child(root);

    EXPECT_EQ(renders, 1);
    EXPECT_EQ(h.Children(show), 1);

    open = false;
    EXPECT_EQ(h.Children(show), 0);

    open = true;
    EXPECT_EQ(renders, 2);
    EXPECT_EQ(h.Children(show), 1);

    open = false;
    open = true;
    EXPECT_EQ(renders, 3);
}

struct MoveOnlyComponent {
    int* renders = nullptr;
    std::unique_ptr<int> token = std::make_unique<int>(0);

    Elem Render() {
        ++*renders;
        return Box({});
    }
};

TEST(ControlFlowTests, ShowRefusesSecondMountOfMoveOnlyComponent) {
    Harness h;
    Owner owner;
    int renders = 0;
    auto open = Signal(true);

    Elem captured = MoveOnlyComponent{&renders};
    const NodeId root = h.Mount(Column({}, Show(open, [captured] { return Column({}, captured); })), owner);
    const NodeId show = h.Child(root);

    EXPECT_EQ(renders, 1);
    EXPECT_EQ(h.Children(h.Child(show)), 1);

    open = false;
    open = true;
    EXPECT_EQ(renders, 1);
    EXPECT_EQ(h.Children(h.Child(show)), 1);
}

TEST(ControlFlowTests, ShowReplaysCapturedInnerShow) {
    Harness h;
    Owner owner;
    auto outer = Signal(true);
    auto inner = Signal(false);

    Elem captured = Show(inner, [] { return Box({}); });
    const NodeId root = h.Mount(Column({}, Show(outer, [captured] { return Column({}, captured); })), owner);
    const NodeId outerShow = h.Child(root);

    outer = false;
    outer = true;

    const NodeId innerShow = h.Child(h.Child(outerShow));
    EXPECT_EQ(h.Kind(innerShow), LiveKind::Show);
    EXPECT_EQ(h.Children(innerShow), 0);

    inner = true;
    EXPECT_EQ(h.Children(innerShow), 1);
    EXPECT_EQ(h.Kind(h.Child(innerShow)), LiveKind::Box);
}

struct Task {
    int id;
};

TEST(ControlFlowTests, EachKeyedReorderPreservesStateAndReorders) {
    Harness h;
    Owner owner;

    int builds = 0;
    std::map<int, Signal<int>> rowSig;

    auto tasks = Signal(std::vector<Task>{{1}, {2}, {3}});

    auto rowBuilder = [&](const Task& t) -> Elem {
        ++builds;
        Signal<int> local(t.id * 100);
        rowSig.insert_or_assign(t.id, local);
        return Text(std::to_string(t.id));
    };

    const NodeId root = h.Mount(Column({}, Each(tasks, &Task::id, rowBuilder)), owner);
    const NodeId each = h.Child(root);

    EXPECT_EQ(h.Kind(each), LiveKind::Each);
    EXPECT_EQ(builds, 3);
    EXPECT_EQ(h.Children(each), 3);

    rowSig.at(2) = 999;
    tasks.Update([](auto& v) { std::swap(v[0], v[2]); });

    EXPECT_EQ(builds, 3);
    EXPECT_EQ(rowSig.at(2).Peek(), 999);

    std::vector<std::string> seen;
    for (NodeId c = h.Child(each); c.Valid(); c = h.Next(c)) {
        seen.push_back(h.arena.PayloadOf<TextPayload>(c).text);
    }
    EXPECT_EQ(seen, (std::vector<std::string>{"3", "2", "1"}));

    h.Solve(root, 200.f, 200.f);
}

TEST(ControlFlowTests, EachInsertsAndDisposesByKey) {
    Harness h;
    Owner owner;

    auto tasks = Signal(std::vector<Task>{{1}, {2}});
    const NodeId root = h.Mount(
        Column({}, Each(tasks, &Task::id, [](const Task& t) { return Text(std::to_string(t.id)); })), owner);
    const NodeId each = h.Child(root);

    EXPECT_EQ(h.Children(each), 2);
    const std::uint32_t base = h.arena.LiveCount();

    tasks.Update([](auto& v) { v.push_back(Task{3}); });
    EXPECT_EQ(h.Children(each), 3);
    EXPECT_EQ(h.arena.LiveCount(), base + 1);

    tasks.Update([](auto& v) { v.erase(v.begin()); });
    EXPECT_EQ(h.Children(each), 2);
    EXPECT_EQ(h.arena.LiveCount(), base);
}

TEST(ControlFlowTests, NestedShowInsideEachDisposesOnRowRemoval) {
    Harness h;
    Owner owner;

    bool innerCleaned = false;
    auto tasks = Signal(std::vector<Task>{{1}, {2}});

    const NodeId root = h.Mount(
        Column({}, Each(tasks, &Task::id, [&](const Task& t) -> Elem {
            return Column({}, Show(Signal(true), [&] {
                OnCleanup([&] { innerCleaned = true; });
                return Box({});
            }));
        })),
        owner);
    const NodeId each = h.Child(root);
    EXPECT_EQ(h.Children(each), 2);

    const std::uint32_t before = h.arena.LiveCount();
    tasks.Update([](auto& v) { v.erase(v.begin()); });

    EXPECT_EQ(h.Children(each), 1);
    EXPECT_TRUE(innerCleaned);
    EXPECT_LT(h.arena.LiveCount(), before);
}

template <typename Component, typename... Args>
detail::MemoConfig MemoCfg(Component component, Args... args) {
    auto values = std::make_tuple(std::move(args)...);
    detail::MemoConfig cfg;
    cfg.args = MakeRef<detail::MemoArgs<Args...>>(values);
    cfg.build = [component = std::move(component), values = std::move(values)]() -> Elem {
        return std::apply([&component](const Args&... a) -> Elem { return component(a...); }, values);
    };
    return cfg;
}

TEST(ControlFlowTests, MemoRebuildsOnlyWhenArgsChange) {
    Harness h;
    Owner owner;

    int builds = 0;
    auto comp = [&](int a, bool b) -> Elem {
        ++builds;
        (void)a;
        (void)b;
        return Box({});
    };

    const NodeId root = h.Mount(Column({}, Memo(comp, 5, true)), owner);
    const NodeId memo = h.Child(root);

    EXPECT_EQ(h.Kind(memo), LiveKind::Memo);
    EXPECT_EQ(builds, 1);
    EXPECT_EQ(h.Children(memo), 1);

    EXPECT_FALSE(MemoReconcile(h.ctx, memo, MemoCfg(comp, 5, true)));
    EXPECT_EQ(builds, 1);

    EXPECT_TRUE(MemoReconcile(h.ctx, memo, MemoCfg(comp, 6, true)));
    EXPECT_EQ(builds, 2);
    EXPECT_EQ(h.Children(memo), 1);
}

TEST(ControlFlowTests, DeferredEditAppliesOnNextTick) {
    Harness h;
    Owner owner;
    auto open = Signal(false);

    const NodeId root = h.Mount(Column({}, Show(open, [] { return Box({}); })), owner);
    const NodeId show = h.Child(root);
    EXPECT_EQ(h.Children(show), 0);

    {
        DeferStructuralScope guard(*h.graph);
        open = true;
        EXPECT_EQ(h.Children(show), 0);
    }
    EXPECT_EQ(h.Children(show), 0);

    h.graph->Tick(0.f);
    EXPECT_EQ(h.Children(show), 1);
}

TEST(ControlFlowTests, ImmediateEditDoesNotOvertakeQueuedOne) {
    Harness h;
    Owner owner;
    auto open = Signal(true);

    const NodeId root = h.Mount(Column({}, Show(open, [] { return Box({}); })), owner);
    const NodeId show = h.Child(root);
    EXPECT_EQ(h.Children(show), 1);

    {
        DeferStructuralScope guard(*h.graph);
        open = false;
    }
    open = true;

    h.graph->Tick(0.f);
    EXPECT_EQ(h.Children(show), 1);
}

int g_warns = 0;

void CountWarns(lumora::log::Level level, const char*, const char*) noexcept {
    if (level == lumora::log::Level::Warn) {
        ++g_warns;
    }
}

TEST(ControlFlowTests, BareSignalReadInBuilderWarns) {
    g_warns = 0;
    lumora::log::SetSink(&CountWarns);

    {
        Harness h;
        Owner owner;
        auto other = Signal(0);
        h.Mount(Column({}, Show(Signal(true), [other] {
                    (void)other();
                    return Box({});
                })),
                owner);
    }

    lumora::log::SetSink(nullptr);
#ifndef NDEBUG
    EXPECT_GE(g_warns, 1);
#else
    EXPECT_EQ(g_warns, 0);
#endif
}

} // namespace
