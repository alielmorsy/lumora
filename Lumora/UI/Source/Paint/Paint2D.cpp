#include <lumora/Paint/Paint2D.h>

#include "Runtime/Paint.h"
#include "Svg/SvgTessellate.h"
#include "Widgets/TextEmit.h"

#include <lumora/Foundation/Geometry.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Widgets/Props.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lumora
{
    namespace
    {
        constexpr float kHalfPi    = 1.57079632679489661923f;
        constexpr float kFringe    = 1.0f;  ///< AA feather width in physical px
        constexpr float kMiterLim  = 8.0f;  ///< clamp the per-vertex miter so sharp corners don't spike
        constexpr float kBezierTol = 0.25f; ///< max flattening error in px
        constexpr int   kMaxBezierDepth = 16;

        [[nodiscard]] constexpr float Dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
        [[nodiscard]] constexpr Vec2  Perp(Vec2 v) noexcept { return {-v.y, v.x}; }
        [[nodiscard]] constexpr float Cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
        [[nodiscard]] constexpr Vec2  Lerp(Vec2 a, Vec2 b, float t) noexcept { return a + (b - a) * t; }

        [[nodiscard]] Vec2 SafeDir(Vec2 from, Vec2 to) noexcept
        {
            const Vec2  d = to - from;
            const float l = d.Length();
            return l > 1e-6f ? d / l : Vec2{};
        }

        [[nodiscard]] Color WithCoverage(Color c, float cov) noexcept
        {
            float a = static_cast<float>(c.a) * cov;
            a = std::clamp(a, 0.f, 255.f);
            return c.WithAlpha(static_cast<std::uint8_t>(a + 0.5f));
        }

        void PushTri(std::vector<PaintVertex>& o, PaintVertex a, PaintVertex b, PaintVertex c)
        {
            o.push_back(a);
            o.push_back(b);
            o.push_back(c);
        }

        void PushQuad(std::vector<PaintVertex>& o, PaintVertex a, PaintVertex b, PaintVertex c, PaintVertex d)
        {
            PushTri(o, a, b, c);
            PushTri(o, a, c, d);
        }

        /// A stroke's coverage profile: full alpha to `inner`, ramping to zero at `outer` (half-widths). A sub-pixel stroke
        /// fades the whole line instead, so a hairline stays a faint ~1px feather rather than flickering.
        struct StrokeProfile
        {
            float inner;
            float outer;
            float alphaScale;
        };

        [[nodiscard]] StrokeProfile ProfileFor(float width) noexcept
        {
            const float half = width * 0.5f;
            if (half >= kFringe)
            {
                return {half - kFringe, half, 1.f};
            }
            return {0.f, kFringe, width > 0.f ? half / kFringe : 0.f};
        }

        [[nodiscard]] int CapSegments(float radius) noexcept
        {
            const int s = static_cast<int>(radius * 0.75f) + 2;
            return std::clamp(s, 3, 24);
        }

        /// A feathered annular fan from `a0` to `a1` about `c`: solid wedges to `inner`, a fringe ring fading to zero at `outer`.
        void EmitFan(std::vector<PaintVertex>& out, Vec2 c, float a0, float a1, float inner, float outer,
                     Color cIn, Color cOut, int segs)
        {
            for (int i = 0; i < segs; ++i)
            {
                const float t0 = a0 + (a1 - a0) * (static_cast<float>(i) / static_cast<float>(segs));
                const float t1 = a0 + (a1 - a0) * (static_cast<float>(i + 1) / static_cast<float>(segs));
                const Vec2  d0{std::cos(t0), std::sin(t0)};
                const Vec2  d1{std::cos(t1), std::sin(t1)};
                PushTri(out, {c, cIn}, {c + d0 * inner, cIn}, {c + d1 * inner, cIn});
                PushQuad(out, {c + d0 * inner, cIn}, {c + d0 * outer, cOut}, {c + d1 * outer, cOut},
                         {c + d1 * inner, cIn});
            }
        }

        /// The four extruded points at one polyline vertex: inner/outer on the left (+normal) and right
        /// (-normal) sides, offset along the clamped miter so the ribbon stays gap-free across joins.
        struct StrokePointSet
        {
            Vec2 inL, outL, inR, outR;
        };

        void StrokePolyline(std::vector<PaintVertex>& out, std::span<const Vec2> input, Color color,
                            float width, bool closed, LineJoin join, LineCap cap)
        {
            // A zero-length segment has no direction, which collapses the neighbouring miter to kMiterLim.
            std::vector<Vec2> unique;
            unique.reserve(input.size());
            for (const Vec2 p : input)
            {
                if (unique.empty() || (p - unique.back()).Length() > 1e-6f)
                {
                    unique.push_back(p);
                }
            }
            if (closed && unique.size() > 1 && (unique.back() - unique.front()).Length() <= 1e-6f)
            {
                unique.pop_back();
            }
            const std::span<const Vec2> pts = unique;

            const int n = static_cast<int>(pts.size());
            if (n < 2)
            {
                return;
            }

            const StrokeProfile prof     = ProfileFor(width);
            const Color         cIn      = WithCoverage(color, prof.alphaScale);
            const Color         cOut     = WithCoverage(color, 0.f);
            const float         miterLim = (join == LineJoin::Miter) ? kMiterLim : 1.f;
            const int           segCount = closed ? n : n - 1;

            const auto edgeDir = [&](int k) -> Vec2 { return SafeDir(pts[k % n], pts[(k + 1) % n]); };

            const auto pointSet = [&](int j) -> StrokePointSet {
                Vec2 dPrev, dNext;
                if (closed)
                {
                    dPrev = edgeDir((j - 1 + n) % n);
                    dNext = edgeDir(j % n);
                }
                else
                {
                    dPrev = (j > 0) ? edgeDir(j - 1) : edgeDir(0);
                    dNext = (j < n - 1) ? edgeDir(j) : edgeDir(n - 2);
                }
                const Vec2 nNext = Perp(dNext);
                Vec2       m     = Perp(dPrev) + nNext;
                const float ml   = m.Length();
                float       scale = 1.f;
                if (ml > 1e-6f)
                {
                    m              = m / ml;
                    const float d  = Dot(m, nNext);
                    scale          = d > 1e-3f ? 1.f / d : miterLim;
                    scale          = std::min(scale, miterLim);
                }
                else
                {
                    m = nNext;
                }
                const Vec2     P = pts[j % n];
                StrokePointSet s;
                s.inL  = P + m * (prof.inner * scale);
                s.outL = P + m * (prof.outer * scale);
                s.inR  = P - m * (prof.inner * scale);
                s.outR = P - m * (prof.outer * scale);
                return s;
            };

            StrokePointSet a = pointSet(0);
            for (int k = 0; k < segCount; ++k)
            {
                const StrokePointSet b = pointSet((k + 1) % n);
                PushQuad(out, {a.inL, cIn}, {a.inR, cIn}, {b.inR, cIn}, {b.inL, cIn});       // solid core
                PushQuad(out, {a.outL, cOut}, {a.inL, cIn}, {b.inL, cIn}, {b.outL, cOut});   // left fringe
                PushQuad(out, {a.inR, cIn}, {a.outR, cOut}, {b.outR, cOut}, {b.inR, cIn});   // right fringe
                a = b;
            }

            if (closed)
            {
                return;
            }

            const auto emitCap = [&](Vec2 P, Vec2 g, const StrokePointSet& s) {
                if (cap == LineCap::Round)
                {
                    const float ang = std::atan2(g.y, g.x);
                    EmitFan(out, P, ang - kHalfPi, ang + kHalfPi, prof.inner, prof.outer, cIn, cOut,
                            CapSegments(prof.outer));
                    return;
                }
                const float ext  = (cap == LineCap::Square) ? prof.outer : 0.f;
                const Vec2  eInL = s.inL + g * ext;
                const Vec2  eInR = s.inR + g * ext;
                if (ext > 0.f)
                {
                    PushQuad(out, {s.inL, cIn}, {eInL, cIn}, {eInR, cIn}, {s.inR, cIn});
                }
                PushQuad(out, {eInL, cIn}, {eInL + g * kFringe, cOut}, {eInR + g * kFringe, cOut},
                         {eInR, cIn});
            };

            const StrokePointSet s0 = pointSet(0);
            const StrokePointSet sN = pointSet(n - 1);
            emitCap(pts[0], edgeDir(0) * -1.f, s0);
            emitCap(pts[n - 1], edgeDir(n - 2), sN);
        }

        /// Convex and simple: every turn has the same sign and the turns add up to one full revolution (rejects stars).
        [[nodiscard]] bool IsConvex(std::span<const Vec2> pts) noexcept
        {
            const int n    = static_cast<int>(pts.size());
            int       sign = 0;
            float     turn = 0.f;
            for (int i = 0; i < n; ++i)
            {
                const Vec2  a = pts[(i + 1) % n] - pts[i];
                const Vec2  b = pts[(i + 2) % n] - pts[(i + 1) % n];
                const float c = Cross(a, b);
                if (std::abs(c) > 1e-6f)
                {
                    const int s = c > 0.f ? 1 : -1;
                    if (sign != 0 && s != sign)
                    {
                        return false;
                    }
                    sign = s;
                }
                turn += std::atan2(c, Dot(a, b));
            }
            return std::abs(turn) < 4.f * kHalfPi + 0.1f;
        }

        /// Feathered polygon fill: solid triangles plus a 1px outward fringe. `tris` (3N positions) overrides the convex fan.
        void FillConvex(std::vector<PaintVertex>& out, std::span<const Vec2> pts, Color fill,
                        std::span<const Vec2> tris = {})
        {
            const int n = static_cast<int>(pts.size());
            if (n < 3)
            {
                return;
            }

            float area2 = 0.f;
            for (int i = 0; i < n; ++i)
            {
                area2 += Cross(pts[i], pts[(i + 1) % n]);
            }
            if (std::abs(area2) < 1e-6f)
            {
                return;
            }
            const float sign = area2 >= 0.f ? 1.f : -1.f;
            const Color cIn  = fill;
            const Color cOut = WithCoverage(fill, 0.f);

            const auto outerPt = [&](int j) -> Vec2 {
                const Vec2 dPrev = SafeDir(pts[(j - 1 + n) % n], pts[j]);
                const Vec2 dNext = SafeDir(pts[j], pts[(j + 1) % n]);
                const Vec2 oPrev{dPrev.y * sign, -dPrev.x * sign};
                const Vec2 oNext{dNext.y * sign, -dNext.x * sign};
                Vec2        m  = oPrev + oNext;
                const float ml = m.Length();
                float       scale = 1.f;
                if (ml > 1e-6f)
                {
                    m              = m / ml;
                    const float d  = Dot(m, oNext);
                    scale          = d > 1e-3f ? 1.f / d : kMiterLim;
                    scale          = std::min(scale, kMiterLim);
                }
                else
                {
                    m = oNext;
                }
                return pts[j] + m * (kFringe * scale);
            };

            if (tris.empty())
            {
                for (int i = 1; i + 1 < n; ++i)
                {
                    PushTri(out, {pts[0], cIn}, {pts[i], cIn}, {pts[i + 1], cIn});
                }
            }
            for (std::size_t i = 0; i + 2 < tris.size(); i += 3)
            {
                PushTri(out, {tris[i], cIn}, {tris[i + 1], cIn}, {tris[i + 2], cIn});
            }
            for (int i = 0; i < n; ++i)
            {
                const int j1 = (i + 1) % n;
                PushQuad(out, {pts[i], cIn}, {outerPt(i), cOut}, {outerPt(j1), cOut}, {pts[j1], cIn});
            }
        }

        void FlattenCubic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int depth)
        {
            if (depth >= kMaxBezierDepth)
            {
                out.push_back(p3);
                return;
            }
            const Vec2  d   = p3 - p0;
            const float d1  = std::abs(Cross(p1 - p0, d));
            const float d2  = std::abs(Cross(p2 - p0, d));
            const float dl2 = d.LengthSquared();
            if (dl2 > 1e-6f && (d1 + d2) * (d1 + d2) < kBezierTol * kBezierTol * dl2)
            {
                out.push_back(p3);
                return;
            }
            const Vec2 p01 = Lerp(p0, p1, .5f), p12 = Lerp(p1, p2, .5f), p23 = Lerp(p2, p3, .5f);
            const Vec2 p012 = Lerp(p01, p12, .5f), p123 = Lerp(p12, p23, .5f), m = Lerp(p012, p123, .5f);
            FlattenCubic(out, p0, p01, p012, m, depth + 1);
            FlattenCubic(out, m, p123, p23, p3, depth + 1);
        }
    } // namespace

    std::vector<PaintVertex>& Paint2D::Scratch(std::size_t reserveHint)
    {
        m_scratch.clear();
        if (m_scratch.capacity() < reserveHint)
        {
            m_scratch.reserve(reserveHint);
        }
        return m_scratch;
    }

    void Paint2D::FillRect(const Rect& r, Color fill, float radius)
    {
        m_canvas.DrawRect({.bounds = r, .fill = fill, .radius = radius});
    }

    void Paint2D::DrawImage(ImageHandle image, const Rect& dst, float opacity, float radius)
    {
        m_canvas.DrawImage(image, dst, opacity, radius);
    }

    void Paint2D::StrokeRect(const Rect& r, Color color, float width, float radius)
    {
        m_canvas.DrawRect({.bounds = r, .borderColor = color, .borderWidth = width, .borderRadius = radius});
    }

    void Paint2D::StrokeRect(const Rect& r, const Border& border, float radius)
    {
        if (!border.HasBorder())
        {
            return;
        }
        if (border.IsUniform())
        {
            m_canvas.DrawRect({.bounds = r,
                               .borderColor = border.ColorTop(),
                               .borderWidth = border.WidthTop(),
                               .borderRadius = radius,
                               .borderDash = border.DashLengthTop()});
            return;
        }

        const float topW = border.WidthTop();
        const float rightW = border.WidthRight();
        const float bottomW = border.WidthBottom();
        const float leftW = border.WidthLeft();

        const Color topC = border.ColorTop();
        const Color rightC = border.ColorRight();
        const Color bottomC = border.ColorBottom();
        const Color leftC = border.ColorLeft();

        const bool hasRadius = radius > 0.f;
        if (hasRadius)
        {
            m_canvas.PushClip(r, radius);
        }
        if (topW > 0.f && topC.a > 0)
        {
            m_canvas.DrawRect({.bounds = Rect{r.x, r.y, r.width, topW}, .fill = topC});
        }
        if (bottomW > 0.f && bottomC.a > 0)
        {
            m_canvas.DrawRect({.bounds = Rect{r.x, r.Bottom() - bottomW, r.width, bottomW}, .fill = bottomC});
        }
        if (leftW > 0.f && leftC.a > 0)
        {
            m_canvas.DrawRect({.bounds = Rect{r.x, r.y, leftW, r.height}, .fill = leftC});
        }
        if (rightW > 0.f && rightC.a > 0)
        {
            m_canvas.DrawRect({.bounds = Rect{r.Right() - rightW, r.y, rightW, r.height}, .fill = rightC});
        }
        if (hasRadius)
        {
            m_canvas.PopClip();
        }
    }

    void Paint2D::FillCircle(Vec2 center, float radius, Color fill)
    {
        if (radius <= 0.f)
        {
            return;
        }
        m_canvas.DrawShape({.center = center, .radius = radius, .kind = ShapeKind::Circle, .color = fill});
    }

    void Paint2D::StrokeCircle(Vec2 center, float radius, Color color, float width)
    {
        if (radius <= 0.f)
        {
            return;
        }
        m_canvas.DrawShape({.center      = center,
                            .radius      = radius,
                            .kind        = ShapeKind::Ring,
                            .color       = color,
                            .strokeWidth = width});
    }

    void Paint2D::FillRing(Vec2 center, float innerRadius, float outerRadius, Color fill)
    {
        const float lo = std::min(innerRadius, outerRadius);
        const float hi = std::max(innerRadius, outerRadius);
        if (hi <= 0.f || hi - lo <= 0.f)
        {
            return;
        }
        m_canvas.DrawShape({.center      = center,
                            .radius      = (lo + hi) * 0.5f,
                            .kind        = ShapeKind::Ring,
                            .color       = fill,
                            .strokeWidth = hi - lo});
    }

    void Paint2D::StrokeArc(Vec2 center, float radius, float startAngle, float endAngle, Color color,
                            float width)
    {
        if (radius <= 0.f)
        {
            return;
        }
        m_canvas.DrawShape({.center      = center,
                            .radius      = radius,
                            .kind        = ShapeKind::Arc,
                            .color       = color,
                            .strokeWidth = width,
                            .startAngle  = startAngle,
                            .sweepAngle  = endAngle - startAngle});
    }

    void Paint2D::FillPie(Vec2 center, float radius, float startAngle, float endAngle, Color fill)
    {
        if (radius <= 0.f)
        {
            return;
        }
        m_canvas.DrawShape({.center     = center,
                            .radius     = radius,
                            .kind       = ShapeKind::Pie,
                            .color      = fill,
                            .startAngle = startAngle,
                            .sweepAngle = endAngle - startAngle});
    }

    void Paint2D::DrawLine(Vec2 a, Vec2 b, Color color, float width, LineCap cap)
    {
        const std::array<Vec2, 2> pts{a, b};
        std::vector<PaintVertex>& v = Scratch(48);
        StrokePolyline(v, pts, color, width, false, LineJoin::Miter, cap);
        if (!v.empty())
        {
            m_canvas.DrawTriangles(v);
        }
    }

    void Paint2D::DrawPolyline(std::span<const Vec2> points, Color color, float width, bool closed,
                               LineJoin join, LineCap cap)
    {
        if (points.size() < 2)
        {
            return;
        }
        std::vector<PaintVertex>& v = Scratch(points.size() * 18);
        StrokePolyline(v, points, color, width, closed, join, cap);
        if (!v.empty())
        {
            m_canvas.DrawTriangles(v);
        }
    }

    void Paint2D::FillPolygon(std::span<const Vec2> points, Color fill)
    {
        if (points.size() < 3)
        {
            return;
        }
        std::vector<PaintVertex>& v = Scratch(points.size() * 9);
        if (IsConvex(points))
        {
            FillConvex(v, points, fill);
        }
        else
        {
            std::vector<Vec2> tris;
            svg::TessellatePolygon(points, svg::FillRule::NonZero, tris);
            if (tris.empty())
            {
                return;
            }
            FillConvex(v, points, fill, tris);
        }
        if (!v.empty())
        {
            m_canvas.DrawTriangles(v);
        }
    }

    void Paint2D::FillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color)
    {
        const std::array<Vec2, 3> pts{a, b, c};
        std::vector<PaintVertex>& v = Scratch(32);
        FillConvex(v, pts, color);
        if (!v.empty())
        {
            m_canvas.DrawTriangles(v);
        }
    }

    void Paint2D::DrawBezier(Vec2 p0, Vec2 c0, Vec2 c1, Vec2 p1, Color color, float width, LineCap cap)
    {
        m_curvePts.clear();
        m_curvePts.push_back(p0);
        FlattenCubic(m_curvePts, p0, c0, c1, p1, 0);
        std::vector<PaintVertex>& v = Scratch(m_curvePts.size() * 18);
        StrokePolyline(v, m_curvePts, color, width, false, LineJoin::Miter, cap);
        if (!v.empty())
        {
            m_canvas.DrawTriangles(v);
        }
    }

    void Paint2D::DrawBezier(Vec2 p0, Vec2 c, Vec2 p1, Color color, float width, LineCap cap)
    {
        const Vec2 c0 = p0 + (c - p0) * (2.f / 3.f);
        const Vec2 c1 = p1 + (c - p1) * (2.f / 3.f);
        DrawBezier(p0, c0, c1, p1, color, width, cap);
    }

    void Paint2D::FillVertices(std::span<const PaintVertex> verts)
    {
        if (verts.size() >= 3)
        {
            m_canvas.DrawTriangles(verts);
        }
    }

    void Paint2D::PushOpacityLayer(float opacity)
    {
        m_canvas.PushLayer(opacity, nullptr);
    }

    void Paint2D::PopLayer()
    {
        m_canvas.PopLayer();
    }

    void Paint2D::DrawString(std::string_view text, Vec2 pos, Color color, float size, bool bold, bool italic)
    {
        const Rect rect{pos.x, pos.y, 100000.f, size * 2.f};
        EmitText(m_canvas, m_ctx, rect, Spacing{}, text, FontProps{.bold = bold, .italic = italic}, size,
                 TextFlowProps{.maxLines = 1, .wrap = false}, color, nullptr, Align::Start, Align::Start, false);
    }

    void Paint2D::PushClip(const Rect& r)
    {
        m_canvas.PushClip(r);
        ++m_clipDepth;
    }

    void Paint2D::PushClip(const Rect& r, float radius)
    {
        m_canvas.PushClip(r, radius);
        ++m_clipDepth;
    }

    void Paint2D::PopClip()
    {
        if (m_clipDepth > 0)
        {
            m_canvas.PopClip();
            --m_clipDepth;
        }
    }

    void Paint2D::Unwind()
    {
        while (m_clipDepth > 0)
        {
            m_canvas.PopClip();
            --m_clipDepth;
        }
    }
} // namespace lumora
