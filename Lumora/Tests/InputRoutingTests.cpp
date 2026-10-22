#include <gtest/gtest.h>

#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"

#include <cstring>
#include <string>
#include <vector>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

[[nodiscard]] Event Mouse(EventType type, PointerButton button, float x, float y) {
    Event ev{};
    ev.Type     = type;
    ev.Position = {x, y};
    ev.Button   = button;
    return ev;
}

[[nodiscard]] Event KeyEvent(EventType type, Key key, KeyMods mods = KeyMods::None) {
    Event ev{};
    ev.Type    = type;
    ev.KeyCode = key;
    ev.Mods    = mods;
    return ev;
}

[[nodiscard]] Event Typed(const char* text) {
    Event ev{};
    ev.Type = EventType::TextInput;
    std::strncpy(ev.Text, text, sizeof(ev.Text) - 1);
    return ev;
}

void Frame(MountedApp& app) {
    app.Tick(1.f / 60.f);
    app.Layout(kSurfaceW, kSurfaceH);
}

struct Probe {
    int         presses  = 0;
    int         releases = 0;
    int         moves    = 0;
    int         menus    = 0;
    int         keyDowns = 0;
    int         keyUps   = 0;
    int         lastButton = -1;
    Key         lastKey  = Key::Unknown;
};

[[nodiscard]] BoxCfg Placed(float x, float y, float w, float h) {
    return BoxCfg{.layout   = {.width = Fixed(w), .height = Fixed(h)},
                  .position = {.position = Position::Absolute, .inset = {.left = x, .top = y}}};
}

[[nodiscard]] BoxCfg Pointerable(Probe& p, float x, float y, float w = 100.f, float h = 60.f) {
    BoxCfg cfg    = Placed(x, y, w, h);
    cfg.on.onPress = [&p](PointerInfo info) {
        ++p.presses;
        p.lastButton = info.button;
    };
    cfg.on.onRelease = [&p](PointerInfo) { ++p.releases; };
    cfg.on.onMove    = [&p](PointerInfo) { ++p.moves; };
    return cfg;
}

[[nodiscard]] Elem Root(Elem a, Elem b = Elem{}) {
    return Stack(StackCfg{.layout = {.width = Expand, .height = Expand}, .anchor = true},
                 std::move(a), std::move(b));
}

TEST(InputRoutingTests, AFocusedFieldOwnsPrintableKeysAndLeavesChordsToBubble) {
    FakeFonts           fonts;
    Probe               parent;
    Signal<std::string> text = Signal(std::string{});

    BoxCfg around{.layout = {.width = Expand, .height = Expand}};
    around.on.onKeyDown = [&parent](KeyInfo key) {
        ++parent.keyDowns;
        parent.lastKey = key.key;
        return false;
    };

    MountedApp app(Box(std::move(around), Input({.value = text})), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyEvent(EventType::KeyDown, Key::Tab));

    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::T)));
    EXPECT_TRUE(app.Dispatch(Typed("t")));
    Frame(app);
    EXPECT_EQ(text(), "t");
    EXPECT_EQ(parent.keyDowns, 0);

    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::Delete)));
    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::Home)));
    EXPECT_EQ(parent.keyDowns, 0);

    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::W, KeyMods::Shift)));
    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::N5)));
    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::Minus)));
    EXPECT_EQ(parent.keyDowns, 0);

    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::Z, KeyMods::Ctrl)));
    EXPECT_EQ(parent.keyDowns, 0);

    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::S, KeyMods::Ctrl)));
    EXPECT_EQ(parent.keyDowns, 1);
    EXPECT_EQ(parent.lastKey, Key::S);
    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::T, KeyMods::Alt)));
    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::F5)));
    EXPECT_EQ(parent.keyDowns, 3);
}

TEST(InputRoutingTests, AKeyNothingHandlesLeavesTheTreeUnconsumed) {
    FakeFonts fonts;
    BoxCfg    cfg{.layout = {.width = Fixed(100.f), .height = Fixed(20.f)}};
    cfg.on.focusPolicy = FocusPolicy::Tab;

    MountedApp app(Column({}, Box(std::move(cfg))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::W)));

    app.Dispatch(KeyEvent(EventType::KeyDown, Key::Tab));
    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::W)));
    EXPECT_FALSE(app.Dispatch(KeyEvent(EventType::KeyUp, Key::W)));
}

TEST(InputRoutingTests, KeyUpReachesTheFocusedNodeAndOnlyIt) {
    FakeFonts fonts;
    Probe     focused;
    Probe     other;

    BoxCfg a{.layout = {.width = Fixed(100.f), .height = Fixed(20.f)}};
    a.on.focusPolicy = FocusPolicy::Tab;
    a.on.onKeyDown   = [&focused](KeyInfo key) { ++focused.keyDowns; focused.lastKey = key.key; return true; };
    a.on.onKeyUp     = [&focused](KeyInfo key) { ++focused.keyUps; focused.lastKey = key.key; return true; };

    BoxCfg b{.layout = {.width = Fixed(100.f), .height = Fixed(20.f)}};
    b.on.focusPolicy = FocusPolicy::Tab;
    b.on.onKeyUp     = [&other](KeyInfo) { ++other.keyUps; return true; };

    MountedApp app(Column({}, Box(std::move(a)), Box(std::move(b))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyEvent(EventType::KeyDown, Key::Tab));

    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyDown, Key::W)));
    EXPECT_EQ(focused.keyDowns, 1);
    EXPECT_TRUE(app.Dispatch(KeyEvent(EventType::KeyUp, Key::W)));
    EXPECT_EQ(focused.keyUps, 1);
    EXPECT_EQ(focused.lastKey, Key::W);
    EXPECT_EQ(other.keyUps, 0);
}

TEST(InputRoutingTests, RightPressReachesOnlyAnOptedInNodeAndCaptures) {
    FakeFonts fonts;
    Probe     optedIn;
    Probe     plain;

    BoxCfg in = Pointerable(optedIn, 20.f, 20.f);
    in.on.rightButton = true;
    BoxCfg out = Pointerable(plain, 220.f, 20.f);

    MountedApp app(Root(Box(std::move(in)), Box(std::move(out))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Right, 230.f, 30.f)));
    EXPECT_EQ(plain.presses, 0);
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Right, 230.f, 30.f));

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Right, 30.f, 30.f)));
    EXPECT_EQ(optedIn.presses, 1);
    EXPECT_EQ(optedIn.lastButton, static_cast<int>(PointerButton::Right));

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseMove, PointerButton::Right, 380.f, 280.f)));
    EXPECT_EQ(optedIn.moves, 1);
    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Right, 380.f, 280.f)));
    EXPECT_EQ(optedIn.releases, 1);
    EXPECT_EQ(plain.presses, 0);
}

TEST(InputRoutingTests, AnOptedInNodeTakesTheRightPressAheadOfItsContextMenu) {
    FakeFonts fonts;
    Probe     p;

    BoxCfg cfg = Pointerable(p, 20.f, 20.f);
    cfg.on.rightButton   = true;
    cfg.on.onContextMenu = [&p] {
        ++p.menus;
        return std::vector<ContextMenuItem>{};
    };

    MountedApp app(Root(Box(std::move(cfg))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Right, 30.f, 30.f)));
    EXPECT_EQ(p.presses, 1);
    EXPECT_EQ(p.menus, 0);
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Right, 30.f, 30.f));
    EXPECT_EQ(p.releases, 1);
}

TEST(InputRoutingTests, TheTopmostNodeSwallowsARightPressOverACanvas) {
    FakeFonts fonts;
    Probe     canvas;
    Probe     popup;

    BoxCfg below = Pointerable(canvas, 0.f, 0.f, kSurfaceW, kSurfaceH);
    below.on.rightButton = true;
    BoxCfg above = Pointerable(popup, 100.f, 100.f, 120.f, 80.f);
    above.on.rightButton = true;

    MountedApp app(Root(Box(std::move(below)), Box(std::move(above))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Right, 150.f, 140.f)));
    EXPECT_EQ(popup.presses, 1);
    EXPECT_EQ(canvas.presses, 0);
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Right, 150.f, 140.f));

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Right, 20.f, 20.f)));
    EXPECT_EQ(canvas.presses, 1);
    EXPECT_EQ(popup.presses, 1);
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Right, 20.f, 20.f));
}

TEST(InputRoutingTests, ASideButtonIsNotALeftClick) {
    FakeFonts fonts;
    Probe     p;

    BoxCfg cfg = Pointerable(p, 20.f, 20.f);
    int    clicks = 0;
    cfg.on.onClick = [&clicks] { ++clicks; };

    MountedApp app(Root(Box(std::move(cfg))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::X1, 30.f, 30.f)));
    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseUp, PointerButton::X1, 30.f, 30.f)));
    EXPECT_EQ(p.presses, 0);
    EXPECT_EQ(clicks, 0);
}

TEST(InputRoutingTests, SubtreePassThroughPrunesNodeAndDescendants) {
    FakeFonts fonts;
    Probe     underneath;
    Probe     child;

    BoxCfg overlayCfg = Placed(10.f, 10.f, 200.f, 200.f);
    overlayCfg.hitThroughSubtree = true;

    BoxCfg childCfg = Pointerable(child, 20.f, 20.f, 100.f, 50.f);
    int childClicks = 0;
    childCfg.on.onClick = [&childClicks] { ++childClicks; };

    BoxCfg underCfg = Pointerable(underneath, 10.f, 10.f, 200.f, 200.f);
    int underClicks = 0;
    underCfg.on.onClick = [&underClicks] { ++underClicks; };

    MountedApp app(Root(Box(std::move(underCfg)),
                        Box(std::move(overlayCfg), Box(std::move(childCfg)))),
                   &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Left, 30.f, 30.f)));
    EXPECT_EQ(child.presses, 0);
    EXPECT_EQ(underneath.presses, 1);

    EXPECT_TRUE(app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Left, 30.f, 30.f)));
    EXPECT_EQ(childClicks, 0);
    EXPECT_EQ(underClicks, 1);
}

TEST(InputRoutingTests, EmptyHitThroughContainerLeavesEventUnconsumed) {
    FakeFonts fonts;

    BoxCfg emptyOverlay{.layout = {.width = Expand, .height = Expand}};
    emptyOverlay.on.hitThrough = true;

    MountedApp app(Box(std::move(emptyOverlay)), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Left, 50.f, 50.f)));
    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseMove, PointerButton::Left, 60.f, 60.f)));
    EXPECT_FALSE(app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Left, 60.f, 60.f)));
}

} // namespace

TEST(InputRoutingTests, AHiddenOverlayDoesNotSwallowClicks) {
    FakeFonts fonts;
    Probe     underneath;
    Signal<bool> shown{false};

    BoxCfg underCfg = Pointerable(underneath, 10.f, 10.f, 200.f, 200.f);
    int underClicks = 0;
    underCfg.on.onClick = [&underClicks] { ++underClicks; };

    BoxCfg scrimCfg = Placed(0.f, 0.f, kSurfaceW, kSurfaceH);
    scrimCfg.view.visible = [shown] { return shown(); };
    int scrimClicks = 0;
    scrimCfg.on.onClick = [&scrimClicks] { ++scrimClicks; };

    MountedApp app(Root(Box(std::move(underCfg)), Box(std::move(scrimCfg))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    Frame(app);

    app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Left, 30.f, 30.f));
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Left, 30.f, 30.f));
    EXPECT_EQ(underClicks, 1);
    EXPECT_EQ(scrimClicks, 0);

    shown = true;
    Frame(app);
    app.Dispatch(Mouse(EventType::MouseDown, PointerButton::Left, 30.f, 30.f));
    app.Dispatch(Mouse(EventType::MouseUp, PointerButton::Left, 30.f, 30.f));
    EXPECT_EQ(underClicks, 1);
    EXPECT_EQ(scrimClicks, 1);
}
