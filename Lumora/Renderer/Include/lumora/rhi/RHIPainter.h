#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Text/Glyphs.h>

#include <lumora/rhi/Common.h>
#include <lumora/rhi/Formats.h>
#include <lumora/rhi/Handles.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace lumora::rhi {
class RHI;
class CommandBuffer;
} // namespace lumora::rhi

namespace lumora {

class Canvas;

/// A glyph-atlas plane change carried from the UI thread to the render thread.
struct AtlasDelta {
    std::uint64_t version = 0; ///< 0 = no change
    int width = 0;
    int height = 0;
    AtlasFormat format = AtlasFormat::R8;
    int y0 = 0;
    int y1 = 0;
    std::vector<std::uint8_t> rows; ///< rows [y0, y1) of the plane; the whole plane when its size changed
};

/// Image work queued by UploadTexture/FreeTexture and applied on the render thread. Uploads run before frees.
struct PainterOps {
    struct Upload {
        std::uint64_t id = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        AtlasFormat format = AtlasFormat::R8;
        std::vector<std::byte> pixels;
    };
    std::vector<Upload> uploads;
    std::vector<std::uint64_t> frees;

    [[nodiscard]] bool Empty() const noexcept { return uploads.empty() && frees.empty(); }
};

/// Resolves a SPIR-V file name (e.g. "LumoraRect.vert.spv") to its bytes; empty means missing. Lets a host serve shaders from
/// a package instead of disk.
using SpirvLoader = std::function<std::vector<std::byte>(std::string_view fileName)>;

/// The GPU painter for Lumora: records geometry into a command buffer handed in by the renderer; owns the vertex/index rings,
/// glyph-atlas textures and image table.
/// Per frame (render thread): ApplyOps and SyncGlyphAtlas (outside the render scope), `BeginFrame` opens rendering and returns
/// the Canvas to emit onto, `EndFrame` flushes and closes the scope. The painter never owns a command buffer. Only the calls
/// marked "any thread"/"UI thread" may run concurrently with rendering.
class LUMORA_API RHIPainter {
public:
    RHIPainter();
    ~RHIPainter();

    RHIPainter(const RHIPainter&)            = delete;
    RHIPainter& operator=(const RHIPainter&) = delete;

    /// Binds the painter to an RHI and creates device resources (rings, samplers); atlas textures are created lazily. Call
    /// once from the rendering module.
    void Init(rhi::RHI& rhi);
    /// Release all device resources. The caller must have made the device idle first.
    void Shutdown();

    /// Creates the UI pipelines from precompiled SPIR-V in `spirvDir` (or via `loader`) for `colorFormat`. Returns false if
    /// any shader is missing; draws stay gated until it succeeds.
    bool LoadPipelines(std::string_view spirvDir, rhi::Format colorFormat, const SpirvLoader& loader = {});

    /// Render thread: applies a glyph-atlas delta to its GPU texture (recreated with a full upload on a size change, else the
    /// delta's rows). version 0 is a no-op. Must run OUTSIDE a render scope (before BeginFrame).
    void SyncGlyphAtlas(AtlasKind kind, const AtlasDelta& delta);

    /// Any thread: allocates the handle now (ids stay sequential) and queues the GPU upload; pixels are copied. The image draws
    /// once ApplyOps has run.
    [[nodiscard]] ImageHandle UploadTexture(std::span<const std::byte> pixels, std::uint32_t width,
                                            std::uint32_t height, AtlasFormat format);
    /// Any thread: queues the release of an UploadTexture/RegisterExternalTexture handle. Applied by ApplyOps, then deferred by
    /// frames-in-flight so an in-flight frame never samples it. A null handle is a no-op.
    void FreeTexture(ImageHandle image);

    /// UI thread: moves every queued op into `out` (appends if `out` still holds ops).
    void TakePendingOps(PainterOps& out);
    /// Render thread, outside a render scope: performs and clears `ops`.
    void ApplyOps(PainterOps& ops);

    /// Opens the frame's render scope on `cmd` and returns the Canvas to draw onto; `extent` is the target size in pixels.
    /// Unset `clear` composites with loadOp=Load over the existing target; set means the UI owns the surface and clears it.
    Canvas& BeginFrame(rhi::CommandBuffer& cmd, std::uint32_t frameIndex,
                       rhi::TextureViewHandle target, rhi::Extent2D extent,
                       std::optional<rhi::Color> clear = std::nullopt);
    /// Flush the recorded geometry into the frame's rings and close the render scope.
    void EndFrame();

    /// Sets the logical to physical pixel ratio (DPI scale), applied from the next BeginFrame.
    void SetContentScale(float scale);

    /// Draws a raw RHI texture view into `dst` for debugging; valid between BeginFrame and EndFrame. A null `sampler` uses
    /// the linear sampler.
    void DrawRhiImage(rhi::TextureViewHandle view, const Rect& dst, rhi::SamplerHandle sampler = {});

    /// Any thread: registers an external texture view in the image table and returns an opaque handle for Canvas::DrawImage,
    /// usable at once. The view is borrowed: FreeTexture drops the slot but never destroys the texture.
    [[nodiscard]] ImageHandle RegisterExternalTexture(rhi::TextureViewHandle view, std::uint32_t width,
                                                      std::uint32_t height);

    /// The view an image currently resolves to; invalid until its upload is applied or after its free is applied.
    [[nodiscard]] rhi::TextureViewHandle ResolveImage(ImageHandle image) const;

    /// Use `owner`'s image table from now on. Call right after Init, before any image is registered here.
    void ShareImagesWith(RHIPainter& owner);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace lumora
