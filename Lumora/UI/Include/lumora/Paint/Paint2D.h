#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>

#include <span>
#include <string_view>
#include <vector>

namespace lumora
{
    class Canvas;
    struct PaintContext;
    struct Border;

    /// End-cap style for open strokes (DrawLine / DrawPolyline / DrawBezier).
    enum class LineCap : std::uint8_t { Butt, Round, Square };

    /// Corner style between polyline segments. Miter falls back to Bevel past the miter limit.
    enum class LineJoin : std::uint8_t { Miter, Bevel, Round };

    /// Immediate-mode surface handed to a Painter's `onPaint`: a facade over the device Canvas, clipped to the content rect.
    /// Coordinates are layer-local (Bounds() and Mouse() share that space); use SurfaceBounds() for surface-space hit-testing.
    /// Constructed by the runtime; the user receives it by reference.
    class LUMORA_API Paint2D
    {
    public:
        Paint2D(PaintContext& ctx, Canvas& canvas, Rect bounds, Vec2 mouse, Vec2 layerOffset = {}) noexcept
            : m_ctx(ctx), m_canvas(canvas), m_bounds(bounds), m_mouse(mouse), m_layerOffset(layerOffset) {}

        [[nodiscard]] const Rect& Bounds() const noexcept { return m_bounds; }
        [[nodiscard]] Vec2        Mouse() const noexcept { return m_mouse; }
        [[nodiscard]] bool        Hovered() const noexcept { return m_bounds.Contains(m_mouse); }

        /// The content rect in surface space (Bounds() offset by every enclosing Scroll/transform). Use it, not Bounds(), for
        /// drop zones and raw-pointer hit-tests.
        [[nodiscard]] Rect SurfaceBounds() const noexcept
        {
            return Rect{m_bounds.x + m_layerOffset.x, m_bounds.y + m_layerOffset.y,
                        m_bounds.width, m_bounds.height};
        }

        void FillRect(const Rect& r, Color fill, float radius = 0.f);
        void StrokeRect(const Rect& r, Color color, float width, float radius = 0.f);
        void StrokeRect(const Rect& r, const Border& border, float radius = 0.f);

        /// Draw a backend-registered image stretched into `dst`; an invalid handle draws nothing.
        void DrawImage(ImageHandle image, const Rect& dst, float opacity = 1.f, float radius = 0.f);

        /// Analytic circles/arcs (no tessellation). Angles are radians, 0 = +X, increasing clockwise on screen (y-down, like
        /// HTML canvas): pi/2 points straight down. A full-turn StrokeArc/FillPie collapses to ring/disc.
        void FillCircle(Vec2 center, float radius, Color fill);
        void StrokeCircle(Vec2 center, float radius, Color color, float width = 1.f);
        void FillRing(Vec2 center, float innerRadius, float outerRadius, Color fill);
        void StrokeArc(Vec2 center, float radius, float startAngle, float endAngle, Color color,
                       float width = 1.f);
        void FillPie(Vec2 center, float radius, float startAngle, float endAngle, Color fill);

        void DrawLine(Vec2 a, Vec2 b, Color color, float width = 1.f, LineCap cap = LineCap::Butt);
        void DrawPolyline(std::span<const Vec2> points, Color color, float width = 1.f,
                          bool closed = false, LineJoin join = LineJoin::Miter,
                          LineCap cap = LineCap::Butt);
        /// Fill a simple polygon (convex or concave, either winding); self-intersections use the nonzero rule.
        void FillPolygon(std::span<const Vec2> points, Color fill);
        void FillTriangle(Vec2 a, Vec2 b, Vec2 c, Color color);

        /// Emit an already-tessellated per-vertex-coloured triangle list (vertex count a multiple of 3), e.g. SVG fills.
        void FillVertices(std::span<const PaintVertex> verts);

        /// Push/pop a pure-opacity layer (SVG group opacity). The caller balances every push; Unwind() does not.
        void PushOpacityLayer(float opacity);
        void PopLayer();

        /// Anti-aliased Bezier curves (cubic and quadratic overloads), adaptively flattened then stroked.
        void DrawBezier(Vec2 p0, Vec2 c0, Vec2 c1, Vec2 p1, Color color, float width = 1.f,
                        LineCap cap = LineCap::Butt);
        void DrawBezier(Vec2 p0, Vec2 c, Vec2 p1, Color color, float width = 1.f,
                        LineCap cap = LineCap::Butt);

        /// Draw a single line of text with its top-left at `pos` in the default family; use a Text widget for rich/multiline text.
        /// Named DrawString because <windows.h> defines DrawText as a macro.
        void DrawString(std::string_view text, Vec2 pos, Color color, float size = 14.f,
                        bool bold = false, bool italic = false);

        /// Restrict subsequent draws to `r` (intersected with the active clip); pair with PopClip. Clips left pushed on return are
        /// unwound by the widget. The `radius` overload clips to a rounded rect.
        void PushClip(const Rect& r);
        void PushClip(const Rect& r, float radius);
        void PopClip();

        /// Pop clips left pushed by the callback; called by the Painter widget after onPaint. Not a user verb.
        void Unwind();

    private:
        friend class SvgImage; // SvgImage::Draw replays parsed SVG through the underlying canvas

        /// Reused vertex scratch, cleared (capacity kept) per tessellating draw.
        [[nodiscard]] std::vector<PaintVertex>& Scratch(std::size_t reserveHint);

        PaintContext&            m_ctx;
        Canvas&                  m_canvas;
        Rect                     m_bounds;
        Vec2                     m_mouse;
        Vec2                     m_layerOffset;
        int                      m_clipDepth = 0;
        std::vector<PaintVertex> m_scratch;
        std::vector<Vec2>        m_curvePts;
    };
}
