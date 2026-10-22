#include <gtest/gtest.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Style/Properties.h"
#include "Style/StyleEngine.h"

#include <string>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace
{
    struct StyleHarness
    {
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        GraphScope scope{*graph};
        NodeArena arena;
        style::StyleEngine engine;
        AppCtx ctx{arena, *graph};

        StyleHarness() { ctx.style = &engine; }

        NodeId Mount(const Elem& tree, Owner& owner)
        {
            return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
        }

        [[nodiscard]] const LiveNode& Node(NodeId id) { return arena.Resolve(id); }
        [[nodiscard]] const StyleState& Style(NodeId id) { return arena.PayloadOf<ContainerPayload>(id).style; }
    };
} // namespace

TEST(StyleApplyTests, BasicApply)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".card { background-color: #ff0000; border-radius: 8px; width: 200px; "
                        "padding: 10px; opacity: 0.5; }");
    const NodeId root = h.Mount(Box({.className = "card"}), owner);

    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
    EXPECT_FLOAT_EQ(h.Style(root).radius, 8.f);
    EXPECT_FLOAT_EQ(h.Style(root).opacity, 0.5f);
    EXPECT_EQ(h.Node(root).style.width, Fixed(200));
    EXPECT_EQ(h.Node(root).style.padding, Spacing(10.f));
}

TEST(StyleApplyTests, InlineOrigin0)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".card { background-color: #00ff00; }");
    const NodeId root = h.Mount(
        Box({.layout = {.width = Fixed(50)}, .style = {.fill = Color::Blue, .radius = 4.f}, .className = "card"}),
        owner);

    EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
    EXPECT_FLOAT_EQ(h.Style(root).radius, 4.f);
    EXPECT_EQ(h.Node(root).style.width, Fixed(50));
}

TEST(StyleApplyTests, InlineFlexItemOrigin0)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #00ff00; } .one { flex: 1; }");
    const NodeId root = h.Mount(
        Box({.layout = {.wrap = FlexWrap::Wrap, .flexWeight = 2.f, .order = 3}, .className = cls}), owner);

    EXPECT_FLOAT_EQ(h.Node(root).style.flexWeight, 2.f);
    EXPECT_EQ(h.Node(root).style.wrap, FlexWrap::Wrap);
    EXPECT_EQ(h.Node(root).style.order, 3);
    cls = std::string("one");
    EXPECT_FLOAT_EQ(h.Node(root).style.flexWeight, 1.f);
    cls = std::string("card");
    EXPECT_FLOAT_EQ(h.Node(root).style.flexWeight, 2.f);
}

TEST(StyleApplyTests, RevertOnClassRemoval)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #0000ff; }");
    const NodeId root = h.Mount(Box({.style = {.fill = Color::Red}, .className = cls}), owner);

    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 255));
    cls = std::string("");
    EXPECT_EQ(h.Style(root).fill, Color::Red);
}

TEST(StyleApplyTests, HotReload)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".card { background-color: #ff0000; }");
    const NodeId root = h.Mount(Box({.className = "card"}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));

    h.engine.LoadSource(".card { background-color: #00ff00; }");
    EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
}

TEST(StyleApplyTests, ClassNameToggleReResolves)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("card");
    h.engine.LoadSource(".card { background-color: #ff0000; } .card.active { background-color: #00ff00; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));

    cls = std::string("card active");
    EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
}

TEST(StyleApplyTests, DescendantSelector)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".panel .item { background-color: #ff0000; }");
    const NodeId root = h.Mount(Box({.className = "panel"}, Box({.className = "item"})), owner);
    const NodeId child = h.Node(root).firstChild;

    EXPECT_EQ(h.Style(child).fill, Color(255, 0, 0));
    EXPECT_EQ(h.Style(root).fill, Color::Transparent);
}

TEST(StyleApplyTests, AncestorClassToggleReResolvesDescendant)
{
    StyleHarness h;
    Owner owner;
    auto theme = Signal<std::string>("");
    h.engine.LoadSource(".dark .item { background-color: #123456; }");
    const NodeId root = h.Mount(Box({.className = theme}, Box({.className = "item"})), owner);
    const NodeId child = h.Node(root).firstChild;

    EXPECT_EQ(h.Style(child).fill, Color::Transparent);
    theme = std::string("dark");
    EXPECT_EQ(h.Style(child).fill, Color(0x12, 0x34, 0x56));
}

TEST(StyleApplyTests, PercentAndMinMax)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".p { width: 50%; padding: 10%; min-width: 100px; max-width: 300px; gap: 5%; }");
    const NodeId root = h.Mount(Box({.className = "p"}), owner);
    const LiveNode& n = h.Node(root);
    EXPECT_EQ(n.style.width, Percent(50));
    EXPECT_EQ(n.style.paddingPct, Spacing(10.f));
    EXPECT_EQ(n.style.padding, Spacing(0.f));
    EXPECT_EQ(n.style.minWidth, Fixed(100));
    EXPECT_EQ(n.style.maxWidth, Fixed(300));
    EXPECT_FLOAT_EQ(n.style.gapPct, 5.f);
}

TEST(StyleApplyTests, VisibilityAndTransform)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".hidden { display: none; } .moved { transform: translateX(10px) translateY(5px); }");
    const NodeId a = h.Mount(Box({.className = "hidden"}), owner);
    Owner owner2;
    const NodeId b = h.Mount(Box({.className = "moved"}), owner2);
    EXPECT_FALSE(h.arena.PayloadOf<ContainerPayload>(a).view.visible);
    EXPECT_FLOAT_EQ(h.arena.PayloadOf<ContainerPayload>(b).transform.x, 10.f);
    EXPECT_FLOAT_EQ(h.arena.PayloadOf<ContainerPayload>(b).transform.y, 5.f);
}

TEST(StyleApplyTests, TextStyling)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".t { color: #ff0000; font-size: 20px; text-align: center; font-weight: bold; "
                        "font-style: italic; }");
    const NodeId root = h.Mount(Text("hi", {.className = "t"}), owner);
    const TextPayload& tp = h.arena.PayloadOf<TextPayload>(root);
    EXPECT_EQ(tp.color, Color(255, 0, 0));
    EXPECT_FLOAT_EQ(tp.size, 20.f);
    EXPECT_EQ(tp.flow.align, Align::Center);
    EXPECT_TRUE(tp.font.bold);
    EXPECT_TRUE(tp.font.italic);
}

TEST(StyleApplyTests, ButtonStyling)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".b { background-color: #00ff00; color: #000000; border-radius: 12px; }");
    const NodeId root = h.Mount(Button({.label = "Go", .className = "b"}), owner);
    const ButtonPayload& bp = h.arena.PayloadOf<ButtonPayload>(root);
    EXPECT_EQ(bp.fill, Color(0, 255, 0));
    EXPECT_EQ(bp.color, Color(0, 0, 0));
    EXPECT_FLOAT_EQ(bp.radius, 12.f);
}

TEST(StyleApplyTests, GridStyling)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".g { background-color: #ff0000; gap: 8px; }");
    const NodeId root = h.Mount(Grid({.className = "g"}), owner);
    EXPECT_EQ(h.arena.PayloadOf<GridPayload>(root).style.fill, Color(255, 0, 0));
    EXPECT_FLOAT_EQ(h.Node(root).style.gap, 8.f);
}

TEST(StyleApplyTests, FlexDirectionFlip)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("r");
    h.engine.LoadSource(".r { flex-direction: column; }");
    const NodeId root = h.Mount(Row({.className = cls}), owner);
    EXPECT_EQ(h.Node(root).style.axis, FlexAxis::Column);
    cls = std::string("");
    EXPECT_EQ(h.Node(root).style.axis, FlexAxis::Row);
}

TEST(StyleApplyTests, DisplayModes)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("b");
    h.engine.LoadSource(".b { display: block; } .f { display: flex; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_EQ(h.Node(root).style.display, DisplayMode::Block);
    cls = std::string("f");
    EXPECT_EQ(h.Node(root).style.display, DisplayMode::Flex);
    cls = std::string("");
    EXPECT_EQ(h.Node(root).style.display, DisplayMode::Flex);
}

TEST(StyleApplyTests, FlexItemProps)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".x { flex-grow: 2; flex-shrink: 1; flex-basis: 40px; order: 3; "
                        "flex-wrap: wrap; align-self: center; align-content: space-between; }");
    const NodeId root = h.Mount(Box({.className = "x"}), owner);
    const LiveNode& n = h.Node(root);
    EXPECT_FLOAT_EQ(n.style.flexWeight, 2.f);
    EXPECT_FLOAT_EQ(n.style.flexShrink, 1.f);
    EXPECT_EQ(n.style.flexBasis, Fixed(40));
    EXPECT_EQ(n.style.order, 3);
    EXPECT_EQ(n.style.wrap, FlexWrap::Wrap);
    EXPECT_EQ(n.style.alignSelf, Align::Center);
    EXPECT_EQ(n.style.alignContent, Align::Between);
}

TEST(StyleApplyTests, FlexShorthandApply)
{
    StyleHarness h;
    Owner owner;
    auto cls = Signal<std::string>("one");
    h.engine.LoadSource(".one { flex: 1; } .none { flex: none; }");
    const NodeId root = h.Mount(Box({.className = cls}), owner);
    EXPECT_FLOAT_EQ(h.Node(root).style.flexWeight, 1.f);
    EXPECT_FLOAT_EQ(h.Node(root).style.flexShrink, 1.f);
    EXPECT_EQ(h.Node(root).style.flexBasis, Fixed(0));
    cls = std::string("none");
    EXPECT_FLOAT_EQ(h.Node(root).style.flexWeight, 0.f);
    EXPECT_FLOAT_EQ(h.Node(root).style.flexShrink, 0.f);
    EXPECT_EQ(h.Node(root).style.flexBasis, Wrap);
}

TEST(StyleApplyTests, PositionAndInset)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".abs { position: absolute; top: 5px; right: 10px; z-index: 7; } "
                        ".rel { position: relative; }");
    const NodeId a = h.Mount(Box({.className = "abs"}), owner);
    const LiveNode& n = h.Node(a);
    EXPECT_EQ(n.style.position, Position::Absolute);
    EXPECT_FLOAT_EQ(n.style.inset.top, 5.f);
    EXPECT_FLOAT_EQ(n.style.inset.right, 10.f);
    EXPECT_TRUE(Inset::IsAuto(n.style.inset.left));
    EXPECT_EQ(n.style.z, 7);

    Owner owner2;
    const NodeId r = h.Mount(Box({.className = "rel"}), owner2);
    EXPECT_EQ(h.Node(r).style.position, Position::Flow);
    EXPECT_TRUE(h.Node(r).style.anchor);
}

TEST(StyleApplyTests, GridTemplateAndPlacement)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".g { display: grid; grid-template-columns: 1fr 2fr 100px; gap: 6px 8px; } "
                        ".c { grid-column: 2 / 4; grid-row: span 2; }");
    const NodeId g = h.Mount(Grid({.className = "g"}), owner);
    const GridPayload& gp = h.arena.PayloadOf<GridPayload>(g);
    EXPECT_EQ(gp.columns.size(), 3u);
    EXPECT_EQ(gp.columns[0], Fr(1));
    EXPECT_EQ(gp.columns[1], Fr(2));
    EXPECT_EQ(gp.columns[2], Px(100));
    EXPECT_FLOAT_EQ(gp.rowGap, 6.f);
    EXPECT_FLOAT_EQ(gp.colGap, 8.f);

    Owner owner2;
    const NodeId c = h.Mount(Cell({.className = "c"}), owner2);
    const GridItemPlacement& p = h.Node(c).style.placement;
    EXPECT_EQ(p.col, 1);
    EXPECT_EQ(p.colSpan, 2);
    EXPECT_EQ(p.row, -1);
    EXPECT_EQ(p.rowSpan, 2);
}

TEST(StyleApplyTests, AspectRatioResolvesAndPicksItsAxis)
{
    StyleHarness h;
    h.engine.LoadSource(".wide { aspect-ratio: 16 / 9; width: 320px; }"
                        ".tall { aspect-ratio: 2; height: 100px; }"
                        ".both { aspect-ratio: 1; width: 50px; height: 70px; }"
                        ".off  { aspect-ratio: auto; width: 50px; }");

    Owner owner;
    const NodeId wide = h.Mount(Box({.className = "wide"}), owner);
    EXPECT_GT(h.Node(wide).style.aspectRatio, 1.77f);
    EXPECT_LT(h.Node(wide).style.aspectRatio, 1.78f);
    EXPECT_EQ(h.Node(wide).style.aspectPin, AspectPin::Height);
    EXPECT_FALSE(h.arena.AspectNodes().empty());

    const NodeId tall = h.Mount(Box({.className = "tall"}), owner);
    EXPECT_FLOAT_EQ(h.Node(tall).style.aspectRatio, 2.f);
    EXPECT_EQ(h.Node(tall).style.aspectPin, AspectPin::Width);

    const NodeId both = h.Mount(Box({.className = "both"}), owner);
    EXPECT_FLOAT_EQ(h.Node(both).style.aspectRatio, 1.f);
    EXPECT_EQ(h.Node(both).style.aspectPin, AspectPin::None);

    const NodeId off = h.Mount(Box({.className = "off"}), owner);
    EXPECT_FLOAT_EQ(h.Node(off).style.aspectRatio, 0.f);
}

TEST(StyleApplyTests, DashedBorderResolves)
{
    StyleHarness h;
    h.engine.LoadSource(".dash  { border: 2px dashed #808080; }"
                        ".solid { border: 2px solid #808080; }"
                        ".late  { border-width: 1px; border-style: dashed; }");

    Owner owner;
    const NodeId dash = h.Mount(Box({.className = "dash"}), owner);
    EXPECT_TRUE(h.Style(dash).border.dashed);
    EXPECT_FLOAT_EQ(h.Style(dash).border.width, 2.f);
    EXPECT_FLOAT_EQ(h.Style(dash).border.DashLength(), 6.f);

    const NodeId solid = h.Mount(Box({.className = "solid"}), owner);
    EXPECT_FALSE(h.Style(solid).border.dashed);
    EXPECT_FLOAT_EQ(h.Style(solid).border.DashLength(), 0.f);

    const NodeId late = h.Mount(Box({.className = "late"}), owner);
    EXPECT_TRUE(h.Style(late).border.dashed);
}

TEST(StyleApplyTests, ConsultedPropertyTableCoversWhatTheResolverReads)
{
    EXPECT_TRUE(style::IsConsultedProperty("width"));
    EXPECT_TRUE(style::IsConsultedProperty("aspect-ratio"));
    EXPECT_TRUE(style::IsConsultedProperty("border-style"));
    EXPECT_TRUE(style::IsConsultedProperty("grid-template-columns"));
    EXPECT_TRUE(style::IsConsultedProperty("-webkit-line-clamp"));
    EXPECT_TRUE(style::IsConsultedProperty("border-left"));
    EXPECT_TRUE(style::IsConsultedProperty("border-top-width"));
    EXPECT_TRUE(style::IsConsultedProperty("border-right-color"));
    EXPECT_TRUE(style::IsConsultedProperty("border-bottom-style"));
    EXPECT_FALSE(style::IsConsultedProperty("box-shadow"));
    EXPECT_FALSE(style::IsConsultedProperty("backgruond-color"));
}

TEST(StyleApplyTests, DirectionalBordersResolveShorthandsAndLonghands)
{
    StyleHarness h;
    h.engine.LoadSource(".side { border: 1px solid #111111; border-top: 4px dashed #ff0000; border-bottom-width: 8px; }"
                        ".split-w { border-width: 1px 2px 3px 4px; }"
                        ".split-c { border-color: #ff0000 #00ff00 #0000ff #ffff00; }");

    Owner owner;
    const NodeId side = h.Mount(Box({.className = "side"}), owner);
    const Border bSide = h.Style(side).border;
    EXPECT_FALSE(bSide.IsUniform());
    EXPECT_TRUE(bSide.HasBorder());
    EXPECT_FLOAT_EQ(bSide.WidthTop(), 4.f);
    EXPECT_TRUE(bSide.DashedTop());
    EXPECT_EQ(bSide.ColorTop(), Color::Rgba(255, 0, 0));
    EXPECT_FLOAT_EQ(bSide.WidthRight(), 1.f);
    EXPECT_FALSE(bSide.DashedRight());
    EXPECT_EQ(bSide.ColorRight(), Color::Rgba(17, 17, 17));
    EXPECT_FLOAT_EQ(bSide.WidthBottom(), 8.f);
    EXPECT_FLOAT_EQ(bSide.WidthLeft(), 1.f);

    const NodeId splitW = h.Mount(Box({.className = "split-w"}), owner);
    const Border bSplitW = h.Style(splitW).border;
    EXPECT_FALSE(bSplitW.IsUniform());
    EXPECT_FLOAT_EQ(bSplitW.WidthTop(), 1.f);
    EXPECT_FLOAT_EQ(bSplitW.WidthRight(), 2.f);
    EXPECT_FLOAT_EQ(bSplitW.WidthBottom(), 3.f);
    EXPECT_FLOAT_EQ(bSplitW.WidthLeft(), 4.f);

    const NodeId splitC = h.Mount(Box({.className = "split-c"}), owner);
    const Border bSplitC = h.Style(splitC).border;
    EXPECT_FALSE(bSplitC.IsUniform());
    EXPECT_EQ(bSplitC.ColorTop(), Color::Rgba(255, 0, 0));
    EXPECT_EQ(bSplitC.ColorRight(), Color::Rgba(0, 255, 0));
    EXPECT_EQ(bSplitC.ColorBottom(), Color::Rgba(0, 0, 255));
    EXPECT_EQ(bSplitC.ColorLeft(), Color::Rgba(255, 255, 0));
}
