#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Text/Glyphs.h>

#include <cstdint>
#include <span>

namespace lumora
{
    /// The drawing seam: an immediate-mode surface the emit walk drives per primitive (no recorded command list). The host
    /// owns the GPU; core links no GPU type, and a null/server build paints into NullCanvas. The implementation owns the clip
    /// and transform/opacity stacks. Coordinates are surface-space pixels, top-left origin.
    class Canvas
    {
    public:
        virtual ~Canvas() = default;

        /// Begin a frame at `surfaceSize` (logical pixels); resets per-frame state and the clip/layer stacks.
        virtual void BeginFrame(Vec2 surfaceSize) = 0;

        /// Re-present the geometry built for the previous frame; true if it could. Called after BeginFrame and before EndFrame,
        /// at most once per frame, only when nothing else has been drawn. Returning false is always safe (the caller replays
        /// its display list); the default does that.
        virtual bool ReuseFrame() { return false; }

        /// End the frame (flush/submit hook); the host still owns buffer-swap and presentation.
        virtual void EndFrame() = 0;

        /// A filled and/or stroked rounded rectangle; one primitive per node.
        virtual void DrawRect(const RectCmd& rect) = 0;

        /// An analytic 2D shape (circle/ring/arc/pie), anti-aliased in the SDF shader; one primitive per node.
        virtual void DrawShape(const ShapeCmd& shape) = 0;

        /// A run of pre-positioned atlas-UV glyph quads sampling `atlas`. `tint` colours a Coverage (R8) run and is ignored for a
        /// Color (RGBA) run. The span is consumed during the call, never retained.
        virtual void DrawGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint) = 0;

        /// A textured quad sampling a painter-minted image. `opacity` multiplies sampled alpha; `radius` rounds the corners.
        virtual void DrawImage(ImageHandle image, const Rect& dst, float opacity = 1.f,
                               float radius = 0.f) = 0;

        /// A per-vertex-coloured triangle list (count a multiple of 3); consumed during the call.
        virtual void DrawTriangles(std::span<const PaintVertex> verts) = 0;

        /// Intersect the clip with `bounds`; a non-zero `radius` clips to an antialiased rounded rect; a rotated layer makes it an oriented box.
        virtual void PushClip(const Rect& bounds, float radius = 0.f) = 0;
        virtual void PopClip() = 0;

        /// Push an arbitrary clip given as antialiased coverage triangles in SURFACE pixels (`boundsSurface` is their bbox); pair
        /// with PopClip. Returns a backend mask id (0 = unsupported, e.g. a null canvas: caller should fall back).
        virtual std::uint32_t PushClipMask(std::span<const PaintVertex> coverageTris,
                                           const Rect& boundsSurface)
        {
            (void) coverageTris;
            (void) boundsSurface;
            return 0;
        }

        /// Push an opacity x transform layer: `opacity` multiplies the current layer's; `transform` is a 3x3 column-major matrix
        /// (index col*3+row, translate at 6/7) composed onto the current one (identity when null).
        virtual void PushLayer(float opacity, const float transform[9] = nullptr) = 0;
        virtual void PopLayer() = 0;
    };
}
