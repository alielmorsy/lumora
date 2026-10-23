#include <gtest/gtest.h>

#include "FakeFonts.h"

#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Paint/NullCanvas.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Widgets/Widgets.h>

#include <string>
#include <vector>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 1280.f;
constexpr float kSurfaceH = 720.f;
constexpr float kStep = 1.f / 60.f;

[[nodiscard]] Elem Tiles(int count, bool withText, const char* cls = nullptr) {
    std::vector<Elem> kids;
    kids.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (withText) {
            kids.push_back(Row(RowCfg{.layout = {.width = Expand, .height = Fixed(20.f)}},
                               Text("label", TextCfg{.size = 12.f})));
        } else {
            kids.push_back(Box(BoxCfg{.layout = {.width = Expand, .height = Fixed(20.f)},
                                      .style = {.fill = Color::Hex(0xFF202027)},
                                      .className = cls ? std::string(cls) : std::string{}}));
        }
    }
    return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, kids);
}

[[nodiscard]] Elem Groups(int groups, int perGroup, const Signal<Color>& fill) {
    std::vector<Elem> rows;
    rows.reserve(static_cast<std::size_t>(groups));
    for (int g = 0; g < groups; ++g) {
        std::vector<Elem> tiles;
        tiles.reserve(static_cast<std::size_t>(perGroup));
        for (int t = 0; t < perGroup; ++t) {
            BoxCfg cfg{};
            cfg.layout = {.width = Expand, .height = Fixed(8.f)};
            if (g == 0 && t == 0) {
                cfg.style.fill = fill;
            } else {
                cfg.style.fill = Color::Hex(0xFF202027);
            }
            tiles.push_back(Box(cfg));
        }
        rows.push_back(Column(ColumnCfg{.layout = {.width = Expand, .height = Wrap}}, tiles));
    }
    return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, rows);
}

FrameCounters Drive(MountedApp& app, int frames) {
    NullCanvas canvas;
    for (int i = 0; i < frames; ++i) {
        app.Tick(kStep);
        app.Layout(kSurfaceW, kSurfaceH);
        canvas.BeginFrame({kSurfaceW, kSurfaceH});
        app.Paint(canvas);
        canvas.EndFrame();
    }
    return app.Counters();
}

TEST(CountersTests, IdleFrameRunsNoSolver) {
    FakeFonts fonts;
    MountedApp app(Tiles(200, false), &fonts);
    const FrameCounters c = Drive(app, 4);

    EXPECT_EQ(c.solveCalls, 0u);
    EXPECT_EQ(c.readBackVisited, 0u);
    EXPECT_EQ(c.remeasureRepins, 0u);
    EXPECT_EQ(c.gridsPlaced, 0u);
    EXPECT_EQ(c.refreshDirty, 0u);

    EXPECT_EQ(c.cascadeCalls, 0u);

    EXPECT_EQ(c.textMeasureCalls, 0u);
    EXPECT_EQ(c.glyphCacheCalls, 0u);

    EXPECT_GT(c.refreshVisited, 200u);
    EXPECT_GE(c.layoutIdleBails, 1u);

    EXPECT_EQ(c.emitVisited, 0u);
    EXPECT_EQ(c.emitRecorded, 0u);
    EXPECT_EQ(c.emitReused, 0u);
    EXPECT_EQ(c.paintFrameReused, 1u);
    EXPECT_GT(c.paintReplayed, 0u);
}

TEST(CountersTests, IdleFrameIsSteady) {
    FakeFonts fonts;
    MountedApp app(Tiles(150, true), &fonts);
    const FrameCounters a = Drive(app, 4);
    const FrameCounters b = Drive(app, 1);

    EXPECT_EQ(a.refreshVisited, b.refreshVisited);
    EXPECT_EQ(a.emitVisited, b.emitVisited);
    EXPECT_EQ(a.solveCalls, b.solveCalls);
    EXPECT_EQ(a.cascadeCalls, b.cascadeCalls);
    EXPECT_EQ(a.textMeasureCalls, b.textMeasureCalls);
    EXPECT_EQ(a.glyphCacheCalls, b.glyphCacheCalls);
    EXPECT_EQ(a.frameCallbacksInvoked, b.frameCallbacksInvoked);
}

TEST(CountersTests, IdleCostScalesWithTreeSize) {
    FakeFonts fonts;
    MountedApp small(Tiles(100, false), &fonts);
    MountedApp large(Tiles(400, false), &fonts);
    const FrameCounters s = Drive(small, 4);
    const FrameCounters l = Drive(large, 4);

    EXPECT_GT(l.refreshVisited, s.refreshVisited * 3);
    EXPECT_EQ(s.emitVisited, 0u);
    EXPECT_EQ(l.emitVisited, 0u);
}

TEST(CountersTests, OneLeafWriteDoesNotRestyleTheTree) {
    FakeFonts fonts;
    const Signal<std::string> caption{std::string("a")};
    std::vector<Elem> kids;
    kids.push_back(Text(caption, TextCfg{.size = 12.f}));
    kids.push_back(Tiles(300, false));
    MountedApp app(Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, kids), &fonts);

    Drive(app, 4);
    app.RunScoped([caption] { caption = "bb"; });
    const FrameCounters c = Drive(app, 1);

    EXPECT_EQ(c.cascadeCalls, 0u);
    EXPECT_GE(c.refreshDirty, 1u);
    EXPECT_LE(c.refreshDirty, 4u);
    EXPECT_GE(c.textMeasureCalls, 1u);
    EXPECT_GE(c.glyphCacheCalls, 1u);
    EXPECT_GE(c.solveCalls, 1u);
    EXPECT_LE(c.solveCalls, 3u);
}

TEST(CountersTests, AGetterReturningTheSameValueDirtiesNothing) {
    FakeFonts fonts;
    const Signal<int> n{0};
    const Reactive<std::string> label([n] { return std::string(n() < 10 ? "low" : "high"); });
    MountedApp app(Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, Text(label, TextCfg{.size = 12.f})),
                   &fonts);
    Drive(app, 4);

    app.RunScoped([n] { n = 1; });
    const FrameCounters same = Drive(app, 1);
    EXPECT_EQ(same.refreshDirty, 0u);
    EXPECT_EQ(same.solveCalls, 0u);
    EXPECT_EQ(same.emitVisited, 0u);

    app.RunScoped([n] { n = 10; });
    const FrameCounters changed = Drive(app, 1);
    EXPECT_GE(changed.refreshDirty, 1u);
}

TEST(CountersTests, UnchangedTreeReEmitsNothing) {
    FakeFonts fonts;
    const Signal<Color> fill{Color::Hex(0xFF202027)};
    MountedApp app(Groups(20, 15, fill), &fonts);
    const FrameCounters a = Drive(app, 4);
    const FrameCounters b = Drive(app, 1);
    const FrameCounters c = Drive(app, 1);

    EXPECT_EQ(a.emitVisited, 0u);
    EXPECT_EQ(b.emitVisited, 0u);
    EXPECT_EQ(c.emitVisited, 0u);
    EXPECT_EQ(b.emitRecorded, 0u);
    EXPECT_EQ(b.paintFrameReused, 1u);
    EXPECT_EQ(b.paintReplayed, c.paintReplayed);
    EXPECT_EQ(b.paintRecordedBytes, c.paintRecordedBytes);
    EXPECT_EQ(b.textMeasureCalls, 0u);
    EXPECT_EQ(b.glyphCacheCalls, 0u);
}

TEST(CountersTests, OneChangedLeafReEmitsOnlyItsOwnSubtree) {
    FakeFonts fonts;
    const Signal<Color> fill{Color::Hex(0xFF202027)};
    constexpr int kGroups = 20;
    constexpr int kPerGroup = 15;
    MountedApp app(Groups(kGroups, kPerGroup, fill), &fonts);
    Drive(app, 4);

    app.RunScoped([fill] { fill = Color::Hex(0xFF3A80FF); });
    const FrameCounters c = Drive(app, 1);

    EXPECT_GT(c.emitVisited, 0u);
    EXPECT_LT(c.emitVisited, static_cast<std::uint32_t>(kGroups + kPerGroup + 40));

    EXPECT_GE(c.emitReused, static_cast<std::uint32_t>(kGroups + kPerGroup - 2));
    EXPECT_LE(c.emitRecorded, 12u);
    EXPECT_GT(c.emitReusedBytes, 0u);

    EXPECT_LE(c.cascadeCalls, 4u);
    EXPECT_EQ(c.textMeasureCalls, 0u);

    const FrameCounters after = Drive(app, 1);
    EXPECT_EQ(after.emitVisited, 0u);
    EXPECT_EQ(after.paintFrameReused, 1u);
}

TEST(CountersTests, PaintReuseOffStillWalksEverything) {
    FakeFonts fonts;
    const Signal<Color> fill{Color::Hex(0xFF202027)};
    MountedApp app(Groups(20, 15, fill), &fonts);
    app.SetPaintReuse(PaintReuse::Off);
    const FrameCounters c = Drive(app, 4);

    EXPECT_GT(c.emitVisited, 300u);
    EXPECT_EQ(c.paintFrameReused, 0u);
    EXPECT_EQ(c.emitReused, 0u);
    EXPECT_EQ(c.paintReplayed, 0u);
}

TEST(CountersTests, ASurfaceResizeRecordsTheWholeTree) {
    FakeFonts fonts;
    const Signal<Color> fill{Color::Hex(0xFF202027)};
    MountedApp app(Groups(10, 10, fill), &fonts);
    Drive(app, 4);

    NullCanvas canvas;
    app.Tick(kStep);
    app.Layout(kSurfaceW * 0.5f, kSurfaceH);
    canvas.BeginFrame({kSurfaceW * 0.5f, kSurfaceH});
    app.Paint(canvas);
    canvas.EndFrame();
    const FrameCounters c = app.Counters();

    EXPECT_GT(c.emitVisited, 100u);
    EXPECT_EQ(c.emitReused, 0u);
    EXPECT_EQ(c.paintFrameReused, 0u);
}

TEST(CountersTests, AHoverRepaintsTheHoveredNodeOnly) {
    FakeFonts fonts;
    MountedApp app(Tiles(200, false, "tile"), &fonts);
    app.SetStylesheetSource(".tile { background-color: #202024; }\n"
                            ".tile:hover { background-color: #3a80ff; }\n");
    Drive(app, 4);

    Event move{};
    move.Type = EventType::MouseMove;
    move.Position = Vec2{12.f, 44.f};
    app.Dispatch(move);
    const FrameCounters c = Drive(app, 1);

    EXPECT_GE(c.emitRecorded, 1u);
    EXPECT_GT(c.emitVisited, 0u);
    EXPECT_GE(c.emitReused, 150u);
}

TEST(CountersTests, NothingAnimatingCostsNoFrameCallbacks) {
    FakeFonts fonts;
    MountedApp app(Tiles(200, false), &fonts);
    app.SetStylesheetSource(".x { color: #fff; }");
    const FrameCounters c = Drive(app, 4);

    EXPECT_EQ(c.frameCallbacksInvoked, 1u);
    EXPECT_EQ(c.frameCallbacksAlive, 1u);
    EXPECT_EQ(c.framePasses, 1u);
}

TEST(CountersTests, AHoverTransitionAdvancesAndThenStops) {
    FakeFonts fonts;
    MountedApp app(Tiles(20, false, "tile"), &fonts);
    app.SetStylesheetSource(".tile { background-color: #202024; transition: background-color 100ms linear; }\n"
                            ".tile:hover { background-color: #3a80ff; }\n");
    Drive(app, 4);

    Event move{};
    move.Type = EventType::MouseMove;
    move.Position = Vec2{12.f, 44.f};
    app.Dispatch(move);
    const FrameCounters hovering = Drive(app, 1);
    EXPECT_GE(hovering.pseudoWrites, 1u);

    Drive(app, 30);
    const FrameCounters settled = Drive(app, 1);
    EXPECT_EQ(settled.frameCallbacksInvoked, 1u);
}

TEST(CountersTests, DisposedNodeQueuedForFlushDoesNotCorruptTheQueue) {
    FakeFonts fonts;
    const Signal<bool> open{true};
    const Signal<std::string> caption{std::string("x")};
    MountedApp app(Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}},
                          Show(open, [caption] { return Text(caption, TextCfg{.size = 12.f}); })),
                   &fonts);
    Drive(app, 3);

    app.RunScoped([open, caption] {
        open = false;
        caption = "y";
    });
    const FrameCounters c = Drive(app, 1);
    EXPECT_GT(c.emitVisited, 0u);
}

TEST(CountersTests, FrameCallbackRegisteredFromInsideATickIsSafe) {
    FakeFonts fonts;
    MountedApp app(Box(BoxCfg{.layout = {.width = Expand, .height = Expand}}), &fonts);
    Drive(app, 2);

    int fired = 0;
    app.RunScoped([&fired] {
        OnFrame([&fired](float) {
            ++fired;
            for (int i = 0; i < 64; ++i) {
                OnFrame([](float) {});
            }
        });
    });
    Drive(app, 2);
    EXPECT_GE(fired, 1);
}

} // namespace
