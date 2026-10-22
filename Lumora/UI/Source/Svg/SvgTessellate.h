#pragma once

#include "Svg/SvgScene.h"

#include <vector>

namespace lumora::svg
{
    /// Flatten a path's cubics to polylines at `tol` (document-space max error).
    void FlattenPath(const RawPath& path, float tol, std::vector<FlatContour>& out);

    /// Tessellate flattened contours into a triangle list (positions, 3N) under a fill rule, and return
    /// the oriented boundary loops (for the AA feather). Uses libtess2.
    void TessellateFill(const std::vector<FlatContour>& contours, FillRule rule, std::vector<Vec2>& outTris,
                        std::vector<FlatContour>& outBoundary);

    /// Subdivide triangles so no edge exceeds `maxEdge` (document space) - for smooth per-vertex
    /// gradient colour across large fills. Writes to `out`.
    void SubdivideTris(const std::vector<Vec2>& in, float maxEdge, std::vector<Vec2>& out);
} // namespace lumora::svg
