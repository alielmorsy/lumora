#include <gtest/gtest.h>
#include "Graph/History.h"

#include <string>

using namespace lumora::nodegraph;
using lumora::Vec2;

namespace
{

    struct Fixture
    {
        Registry   reg;
        Model      model;
        History    history;
        NodeTypeId type;

        Fixture()
        {
            NodeTypeDesc d;
            d.title = "Op";
            d.pins = {PinDesc{.name = "In", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
            type = reg.Register("op", std::move(d));
        }

        GraphNodeId Add(Vec2 at)
        {
            history.Begin("Add");
            const GraphNodeId id = model.Add(type, at);
            history.Record(Edit{.kind = Edit::Kind::AddNode, .node = *model.Get(id), .nodeId = id});
            history.Commit();
            return id;
        }

        LinkId Connect(GraphNodeId a, GraphNodeId b)
        {
            history.Begin("Connect");
            const PinRef from{a, 1, PinDir::Output};
            const PinRef to{b, 0, PinDir::Input};
            const LinkId id = model.Connect(from, to);
            history.Record(
                Edit{.kind = Edit::Kind::Connect, .from = from, .to = to, .linkId = id});
            history.Commit();
            return id;
        }

        void Move(GraphNodeId id, Vec2 to)
        {
            history.Begin("Move");
            const Node before = *model.Get(id);
            model.Get(id)->position = to;
            history.Record(Edit{.kind = Edit::Kind::Mutate,
                                .nodeId = id,
                                .before = before,
                                .after = *model.Get(id)});
            history.Commit();
        }
    };
} // namespace

TEST(GraphHistoryTests, AFreshHistoryHasNothingToUndo)
{
    Fixture f;
    EXPECT_FALSE(f.history.CanUndo());
    EXPECT_FALSE(f.history.CanRedo());
    EXPECT_FALSE(f.history.Undo(f.model));
    EXPECT_FALSE(f.history.Redo(f.model));
    EXPECT_FALSE(f.history.Open());
}

TEST(GraphHistoryTests, UndoingAnAddRemovesTheNodeAndRedoBringsItBack)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{10.f, 20.f});
    EXPECT_EQ(f.model.NodeCount(), 1u);
    EXPECT_TRUE(f.history.CanUndo());

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.NodeCount(), 0u);
    EXPECT_FALSE(f.model.Alive(a));
    EXPECT_TRUE(f.history.CanRedo());

    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_EQ(f.model.NodeCount(), 1u);
    EXPECT_TRUE(f.model.At(0).Valid());
    EXPECT_EQ(f.model.Get(f.model.At(0))->position, (Vec2{10.f, 20.f}));
}

TEST(GraphHistoryTests, UndoingAMutateRestoresTheOldFieldsAndRedoReappliesThem)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{0.f, 0.f});
    f.Move(a, Vec2{100.f, 200.f});
    EXPECT_EQ(f.model.Get(a)->position, (Vec2{100.f, 200.f}));

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.Get(a)->position, (Vec2{0.f, 0.f}));

    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_EQ(f.model.Get(a)->position, (Vec2{100.f, 200.f}));
}

TEST(GraphHistoryTests, UndoingAConnectDropsTheLinkAndRedoRestoresIt)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{});
    const GraphNodeId b = f.Add(Vec2{300.f, 0.f});
    f.Connect(a, b);
    EXPECT_EQ(f.model.LinkCount(), 1u);

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.LinkCount(), 0u);

    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_EQ(f.model.LinkCount(), 1u);
}

TEST(GraphHistoryTests, OneTransactionIsOneUndoStepHoweverManyEdits)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{});
    const GraphNodeId b = f.Add(Vec2{300.f, 0.f});

    f.history.Begin("Move both");
    for (const GraphNodeId id : {a, b})
    {
        const Node before = *f.model.Get(id);
        f.model.Get(id)->position += Vec2{50.f, 0.f};
        f.history.Record(Edit{.kind = Edit::Kind::Mutate,
                              .nodeId = id,
                              .before = before,
                              .after = *f.model.Get(id)});
    }
    f.history.Commit();

    EXPECT_EQ(f.model.Get(a)->position, (Vec2{50.f, 0.f}));
    EXPECT_EQ(f.model.Get(b)->position, (Vec2{350.f, 0.f}));

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.Get(a)->position, (Vec2{0.f, 0.f}));
    EXPECT_EQ(f.model.Get(b)->position, (Vec2{300.f, 0.f}));
    EXPECT_TRUE(f.history.CanUndo());
    EXPECT_EQ(f.history.UndoLabel(), "Add");
}

TEST(GraphHistoryTests, AnEmptyTransactionIsNotPushed)
{
    Fixture f;
    f.history.Begin("Nothing");
    f.history.Commit();
    EXPECT_FALSE(f.history.CanUndo());
}

TEST(GraphHistoryTests, AbortRollsBackWithoutTouchingTheUndoStack)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{0.f, 0.f});

    f.history.Begin("Drag");
    const Node before = *f.model.Get(a);
    f.model.Get(a)->position = Vec2{999.f, 999.f};
    f.history.Record(Edit{.kind = Edit::Kind::Mutate,
                          .nodeId = a,
                          .before = before,
                          .after = *f.model.Get(a)});
    EXPECT_TRUE(f.history.Open());
    f.history.Abort(f.model);

    EXPECT_FALSE(f.history.Open());
    EXPECT_EQ(f.model.Get(a)->position, (Vec2{0.f, 0.f}));
    EXPECT_TRUE(f.history.CanUndo());
    EXPECT_EQ(f.history.UndoLabel(), "Add");
}

TEST(GraphHistoryTests, ANewEditClearsTheRedoStack)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{0.f, 0.f});
    f.Move(a, Vec2{10.f, 0.f});
    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_TRUE(f.history.CanRedo());

    f.Move(f.model.At(0), Vec2{20.f, 0.f});
    EXPECT_FALSE(f.history.CanRedo());
}

TEST(GraphHistoryTests, HandlesStayValidAcrossAnUndoRedoOfAnEarlierAdd)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{0.f, 0.f});
    f.Move(a, Vec2{10.f, 10.f});

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.NodeCount(), 0u);

    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_EQ(f.model.NodeCount(), 1u);

    const GraphNodeId current = f.model.At(0);
    EXPECT_TRUE(current.Valid());
    EXPECT_EQ(f.model.Get(current)->position, (Vec2{10.f, 10.f}));
}

TEST(GraphHistoryTests, ALinkSurvivesAnUndoRedoOfItsEndpointNode)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{});
    const GraphNodeId b = f.Add(Vec2{300.f, 0.f});
    f.Connect(a, b);

    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_TRUE(f.history.Undo(f.model));
    EXPECT_EQ(f.model.NodeCount(), 1u);

    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_TRUE(f.history.Redo(f.model));
    EXPECT_EQ(f.model.LinkCount(), 1u);
}

TEST(GraphHistoryTests, ClearDropsBothStacks)
{
    Fixture f;
    f.Add(Vec2{});
    f.history.Clear();
    EXPECT_FALSE(f.history.CanUndo());
    EXPECT_FALSE(f.history.CanRedo());
}

TEST(GraphHistoryTests, UndoLabelNamesTheTopTransaction)
{
    Fixture f;
    const GraphNodeId a = f.Add(Vec2{});
    EXPECT_EQ(f.history.UndoLabel(), "Add");
    f.Move(a, Vec2{1.f, 1.f});
    EXPECT_EQ(f.history.UndoLabel(), "Move");
}
