#pragma once

#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>

#include <algorithm>

namespace lumora::nodegraph
{
    /// Graph view transform (canvas units in, layer pixels out), scale and pan only: the element system is
    /// translate-only and paint is axis-aligned. `viewport` is the painter's Bounds(), passed in because the
    /// camera outlives any solved rect.
    class Camera
    {
    public:
        static constexpr float MinZoom = 0.1f;
        static constexpr float MaxZoom = 4.f;

        [[nodiscard]] Vec2  Pan() const noexcept { return m_pan; }
        [[nodiscard]] float Zoom() const noexcept { return m_zoom; }

        void SetPan(Vec2 pan) noexcept { m_pan = pan; }
        void SetZoom(float zoom) noexcept { m_zoom = std::clamp(zoom, MinZoom, MaxZoom); }

        /// Shift by a layer-pixel delta (a drag's pointer delta), tracking the cursor 1:1 at any zoom.
        void PanBy(Vec2 layerDelta) noexcept { m_pan -= layerDelta / m_zoom; }

        [[nodiscard]] Vec2 ToLayer(Vec2 canvas, const Rect& viewport) const noexcept
        {
            return viewport.Position() + (canvas - m_pan) * m_zoom;
        }

        [[nodiscard]] Vec2 ToCanvas(Vec2 layer, const Rect& viewport) const noexcept
        {
            return m_pan + (layer - viewport.Position()) / m_zoom;
        }

        [[nodiscard]] Rect ToLayer(const Rect& canvas, const Rect& viewport) const noexcept
        {
            const Vec2 origin = ToLayer(canvas.Position(), viewport);
            return Rect{origin.x, origin.y, canvas.width * m_zoom, canvas.height * m_zoom};
        }

        /// The canvas-space region the viewport currently shows - the cull window.
        [[nodiscard]] Rect VisibleCanvas(const Rect& viewport) const noexcept
        {
            return Rect{m_pan.x, m_pan.y, viewport.width / m_zoom, viewport.height / m_zoom};
        }

        /// Scale by `factor` holding the canvas point under `layerAnchor` fixed (re-read after the clamp, so it holds
        /// even when the factor is refused).
        void ZoomAt(Vec2 layerAnchor, float factor, const Rect& viewport) noexcept
        {
            const Vec2 before = ToCanvas(layerAnchor, viewport);
            SetZoom(m_zoom * factor);
            m_pan += before - ToCanvas(layerAnchor, viewport);
        }

        /// Frame `canvasBounds` in `viewport` with `margin` layer pixels; degenerate bounds are a no-op.
        void Fit(const Rect& canvasBounds, const Rect& viewport, float margin = 40.f) noexcept
        {
            if (canvasBounds.width <= 0.f || canvasBounds.height <= 0.f)
            {
                return;
            }
            const float usableW = std::max(viewport.width - margin * 2.f, 1.f);
            const float usableH = std::max(viewport.height - margin * 2.f, 1.f);
            SetZoom(std::min(usableW / canvasBounds.width, usableH / canvasBounds.height));

            const Vec2 centre = canvasBounds.Center();
            m_pan = centre - Vec2{viewport.width / m_zoom, viewport.height / m_zoom} * 0.5f;
        }

    private:
        Vec2  m_pan{};
        float m_zoom = 1.f;
    };
}
