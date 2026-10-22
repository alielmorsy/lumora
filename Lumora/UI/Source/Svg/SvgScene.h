#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace lumora::svg
{
    /// 2x3 affine `[a c e ; b d f]`. The CTM is baked into path geometry at parse time, so the renderer composes only the
    /// viewBox-fit transform.
    struct Affine
    {
        float a = 1.f, b = 0.f, c = 0.f, d = 1.f, e = 0.f, f = 0.f;

        [[nodiscard]] Vec2 Apply(Vec2 p) const noexcept
        {
            return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
        }

        /// Apply the linear part only (no translation) - for direction vectors / gradient axes.
        [[nodiscard]] Vec2 ApplyVector(Vec2 v) const noexcept
        {
            return {a * v.x + c * v.y, b * v.x + d * v.y};
        }

        /// `this ∘ m`: the result applies `m` first, then `this` (parent.Compose(child) walks a tree).
        [[nodiscard]] Affine Compose(const Affine& m) const noexcept
        {
            return {
                a * m.a + c * m.b, b * m.a + d * m.b,
                a * m.c + c * m.d, b * m.c + d * m.d,
                a * m.e + c * m.f + e, b * m.e + d * m.f + f,
            };
        }

        /// Magnification of the linear part, used to choose curve-flatten density in device space.
        [[nodiscard]] float MaxScale() const noexcept
        {
            const float sx = std::sqrt(a * a + b * b);
            const float sy = std::sqrt(c * c + d * d);
            return sx > sy ? sx : sy;
        }

        [[nodiscard]] bool Invert(Affine& out) const noexcept
        {
            const float det = a * d - b * c;
            if (std::abs(det) < 1e-12f)
            {
                return false;
            }
            const float inv = 1.f / det;
            out.a = d * inv;
            out.b = -b * inv;
            out.c = -c * inv;
            out.d = a * inv;
            out.e = (c * f - d * e) * inv;
            out.f = (b * e - a * f) * inv;
            return true;
        }
    };

    struct GradientStop
    {
        float offset = 0.f; ///< 0..1 along the gradient
        Color color{};      ///< stop-color already multiplied by stop-opacity (into alpha)
    };

    enum class GradientKind : std::uint8_t { Linear, Radial };
    enum class SpreadMethod : std::uint8_t { Pad, Reflect, Repeat };

    /// Coordinates are in gradient space: the painted element's bounding-box unit square when `userSpace` is false, else
    /// document units. `transform` is the parsed gradientTransform. Sampled per vertex.
    struct Gradient
    {
        GradientKind kind = GradientKind::Linear;
        SpreadMethod spread = SpreadMethod::Pad;
        bool userSpace = false;
        Affine transform;
        Vec2 p0{0.f, 0.f}, p1{1.f, 0.f};
        Vec2 center{0.5f, 0.5f}, focal{0.5f, 0.5f};
        float radius = 0.5f;
        std::vector<GradientStop> stops;
    };

    enum class PaintKind : std::uint8_t { None, Solid, Gradient };

    struct Paint
    {
        PaintKind kind = PaintKind::None;
        Color solid = Color::Black;
        int gradient = -1; ///< index into SvgScene::gradients when kind == Gradient
        float opacity = 1.f; ///< fill-opacity / stroke-opacity (folded into emitted alpha)
    };

    enum class FillRule : std::uint8_t { NonZero, EvenOdd };
    enum class LineCapStyle : std::uint8_t { Butt, Round, Square };
    enum class LineJoinStyle : std::uint8_t { Miter, Round, Bevel };

    struct StrokeStyle
    {
        float width = 1.f;
        LineCapStyle cap = LineCapStyle::Butt;
        LineJoinStyle join = LineJoinStyle::Miter;
        float miterLimit = 4.f;
        std::vector<float> dash;
        float dashOffset = 0.f;
    };

    /// One path segment in document space (CTM baked); quadratics and arcs are converted to cubics at parse.
    struct PathSeg
    {
        enum class Type : std::uint8_t { Line, Cubic } type = Type::Line;
        Vec2 c1{}, c2{}, p{}; ///< Line: p only. Cubic: c1, c2, p.
    };

    struct SubPath
    {
        Vec2 start{};
        std::vector<PathSeg> segs;
        bool closed = false;
    };

    struct RawPath
    {
        std::vector<SubPath> subs;

        [[nodiscard]] bool Empty() const noexcept { return subs.empty(); }
    };

    enum class OpType : std::uint8_t { Fill, Stroke, PushGroupOpacity, PopGroupOpacity, PushClip, PopClip };

    /// One painting instruction in document order; group push/pop ops are balanced. Geometry stays unflattened (RawPath) so the
    /// renderer flattens in device space.
    struct DrawOp
    {
        OpType type = OpType::Fill;
        RawPath path;
        Paint paint;
        FillRule fillRule = FillRule::NonZero; ///< Fill
        StrokeStyle stroke;                    ///< Stroke
        float groupOpacity = 1.f;              ///< PushGroupOpacity
        int clip = -1;                         ///< PushClip → index into SvgScene::clips
    };

    struct ClipPath
    {
        RawPath path;
        FillRule rule = FillRule::NonZero;
    };

    /// The immutable parse product, stored by value in the payload.
    struct SvgScene
    {
        bool valid = false;
        Vec2 viewBoxMin{0.f, 0.f};
        Vec2 viewBoxSize{0.f, 0.f}; ///< (0,0) when absent
        Vec2 intrinsic{0.f, 0.f};   ///< root width/height attrs in px, else viewBoxSize
        float docOpacity = 1.f;
        std::vector<Gradient> gradients;
        std::vector<ClipPath> clips;
        std::vector<DrawOp> ops;
    };

    /// A flattened polyline (built lazily for a device-scale bucket).
    struct FlatContour
    {
        std::vector<Vec2> pts;
        bool closed = false;
    };

    /// Per-op build product, rebuilt when the device-scale bucket changes: fills bake coloured/feathered triangles, strokes keep
    /// flattened contours for the AA stroker each frame.
    struct OpGeometry
    {
        std::vector<PaintVertex> fillVerts;     ///< scale-space coloured tris - feathered when !usesMask
        std::vector<FlatContour> strokeContours;
        Color strokeColor = Color::Transparent;  ///< resolved stroke colour (gradient sampled at bbox centre)

        /// Translucent fills use their own AA coverage mask so the feather ribbon cannot double-blend the interior; `fillVerts` is
        /// then the un-feathered colour, drawn under PushClipMask(maskCoverage).
        bool                     usesMask = false;
        std::vector<PaintVertex> maskCoverage; ///< scale-space white coverage: interior α=1 + feather α→0
        Vec2                     bboxMin{0.f, 0.f};
        Vec2                     bboxMax{0.f, 0.f};
    };

    /// A clip path baked to scale-space AA coverage (interior alpha 1 + 1px feather) with its bounding box (incl. feather).
    struct ClipGeometry
    {
        std::vector<PaintVertex> coverage;
        Vec2 bboxMin{0.f, 0.f};
        Vec2 bboxMax{0.f, 0.f};
    };

    /// The renderer's cache, owned by the payload. Geometry is baked in scale-space (viewBox scale applied, box translation
    /// deferred) so resizing rebuilds but moving does not; a change in per-axis or OS content scale rebuilds fully.
    struct SceneCache
    {
        float builtSx = -1.f;
        float builtSy = -1.f;
        float builtContentScale = -1.f;
        std::vector<OpGeometry> ops;
        std::vector<ClipGeometry> clips;
    };
} // namespace lumora::svg
