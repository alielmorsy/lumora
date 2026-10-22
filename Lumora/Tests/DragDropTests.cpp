#include <gtest/gtest.h>

#include <lumora/Input/DragDrop.h>
#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Widgets.h>

#include "FakeFonts.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace lumora;

namespace {

constexpr float kSurfaceW = 400.f;
constexpr float kSurfaceH = 300.f;

constexpr float kSourceX = 20.f, kSourceY = 20.f;
constexpr float kTargetX = 220.f, kTargetY = 20.f;

[[nodiscard]] Event Mouse(EventType type, float x, float y) {
    Event ev{};
    ev.Type     = type;
    ev.Position = {x, y};
    ev.Button   = PointerButton::Left;
    return ev;
}

[[nodiscard]] Event EscapeKey() {
    Event ev{};
    ev.Type    = EventType::KeyDown;
    ev.KeyCode = Key::Escape;
    return ev;
}

void Frame(MountedApp& app) {
    app.Tick(1.f / 60.f);
    app.Layout(kSurfaceW, kSurfaceH);
}

struct Probe {
    int         clicks     = 0;
    int         releases   = 0;
    int         drops      = 0;
    int         entered    = 0;
    int         left       = 0;
    int         accepts    = 0;
    std::string droppedValue;
    std::string droppedOn;
};

[[nodiscard]] BoxCfg Placed(float x, float y, float w, float h) {
    return BoxCfg{.layout   = {.width = Fixed(w), .height = Fixed(h)},
                  .position = {.position = Position::Absolute, .inset = {.left = x, .top = y}}};
}

[[nodiscard]] Elem Source(Probe& p, std::string value) {
    BoxCfg cfg = Placed(kSourceX, kSourceY, 100.f, 60.f);
    cfg.on.onClick     = [&p] { ++p.clicks; };
    cfg.on.onDragStart = [value] {
        return std::optional<DragPayload>{DragPayload{lumora::StringId("asset"), value}};
    };
    return Box(std::move(cfg));
}

[[nodiscard]] BoxCfg TargetCfg(Probe& p, std::string name, const char* kind, float x, float y,
                               float w = 100.f, float h = 60.f) {
    BoxCfg cfg = Placed(x, y, w, h);
    const lumora::StringId accepted{kind};
    cfg.on.dropAccepts = [&p, accepted](const DragPayload& d) {
        ++p.accepts;
        return d.Is(accepted);
    };
    cfg.on.onDrop = [&p, name](const DragPayload& d, PointerInfo) {
        ++p.drops;
        p.droppedValue = d.value;
        p.droppedOn    = name;
    };
    cfg.on.onDragOver = [&p](const DragPayload&, bool in) { in ? ++p.entered : ++p.left; };
    return cfg;
}

[[nodiscard]] Elem Root(Elem a, Elem b) {
    return Stack(StackCfg{.layout = {.width = Expand, .height = Expand}, .anchor = true},
                 std::move(a), std::move(b));
}

TEST(DragDropTests, PressBelowThresholdStaysAClick) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "mesh.dmesh"), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 12.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kSourceX + 12.f, kSourceY + 10.f));
    Frame(app);

    EXPECT_EQ(p.clicks, 1);
    EXPECT_EQ(p.drops, 0);
    EXPECT_EQ(p.entered, 0);
}

TEST(DragDropTests, PastThresholdDropsOnTheTarget) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "mesh.dmesh"), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    EXPECT_EQ(p.entered, 1);
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 1);
    EXPECT_EQ(p.droppedValue, "mesh.dmesh");
    EXPECT_EQ(p.droppedOn, "slot");
    EXPECT_EQ(p.clicks, 0);
}

TEST(DragDropTests, ARefusedPayloadNeverDrops) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "cube"), Box(TargetCfg(p, "slot", "entity", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_GT(p.accepts, 0);
    EXPECT_EQ(p.entered, 0);
    EXPECT_EQ(p.drops, 0);
}

TEST(DragDropTests, ARefusingChildDoesNotBlockTheAcceptingParent) {
    Probe p;
    FakeFonts fonts;
    BoxCfg row  = TargetCfg(p, "row", "asset", kTargetX, kTargetY, 100.f, 60.f);
    BoxCfg chip = TargetCfg(p, "chip", "entity", 0.f, 0.f, 40.f, 20.f);
    chip.position = {};
    Elem target = Box(std::move(row), Box(std::move(chip)));

    MountedApp app(Root(Source(p, "wood.mat"), std::move(target)), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 1);
    EXPECT_EQ(p.droppedOn, "row");
}

TEST(DragDropTests, EscapeMidDragFiresNoDrop) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "mesh.dmesh"), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    EXPECT_EQ(p.entered, 1);
    EXPECT_TRUE(app.Dispatch(EscapeKey()));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 0);
    EXPECT_EQ(p.left, 1);
}

TEST(DragDropTests, DroppingOnNothingEndsTheDrag) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "mesh.dmesh"), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kSourceX + 60.f, kSurfaceH - 10.f));
    Frame(app);
    EXPECT_EQ(p.drops, 0);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kSourceX + 10.f, kSourceY + 10.f));
    Frame(app);
    EXPECT_EQ(p.clicks, 1);
}

TEST(DragDropTests, EnterAndLeaveFireOncePerTarget) {
    Probe p;
    FakeFonts fonts;
    MountedApp app(Root(Source(p, "mesh.dmesh"), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 5.f, kTargetY + 5.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 25.f, kTargetY + 25.f));
    EXPECT_EQ(p.entered, 1);
    EXPECT_EQ(p.left, 0);
    app.Dispatch(Mouse(EventType::MouseMove, kSurfaceW - 5.f, kSurfaceH - 5.f));
    EXPECT_EQ(p.left, 1);
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 5.f, kTargetY + 5.f));
    EXPECT_EQ(p.entered, 2);
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 5.f, kTargetY + 5.f));
    Frame(app);
    EXPECT_EQ(p.drops, 1);
}

TEST(DragDropTests, TheGhostIsNeverTheDropTarget) {
    Probe p;
    FakeFonts fonts;
    Probe ghostProbe;
    MountedApp app(Root(Source(p, "mesh.dmesh"),
                        Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.SetDragPreview([&ghostProbe](const DragPayload&) {
        return Box(TargetCfg(ghostProbe, "ghost", "asset", 0.f, 0.f, kSurfaceW, kSurfaceH));
    });
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    Frame(app);
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 1);
    EXPECT_EQ(p.droppedOn, "slot");
    EXPECT_EQ(ghostProbe.drops, 0);
    EXPECT_EQ(ghostProbe.accepts, 0);
}

TEST(DragDropTests, ADisabledSourceDoesNotDrag) {
    Probe  p;
    FakeFonts fonts;
    BoxCfg cfg = Placed(kSourceX, kSourceY, 100.f, 60.f);
    cfg.on.onDragStart = [] { return std::optional<DragPayload>{DragPayload{lumora::StringId("asset"), "x"}}; };
    cfg.on.disabled    = true;

    MountedApp app(Root(Box(std::move(cfg)), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 0);
    EXPECT_EQ(p.entered, 0);
}

TEST(DragDropTests, AReleaseOnlyRowActsOnClicksButNotOnDrags) {
    Probe     p;
    FakeFonts fonts;

    BoxCfg cfg         = Placed(kSourceX, kSourceY, 100.f, 60.f);
    cfg.on.onRelease   = [&p](PointerInfo) { ++p.releases; };
    cfg.on.onDragStart = [] {
        return std::optional<DragPayload>{DragPayload{lumora::StringId("asset"), "row"}};
    };

    MountedApp app(Root(Box(std::move(cfg)), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kSourceX + 10.f, kSourceY + 10.f));
    Frame(app);
    EXPECT_EQ(p.releases, 1);
    EXPECT_EQ(p.drops, 0);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kSourceX + 40.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);
    EXPECT_EQ(p.drops, 1);
    EXPECT_EQ(p.droppedOn, "slot");
    EXPECT_EQ(p.releases, 1);
}

TEST(DragDropTests, ASourceThatDeclinesProducesNoDrag) {
    Probe  p;
    FakeFonts fonts;
    BoxCfg cfg = Placed(kSourceX, kSourceY, 100.f, 60.f);
    cfg.on.onClick     = [&p] { ++p.clicks; };
    cfg.on.onDragStart = [] { return std::optional<DragPayload>{}; };

    MountedApp app(Root(Box(std::move(cfg)), Box(TargetCfg(p, "slot", "asset", kTargetX, kTargetY))),
                   &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    app.Dispatch(Mouse(EventType::MouseDown, kSourceX + 10.f, kSourceY + 10.f));
    app.Dispatch(Mouse(EventType::MouseMove, kTargetX + 10.f, kTargetY + 10.f));
    app.Dispatch(Mouse(EventType::MouseUp, kTargetX + 10.f, kTargetY + 10.f));
    Frame(app);

    EXPECT_EQ(p.drops, 0);
    EXPECT_EQ(p.entered, 0);
}

TEST(DragDropTests, DropExternalReachesInnermostAcceptingTarget) {
    Probe p;
    FakeFonts fonts;

    BoxCfg parentCfg = TargetCfg(p, "parent", "os.files", 0.f, 0.f, 200.f, 200.f);
    BoxCfg childCfg = TargetCfg(p, "child", "os.files", 20.f, 20.f, 60.f, 60.f);
    BoxCfg declinerCfg = TargetCfg(p, "decliner", "entity", 100.f, 20.f, 60.f, 60.f);

    Elem tree = Box(std::move(parentCfg), Box(std::move(childCfg)), Box(std::move(declinerCfg)));
    MountedApp app(std::move(tree), &fonts);
    app.Layout(kSurfaceW, kSurfaceH);

    const DragPayload osPayload{DragKindOsFiles, "C:/path/file.txt"};

    const bool drop1 = app.DropExternal(osPayload, Vec2{30.f, 30.f});
    Frame(app);
    EXPECT_TRUE(drop1);
    EXPECT_EQ(p.drops, 1);
    EXPECT_EQ(p.droppedOn, "child");
    EXPECT_EQ(p.droppedValue, "C:/path/file.txt");

    const bool drop2 = app.DropExternal(osPayload, Vec2{110.f, 30.f});
    Frame(app);
    EXPECT_TRUE(drop2);
    EXPECT_EQ(p.drops, 2);
    EXPECT_EQ(p.droppedOn, "parent");

    const bool drop3 = app.DropExternal(osPayload, Vec2{300.f, 250.f});
    Frame(app);
    EXPECT_FALSE(drop3);
    EXPECT_EQ(p.drops, 2);
}

TEST(DragDropTests, DropExternalAtContentScaleTwoUsesLogicalCoordinates) {
    Probe     p;
    FakeFonts fonts;

    Elem tree = Box(TargetCfg(p, "parent", "os.files", 0.f, 0.f, 200.f, 200.f),
                    Box(TargetCfg(p, "child", "os.files", 20.f, 20.f, 60.f, 60.f)));
    MountedApp app(std::move(tree), &fonts);
    app.SetContentScale(2.f);
    app.Layout(kSurfaceW, kSurfaceH);

    const DragPayload osPayload{DragKindOsFiles, "C:/a.png"};

    EXPECT_TRUE(app.DropExternal(osPayload, PhysicalToLogical(Vec2{100.f, 100.f}, 2.f)));
    Frame(app);
    EXPECT_EQ(p.droppedOn, "child");

    EXPECT_TRUE(app.DropExternal(osPayload, PhysicalToLogical(Vec2{30.f, 30.f}, 2.f)));
    Frame(app);
    EXPECT_EQ(p.droppedOn, "parent");

    EXPECT_EQ(PhysicalToLogical(Vec2{10.f, 20.f}, 0.f).x, 10.f);
}

TEST(DragDropTests, OsFilesSplitAndJoinRoundTrip) {
    const std::vector<std::string_view> split = SplitOsFiles("C:/a.png\n\nC:/b c.fbx\n");
    ASSERT_EQ(split.size(), 2u);
    EXPECT_EQ(split[0], "C:/a.png");
    EXPECT_EQ(split[1], "C:/b c.fbx");
    EXPECT_TRUE(SplitOsFiles("").empty());
    EXPECT_TRUE(SplitOsFiles("\n\n").empty());
    EXPECT_EQ(SplitOsFiles("single").size(), 1u);

    const std::vector<std::string> paths{"C:/a.png", "", "C:/b c.fbx"};
    const std::string joined = JoinOsFiles(paths);
    EXPECT_EQ(joined, "C:/a.png\nC:/b c.fbx");
    EXPECT_EQ(SplitOsFiles(joined).size(), 2u);
    EXPECT_TRUE(JoinOsFiles({}).empty());
}

} // namespace
