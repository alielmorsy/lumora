#include <gtest/gtest.h>

#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Layout/Measure.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/Widget.h"

#include <lumora/Dock/Dock.h>

#include <string>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope         scope{*graph};
    NodeArena          arena;
    AppCtx             ctx{arena, *graph};

    NodeId Mount(const Elem& tree, Owner& owner) {
        return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
    }
};

TEST(WidgetTests, BoxGroupsBindAndChildLinks) {
    Harness h;
    Owner owner;

    Elem tree = Box({.layout = {.gap = 8.f}, .style = {.fill = Color::Blue, .radius = 6.f}},
                    Text("Hello", {.size = 24.f}));
    const NodeId root = h.Mount(tree, owner);

    EXPECT_EQ(h.arena.LiveCount(), 2);
    const LiveNode& box = h.arena.Resolve(root);
    EXPECT_EQ(box.kind, LiveKind::Box);
    EXPECT_EQ(box.childCount, 1);

    const ContainerPayload& bp = h.arena.PayloadOf<ContainerPayload>(root);
    EXPECT_EQ(box.style.gap, 8.f);
    EXPECT_EQ(bp.style.fill, Color::Blue);
    EXPECT_EQ(bp.style.radius, 6.f);

    const NodeId textId = box.firstChild;
    const LiveNode& text = h.arena.Resolve(textId);
    EXPECT_EQ(text.kind, LiveKind::Text);
    EXPECT_EQ(text.parent, root);
    const TextPayload& tp = h.arena.PayloadOf<TextPayload>(textId);
    EXPECT_EQ(tp.text, "Hello");
    EXPECT_EQ(tp.size, 24.f);
}

TEST(WidgetTests, DefaultGroupsLandDefaults) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Box(), owner);
    const LiveNode&         node = h.arena.Resolve(root);
    const ContainerPayload& bp   = h.arena.PayloadOf<ContainerPayload>(root);
    EXPECT_EQ(node.style.width, Wrap);
    EXPECT_EQ(node.style.gap, 0.f);
    EXPECT_EQ(bp.view.visible, true);
    EXPECT_EQ(bp.style.opacity, 1.f);
    EXPECT_FALSE(bp.on.onClick);
}

TEST(WidgetTests, ButtonOnClickPromotion) {
    Harness h;
    Owner owner;

    int clicks = 0;
    const NodeId root = h.Mount(Button({.label = "Go", .onClick = [&] { ++clicks; }}), owner);

    EXPECT_EQ(h.arena.Resolve(root).kind, LiveKind::Button);
    ButtonPayload& bp = h.arena.PayloadOf<ButtonPayload>(root);
    EXPECT_EQ(bp.label, "Go");
    EXPECT_TRUE(static_cast<bool>(bp.on.onClick));
    bp.on.onClick();
    EXPECT_EQ(clicks, 1);
}

TEST(WidgetTests, TextContentPatchesWithoutReinstantiation) {
    Harness h;
    Owner owner;

    auto name = Signal<std::string>("a");
    const NodeId root = h.Mount(Text(name), owner);

    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(root).text, "a");
    name = std::string("b");
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(root).text, "b");
    EXPECT_EQ(h.arena.LiveCount(), 1);
}

TEST(WidgetTests, SwitchReadsAndWritesBinding) {
    Harness h;
    Owner owner;

    auto on = Signal<bool>(false);
    const NodeId root = h.Mount(Switch({.on = on}), owner);

    EXPECT_EQ(h.arena.PayloadOf<SwitchPayload>(root).value, false);
    on = true;
    EXPECT_EQ(h.arena.PayloadOf<SwitchPayload>(root).value, true);

    h.arena.PayloadOf<SwitchPayload>(root).bind->Set(false);
    EXPECT_EQ(on.Peek(), false);
}

TEST(WidgetTests, ChildrenFromEachForm) {
    {
        Harness h;
        Owner owner;
        const NodeId root = h.Mount(Column({}, Text("a")), owner);
        EXPECT_EQ(h.arena.Resolve(root).childCount, 1);
    }
    {
        Harness h;
        Owner owner;
        const NodeId root = h.Mount(Column({}, Children{Text("a"), Text("b")}), owner);
        EXPECT_EQ(h.arena.Resolve(root).childCount, 2);
    }
    {
        Harness h;
        Owner owner;
        std::vector<Elem> kids;
        kids.push_back(Text("a"));
        kids.push_back(Text("b"));
        kids.push_back(Text("c"));
        const NodeId root = h.Mount(Column({}, kids), owner);
        EXPECT_EQ(h.arena.Resolve(root).childCount, 3);
    }
}

struct Badge {
    Str text;
    Elem Render() { return Text(text); }
};

TEST(WidgetTests, ClassComponentAsChildOfFunctionWidget) {
    Harness h;
    Owner owner;

    const NodeId root = h.Mount(Row({}, Badge{.text = "hi"}, Text("world")), owner);
    const LiveNode& row = h.arena.Resolve(root);
    EXPECT_EQ(row.kind, LiveKind::Row);
    EXPECT_EQ(row.childCount, 2);

    const NodeId compId = row.firstChild;
    EXPECT_EQ(h.arena.Resolve(compId).kind, LiveKind::Component);
    const NodeId badgeText = h.arena.Resolve(compId).firstChild;
    EXPECT_EQ(h.arena.Resolve(badgeText).kind, LiveKind::Text);
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(badgeText).text, "hi");

    const NodeId worldText = h.arena.Resolve(compId).nextSibling;
    EXPECT_EQ(h.arena.Resolve(worldText).kind, LiveKind::Text);
    EXPECT_EQ(h.arena.PayloadOf<TextPayload>(worldText).text, "world");
}

struct Inner {
    std::vector<std::string>* log = nullptr;
    Elem Render() { return Text("inner"); }
    void OnMount() { log->push_back("inner-mount"); }
    void OnUnmount() { log->push_back("inner-unmount"); }
};

struct Outer {
    std::vector<std::string>* log = nullptr;
    Elem Render() { return Column({}, Inner{.log = log}); }
    void OnMount() { log->push_back("outer-mount"); }
    void OnUnmount() { log->push_back("outer-unmount"); }
};

TEST(WidgetTests, LifecycleOrdering) {
    Harness h;
    std::vector<std::string> log;
    {
        Owner owner;
        h.Mount(Box({}, Outer{.log = &log}), owner);
        EXPECT_EQ(log, (std::vector<std::string>{"inner-mount", "outer-mount"}));
    }
    EXPECT_EQ(log, (std::vector<std::string>{"inner-mount", "outer-mount", "outer-unmount", "inner-unmount"}));
}

void CollectText(NodeArena& arena, NodeId id, std::vector<std::string>& out) {
    if (!id.Valid()) return;
    const LiveNode& n = arena.Resolve(id);
    if (n.kind == LiveKind::Text) out.push_back(arena.PayloadOf<TextPayload>(id).text);
    for (NodeId c = n.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling) CollectText(arena, c, out);
}

void CollectClickable(NodeArena& arena, NodeId id, std::vector<NodeId>& out) {
    if (!id.Valid()) return;
    const LiveNode& n = arena.Resolve(id);
    if (n.kind == LiveKind::Box || n.kind == LiveKind::Row || n.kind == LiveKind::Column) {
        if (arena.PayloadOf<ContainerPayload>(id).on.onClick) out.push_back(id);
    }
    for (NodeId c = n.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling) CollectClickable(arena, c, out);
}

TEST(WidgetTests, ChipDotAndRemoveAreOptionalParts) {
    Harness h;
    Owner owner;

    const NodeId bare = h.Mount(Chip({.label = "Sleeping"}), owner);
    EXPECT_EQ(h.arena.Resolve(bare).childCount, 1);

    int removed = 0;
    const NodeId full = h.Mount(Chip({.label = "Sleeping",
                                      .dotColor = Color::Blue,
                                      .onRemove = [&removed] { ++removed; }}),
                                owner);
    EXPECT_EQ(h.arena.Resolve(full).childCount, 3);

    const NodeId removeBox = h.arena.Resolve(full).lastChild;
    const ContainerPayload& rp = h.arena.PayloadOf<ContainerPayload>(removeBox);
    EXPECT_TRUE(static_cast<bool>(rp.on.onClick));
    rp.on.onClick();
    EXPECT_EQ(removed, 1);
}

TEST(WidgetTests, BreadcrumbCollapsesFromTheLeftAndKeepsTheLast) {
    Harness h;
    Owner owner;

    const std::vector<std::string> path{"Content", "Scenes", "Level1", "Props", "Rocks", "Big"};

    const NodeId all = h.Mount(Breadcrumb({.path = [&path] { return path; }, .maxVisible = 0}), owner);
    std::vector<std::string> shown;
    CollectText(h.arena, all, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content", "/", "Scenes", "/", "Level1", "/", "Props", "/",
                                              "Rocks", "/", "Big"}));

    const NodeId few = h.Mount(Breadcrumb({.path = [&path] { return path; }, .maxVisible = 4}), owner);
    shown.clear();
    CollectText(h.arena, few, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content", "/", "\xE2\x80\xA6", "/", "Rocks", "/", "Big"}));

    const NodeId two = h.Mount(Breadcrumb({.path = [&path] { return path; }, .maxVisible = 1}), owner);
    shown.clear();
    CollectText(h.arena, two, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content", "/", "\xE2\x80\xA6", "/", "Big"}));
}

TEST(WidgetTests, BreadcrumbFollowsItsPathWhenItChanges) {
    Harness h;
    Owner owner;

    Signal<std::string> folder{""};
    const NodeId root = h.Mount(
        Breadcrumb({.path = [folder]
                    {
                        std::vector<std::string> out{"Content"};
                        if (!folder().empty()) out.push_back(folder());
                        return out;
                    },
                    .maxVisible = 0}),
        owner);

    std::vector<std::string> shown;
    CollectText(h.arena, root, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content"}));

    folder = std::string("Scenes");
    shown.clear();
    CollectText(h.arena, root, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content", "/", "Scenes"}));

    folder = std::string("Materials");
    shown.clear();
    CollectText(h.arena, root, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content", "/", "Materials"}));

    folder = std::string("");
    shown.clear();
    CollectText(h.arena, root, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"Content"}));
}

TEST(WidgetTests, BreadcrumbReportsTheIndexPressed) {
    Harness h;
    Owner owner;

    const std::vector<std::string> path{"Content", "Scenes", "Level1"};
    int                            picked = -1;

    const NodeId root = h.Mount(Breadcrumb({.path     = [&path] { return path; },
                                            .onSelect = [&picked](int index) { picked = index; },
                                            .maxVisible = 0}),
                                owner);

    std::vector<NodeId> clickable;
    CollectClickable(h.arena, root, clickable);
    ASSERT_EQ(clickable.size(), 3);
    h.arena.PayloadOf<ContainerPayload>(clickable[1]).on.onClick();
    EXPECT_EQ(picked, 1);
    h.arena.PayloadOf<ContainerPayload>(clickable[2]).on.onClick();
    EXPECT_EQ(picked, 2);
}

TEST(WidgetTests, BadgeResolvesItsVariantPalette) {
    Harness h;
    Owner owner;

    const NodeId neutral = h.Mount(lumora::Badge({.label = "cooked"}), owner);
    const NodeId warn    = h.Mount(lumora::Badge({.label = "stale", .variant = BadgeVariant::Warn}), owner);
    const Color  nFill   = h.arena.PayloadOf<ContainerPayload>(neutral).style.fill;
    const Color  wFill   = h.arena.PayloadOf<ContainerPayload>(warn).style.fill;
    EXPECT_FALSE(nFill == wFill);
    EXPECT_NE(nFill.a, 0);

    const NodeId forced = h.Mount(lumora::Badge({.label = "x", .variant = BadgeVariant::Warn, .fill = Color::Blue}),
                                  owner);
    EXPECT_EQ(h.arena.PayloadOf<ContainerPayload>(forced).style.fill, Color::Blue);

    std::vector<std::string> shown;
    CollectText(h.arena, neutral, shown);
    EXPECT_EQ(shown, (std::vector<std::string>{"cooked"}));
}

TEST(WidgetTests, SwitchAndCheckboxContentSized) {
    Harness h;
    Owner owner;

    auto swSignal = Signal<bool>(false);
    auto cbSignal = Signal<bool>(false);
    const NodeId swNode = h.Mount(Switch({.on = swSignal}), owner);
    const NodeId cbNode = h.Mount(Checkbox({.checked = cbSignal, .label = "Test"}), owner);

    Widget* swWidget = KindRegistry::For(LiveKind::Switch);
    ASSERT_NE(swWidget, nullptr);
    EXPECT_TRUE(swWidget->ContentSized());

    Widget* cbWidget = KindRegistry::For(LiveKind::Checkbox);
    ASSERT_NE(cbWidget, nullptr);
    EXPECT_TRUE(cbWidget->ContentSized());

    MeasureContext mctx{nullptr, &h.arena, nullptr, nullptr};
    const Vec2 swSize = swWidget->Measure(mctx, swNode, {}, {});
    EXPECT_EQ(swSize.x, 40.f);
    EXPECT_EQ(swSize.y, 22.f);

    const Vec2 cbSize = cbWidget->Measure(mctx, cbNode, {}, {});
    EXPECT_GT(cbSize.x, 0.f);
    EXPECT_GT(cbSize.y, 0.f);
}

TEST(WidgetTests, CheckboxBindingAndCustomColors) {
    Harness h;
    Owner owner;

    auto checked = Signal<bool>(false);
    const NodeId cbNode = h.Mount(Checkbox({
        .checked = checked,
        .label = "Enable feature",
        .checkColor = Color::Green,
        .borderColor = Color::Red,
        .labelColor = Color::Blue,
    }), owner);

    const auto& payload = h.arena.PayloadOf<CheckboxPayload>(cbNode);
    EXPECT_FALSE(payload.value);
    EXPECT_EQ(payload.label, "Enable feature");

    checked = true;
    EXPECT_TRUE(h.arena.PayloadOf<CheckboxPayload>(cbNode).value);
}

TEST(WidgetTests, SplitterMountsWithClass) {
    Harness h;
    Owner owner;

    const NodeId split = h.Mount(Splitter({
        .axis = DockAxis::Horizontal,
        .className = "splitter",
    }), owner);

    const LiveNode& node = h.arena.Resolve(split);
    EXPECT_EQ(node.kind, LiveKind::Splitter);
}

} // namespace
