#include "BatchBuilder.h"

#include <algorithm>
#include <cmath>

namespace lumora
{
    namespace
    {
        /// 3×3 column-major matrix product C = A · B (storage index col*3+row).
        void Mul3(const float a[9], const float b[9], float out[9])
        {
            for (int col = 0; col < 3; ++col)
            {
                for (int row = 0; row < 3; ++row)
                {
                    out[col * 3 + row] = a[0 * 3 + row] * b[col * 3 + 0] +
                        a[1 * 3 + row] * b[col * 3 + 1] +
                        a[2 * 3 + row] * b[col * 3 + 2];
                }
            }
        }

        [[nodiscard]] std::uint8_t MulU8(std::uint8_t v, float t) noexcept
        {
            float f = static_cast<float>(v) * t;
            if (f < 0.f) f = 0.f;
            if (f > 255.f) f = 255.f;
            return static_cast<std::uint8_t>(f + 0.5f);
        }
    } // namespace

    Vec2 BatchBuilder::Apply(const Layer& l, Vec2 p) const noexcept
    {
        return {
            l.transform[0] * p.x + l.transform[3] * p.y + l.transform[6],
            l.transform[1] * p.x + l.transform[4] * p.y + l.transform[7]
        };
    }

    std::uint32_t BatchBuilder::Pack(Color c, float opacity) const noexcept
    {
        const std::uint32_t a = MulU8(c.a, opacity);
        return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
            (static_cast<std::uint32_t>(c.b) << 16) | (a << 24);
    }

    ScissorRect BatchBuilder::ClipToScissor(const Rect& bounds)
    {
        const Layer& l = _layers.back();
        if (l.transform[1] != 0.f || l.transform[3] != 0.f)
        {
            _sawSkewedClip = true; // off-diagonal terms → not axis-aligned; AABB over-draws
        }

        const Vec2 c0 = Apply(l, {bounds.x, bounds.y});
        const Vec2 c1 = Apply(l, {bounds.x + bounds.width, bounds.y});
        const Vec2 c2 = Apply(l, {bounds.x + bounds.width, bounds.y + bounds.height});
        const Vec2 c3 = Apply(l, {bounds.x, bounds.y + bounds.height});

        const float minX = std::min({c0.x, c1.x, c2.x, c3.x});
        const float minY = std::min({c0.y, c1.y, c2.y, c3.y});
        const float maxX = std::max({c0.x, c1.x, c2.x, c3.x});
        const float maxY = std::max({c0.y, c1.y, c2.y, c3.y});

        const ScissorRect& parent = _clips.back().scissor;
        const std::int32_t px0 = parent.x;
        const std::int32_t py0 = parent.y;
        const std::int32_t px1 = parent.x + static_cast<std::int32_t>(parent.w);
        const std::int32_t py1 = parent.y + static_cast<std::int32_t>(parent.h);

        const std::int32_t ix0 = std::max(static_cast<std::int32_t>(std::floor(minX)), px0);
        const std::int32_t iy0 = std::max(static_cast<std::int32_t>(std::floor(minY)), py0);
        const std::int32_t ix1 = std::min(static_cast<std::int32_t>(std::ceil(maxX)), px1);
        const std::int32_t iy1 = std::min(static_cast<std::int32_t>(std::ceil(maxY)), py1);

        if (ix1 <= ix0 || iy1 <= iy0)
        {
            return {ix0, iy0, 0, 0};
        }
        return {ix0, iy0, static_cast<std::uint32_t>(ix1 - ix0), static_cast<std::uint32_t>(iy1 - iy0)};
    }

    ClipState BatchBuilder::ComputeClip(const Rect& bounds, float radius)
    {
        ClipState out;
        out.scissor = ClipToScissor(bounds); // conservative AABB reject (sets _sawSkewedClip on shear/rotation)

        const Layer&     l      = _layers.back();
        const ClipState& parent = _clips.back();

        // A mask parent cannot be refined analytically (its intersection with an rrect is not an rrect); inherit it so a
        // child only tightens the scissor.
        if (parent.ref.mode == ClipMode::Mask)
        {
            out.ref = parent.ref;
            return out;
        }

        // Decompose the layer's 2x2 into per-axis scale + orthogonality: rotation+scale gives an oriented box (analytic); a
        // shear cannot be.
        const float sx       = std::sqrt(l.transform[0] * l.transform[0] + l.transform[1] * l.transform[1]);
        const float sy       = std::sqrt(l.transform[3] * l.transform[3] + l.transform[4] * l.transform[4]);
        const float dotAxes  = l.transform[0] * l.transform[3] + l.transform[1] * l.transform[4];
        const bool  ortho    = std::abs(dotAxes) <= 1e-4f * (sx * sy + 1e-6f);
        const bool  rotated  = l.transform[1] != 0.f || l.transform[3] != 0.f;
        const float halfX    = bounds.width * 0.5f * sx;
        const float halfY    = bounds.height * 0.5f * sy;

        // Analytic when there are rounded corners to AA or rotation would leave the scissor over-drawing; a plain
        // axis-aligned box is exact under the scissor alone (None).
        if ((radius > 0.f || rotated) && ortho && sx > 1e-6f && sy > 1e-6f)
        {
            // A second rounded clip that does not simply tighten an existing one is not one rrect; the inner shape wins for now.
            if (parent.ref.mode != ClipMode::None)
            {
                _sawUnsupportedClipCombo = true;
            }
            const Vec2  ctr = Apply(l, {bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f});
            out.ref.mode       = ClipMode::Rrect;
            out.ref.center     = ctr;
            out.ref.halfExtent = {halfX, halfY};
            out.ref.radius     = std::min(radius * 0.5f * (sx + sy), std::min(halfX, halfY));
            out.ref.rot        = {l.transform[0] / sx, l.transform[1] / sx};
        }
        else
        {
            // Exact axis-aligned box or an unrepresentable shape: the scissor already bounds it; inherit any rounded/oriented
            // ancestor so a square child keeps its corners.
            out.ref = parent.ref;
        }
        return out;
    }

    void BatchBuilder::Begin(Vec2 surfaceSize, float contentScale)
    {
        _verts.clear();
        _batches.clear();
        _layers.clear();
        _clips.clear();
        _sawSkewedClip = false;
        _sawUnsupportedClipCombo = false;
        _surface = surfaceSize;
        ++_generation;

        Layer base;
        base.transform[0] = contentScale;
        base.transform[4] = contentScale;
        _layers.push_back(base);
        _clips.push_back(ClipState{
            .scissor = {0, 0, static_cast<std::uint32_t>(std::max(0.f, surfaceSize.x)),
                        static_cast<std::uint32_t>(std::max(0.f, surfaceSize.y))}
        });
    }

    void BatchBuilder::End()
    {
    }

    void BatchBuilder::Retain()
    {
        _retainedVerts.swap(_verts);
        _retainedBatches.swap(_batches);
        _haveRetained = !_retainedBatches.empty();
        _retainedGeneration = _generation;
    }

    bool BatchBuilder::Restore()
    {
        if (!_haveRetained)
        {
            return false;
        }
        _verts.swap(_retainedVerts);
        _batches.swap(_retainedBatches);
        _generation = _retainedGeneration;
        _haveRetained = false; // consumed: the streams are current again, and Retain() re-arms it
        return true;
    }

    void BatchBuilder::OpenOrExtend(PipelineKind kind, std::uint32_t textureKey, std::uint32_t baseVertex)
    {
        const ScissorRect sc  = _clips.back().scissor;
        const ClipRef&    clip = _clips.back().ref;
        if (!_batches.empty())
        {
            const Batch& b = _batches.back();
            if (b.pipeline == kind && b.textureKey == textureKey && b.scissor == sc && b.clip == clip)
            {
                return;
            }
        }
        _batches.push_back(Batch{
            .firstVertex = baseVertex, .vertexCount = 0, .indexCount = 0,
            .scissor = sc, .clip = clip, .pipeline = kind, .textureKey = textureKey
        });
    }

    void BatchBuilder::EmitQuad(const Rect& dst, const Rect& uv, std::uint32_t rgba, PipelineKind kind,
                                std::uint32_t textureKey)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(_verts.size());
        OpenOrExtend(kind, textureKey, base);

        const Layer& l = _layers.back();

        _verts.push_back({.pos = Apply(l, {dst.x, dst.y}), .uv = {uv.x, uv.y}, .rgba = rgba});
        _verts.push_back({
            .pos = Apply(l, {dst.x + dst.width, dst.y}),
            .uv = {uv.x + uv.width, uv.y}, .rgba = rgba
        });
        _verts.push_back({
            .pos = Apply(l, {dst.x + dst.width, dst.y + dst.height}),
            .uv = {uv.x + uv.width, uv.y + uv.height}, .rgba = rgba
        });
        _verts.push_back({
            .pos = Apply(l, {dst.x, dst.y + dst.height}),
            .uv = {uv.x, uv.y + uv.height}, .rgba = rgba
        });

        _batches.back().vertexCount += 4;
        _batches.back().indexCount += 6;
    }

    void BatchBuilder::AddRect(const RectCmd& rect)
    {
        // One batch per rect: the SDF pixel shader reads this rect's radius/border from push constants, so rects never merge.
        // `uv` is the rect-local centred position; `pos` folds the layer transform.
        const Layer&        l    = _layers.back();
        const Rect&         dst  = rect.bounds;
        const float         hx   = dst.width * 0.5f;
        const float         hy   = dst.height * 0.5f;
        const std::uint32_t fill = Pack(rect.fill, l.opacity);
        const std::uint32_t base = static_cast<std::uint32_t>(_verts.size());

        _batches.push_back(Batch{.firstVertex     = base,
                                 .vertexCount     = 4,
                                 .indexCount      = 6,
                                 .scissor         = _clips.back().scissor,
                                 .clip            = _clips.back().ref,
                                 .pipeline        = PipelineKind::Rect,
                                 .textureKey      = 0,
                                 .rectHalf        = {hx, hy},
                                 .rectRadius      = rect.radius,
                                 .rectBorderWidth = rect.borderWidth,
                                 .rectBorderColor = Pack(rect.borderColor, l.opacity),
                                 .rectBorderDash  = rect.borderDash});

        _verts.push_back({.pos = Apply(l, {dst.x, dst.y}), .uv = {-hx, -hy}, .rgba = fill});
        _verts.push_back({.pos = Apply(l, {dst.x + dst.width, dst.y}), .uv = {hx, -hy}, .rgba = fill});
        _verts.push_back({.pos = Apply(l, {dst.x + dst.width, dst.y + dst.height}), .uv = {hx, hy}, .rgba = fill});
        _verts.push_back({.pos = Apply(l, {dst.x, dst.y + dst.height}), .uv = {-hx, hy}, .rgba = fill});
    }

    void BatchBuilder::AddShape(const ShapeCmd& shape)
    {
        // One batch per shape, like AddRect. `pos` is an axis-aligned quad around the shape's reach plus an AA pad;
        // `uv` is the same offset pre-rotated into the shader's +Y frame, so a layer transform still rotates the shape.
        constexpr float kHalfPi = 1.57079632679489661923f;
        constexpr float kTwoPi  = 6.28318530717958647692f;
        constexpr float kAaPad  = 2.f;

        const Layer& l       = _layers.back();
        const float  hw      = std::max(0.f, shape.strokeWidth) * 0.5f;
        const bool   stroked = shape.kind == ShapeKind::Ring || shape.kind == ShapeKind::Arc;
        const float  reach   = std::max(0.f, shape.radius) + (stroked ? hw : 0.f);
        const float  half    = reach + kAaPad;

        float        sinHalf = 0.f, cosHalf = 1.f; // (sin,cos) of the half aperture (Arc/Pie)
        float        rc = 1.f, rs = 0.f;           // rotation of the local corner offset into uv
        std::uint8_t kind = static_cast<std::uint8_t>(shape.kind);
        if (shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Pie)
        {
            if (std::abs(shape.sweepAngle) >= kTwoPi)
            {
                // Full turn: degrade to the seam-free closed form (no aperture branch in the SDF).
                kind = static_cast<std::uint8_t>(shape.kind == ShapeKind::Arc ? ShapeKind::Ring
                                                                              : ShapeKind::Circle);
            }
            else
            {
                const float halfSweep = std::abs(shape.sweepAngle) * 0.5f;
                sinHalf               = std::sin(halfSweep);
                cosHalf               = std::cos(halfSweep);
                const float phi       = kHalfPi - (shape.startAngle + shape.sweepAngle * 0.5f);
                rc                    = std::cos(phi);
                rs                    = std::sin(phi);
            }
        }

        const std::uint32_t rgba = Pack(shape.color, l.opacity);
        const Vec2          c    = shape.center;
        const std::uint32_t base = static_cast<std::uint32_t>(_verts.size());

        Batch b{};
        b.firstVertex       = base;
        b.vertexCount       = 4;
        b.indexCount        = 6;
        b.scissor           = _clips.back().scissor;
        b.clip              = _clips.back().ref;
        b.pipeline          = PipelineKind::Sdf;
        b.sdfKind           = kind;
        b.sdfRadius         = std::max(0.f, shape.radius);
        b.sdfHalfWidth      = hw;
        b.sdfApertureSinCos = {sinHalf, cosHalf};
        b.sdfHalf           = {half, half};
        _batches.push_back(b);

        const Vec2          offs[4] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
        for (const Vec2& o : offs)
        {
            const Vec2 uv{rc * o.x - rs * o.y, rs * o.x + rc * o.y};
            _verts.push_back({.pos = Apply(l, {c.x + o.x, c.y + o.y}), .uv = uv, .rgba = rgba});
        }
    }

    void BatchBuilder::AddGlyphs(std::span<const GlyphQuad> quads, std::uint32_t textureKey, Color tint)
    {
        if (quads.empty())
        {
            return;
        }
        // A glyph run shares pipeline/texture/scissor: open the batch once and bulk-append, skipping per-glyph OpenOrExtend checks.
        const std::uint32_t rgba = Pack(tint, _layers.back().opacity);
        _verts.reserve(_verts.size() + quads.size() * 4);
        const std::uint32_t base = static_cast<std::uint32_t>(_verts.size());
        OpenOrExtend(PipelineKind::Textured, textureKey, base);

        const Layer& l = _layers.back();
        for (const GlyphQuad& q : quads)
        {
            const Rect& dst = q.dst;
            const Rect& uv  = q.uv;
            _verts.push_back({.pos = Apply(l, {dst.x, dst.y}), .uv = {uv.x, uv.y}, .rgba = rgba});
            _verts.push_back({
                .pos = Apply(l, {dst.x + dst.width, dst.y}),
                .uv = {uv.x + uv.width, uv.y}, .rgba = rgba
            });
            _verts.push_back({
                .pos = Apply(l, {dst.x + dst.width, dst.y + dst.height}),
                .uv = {uv.x + uv.width, uv.y + uv.height}, .rgba = rgba
            });
            _verts.push_back({
                .pos = Apply(l, {dst.x, dst.y + dst.height}),
                .uv = {uv.x, uv.y + uv.height}, .rgba = rgba
            });
        }
        _batches.back().vertexCount += static_cast<std::uint32_t>(quads.size()) * 4;
        _batches.back().indexCount += static_cast<std::uint32_t>(quads.size()) * 6;
    }

    void BatchBuilder::AddImage(const Rect& dst, std::uint32_t textureKey, float opacity, float)
    {
        const std::uint32_t rgba = Pack(Color::White, _layers.back().opacity * opacity);
        EmitQuad(dst, Rect{0.f, 0.f, 1.f, 1.f}, rgba, PipelineKind::Textured, textureKey);
    }

    void BatchBuilder::AddTriangles(std::span<const PaintVertex> verts)
    {
        if (verts.size() < 3)
        {
            return;
        }
        const std::uint32_t base = static_cast<std::uint32_t>(_verts.size());
        OpenOrExtend(PipelineKind::Tris, 0, base);

        const Layer& l = _layers.back();
        const std::uint32_t count = static_cast<std::uint32_t>((verts.size() / 3) * 3);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const PaintVertex& v = verts[i];
            _verts.push_back({.pos = Apply(l, v.pos), .uv = {0.f, 0.f}, .rgba = Pack(v.col, l.opacity)});
        }
        _batches.back().vertexCount += count;
    }

    void BatchBuilder::PushClip(const Rect& bounds, float radius)
    {
        _clips.push_back(ComputeClip(bounds, radius));
    }

    void BatchBuilder::PushClipMask(std::uint32_t maskId, const Rect& bounds)
    {
        ClipState out;

        // The bbox is already in surface px, so intersect the parent scissor directly without ClipToScissor's layer transform.
        const ScissorRect& parent = _clips.back().scissor;
        const std::int32_t px1 = parent.x + static_cast<std::int32_t>(parent.w);
        const std::int32_t py1 = parent.y + static_cast<std::int32_t>(parent.h);
        const std::int32_t ix0 = std::max(static_cast<std::int32_t>(std::floor(bounds.x)), parent.x);
        const std::int32_t iy0 = std::max(static_cast<std::int32_t>(std::floor(bounds.y)), parent.y);
        const std::int32_t ix1 = std::min(static_cast<std::int32_t>(std::ceil(bounds.x + bounds.width)), px1);
        const std::int32_t iy1 = std::min(static_cast<std::int32_t>(std::ceil(bounds.y + bounds.height)), py1);
        out.scissor = (ix1 <= ix0 || iy1 <= iy0)
                          ? ScissorRect{ix0, iy0, 0, 0}
                          : ScissorRect{ix0, iy0, static_cast<std::uint32_t>(ix1 - ix0),
                                        static_cast<std::uint32_t>(iy1 - iy0)};

        if (maskId != 0)
        {
            out.ref.mode       = ClipMode::Mask;
            out.ref.maskId     = maskId;
            out.ref.center     = {bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
            out.ref.halfExtent = {bounds.width * 0.5f, bounds.height * 0.5f};
        }
        else
        {
            out.ref = _clips.back().ref; // degenerate mask → scissor-only, inherit any analytic ancestor
        }
        _clips.push_back(out);
    }

    void BatchBuilder::PopClip()
    {
        if (_clips.size() > 1)
        {
            _clips.pop_back();
        }
    }

    void BatchBuilder::PushLayer(float opacity, const float transform[9])
    {
        const Layer& parent = _layers.back();
        Layer next;
        if (transform)
        {
            Mul3(parent.transform, transform, next.transform);
        }
        else
        {
            next = parent;
        }
        next.opacity = parent.opacity * opacity;
        _layers.push_back(next);
    }

    void BatchBuilder::PopLayer()
    {
        if (_layers.size() > 1)
        {
            _layers.pop_back();
        }
    }
} // namespace lumora
