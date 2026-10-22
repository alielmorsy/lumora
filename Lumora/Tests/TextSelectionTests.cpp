#include <gtest/gtest.h>

#include <lumora/Clipboard.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Text.h>
#include <lumora/Widgets/TextSelectionScope.h>

#include "FakeFonts.h"

#include <string>
#include <vector>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

constexpr float kRow  = 14.f;
constexpr float kChar = 7.f;

const std::vector<std::string> kLines = {"alpha", "beta", "gamma"};

[[nodiscard]] Event Mouse(EventType type, float x, float y) {
    Event ev{};
    ev.Type     = type;
    ev.Position = {x, y};
    ev.Button   = PointerButton::Left;
    return ev;
}

[[nodiscard]] Event KeyEvent(Key code, KeyMods mods) {
    Event ev{};
    ev.Type    = EventType::KeyDown;
    ev.KeyCode = code;
    ev.Mods    = mods;
    return ev;
}

[[nodiscard]] float RowMid(int index) { return static_cast<float>(index) * kRow + kRow * 0.5f; }
[[nodiscard]] float CharX(int offset) { return static_cast<float>(offset) * kChar; }

[[nodiscard]] Elem Scene(const TextSelectionRef& selection, TextSelectionScopeCfg cfg = {}) {
    cfg.selection = selection;
    std::vector<Elem> items;
    for (std::size_t i = 0; i < kLines.size(); ++i) {
        items.push_back(Text(kLines[i], {.selection = selection, .selectionKey = i}));
    }
    return TextSelectionScope(std::move(cfg),
                              Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}},
                                     Children(items)));
}

TEST(TextSelectionTests, RangeInCoversWholeMiddleLeaves) {
    const TextSelectionRef selection = MakeTextSelection();
    selection->Begin(TextPos{0, 2});
    selection->ExtendTo(TextPos{2, 3});

    EXPECT_TRUE(selection->HasSelection());
    EXPECT_EQ(selection->Min(), (TextPos{0, 2}));
    EXPECT_EQ(selection->Max(), (TextPos{2, 3}));

    const auto first = selection->RangeIn(0, 5);
    EXPECT_TRUE(first && first->first == 2 && first->second == 5);

    const auto middle = selection->RangeIn(1, 4);
    EXPECT_TRUE(middle && middle->first == 0 && middle->second == 4);

    const auto last = selection->RangeIn(2, 5);
    EXPECT_TRUE(last && last->first == 0 && last->second == 3);

    EXPECT_FALSE(selection->RangeIn(3, 5));
}

TEST(TextSelectionTests, RangeInIsDirectionAgnostic) {
    const TextSelectionRef forward = MakeTextSelection();
    forward->Begin(TextPos{0, 1});
    forward->ExtendTo(TextPos{1, 2});

    const TextSelectionRef backward = MakeTextSelection();
    backward->Begin(TextPos{1, 2});
    backward->ExtendTo(TextPos{0, 1});

    EXPECT_EQ(forward->RangeIn(0, 5), backward->RangeIn(0, 5));
    EXPECT_EQ(forward->RangeIn(1, 5), backward->RangeIn(1, 5));
}

TEST(TextSelectionTests, RangeInClampsToTheLeafText) {
    const TextSelectionRef selection = MakeTextSelection();
    selection->Set(TextPos{0, 0}, TextPos{0, 99});

    const auto range = selection->RangeIn(0, 4);
    EXPECT_TRUE(range && range->first == 0 && range->second == 4);
}

TEST(TextSelectionTests, CollapsedSelectionSelectsNothing) {
    const TextSelectionRef selection = MakeTextSelection();
    selection->Begin(TextPos{1, 3});

    EXPECT_TRUE(selection->Active());
    EXPECT_FALSE(selection->HasSelection());
    EXPECT_FALSE(selection->RangeIn(1, 5));
}

TEST(TextSelectionTests, EpochTracksRealChangesOnly) {
    const TextSelectionRef selection = MakeTextSelection();
    selection->Begin(TextPos{0, 0});
    const std::uint64_t after = selection->Epoch();

    selection->ExtendTo(TextPos{0, 0});
    EXPECT_EQ(selection->Epoch(), after);

    selection->ExtendTo(TextPos{0, 2});
    EXPECT_GT(selection->Epoch(), after);
}

TEST(TextSelectionTests, DragAcrossLeavesSelectsThrough) {
    FakeFonts              fonts;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(1), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseMove, CharX(2), RowMid(2)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(2), RowMid(2)));

    EXPECT_TRUE(selection->HasSelection());
    EXPECT_EQ(selection->Min(), (TextPos{0, 1}));
    EXPECT_EQ(selection->Max(), (TextPos{2, 2}));
}

TEST(TextSelectionTests, CtrlCCopiesTheSelectedSpan) {
    FakeFonts              fonts;
    DefaultClipboard       clip;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.SetInteractionEnv(&fonts, &clip);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(1), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseMove, CharX(2), RowMid(2)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(2), RowMid(2)));
    app.Dispatch(KeyEvent(Key::C, KeyMods::Ctrl));

    EXPECT_EQ(clip.GetText(), "lpha\nbeta\nga");
}

TEST(TextSelectionTests, CopyPrefersTheCallerRange) {
    FakeFonts              fonts;
    DefaultClipboard       clip;
    const TextSelectionRef selection = MakeTextSelection();

    TextSelectionScopeCfg cfg{};
    cfg.textRange = [](TextPos from, TextPos to) {
        return "range " + std::to_string(from.key) + ":" + std::to_string(from.offset) + "-" +
               std::to_string(to.key) + ":" + std::to_string(to.offset);
    };

    MountedApp app(Scene(selection, std::move(cfg)), &fonts);
    app.SetInteractionEnv(&fonts, &clip);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(1), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseMove, CharX(2), RowMid(1)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(2), RowMid(1)));
    app.Dispatch(KeyEvent(Key::C, KeyMods::Ctrl));

    EXPECT_EQ(clip.GetText(), "range 0:1-1:2");
}

TEST(TextSelectionTests, DoubleClickSelectsTheWord) {
    FakeFonts              fonts;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(2), RowMid(1)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(2), RowMid(1)));
    app.Dispatch(Mouse(EventType::MouseDown, CharX(2), RowMid(1)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(2), RowMid(1)));

    EXPECT_EQ(selection->Min(), (TextPos{1, 0}));
    EXPECT_EQ(selection->Max(), (TextPos{1, 4}));
}

TEST(TextSelectionTests, CtrlASelectsEveryLeaf) {
    FakeFonts              fonts;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(1), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(1), RowMid(0)));
    app.Dispatch(KeyEvent(Key::A, KeyMods::Ctrl));

    EXPECT_EQ(selection->Min(), (TextPos{0, 0}));
    EXPECT_EQ(selection->Max(), (TextPos{2, kLines[2].size()}));
}

TEST(TextSelectionTests, EscapeClearsTheSelection) {
    FakeFonts              fonts;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(0), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseMove, CharX(3), RowMid(1)));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(3), RowMid(1)));
    EXPECT_TRUE(selection->HasSelection());

    app.Dispatch(KeyEvent(Key::Escape, KeyMods::None));
    EXPECT_FALSE(selection->HasSelection());
}

TEST(TextSelectionTests, DragBelowTheLastLeafSelectsToItsEnd) {
    FakeFonts              fonts;
    const TextSelectionRef selection = MakeTextSelection();
    MountedApp             app(Scene(selection), &fonts);
    app.SetInteractionEnv(&fonts, nullptr);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, CharX(0), RowMid(0)));
    app.Dispatch(Mouse(EventType::MouseMove, CharX(0), kSurfaceH - 1.f));
    app.Dispatch(Mouse(EventType::MouseUp, CharX(0), kSurfaceH - 1.f));

    EXPECT_EQ(selection->Max(), (TextPos{2, kLines[2].size()}));
}

} // namespace
