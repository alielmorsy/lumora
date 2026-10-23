#include "Svg/SvgTessellate.h"

#include <tesselator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace lumora::svg
{
    namespace
    {
        constexpr int kMaxFlattenDepth = 16;
        constexpr int kMaxSubdivDepth = 7;

        [[nodiscard]] float Cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }
        [[nodiscard]] Vec2 Lerp(Vec2 a, Vec2 b, float t) noexcept { return a + (b - a) * t; }

        void FlattenCubicTo(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float tol, int depth)
        {
            if (depth >= kMaxFlattenDepth)
            {
                out.push_back(p3);
                return;
            }
            const Vec2 d = p3 - p0;
            const float d1 = std::abs(Cross(p1 - p0, d));
            const float d2 = std::abs(Cross(p2 - p0, d));
            const float dl2 = d.LengthSquared();
            if (dl2 > 1e-9f && (d1 + d2) * (d1 + d2) < tol * tol * dl2)
            {
                out.push_back(p3);
                return;
            }
            if (dl2 <= 1e-9f && (d1 + d2) < tol)
            {
                out.push_back(p3);
                return;
            }
            const Vec2 p01 = Lerp(p0, p1, .5f), p12 = Lerp(p1, p2, .5f), p23 = Lerp(p2, p3, .5f);
            const Vec2 p012 = Lerp(p01, p12, .5f), p123 = Lerp(p12, p23, .5f), m = Lerp(p012, p123, .5f);
            FlattenCubicTo(out, p0, p01, p012, m, tol, depth + 1);
            FlattenCubicTo(out, m, p123, p23, p3, tol, depth + 1);
        }

        void SplitTri(Vec2 a, Vec2 b, Vec2 c, float maxEdge2, int depth, std::vector<Vec2>& out)
        {
            const float ab = (b - a).LengthSquared();
            const float bc = (c - b).LengthSquared();
            const float ca = (a - c).LengthSquared();
            const float longest = std::max(ab, std::max(bc, ca));
            if (depth >= kMaxSubdivDepth || longest <= maxEdge2)
            {
                out.push_back(a);
                out.push_back(b);
                out.push_back(c);
                return;
            }
            if (ab >= bc && ab >= ca)
            {
                const Vec2 m = Lerp(a, b, .5f);
                SplitTri(a, m, c, maxEdge2, depth + 1, out);
                SplitTri(m, b, c, maxEdge2, depth + 1, out);
            }
            else if (bc >= ab && bc >= ca)
            {
                const Vec2 m = Lerp(b, c, .5f);
                SplitTri(a, b, m, maxEdge2, depth + 1, out);
                SplitTri(a, m, c, maxEdge2, depth + 1, out);
            }
            else
            {
                const Vec2 m = Lerp(c, a, .5f);
                SplitTri(a, b, m, maxEdge2, depth + 1, out);
                SplitTri(m, b, c, maxEdge2, depth + 1, out);
            }
        }
    } // namespace

    void FlattenPath(const RawPath& path, float tol, std::vector<FlatContour>& out)
    {
        if (tol <= 0.f)
        {
            tol = 0.1f;
        }
        for (const SubPath& sub : path.subs)
        {
            FlatContour fc;
            fc.closed = sub.closed;
            fc.pts.push_back(sub.start);
            Vec2 cur = sub.start;
            for (const PathSeg& seg : sub.segs)
            {
                if (seg.type == PathSeg::Type::Line)
                {
                    fc.pts.push_back(seg.p);
                }
                else
                {
                    FlattenCubicTo(fc.pts, cur, seg.c1, seg.c2, seg.p, tol, 0);
                }
                cur = seg.p;
            }
            // A closed subpath's flattened polyline ends on its first point; drop it or the stroker's wrap edge is zero-length (bad
            // miter at the seam, degenerate tessellator edge).
            if (fc.closed && fc.pts.size() >= 2 && (fc.pts.back() - fc.pts.front()).LengthSquared() < 1e-6f)
            {
                fc.pts.pop_back();
            }
            if (fc.pts.size() >= 2)
            {
                out.push_back(std::move(fc));
            }
        }
    }

    void TessellateFill(const std::vector<FlatContour>& contours, FillRule rule, std::vector<Vec2>& outTris,
                        std::vector<FlatContour>& outBoundary)
    {
        if (contours.empty())
        {
            return;
        }
        const int winding = (rule == FillRule::EvenOdd) ? TESS_WINDING_ODD : TESS_WINDING_NONZERO;

        // Pack each contour into a flat float array (kept alive for the duration of tessellation).
        std::vector<std::vector<float>> arrays;
        arrays.reserve(contours.size());
        for (const FlatContour& c : contours)
        {
            if (c.pts.size() < 2)
            {
                continue;
            }
            std::vector<float> a;
            a.reserve(c.pts.size() * 2);
            for (const Vec2& p : c.pts)
            {
                a.push_back(p.x);
                a.push_back(p.y);
            }
            arrays.push_back(std::move(a));
        }
        if (arrays.empty())
        {
            return;
        }

        if (TESStesselator* tess = tessNewTess(nullptr))
        {
            for (const std::vector<float>& a : arrays)
            {
                tessAddContour(tess, 2, a.data(), sizeof(float) * 2, static_cast<int>(a.size() / 2));
            }
            if (tessTesselate(tess, winding, TESS_POLYGONS, 3, 2, nullptr))
            {
                const TESSreal* verts = tessGetVertices(tess);
                const TESSindex* elems = tessGetElements(tess);
                const int count = tessGetElementCount(tess);
                outTris.reserve(outTris.size() + static_cast<std::size_t>(count) * 3);
                for (int i = 0; i < count; ++i)
                {
                    const TESSindex i0 = elems[i * 3 + 0];
                    const TESSindex i1 = elems[i * 3 + 1];
                    const TESSindex i2 = elems[i * 3 + 2];
                    if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF)
                    {
                        continue;
                    }
                    outTris.push_back({verts[i0 * 2], verts[i0 * 2 + 1]});
                    outTris.push_back({verts[i1 * 2], verts[i1 * 2 + 1]});
                    outTris.push_back({verts[i2 * 2], verts[i2 * 2 + 1]});
                }
            }
            tessDeleteTess(tess);
        }

        // Pass 2: oriented boundary loops (fresh tesselator - the first run consumed its mesh).
        if (TESStesselator* tess = tessNewTess(nullptr))
        {
            for (const std::vector<float>& a : arrays)
            {
                tessAddContour(tess, 2, a.data(), sizeof(float) * 2, static_cast<int>(a.size() / 2));
            }
            if (tessTesselate(tess, winding, TESS_BOUNDARY_CONTOURS, 0, 2, nullptr))
            {
                const TESSreal* verts = tessGetVertices(tess);
                const TESSindex* elems = tessGetElements(tess);
                const int count = tessGetElementCount(tess);
                for (int i = 0; i < count; ++i)
                {
                    const int base = elems[i * 2 + 0];
                    const int n = elems[i * 2 + 1];
                    if (n < 3)
                    {
                        continue;
                    }
                    FlatContour fc;
                    fc.closed = true;
                    fc.pts.reserve(static_cast<std::size_t>(n));
                    for (int k = 0; k < n; ++k)
                    {
                        fc.pts.push_back({verts[(base + k) * 2], verts[(base + k) * 2 + 1]});
                    }
                    outBoundary.push_back(std::move(fc));
                }
            }
            tessDeleteTess(tess);
        }
    }

    void TessellatePolygon(std::span<const Vec2> pts, FillRule rule, std::vector<Vec2>& outTris)
    {
        if (pts.size() < 3)
        {
            return;
        }
        static_assert(sizeof(Vec2) == sizeof(float) * 2);
        // libtess2's default buckets (256-512 records each) dominate the cost for icon-sized polygons.
        const int n = static_cast<int>(pts.size());
        TESSalloc alloc{};
        alloc.memalloc = [](void*, unsigned int size) { return std::malloc(size); };
        alloc.memrealloc = [](void*, void* p, unsigned int size) { return std::realloc(p, size); };
        alloc.memfree = [](void*, void* p) { std::free(p); };
        alloc.meshEdgeBucketSize = std::max(16, n * 4);
        alloc.meshVertexBucketSize = std::max(16, n * 2);
        alloc.meshFaceBucketSize = std::max(8, n);
        alloc.dictNodeBucketSize = std::max(16, n * 2);
        alloc.regionBucketSize = std::max(8, n);

        TESStesselator* tess = tessNewTess(&alloc);
        if (!tess)
        {
            return;
        }
        tessAddContour(tess, 2, &pts[0].x, sizeof(Vec2), n);
        const int winding = (rule == FillRule::EvenOdd) ? TESS_WINDING_ODD : TESS_WINDING_NONZERO;
        if (tessTesselate(tess, winding, TESS_POLYGONS, 3, 2, nullptr))
        {
            const TESSreal* verts = tessGetVertices(tess);
            const TESSindex* elems = tessGetElements(tess);
            const int count = tessGetElementCount(tess);
            outTris.reserve(outTris.size() + static_cast<std::size_t>(count) * 3);
            for (int i = 0; i < count * 3; i += 3)
            {
                if (elems[i] == TESS_UNDEF || elems[i + 1] == TESS_UNDEF || elems[i + 2] == TESS_UNDEF)
                {
                    continue;
                }
                for (int k = 0; k < 3; ++k)
                {
                    outTris.push_back({verts[elems[i + k] * 2], verts[elems[i + k] * 2 + 1]});
                }
            }
        }
        tessDeleteTess(tess);
    }

    void SubdivideTris(const std::vector<Vec2>& in, float maxEdge, std::vector<Vec2>& out)
    {
        const float maxEdge2 = maxEdge > 0.f ? maxEdge * maxEdge : 0.f;
        out.reserve(in.size());
        for (std::size_t i = 0; i + 2 < in.size(); i += 3)
        {
            if (maxEdge2 <= 0.f)
            {
                out.push_back(in[i]);
                out.push_back(in[i + 1]);
                out.push_back(in[i + 2]);
            }
            else
            {
                SplitTri(in[i], in[i + 1], in[i + 2], maxEdge2, 0, out);
            }
        }
    }
} // namespace lumora::svg
