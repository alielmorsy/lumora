#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Text/Glyphs.h>

#include <cstdint>
#include <span>
#include <vector>

namespace lumora {

/// Which shader a batch draws under. Kinds never merge into one another, so a kind change always breaks the batch.
enum class PipelineKind : std::uint8_t {
    Rect,     ///< solid rounded rect + border (SDF in the pixel shader)
    Textured, ///< glyph / image quad sampling one texture
    Tris,     ///< per-vertex-coloured triangle list (Canvas custom paint)
    Sdf,      ///< analytic circle / ring / arc / pie (SDF in the pixel shader)
    Mask,     ///< coverage rasterized into an offscreen R8 clip mask (pre-pass only, never a Batch)
};

/// One GPU vertex. The layer transform is folded into `pos` and layer opacity into `rgba`'s alpha. Plain data, no RHI type.
struct UiVertex {
    Vec2          pos{};      ///< surface-space pixels
    Vec2          uv{};       ///< atlas / image UV (0 for untextured)
    std::uint32_t rgba = 0;   ///< straight-alpha, byte order R,G,B,A (low→high)
};

/// An axis-aligned scissor in framebuffer pixels; the canvas converts it to rhi::Rect2D at submit.
struct ScissorRect {
    std::int32_t  x = 0;
    std::int32_t  y = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;

    [[nodiscard]] constexpr bool operator==(const ScissorRect&) const = default;
};

/// How the active clip is enforced per draw. `None` = the scissor AABB alone; `Rrect` = analytic rounded/oriented box
/// measured as SDF coverage;
/// `Mask` = offscreen R8 coverage mask multiplied into fragment coverage (arbitrary shapes).
enum class ClipMode : std::uint8_t { None, Rrect, Mask };

/// The active clip in surface pixels, folded onto every Batch; the scissor AABB stays alongside as a cheap GPU reject.
/// Mirrors the 32-byte clip tail of every push block.
struct ClipRef {
    ClipMode      mode   = ClipMode::None;
    Vec2          center{};         ///< rrect centre (surface px)
    Vec2          halfExtent{};     ///< rrect half-extent, pre-rotation (surface px)
    float         radius = 0.f;     ///< corner radius (surface px)
    Vec2          rot{1.f, 0.f};    ///< (cos,sin) of clip orientation; identity = axis-aligned
    std::uint32_t maskId = 0;       ///< per-frame mask table index (Mask)

    [[nodiscard]] constexpr bool operator==(const ClipRef& o) const noexcept {
        return mode == o.mode && center.x == o.center.x && center.y == o.center.y &&
            halfExtent.x == o.halfExtent.x && halfExtent.y == o.halfExtent.y && radius == o.radius &&
            rot.x == o.rot.x && rot.y == o.rot.y && maskId == o.maskId;
    }
};

/// One clip-stack entry: the conservative scissor AABB plus the analytic/mask shape applied on top.
struct ClipState {
    ScissorRect scissor{};
    ClipRef     ref{};
};

/// A contiguous run of indices sharing one {pipeline, texture, scissor, clip}. `textureKey` is an
/// opaque id the canvas resolves to a texture view (0 = untextured).
struct Batch {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount  = 0;
    ScissorRect   scissor{};
    ClipRef       clip{};
    PipelineKind  pipeline   = PipelineKind::Rect;
    std::uint32_t textureKey = 0;

    // Per-rect SDF parameters (Rect only), pushed as constants; each rect is therefore its own batch.
    Vec2          rectHalf{};            ///< half extent (px)
    float         rectRadius      = 0.f; ///< corner radius (px)
    float         rectBorderWidth = 0.f; ///< inner border thickness (px); 0 → no border
    std::uint32_t rectBorderColor = 0;   ///< packed R,G,B,A (low→high), layer opacity pre-applied
    float         rectBorderDash  = 0.f; ///< dash length (px); 0 → solid stroke

    // Per-shape SDF parameters (Sdf only); each shape is its own batch, its `uv` pre-rotated into the shader's +Y frame.
    std::uint8_t  sdfKind          = 0;  ///< ShapeKind cast to uint8 → shader shapeKind
    float         sdfRadius        = 0.f;///< disc / centerline radius (px)
    float         sdfHalfWidth     = 0.f;///< stroke half-thickness (px); Ring/Arc
    Vec2          sdfApertureSinCos{};   ///< (sin,cos) of the half aperture; Arc/Pie
    Vec2          sdfHalf{};             ///< quad half-extent in local px (reach + AA pad)
};

/// Pure, RHI-free batch construction. RHICanvas feeds it primitives during the emit walk; it accumulates a vertex/index
/// stream and a merged batch list while maintaining clip and layer stacks. Unit-testable without a GPU.
class BatchBuilder {
public:
    /// Resets all streams and stacks; the whole surface becomes the initial clip. `surfaceSize` is physical pixels;
    /// `contentScale` seeds the base layer (logical to physical).
    void Begin(Vec2 surfaceSize, float contentScale = 1.f);
    /// Close the final open batch. Call once after the walk, before reading the streams.
    void End();

    /// Moves the built streams aside so the next Begin() starts empty while keeping vector capacity alive across frames.
    /// Called at BeginFrame; it is what makes Restore() possible one call later.
    void Retain();

    /// Puts the retained streams back as current and returns whether there were any: the static-UI path where last frame's
    /// geometry is still exactly right.
    [[nodiscard]] bool Restore();

    void AddRect(const RectCmd& rect);
    void AddShape(const ShapeCmd& shape);
    void AddGlyphs(std::span<const GlyphQuad> quads, std::uint32_t textureKey, Color tint);
    void AddImage(const Rect& dst, std::uint32_t textureKey, float opacity, float radius);
    void AddTriangles(std::span<const PaintVertex> verts);

    void PushClip(const Rect& bounds, float radius = 0.f);
    /// Pushes an arbitrary-shape clip whose coverage is offscreen R8 mask `maskId` (0 = none: scissor-only).
    /// `boundsSurface` is the mask's surface-pixel bbox (already resolved, NOT layer-transformed); it sizes the mask and seeds the scissor.
    void PushClipMask(std::uint32_t maskId, const Rect& boundsSurface);
    void PopClip();
    void PushLayer(float opacity, const float transform[9]);
    void PopLayer();

    /// Maps a point through the active layer transform; the canvas uses it to put clip-mask geometry in surface space,
    /// matching the fills it clips.
    [[nodiscard]] Vec2 ApplyCurrentLayer(Vec2 p) const noexcept { return Apply(_layers.back(), p); }

    [[nodiscard]] std::span<const UiVertex>      Vertices() const noexcept { return _verts; }
    [[nodiscard]] std::span<const Batch>         Batches()  const noexcept { return _batches; }
    [[nodiscard]] std::uint64_t                  Generation() const noexcept { return _generation; }

    /// True when a sheared layer transform was active under a clip; neither the scissor nor the oriented box can represent it.
    [[nodiscard]] bool SawSkewedClip() const noexcept { return _sawSkewedClip; }

    /// True when two analytic rounded clips nested without containment (the intersection is not one rrect); the inner one wins.
    [[nodiscard]] bool SawUnsupportedClipCombo() const noexcept { return _sawUnsupportedClipCombo; }

private:
    struct Layer {
        float transform[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
        float opacity      = 1.f;
    };

    [[nodiscard]] Vec2          Apply(const Layer&, Vec2 p) const noexcept;
    [[nodiscard]] std::uint32_t Pack(Color, float opacity) const noexcept;
    [[nodiscard]] ScissorRect   ClipToScissor(const Rect&);
    [[nodiscard]] ClipState     ComputeClip(const Rect&, float radius);

    void EmitQuad(const Rect& dst, const Rect& uv, std::uint32_t rgba, PipelineKind, std::uint32_t textureKey);
    void OpenOrExtend(PipelineKind, std::uint32_t textureKey, std::uint32_t baseVertex);

    std::vector<UiVertex>      _verts;
    std::vector<Batch>         _batches;

    std::vector<UiVertex>      _retainedVerts;
    std::vector<Batch>         _retainedBatches;
    bool                       _haveRetained = false;
    std::uint64_t              _generation = 1;
    std::uint64_t              _retainedGeneration = 0;

    std::vector<Layer>     _layers;
    std::vector<ClipState> _clips;

    Vec2 _surface{};
    bool _sawSkewedClip = false;
    bool _sawUnsupportedClipCombo = false;
};

} // namespace lumora
