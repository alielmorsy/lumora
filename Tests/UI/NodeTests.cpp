#include <gtest/gtest.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/Widget.h"

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

constexpr LiveKind TestBoxKind  = static_cast<LiveKind>(static_cast<std::uint16_t>(LiveKind::CustomBase));
constexpr LiveKind TestLeafKind = static_cast<LiveKind>(static_cast<std::uint16_t>(LiveKind::CustomBase) + 1);

struct TestBoxCfg {};
struct TestLeafCfg {
    Reactive<int> value;
};
struct TestLeafPayload {
    int value = 0;
};

class TestBoxWidget final : public Widget {
    [[nodiscard]] const char* Name() const noexcept override { return "TestBox"; }
    void Instantiate(AppCtx&, detail::Node&, NodeId, Owner&) override {}
};

class TestLeafWidget final : public TypedWidget<TestLeafCfg> {
    [[nodiscard]] const char* Name() const noexcept override { return "TestLeaf"; }
    void Build(AppCtx& ctx, TestLeafCfg& config, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<TestLeafPayload>(TestLeafKind).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{TestLeafKind, slot};
        BindProp(ctx.arena, id, config.value,
                 [](NodeArena& arena, NodeId nid, const int& v) {
                     arena.PayloadOf<TestLeafPayload>(nid).value = v;
                 },
                 Dirty::Repaint);
    }
};

void RegisterTestKinds() {
    KindRegistry::Register(TestBoxKind, MakeRef<TestBoxWidget>());
    KindRegistry::Register(TestLeafKind, MakeRef<TestLeafWidget>());
}

Elem TestBox(std::initializer_list<Elem> children) {
    auto node = MakeRef<detail::TypedNode<TestBoxCfg>>(static_cast<detail::KindId>(TestBoxKind), TestBoxCfg{});
    for (const Elem& child : children) {
        node->AddChild(child.Share());
    }
    return Elem(node);
}

Elem TestLeaf(Reactive<int> value) {
    return Elem(MakeRef<detail::TypedNode<TestLeafCfg>>(static_cast<detail::KindId>(TestLeafKind),
                                                        TestLeafCfg{std::move(value)}));
}

struct Probe {
    Signal<int> value;
    bool*       unmounted = nullptr;
    int*        mounts = nullptr;

    Elem Render() {
        Signal<int> shared = value;
        return TestLeaf(Reactive<int>([shared] { return shared(); }));
    }

    void OnMount() {
        if (mounts) {
            ++*mounts;
        }
    }
    void OnUnmount() {
        if (unmounted) {
            *unmounted = true;
        }
    }
};

class NodeTests : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        RegisterTestKinds();
    }
};

} // namespace

TEST_F(NodeTests, MountStaticTree) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};
    Owner owner;

    Elem tree = TestBox({
        TestLeaf(1),
        TestBox({TestLeaf(2), TestLeaf(3)}),
    });
    const NodeId root = Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);

    EXPECT_EQ(arena.LiveCount(), 5u);

    LiveNode& rootNode = arena.Resolve(root);
    EXPECT_EQ(rootNode.kind, TestBoxKind);
    EXPECT_FALSE(rootNode.parent.Valid());
    EXPECT_EQ(rootNode.childCount, 2);

    const NodeId firstId = rootNode.firstChild;
    LiveNode& first = arena.Resolve(firstId);
    EXPECT_EQ(first.kind, TestLeafKind);
    EXPECT_EQ(first.parent, root);

    const NodeId secondId = first.nextSibling;
    LiveNode& second = arena.Resolve(secondId);
    EXPECT_EQ(second.kind, TestBoxKind);
    EXPECT_EQ(second.prevSibling, firstId);
    EXPECT_EQ(second.childCount, 2);
    EXPECT_EQ(arena.Resolve(rootNode.lastChild).id, secondId);
    EXPECT_EQ(arena.Resolve(second.firstChild).parent, secondId);
}

TEST_F(NodeTests, ReactivePropPatchesWithoutReinstantiation) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};
    Owner owner;

    auto value = Signal(10);
    Elem leaf = TestLeaf(value);
    const NodeId root = Instantiate(ctx, *leaf.RawNode(), NodeId{}, owner);

    EXPECT_EQ(arena.LiveCount(), 1u);
    EXPECT_EQ(arena.PayloadOf<TestLeafPayload>(root).value, 10);
    EXPECT_TRUE(arena.Resolve(root).IsDirty(Dirty::Repaint));

    arena.Resolve(root).ClearDirty(Dirty::All);
    value = 25;

    EXPECT_EQ(arena.PayloadOf<TestLeafPayload>(root).value, 25);
    EXPECT_TRUE(arena.Resolve(root).IsDirty(Dirty::Repaint));
    EXPECT_EQ(arena.LiveCount(), 1u);
}

TEST_F(NodeTests, SubtreeDisposalRecyclesAndRunsCleanupsLifo) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};

    std::vector<int> order;
    {
        Owner owner;
        OnCleanup([&] { order.push_back(1); });
        OnCleanup([&] { order.push_back(2); });

        auto value = Signal(0);
        Elem tree = TestBox({TestLeaf(value), TestLeaf(value)});
        Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
        EXPECT_EQ(arena.LiveCount(), 3u);
    }

    EXPECT_EQ(arena.LiveCount(), 0u);
    EXPECT_EQ(order, (std::vector<int>{2, 1}));
}

TEST_F(NodeTests, ClassComponentChildPatchesAndUnmounts) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};

    Signal<int> shared(7);
    bool        unmounted = false;
    int         mounts = 0;

    {
        Owner owner;
        Elem tree = TestBox({Probe{.value = shared, .unmounted = &unmounted, .mounts = &mounts}});
        const NodeId root = Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);

        EXPECT_EQ(arena.LiveCount(), 3u);
        EXPECT_EQ(mounts, 1);

        const NodeId componentId = arena.Resolve(root).firstChild;
        EXPECT_EQ(arena.Resolve(componentId).kind, LiveKind::Component);

        const NodeId leafId = arena.Resolve(componentId).firstChild;
        EXPECT_EQ(arena.Resolve(leafId).kind, TestLeafKind);
        EXPECT_EQ(arena.PayloadOf<TestLeafPayload>(leafId).value, 7);

        shared = 99;
        EXPECT_EQ(arena.PayloadOf<TestLeafPayload>(leafId).value, 99);
        EXPECT_FALSE(unmounted);
    }

    EXPECT_TRUE(unmounted);
    EXPECT_EQ(arena.LiveCount(), 0u);
}
