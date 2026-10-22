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

    struct ClassedTextProbe
    {
        Elem Render() { return Text("hi", {.className = std::string("t")}); }
    };

    struct BareTextProbe
    {
        Elem Render() { return Text("hi"); }
    };
} // namespace

TEST(StyleInheritVarsMediaTests, VarBasicAndFallback)
{
    {
        StyleHarness h;
        Owner owner;
        h.engine.LoadSource(":root { --brand: #ff0000; } .a { background-color: var(--brand); }");
        const NodeId root = h.Mount(Box({.className = "a"}), owner);
        EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
    }
    {
        StyleHarness h;
        Owner owner;
        h.engine.LoadSource(".a { background-color: var(--missing, #00ff00); }");
        const NodeId root = h.Mount(Box({.className = "a"}), owner);
        EXPECT_EQ(h.Style(root).fill, Color(0, 255, 0));
    }
}

TEST(StyleInheritVarsMediaTests, VarInheritedOverride)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(":root { --c: #ff0000; } .parent { --c: #00ff00; } "
                        ".parent .child { background-color: var(--c); }");
    const NodeId root = h.Mount(Box({.className = "parent"}, Box({.className = "child"})), owner);
    const NodeId child = h.Node(root).firstChild;
    EXPECT_EQ(h.Style(child).fill, Color(0, 255, 0));
}

TEST(StyleInheritVarsMediaTests, ColorInherits)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".p { color: #ff0000; }");
    const NodeId root = h.Mount(Box({.className = "p"}, Text("hi")), owner);
    const NodeId text = h.Node(root).firstChild;
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(text).color, Color(255, 0, 0));
}

TEST(StyleInheritVarsMediaTests, InheritKeyword)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".p { color: #ff0000; } .c { color: inherit; }");
    const NodeId root = h.Mount(Box({.className = "p"}, Text("hi", {.color = Color::Blue, .className = "c"})), owner);
    const NodeId text = h.Node(root).firstChild;
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(text).color, Color(255, 0, 0));
}

TEST(StyleInheritVarsMediaTests, MediaQueryReflows)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource("@media (max-width: 600px) { .a { background-color: #ff0000; } }");
    h.engine.SetViewport({500.f, 800.f});
    const NodeId root = h.Mount(Box({.className = "a"}), owner);
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
    h.engine.SetViewport({900.f, 800.f});
    EXPECT_EQ(h.Style(root).fill, Color::Transparent);
    h.engine.SetViewport({400.f, 800.f});
    EXPECT_EQ(h.Style(root).fill, Color(255, 0, 0));
}

TEST(StyleInheritVarsMediaTests, RemAndEmUnits)
{
    {
        StyleHarness h;
        Owner owner;
        h.engine.LoadSource(".x { width: 2rem; }");
        const NodeId root = h.Mount(Box({.className = "x"}), owner);
        EXPECT_EQ(h.Node(root).style.width, Fixed(32));
    }
    {
        StyleHarness h;
        Owner owner;
        h.engine.LoadSource(".parent { font-size: 20px; } .parent .child { width: 2em; }");
        const NodeId root = h.Mount(Box({.className = "parent"}, Box({.className = "child"})), owner);
        const NodeId child = h.Node(root).firstChild;
        EXPECT_EQ(h.Node(child).style.width, Fixed(40));
    }
}

TEST(StyleInheritVarsMediaTests, VarResolvesAcrossUnstyledAncestor)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(":root { --ink: #ff0000; } .t { color: var(--ink); }");
    const NodeId root = h.Mount(Box({.className = "shell"}, ClassedTextProbe{}), owner);
    const NodeId comp = h.Node(root).firstChild;
    const NodeId text = h.Node(comp).firstChild;
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(text).color, Color(255, 0, 0));
}

TEST(StyleInheritVarsMediaTests, ColorInheritsAcrossUnstyledAncestor)
{
    StyleHarness h;
    Owner owner;
    h.engine.LoadSource(".shell { color: #00ff00; }");
    const NodeId root = h.Mount(Box({.className = "shell"}, BareTextProbe{}), owner);
    const NodeId comp = h.Node(root).firstChild;
    const NodeId text = h.Node(comp).firstChild;
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(text).color, Color(0, 255, 0));
}

TEST(StyleInheritVarsMediaTests, VwUnit)
{
    StyleHarness h;
    Owner owner;
    h.engine.SetViewport({200.f, 100.f});
    h.engine.LoadSource(".x { width: 50vw; }");
    const NodeId root = h.Mount(Box({.className = "x"}), owner);
    EXPECT_EQ(h.Node(root).style.width, Fixed(100));
    h.engine.SetViewport({400.f, 100.f});
    EXPECT_EQ(h.Node(root).style.width, Fixed(200));
}
