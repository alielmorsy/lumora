#include "Svg/SvgRenderer.h"

#include "Paint/CanvasStroke.h"
#include "Svg/SvgTessellate.h"

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Paint/Paint2D.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace lumora::svg
{
    namespace
    {
        constexpr float kBaseFlattenTol = 0.25f; ///< target device-space flatten error (px)
        constexpr float kFeatherPx = 1.0f;        ///< AA feather, physical px
        constexpr float kGradMaxEdgePx = 6.0f;    ///< gradient subdivision target edge, physical px
        constexpr float kMiterLim = 6.0f;

        [[nodiscard]] float Cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
        [[nodiscard]] float Dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

        [[nodiscard]] Vec2 SafeDir(Vec2 from, Vec2 to) noexcept
        {
            const Vec2 d = to - from;
            const float l = d.Length();
            return l > 1e-6f ? d / l : Vec2{};
        }

        [[nodiscard]] Color WithOpacity(Color c, float op) noexcept
        {
            const float a = std::clamp(static_cast<float>(c.a) * op, 0.f, 255.f);
            return c.WithAlpha(static_cast<std::uint8_t>(a + 0.5f));
        }

        [[nodiscard]] Color ColorLerp(Color a, Color b, float u) noexcept
        {
            u = std::clamp(u, 0.f, 1.f);
            const auto ch = [&](std::uint8_t x, std::uint8_t y) {
                return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * u + 0.5f);
            };
            return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
        }

        [[nodiscard]] Color SampleStops(const std::vector<GradientStop>& stops, float t) noexcept
        {
            if (stops.empty())
            {
                return Color::Transparent;
            }
            if (t <= stops.front().offset)
            {
                return stops.front().color;
            }
            if (t >= stops.back().offset)
            {
                return stops.back().color;
            }
            for (std::size_t i = 0; i + 1 < stops.size(); ++i)
            {
                if (t >= stops[i].offset && t <= stops[i + 1].offset)
                {
                    const float span = stops[i + 1].offset - stops[i].offset;
                    const float u = span > 1e-6f ? (t - stops[i].offset) / span : 0.f;
                    return ColorLerp(stops[i].color, stops[i + 1].color, u);
                }
            }
            return stops.back().color;
        }

        [[nodiscard]] float ApplySpread(float t, SpreadMethod spread) noexcept
        {
            switch (spread)
            {
            case SpreadMethod::Repeat:
                return t - std::floor(t);
            case SpreadMethod::Reflect:
            {
                float m = std::fmod(std::abs(t), 2.f);
                return m > 1.f ? 2.f - m : m;
            }
            case SpreadMethod::Pad:
            default:
                return std::clamp(t, 0.f, 1.f);
            }
        }

        [[nodiscard]] Color SampleGradient(const Gradient& g, Vec2 docPoint, Vec2 bbMin, Vec2 bbSize) noexcept
        {
            Affine inv;
            if (!g.transform.Invert(inv))
            {
                inv = Affine{};
            }
            Vec2 q;
            if (g.userSpace)
            {
                q = inv.Apply(docPoint);
            }
            else
            {
                const Vec2 u{bbSize.x > 1e-6f ? (docPoint.x - bbMin.x) / bbSize.x : 0.f,
                            bbSize.y > 1e-6f ? (docPoint.y - bbMin.y) / bbSize.y : 0.f};
                q = inv.Apply(u);
            }

            float t = 0.f;
            if (g.kind == GradientKind::Linear)
            {
                const Vec2 d = g.p1 - g.p0;
                const float len2 = d.x * d.x + d.y * d.y;
                t = len2 > 1e-9f ? Dot(q - g.p0, d) / len2 : 0.f;
            }
            else
            {
                const Vec2 dd = q - g.center;
                t = g.radius > 1e-6f ? std::sqrt(dd.x * dd.x + dd.y * dd.y) / g.radius : 0.f;
            }
            return SampleStops(g.stops, ApplySpread(t, g.spread));
        }

        [[nodiscard]] Color ResolveColor(const Paint& paint, const SvgScene& scene, Vec2 docPoint, Vec2 bbMin,
                                          Vec2 bbSize) noexcept
        {
            if (paint.kind == PaintKind::Solid)
            {
                return paint.solid;
            }
            if (paint.kind == PaintKind::Gradient && paint.gradient >= 0
                && paint.gradient < static_cast<int>(scene.gradients.size()))
            {
                return SampleGradient(scene.gradients[paint.gradient], docPoint, bbMin, bbSize);
            }
            return Color::Transparent;
        }

        struct Fit
        {
            float sx = 1.f, sy = 1.f, tx = 0.f, ty = 0.f;
        };

        [[nodiscard]] Vec2 AlignFrac(SvgAlign a) noexcept
        {
            switch (a)
            {
            case SvgAlign::XMinYMin: return {0.f, 0.f};
            case SvgAlign::XMidYMin: return {0.5f, 0.f};
            case SvgAlign::XMaxYMin: return {1.f, 0.f};
            case SvgAlign::XMinYMid: return {0.f, 0.5f};
            case SvgAlign::XMidYMid: return {0.5f, 0.5f};
            case SvgAlign::XMaxYMid: return {1.f, 0.5f};
            case SvgAlign::XMinYMax: return {0.f, 1.f};
            case SvgAlign::XMidYMax: return {0.5f, 1.f};
            case SvgAlign::XMaxYMax: return {1.f, 1.f};
            case SvgAlign::None:
            default: return {0.f, 0.f};
            }
        }

        [[nodiscard]] Fit ComputeFit(const SvgScene& scene, const Rect& box, SvgAlign align, SvgMeetOrSlice mos)
        {
            Vec2 vb = scene.viewBoxSize;
            if (vb.x <= 0.f) vb.x = scene.intrinsic.x > 0.f ? scene.intrinsic.x : box.width;
            if (vb.y <= 0.f) vb.y = scene.intrinsic.y > 0.f ? scene.intrinsic.y : box.height;
            Fit f;
            f.tx = box.x;
            f.ty = box.y;
            if (vb.x <= 0.f || vb.y <= 0.f)
            {
                return f;
            }
            float sx = box.width / vb.x;
            float sy = box.height / vb.y;
            if (align != SvgAlign::None)
            {
                const float s = (mos == SvgMeetOrSlice::Slice) ? std::max(sx, sy) : std::min(sx, sy);
                sx = sy = s;
            }
            f.sx = sx;
            f.sy = sy;
            const Vec2 frac = AlignFrac(align);
            f.tx = box.x + (box.width - vb.x * sx) * frac.x;
            f.ty = box.y + (box.height - vb.y * sy) * frac.y;
            return f;
        }

        void PushTri(std::vector<PaintVertex>& o, PaintVertex a, PaintVertex b, PaintVertex c)
        {
            o.push_back(a);
            o.push_back(b);
            o.push_back(c);
        }


        struct Builder
        {
            SvgScene& scene;
            SceneCache& cache;
            Vec2 vbMin;
            float sx, sy;
            float deviceScale;
            float contentScale;

            [[nodiscard]] Vec2 ToS(Vec2 doc) const noexcept { return {(doc.x - vbMin.x) * sx, (doc.y - vbMin.y) * sy}; }

            // White AA coverage in scale space: interior tris at α=1 + a 1px feather ribbon fading to 0.
            // Used both for clip masks and for translucent-fill masks. Returns the scale-space bbox.
            void BuildCoverage(const std::vector<Vec2>& tris, const std::vector<FlatContour>& boundary,
                               std::vector<PaintVertex>& out, Vec2& bbMinS, Vec2& bbMaxS)
            {
                out.clear();
                out.reserve(tris.size());
                for (const Vec2& v : tris)
                {
                    out.push_back({ToS(v), Color::White});
                }
                const float widthS = kFeatherPx / std::max(contentScale, 0.01f);
                const auto  whiteSample = [](Vec2) { return Color::White; };
                for (const FlatContour& loop : boundary)
                {
                    BuildFeatherLoop(loop, widthS, whiteSample, out);
                }
                bbMinS = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                bbMaxS = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
                for (const PaintVertex& pv : out)
                {
                    bbMinS.x = std::min(bbMinS.x, pv.pos.x);
                    bbMinS.y = std::min(bbMinS.y, pv.pos.y);
                    bbMaxS.x = std::max(bbMaxS.x, pv.pos.x);
                    bbMaxS.y = std::max(bbMaxS.y, pv.pos.y);
                }
            }

            void BuildClips(float tolDoc)
            {
                for (std::size_t i = 0; i < scene.clips.size(); ++i)
                {
                    std::vector<FlatContour> contours;
                    FlattenPath(scene.clips[i].path, tolDoc, contours);
                    std::vector<Vec2>        tris;
                    std::vector<FlatContour> boundary;
                    TessellateFill(contours, scene.clips[i].rule, tris, boundary);
                    ClipGeometry& cg = cache.clips[i];
                    BuildCoverage(tris, boundary, cg.coverage, cg.bboxMin, cg.bboxMax);
                }
            }

            template <typename SampleFn>
            void BuildFeatherLoop(const FlatContour& loop, float widthS, const SampleFn& sample,
                                  std::vector<PaintVertex>& out)
            {
                const std::size_t n = loop.pts.size();
                if (n < 2)
                {
                    return;
                }
                std::vector<Vec2> sp(n);
                std::vector<Color> col(n);
                for (std::size_t k = 0; k < n; ++k)
                {
                    sp[k] = ToS(loop.pts[k]);
                    col[k] = sample(loop.pts[k]);
                }
                float area2 = 0.f;
                for (std::size_t k = 0; k < n; ++k)
                {
                    area2 += Cross(sp[k], sp[(k + 1) % n]);
                }
                const float sign = area2 >= 0.f ? 1.f : -1.f;
                std::vector<Vec2> outer(n);
                for (std::size_t j = 0; j < n; ++j)
                {
                    const Vec2 dPrev = SafeDir(sp[(j + n - 1) % n], sp[j]);
                    const Vec2 dNext = SafeDir(sp[j], sp[(j + 1) % n]);
                    const Vec2 oPrev{dPrev.y * sign, -dPrev.x * sign};
                    const Vec2 oNext{dNext.y * sign, -dNext.x * sign};
                    Vec2 m = oPrev + oNext;
                    const float ml = m.Length();
                    float scale = 1.f;
                    if (ml > 1e-6f)
                    {
                        m = m / ml;
                        const float d = Dot(m, oNext);
                        scale = d > 1e-3f ? std::min(1.f / d, kMiterLim) : kMiterLim;
                    }
                    else
                    {
                        m = oNext;
                    }
                    outer[j] = sp[j] + m * (widthS * scale);
                }
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::size_t j = (i + 1) % n;
                    const Color ci0 = col[i];
                    const Color cj0 = col[j];
                    const PaintVertex a{sp[i], ci0};
                    const PaintVertex b{outer[i], ci0.WithAlpha(0)};
                    const PaintVertex c{outer[j], cj0.WithAlpha(0)};
                    const PaintVertex d{sp[j], cj0};
                    PushTri(out, a, b, c);
                    PushTri(out, a, c, d);
                }
            }

            void BuildFill(const DrawOp& op, OpGeometry& geo, float tolDoc)
            {
                std::vector<FlatContour> contours;
                FlattenPath(op.path, tolDoc, contours);
                if (contours.empty())
                {
                    return;
                }
                Vec2 bbMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                Vec2 bbMax{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
                for (const FlatContour& c : contours)
                {
                    for (const Vec2& p : c.pts)
                    {
                        bbMin.x = std::min(bbMin.x, p.x);
                        bbMin.y = std::min(bbMin.y, p.y);
                        bbMax.x = std::max(bbMax.x, p.x);
                        bbMax.y = std::max(bbMax.y, p.y);
                    }
                }
                const Vec2 bbSize{bbMax.x - bbMin.x, bbMax.y - bbMin.y};

                std::vector<Vec2>        tris;
                std::vector<FlatContour> boundary;
                TessellateFill(contours, op.fillRule, tris, boundary);
                if (tris.empty())
                {
                    return;
                }

                // A translucent solid fill's feather ribbon would double-blend its edge (conflation), so route it through its own AA
                // coverage mask. Gradient/opaque fills keep the cheap feather path.
                const bool maskFill = op.paint.kind == PaintKind::Solid
                    && (op.paint.opacity < 0.996f || op.paint.solid.a < 255);
                if (maskFill)
                {
                    geo.usesMask = true;
                    BuildCoverage(tris, boundary, geo.maskCoverage, geo.bboxMin, geo.bboxMax);
                    const Color c  = WithOpacity(op.paint.solid, op.paint.opacity);
                    const Vec2  mn = geo.bboxMin;
                    const Vec2  mx = geo.bboxMax;
                    geo.fillVerts.clear();
                    PushTri(geo.fillVerts, {{mn.x, mn.y}, c}, {{mx.x, mn.y}, c}, {{mx.x, mx.y}, c});
                    PushTri(geo.fillVerts, {{mn.x, mn.y}, c}, {{mx.x, mx.y}, c}, {{mn.x, mx.y}, c});
                    return;
                }

                if (op.paint.kind == PaintKind::Gradient)
                {
                    const float       maxEdgeDoc = kGradMaxEdgePx / std::max(deviceScale, 0.01f);
                    std::vector<Vec2> sub;
                    SubdivideTris(tris, maxEdgeDoc, sub);
                    tris.swap(sub);
                }

                const auto sample = [&](Vec2 docp) {
                    return WithOpacity(ResolveColor(op.paint, scene, docp, bbMin, bbSize), op.paint.opacity);
                };

                geo.fillVerts.reserve(geo.fillVerts.size() + tris.size());
                for (const Vec2& v : tris)
                {
                    geo.fillVerts.push_back({ToS(v), sample(v)});
                }

                // Always feather: clips are antialiased by the mask, so the feather is never dropped when clipped.
                const float widthS = kFeatherPx / std::max(contentScale, 0.01f);
                for (const FlatContour& loop : boundary)
                {
                    BuildFeatherLoop(loop, widthS, sample, geo.fillVerts);
                }
            }

            void BuildStroke(const DrawOp& op, OpGeometry& geo, float tolDoc)
            {
                std::vector<FlatContour> contours;
                FlattenPath(op.path, tolDoc, contours);
                if (contours.empty())
                {
                    return;
                }
                Vec2 bbMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                Vec2 bbMax{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
                for (const FlatContour& c : contours)
                {
                    FlatContour s;
                    s.closed = c.closed;
                    s.pts.reserve(c.pts.size());
                    for (const Vec2& p : c.pts)
                    {
                        s.pts.push_back(ToS(p));
                        bbMin.x = std::min(bbMin.x, p.x);
                        bbMin.y = std::min(bbMin.y, p.y);
                        bbMax.x = std::max(bbMax.x, p.x);
                        bbMax.y = std::max(bbMax.y, p.y);
                    }
                    geo.strokeContours.push_back(std::move(s));
                }
                const Vec2 center{(bbMin.x + bbMax.x) * 0.5f, (bbMin.y + bbMax.y) * 0.5f};
                geo.strokeColor = WithOpacity(ResolveColor(op.paint, scene, center, bbMin, {bbMax.x - bbMin.x, bbMax.y - bbMin.y}), op.paint.opacity);
            }

            void Run()
            {
                cache.ops.assign(scene.ops.size(), OpGeometry{});
                cache.clips.assign(scene.clips.size(), ClipGeometry{});
                const float tolDoc = kBaseFlattenTol / std::max(deviceScale, 0.01f);
                BuildClips(tolDoc);

                for (std::size_t i = 0; i < scene.ops.size(); ++i)
                {
                    const DrawOp& op = scene.ops[i];
                    switch (op.type)
                    {
                    case OpType::Fill: BuildFill(op, cache.ops[i], tolDoc); break;
                    case OpType::Stroke: BuildStroke(op, cache.ops[i], tolDoc); break;
                    default: break;
                    }
                }
                cache.builtSx = sx;
                cache.builtSy = sy;
                cache.builtContentScale = contentScale;
            }
        };

        [[nodiscard]] bool NeedsRebuild(const SceneCache& cache, const SvgScene& scene, float sx, float sy,
                                        float contentScale) noexcept
        {
            if (cache.builtSx <= 0.f || cache.ops.size() != scene.ops.size()
                || cache.clips.size() != scene.clips.size())
            {
                return true;
            }
            const auto changed = [](float a, float b) {
                return b <= 0.f || std::abs(a / b - 1.f) > 0.05f;
            };
            return changed(sx, cache.builtSx) || changed(sy, cache.builtSy)
                || changed(contentScale, cache.builtContentScale);
        }


        [[nodiscard]] LineCap MapCap(LineCapStyle c) noexcept
        {
            switch (c)
            {
            case LineCapStyle::Round: return LineCap::Round;
            case LineCapStyle::Square: return LineCap::Square;
            case LineCapStyle::Butt:
            default: return LineCap::Butt;
            }
        }

        [[nodiscard]] LineJoin MapJoin(LineJoinStyle j) noexcept
        {
            switch (j)
            {
            case LineJoinStyle::Round: return LineJoin::Round;
            case LineJoinStyle::Bevel: return LineJoin::Bevel;
            case LineJoinStyle::Miter:
            default: return LineJoin::Miter;
            }
        }

        struct StrokeSink
        {
            Canvas& canvas;
            std::vector<PaintVertex> scratch;

            void DrawPolyline(std::span<const Vec2> pts, Color col, float width, bool closed, LineJoin join, LineCap cap)
            {
                DrawPolylineOnCanvas(canvas, scratch, pts, col, width, closed, join, cap);
            }
        };

        void FillVertices(Canvas& canvas, std::span<const PaintVertex> verts)
        {
            if (verts.size() >= 3)
            {
                canvas.DrawTriangles(verts);
            }
        }

        void EmitDashed(StrokeSink& sink, const std::vector<Vec2>& pts, bool closed, Color col, float width,
                        LineCap cap, const std::vector<float>& dash, float offset)
        {
            std::vector<float> pat = dash;
            if (pat.size() % 2 == 1)
            {
                pat.insert(pat.end(), dash.begin(), dash.end());
            }
            float total = 0.f;
            for (const float d : pat) total += std::max(0.f, d);
            if (pat.empty() || total <= 1e-3f)
            {
                sink.DrawPolyline(pts, col, width, closed, LineJoin::Round, cap);
                return;
            }

            std::vector<Vec2> walk(pts.begin(), pts.end());
            if (closed && !walk.empty())
            {
                walk.push_back(walk.front());
            }

            std::size_t patIdx = 0;
            float remain = pat[0];
            bool on = true;
            float off = std::fmod(offset, total);
            if (off < 0.f) off += total;
            while (off > 0.f)
            {
                if (off >= remain)
                {
                    off -= remain;
                    patIdx = (patIdx + 1) % pat.size();
                    remain = pat[patIdx];
                    on = !on;
                }
                else
                {
                    remain -= off;
                    off = 0.f;
                }
            }

            std::vector<Vec2> run;
            const auto flush = [&]() {
                if (on && run.size() >= 2)
                {
                    sink.DrawPolyline(run, col, width, false, LineJoin::Round, cap);
                }
                run.clear();
            };

            for (std::size_t i = 0; i + 1 < walk.size(); ++i)
            {
                Vec2 a = walk[i];
                const Vec2 b = walk[i + 1];
                float segLen = (b - a).Length();
                Vec2 dir = segLen > 1e-6f ? (b - a) / segLen : Vec2{};
                if (on && run.empty()) run.push_back(a);
                while (segLen > remain)
                {
                    a = a + dir * remain;
                    segLen -= remain;
                    if (on) run.push_back(a);
                    flush();
                    patIdx = (patIdx + 1) % pat.size();
                    remain = pat[patIdx];
                    on = !on;
                    if (on) run.push_back(a);
                }
                remain -= segLen;
                if (on) run.push_back(b);
            }
            flush();
        }

        void EmitStroke(StrokeSink& sink, const DrawOp& op, const OpGeometry& geo, const Builder& fitter)
        {
            const float scaleAvg = (fitter.sx + fitter.sy) * 0.5f;
            const float width = op.stroke.width * scaleAvg;
            if (width <= 0.f || geo.strokeColor.a == 0)
            {
                return;
            }
            const LineCap cap = MapCap(op.stroke.cap);
            const LineJoin join = MapJoin(op.stroke.join);

            std::vector<float> dashS;
            if (!op.stroke.dash.empty())
            {
                dashS.reserve(op.stroke.dash.size());
                for (const float d : op.stroke.dash) dashS.push_back(d * scaleAvg);
            }

            for (const FlatContour& c : geo.strokeContours)
            {
                if (c.pts.size() < 2)
                {
                    continue;
                }
                if (dashS.empty())
                {
                    sink.DrawPolyline(c.pts, geo.strokeColor, width, c.closed, join, cap);
                }
                else
                {
                    EmitDashed(sink, c.pts, c.closed, geo.strokeColor, width, cap, dashS,
                               op.stroke.dashOffset * scaleAvg);
                }
            }
        }
    } // namespace

    void RenderScene(Canvas& canvas, SvgScene& scene, SceneCache& cache, const Rect& box,
                     SvgAlign align, SvgMeetOrSlice meetOrSlice, float widgetOpacity, float contentScale)
    {
        if (!scene.valid || box.width <= 0.f || box.height <= 0.f)
        {
            return;
        }
        const float cs = contentScale > 0.f ? contentScale : 1.f;
        const Fit fit = ComputeFit(scene, box, align, meetOrSlice);
        const float deviceScale = std::max(0.01f, std::max(fit.sx, fit.sy) * cs);

        if (NeedsRebuild(cache, scene, fit.sx, fit.sy, cs))
        {
            Builder b{scene, cache, scene.viewBoxMin, fit.sx, fit.sy, deviceScale, cs};
            b.Run();
        }

        Builder fitter{scene, cache, scene.viewBoxMin, fit.sx, fit.sy, deviceScale, cs};
        StrokeSink sink{canvas, {}};

        const float layerOpacity = std::clamp(widgetOpacity * scene.docOpacity, 0.f, 1.f);
        const float trans[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, fit.tx, fit.ty, 1.f};
        canvas.PushLayer(layerOpacity, trans);

        for (std::size_t i = 0; i < scene.ops.size(); ++i)
        {
            const DrawOp& op = scene.ops[i];
            switch (op.type)
            {
            case OpType::Fill:
            {
                const OpGeometry& g = cache.ops[i];
                if (g.usesMask)
                {
                    // Translucent fill: clip a flat colour quad to the shape's AA coverage mask.
                    canvas.PushClipMask(g.maskCoverage, {g.bboxMin.x, g.bboxMin.y,
                                                         g.bboxMax.x - g.bboxMin.x, g.bboxMax.y - g.bboxMin.y});
                    FillVertices(canvas, g.fillVerts);
                    canvas.PopClip();
                }
                else
                {
                    FillVertices(canvas, g.fillVerts);
                }
                break;
            }
            case OpType::Stroke:
                EmitStroke(sink, op, cache.ops[i], fitter);
                break;
            case OpType::PushGroupOpacity:
                canvas.PushLayer(op.groupOpacity, nullptr);
                break;
            case OpType::PopGroupOpacity:
                canvas.PopLayer();
                break;
            case OpType::PushClip:
            {
                // clipPath → an antialiased GPU coverage-mask clip (any shape). An empty/invalid clip
                // pushes an empty scissor so nothing under it draws (and PopClip stays balanced).
                if (op.clip >= 0 && op.clip < static_cast<int>(cache.clips.size())
                    && !cache.clips[op.clip].coverage.empty())
                {
                    const ClipGeometry& cg = cache.clips[op.clip];
                    canvas.PushClipMask(cg.coverage, {cg.bboxMin.x, cg.bboxMin.y,
                                                      cg.bboxMax.x - cg.bboxMin.x, cg.bboxMax.y - cg.bboxMin.y});
                }
                else
                {
                    canvas.PushClip({0.f, 0.f, 0.f, 0.f});
                }
                break;
            }
            case OpType::PopClip:
                canvas.PopClip();
                break;
            }
        }

        canvas.PopLayer();
    }
} // namespace lumora::svg
