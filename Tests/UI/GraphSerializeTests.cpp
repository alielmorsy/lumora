#include <gtest/gtest.h>
#include <lumora/Graph/Graph.h>

#include <cstring>
#include <vector>

using namespace lumora::nodegraph;
using lumora::Vec2;

namespace
{
    [[nodiscard]] NodeTypeDesc OpDesc()
    {
        NodeTypeDesc d;
        d.title = "Op";
        d.minWidth = 150.f;
        d.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                  PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                  PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output,
                          .multi = true}};
        d.body = {PinRowCell{.input = 0, .output = 2}, PinRowCell{.input = 1}};
        return d;
    }
} // namespace

TEST(GraphSerializeTests, ARoundTripPreservesNodesLinksAndCamera)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId a = source.AddNode(type, Vec2{10.f, 20.f});
    const GraphNodeId b = source.AddNode(type, Vec2{300.f, 40.f});
    source.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});
    (void)source.Data().Get(a);

    source.View().SetPan(Vec2{7.f, 9.f});
    source.View().SetZoom(2.5f);

    const std::vector<std::byte> blob = source.Serialize();
    EXPECT_FALSE(blob.empty());

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));

    EXPECT_EQ(target.Data().NodeCount(), 2u);
    EXPECT_EQ(target.Data().LinkCount(), 1u);
    EXPECT_EQ(target.View().Pan(), (Vec2{7.f, 9.f}));
    EXPECT_FLOAT_EQ(target.View().Zoom(), 2.5f);
}

TEST(GraphSerializeTests, EveryNodeFieldSurvivesTheRoundTrip)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId a = source.AddNode(type, Vec2{1.f, 2.f});

    Node& node = *const_cast<Node*>(source.Data().Get(a));
    node.width = 275.f;
    node.title = "Renamed";
    node.collapsed = true;
    node.openGroups = 0b1011u;
    node.groupHeight[2] = 66.f;
    node.userData = 0x1234ABCDull;

    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));

    const GraphNodeId restored = target.Data().At(0);
    EXPECT_TRUE(restored.Valid());
    const Node* got = target.Data().Get(restored);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->position, (Vec2{1.f, 2.f}));
    EXPECT_FLOAT_EQ(got->width, 275.f);
    EXPECT_EQ(got->title, "Renamed");
    EXPECT_TRUE(got->collapsed);
    EXPECT_EQ(got->openGroups, 0b1011u);
    EXPECT_FLOAT_EQ(got->groupHeight[2], 66.f);
    EXPECT_EQ(got->userData, 0x1234ABCDull);
}

TEST(GraphSerializeTests, SelectionSurvivesTheRoundTrip)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId a = source.AddNode(type, Vec2{0.f, 0.f});
    source.AddNode(type, Vec2{300.f, 0.f});
    source.Select(std::vector<GraphNodeId>{a});

    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));
    EXPECT_EQ(target.Selection().size(), 1u);
}

TEST(GraphSerializeTests, RestoringReplacesWhateverWasThereAndClearsUndo)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    source.AddNode(type, Vec2{0.f, 0.f});
    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    target.Type("op", OpDesc());
    target.AddNode(NodeTypeId{"op"}, Vec2{500.f, 500.f});
    target.AddNode(NodeTypeId{"op"}, Vec2{600.f, 600.f});
    EXPECT_TRUE(target.CanUndo());

    EXPECT_TRUE(target.Restore(blob));
    EXPECT_EQ(target.Data().NodeCount(), 1u);
    EXPECT_FALSE(target.CanUndo());
    EXPECT_FALSE(target.CanRedo());
}

TEST(GraphSerializeTests, NodesOfUnregisteredTypesAreDroppedWithTheirLinks)
{
    Controller source;
    const NodeTypeId op = source.Type("op", OpDesc());
    const NodeTypeId ghost = source.Type("ghost", OpDesc());
    const GraphNodeId a = source.AddNode(op, Vec2{0.f, 0.f});
    const GraphNodeId g = source.AddNode(ghost, Vec2{300.f, 0.f});
    source.Connect(PinRef{a, 2, PinDir::Output}, PinRef{g, 0, PinDir::Input});

    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));

    EXPECT_EQ(target.Data().NodeCount(), 1u);
    EXPECT_EQ(target.Data().LinkCount(), 0u);
}

TEST(GraphSerializeTests, AnEmptyGraphRoundTrips)
{
    Controller source;
    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    EXPECT_TRUE(target.Restore(blob));
    EXPECT_EQ(target.Data().NodeCount(), 0u);
    EXPECT_EQ(target.Data().LinkCount(), 0u);
}

TEST(GraphSerializeTests, AnEmptyBlobIsRefusedWithoutTouchingTheGraph)
{
    Controller target;
    const NodeTypeId type = target.Type("op", OpDesc());
    target.AddNode(type, Vec2{});

    EXPECT_FALSE(target.Restore(std::vector<std::byte>{}));
    EXPECT_EQ(target.Data().NodeCount(), 1u);
}

TEST(GraphSerializeTests, ABadMagicIsRefused)
{
    Controller target;
    std::vector<std::byte> junk(64, std::byte{0x7A});
    EXPECT_FALSE(target.Restore(junk));
}

TEST(GraphSerializeTests, AFutureVersionIsRefused)
{
    Controller source;
    std::vector<std::byte> blob = source.Serialize();
    EXPECT_GE(blob.size(), 8u);
    const std::uint32_t future = 999u;
    std::memcpy(blob.data() + 4, &future, sizeof(future));

    Controller target;
    EXPECT_FALSE(target.Restore(blob));
}

TEST(GraphSerializeTests, ATruncatedBlobIsRefusedWithoutTouchingTheGraph)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    source.AddNode(type, Vec2{5.f, 5.f});
    const std::vector<std::byte> blob = source.Serialize();

    Controller target;
    target.Type("op", OpDesc());
    const GraphNodeId keep = target.AddNode(NodeTypeId{"op"}, Vec2{1.f, 1.f});

    for (std::size_t cut = 1; cut < blob.size(); cut += 3)
    {
        const std::vector<std::byte> partial(blob.begin(), blob.begin() + cut);
        EXPECT_FALSE(target.Restore(partial));
    }
    EXPECT_EQ(target.Data().NodeCount(), 1u);
    EXPECT_TRUE(target.Data().Alive(keep));
    EXPECT_EQ(target.Data().Get(keep)->position, (Vec2{1.f, 1.f}));
}

TEST(GraphSerializeTests, ALinkNamingAnOutOfRangeNodeIsDropped)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId a = source.AddNode(type, Vec2{0.f, 0.f});
    const GraphNodeId b = source.AddNode(type, Vec2{300.f, 0.f});
    source.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});

    std::vector<std::byte> blob = source.Serialize();
    EXPECT_GT(blob.size(), 20u);
    const std::uint32_t bogus = 4242u;
    std::memcpy(blob.data() + blob.size() - 4 - 12, &bogus, sizeof(bogus));

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));
    EXPECT_EQ(target.Data().NodeCount(), 2u);
    EXPECT_EQ(target.Data().LinkCount(), 0u);
}
