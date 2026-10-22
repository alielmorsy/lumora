#include <gtest/gtest.h>
#include <lumora/Graph/Graph.h>

#include <vector>

using namespace lumora::nodegraph;
using lumora::Vec2;
using lumora::Rect;

namespace
{
    struct Fixture
    {
        Controller ctrl;
        NodeTypeId type;

        Fixture()
        {
            NodeTypeDesc d;
            d.title = "Op";
            d.minWidth = 150.f;
            d.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output,
                              .multi = true}};
            d.body = {PinRowCell{.input = 0, .output = 2}, PinRowCell{.input = 1}};
            type = ctrl.Type("op", std::move(d));
        }

        [[nodiscard]] static PinRef Out(GraphNodeId n) { return PinRef{n, 2, PinDir::Output}; }
        [[nodiscard]] static PinRef In(GraphNodeId n, std::uint16_t slot)
        {
            return PinRef{n, slot, PinDir::Input};
        }
    };
} // namespace

TEST(GraphControllerTests, TypeRegistersThroughTheController)
{
    Fixture f;
    EXPECT_EQ(f.type, NodeTypeId{"op"});
    EXPECT_NE(f.ctrl.Types().Find(f.type), nullptr);
    EXPECT_EQ(f.ctrl.Types().All().size(), 1u);
}

TEST(GraphControllerTests, EveryEditMovesTheRevision)
{
    Fixture f;
    const std::uint64_t start = f.ctrl.Revision().Peek();

    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const std::uint64_t afterAdd = f.ctrl.Revision().Peek();
    EXPECT_NE(afterAdd, start);

    f.ctrl.MoveNodes(std::vector<GraphNodeId>{a}, Vec2{10.f, 0.f});
    EXPECT_NE(f.ctrl.Revision().Peek(), afterAdd);
}

TEST(GraphControllerTests, AddNodeIsUndoable)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{5.f, 6.f});
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 1u);
    EXPECT_TRUE(f.ctrl.CanUndo());

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 0u);
    EXPECT_FALSE(f.ctrl.Data().Alive(a));

    f.ctrl.Redo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 1u);
}

TEST(GraphControllerTests, ConnectGoesThroughThePolicyGate)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});

    EXPECT_TRUE(f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0)).Valid());
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);

    EXPECT_FALSE(f.ctrl.Connect(Fixture::Out(b), Fixture::In(a, 0)).Valid());
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
}

TEST(GraphControllerTests, ASecondLinkOntoASingleConnectInputReplacesTheIncumbent)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const GraphNodeId c = f.ctrl.AddNode(f.type, Vec2{0.f, 300.f});

    const LinkId first = f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));
    EXPECT_TRUE(first.Valid());

    const LinkId second = f.ctrl.Connect(Fixture::Out(c), Fixture::In(b, 0));
    EXPECT_TRUE(second.Valid());
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
    EXPECT_FALSE(f.ctrl.Data().Alive(first));
}

TEST(GraphControllerTests, UndoingAReplacementRestoresTheIncumbent)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const GraphNodeId c = f.ctrl.AddNode(f.type, Vec2{0.f, 300.f});

    f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.ctrl.Connect(Fixture::Out(c), Fixture::In(b, 0));
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);

    const LinkId now = f.ctrl.Data().LinkOnInput(Fixture::In(b, 0));
    EXPECT_TRUE(now.Valid());
    EXPECT_EQ(f.ctrl.Data().Get(now)->from.node, a);
}

TEST(GraphControllerTests, AMultiConnectOutputTakesManyLinks)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});

    EXPECT_TRUE(f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0)).Valid());
    EXPECT_TRUE(f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 1)).Valid());
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 2u);
}

TEST(GraphControllerTests, RemovingANodeAndUndoingItRestoresItsLinks)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));

    f.ctrl.RemoveNode(b);
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 1u);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 0u);

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 2u);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
}

TEST(GraphControllerTests, SelectionReplacesOrAddsAndReportsChanges)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});

    int changes = 0;
    f.ctrl.onSelectionChanged = [&changes] { ++changes; };

    f.ctrl.Select(std::vector<GraphNodeId>{a});
    EXPECT_EQ(f.ctrl.Selection().size(), 1u);
    EXPECT_TRUE(f.ctrl.IsSelected(a));
    EXPECT_EQ(changes, 1);

    f.ctrl.Select(std::vector<GraphNodeId>{b}, true);
    EXPECT_EQ(f.ctrl.Selection().size(), 2u);
    EXPECT_TRUE(f.ctrl.IsSelected(a));
    EXPECT_TRUE(f.ctrl.IsSelected(b));

    f.ctrl.Select(std::vector<GraphNodeId>{b});
    EXPECT_EQ(f.ctrl.Selection().size(), 1u);
    EXPECT_FALSE(f.ctrl.IsSelected(a));

    f.ctrl.ToggleSelected(b);
    EXPECT_TRUE(f.ctrl.Selection().empty());
}

TEST(GraphControllerTests, SelectionDropsHandlesThatDie)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    f.ctrl.Select(std::vector<GraphNodeId>{a});
    f.ctrl.RemoveNode(a);
    EXPECT_TRUE(f.ctrl.Selection().empty());
}

TEST(GraphControllerTests, RemoveSelectionIsOneUndoStep)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a, b});

    f.ctrl.RemoveSelection();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 0u);

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 2u);
}

TEST(GraphControllerTests, CollapseAndGroupStateAreUndoable)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});

    f.ctrl.SetCollapsed(a, true);
    EXPECT_TRUE(f.ctrl.Data().Get(a)->collapsed);
    f.ctrl.Undo();
    EXPECT_FALSE(f.ctrl.Data().Get(a)->collapsed);

    f.ctrl.SetGroupOpen(a, 0, false);
    EXPECT_EQ(f.ctrl.Data().Get(a)->openGroups & 1u, 0u);
    f.ctrl.Undo();
    EXPECT_NE(f.ctrl.Data().Get(a)->openGroups & 1u, 0u);

    f.ctrl.SetGroupHeight(a, 0, 120.f);
    EXPECT_FLOAT_EQ(f.ctrl.Data().Get(a)->groupHeight[0], 120.f);
    f.ctrl.Undo();
    EXPECT_FLOAT_EQ(f.ctrl.Data().Get(a)->groupHeight[0], 0.f);
}

TEST(GraphControllerTests, SetGroupHeightIgnoresIndicesPastTheStoredFour)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    f.ctrl.SetGroupHeight(a, 7, 50.f);
    EXPECT_FLOAT_EQ(f.ctrl.Data().Get(a)->groupHeight[0], 0.f);
}

TEST(GraphControllerTests, DuplicateCopiesTheNodesAndTheLinksBetweenThem)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const GraphNodeId outside = f.ctrl.AddNode(f.type, Vec2{0.f, 400.f});
    f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.ctrl.Connect(Fixture::Out(outside), Fixture::In(a, 0));

    const std::vector<GraphNodeId> copies =
        f.ctrl.Duplicate(std::vector<GraphNodeId>{a, b}, Vec2{20.f, 20.f});

    EXPECT_EQ(copies.size(), 2u);
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 5u);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 3u);
    EXPECT_EQ(f.ctrl.Data().Get(copies[0])->position, (Vec2{20.f, 20.f}));

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 3u);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 2u);
}

TEST(GraphControllerTests, ATransactionGroupsSeveralEditsIntoOneUndo)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});

    f.ctrl.Begin("Drag");
    f.ctrl.MoveNodes(std::vector<GraphNodeId>{a}, Vec2{10.f, 0.f});
    f.ctrl.MoveNodes(std::vector<GraphNodeId>{a}, Vec2{10.f, 0.f});
    f.ctrl.Commit();
    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{20.f, 0.f}));

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{0.f, 0.f}));
}

TEST(GraphControllerTests, AbortRollsBackTheOpenTransaction)
{
    Fixture f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});

    f.ctrl.Begin("Drag");
    f.ctrl.MoveNodes(std::vector<GraphNodeId>{a}, Vec2{500.f, 500.f});
    f.ctrl.Abort();
    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{0.f, 0.f}));
}

TEST(GraphControllerTests, FitAllFramesEveryNodeOnceAViewportIsKnown)
{
    Fixture f;
    f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    f.ctrl.AddNode(f.type, Vec2{600.f, 400.f});

    f.ctrl.SetViewport(Rect{0.f, 0.f, 800.f, 600.f});
    f.ctrl.FitAll();

    const Rect visible = f.ctrl.View().VisibleCanvas(Rect{0.f, 0.f, 800.f, 600.f});
    EXPECT_LE(visible.x, 0.f);
    EXPECT_GE(visible.Right(), 600.f + 150.f);
}

TEST(GraphControllerTests, FitAllOnAnEmptyGraphLeavesTheCameraAlone)
{
    Fixture f;
    f.ctrl.SetViewport(Rect{0.f, 0.f, 800.f, 600.f});
    f.ctrl.View().SetZoom(1.75f);
    f.ctrl.FitAll();
    EXPECT_FLOAT_EQ(f.ctrl.View().Zoom(), 1.75f);
}

TEST(GraphControllerTests, CopyingAControllerSharesOneWorkspace)
{
    Fixture f;
    Controller other = f.ctrl;
    const GraphNodeId a = other.AddNode(f.type, Vec2{});
    EXPECT_TRUE(f.ctrl.Data().Alive(a));
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 1u);
}

TEST(GraphControllerTests, RemoveNodeFiresLinkRemovedOnlyAfterTheLinkIsGone)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const LinkId l = f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));

    bool sawItStillAlive = false;
    int  fired = 0;
    f.ctrl.onLinkRemoved = [&f, &sawItStillAlive, &fired](LinkId id)
    {
        ++fired;
        if (f.ctrl.Data().Alive(id))
        {
            sawItStillAlive = true;
        }
    };

    f.ctrl.RemoveNode(b);
    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(sawItStillAlive);
    EXPECT_FALSE(f.ctrl.Data().Alive(l));
}

TEST(GraphControllerTests, ReplacingALinkClearsASelectionOfTheIncumbent)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const GraphNodeId c = f.ctrl.AddNode(f.type, Vec2{0.f, 300.f});

    const LinkId first = f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.ctrl.SelectLink(first);
    EXPECT_EQ(f.ctrl.SelectedLink(), first);

    f.ctrl.Connect(Fixture::Out(c), Fixture::In(b, 0));
    EXPECT_FALSE(f.ctrl.SelectedLink().Valid());
}

TEST(GraphControllerTests, HooksFireForNodesAndLinks)
{
    Fixture f;
    int added = 0;
    int removed = 0;
    int linked = 0;
    int unlinked = 0;
    f.ctrl.onNodeAdded = [&added](GraphNodeId) { ++added; };
    f.ctrl.onNodeRemoved = [&removed](GraphNodeId) { ++removed; };
    f.ctrl.onLinkAdded = [&linked](LinkId) { ++linked; };
    f.ctrl.onLinkRemoved = [&unlinked](LinkId) { ++unlinked; };

    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const LinkId l = f.ctrl.Connect(Fixture::Out(a), Fixture::In(b, 0));
    f.ctrl.Disconnect(l);
    f.ctrl.RemoveNode(b);

    EXPECT_EQ(added, 2);
    EXPECT_EQ(linked, 1);
    EXPECT_EQ(unlinked, 1);
    EXPECT_EQ(removed, 1);
}
