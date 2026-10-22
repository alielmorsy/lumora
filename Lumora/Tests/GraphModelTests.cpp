#include <gtest/gtest.h>
#include <lumora/Graph/Model.h>

#include <vector>

using namespace lumora::nodegraph;
using lumora::Vec2;

namespace
{

    struct Fixture
    {
        Registry   reg;
        Model      model;
        LinkPolicy policy{};
        NodeTypeId type;

        Fixture()
        {
            NodeTypeDesc d;
            d.title = "Op";
            d.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output, .multi = true},
                      PinDesc{.name = "Vec", .type = PinTypeId{"float4"}, .dir = PinDir::Input}};
            type = reg.Register("op", std::move(d));
        }

        [[nodiscard]] GraphNodeId Spawn(float x = 0.f, float y = 0.f) { return model.Add(type, Vec2{x, y}); }
        [[nodiscard]] static PinRef In(GraphNodeId n, std::uint16_t slot) { return PinRef{n, slot, PinDir::Input}; }
        [[nodiscard]] static PinRef Out(GraphNodeId n) { return PinRef{n, 2, PinDir::Output}; }
    };
} // namespace

TEST(GraphModelTests, AddedNodesGetLiveHandlesAndTheirTypeDefaults)
{
    Fixture f;
    const GraphNodeId a = f.Spawn(10.f, 20.f);

    EXPECT_TRUE(a.Valid());
    EXPECT_TRUE(f.model.Alive(a));
    EXPECT_EQ(f.model.NodeCount(), 1u);

    const Node* n = f.model.Get(a);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->type, f.type);
    EXPECT_EQ(n->position, (Vec2{10.f, 20.f}));
    EXPECT_FLOAT_EQ(n->width, 0.f);
    EXPECT_FALSE(n->collapsed);
    EXPECT_EQ(n->openGroups, ~0u);
    EXPECT_EQ(n->userData, 0u);
}

TEST(GraphModelTests, RemovingANodeInvalidatesItsHandle)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    EXPECT_TRUE(f.model.Remove(a));
    EXPECT_FALSE(f.model.Alive(a));
    EXPECT_EQ(f.model.Get(a), nullptr);
    EXPECT_EQ(f.model.NodeCount(), 0u);
    EXPECT_FALSE(f.model.Remove(a));
}

TEST(GraphModelTests, AReusedSlotDoesNotResolveTheOldHandle)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    f.model.Remove(a);
    const GraphNodeId b = f.Spawn();

    EXPECT_EQ(b.index, a.index);
    EXPECT_NE(b.gen, a.gen);
    EXPECT_TRUE(f.model.Alive(b));
    EXPECT_FALSE(f.model.Alive(a));
    EXPECT_EQ(f.model.Get(a), nullptr);
}

TEST(GraphModelTests, ConnectRequiresOppositeDirectionsAndDistinctNodes)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();

    EXPECT_FALSE(f.model.Connect(Fixture::Out(a), Fixture::Out(b)).Valid());
    EXPECT_FALSE(f.model.Connect(Fixture::In(a, 0), Fixture::In(b, 0)).Valid());
    EXPECT_FALSE(f.model.Connect(Fixture::Out(a), Fixture::In(a, 0)).Valid());
    EXPECT_EQ(f.model.LinkCount(), 0u);

    const LinkId ok = f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    EXPECT_TRUE(ok.Valid());
    EXPECT_EQ(f.model.LinkCount(), 1u);
    EXPECT_EQ(f.model.Get(ok)->from, Fixture::Out(a));
    EXPECT_EQ(f.model.Get(ok)->to, Fixture::In(b, 0));
}

TEST(GraphModelTests, ConnectRejectsDeadEndpointsAndDuplicates)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    const GraphNodeId dead = f.Spawn();
    f.model.Remove(dead);

    EXPECT_FALSE(f.model.Connect(Fixture::Out(dead), Fixture::In(b, 0)).Valid());
    EXPECT_FALSE(f.model.Connect(Fixture::Out(a), Fixture::In(dead, 0)).Valid());

    EXPECT_TRUE(f.model.Connect(Fixture::Out(a), Fixture::In(b, 0)).Valid());
    EXPECT_FALSE(f.model.Connect(Fixture::Out(a), Fixture::In(b, 0)).Valid());
    EXPECT_EQ(f.model.LinkCount(), 1u);
}

TEST(GraphModelTests, RemovingANodeDropsItsIncidentLinks)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    const GraphNodeId c = f.Spawn();

    const LinkId ab = f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    const LinkId bc = f.model.Connect(Fixture::Out(b), Fixture::In(c, 0));
    EXPECT_EQ(f.model.LinkCount(), 2u);

    f.model.Remove(b);
    EXPECT_EQ(f.model.LinkCount(), 0u);
    EXPECT_FALSE(f.model.Alive(ab));
    EXPECT_FALSE(f.model.Alive(bc));
    EXPECT_TRUE(f.model.Alive(a));
    EXPECT_TRUE(f.model.Alive(c));
}

TEST(GraphModelTests, LinksOfListsBothDirections)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    const GraphNodeId c = f.Spawn();
    f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.model.Connect(Fixture::Out(b), Fixture::In(c, 0));

    std::vector<LinkId> out;
    f.model.LinksOf(b, out);
    EXPECT_EQ(out.size(), 2u);

    f.model.LinksOf(a, out);
    EXPECT_EQ(out.size(), 1u);
}

TEST(GraphModelTests, LinkOnInputFindsTheOccupant)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();

    EXPECT_FALSE(f.model.LinkOnInput(Fixture::In(b, 0)).Valid());
    const LinkId l = f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    EXPECT_EQ(f.model.LinkOnInput(Fixture::In(b, 0)), l);
    EXPECT_FALSE(f.model.LinkOnInput(Fixture::In(b, 1)).Valid());
}

TEST(GraphModelTests, CanConnectRejectsMismatchedPinTypes)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();

    EXPECT_TRUE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(a), Fixture::In(b, 0)));
    EXPECT_FALSE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(a), Fixture::In(b, 3)));
}

TEST(GraphModelTests, CanConnectRejectsAnOutOfRangeSlot)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    EXPECT_FALSE(f.model.CanConnect(f.reg, f.policy, PinRef{a, 99, PinDir::Output}, Fixture::In(b, 0)));
}

TEST(GraphModelTests, CanConnectRejectsAPinWhoseDeclaredDirectionDisagrees)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    EXPECT_FALSE(f.model.CanConnect(f.reg, f.policy, PinRef{a, 0, PinDir::Output}, Fixture::In(b, 0)));
}

TEST(GraphModelTests, APolicyPredicateOverridesTypeEquality)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();

    f.policy.canConnect = [](const Model&, PinRef, PinRef) { return true; };
    EXPECT_TRUE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(a), Fixture::In(b, 3)));

    f.policy.canConnect = [](const Model&, PinRef, PinRef) { return false; };
    EXPECT_FALSE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(a), Fixture::In(b, 0)));
}

TEST(GraphModelTests, AnAcyclicPolicyRefusesAnEdgeThatWouldCloseALoop)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    const GraphNodeId c = f.Spawn();

    f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.model.Connect(Fixture::Out(b), Fixture::In(c, 0));

    EXPECT_TRUE(f.model.Reaches(a, c));
    EXPECT_FALSE(f.model.Reaches(c, a));

    EXPECT_FALSE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(c), Fixture::In(a, 0)));

    f.policy.acyclic = false;
    EXPECT_TRUE(f.model.CanConnect(f.reg, f.policy, Fixture::Out(c), Fixture::In(a, 0)));
}

TEST(GraphModelTests, ReachesHandlesACycleWithoutSpinning)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    f.model.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.model.Connect(Fixture::Out(b), Fixture::In(a, 0));

    EXPECT_TRUE(f.model.Reaches(a, b));
    EXPECT_TRUE(f.model.Reaches(b, a));
}

TEST(GraphModelTests, SlotIterationSkipsFreedNodes)
{
    Fixture f;
    const GraphNodeId a = f.Spawn();
    const GraphNodeId b = f.Spawn();
    const GraphNodeId c = f.Spawn();
    f.model.Remove(b);

    int live = 0;
    for (std::uint32_t i = 0; i < f.model.SlotCount(); ++i)
    {
        if (f.model.At(i).Valid())
        {
            ++live;
        }
    }
    EXPECT_EQ(live, 2);
    EXPECT_EQ(f.model.At(a.index), a);
    EXPECT_FALSE(f.model.At(b.index).Valid());
    EXPECT_EQ(f.model.At(c.index), c);
}

TEST(GraphModelTests, AddRawPreservesEveryField)
{
    Fixture f;
    Node n;
    n.type = f.type;
    n.position = {3.f, 4.f};
    n.width = 220.f;
    n.title = "Custom";
    n.collapsed = true;
    n.openGroups = 0b1010u;
    n.groupHeight[1] = 90.f;
    n.userData = 0xDEADBEEFull;

    const GraphNodeId id = f.model.AddRaw(n);
    const Node* got = f.model.Get(id);
    ASSERT_NE(got, nullptr);
    EXPECT_FLOAT_EQ(got->width, 220.f);
    EXPECT_EQ(got->title, "Custom");
    EXPECT_TRUE(got->collapsed);
    EXPECT_EQ(got->openGroups, 0b1010u);
    EXPECT_FLOAT_EQ(got->groupHeight[1], 90.f);
    EXPECT_EQ(got->userData, 0xDEADBEEFull);
}
