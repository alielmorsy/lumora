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
        NodeId Mount(const Elem& tree, Owner& owner) { return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner); }
        [[nodiscard]] const LiveNode& Node(NodeId id) { return arena.Resolve(id); }
        [[nodiscard]] const StyleState& Style(NodeId id) { return arena.PayloadOf<ContainerPayload>(id).style; }
    };
} // namespace

TEST(StylePseudoTests, HoverApplies)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".btn { background-color: #ff0000; } .btn:hover { background-color: #00ff00; }");
    const NodeId root = h.Mount(Box({.className = "btn"}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
    h.engine.SetPseudo(root, style::Pseudo::Hover, true);
    EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
    h.engine.SetPseudo(root, style::Pseudo::Hover, false);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
}

TEST(StylePseudoTests, FocusAndActiveApply)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".x:focus { background-color: #0000ff; } .x:active { background-color: #00ff00; }");
    const NodeId root = h.Mount(Box({.className = "x"}), owner);
    EXPECT_EQ(h.Style(root).fill, Color::Transparent);
    h.engine.SetPseudo(root, style::Pseudo::Focus, true);
    EXPECT_EQ(h.Style(root).fill, Color(0, 0, 255));
    h.engine.SetPseudo(root, style::Pseudo::Focus, false);
    h.engine.SetPseudo(root, style::Pseudo::Active, true);
    EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
}

TEST(StylePseudoTests, AncestorHoverStylesDescendant)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".menu:hover .item { background-color: #ff0000; }");
    const NodeId root = h.Mount(Box({.className = "menu"}, Box({.className = "item"})), owner);
    const NodeId child = h.Node(root).firstChild;
    EXPECT_EQ(h.Style(child).fill, Color::Transparent);
    h.engine.SetPseudo(root, style::Pseudo::Hover, true);
    EXPECT_EQ(h.Style(child).fill, Color(255, 0, 0));
    h.engine.SetPseudo(root, style::Pseudo::Hover, false);
    EXPECT_EQ(h.Style(child).fill, Color::Transparent);
}

TEST(StylePseudoTests, DisabledCompanion)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".d:disabled { background-color: #ff0000; }");
    const NodeId root = h.Mount(Box({.on = {.disabled = true}, .className = "d"}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
}

TEST(StylePseudoTests, CheckedToggle)
{
    StyleHarness h;
    Owner owner;
    auto sig = Signal<bool>(false);
    h.engine.LoadSource(".c:checked { width: 50px; }");
    const NodeId root = h.Mount(Checkbox({.checked = sig, .className = "c"}), owner);
    EXPECT_EQ(h.Node(root).style.width, Wrap);
    sig = true;
    EXPECT_EQ(h.Node(root).style.width, Fixed(50));
    sig = false;
    EXPECT_EQ(h.Node(root).style.width, Wrap);
}
