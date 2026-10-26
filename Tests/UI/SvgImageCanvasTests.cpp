#include <gtest/gtest.h>

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/SvgImage.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
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

namespace {

// Every fill edge must be shared by exactly two triangles; a lone edge inside the shape is a T-junction, which rasterizes
// as pinhole cracks. Only the feather ribbon's outer rim (alpha 0 at both ends) is a true border.
int OpenFillEdges(const std::vector<PaintVertex>& verts) {
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    const auto bits = [](Vec2 p) {
        std::uint32_t x, y;
        std::memcpy(&x, &p.x, 4);
        std::memcpy(&y, &p.y, 4);
        return static_cast<std::uint64_t>(x) << 32 | y;
    };
    std::map<Key, int> edges;
    for (std::size_t t = 0; t + 2 < verts.size(); t += 3) {
        for (int e = 0; e < 3; ++e) {
            const PaintVertex& a = verts[t + e];
            const PaintVertex& b = verts[t + (e + 1) % 3];
            if (a.col.a == 0 && b.col.a == 0) {
                continue;
            }
            const std::uint64_t ka = bits(a.pos), kb = bits(b.pos);
            ++edges[{std::min(ka, kb), std::max(ka, kb)}];
        }
    }
    int open = 0;
    for (const auto& [k, n] : edges) {
        open += n != 2;
    }
    return open;
}

constexpr const char* kWax =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="-176 -172 352 352"><defs><radialGradient id="g0" gradientUnits="userSpaceOnUse" cx="-50" cy="-60" r="260"><stop offset="0" stop-color="#b5233a"/><stop offset="0.5" stop-color="#8e1424"/><stop offset="1" stop-color="#5e0b17"/></radialGradient></defs><path d="M 0 -170 C 95 -175 168 -100 172 -10 C 176 80 110 165 5 172 C -90 178 -170 110 -174 10 C -178 -85 -100 -165 0 -170 Z" fill="url(#g0)"/></svg>)svg";

} // namespace

TEST(SvgImageCanvas, SolidFillMeshIsWatertight) {
    const SvgImage img = SvgImage::FromText(
        "<svg viewBox='-176 -172 352 352'><path d='M 0 -170 C 95 -175 168 -100 172 -10 C 176 80 110 165 5 172 "
        "C -90 178 -170 110 -174 10 C -178 -85 -100 -165 0 -170 Z' fill='#b5233a'/></svg>");
    VertexCanvas canvas;
    img.Draw(canvas, img.ViewBox(), 1.f, 1.06f);
    ASSERT_GT(canvas.verts.size(), 0u);
    EXPECT_EQ(OpenFillEdges(canvas.verts), 0);
}

TEST(SvgImageCanvas, GradientFillMeshIsWatertight) {
    const SvgImage img = SvgImage::FromText(kWax);
    for (const float scale : {0.5f, 1.f, 1.06f, 2.3f}) {
        VertexCanvas canvas;
        img.Draw(canvas, img.ViewBox(), 1.f, scale);
        ASSERT_GT(canvas.verts.size(), 0u);
        EXPECT_EQ(OpenFillEdges(canvas.verts), 0) << "scale " << scale;
    }
}
