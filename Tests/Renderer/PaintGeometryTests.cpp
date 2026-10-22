#include <gtest/gtest.h>

#include "BatchBuilder.h"
#include "Runtime/Paint.h"

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/Paint2D.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace lumora;

namespace {

constexpr float kPi = 3.14159265f;

// Keeps every DrawTriangles vertex; everything else is dropped.
class TriCanvas final : public Canvas {
public:
    std::vector<PaintVertex> verts;

    void BeginFrame(Vec2) override {}
    void EndFrame() override {}
    void DrawRect(const RectCmd&) override {}
    void DrawShape(const ShapeCmd&) override {}
    void DrawGlyphs(std::span<const GlyphQuad>, AtlasKind, Color) override {}
    void DrawImage(ImageHandle, const Rect&, float, float) override {}
    void DrawTriangles(std::span<const PaintVertex> v) override { verts.insert(verts.end(), v.begin(), v.end()); }
    void PushClip(const Rect&, float) override {}
    void PopClip() override {}
    void PushLayer(float, const float*) override {}
    void PopLayer() override {}
};

float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

bool InTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const float d0 = Cross(b - a, p - a), d1 = Cross(c - b, p - b), d2 = Cross(a - c, p - c);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

struct Fill {
    float solidArea = 0.f;
    bool  covers    = false;
};

// Solid (full-alpha) triangle area, and whether any triangle covers `probe`.
Fill FillOf(std::span<const Vec2> poly, Vec2 probe) {
    PaintContext ctx;
    TriCanvas    canvas;
    Paint2D      p(ctx, canvas, {0.f, 0.f, 100.f, 100.f}, {});
    p.FillPolygon(poly, Color::Red);
    Fill f;
    const auto& v = canvas.verts;
    for (std::size_t i = 0; i + 2 < v.size(); i += 3) {
        if (v[i].col.a == 255 && v[i + 1].col.a == 255 && v[i + 2].col.a == 255)
            f.solidArea += std::abs(Cross(v[i + 1].pos - v[i].pos, v[i + 2].pos - v[i].pos)) * 0.5f;
        f.covers = f.covers || InTri(probe, v[i].pos, v[i + 1].pos, v[i + 2].pos);
    }
    return f;
}

// CPU port of sdPie in Renderer/Shaders/LumoraSDF.hlsl.
float SdPie(Vec2 p, Vec2 c, float r) {
    p.x = std::abs(p.x);
    const float l = p.Length() - r;
    const float t = std::clamp(p.x * c.x + p.y * c.y, 0.f, r);
    const float m = (p - c * t).Length();
    const float s = c.y * p.x - c.x * p.y;
    return std::max(l, m * (s > 0.f ? 1.f : (s < 0.f ? -1.f : 0.f)));
}

} // namespace

TEST(PaintGeometryTests, ConcaveLFillsExactArea) {
    // Started at (20,8) so a naive fan from vertex 0 would cover the notch.
    const std::array<Vec2, 6> l{{{20, 8}, {8, 8}, {8, 20}, {0, 20}, {0, 0}, {20, 0}}};
    const Fill f = FillOf(l, {11.f, 11.f});
    EXPECT_NEAR(f.solidArea, 256.f, 0.01f);
    EXPECT_FALSE(f.covers);
}

TEST(PaintGeometryTests, CrescentLeavesBiteEmpty) {
    // Outer circle (50,50) r20 from -60 to 240 degrees, then back along a bite circle through both tips.
    std::vector<Vec2> pts;
    for (int i = 0; i < 32; ++i) {
        const float a = (-60.f + 300.f * static_cast<float>(i) / 32.f) * kPi / 180.f;
        pts.push_back({50.f + std::cos(a) * 20.f, 50.f + std::sin(a) * 20.f});
    }
    const Vec2  bc{50.f, 30.f};
    const Vec2  tipL{50.f + std::cos(240.f * kPi / 180.f) * 20.f, 50.f + std::sin(240.f * kPi / 180.f) * 20.f};
    const float br = (tipL - bc).Length();
    const float a0 = std::atan2(tipL.y - bc.y, tipL.x - bc.x), a1 = kPi - a0;
    for (int i = 0; i < 32; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / 32.f;
        pts.push_back({bc.x + std::cos(a) * br, bc.y + std::sin(a) * br});
    }
    float area2 = 0.f;
    for (std::size_t i = 0; i < pts.size(); ++i)
        area2 += Cross(pts[i], pts[(i + 1) % pts.size()]);
    const Fill f = FillOf(pts, {50.f, 35.f}); // inside the bite
    EXPECT_FALSE(f.covers);
    EXPECT_NEAR(f.solidArea, std::abs(area2) * 0.5f, 0.5f);
}

TEST(PaintGeometryTests, ConvexSquareFastPath) {
    const std::array<Vec2, 4> sq{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    EXPECT_NEAR(FillOf(sq, {}).solidArea, 100.f, 0.01f);
}

TEST(PaintGeometryTests, ArcAnglesRunClockwiseOnScreen) {
    // A narrow pie centred on pi/2 must cover the point below the centre (y-down), not above.
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddShape({.center = {50.f, 50.f}, .radius = 10.f, .kind = ShapeKind::Pie, .color = Color::Red,
                .startAngle = kPi * 0.5f - 0.4f, .sweepAngle = 0.8f});
    b.End();
    ASSERT_EQ(b.Batches().size(), 1u);
    const Batch& batch = b.Batches()[0];
    const auto   v     = b.Vertices();
    const float  h     = batch.sdfHalf.x;
    // uv is linear in the screen offset: recover its two columns from the quad corners.
    const Vec2 ex = (v[1].uv - v[0].uv) / (2.f * h), ey = (v[3].uv - v[0].uv) / (2.f * h);
    const auto sd = [&](Vec2 o) { return SdPie(ex * o.x + ey * o.y, batch.sdfApertureSinCos, batch.sdfRadius); };
    EXPECT_LT(sd({0.f, 6.f}), 0.f);  // below centre: inside
    EXPECT_GT(sd({0.f, -6.f}), 0.f); // above: outside
    EXPECT_GT(sd({6.f, 0.f}), 0.f);  // right (angle 0): outside
}
