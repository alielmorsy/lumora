#include <gtest/gtest.h>

#include "Widgets/ImageFit.h"
#include <lumora/Elem.h>
#include <lumora/Paint/ImageProvider.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Image.h>

#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"

#include "RecordingCanvas.h"

#include <cmath>
#include <string>
#include <unordered_map>

using namespace lumora;

namespace {

bool RectEq(const Rect& a, float x, float y, float w, float h) {
    constexpr float e = 0.01f;
    return std::fabs(a.x - x) < e && std::fabs(a.y - y) < e &&
           std::fabs(a.width - w) < e && std::fabs(a.height - h) < e;
}

TEST(ImageTests, FillReturnsContent) {
    EXPECT_TRUE(RectEq(ImageFitRect(ImageFit::Fill, {0, 0, 400, 300}, {200, 100}), 0, 0, 400, 300));
}

TEST(ImageTests, ContainFitsAndCenters) {
    EXPECT_TRUE(RectEq(ImageFitRect(ImageFit::Contain, {0, 0, 400, 300}, {200, 100}), 0, 50, 400, 200));
}

TEST(ImageTests, CoverFillsAndOverflows) {
    EXPECT_TRUE(RectEq(ImageFitRect(ImageFit::Cover, {0, 0, 400, 300}, {200, 100}), -100, 0, 600, 300));
}

TEST(ImageTests, NoneIsNaturalCentered) {
    EXPECT_TRUE(RectEq(ImageFitRect(ImageFit::None, {0, 0, 400, 300}, {200, 100}), 100, 100, 200, 100));
}

TEST(ImageTests, DegenerateImageReturnsContent) {
    EXPECT_TRUE(RectEq(ImageFitRect(ImageFit::Contain, {10, 20, 400, 300}, {0, 0}), 10, 20, 400, 300));
}

TEST(ImageTests, ClipsForCoverAndNoneOnly) {
    EXPECT_TRUE(ImageFitClips(ImageFit::Cover));
    EXPECT_TRUE(ImageFitClips(ImageFit::None));
    EXPECT_FALSE(ImageFitClips(ImageFit::Fill));
    EXPECT_FALSE(ImageFitClips(ImageFit::Contain));
}

struct FakeProvider final : ImageProvider {
    std::unordered_map<std::string, int> acquires;
    std::unordered_map<std::string, int> releases;
    int live = 0;

    ImageHandle Acquire(std::string_view src) override {
        const std::string key{src};
        ++acquires[key];
        ++live;
        return ImageHandle{.id = 7, .width = 200.f, .height = 100.f};
    }
    void Release(std::string_view src) override {
        ++releases[std::string{src}];
        --live;
    }
};

const CanvasCall* FirstImage(const RecordingCanvas& c) {
    for (const CanvasCall& call : c.calls) {
        if (call.op == CanvasCall::Op::Image) {
            return &call;
        }
    }
    return nullptr;
}

TEST(ImageTests, SrcResolvesAndDraws) {
    auto graph = lumora::MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};
    FakeProvider provider;
    ctx.images = &provider;

    {
        Owner owner;
        Elem e = Image({.src = std::string{"cat.png"}, .fit = ImageFit::Fill, .opacity = 0.5f});
        NodeId root = Instantiate(ctx, *e.RawNode(), NodeId{}, owner);

        RecordingCanvas canvas;
        lumora::Paint(ctx, root, canvas);

        const CanvasCall* img = FirstImage(canvas);
        ASSERT_NE(img, nullptr);
        EXPECT_EQ(img->image.id, 7u);
        EXPECT_NEAR(img->opacity, 0.5f, 0.01f);
        EXPECT_EQ(provider.acquires["cat.png"], 1);
    }
    EXPECT_EQ(provider.releases["cat.png"], 1);
    EXPECT_EQ(provider.live, 0);
}

TEST(ImageTests, HandleWinsOverSrc) {
    auto graph = lumora::MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};
    FakeProvider provider;
    ctx.images = &provider;

    {
        Owner owner;
        Elem e = Image({.src    = std::string{"cat.png"},
                        .handle = ImageHandle{.id = 42, .width = 10.f, .height = 10.f},
                        .fit    = ImageFit::Fill});
        NodeId root = Instantiate(ctx, *e.RawNode(), NodeId{}, owner);

        RecordingCanvas canvas;
        lumora::Paint(ctx, root, canvas);

        const CanvasCall* img = FirstImage(canvas);
        ASSERT_NE(img, nullptr);
        EXPECT_EQ(img->image.id, 42u);
        EXPECT_TRUE(provider.acquires.find("cat.png") == provider.acquires.end());
    }
}

TEST(ImageTests, HeadlessDrawsNothing) {
    auto graph = lumora::MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    NodeArena arena;
    AppCtx ctx{arena, *graph};

    Owner owner;
    Elem e = Image({.src = std::string{"cat.png"}});
    NodeId root = Instantiate(ctx, *e.RawNode(), NodeId{}, owner);

    RecordingCanvas canvas;
    lumora::Paint(ctx, root, canvas);
    EXPECT_EQ(FirstImage(canvas), nullptr);
}

} // namespace
