#include <gtest/gtest.h>
#include <lumora/Graph/Types.h>

#include <string>
#include <variant>

using namespace lumora::nodegraph;
using lumora::ImageHandle;

TEST(GraphTypesTests, HashedIdsAreStableDistinctAndNeverZero)
{
    EXPECT_EQ(NodeTypeId{"add"}, NodeTypeId{"add"});
    EXPECT_FALSE(NodeTypeId{"add"} == NodeTypeId{"multiply"});
    EXPECT_NE(NodeTypeId{"add"}.value, 0u);
    EXPECT_NE(NodeTypeId{""}.value, 0u);
    EXPECT_EQ(NodeTypeId{}.value, 0u);

    EXPECT_EQ(PinTypeId{"float3"}, PinTypeId{"float3"});
    EXPECT_FALSE(PinTypeId{"float3"} == PinTypeId{"float4"});
    EXPECT_EQ(PinTypeId{}.value, 0u);
}

TEST(GraphTypesTests, DefaultHandlesAreInvalid)
{
    EXPECT_FALSE(GraphNodeId{}.Valid());
    EXPECT_FALSE(LinkId{}.Valid());
    EXPECT_TRUE((GraphNodeId{1, 1}.Valid()));
    EXPECT_TRUE((GraphNodeId{0, 1}.Valid()));
    EXPECT_TRUE((GraphNodeId{1, 1} == GraphNodeId{1, 1}));
    EXPECT_FALSE((GraphNodeId{1, 1} == GraphNodeId{1, 2}));
}

TEST(GraphTypesTests, RegistryRoundTripsDescriptorsAndSourceStrings)
{
    Registry reg;

    NodeTypeDesc add;
    add.title = "Add";
    add.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};

    const NodeTypeId id = reg.Register("math.add", std::move(add));
    EXPECT_EQ(id, NodeTypeId{"math.add"});
    EXPECT_EQ(reg.SourceOf(id), "math.add");

    const NodeTypeDesc* found = reg.Find(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->title, "Add");
    EXPECT_EQ(found->pins.size(), 3u);
    EXPECT_EQ(found->pins[2].dir, PinDir::Output);

    EXPECT_EQ(reg.Find(NodeTypeId{"nope"}), nullptr);
    EXPECT_TRUE(reg.SourceOf(NodeTypeId{"nope"}).empty());
}

TEST(GraphTypesTests, RegistryPreservesRegistrationOrder)
{
    Registry reg;
    reg.Register("c", NodeTypeDesc{.title = "C"});
    reg.Register("a", NodeTypeDesc{.title = "A"});
    reg.Register("b", NodeTypeDesc{.title = "B"});

    const std::span<const NodeTypeId> all = reg.All();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0], NodeTypeId{"c"});
    EXPECT_EQ(all[1], NodeTypeId{"a"});
    EXPECT_EQ(all[2], NodeTypeId{"b"});
}

TEST(GraphTypesTests, ReRegisteringReplacesWithoutDuplicatingTheOrderEntry)
{
    Registry reg;
    reg.Register("x", NodeTypeDesc{.title = "First"});
    reg.Register("x", NodeTypeDesc{.title = "Second"});

    EXPECT_EQ(reg.All().size(), 1u);
    EXPECT_EQ(reg.Find(NodeTypeId{"x"})->title, "Second");
}

TEST(GraphTypesTests, ACollapsibleResizablePreviewIsJustAGroupHoldingAnImage)
{
    NodeTypeDesc desc;
    desc.title = "Texture";
    desc.body.push_back(GroupCell{.title = "Preview",
                                  .defaultOpen = true,
                                  .resizable = true,
                                  .children = {ImageCell{.image = [](GraphNodeId) { return ImageHandle{}; }}}});

    EXPECT_EQ(desc.body.size(), 1u);
    const GroupCell* group = std::get_if<GroupCell>(&desc.body[0]);
    ASSERT_NE(group, nullptr);
    EXPECT_TRUE(group->resizable);
    EXPECT_TRUE(group->defaultOpen);
    EXPECT_EQ(group->children.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ImageCell>(group->children[0]));
}

TEST(GraphTypesTests, EveryCellKindIsConstructibleAsACell)
{
    std::vector<Cell> body;
    body.push_back(PinRowCell{.input = 0, .output = 1});
    body.push_back(LabelCell{.text = "hello"});
    body.push_back(TextCell{.label = "value", .value = [](GraphNodeId) { return std::string{"1.0"}; }});
    body.push_back(ImageCell{});
    body.push_back(SpacerCell{});
    body.push_back(CustomCell{.height = 30.f});
    body.push_back(GroupCell{.title = "g"});
    EXPECT_EQ(body.size(), 7u);
    EXPECT_EQ(std::get<PinRowCell>(body[0]).output, 1);
    EXPECT_EQ(std::get<PinRowCell>(body[0]).input, 0);
    EXPECT_EQ(PinRowCell{}.input, NoPin);
    EXPECT_EQ(PinRowCell{}.output, NoPin);
}

TEST(GraphTypesTests, ThemeDefaultsAreSaneMetrics)
{
    const Theme t;
    EXPECT_GT(t.HeaderHeight, 0.f);
    EXPECT_GT(t.CellHeight, 0.f);
    EXPECT_GT(t.PinRadius, 0.f);
    EXPECT_GT(t.GridSpacing, 0.f);
    EXPECT_GE(t.NodeRadius, 0.f);
    EXPECT_FLOAT_EQ(t.SnapSize, 0.f);
}
