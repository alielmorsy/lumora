#include <gtest/gtest.h>

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/SvgImage.h>

#include <set>
#include <vector>

using namespace lumora;

namespace {

class VertexCanvas final : public Canvas {
public:
    std::vector<PaintVertex> verts;
    int                      triangleCalls = 0;
    int                      pushClips     = 0;
    int                      popClips      = 0;

    void BeginFrame(Vec2) override {}
    void EndFrame() override {}
    void DrawRect(const RectCmd&) override {}
    void DrawShape(const ShapeCmd&) override {}
    void DrawGlyphs(std::span<const GlyphQuad>, AtlasKind, Color) override {}
    void DrawImage(ImageHandle, const Rect&, float, float) override {}
    void DrawTriangles(std::span<const PaintVertex> v) override {
        ++triangleCalls;
        verts.insert(verts.end(), v.begin(), v.end());
    }
    void PushClip(const Rect&, float) override { ++pushClips; }
    void PopClip() override { ++popClips; }
    void PushLayer(float, const float[9]) override {}
    void PopLayer() override {}
};

constexpr const char* kRect =
    "<svg viewBox='-50 -100 100 120'><rect x='-10' y='-10' width='20' height='20' fill='#f00'/></svg>";

} // namespace

TEST(SvgImageCanvas, ViewBoxParsed) {
    const Rect vb = SvgImage::FromText(kRect).ViewBox();
    EXPECT_FLOAT_EQ(vb.x, -50.f);
    EXPECT_FLOAT_EQ(vb.y, -100.f);
    EXPECT_FLOAT_EQ(vb.width, 100.f);
    EXPECT_FLOAT_EQ(vb.height, 120.f);
}

TEST(SvgImageCanvas, ViewBoxFallsBackToIntrinsic) {
    const Rect vb = SvgImage::FromText("<svg width='30' height='40'><rect width='5' height='5'/></svg>").ViewBox();
    EXPECT_FLOAT_EQ(vb.x, 0.f);
    EXPECT_FLOAT_EQ(vb.y, 0.f);
    EXPECT_FLOAT_EQ(vb.width, 30.f);
    EXPECT_FLOAT_EQ(vb.height, 40.f);
}

TEST(SvgImageCanvas, DrawEmitsGeometry) {
    const SvgImage img = SvgImage::FromText(kRect);
    VertexCanvas   canvas;
    img.Draw(canvas, {0.f, 0.f, 100.f, 120.f});
    ASSERT_GT(canvas.triangleCalls, 0);
    bool red = false;
    for (const PaintVertex& v : canvas.verts) {
        red = red || (v.col.r == 255 && v.col.g == 0 && v.col.b == 0);
    }
    EXPECT_TRUE(red);
    EXPECT_GE(canvas.pushClips, 1);
    EXPECT_EQ(canvas.pushClips, canvas.popClips);
}

TEST(SvgImageCanvas, GradientFill) {
    const SvgImage img = SvgImage::FromText(
        "<svg viewBox='0 0 100 100'><defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='0'>"
        "<stop offset='0' stop-color='#f00'/><stop offset='1' stop-color='#00f'/></linearGradient></defs>"
        "<path d='M0 0 L100 0 L100 100 L0 100 Z' fill='url(#g)'/></svg>");
    VertexCanvas canvas;
    img.Draw(canvas, {0.f, 0.f, 100.f, 100.f});
    std::set<std::uint32_t> colours;
    for (const PaintVertex& v : canvas.verts) {
        colours.insert(static_cast<std::uint32_t>(v.col.r) << 16 | static_cast<std::uint32_t>(v.col.g) << 8 | v.col.b);
    }
    EXPECT_GT(colours.size(), 1u);
}
