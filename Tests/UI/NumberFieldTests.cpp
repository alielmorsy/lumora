#include <gtest/gtest.h>

#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "Layout/LayoutBridge.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"

#include <cmath>
#include <functional>
#include <string>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

[[nodiscard]] bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

struct Harness {
    Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
    GraphScope         scope{*graph};
    NodeArena          arena;
    LayoutTree         layout;
    AppCtx             ctx{arena, *graph, &layout};

    NodeId Mount(const Elem& tree, Owner& owner) {
        return Instantiate(ctx, *tree.RawNode(), NodeId{}, owner);
    }

    [[nodiscard]] InteractState& Handle(NodeId field) {
        NodeId node = field;
        while (arena.Resolve(node).kind != LiveKind::Box && arena.Resolve(node).firstChild.Valid()) {
            node = arena.Resolve(node).firstChild;
        }
        return arena.PayloadOf<ContainerPayload>(node).on;
    }
};

[[nodiscard]] PointerInfo Drag(float dx, KeyMods mods = KeyMods::None) {
    return PointerInfo{.delta = {dx, 0.f}, .pressed = true, .modifiers = mods};
}

[[nodiscard]] NodeId FindKind(NodeArena& arena, NodeId id, LiveKind kind) {
    if (!id.Valid()) {
        return {};
    }
    if (arena.Resolve(id).kind == kind) {
        return id;
    }
    for (NodeId child = arena.Resolve(id).firstChild; child.Valid(); child = arena.Resolve(child).nextSibling) {
        if (const NodeId hit = FindKind(arena, child, kind); hit.Valid()) {
            return hit;
        }
    }
    return {};
}

[[nodiscard]] NodeId MountField(Harness& h, Owner& owner, NumberFieldCfg cfg) {
    return h.Mount(Column({}, NumberField(std::move(cfg))), owner);
}

TEST(NumberFieldTests, ScrubMovesTheValueByStepPerPixel) {
    Harness h;
    Owner   owner;
    auto    value = Signal(0.0);

    const NodeId root = MountField(h, owner, {.value = value, .step = 0.5});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onMove(Drag(10.f));
    EXPECT_TRUE(Near(value(), 5.0));

    on.onMove(Drag(-4.f));
    EXPECT_TRUE(Near(value(), 3.0));

    on.onRelease(Drag(0.f));
    EXPECT_TRUE(Near(value(), 3.0));
}

TEST(NumberFieldTests, ShiftScalesUpAndAltScalesDown) {
    Harness h;
    Owner   owner;
    auto    value = Signal(0.0);

    const NodeId root = MountField(h, owner, {.value = value, .step = 1.0});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onMove(Drag(1.f, KeyMods::Shift));
    EXPECT_TRUE(Near(value(), 10.0));

    on.onMove(Drag(1.f, KeyMods::Alt));
    EXPECT_TRUE(Near(value(), 10.1));
    on.onRelease(Drag(0.f));
}

TEST(NumberFieldTests, MinAndMaxClampTheScrub) {
    Harness h;
    Owner   owner;
    auto    value = Signal(0.5);

    const NodeId root = MountField(h, owner, {.value = value, .min = 0.0, .max = 1.0, .step = 0.1});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onMove(Drag(100.f));
    EXPECT_TRUE(Near(value(), 1.0));

    on.onMove(Drag(-1000.f));
    EXPECT_TRUE(Near(value(), 0.0));
    on.onRelease(Drag(0.f));
}

TEST(NumberFieldTests, SnapQuantisesTheWrittenValue) {
    Harness h;
    Owner   owner;
    auto    value = Signal(0.0);

    const NodeId root = MountField(h, owner, {.value = value, .step = 0.1, .snap = 0.25});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onMove(Drag(12.f));
    EXPECT_TRUE(Near(std::fmod(value(), 0.25), 0.0, 1e-9));
    on.onMove(Drag(3.f));
    EXPECT_TRUE(Near(std::fmod(value(), 0.25), 0.0, 1e-9));
    on.onRelease(Drag(0.f));
}

TEST(NumberFieldTests, OneDragIsOneCommit) {
    Harness h;
    Owner   owner;
    auto    value   = Signal(0.0);
    int     changes = 0;
    int     commits = 0;

    const NodeId root = MountField(h, owner,
                                   {.value    = value,
                                    .step     = 1.0,
                                    .onChange = [&changes](double) { ++changes; },
                                    .onCommit = [&commits](double) { ++commits; }});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    for (int i = 0; i < 20; ++i) {
        on.onMove(Drag(3.f));
    }
    on.onRelease(Drag(0.f));

    EXPECT_EQ(changes, 20);
    EXPECT_EQ(commits, 1);
}

TEST(NumberFieldTests, APressWithNoMovementCommitsNothing) {
    Harness h;
    Owner   owner;
    auto    value   = Signal(2.0);
    int     commits = 0;

    const NodeId root = MountField(h, owner,
                                   {.value = value, .step = 1.0,
                                    .onCommit = [&commits](double) { ++commits; }});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onRelease(Drag(0.f));

    EXPECT_EQ(commits, 0);
    EXPECT_TRUE(Near(value(), 2.0));
}

TEST(NumberFieldTests, ADisabledFieldIgnoresTheGesture) {
    Harness h;
    Owner   owner;
    auto    value = Signal(1.0);

    const NodeId root = MountField(h, owner, {.value = value, .step = 1.0, .disabled = true});
    InteractState& on = h.Handle(root);

    EXPECT_TRUE(on.disabled);
    if (on.onPress) on.onPress(Drag(10.f));
    if (on.onMove) on.onMove(Drag(10.f));
    if (on.onRelease) on.onRelease(Drag(0.f));
    EXPECT_TRUE(Near(value(), 1.0));
}

TEST(NumberFieldTests, BlurCommitsTheTypedValueAndClosesTheEditor) {
    Harness h;
    Owner   owner;
    auto    value   = Signal(1.0);
    int     commits = 0;

    const NodeId root = MountField(h, owner,
                                   {.value = value, .step = 1.0,
                                    .onCommit = [&commits](double) { ++commits; }});
    InteractState& on = h.Handle(root);

    on.onPress(Drag(0.f));
    on.onRelease(Drag(0.f));

    const NodeId input = FindKind(h.arena, root, LiveKind::Input);
    EXPECT_TRUE(input.Valid());

    const std::function<void(std::string)> blur = h.arena.PayloadOf<InputPayload>(input).onBlur;
    EXPECT_TRUE(static_cast<bool>(blur));
    if (blur) {
        blur("4.5");
    }

    EXPECT_TRUE(Near(value(), 4.5));
    EXPECT_EQ(commits, 1);
    EXPECT_FALSE(FindKind(h.arena, root, LiveKind::Input).Valid());
}

TEST(NumberFieldTests, SliderPressWritesThroughTheRange) {
    Harness h;
    Owner   owner;
    auto    value = Signal(0.0);

    const NodeId root = h.Mount(Column({.layout = {.width = Fixed(200.f)}},
                                       Slider({.value = value, .min = 0.0, .max = 10.0})),
                                owner);
    Layout(h.ctx, root, 200.f, 40.f);

    NodeId painter = h.arena.Resolve(root).firstChild;
    while (painter.Valid() && h.arena.Resolve(painter).kind != LiveKind::Painter) {
        painter = h.arena.Resolve(painter).firstChild;
    }
    EXPECT_TRUE(painter.Valid());

    InteractState& on = h.arena.PayloadOf<PainterPayload>(painter).on;
    on.onPress(PointerInfo{.position = {100.f, 10.f}, .pressed = true});
    EXPECT_TRUE(Near(value(), 0.0));
}

} // namespace
