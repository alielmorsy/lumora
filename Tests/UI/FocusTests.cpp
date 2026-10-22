#include <gtest/gtest.h>

#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

[[nodiscard]] bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

[[nodiscard]] Event KeyPress(Key key, KeyMods mods = KeyMods::None) {
    Event ev{};
    ev.Type    = EventType::KeyDown;
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

struct Grid {
    Signal<double>      x = Signal(1.0);
    Signal<double>      y = Signal(2.0);
    Signal<double>      z = Signal(3.0);
    Signal<std::string> search = Signal(std::string{});

    [[nodiscard]] Elem Tree() {
        return Column({.layout = {.width = Expand}},
                      NumberField({.value = x, .step = 1.0, .label = "X"}),
                      NumberField({.value = y, .step = 1.0, .label = "Y"}),
                      NumberField({.value = z, .step = 1.0, .label = "Z"}),
                      Input({.value = search}));
    }
};

TEST(FocusTests, TabWalksTheNumericRowsBeforeLeavingTheGrid) {
    FakeFonts fonts;
    Grid      grid;
    MountedApp app(grid.Tree(), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("5"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Enter));
    Frame(app);

    EXPECT_TRUE(Near(grid.y(), 5.0));
    EXPECT_TRUE(Near(grid.x(), 1.0));
    EXPECT_TRUE(Near(grid.z(), 3.0));
    EXPECT_TRUE(grid.search().empty());

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("7"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Enter));
    Frame(app);

    EXPECT_TRUE(Near(grid.z(), 7.0));
    EXPECT_TRUE(grid.search().empty());
}

TEST(FocusTests, CommittingWithEnterKeepsFocusOnTheRow) {
    FakeFonts fonts;
    Grid      grid;
    MountedApp app(grid.Tree(), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("9"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Enter));
    Frame(app);

    EXPECT_TRUE(Near(grid.x(), 9.0));

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("3"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Enter));
    Frame(app);

    EXPECT_TRUE(Near(grid.y(), 3.0));
    EXPECT_TRUE(grid.search().empty());
}

TEST(FocusTests, EscapeLeavesTheValueAndKeepsTheRowFocused) {
    FakeFonts fonts;
    Grid      grid;
    MountedApp app(grid.Tree(), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("4"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Escape));
    Frame(app);

    EXPECT_TRUE(Near(grid.x(), 1.0));

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(Typed("6"));
    Frame(app);
    app.Dispatch(KeyPress(Key::Enter));
    Frame(app);

    EXPECT_TRUE(Near(grid.y(), 6.0));
    EXPECT_TRUE(grid.search().empty());
}

TEST(FocusTests, ArrowsNudgeTheFocusedRow) {
    FakeFonts fonts;
    Grid      grid;
    MountedApp app(grid.Tree(), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    app.Dispatch(KeyPress(Key::Up));
    EXPECT_TRUE(Near(grid.x(), 2.0));

    app.Dispatch(KeyPress(Key::Up, KeyMods::Shift));
    EXPECT_TRUE(Near(grid.x(), 12.0));

    app.Dispatch(KeyPress(Key::Down));
    EXPECT_TRUE(Near(grid.x(), 11.0));
    EXPECT_TRUE(Near(grid.y(), 2.0));
}

TEST(FocusTests, AFocusableBoxSeesFocusAndKeys) {
    FakeFonts fonts;
    int       focuses = 0;
    int       blurs   = 0;
    int       spaces  = 0;
    bool      viaKeyboard = false;

    BoxCfg cfg{.layout = {.width = Fixed(100.f), .height = Fixed(20.f)}};
    cfg.on.focusPolicy = FocusPolicy::Tab;
    cfg.on.onFocus     = [&](bool keyboard) { ++focuses; viaKeyboard = keyboard; };
    cfg.on.onBlur      = [&] { ++blurs; };
    cfg.on.onKeyDown   = [&](KeyInfo key) {
        if (key.key != Key::Space) {
            return false;
        }
        ++spaces;
        return true;
    };

    Signal<std::string> text = Signal(std::string{});
    MountedApp app(Column({}, Box(std::move(cfg)), Input({.value = text})), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    EXPECT_EQ(focuses, 1);
    EXPECT_TRUE(viaKeyboard);

    app.Dispatch(KeyPress(Key::Space));
    EXPECT_EQ(spaces, 1);

    app.Dispatch(KeyPress(Key::Tab));
    EXPECT_EQ(blurs, 1);
    app.Dispatch(Typed("a"));
    Frame(app);
    EXPECT_EQ(text(), "a");
}

TEST(FocusTests, AClickOnlyBoxIsSkippedByTab) {
    FakeFonts fonts;
    int       focuses = 0;

    BoxCfg skipped{.layout = {.width = Fixed(100.f), .height = Fixed(20.f)}};
    skipped.on.focusPolicy = FocusPolicy::Click;
    skipped.on.onFocus     = [&](bool) { ++focuses; };

    Signal<std::string> text = Signal(std::string{});
    MountedApp app(Column({}, Box(std::move(skipped)), Input({.value = text})), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(KeyPress(Key::Tab));
    EXPECT_EQ(focuses, 0);
    app.Dispatch(Typed("b"));
    Frame(app);
    EXPECT_EQ(text(), "b");
}

TEST(FocusTests, DirectionalFocusGridNavigation) {
    FakeFonts fonts;
    Anchor aTL, aTR, aBL, aBR;
    int focusTL = 0, focusTR = 0, focusBL = 0, focusBR = 0;

    auto makeBtn = [](Anchor& a, int& counter) {
        BoxCfg cfg{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
        cfg.on.ref = a;
        cfg.on.focusPolicy = FocusPolicy::Tab;
        cfg.on.onFocus = [&](bool) { ++counter; };
        return Box(std::move(cfg));
    };

    Elem tree = Column({},
        Row({}, makeBtn(aTL, focusTL), makeBtn(aTR, focusTR)),
        Row({}, makeBtn(aBL, focusBL), makeBtn(aBR, focusBR))
    );

    MountedApp app(std::move(tree), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.FocusNode(aTL);
    EXPECT_EQ(focusTL, 1);

    EXPECT_TRUE(app.MoveFocus(Direction::Right));
    EXPECT_EQ(focusTR, 1);

    EXPECT_TRUE(app.MoveFocus(Direction::Down));
    EXPECT_EQ(focusBR, 1);

    EXPECT_TRUE(app.MoveFocus(Direction::Left));
    EXPECT_EQ(focusBL, 1);

    EXPECT_TRUE(app.MoveFocus(Direction::Up));
    EXPECT_EQ(focusTL, 2);
}

TEST(FocusTests, NavOverridesTakePrecedence) {
    FakeFonts fonts;
    Anchor aA, aB, aC;
    int focusA = 0, focusB = 0, focusC = 0;

    BoxCfg cfgA{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
    cfgA.on.ref = aA;
    cfgA.on.focusPolicy = FocusPolicy::Tab;
    cfgA.on.nav.down = aC;
    cfgA.on.onFocus = [&](bool) { ++focusA; };

    BoxCfg cfgB{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
    cfgB.on.ref = aB;
    cfgB.on.focusPolicy = FocusPolicy::Tab;
    cfgB.on.onFocus = [&](bool) { ++focusB; };

    BoxCfg cfgC{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
    cfgC.on.ref = aC;
    cfgC.on.focusPolicy = FocusPolicy::Tab;
    cfgC.on.onFocus = [&](bool) { ++focusC; };

    Elem tree = Column({}, Box(std::move(cfgA)), Box(std::move(cfgB)), Box(std::move(cfgC)));

    MountedApp app(std::move(tree), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.FocusNode(aA);
    EXPECT_EQ(focusA, 1);

    EXPECT_TRUE(app.MoveFocus(Direction::Down));
    EXPECT_EQ(focusC, 1);
    EXPECT_EQ(focusB, 0);
}

TEST(FocusTests, FocusScopeConfinesTraversal) {
    FakeFonts fonts;
    Anchor aIn1, aIn2, aOutside;
    int focusIn1 = 0, focusIn2 = 0, focusOut = 0;

    auto makeBtn = [](Anchor& a, int& counter) {
        BoxCfg cfg{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
        cfg.on.ref = a;
        cfg.on.focusPolicy = FocusPolicy::Tab;
        cfg.on.onFocus = [&](bool) { ++counter; };
        return Box(std::move(cfg));
    };

    BoxCfg scopeCfg{.layout = {.width = Fixed(200.f), .height = Fixed(200.f)}};
    scopeCfg.focusScope = true;

    Elem tree = Column({},
        Box(std::move(scopeCfg),
            makeBtn(aIn1, focusIn1),
            makeBtn(aIn2, focusIn2)
        ),
        makeBtn(aOutside, focusOut)
    );

    MountedApp app(std::move(tree), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.FocusNode(aIn1);
    EXPECT_EQ(focusIn1, 1);

    app.FocusNext();
    EXPECT_EQ(focusIn2, 1);

    app.FocusNext();
    EXPECT_EQ(focusIn1, 2);
    EXPECT_EQ(focusOut, 0);
}

TEST(FocusTests, FocusScopeMemoryRestoresLastFocused) {
    FakeFonts fonts;
    Anchor aScope1, aScope2, a1A, a1B, a2A;
    int focus1A = 0, focus1B = 0, focus2A = 0;

    auto makeBtn = [](Anchor& a, int& counter) {
        BoxCfg cfg{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
        cfg.on.ref = a;
        cfg.on.focusPolicy = FocusPolicy::Tab;
        cfg.on.onFocus = [&](bool) { ++counter; };
        return Box(std::move(cfg));
    };

    BoxCfg scope1Cfg{.layout = {.width = Fixed(100.f), .height = Fixed(100.f)}};
    scope1Cfg.on.ref = aScope1;
    scope1Cfg.focusScope = true;

    BoxCfg scope2Cfg{.layout = {.width = Fixed(100.f), .height = Fixed(100.f)}};
    scope2Cfg.on.ref = aScope2;
    scope2Cfg.focusScope = true;

    Elem tree = Row({},
        Box(std::move(scope1Cfg), makeBtn(a1A, focus1A), makeBtn(a1B, focus1B)),
        Box(std::move(scope2Cfg), makeBtn(a2A, focus2A))
    );

    MountedApp app(std::move(tree), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.FocusNode(a1B);
    EXPECT_EQ(focus1B, 1);

    app.FocusNode(a2A);
    EXPECT_EQ(focus2A, 1);

    app.FocusNode(aScope1);
    EXPECT_EQ(focus1B, 2);
}

TEST(FocusTests, AutofocusInScopeFocusesOnMount) {
    FakeFonts fonts;
    int autoFocused = 0;

    BoxCfg autoBtn{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
    autoBtn.on.autofocus = true;
    autoBtn.on.focusPolicy = FocusPolicy::Tab;
    autoBtn.on.onFocus = [&](bool) { ++autoFocused; };

    BoxCfg scopeCfg{.layout = {.width = Fixed(100.f), .height = Fixed(100.f)}};
    scopeCfg.focusScope = true;

    MountedApp app(Box(std::move(scopeCfg), Box(std::move(autoBtn))), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    EXPECT_EQ(autoFocused, 1);
}

TEST(FocusTests, ActivateFocusedTriggersOnClick) {
    FakeFonts fonts;
    Anchor btnAnchor;
    int clicks = 0;

    BoxCfg btn{.layout = {.width = Fixed(50.f), .height = Fixed(50.f)}};
    btn.on.ref = btnAnchor;
    btn.on.focusPolicy = FocusPolicy::Tab;
    btn.on.onClick = [&] { ++clicks; };

    MountedApp app(Box(std::move(btn)), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.FocusNode(btnAnchor);
    EXPECT_EQ(clicks, 0);

    EXPECT_TRUE(app.ActivateFocused());
    EXPECT_EQ(clicks, 1);

    app.Dispatch(KeyPress(Key::Enter));
    EXPECT_EQ(clicks, 1);
    Event keyUp{};
    keyUp.Type = EventType::KeyUp;
    keyUp.KeyCode = Key::Enter;
    app.Dispatch(keyUp);
    EXPECT_EQ(clicks, 2);
}

} // namespace
