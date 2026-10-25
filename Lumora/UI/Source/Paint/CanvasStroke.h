#pragma once

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/Paint2D.h>

#include <span>
#include <vector>

namespace lumora
{
    /// Tessellate a stroked polyline into `scratch` (cleared first) and submit it to `canvas`; what Paint2D::DrawPolyline does,
    /// without needing a Paint2D.
    void DrawPolylineOnCanvas(Canvas& canvas, std::vector<PaintVertex>& scratch, std::span<const Vec2> points,
                              Color color, float width, bool closed, LineJoin join, LineCap cap);
}
