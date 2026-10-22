#include <gtest/gtest.h>
#include "Graph/Input.h"
#include "Graph/State.h"

#include <vector>

using namespace lumora::nodegraph;
using lumora::Rect;
using lumora::Vec2;
using lumora::KeyMods;
using lumora::Key;
using lumora::KeyInfo;
using lumora::ContextMenuItem;

namespace
{
    constexpr Rect kViewport{0.f, 0.f, 800.f, 600.f};

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
            ctrl.SetViewport(kViewport);
            S().EnsureLayout();
        }

        [[nodiscard]] State& S() { return lumora::nodegraph::detail::Access(ctrl); }

        [[nodiscard]] Vec2 HeaderAt(GraphNodeId id)
        {
            S().EnsureLayout();
            const NodeGeom* g = S().layout.Of(id);
            return S().camera.ToLayer(Vec2{g->header.Center().x, g->header.Center().y}, kViewport);
        }

        [[nodiscard]] Vec2 PinAt(GraphNodeId id, std::uint16_t slot, PinDir dir)
        {
            S().EnsureLayout();
            Vec2 canvas{};
            EXPECT_TRUE(S().layout.PinPos(id, slot, dir, canvas));
            return S().camera.ToLayer(canvas, kViewport);
        }

        [[nodiscard]] Vec2 LayerOf(Vec2 canvas) { return S().camera.ToLayer(canvas, kViewport); }
    };
} // namespace

TEST(GraphInputTests, PressingEmptyCanvasStartsABoxSelectAndClearsSelection)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a});

    OnPress(f.ctrl, f.LayerOf(Vec2{600.f, 500.f}), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::BoxSelect);
    EXPECT_TRUE(f.ctrl.Selection().empty());

    OnRelease(f.ctrl, f.LayerOf(Vec2{620.f, 520.f}), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::Idle);
}

TEST(GraphInputTests, ABoxSelectPicksUpEveryNodeItCovers)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{200.f, 0.f});
    f.ctrl.AddNode(f.type, Vec2{0.f, 400.f});

    OnPress(f.ctrl, f.LayerOf(Vec2{-20.f, -20.f}), KeyMods::None);
    OnMove(f.ctrl, f.LayerOf(Vec2{400.f, 100.f}), Vec2{}, KeyMods::None);
    OnRelease(f.ctrl, f.LayerOf(Vec2{400.f, 100.f}), KeyMods::None);

    EXPECT_EQ(f.ctrl.Selection().size(), 2u);
    EXPECT_TRUE(f.ctrl.IsSelected(a));
    EXPECT_TRUE(f.ctrl.IsSelected(b));
}

TEST(GraphInputTests, ShiftBoxSelectAddsToTheExistingSelection)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{0.f, 400.f});
    f.ctrl.Select(std::vector<GraphNodeId>{b});

    OnPress(f.ctrl, f.LayerOf(Vec2{-20.f, -20.f}), KeyMods::Shift);
    OnMove(f.ctrl, f.LayerOf(Vec2{200.f, 100.f}), Vec2{}, KeyMods::Shift);
    OnRelease(f.ctrl, f.LayerOf(Vec2{200.f, 100.f}), KeyMods::Shift);

    EXPECT_EQ(f.ctrl.Selection().size(), 2u);
    EXPECT_TRUE(f.ctrl.IsSelected(a));
    EXPECT_TRUE(f.ctrl.IsSelected(b));
}

TEST(GraphInputTests, PressingANodeHeaderSelectsItAndArmsADrag)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    OnPress(f.ctrl, f.HeaderAt(a), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::DragNodes);
    EXPECT_TRUE(f.ctrl.IsSelected(a));

    OnMove(f.ctrl, f.HeaderAt(a) + Vec2{50.f, 30.f}, Vec2{50.f, 30.f}, KeyMods::None);
    OnRelease(f.ctrl, f.HeaderAt(a), KeyMods::None);

    EXPECT_EQ(f.S().tool, Tool::Idle);
    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{50.f, 30.f}));
}

TEST(GraphInputTests, ADragOfManySelectedNodesMovesThemAllAsOneUndoStep)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{0.f, 300.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a, b});

    OnPress(f.ctrl, f.HeaderAt(a), KeyMods::None);
    OnMove(f.ctrl, f.HeaderAt(a) + Vec2{10.f, 0.f}, Vec2{10.f, 0.f}, KeyMods::None);
    OnMove(f.ctrl, f.HeaderAt(a) + Vec2{20.f, 0.f}, Vec2{10.f, 0.f}, KeyMods::None);
    OnRelease(f.ctrl, f.HeaderAt(a), KeyMods::None);

    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{20.f, 0.f}));
    EXPECT_EQ(f.ctrl.Data().Get(b)->position, (Vec2{20.f, 300.f}));

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{0.f, 0.f}));
    EXPECT_EQ(f.ctrl.Data().Get(b)->position, (Vec2{0.f, 300.f}));
    EXPECT_TRUE((!f.ctrl.CanUndo() || f.ctrl.Data().Get(b)->position == Vec2{0.f, 300.f}));
}

TEST(GraphInputTests, CtrlPressingANodeTogglesItWithoutDroppingTheRest)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{0.f, 300.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a, b});

    OnPress(f.ctrl, f.HeaderAt(a), KeyMods::Ctrl);
    OnRelease(f.ctrl, f.HeaderAt(a), KeyMods::Ctrl);

    EXPECT_FALSE(f.ctrl.IsSelected(a));
    EXPECT_TRUE(f.ctrl.IsSelected(b));
}

TEST(GraphInputTests, AltDragPansTheCameraWithoutMovingAnything)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    OnPress(f.ctrl, f.HeaderAt(a), KeyMods::Alt);
    EXPECT_EQ(f.S().tool, Tool::PanCamera);

    OnMove(f.ctrl, f.HeaderAt(a) + Vec2{40.f, 0.f}, Vec2{40.f, 0.f}, KeyMods::Alt);
    OnRelease(f.ctrl, f.HeaderAt(a), KeyMods::Alt);

    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{0.f, 0.f}));
    EXPECT_LT(f.ctrl.View().Pan().x, 0.f);
}

TEST(GraphInputTests, MiddleDragPansWithoutModifiers)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    OnPress(f.ctrl, f.HeaderAt(a), KeyMods::None, true);
    EXPECT_EQ(f.S().tool, Tool::PanCamera);

    OnMove(f.ctrl, f.HeaderAt(a) + Vec2{40.f, 0.f}, Vec2{40.f, 0.f}, KeyMods::None);
    OnRelease(f.ctrl, f.HeaderAt(a), KeyMods::None);

    EXPECT_EQ(f.ctrl.Data().Get(a)->position, (Vec2{0.f, 0.f}));
    EXPECT_EQ(f.S().tool, Tool::Idle);
}

TEST(GraphInputTests, PanIsLimitedToTheContentPlusAMargin)
{
    Fixture f;
    f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    const auto shove = [&f](Vec2 perMove)
    {
        OnPress(f.ctrl, Vec2{0.f, 0.f}, KeyMods::None, true);
        for (int i = 0; i < 200; ++i)
        {
            OnMove(f.ctrl, Vec2{0.f, 0.f}, perMove, KeyMods::None);
        }
        OnRelease(f.ctrl, Vec2{0.f, 0.f}, KeyMods::None);
    };

    f.S().EnsureLayout();
    const Rect content = f.S().layout.Bounds();
    constexpr float margin = 300.f;
    constexpr float slack = 0.01f;

    shove(Vec2{-100.f, -100.f});
    Rect visible = f.ctrl.View().VisibleCanvas(kViewport);
    EXPECT_LE(visible.Center().x, content.Right() + margin + slack);
    EXPECT_LE(visible.Center().y, content.Bottom() + margin + slack);

    shove(Vec2{100.f, 100.f});
    visible = f.ctrl.View().VisibleCanvas(kViewport);
    EXPECT_GE(visible.Center().x, content.x - margin - slack);
    EXPECT_GE(visible.Center().y, content.y - margin - slack);
}

TEST(GraphInputTests, SpaceDragPansAndDisarmsOnRelease)
{
    Fixture f;
    f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    KeyInfo space{};
    space.key = Key::Space;
    EXPECT_TRUE(OnKey(f.ctrl, space));

    OnPress(f.ctrl, f.LayerOf(Vec2{400.f, 300.f}), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::PanCamera);
    OnRelease(f.ctrl, f.LayerOf(Vec2{400.f, 300.f}), KeyMods::None);

    EXPECT_FALSE(f.S().spaceHeld);
    OnPress(f.ctrl, f.LayerOf(Vec2{400.f, 300.f}), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::BoxSelect);
    OnRelease(f.ctrl, f.LayerOf(Vec2{400.f, 300.f}), KeyMods::None);
}

TEST(GraphInputTests, OnKeyUpAlsoDisarmsSpaceForAHostThatDeliversIt)
{
    Fixture f;
    KeyInfo space{};
    space.key = Key::Space;

    EXPECT_TRUE(OnKey(f.ctrl, space));
    EXPECT_TRUE(f.S().spaceHeld);
    OnKeyUp(f.ctrl, space);
    EXPECT_FALSE(f.S().spaceHeld);
}

TEST(GraphInputTests, DraggingFromAnOutputPinToACompatibleInputConnects)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});

    OnPress(f.ctrl, f.PinAt(a, 2, PinDir::Output), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::DragLink);

    OnMove(f.ctrl, f.PinAt(b, 0, PinDir::Input), Vec2{}, KeyMods::None);
    EXPECT_TRUE(f.S().pendingLegal);

    OnRelease(f.ctrl, f.PinAt(b, 0, PinDir::Input), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::Idle);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
}

TEST(GraphInputTests, DroppingALinkOnEmptyCanvasConnectsNothingAndLeavesNoState)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    OnPress(f.ctrl, f.PinAt(a, 2, PinDir::Output), KeyMods::None);
    OnMove(f.ctrl, f.LayerOf(Vec2{600.f, 500.f}), Vec2{}, KeyMods::None);
    EXPECT_FALSE(f.S().pendingLegal);
    OnRelease(f.ctrl, f.LayerOf(Vec2{600.f, 500.f}), KeyMods::None);

    EXPECT_EQ(f.S().tool, Tool::Idle);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 0u);
    EXPECT_FALSE(f.S().pendingFrom.node.Valid());
    EXPECT_TRUE(!f.ctrl.CanUndo() || f.ctrl.Data().LinkCount() == 0);
}

TEST(GraphInputTests, AnIllegalDropIsRefusedAndReportedBeforeRelease)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    f.ctrl.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});

    OnPress(f.ctrl, f.PinAt(b, 2, PinDir::Output), KeyMods::None);
    OnMove(f.ctrl, f.PinAt(a, 0, PinDir::Input), Vec2{}, KeyMods::None);
    EXPECT_FALSE(f.S().pendingLegal);
    OnRelease(f.ctrl, f.PinAt(a, 0, PinDir::Input), KeyMods::None);

    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
}

TEST(GraphInputTests, DraggingFromAConnectedInputDetachesTheExistingLink)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const GraphNodeId c = f.ctrl.AddNode(f.type, Vec2{0.f, 400.f});
    f.ctrl.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});

    OnPress(f.ctrl, f.PinAt(b, 0, PinDir::Input), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::DragLink);
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 0u);
    EXPECT_EQ(f.S().pendingFrom, (PinRef{a, 2, PinDir::Output}));

    OnMove(f.ctrl, f.PinAt(c, 0, PinDir::Input), Vec2{}, KeyMods::None);
    OnRelease(f.ctrl, f.PinAt(c, 0, PinDir::Input), KeyMods::None);

    EXPECT_EQ(f.ctrl.Data().LinkCount(), 1u);
    const LinkId now = f.ctrl.Data().LinkOnInput(PinRef{c, 0, PinDir::Input});
    EXPECT_TRUE(now.Valid());
}

TEST(GraphInputTests, PressingALinkSelectsItAndDeleteRemovesIt)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{400.f, 0.f});
    const LinkId l = f.ctrl.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});

    f.S().EnsureLayout();
    Vec2 from{}, to{};
    EXPECT_TRUE(f.S().layout.PinPos(a, 2, PinDir::Output, from));
    EXPECT_TRUE(f.S().layout.PinPos(b, 0, PinDir::Input, to));
    const Vec2 mid = f.LayerOf(Vec2{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f});

    OnPress(f.ctrl, mid, KeyMods::None);
    OnRelease(f.ctrl, mid, KeyMods::None);
    EXPECT_EQ(f.ctrl.SelectedLink(), l);

    KeyInfo del{};
    del.key = Key::Delete;
    EXPECT_TRUE(OnKey(f.ctrl, del));
    EXPECT_EQ(f.ctrl.Data().LinkCount(), 0u);
}

TEST(GraphInputTests, ScrollZoomsAboutTheLastPointerPosition)
{
    Fixture f;
    OnMove(f.ctrl, Vec2{400.f, 300.f}, Vec2{}, KeyMods::None);
    const Vec2 before = f.S().camera.ToCanvas(Vec2{400.f, 300.f}, kViewport);

    OnScroll(f.ctrl, 1.f);
    EXPECT_GT(f.ctrl.View().Zoom(), 1.f);

    const Vec2 after = f.S().camera.ToCanvas(Vec2{400.f, 300.f}, kViewport);
    EXPECT_LT(std::fabs(before.x - after.x), 0.01f);
    EXPECT_LT(std::fabs(before.y - after.y), 0.01f);

    OnScroll(f.ctrl, -1.f);
    EXPECT_LT(std::fabs(f.ctrl.View().Zoom() - 1.f), 0.01f);
}

TEST(GraphInputTests, DoubleClickingAHeaderTogglesCollapse)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    OnDoubleClick(f.ctrl, f.HeaderAt(a));
    EXPECT_TRUE(f.ctrl.Data().Get(a)->collapsed);

    OnDoubleClick(f.ctrl, f.HeaderAt(a));
    EXPECT_FALSE(f.ctrl.Data().Get(a)->collapsed);
}

TEST(GraphInputTests, DoubleClickingABodyReportsActivation)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    GraphNodeId activated{};
    f.ctrl.onNodeActivated = [&activated](GraphNodeId id) { activated = id; };

    f.S().EnsureLayout();
    const NodeGeom* g = f.S().layout.Of(a);
    OnDoubleClick(f.ctrl, f.LayerOf(Vec2{g->rect.Center().x, g->body.Center().y}));

    EXPECT_EQ(activated, a);
    EXPECT_FALSE(f.ctrl.Data().Get(a)->collapsed);
}

TEST(GraphInputTests, ClickingAGroupCaretTogglesThatGroup)
{
    Fixture f;
    NodeTypeDesc d;
    d.title = "Tex";
    d.minWidth = 200.f;
    d.pins = {PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
    d.body = {PinRowCell{.output = 0},
              GroupCell{.title = "Preview", .resizable = true,
                        .children = {ImageCell{.aspect = 2.f}}}};
    const NodeTypeId tex = f.ctrl.Type("tex", std::move(d));
    const GraphNodeId     a = f.ctrl.AddNode(tex, Vec2{0.f, 0.f});

    f.S().EnsureLayout();
    const NodeGeom* g = f.S().layout.Of(a);
    int caret = -1;
    for (int i = 0; i < static_cast<int>(g->cells.size()); ++i)
    {
        if (g->cells[i].groupIndex == 0)
        {
            caret = i;
        }
    }
    ASSERT_GE(caret, 0);

    const Rect cell = g->cells[caret].rect;
    OnPress(f.ctrl, f.LayerOf(Vec2{cell.x + 2.f, cell.Center().y}), KeyMods::None);
    OnRelease(f.ctrl, f.LayerOf(Vec2{cell.x + 2.f, cell.Center().y}), KeyMods::None);

    EXPECT_EQ(f.ctrl.Data().Get(a)->openGroups & 1u, 0u);
}

TEST(GraphInputTests, SelectAllAndDeleteWorkFromTheKeyboard)
{
    Fixture f;
    f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    f.ctrl.AddNode(f.type, Vec2{300.f, 0.f});

    KeyInfo selectAll{};
    selectAll.key = Key::A;
    selectAll.modifiers = KeyMods::Ctrl;
    EXPECT_TRUE(OnKey(f.ctrl, selectAll));
    EXPECT_EQ(f.ctrl.Selection().size(), 2u);

    KeyInfo del{};
    del.key = Key::Delete;
    EXPECT_TRUE(OnKey(f.ctrl, del));
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 0u);

    f.ctrl.Undo();
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 2u);
}

TEST(GraphInputTests, UndoAndRedoAreBoundToCtrlZAndCtrlY)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});

    KeyInfo undo{};
    undo.key = Key::Z;
    undo.modifiers = KeyMods::Ctrl;
    EXPECT_TRUE(OnKey(f.ctrl, undo));
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 0u);

    KeyInfo redo{};
    redo.key = Key::Y;
    redo.modifiers = KeyMods::Ctrl;
    EXPECT_TRUE(OnKey(f.ctrl, redo));
    EXPECT_EQ(f.ctrl.Data().NodeCount(), 1u);
}

TEST(GraphInputTests, HomeFitsEverythingAndEndFitsTheSelection)
{
    Fixture f;
    f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId far = f.ctrl.AddNode(f.type, Vec2{2000.f, 1500.f});

    KeyInfo home{};
    home.key = Key::Home;
    EXPECT_TRUE(OnKey(f.ctrl, home));
    const float fitAll = f.ctrl.View().Zoom();
    EXPECT_LT(fitAll, 1.f);

    f.ctrl.Select(std::vector<GraphNodeId>{far});
    KeyInfo end{};
    end.key = Key::End;
    EXPECT_TRUE(OnKey(f.ctrl, end));
    EXPECT_GT(f.ctrl.View().Zoom(), fitAll);
}

TEST(GraphInputTests, EscapeCancelsABoxSelectAndRestoresThePriorSelection)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    const GraphNodeId b = f.ctrl.AddNode(f.type, Vec2{0.f, 400.f});
    f.ctrl.Select(std::vector<GraphNodeId>{b});

    OnPress(f.ctrl, f.LayerOf(Vec2{-20.f, -20.f}), KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::BoxSelect);
    OnMove(f.ctrl, f.LayerOf(Vec2{200.f, 100.f}), Vec2{}, KeyMods::None);

    KeyInfo esc{};
    esc.key = Key::Escape;
    EXPECT_TRUE(OnKey(f.ctrl, esc));

    EXPECT_EQ(f.S().tool, Tool::Idle);
    EXPECT_EQ(f.ctrl.Selection().size(), 1u);
    EXPECT_TRUE(f.ctrl.IsSelected(b));
    EXPECT_FALSE(f.ctrl.IsSelected(a));
}

TEST(GraphInputTests, AnUnboundKeyIsNotConsumed)
{
    Fixture f;
    KeyInfo tab{};
    tab.key = Key::Tab;
    EXPECT_FALSE(OnKey(f.ctrl, tab));
}

TEST(GraphInputTests, CopyThenPasteDuplicatesTheSelection)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    f.ctrl.Select(std::vector<GraphNodeId>{a});

    KeyInfo copy{};
    copy.key = Key::C;
    copy.modifiers = KeyMods::Ctrl;
    EXPECT_TRUE(OnKey(f.ctrl, copy));

    KeyInfo paste{};
    paste.key = Key::V;
    paste.modifiers = KeyMods::Ctrl;
    EXPECT_TRUE(OnKey(f.ctrl, paste));

    EXPECT_EQ(f.ctrl.Data().NodeCount(), 2u);
    EXPECT_EQ(f.ctrl.Selection().size(), 1u);
    EXPECT_FALSE(f.ctrl.IsSelected(a));
}

TEST(GraphInputTests, DraggingANodeRightEdgeResizesIt)
{
    Fixture      f;
    const GraphNodeId a = f.ctrl.AddNode(f.type, Vec2{0.f, 0.f});
    f.S().EnsureLayout();
    const NodeGeom* g = f.S().layout.Of(a);

    const Vec2 edge = f.LayerOf(Vec2{g->rect.Right() - 1.f, g->body.Center().y});
    OnPress(f.ctrl, edge, KeyMods::None);
    EXPECT_EQ(f.S().tool, Tool::ResizeNode);

    OnMove(f.ctrl, edge + Vec2{60.f, 0.f}, Vec2{60.f, 0.f}, KeyMods::None);
    OnRelease(f.ctrl, edge + Vec2{60.f, 0.f}, KeyMods::None);

    EXPECT_GT(f.ctrl.Data().Get(a)->width, 150.f);
    f.ctrl.Undo();
    EXPECT_TRUE(f.ctrl.Data().Get(a)->width == 0.f || f.ctrl.Data().Get(a)->width == 150.f);
}

TEST(GraphInputTests, TheMenuOffersEveryRegisteredTypeAndRespectsAFilter)
{
    Fixture f;
    f.ctrl.Type("other", NodeTypeDesc{.title = "Other"});
    ViewCfg cfg;

    const std::vector<ContextMenuItem> all = BuildMenu(f.ctrl, cfg, Vec2{100.f, 100.f});
    EXPECT_FALSE(all.empty());
    EXPECT_EQ(all[0].submenu.size(), 2u);

    cfg.paletteFilter = [&f] { return std::vector<NodeTypeId>{f.type}; };
    const std::vector<ContextMenuItem> filtered = BuildMenu(f.ctrl, cfg, Vec2{100.f, 100.f});
    EXPECT_FALSE(filtered.empty());
    EXPECT_EQ(filtered[0].submenu.size(), 1u);
}
