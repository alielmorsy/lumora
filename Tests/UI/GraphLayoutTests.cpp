#include <gtest/gtest.h>
#include "Graph/Layout.h"

#include <cmath>
#include <variant>
#include <vector>

using namespace lumora::nodegraph;
using lumora::Vec2;
using lumora::Rect;

namespace
{

    [[nodiscard]] bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

    struct Fixture
    {
        Registry   reg;
        Model      model;
        Theme      theme;
        Layout     layout;
        NodeTypeId plain;
        NodeTypeId grouped;

        Fixture()
        {
            NodeTypeDesc p;
            p.title = "Plain";
            p.minWidth = 160.f;
            p.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
            p.body = {PinRowCell{.input = 0, .output = 2}, PinRowCell{.input = 1}};
            plain = reg.Register("plain", std::move(p));

            NodeTypeDesc g;
            g.title = "Grouped";
            g.minWidth = 200.f;
            g.pins = {PinDesc{.name = "In", .type = PinTypeId{"float"}, .dir = PinDir::Input},
                      PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
            g.body = {PinRowCell{.input = 0, .output = 1},
                      GroupCell{.title = "Preview",
                                .defaultOpen = true,
                                .resizable = true,
                                .children = {ImageCell{.aspect = 2.f}, LabelCell{.text = "note"}}}};
            grouped = reg.Register("grouped", std::move(g));
        }

        void Solve() { layout.Rebuild(model, reg, theme); }
    };
} // namespace

TEST(GraphLayoutTests, ANodeIsAsWideAsItsTypeMinimumUnlessOverridden)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{100.f, 50.f});
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(Near(g->rect.x, 100.f));
    EXPECT_TRUE(Near(g->rect.y, 50.f));
    EXPECT_TRUE(Near(g->rect.width, 160.f));

    f.model.Get(a)->width = 240.f;
    f.Solve();
    EXPECT_TRUE(Near(f.layout.Of(a)->rect.width, 240.f));
}

TEST(GraphLayoutTests, HeightIsTheHeaderPlusItsVisibleCells)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{});
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(Near(g->header.height, f.theme.HeaderHeight));
    EXPECT_TRUE(Near(g->rect.height, f.theme.HeaderHeight + f.theme.CellHeight * 2.f));
    EXPECT_EQ(g->cells.size(), 2u);
    EXPECT_TRUE(Near(g->cells[0].rect.y, g->rect.y + f.theme.HeaderHeight));
    EXPECT_TRUE(Near(g->cells[1].rect.y, g->rect.y + f.theme.HeaderHeight + f.theme.CellHeight));
}

TEST(GraphLayoutTests, PinsSitOnTheEdgesAtTheirRowCentre)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->pins.size(), 3u);

    Vec2 in0{};
    EXPECT_TRUE(f.layout.PinPos(a, 0, PinDir::Input, in0));
    EXPECT_TRUE(Near(in0.x, g->rect.x - f.theme.PinInset));
    EXPECT_TRUE(Near(in0.y, g->rect.y + f.theme.HeaderHeight + f.theme.CellHeight * 0.5f));

    Vec2 out{};
    EXPECT_TRUE(f.layout.PinPos(a, 2, PinDir::Output, out));
    EXPECT_TRUE(Near(out.x, g->rect.Right() + f.theme.PinInset));
    EXPECT_TRUE(Near(out.y, in0.y));

    Vec2 in1{};
    EXPECT_TRUE(f.layout.PinPos(a, 1, PinDir::Input, in1));
    EXPECT_TRUE(Near(in1.y, in0.y + f.theme.CellHeight));
}

TEST(GraphLayoutTests, AnUnclaimedPinStacksBelowTheBody)
{
    Fixture f;
    NodeTypeDesc d;
    d.title = "Loose";
    d.minWidth = 100.f;
    d.pins = {PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
              PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output}};
    const NodeTypeId loose = f.reg.Register("loose", std::move(d));

    const GraphNodeId a = f.model.Add(loose, Vec2{});
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->pins.size(), 2u);
    EXPECT_GT(g->rect.height, f.theme.HeaderHeight);

    Vec2 in{};
    Vec2 out{};
    EXPECT_TRUE(f.layout.PinPos(a, 0, PinDir::Input, in));
    EXPECT_TRUE(f.layout.PinPos(a, 1, PinDir::Output, out));
    EXPECT_GT(in.y, g->rect.y + f.theme.HeaderHeight);
    EXPECT_TRUE(Near(in.y, out.y));
}

TEST(GraphLayoutTests, AnOpenGroupContributesItsCaretAndItsChildren)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.grouped, Vec2{});
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    const float inner = g->rect.width - f.theme.CellPadding * 2.f;
    const float expected = f.theme.HeaderHeight
                         + f.theme.CellHeight
                         + f.theme.CellHeight
                         + inner / 2.f
                         + f.theme.CellHeight;
    EXPECT_TRUE(Near(g->rect.height, expected));
}

TEST(GraphLayoutTests, AClosedGroupKeepsItsCaretAndDropsItsChildren)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.grouped, Vec2{});
    f.model.Get(a)->openGroups = 0u;
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(Near(g->rect.height, f.theme.HeaderHeight + f.theme.CellHeight * 2.f));

    bool sawImage = false;
    for (const CellGeom& c : g->cells)
    {
        if (std::holds_alternative<ImageCell>(*c.cell))
        {
            sawImage = true;
        }
    }
    EXPECT_FALSE(sawImage);
}

TEST(GraphLayoutTests, AResizedGroupUsesItsStoredHeight)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.grouped, Vec2{});
    f.model.Get(a)->groupHeight[0] = 300.f;
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(Near(g->rect.height, f.theme.HeaderHeight + f.theme.CellHeight * 2.f + 300.f));
}

TEST(GraphLayoutTests, ACollapsedNodeIsHeaderOnlyAndStacksItsPinsOnTheHeader)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.model.Get(a)->collapsed = true;
    f.Solve();

    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);
    EXPECT_TRUE(Near(g->rect.height, f.theme.HeaderHeight));
    EXPECT_TRUE(g->cells.empty());
    EXPECT_EQ(g->pins.size(), 3u);

    Vec2 in0{}, in1{}, out{};
    EXPECT_TRUE(f.layout.PinPos(a, 0, PinDir::Input, in0));
    EXPECT_TRUE(f.layout.PinPos(a, 1, PinDir::Input, in1));
    EXPECT_TRUE(f.layout.PinPos(a, 2, PinDir::Output, out));
    EXPECT_TRUE(Near(in0.x, g->rect.x - f.theme.PinInset));
    EXPECT_TRUE(Near(out.x, g->rect.Right() + f.theme.PinInset));
    EXPECT_LT(in0.y, in1.y);
    EXPECT_LE(in1.y, g->rect.Bottom());
}

TEST(GraphLayoutTests, BoundsIsTheUnionOfEveryNodeAndItsPins)
{
    Fixture f;
    f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.model.Add(f.plain, Vec2{500.f, 300.f});
    f.Solve();

    const Rect b = f.layout.Bounds();
    EXPECT_LE(b.x, -f.theme.PinInset);
    EXPECT_GE(b.Right(), 500.f + 160.f + f.theme.PinInset);
    EXPECT_GE(b.Bottom(), 300.f);
}

TEST(GraphLayoutTests, VisibleCullsNodesOutsideTheWindow)
{
    Fixture f;
    const GraphNodeId inside = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.model.Add(f.plain, Vec2{5000.f, 5000.f});
    f.Solve();

    std::vector<GraphNodeId> out;
    f.layout.Visible(f.model, Rect{-50.f, -50.f, 400.f, 400.f}, out);
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], inside);
}

TEST(GraphLayoutTests, ProbeFindsAPinBeforeTheNodeUnderIt)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.Solve();

    Vec2 pin{};
    EXPECT_TRUE(f.layout.PinPos(a, 0, PinDir::Input, pin));

    const Layout::Hit hit = f.layout.Probe(f.model, f.theme, pin, 1.f);
    EXPECT_EQ(hit.what, Layout::Hit::What::Pin);
    EXPECT_EQ(hit.node, a);
    EXPECT_EQ(hit.pinSlot, 0);
    EXPECT_EQ(hit.pinDir, PinDir::Input);
}

TEST(GraphLayoutTests, ProbeFindsTheHeaderAndTheCellsSeparately)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.Solve();
    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);

    const Layout::Hit head =
        f.layout.Probe(f.model, f.theme, Vec2{g->rect.Center().x, g->rect.y + 4.f}, 1.f);
    EXPECT_EQ(head.what, Layout::Hit::What::Header);
    EXPECT_EQ(head.node, a);

    const Layout::Hit cell = f.layout.Probe(
        f.model, f.theme, Vec2{g->rect.Center().x, g->rect.y + f.theme.HeaderHeight + 2.f}, 1.f);
    EXPECT_EQ(cell.what, Layout::Hit::What::Cell);
    EXPECT_EQ(cell.cellIndex, 0);
}

TEST(GraphLayoutTests, ProbeFindsTheGroupCaretAndItsResizeEdge)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.grouped, Vec2{0.f, 0.f});
    f.Solve();
    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);

    const float caretY =
        g->rect.y + f.theme.HeaderHeight + f.theme.CellHeight + f.theme.CellHeight * 0.5f;
    const Layout::Hit caret =
        f.layout.Probe(f.model, f.theme, Vec2{g->rect.x + f.theme.CellPadding + 2.f, caretY}, 1.f);
    EXPECT_EQ(caret.what, Layout::Hit::What::GroupCaret);
    EXPECT_EQ(caret.groupIndex, 0);

    const Layout::Hit edge = f.layout.Probe(
        f.model, f.theme, Vec2{g->rect.Center().x, g->rect.Bottom() - 1.f}, 1.f);
    EXPECT_EQ(edge.what, Layout::Hit::What::GroupResize);
    EXPECT_EQ(edge.groupIndex, 0);
}

TEST(GraphLayoutTests, ProbeFindsTheNodeResizeEdge)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.Solve();
    const NodeGeom* g = f.layout.Of(a);
    ASSERT_NE(g, nullptr);

    const Layout::Hit hit = f.layout.Probe(
        f.model, f.theme, Vec2{g->rect.Right() - 1.f, g->rect.y + f.theme.HeaderHeight + 4.f}, 1.f);
    EXPECT_EQ(hit.what, Layout::Hit::What::NodeResize);
    EXPECT_EQ(hit.node, a);
}

TEST(GraphLayoutTests, ProbePrefersTheFrontmostNodeWhenTwoOverlap)
{
    Fixture f;
    const GraphNodeId back = f.model.Add(f.plain, Vec2{0.f, 0.f});
    const GraphNodeId front = f.model.Add(f.plain, Vec2{10.f, 10.f});
    f.Solve();

    const Layout::Hit hit = f.layout.Probe(f.model, f.theme, Vec2{60.f, 20.f}, 1.f);
    EXPECT_EQ(hit.node, front);
    EXPECT_FALSE(hit.node == back);
}

TEST(GraphLayoutTests, ProbeFindsALinkOnlyWhenNothingElseIsUnderThePoint)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    const GraphNodeId b = f.model.Add(f.plain, Vec2{400.f, 0.f});
    const LinkId l = f.model.Connect(PinRef{a, 2, PinDir::Output}, PinRef{b, 0, PinDir::Input});
    f.Solve();

    Vec2 from{}, to{};
    EXPECT_TRUE(f.layout.PinPos(a, 2, PinDir::Output, from));
    EXPECT_TRUE(f.layout.PinPos(b, 0, PinDir::Input, to));

    const Vec2 mid{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f};
    const Layout::Hit hit = f.layout.Probe(f.model, f.theme, mid, 1.f);
    EXPECT_EQ(hit.what, Layout::Hit::What::Link);
    EXPECT_EQ(hit.link, l);

    const Layout::Hit miss = f.layout.Probe(f.model, f.theme, Vec2{mid.x, mid.y + 200.f}, 1.f);
    EXPECT_EQ(miss.what, Layout::Hit::What::None);
}

TEST(GraphLayoutTests, ProbeToleranceShrinksInCanvasUnitsAsZoomGrows)
{
    Fixture f;
    const GraphNodeId a = f.model.Add(f.plain, Vec2{0.f, 0.f});
    f.Solve();

    Vec2 pin{};
    EXPECT_TRUE(f.layout.PinPos(a, 0, PinDir::Input, pin));
    const Vec2 justOutside{pin.x - (f.theme.PinRadius + 6.f), pin.y};

    EXPECT_EQ(f.layout.Probe(f.model, f.theme, justOutside, 1.f).what, Layout::Hit::What::Pin);
    EXPECT_NE(f.layout.Probe(f.model, f.theme, justOutside, 4.f).what, Layout::Hit::What::Pin);
}

TEST(GraphLayoutTests, LinkPathBowsHorizontallyAndIsClamped)
{
    Vec2 c0{}, c1{};
    Layout::LinkPath(Vec2{0.f, 0.f}, Vec2{10.f, 0.f}, c0, c1);
    EXPECT_TRUE(Near(c0.x, 24.f));
    EXPECT_TRUE(Near(c1.x, 10.f - 24.f));
    EXPECT_TRUE(Near(c0.y, 0.f));

    Layout::LinkPath(Vec2{0.f, 0.f}, Vec2{2000.f, 0.f}, c0, c1);
    EXPECT_TRUE(Near(c0.x, 160.f));
    EXPECT_TRUE(Near(c1.x, 2000.f - 160.f));
}

TEST(GraphLayoutTests, DistanceToLinkIsZeroOnTheEndpointsAndLargeFarAway)
{
    const Vec2 from{0.f, 0.f};
    const Vec2 to{200.f, 100.f};
    EXPECT_LT(Layout::DistanceToLink(from, to, from), 1.f);
    EXPECT_LT(Layout::DistanceToLink(from, to, to), 1.f);
    EXPECT_GT(Layout::DistanceToLink(from, to, Vec2{100.f, 900.f}), 500.f);
}
