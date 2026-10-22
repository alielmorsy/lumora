#include <gtest/gtest.h>
#include <lumora/Graph/Fields.h>
#include <lumora/Graph/Graph.h>

#include "Graph/Input.h"
#include "Graph/State.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <variant>
#include <vector>

using namespace lumora::nodegraph;
using lumora::Vec2;
using lumora::Rect;
using lumora::KeyMods;
using lumora::Key;
using lumora::KeyInfo;
using lumora::Color;

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

    [[nodiscard]] bool Member(const Frame& frame, GraphNodeId id)
    {
        return std::ranges::find(frame.members, id) != frame.members.end();
    }

    struct V1Writer
    {
        std::vector<std::byte> bytes;

        template <typename T>
        void Pod(const T& value)
        {
            const auto* first = reinterpret_cast<const std::byte*>(&value);
            bytes.insert(bytes.end(), first, first + sizeof(T));
        }

        void Str(std::string_view text)
        {
            Pod(static_cast<std::uint32_t>(text.size()));
            const auto* first = reinterpret_cast<const std::byte*>(text.data());
            bytes.insert(bytes.end(), first, first + text.size());
        }
    };

    struct CompatFixture
    {
        Registry   reg;
        Model      model;
        LinkPolicy policy;
        NodeTypeId floatOp;
        NodeTypeId vecOp;
        NodeTypeId source;
        NodeTypeId mixed;

        CompatFixture()
        {
            floatOp = reg.Register("f", OpDesc());
            vecOp = reg.Register("v", NodeTypeDesc{
                .title = "Vec",
                .pins = {PinDesc{.name = "In", .type = PinTypeId{"vec"}, .dir = PinDir::Input},
                         PinDesc{.name = "Out", .type = PinTypeId{"vec"}, .dir = PinDir::Output}}});
            source = reg.Register("src", NodeTypeDesc{
                .title = "Source",
                .pins = {PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}}});
            mixed = reg.Register("mix", NodeTypeDesc{
                .title = "Mixed",
                .pins = {PinDesc{.name = "V", .type = PinTypeId{"vec"}, .dir = PinDir::Input},
                         PinDesc{.name = "F", .type = PinTypeId{"float"}, .dir = PinDir::Input}}});
        }
    };

    void DragHeader(Controller& ctrl, Vec2 from, Vec2 to, KeyMods mods)
    {
        OnPress(ctrl, from, KeyMods::None);
        OnMove(ctrl, to, to - from, mods);
        OnRelease(ctrl, to, mods);
    }
} // namespace

TEST(GraphFlowTests, AFrameAroundTheSelectionHoldsExactlyTheNodesInsideIt)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const GraphNodeId a = ctrl.AddNode(type, Vec2{0.f, 0.f});
    const GraphNodeId b = ctrl.AddNode(type, Vec2{200.f, 0.f});
    const GraphNodeId far = ctrl.AddNode(type, Vec2{2000.f, 2000.f});
    const GraphNodeId pick[2] = {a, b};
    ctrl.Select(pick);

    const FrameId id = ctrl.AddFrame("Island", Color::Rgba(10, 20, 30, 255));
    EXPECT_TRUE(id.Valid());
    EXPECT_EQ(ctrl.Frames().size(), 1u);
    const Frame& frame = ctrl.Frames().front();
    EXPECT_EQ(frame.title, "Island");
    EXPECT_TRUE(Member(frame, a));
    EXPECT_TRUE(Member(frame, b));
    EXPECT_FALSE(Member(frame, far));
    EXPECT_TRUE(frame.rect.x < 0.f && frame.rect.y < 0.f);
}

TEST(GraphFlowTests, AddFrameWithNothingSelectedIsRefused)
{
    Controller ctrl;
    ctrl.Type("op", OpDesc());
    EXPECT_FALSE(ctrl.AddFrame().Valid());
    EXPECT_TRUE(ctrl.Frames().empty());
}

TEST(GraphFlowTests, MovingAFrameCarriesItsMembersOnly)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const GraphNodeId inside = ctrl.AddNode(type, Vec2{10.f, 40.f});
    const GraphNodeId outside = ctrl.AddNode(type, Vec2{900.f, 900.f});
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");

    ctrl.MoveFrame(id, Vec2{50.f, 25.f});
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, (Rect{50.f, 25.f, 400.f, 300.f}));
    EXPECT_EQ(ctrl.Data().Get(inside)->position, (Vec2{60.f, 65.f}));
    EXPECT_EQ(ctrl.Data().Get(outside)->position, (Vec2{900.f, 900.f}));

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, (Rect{0.f, 0.f, 400.f, 300.f}));
    EXPECT_EQ(ctrl.Data().Get(inside)->position, (Vec2{10.f, 40.f}));
}

TEST(GraphFlowTests, ResizeThenRefreshRecomputesMembership)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const GraphNodeId near = ctrl.AddNode(type, Vec2{10.f, 40.f});
    const GraphNodeId later = ctrl.AddNode(type, Vec2{600.f, 40.f});
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), later));

    ctrl.Begin("Resize frame");
    ctrl.SetFrameRect(id, Rect{0.f, 0.f, 1000.f, 300.f});
    ctrl.RefreshFrameMembers(id);
    ctrl.Commit();
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), near));
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), later));

    ctrl.Undo();
    EXPECT_FLOAT_EQ(ctrl.Data().GetFrame(id)->rect.width, 400.f);
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), later));
}

TEST(GraphFlowTests, FrameEditsUndoAndRedoUnderTheSameId)
{
    Controller ctrl;
    ctrl.Type("op", OpDesc());
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 100.f, 100.f}, "Old");
    ctrl.SetFrameTitle(id, "New");
    ctrl.SetFrameColor(id, Color::Rgba(1, 2, 3, 255));
    EXPECT_EQ(ctrl.Data().GetFrame(id)->title, "New");

    ctrl.Undo();
    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->title, "Old");
    ctrl.Undo();
    EXPECT_TRUE(ctrl.Frames().empty());
    ctrl.Redo();
    EXPECT_NE(ctrl.Data().GetFrame(id), nullptr);

    ctrl.RemoveFrame(id);
    EXPECT_TRUE(ctrl.Frames().empty());
    ctrl.Undo();
    EXPECT_NE(ctrl.Data().GetFrame(id), nullptr);
    EXPECT_EQ(ctrl.Data().GetFrame(id)->title, "Old");
}

TEST(GraphFlowTests, UndoingANodeDeletionPutsItBackInItsFrame)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const GraphNodeId node = ctrl.AddNode(type, Vec2{10.f, 40.f});
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));

    ctrl.RemoveNode(node);
    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().NodeCount(), 1u);
    const Frame& frame = *ctrl.Data().GetFrame(id);
    EXPECT_EQ(frame.members.size(), 1u);
    EXPECT_TRUE(ctrl.Data().Alive(frame.members.front()));
}

TEST(GraphFlowTests, FramesSurviveTheRoundTrip)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId a = source.AddNode(type, Vec2{10.f, 40.f});
    source.AddNode(type, Vec2{900.f, 900.f});
    source.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "Island", Color::Rgba(9, 8, 7, 200));
    (void)a;

    const std::vector<std::byte> blob = source.Serialize();
    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(blob));
    EXPECT_EQ(target.Frames().size(), 1u);
    const Frame& frame = target.Frames().front();
    EXPECT_EQ(frame.title, "Island");
    EXPECT_EQ(frame.rect, (Rect{0.f, 0.f, 400.f, 300.f}));
    EXPECT_EQ(frame.color.r, 9);
    EXPECT_EQ(frame.color.a, 200);
    EXPECT_EQ(frame.members.size(), 1u);
    EXPECT_EQ(target.Data().Get(frame.members.front())->position, (Vec2{10.f, 40.f}));
    EXPECT_FALSE(target.CanUndo());
}

TEST(GraphFlowTests, AVersionOneBlobStillRestores)
{
    V1Writer w;
    const char magic[4] = {'D', 'N', 'G', 'R'};
    for (const char c : magic)
    {
        w.Pod(c);
    }
    w.Pod(std::uint32_t{1});
    w.Pod(3.f);
    w.Pod(4.f);
    w.Pod(1.5f);
    w.Pod(std::uint32_t{1});
    w.Str("op");
    w.Pod(std::uint32_t{2});
    for (const float x : {10.f, 300.f})
    {
        w.Pod(std::uint32_t{0});
        w.Pod(x);
        w.Pod(20.f);
        w.Pod(0.f);
        w.Str("");
        w.Pod(std::uint8_t{0});
        w.Pod(~0u);
        for (int i = 0; i < 4; ++i)
        {
            w.Pod(0.f);
        }
        w.Pod(std::uint64_t{77});
    }
    w.Pod(std::uint32_t{1});
    w.Pod(std::uint32_t{0});
    w.Pod(std::uint16_t{2});
    w.Pod(std::uint32_t{1});
    w.Pod(std::uint16_t{0});
    w.Pod(std::uint32_t{1});
    w.Pod(std::uint32_t{1});

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(w.bytes));
    EXPECT_EQ(target.Data().NodeCount(), 2u);
    EXPECT_EQ(target.Data().LinkCount(), 1u);
    EXPECT_TRUE(target.Frames().empty());
    EXPECT_EQ(target.Selection().size(), 1u);
    EXPECT_FLOAT_EQ(target.View().Zoom(), 1.5f);
}

TEST(GraphFlowTests, FuzzyScoreFavoursWordStarts)
{
    EXPECT_EQ(FuzzyScore("", "Anything"), 0);
    EXPECT_EQ(FuzzyScore("xyz", "Add"), -1);
    EXPECT_GE(FuzzyScore("ADD", "add"), 0);
    EXPECT_GT(FuzzyScore("cn", "Curl Noise"), FuzzyScore("cn", "Cone"));
    EXPECT_GT(FuzzyScore("noise", "Noise"), FuzzyScore("noise", "Curl Noise"));
    EXPECT_GT(FuzzyScore("vl", "SetVelocity"), FuzzyScore("vl", "Level"));
    EXPECT_GT(FuzzyScore("curl noise", "Curl Noise"), 0);
}

TEST(GraphFlowTests, RankTypesUsesTitleKeywordsAndCategory)
{
    Registry reg;
    const NodeTypeId curl = reg.Register("curl", NodeTypeDesc{.title = "Curl Noise", .category = "Noise",
                                                              .keywords = {"turbulence"}});
    const NodeTypeId cone = reg.Register("cone", NodeTypeDesc{.title = "Cone", .category = "Shape"});
    const NodeTypeId mul = reg.Register("mul", NodeTypeDesc{.title = "Multiply", .category = "Math",
                                                            .keywords = {"times"}});

    const std::vector<NodeTypeId> byTitle = RankTypes(reg, reg.All(), "cn");
    EXPECT_EQ(byTitle.size(), 2u);
    EXPECT_EQ(byTitle[0], curl);
    EXPECT_EQ(byTitle[1], cone);

    const std::vector<NodeTypeId> byKeyword = RankTypes(reg, reg.All(), "times");
    ASSERT_EQ(byKeyword.size(), 1u);
    EXPECT_EQ(byKeyword[0], mul);

    const std::vector<NodeTypeId> byCategory = RankTypes(reg, reg.All(), "math");
    ASSERT_EQ(byCategory.size(), 1u);
    EXPECT_EQ(byCategory[0], mul);

    EXPECT_EQ(RankTypes(reg, reg.All(), "").size(), 3u);
    EXPECT_TRUE(RankTypes(reg, reg.All(), "qqq").empty());
}

TEST(GraphFlowTests, CompatibleTypesFilterByTheDraggedPin)
{
    CompatFixture f;
    const GraphNodeId from = f.model.Add(f.floatOp, Vec2{});
    const PinRef dragged{from, 2, PinDir::Output};

    const std::vector<NodeTypeId> compatible =
        CompatibleTypes(f.model, f.reg, f.policy, dragged, f.reg.All());
    EXPECT_EQ(compatible.size(), 2u);
    EXPECT_EQ(compatible[0], f.floatOp);
    EXPECT_EQ(compatible[1], f.mixed);

    EXPECT_EQ(FirstCompatiblePin(f.model, f.reg, f.policy, dragged, f.mixed), 1);
    EXPECT_EQ(FirstCompatiblePin(f.model, f.reg, f.policy, dragged, f.vecOp), NoPin);
    EXPECT_EQ(FirstCompatiblePin(f.model, f.reg, f.policy, dragged, f.source), NoPin);
    EXPECT_EQ(f.model.NodeCount(), 1u);

    const PinRef draggedInput{from, 0, PinDir::Input};
    const std::vector<NodeTypeId> feeders =
        CompatibleTypes(f.model, f.reg, f.policy, draggedInput, f.reg.All());
    EXPECT_EQ(feeders.size(), 2u);
    EXPECT_EQ(feeders[0], f.floatOp);
    EXPECT_EQ(feeders[1], f.source);
}

TEST(GraphFlowTests, CompatibleTypesHonourThePolicy)
{
    CompatFixture f;
    f.policy.canConnect = [](const Model&, PinRef, PinRef) { return true; };
    const GraphNodeId from = f.model.Add(f.floatOp, Vec2{});
    const PinRef dragged{from, 2, PinDir::Output};
    EXPECT_EQ(FirstCompatiblePin(f.model, f.reg, f.policy, dragged, f.mixed), 0);
    EXPECT_EQ(CompatibleTypes(f.model, f.reg, f.policy, dragged, f.reg.All()).size(), 3u);
}

TEST(GraphFlowTests, PalettePicksByQueryAndPlacesAtTheCursor)
{
    Controller ctrl;
    ctrl.Type("op", OpDesc());
    const NodeTypeId curl = ctrl.Type("curl", NodeTypeDesc{.title = "Curl Noise"});
    ctrl.SetViewport(Rect{0.f, 0.f, 800.f, 600.f});

    ctrl.OpenPalette(Vec2{50.f, 60.f});
    EXPECT_TRUE(ctrl.PopupOpen());
    EXPECT_TRUE(OnText(ctrl, " "));
    EXPECT_TRUE(OnText(ctrl, "curl"));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Enter}));
    EXPECT_FALSE(ctrl.PopupOpen());
    EXPECT_EQ(ctrl.Data().NodeCount(), 1u);
    EXPECT_EQ(ctrl.Selection().size(), 1u);
    const Node* added = ctrl.Data().Get(ctrl.Selection().front());
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->type, curl);
    EXPECT_EQ(added->position, (Vec2{50.f, 60.f}));

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().NodeCount(), 0u);
}

TEST(GraphFlowTests, PaletteFromADraggedPinConnectsTheNewNode)
{
    Controller ctrl;
    const NodeTypeId op = ctrl.Type("op", OpDesc());
    ctrl.Type("vec", NodeTypeDesc{
        .title = "Vec",
        .pins = {PinDesc{.name = "In", .type = PinTypeId{"vec"}, .dir = PinDir::Input}}});
    ctrl.SetViewport(Rect{0.f, 0.f, 800.f, 600.f});
    const GraphNodeId from = ctrl.AddNode(op, Vec2{});

    OpenPaletteFor(ctrl, Vec2{400.f, 0.f}, PinRef{from, 2, PinDir::Output});
    EXPECT_EQ(lumora::nodegraph::detail::Access(ctrl).popup.items.size(), 1u);
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Enter}));
    EXPECT_EQ(ctrl.Data().NodeCount(), 2u);
    EXPECT_EQ(ctrl.Data().LinkCount(), 1u);

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().NodeCount(), 1u);
    EXPECT_EQ(ctrl.Data().LinkCount(), 0u);
}

TEST(GraphFlowTests, AFloatFieldScrubsInsideOneBeginCommitPair)
{
    float value = 1.f;
    int   begins = 0;
    int   commits = 0;
    int   sets = 0;
    const Cell cell = FloatField(FloatFieldCfg{
        .label = "Rate",
        .get = [&value](GraphNodeId) { return value; },
        .set = [&value, &sets](GraphNodeId, float v) { value = v; ++sets; },
        .onEdit = [&begins, &commits](GraphNodeId, EditPhase phase)
        { ++(phase == EditPhase::Begin ? begins : commits); },
        .speed = 0.5f});
    const auto* custom = std::get_if<CustomCell>(&cell);
    ASSERT_NE(custom, nullptr);
    EXPECT_TRUE(custom->input);
    EXPECT_EQ(custom->label, "Rate");

    CellInputCtx ctx{.rect = Rect{0.f, 0.f, 100.f, 22.f}};
    ctx.canvasPos = Vec2{10.f, 11.f};
    ctx.event = CellEvent::Press;
    EXPECT_FALSE(custom->input(ctx));

    ctx.canvasPos = Vec2{80.f, 11.f};
    EXPECT_TRUE(custom->input(ctx));
    ctx.event = CellEvent::Drag;
    ctx.pointer.delta = Vec2{1.f, 0.f};
    custom->input(ctx);
    EXPECT_EQ(begins, 0);
    EXPECT_EQ(sets, 0);
    ctx.pointer.delta = Vec2{9.f, 0.f};
    custom->input(ctx);
    custom->input(ctx);
    EXPECT_EQ(begins, 1);
    EXPECT_EQ(sets, 2);
    EXPECT_FLOAT_EQ(value, 1.f + 19.f * 0.5f);
    ctx.event = CellEvent::Release;
    custom->input(ctx);
    EXPECT_EQ(commits, 1);
}

TEST(GraphFlowTests, FramesAreStickyAndAltMovesNodesInOrOut)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    ctrl.SetViewport(Rect{0.f, 0.f, 2000.f, 1500.f});
    const GraphNodeId node = ctrl.AddNode(type, Vec2{40.f, 60.f});
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));

    DragHeader(ctrl, Vec2{100.f, 70.f}, Vec2{700.f, 70.f}, KeyMods::None);
    EXPECT_EQ(ctrl.Data().Get(node)->position, (Vec2{640.f, 60.f}));
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));
    const Rect grown = ctrl.Data().GetFrame(id)->rect;
    EXPECT_FLOAT_EQ(grown.x, 0.f);
    EXPECT_FLOAT_EQ(grown.y, 0.f);
    EXPECT_FLOAT_EQ(grown.height, 300.f);
    EXPECT_FLOAT_EQ(grown.Right(), 640.f + 150.f + ctrl.Visuals().FramePadding);

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().Get(node)->position, (Vec2{40.f, 60.f}));
    EXPECT_FLOAT_EQ(ctrl.Data().GetFrame(id)->rect.width, 400.f);
    ctrl.Redo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, grown);

    DragHeader(ctrl, Vec2{700.f, 70.f}, Vec2{1500.f, 70.f}, KeyMods::Alt);
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), node));
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, grown);

    DragHeader(ctrl, Vec2{1500.f, 70.f}, Vec2{160.f, 110.f}, KeyMods::None);
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), node));

    DragHeader(ctrl, Vec2{160.f, 110.f}, Vec2{161.f, 110.f}, KeyMods::Alt);
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));

    ctrl.RemoveFromFrame(node);
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), node));
    ctrl.Undo();
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));
}

TEST(GraphFlowTests, ANodeAddedInsideAFrameJoinsIt)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");
    const GraphNodeId inside = ctrl.AddNode(type, Vec2{20.f, 40.f});
    const GraphNodeId outside = ctrl.AddNode(type, Vec2{900.f, 40.f});
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), inside));
    EXPECT_FALSE(Member(*ctrl.Data().GetFrame(id), outside));

    const GraphNodeId one[1] = {inside};
    const std::vector<GraphNodeId> copies = ctrl.Duplicate(one, Vec2{24.f, 24.f});
    EXPECT_EQ(copies.size(), 1u);
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), copies.front()));

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->members.size(), 1u);
}

TEST(GraphFlowTests, CategoryPathsBuildACascadeTree)
{
    const std::vector<PopupItem> items{PopupItem{.title = "Sin", .category = "Math/Trigonometry"},
                                       PopupItem{.title = "Add", .category = " Math / Arithmetic "},
                                       PopupItem{.title = "Cos", .category = "Math/Trigonometry"},
                                       PopupItem{.title = "Reroute"},
                                       PopupItem{.title = "Time", .category = "Inputs//Time"}};
    const PaletteTree tree = BuildCategoryTree(items);
    EXPECT_EQ(tree.items, (std::vector<int>{3}));
    ASSERT_EQ(tree.children.size(), 2u);
    const PaletteTree& math = tree.children[0];
    EXPECT_EQ(math.name, "Math");
    EXPECT_TRUE(math.items.empty());
    ASSERT_EQ(math.children.size(), 2u);
    EXPECT_EQ(math.children[0].name, "Trigonometry");
    EXPECT_EQ(math.children[0].items, (std::vector<int>{0, 2}));
    EXPECT_EQ(math.children[1].name, "Arithmetic");
    EXPECT_EQ(math.children[1].items, (std::vector<int>{1}));
    EXPECT_EQ(tree.children[1].name, "Inputs");
    ASSERT_EQ(tree.children[1].children.size(), 1u);
    EXPECT_EQ(tree.children[1].children[0].name, "Time");
}

TEST(GraphFlowTests, TheEmptyPaletteCascadesThroughCategories)
{
    Controller ctrl;
    const NodeTypeId sine = ctrl.Type("sin", NodeTypeDesc{.title = "Sine", .category = "Math/Trigonometry"});
    ctrl.Type("add", NodeTypeDesc{.title = "Add", .category = "Math/Arithmetic"});
    const NodeTypeId time = ctrl.Type("time", NodeTypeDesc{.title = "Time"});
    ctrl.SetViewport(Rect{0.f, 0.f, 1200.f, 800.f});
    const Popup& popup = lumora::nodegraph::detail::Access(ctrl).popup;

    ctrl.OpenPalette(Vec2{10.f, 10.f});
    EXPECT_TRUE(popup.Cascading());
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Down}));
    EXPECT_EQ(popup.path, (std::vector<int>{1}));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Enter}));
    EXPECT_FALSE(ctrl.PopupOpen());
    ASSERT_EQ(ctrl.Selection().size(), 1u);
    EXPECT_EQ(ctrl.Data().Get(ctrl.Selection().front())->type, time);

    ctrl.OpenPalette(Vec2{10.f, 10.f});
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Right}));
    EXPECT_EQ(popup.path, (std::vector<int>{0, 0}));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Left}));
    EXPECT_EQ(popup.path, (std::vector<int>{0}));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Right}));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Right}));
    EXPECT_EQ(popup.path, (std::vector<int>{0, 0, 0}));
    EXPECT_TRUE(OnKey(ctrl, KeyInfo{.key = Key::Enter}));
    EXPECT_EQ(ctrl.Data().Get(ctrl.Selection().front())->type, sine);

    ctrl.OpenPalette(Vec2{10.f, 10.f});
    EXPECT_TRUE(OnText(ctrl, "add"));
    EXPECT_FALSE(popup.Cascading());
    EXPECT_FALSE(popup.rows.empty());
}

TEST(GraphFlowTests, PinsAnOverrideBodyLeavesOutAreHiddenAndUnconnectable)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    const GraphNodeId a = ctrl.AddNode(type, Vec2{0.f, 0.f});
    const GraphNodeId b = ctrl.AddNode(type, Vec2{300.f, 0.f});
    const std::vector<Cell> narrow{PinRowCell{.input = 0, .output = 2}};
    State& s = lumora::nodegraph::detail::Access(ctrl);
    s.cfg.nodeBody = [&narrow, b](GraphNodeId id) { return id == b ? &narrow : nullptr; };
    ctrl.Repaint();

    EXPECT_FALSE(ctrl.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 1, PinDir::Input}).Valid());
    EXPECT_TRUE(ctrl.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input}).Valid());
    EXPECT_EQ(ctrl.Data().LinkCount(), 1u);

    s.EnsureLayout();
    Vec2 centre{};
    EXPECT_FALSE(s.layout.PinPos(b, 1, PinDir::Input, centre));
    EXPECT_TRUE(s.layout.PinPos(a, 1, PinDir::Input, centre));
    EXPECT_GT(ctrl.CellRect(b, 0).height, 0.f);
    EXPECT_FLOAT_EQ(ctrl.CellRect(b, 1).width, 0.f);
    EXPECT_GT(ctrl.CellRect(a, 1).height, 0.f);
}

TEST(GraphFlowTests, ACompactNodeHasNoHeaderAndNeverCollapses)
{
    Controller ctrl;
    NodeTypeDesc desc = OpDesc();
    desc.compact = true;
    desc.symbol = "+";
    const NodeTypeId type = ctrl.Type("add", std::move(desc));
    const GraphNodeId node = ctrl.AddNode(type, Vec2{0.f, 0.f});
    State& s = lumora::nodegraph::detail::Access(ctrl);
    s.EnsureLayout();
    const NodeGeom* geom = s.layout.Of(node);
    ASSERT_NE(geom, nullptr);
    EXPECT_TRUE(geom->compact);
    EXPECT_FLOAT_EQ(geom->rect.width, ctrl.Visuals().CompactWidth);
    EXPECT_FLOAT_EQ(geom->header.height, 0.f);

    ctrl.SetCollapsed(node, true);
    EXPECT_FALSE(ctrl.Data().Get(node)->collapsed);
    ctrl.SetNodeTitle(node, "Speed");
    EXPECT_EQ(ctrl.Data().Get(node)->title, "Speed");
    ctrl.Undo();
    EXPECT_TRUE(ctrl.Data().Get(node)->title.empty());
}

TEST(GraphFlowTests, HeightResizeClampsToContentAndUndoes)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    ctrl.SetViewport(Rect{0.f, 0.f, 2000.f, 1500.f});
    const GraphNodeId node = ctrl.AddNode(type, Vec2{0.f, 0.f});
    State& s = lumora::nodegraph::detail::Access(ctrl);
    s.EnsureLayout();
    const float content = s.layout.Of(node)->rect.height;
    EXPECT_FLOAT_EQ(s.layout.Of(node)->contentHeight, content);

    ctrl.SetNodeSize(node, Vec2{200.f, content + 50.f});
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 200.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, content + 50.f);
    s.EnsureLayout();
    EXPECT_FLOAT_EQ(s.layout.Of(node)->rect.height, content + 50.f);

    ctrl.SetNodeSize(node, Vec2{10.f, 5.f});
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 150.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 0.f);
    s.EnsureLayout();
    EXPECT_FLOAT_EQ(s.layout.Of(node)->rect.height, content);

    ctrl.Undo();
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, content + 50.f);
    ctrl.Undo();
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 0.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 0.f);

    int resized = 0;
    ctrl.onNodeResized = [&resized](GraphNodeId) { ++resized; };
    s.EnsureLayout();
    const Rect r = s.layout.Of(node)->rect;
    const Vec2 bottom{r.Center().x, r.Bottom() - 1.f};
    OnPress(ctrl, bottom, KeyMods::None);
    EXPECT_EQ(s.tool, Tool::ResizeNode);
    EXPECT_TRUE(s.resizeEdges.height && !s.resizeEdges.width);
    OnMove(ctrl, bottom + Vec2{0.f, 40.f}, Vec2{0.f, 40.f}, KeyMods::None);
    OnRelease(ctrl, bottom + Vec2{0.f, 40.f}, KeyMods::None);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, content + 40.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 0.f);
    EXPECT_EQ(resized, 1);
    ctrl.Undo();
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 0.f);

    ctrl.SetCollapsed(node, true);
    ctrl.SetNodeSize(node, Vec2{0.f, 400.f});
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 0.f);
    s.EnsureLayout();
    EXPECT_FLOAT_EQ(s.layout.Of(node)->rect.height, ctrl.Visuals().HeaderHeight);
}

TEST(GraphFlowTests, NodeHeightSurvivesTheRoundTripAndOldBlobsReadZero)
{
    Controller source;
    const NodeTypeId type = source.Type("op", OpDesc());
    const GraphNodeId node = source.AddNode(type, Vec2{10.f, 20.f});
    source.SetNodeSize(node, Vec2{220.f, 180.f});

    Controller target;
    target.Type("op", OpDesc());
    EXPECT_TRUE(target.Restore(source.Serialize()));
    EXPECT_EQ(target.Data().NodeCount(), 1u);
    const Node* got = target.Data().Get(target.Data().At(0));
    ASSERT_NE(got, nullptr);
    EXPECT_FLOAT_EQ(got->width, 220.f);
    EXPECT_FLOAT_EQ(got->height, 180.f);

    V1Writer w;
    for (const char c : {'D', 'N', 'G', 'R'})
    {
        w.Pod(c);
    }
    w.Pod(std::uint32_t{2});
    w.Pod(0.f);
    w.Pod(0.f);
    w.Pod(1.f);
    w.Pod(std::uint32_t{1});
    w.Str("op");
    w.Pod(std::uint32_t{1});
    w.Pod(std::uint32_t{0});
    w.Pod(10.f);
    w.Pod(20.f);
    w.Pod(190.f);
    w.Str("");
    w.Pod(std::uint8_t{0});
    w.Pod(~0u);
    for (int i = 0; i < 4; ++i)
    {
        w.Pod(0.f);
    }
    w.Pod(std::uint64_t{5});
    w.Pod(std::uint32_t{0});
    w.Pod(std::uint32_t{0});
    w.Pod(std::uint32_t{0});

    Controller legacy;
    legacy.Type("op", OpDesc());
    EXPECT_TRUE(legacy.Restore(w.bytes));
    const Node* old = legacy.Data().Get(legacy.Data().At(0));
    ASSERT_NE(old, nullptr);
    EXPECT_FLOAT_EQ(old->width, 190.f);
    EXPECT_EQ(old->userData, 5u);
    EXPECT_FLOAT_EQ(old->height, 0.f);
}

TEST(GraphFlowTests, AStretchCellTakesTheExtraHeight)
{
    Controller   ctrl;
    NodeTypeDesc desc = OpDesc();
    desc.body = {PinRowCell{.input = 0, .output = 2}, PinRowCell{.input = 1},
                 CustomCell{.height = 30.f, .stretch = true}, CustomCell{.height = 20.f, .stretch = true},
                 LabelCell{.text = "Tail"}};
    const NodeTypeId stretchy = ctrl.Type("stretchy", std::move(desc));
    const NodeTypeId plain = ctrl.Type("op", OpDesc());
    const GraphNodeId a = ctrl.AddNode(stretchy, Vec2{0.f, 0.f});
    const GraphNodeId b = ctrl.AddNode(plain, Vec2{400.f, 0.f});
    State&           s = lumora::nodegraph::detail::Access(ctrl);
    s.EnsureLayout();
    const float content = s.layout.Of(a)->rect.height;

    ctrl.SetNodeSize(a, Vec2{0.f, content + 100.f});
    EXPECT_FLOAT_EQ(ctrl.CellRect(a, 2).height, 80.f);
    EXPECT_FLOAT_EQ(ctrl.CellRect(a, 3).height, 70.f);
    EXPECT_FLOAT_EQ(ctrl.CellRect(a, 3).y, ctrl.CellRect(a, 2).Bottom());
    EXPECT_FLOAT_EQ(ctrl.CellRect(a, 4).y, ctrl.CellRect(a, 3).Bottom());
    s.EnsureLayout();
    EXPECT_FLOAT_EQ(s.layout.Of(a)->rect.height, content + 100.f);
    EXPECT_FLOAT_EQ(ctrl.CellRect(a, 4).Bottom(), s.layout.Of(a)->rect.Bottom());

    const float plainContent = s.layout.Of(b)->rect.height;
    ctrl.SetNodeSize(b, Vec2{0.f, plainContent + 30.f});
    EXPECT_FLOAT_EQ(ctrl.CellRect(b, 1).height, ctrl.Visuals().CellHeight);
    s.EnsureLayout();
    EXPECT_FLOAT_EQ(s.layout.Of(b)->body.height, plainContent - ctrl.Visuals().HeaderHeight + 30.f);
}

TEST(GraphFlowTests, AFrameFollowsADraggedOrResizedMemberLive)
{
    Controller ctrl;
    const NodeTypeId type = ctrl.Type("op", OpDesc());
    ctrl.SetViewport(Rect{0.f, 0.f, 2000.f, 1500.f});
    const GraphNodeId node = ctrl.AddNode(type, Vec2{40.f, 60.f});
    const FrameId id = ctrl.AddFrame(Rect{0.f, 0.f, 400.f, 300.f}, "F");
    const Rect    before{0.f, 0.f, 400.f, 300.f};
    const float   pad = ctrl.Visuals().FramePadding;

    OnPress(ctrl, Vec2{100.f, 70.f}, KeyMods::None);
    OnMove(ctrl, Vec2{400.f, 70.f}, Vec2{300.f, 0.f}, KeyMods::None);
    EXPECT_FLOAT_EQ(ctrl.Data().GetFrame(id)->rect.Right(), 340.f + 150.f + pad);
    OnMove(ctrl, Vec2{600.f, 400.f}, Vec2{200.f, 330.f}, KeyMods::None);
    const Rect live = ctrl.Data().GetFrame(id)->rect;
    EXPECT_FLOAT_EQ(live.x, 0.f);
    EXPECT_FLOAT_EQ(live.y, 0.f);
    EXPECT_FLOAT_EQ(live.Right(), 540.f + 150.f + pad);
    EXPECT_FLOAT_EQ(live.Bottom(), 390.f + 70.f + pad);

    OnMove(ctrl, Vec2{620.f, 400.f}, Vec2{20.f, 0.f}, KeyMods::Alt);
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, before);
    OnMove(ctrl, Vec2{600.f, 400.f}, Vec2{-20.f, 0.f}, KeyMods::None);
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, live);

    OnRelease(ctrl, Vec2{600.f, 400.f}, KeyMods::None);
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, live);
    EXPECT_TRUE(Member(*ctrl.Data().GetFrame(id), node));

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, before);
    EXPECT_EQ(ctrl.Data().Get(node)->position, (Vec2{40.f, 60.f}));

    const Vec2 corner{189.f, 129.f};
    OnPress(ctrl, corner, KeyMods::None);
    State& s = lumora::nodegraph::detail::Access(ctrl);
    EXPECT_TRUE(s.tool == Tool::ResizeNode && s.resizeEdges.width && s.resizeEdges.height);
    OnMove(ctrl, corner + Vec2{300.f, 250.f}, Vec2{300.f, 250.f}, KeyMods::None);
    EXPECT_FLOAT_EQ(ctrl.Data().GetFrame(id)->rect.Right(), 490.f + pad);
    EXPECT_FLOAT_EQ(ctrl.Data().GetFrame(id)->rect.Bottom(), 380.f + pad);
    OnRelease(ctrl, corner + Vec2{300.f, 250.f}, KeyMods::None);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 450.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 320.f);

    ctrl.Undo();
    EXPECT_EQ(ctrl.Data().GetFrame(id)->rect, before);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->width, 0.f);
    EXPECT_FLOAT_EQ(ctrl.Data().Get(node)->height, 0.f);
}
