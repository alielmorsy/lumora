#pragma once

#include "Svg/SvgScene.h"

#include <span>
#include <vector>

namespace lumora::svg
{
    /// Flatten a path's cubics to polylines at `tol` (document-space max error).
    void FlattenPath(const RawPath& path, float tol, std::vector<FlatContour>& out);

    /// Tessellate flattened contours into a triangle list (positions, 3N) under a fill rule, and return
    /// the oriented boundary loops (for the AA feather). Uses libtess2.
    void TessellateFill(const std::vector<FlatContour>& contours, FillRule rule, std::vector<Vec2>& outTris,
                        std::vector<FlatContour>& outBoundary);

    /// Triangles only for one closed polygon: no boundary pass, no contour copy, mesh buckets sized to the input.
    void TessellatePolygon(std::span<const Vec2> pts, FillRule rule, std::vector<Vec2>& outTris);

    /// Subdivide a fill's triangles and boundary loops in place so no edge exceeds `maxEdge` (document space), for smooth
    /// per-vertex gradient colour across large fills. Watertight: shared edges split identically.
    void SubdivideFill(std::vector<Vec2>& tris, std::vector<FlatContour>& boundary, float maxEdge);
} // namespace lumora::svg
