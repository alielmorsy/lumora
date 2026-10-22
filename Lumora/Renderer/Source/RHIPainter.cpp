#include <lumora/rhi/RHIPainter.h>

#include "FrameRings.h"
#include "LumoraPipelines.h"
#include "RHICanvas.h"

#include <lumora/Ref.h>
#include <lumora/log/Log.h>
#include <lumora/rhi/CommandBuffer.h>
#include <lumora/rhi/RHI.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>

LUMORA_LOG_CATEGORY (
"Lumora.RHI"
)

namespace lumora
{
    namespace
    {
        [[nodiscard]] rhi::Format FormatOf(AtlasFormat f) noexcept
        {
            return f == AtlasFormat::R8 ? rhi::Format::R8_Unorm : rhi::Format::R8G8B8A8_Unorm;
        }

        [[nodiscard]] std::uint32_t BytesPerPixel(AtlasFormat f) noexcept
        {
            return f == AtlasFormat::R8 ? 1u : 4u;
        }

        [[nodiscard]] const char* KindName(PipelineKind k) noexcept
        {
            switch (k)
            {
            case PipelineKind::Rect: return "Rect";
            case PipelineKind::Textured: return "Textured";
            case PipelineKind::Tris: return "Tris";
            case PipelineKind::Sdf: return "Sdf";
            case PipelineKind::Mask: return "Mask";
            }
            return "?";
        }

        /// The 32-byte analytic-clip tail shared by every push block at offset 48 (matches the HLSL tail). `clipMode` is ClipMode as uint;
        /// every field is 4-byte and none straddles a 16-byte boundary, so scalar and cbuffer packing agree.
        struct ClipTail
        {
            float clipCenter[2] = {0.f, 0.f}; // rrect centre (surface px)   (offset 48)
            float clipHalf[2] = {0.f, 0.f}; // rrect half-extent (px)      (offset 56)
            float clipRadius = 0.f; // corner radius (px)          (offset 64)
            std::uint32_t clipMode = 0; // 0 none / 1 rrect / 2 mask   (offset 68)
            float clipRot[2] = {1.f, 0.f}; // (cos,sin) orientation       (offset 72)
        };

        /// The push-constant block (matches the HLSL `PushConstants`): base scale/translate/mode for every pipeline, per-rect
        /// SDF params for Rect, clip tail at offset 48 for all. 8 bytes of pad keep the tail 16-aligned.
        struct PushBlock
        {
            float scale[2] = {0.f, 0.f}; // 2 / surfaceSize             (offset 0)
            float translate[2] = {-1.f, -1.f}; //                             (offset 8)
            std::uint32_t mode = 0; // textured: 0 = R8 mask, 1 = RGBA (offset 16)
            float radius = 0.f; // rect: corner radius (px)    (offset 20)
            float borderWidth = 0.f; // rect: border thickness (px) (offset 24)
            std::uint32_t borderColor = 0; // rect: packed R,G,B,A        (offset 28)
            float halfSize[2] = {0.f, 0.f}; // rect: half extent (px)      (offset 32)
            float borderDash = 0.f; // rect: dash length px, 0=solid (offset 40)
            float pad0 = 0.f; // pad to 16-aligned clip tail (offset 44)
            ClipTail clip{}; //                             (offset 48)
        };

        static_assert(sizeof(PushBlock) == 80);
        static_assert(offsetof(PushBlock, clip) == 48);

        /// The Sdf pipeline's push block (matches LumoraSDF.hlsl). Its first 20 bytes alias PushBlock's base but the tail
        /// diverges (apertureSinCos at 32, halfSize at 40), so it is a separate 48-byte struct.
        struct SdfPushBlock
        {
            float scale[2] = {0.f, 0.f}; // 2 / surfaceSize          (offset 0)
            float translate[2] = {-1.f, -1.f}; //                          (offset 8)
            std::uint32_t shapeKind = 0; // ShapeKind                (offset 16)
            float radius = 0.f; // disc / centerline radius (offset 20)
            float halfWidth = 0.f; // ring/arc half-thickness  (offset 24)
            std::uint32_t pad0 = 0; // keeps the float2 8-aligned (offset 28)
            float apertureSinCos[2] = {0.f, 0.f}; // (sin,cos) half aperture  (offset 32)
            float halfSize[2] = {0.f, 0.f}; // quad half-extent (px)    (offset 40)
            ClipTail clip{}; // shared clip tail         (offset 48)
        };

        static_assert(sizeof(SdfPushBlock) == 80);
        static_assert(offsetof(SdfPushBlock, apertureSinCos) == 32);
        static_assert(offsetof(SdfPushBlock, halfSize) == 40);
        static_assert(offsetof(SdfPushBlock, clip) == 48);

        /// The mask pre-pass push block (matches LumoraMask.hlsl): scale/translate mapping the clip bbox onto the whole R8
        /// target; 20 B, no clip tail.
        struct MaskPushBlock
        {
            float scale[2] = {0.f, 0.f};
            float translate[2] = {0.f, 0.f};
            std::uint32_t mode = 0;
        };

        static_assert(sizeof(MaskPushBlock) == 20);

        /// Fill the shared 32-byte clip tail of a push block from a batch's resolved clip.
        inline void FillClipTail(ClipTail& t, const ClipRef& c) noexcept
        {
            t.clipCenter[0] = c.center.x;
            t.clipCenter[1] = c.center.y;
            t.clipHalf[0] = c.halfExtent.x;
            t.clipHalf[1] = c.halfExtent.y;
            t.clipRadius = c.radius;
            t.clipMode = static_cast<std::uint32_t>(c.mode);
            t.clipRot[0] = c.rot.x;
            t.clipRot[1] = c.rot.y;
        }

        /// A ring peak past 256 KiB logs geometry distribution (per PipelineKind, quad/vert counts, ring capacity) to
        /// attribute upload volume.
        constexpr std::uint64_t kRingReportBytes = 256ull * 1024ull;

        bool ReportGeometry(std::span<const UiVertex> verts, std::size_t maskVertCount,
                            std::span<const Batch> batches, std::uint64_t bytes,
                            std::uint64_t vertexCap)
        {
            using namespace std::chrono_literals;
            static std::atomic<std::chrono::steady_clock::rep> lastReportStamp{0};
            const auto now = std::chrono::steady_clock::now();
            const auto prev = lastReportStamp.load(std::memory_order_relaxed);
            const auto nowStamp = now.time_since_epoch().count();
            if (prev != 0 && std::chrono::steady_clock::duration{nowStamp - prev} < 2s)
            {
                return false;
            }

            std::array<std::uint32_t, 5> batchesPerKind{};
            std::array<std::uint64_t, 5> quadsPerKind{};
            std::array<std::uint64_t, 5> vertsPerKind{};
            const Batch* largest = nullptr;

            std::uint32_t zeroScissorBatches = 0;
            std::uint64_t zeroScissorVerts = 0;
            std::uint32_t trisSmallBatches = 0; // <= 12 tris
            std::uint32_t trisLargeBatches = 0; // > 12 tris

            struct TrisGroup
            {
                ScissorRect scissor{};
                std::uint32_t batchCount = 0;
                std::uint64_t vertexCount = 0;
                float minX = std::numeric_limits<float>::infinity();
                float minY = std::numeric_limits<float>::infinity();
                float maxX = -std::numeric_limits<float>::infinity();
                float maxY = -std::numeric_limits<float>::infinity();
            };
            std::vector<TrisGroup> trisGroups;

            for (const Batch& b : batches)
            {
                const auto kind = static_cast<std::size_t>(b.pipeline);
                ++batchesPerKind[kind];

                const bool zeroScissor = (b.scissor.w == 0 || b.scissor.h == 0);
                const std::uint64_t bVerts = (b.pipeline == PipelineKind::Tris)
                                                 ? b.vertexCount
                                                 : static_cast<std::uint64_t>(b.indexCount / 6) * 4u;
                if (zeroScissor)
                {
                    ++zeroScissorBatches;
                    zeroScissorVerts += bVerts;
                }

                if (b.pipeline == PipelineKind::Tris)
                {
                    vertsPerKind[kind] += b.vertexCount;
                    const std::uint32_t tris = b.vertexCount / 3;
                    quadsPerKind[kind] += tris; // triangles

                    if (tris <= 12)
                    {
                        ++trisSmallBatches;
                    }
                    else
                    {
                        ++trisLargeBatches;
                    }

                    auto it = std::find_if(trisGroups.begin(), trisGroups.end(), [&](const TrisGroup& g) {
                        return g.scissor == b.scissor;
                    });
                    if (it == trisGroups.end())
                    {
                        trisGroups.push_back(TrisGroup{.scissor = b.scissor});
                        it = trisGroups.end() - 1;
                    }
                    it->batchCount += 1;
                    it->vertexCount += b.vertexCount;

                    const std::uint32_t end = b.firstVertex + b.vertexCount;
                    for (std::uint32_t i = b.firstVertex; i < end && i < verts.size(); ++i)
                    {
                        const Vec2 p = verts[i].pos;
                        it->minX = std::min(it->minX, p.x);
                        it->minY = std::min(it->minY, p.y);
                        it->maxX = std::max(it->maxX, p.x);
                        it->maxY = std::max(it->maxY, p.y);
                    }
                }
                else
                {
                    const std::uint32_t q = b.indexCount / 6;
                    quadsPerKind[kind] += q;
                    vertsPerKind[kind] += q * 4;
                }

                if (largest == nullptr || b.indexCount > largest->indexCount ||
                    (b.pipeline == PipelineKind::Tris && b.vertexCount > (largest ? largest->vertexCount : 0)))
                {
                    largest = &b;
                }
            }

            lastReportStamp.store(nowStamp, std::memory_order_relaxed);

            LOG_TRACE(
                "UI geometry upload peak: {:.1f} KiB (ring cap: {:.1f} KiB): "
                "{} verts + {} mask verts, {} batches. "
                "Per-kind (quads/verts/batches): Rect {}/{}/{}, Textured {}/{}/{}, Tris {} tris/{}/{}, Sdf {}/{}/{}. "
                "Largest batch: {} with {} indices / {} verts. "
                "Waste: {} zero-scissor batches ({} verts). "
                "Tris size split: {} batches <=12 tris vs {} >12 tris.",
                static_cast<double>(bytes) / 1024.0,
                static_cast<double>(vertexCap) / 1024.0,
                verts.size(), maskVertCount, batches.size(),
                quadsPerKind[0], vertsPerKind[0], batchesPerKind[0],
                quadsPerKind[1], vertsPerKind[1], batchesPerKind[1],
                quadsPerKind[2], vertsPerKind[2], batchesPerKind[2],
                quadsPerKind[3], vertsPerKind[3], batchesPerKind[3],
                largest ? KindName(largest->pipeline) : "-",
                largest ? largest->indexCount : 0u,
                largest ? largest->vertexCount : 0u,
                zeroScissorBatches, zeroScissorVerts,
                trisSmallBatches, trisLargeBatches);

            std::sort(trisGroups.begin(), trisGroups.end(), [](const TrisGroup& a, const TrisGroup& b) {
                return a.vertexCount > b.vertexCount;
            });

            const std::size_t topN = std::min<std::size_t>(trisGroups.size(), 6);
            for (std::size_t i = 0; i < topN; ++i)
            {
                const TrisGroup& g = trisGroups[i];
                const std::uint64_t tris = g.vertexCount / 3;
                if (g.minX <= g.maxX)
                {
                    LOG_TRACE(
                        "  Tris region #{}: scissor [{},{} {}x{}], {} batches, {} tris, {} verts, AABB [{:.1f},{:.1f} {:.1f}x{:.1f}]",
                        i + 1, g.scissor.x, g.scissor.y, g.scissor.w, g.scissor.h,
                        g.batchCount, tris, g.vertexCount,
                        g.minX, g.minY, g.maxX - g.minX, g.maxY - g.minY);
                }
                else
                {
                    LOG_TRACE(
                        "  Tris region #{}: scissor [{},{} {}x{}], {} batches, {} tris, {} verts, AABB [none]",
                        i + 1, g.scissor.x, g.scissor.y, g.scissor.w, g.scissor.h,
                        g.batchCount, tris, g.vertexCount);
                }
            }

            return true;
        }
    } // namespace

    struct RHIPainter::Impl final : TextureResolver
    {
        Impl() : canvas(*this)
        {
        }

        rhi::RHI* rhi = nullptr;
        std::uint32_t framesInFlight = 1;

        FrameRings rings;
        RHICanvas canvas;

        rhi::SamplerHandle linearSampler{};
        rhi::SamplerHandle nearestSampler{};

        struct Plane
        {
            rhi::TextureHandle texture{};
            rhi::TextureViewHandle view{};
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint64_t version = static_cast<std::uint64_t>(-1);
        };

        Plane coverage;
        Plane color;

        Plane whiteMask; // 1×1 R8 = 255: the clip mask bound when a draw is unclipped/analytic
        std::vector<Plane> maskTargets; // transient R8 clip masks for this frame (one per arbitrary clip)

        struct Image
        {
            rhi::TextureHandle texture{};
            rhi::TextureViewHandle view{};
        };

        class ImageTable : public RefCounted
        {
        public:
            explicit ImageTable(rhi::RHI& rhi) : m_rhi(rhi) {}
            ~ImageTable() override
            {
                for (const Image& im : entries)
                    if (im.texture.IsValid()) m_rhi.Resources().Destroy(im.texture);
            }

            std::vector<Image> entries; // ImageHandle.id - 1 = index

        private:
            rhi::RHI& m_rhi;
        };

        Ref<ImageTable> images;

        struct DeferredFree
        {
            std::uint64_t stamp = 0;
            rhi::TextureHandle texture{};
        };

        std::vector<DeferredFree> deferred;

        rhi::BufferHandle quadIndexBuffer{};
        std::uint32_t quadIndexCapacity = 0; // number of uint32 indices

        struct DeferredBufferFree
        {
            std::uint64_t stamp = 0;
            rhi::BufferHandle buffer{};
        };
        std::vector<DeferredBufferFree> deferredBuffers;

        std::uint64_t frameCounter = 0;

        // active frame
        rhi::CommandBuffer* cmd = nullptr;
        std::uint32_t frameIndex = 0;
        rhi::TextureViewHandle target{};
        rhi::Extent2D extent{};
        std::optional<rhi::Color> clearColor{}; // set → main pass clears the (UI-owned) surface; unset → loadOp=Load
        float contentScale = 1.f;
        PushBlock pushBase{};

        LumoraPipelines pipelines;
        std::array<bool, 5> loggedMissing{}; // indexed by PipelineKind (Rect..Mask)
        bool ringFailureReported = false;
        std::uint64_t reportHighWaterBytes = 0;
        std::uint32_t reportLowFrames = 0;

        [[nodiscard]] Plane& PlaneFor(AtlasKind kind) noexcept
        {
            return kind == AtlasKind::Coverage ? coverage : color;
        }

        TextureRef AtlasRef(AtlasKind kind) override
        {
            return {PlaneFor(kind).view, linearSampler, kind == AtlasKind::Coverage};
        }

        TextureRef ImageRef(ImageHandle image) override
        {
            if (!images || !image.Valid() || image.id > images->entries.size())
            {
                return {};
            }
            return {images->entries[image.id - 1].view, linearSampler, false};
        }

        void DrainDeferred()
        {
            for (auto it = deferred.begin(); it != deferred.end();)
            {
                if (frameCounter - it->stamp >= framesInFlight)
                {
                    rhi->Resources().Destroy(it->texture);
                    it = deferred.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (auto it = deferredBuffers.begin(); it != deferredBuffers.end();)
            {
                if (frameCounter - it->stamp >= framesInFlight)
                {
                    rhi->Resources().Destroy(it->buffer);
                    it = deferredBuffers.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void EnsureQuadIndexBuffer(std::uint32_t neededIndices)
        {
            if (quadIndexBuffer.IsValid() && quadIndexCapacity >= neededIndices)
            {
                return;
            }
            constexpr std::uint32_t kMinIndices = 6u * 1024u; // 1024 quads = 24 KiB
            const std::uint32_t newCap = std::max(neededIndices, quadIndexCapacity == 0 ? kMinIndices : quadIndexCapacity + quadIndexCapacity / 2u);
            const std::uint32_t alignedCap = ((newCap + 5u) / 6u) * 6u;

            std::vector<std::uint32_t> pattern(alignedCap);
            const std::uint32_t numQuads = alignedCap / 6u;
            for (std::uint32_t q = 0; q < numQuads; ++q)
            {
                const std::uint32_t v = q * 4u;
                pattern[q * 6u + 0] = v + 0;
                pattern[q * 6u + 1] = v + 1;
                pattern[q * 6u + 2] = v + 2;
                pattern[q * 6u + 3] = v + 0;
                pattern[q * 6u + 4] = v + 2;
                pattern[q * 6u + 5] = v + 3;
            }

            if (quadIndexBuffer.IsValid())
            {
                deferredBuffers.push_back({frameCounter, quadIndexBuffer});
            }

            quadIndexBuffer = rhi->Resources().CreateBuffer({
                .size = alignedCap * sizeof(std::uint32_t),
                .usage = rhi::BufferUsage::Index,
                .memory = rhi::MemoryPattern::CpuToGpuMapped,
                .debugName = "Lumora.StaticQuadIndices",
                .preferHost = true
            });
            quadIndexCapacity = quadIndexBuffer.IsValid() ? alignedCap : 0;
            if (quadIndexBuffer.IsValid())
            {
                void* ptr = rhi->Resources().GetMappedPointer(quadIndexBuffer);
                if (ptr != nullptr)
                {
                    std::memcpy(ptr, pattern.data(), pattern.size() * sizeof(std::uint32_t));
                }
            }
        }

        void WarnMissing(PipelineKind kind)
        {
            const auto idx = static_cast<std::size_t>(kind);
            if (!loggedMissing[idx])
            {
                LOG_WARN("no pipeline for {} batches yet (pipelines land in a later phase); skipping draw",
                         KindName(kind));
                loggedMissing[idx] = true;
            }
        }

        // Grow-only, 64px-bucketed R8 clip-mask target. The mask geometry maps its bbox onto the FULL target extent and the
        // main pass samples [0,1] over it, so any size >= the bucket works.
        Plane& EnsureMaskTarget(std::size_t i, std::uint32_t w, std::uint32_t h)
        {
            const std::uint32_t bw = (std::max(1u, w) + 63u) & ~63u;
            const std::uint32_t bh = (std::max(1u, h) + 63u) & ~63u;
            Plane& p = maskTargets[i];
            if (!p.texture.IsValid() || p.width < bw || p.height < bh)
            {
                if (p.texture.IsValid())
                {
                    deferred.push_back({frameCounter, p.texture});
                }
                const std::uint32_t nw = std::max(bw, p.width);
                const std::uint32_t nh = std::max(bh, p.height);
                p.texture = rhi->Resources().CreateTexture({
                    .type = rhi::TextureType::e2D,
                    .extent = {nw, nh, 1},
                    .format = rhi::Format::R8_Unorm,
                    .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
                    .debugName = "Lumora.ClipMask"
                });
                p.view = rhi->Resources().GetDefaultView(p.texture);
                p.width = nw;
                p.height = nh;
            }
            return p;
        }
    };

    RHIPainter::RHIPainter() : _impl(std::make_unique<Impl>())
    {
    }

    RHIPainter::~RHIPainter() = default;

    void RHIPainter::Init(rhi::RHI& rhi)
    {
        _impl->rhi = &rhi;
        _impl->images = MakeRef<Impl::ImageTable>(rhi);
        _impl->framesInFlight = std::max(1u, rhi.GetFramesInFlight());
        _impl->rings.Init(rhi, _impl->framesInFlight);

        _impl->linearSampler = rhi.Resources().CreateSampler({
            .magFilter = rhi::Filter::Linear,
            .minFilter = rhi::Filter::Linear,
            .mipmapMode = rhi::MipmapMode::Nearest,
            .addressU = rhi::AddressMode::ClampToEdge,
            .addressV = rhi::AddressMode::ClampToEdge,
            .addressW = rhi::AddressMode::ClampToEdge,
            .maxLod = 0.f,
            .debugName = "Lumora.LinearClamp"
        });
        _impl->nearestSampler = rhi.Resources().CreateSampler({
            .magFilter = rhi::Filter::Nearest,
            .minFilter = rhi::Filter::Nearest,
            .mipmapMode = rhi::MipmapMode::Nearest,
            .addressU = rhi::AddressMode::ClampToEdge,
            .addressV = rhi::AddressMode::ClampToEdge,
            .addressW = rhi::AddressMode::ClampToEdge,
            .maxLod = 0.f,
            .debugName = "Lumora.NearestClamp"
        });

        // 1x1 white R8 bound for every unclipped/analytic draw so the mask descriptor is always valid (the clipMode branch returns early).
        _impl->whiteMask.texture = rhi.Resources().CreateTexture({
            .type = rhi::TextureType::e2D,
            .extent = {1, 1, 1},
            .format = rhi::Format::R8_Unorm,
            .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
            .debugName = "Lumora.WhiteMask"
        });
        _impl->whiteMask.view = rhi.Resources().GetDefaultView(_impl->whiteMask.texture);
        _impl->whiteMask.width = 1;
        _impl->whiteMask.height = 1;
        const std::uint8_t whitePixel = 255;
        rhi::UploadRequest whiteReq{};
        whiteReq.dstTexture = _impl->whiteMask.texture;
        whiteReq.data = &whitePixel;
        whiteReq.size = 1;
        whiteReq.textureRegion.extent = {1, 1, 1};
        rhi.Uploads().UploadBlocking({&whiteReq, 1});

        LOG_INFO("RHIPainter initialised ({} frames in flight)", _impl->framesInFlight);
    }

    void RHIPainter::Shutdown()
    {
        if (!_impl->rhi)
        {
            return;
        }
        rhi::RHIResources& res = _impl->rhi->Resources();
        for (Impl::DeferredFree& d : _impl->deferred)
        {
            res.Destroy(d.texture);
        }
        _impl->deferred.clear();
        _impl->images = nullptr;
        if (_impl->coverage.texture.IsValid()) res.Destroy(_impl->coverage.texture);
        if (_impl->color.texture.IsValid()) res.Destroy(_impl->color.texture);
        _impl->coverage = {};
        _impl->color = {};
        if (_impl->whiteMask.texture.IsValid()) res.Destroy(_impl->whiteMask.texture);
        _impl->whiteMask = {};
        for (Impl::Plane& m : _impl->maskTargets)
        {
            if (m.texture.IsValid()) res.Destroy(m.texture);
        }
        _impl->maskTargets.clear();
        if (_impl->linearSampler.IsValid()) res.Destroy(_impl->linearSampler);
        if (_impl->nearestSampler.IsValid()) res.Destroy(_impl->nearestSampler);
        _impl->linearSampler = {};
        _impl->nearestSampler = {};
        if (_impl->quadIndexBuffer.IsValid())
        {
            res.Destroy(_impl->quadIndexBuffer);
            _impl->quadIndexBuffer = {};
            _impl->quadIndexCapacity = 0;
        }
        for (Impl::DeferredBufferFree& d : _impl->deferredBuffers)
        {
            res.Destroy(d.buffer);
        }
        _impl->deferredBuffers.clear();
        _impl->pipelines.Destroy(*_impl->rhi);
        _impl->rings.Shutdown();
        _impl->rhi = nullptr;
    }

    bool RHIPainter::LoadPipelines(std::string_view spirvDir, rhi::Format colorFormat, const SpirvLoader& loader)
    {
        if (!_impl->rhi)
        {
            return false;
        }
        _impl->loggedMissing = {};
        return _impl->pipelines.Create(*_impl->rhi, spirvDir, colorFormat, loader);
    }

    void RHIPainter::SyncGlyphAtlas(AtlasKind kind, const AtlasView& view)
    {
        Impl::Plane& plane = _impl->PlaneFor(kind);
        if (view.pixels == nullptr || view.width <= 0 || view.height <= 0)
        {
            return;
        }
        if (plane.version == view.version)
        {
            return; // version-gated: nothing changed
        }

        const auto w = static_cast<std::uint32_t>(view.width);
        const auto h = static_cast<std::uint32_t>(view.height);
        rhi::RHIResources& res = _impl->rhi->Resources();
        bool recreated = false;
        if (!plane.texture.IsValid() || plane.width != w || plane.height != h)
        {
            if (plane.texture.IsValid())
            {
                _impl->deferred.push_back({_impl->frameCounter, plane.texture});
            }
            plane.texture = res.CreateTexture({
                .type = rhi::TextureType::e2D,
                .extent = {w, h, 1},
                .format = FormatOf(view.format),
                .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                .debugName = kind == AtlasKind::Coverage
                                 ? "Lumora.AtlasCoverage"
                                 : "Lumora.AtlasColor"
            });
            plane.view = res.GetDefaultView(plane.texture);
            plane.width = w;
            plane.height = h;
            recreated = true;
        }

        const std::size_t bpp = BytesPerPixel(view.format);
        rhi::UploadRequest req{};
        req.dstTexture = plane.texture;
        // Upload only changed full-width rows when the size is unchanged and the dirty range is a proper subset; a new
        // texture, or an empty range on a version bump, takes the whole plane.
        const bool partial = !recreated && view.dirtyY1 > view.dirtyY0 &&
            (view.dirtyY0 > 0 || view.dirtyY1 < view.height);
        if (partial)
        {
            const auto rows = static_cast<std::uint32_t>(view.dirtyY1 - view.dirtyY0);
            req.data = view.pixels + static_cast<std::size_t>(view.dirtyY0) * view.width * bpp;
            req.size = static_cast<std::size_t>(w) * rows * bpp;
            req.textureRegion.offset = {0, view.dirtyY0, 0};
            req.textureRegion.extent = {w, rows, 1};
        }
        else
        {
            req.data = view.pixels;
            req.size = static_cast<std::size_t>(w) * h * bpp;
            req.textureRegion.extent = {w, h, 1};
        }
        if (recreated) _impl->rhi->Uploads().UploadBlocking({&req, 1});
        else _impl->rhi->Uploads().UpdateBlocking({&req, 1});

        plane.version = view.version;
    }

    ImageHandle RHIPainter::UploadTexture(std::span<const std::byte> pixels, std::uint32_t width,
                                          std::uint32_t height, AtlasFormat format)
    {
        if (width == 0 || height == 0)
        {
            return {};
        }
        rhi::RHIResources& res = _impl->rhi->Resources();
        const rhi::TextureHandle tex = res.CreateTexture({
            .type = rhi::TextureType::e2D,
            .extent = {width, height, 1},
            .format = FormatOf(format),
            .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
            .debugName = "Lumora.Image"
        });
        const rhi::TextureViewHandle view = res.GetDefaultView(tex);

        if (!pixels.empty())
        {
            rhi::UploadRequest req{};
            req.dstTexture = tex;
            req.data = pixels.data();
            req.size = pixels.size_bytes();
            req.textureRegion.extent = {width, height, 1};
            _impl->rhi->Uploads().UploadBlocking({&req, 1});
        }

        _impl->images->entries.push_back({tex, view});
        return ImageHandle{
            static_cast<std::uint64_t>(_impl->images->entries.size()), static_cast<float>(width),
            static_cast<float>(height)
        };
    }

    void RHIPainter::FreeTexture(ImageHandle image)
    {
        if (!_impl->images || !image.Valid() || image.id > _impl->images->entries.size())
        {
            return;
        }
        Impl::Image& im = _impl->images->entries[image.id - 1];
        if (im.texture.IsValid())
        {
            _impl->deferred.push_back({_impl->frameCounter, im.texture});
        }
        im = {};
    }

    ImageHandle RHIPainter::RegisterExternalTexture(rhi::TextureViewHandle view, std::uint32_t width,
                                                    std::uint32_t height)
    {
        if (!view.IsValid() || width == 0 || height == 0)
        {
            return {};
        }
        // Invalid texture handle -> the slot borrows the view; FreeTexture won't destroy it.
        _impl->images->entries.push_back({rhi::TextureHandle{}, view});
        return ImageHandle{
            static_cast<std::uint64_t>(_impl->images->entries.size()), static_cast<float>(width),
            static_cast<float>(height)
        };
    }

    void RHIPainter::ShareImagesWith(RHIPainter& owner)
    {
        if (!owner._impl->images) return;
        _impl->images = owner._impl->images;
    }

    Canvas& RHIPainter::BeginFrame(rhi::CommandBuffer& cmd, std::uint32_t frameIndex,
                                   rhi::TextureViewHandle target, rhi::Extent2D extent,
                                   std::optional<rhi::Color> clear)
    {
        _impl->cmd = &cmd;
        _impl->frameIndex = frameIndex;
        _impl->target = target;
        _impl->extent = extent;
        _impl->clearColor = clear;
        ++_impl->frameCounter;
        _impl->DrainDeferred();

        _impl->pushBase.scale[0] = extent.width > 0 ? 2.f / static_cast<float>(extent.width) : 0.f;
        _impl->pushBase.scale[1] = extent.height > 0 ? 2.f / static_cast<float>(extent.height) : 0.f;
        _impl->pushBase.translate[0] = -1.f;
        _impl->pushBase.translate[1] = -1.f;
        _impl->canvas.SetContentScale(_impl->contentScale);
        _impl->canvas.BeginFrame({static_cast<float>(extent.width), static_cast<float>(extent.height)});

        // The main rendering scope opens in EndFrame, AFTER the clip-mask pre-pass; only the builder is touched until then
        // (no GPU commands).
        return _impl->canvas;
    }

    void RHIPainter::EndFrame()
    {
        _impl->canvas.EndFrame();
        rhi::CommandBuffer& cmd = *_impl->cmd;
        rhi::RHIResources& res = _impl->rhi->Resources();

        const std::span<const UiVertex> verts = _impl->canvas.Builder().Vertices();
        const std::span<const Batch> batches = _impl->canvas.Builder().Batches();
        const std::span<const UiVertex> maskVerts = _impl->canvas.MaskVertices();
        const std::span<const MaskRequest> maskReqs = _impl->canvas.MaskRequests();
        const std::uint64_t generation = _impl->canvas.Builder().Generation();

        std::uint32_t maxQuadIndices = 0;
        for (const Batch& b : batches)
        {
            if (b.pipeline != PipelineKind::Tris)
            {
                maxQuadIndices = std::max(maxQuadIndices, b.indexCount);
            }
        }
        if (maxQuadIndices > 0)
        {
            _impl->EnsureQuadIndexBuffer(maxQuadIndices);
        }

        // One vertex buffer holds the main UI geometry then every mask's coverage geometry; quads use the shared static index
        // pattern, tris draw non-indexed.
        rhi::BufferHandle vbuf{};
        std::uint32_t maskBaseVertex = 0;
        const std::uint64_t vbytes = verts.size_bytes() + maskVerts.size_bytes();

        if (vbytes < kRingReportBytes)
        {
            if (++_impl->reportLowFrames >= 600)
            {
                _impl->reportHighWaterBytes = 0;
                _impl->reportLowFrames = 0;
            }
        }
        else
        {
            _impl->reportLowFrames = 0;
        }

        if (!verts.empty() || !maskVerts.empty())
        {
            const FrameRings::Frame ring = _impl->rings.Prepare(_impl->frameIndex, vbytes, generation);
            const bool wantsReport = vbytes >= kRingReportBytes &&
                (vbytes >= _impl->reportHighWaterBytes + _impl->reportHighWaterBytes / 4);
            if (wantsReport)
            {
                if (ReportGeometry(verts, maskVerts.size(), batches, vbytes,
                                   _impl->rings.VertexCapacity(_impl->frameIndex)))
                {
                    _impl->reportHighWaterBytes = vbytes;
                }
            }

            auto* vp = ring.Valid() ? static_cast<std::byte*>(res.GetMappedPointer(ring.vertex)) : nullptr;
            if (vp != nullptr)
            {
                if (ring.needsUpload)
                {
                    std::memcpy(vp, verts.data(), verts.size_bytes());
                    std::memcpy(vp + verts.size_bytes(), maskVerts.data(), maskVerts.size_bytes());
                    _impl->rings.MarkUploaded(_impl->frameIndex, generation);
                }
                vbuf = ring.vertex;
                maskBaseVertex = static_cast<std::uint32_t>(verts.size());
                _impl->ringFailureReported = false;
            }
            else
            {
                _impl->rings.Invalidate();
                if (!std::exchange(_impl->ringFailureReported, true))
                {
                    LOG_ERROR("UI geometry ring unavailable ({} bytes requested); UI frames are skipped until an "
                              "allocation succeeds", vbytes);
                }
            }
        }

        // Clip-mask pre-pass: rasterize each arbitrary clip's coverage into its own R8 target
        const rhi::PipelineHandle maskPipe = _impl->pipelines.Get(PipelineKind::Mask);
        if (!maskReqs.empty() && vbuf.IsValid() && maskPipe.IsValid())
        {
            if (_impl->maskTargets.size() < maskReqs.size())
            {
                _impl->maskTargets.resize(maskReqs.size());
            }
            for (std::size_t i = 0; i < maskReqs.size(); ++i)
            {
                const MaskRequest& m = maskReqs[i];
                Impl::Plane& tgt = _impl->EnsureMaskTarget(
                    i, static_cast<std::uint32_t>(std::ceil(m.bounds.width)),
                    static_cast<std::uint32_t>(std::ceil(m.bounds.height)));

                // This RHI manages layouts explicitly (BeginRendering does not auto-transition; the main pass trusts its
                // target is already ColorAttachment).
                // Undefined as the old layout discards prior contents, which is fine since we Clear; works for a fresh target
                // and one reused in ShaderReadOnly.
                const rhi::TextureBarrier toColor{
                    .texture = tgt.texture,
                    .srcStage = rhi::PipelineStage::TopOfPipe,
                    .srcAccess = rhi::AccessMask::None,
                    .dstStage = rhi::PipelineStage::ColorAttachment,
                    .dstAccess = rhi::AccessMask::ColorAttachmentWrite,
                    .oldLayout = rhi::ImageLayout::Undefined,
                    .newLayout = rhi::ImageLayout::ColorAttachment
                };
                cmd.Barrier({&toColor, 1}, {});

                rhi::ColorAttachment att{};
                att.view = tgt.view;
                att.layout = rhi::ImageLayout::ColorAttachment;
                att.loadOp = rhi::LoadOp::Clear;
                att.storeOp = rhi::StoreOp::Store;
                att.clearColor = rhi::Color{0.f, 0.f, 0.f, 0.f};
                rhi::RenderingInfo info{};
                info.renderArea = {{0, 0}, {tgt.width, tgt.height}};
                info.colorAttachments = {&att, 1};
                cmd.BeginRendering(info);
                cmd.SetViewport(
                    {0.f, 0.f, static_cast<float>(tgt.width), static_cast<float>(tgt.height), 0.f, 1.f});
                cmd.SetScissor({{0, 0}, {tgt.width, tgt.height}});
                cmd.BindPipeline(maskPipe);
                cmd.BindVertexBuffer(0, vbuf, 0);

                // Map the surface-pixel bbox onto the whole target (scale 2/bbox, origin folded into translate).
                MaskPushBlock mp{};
                mp.scale[0] = 2.f / m.bounds.width;
                mp.scale[1] = 2.f / m.bounds.height;
                mp.translate[0] = -1.f - 2.f * m.bounds.x / m.bounds.width;
                mp.translate[1] = -1.f - 2.f * m.bounds.y / m.bounds.height;
                const auto* mpb = reinterpret_cast<const std::byte*>(&mp);
                cmd.PushConstants(rhi::ShaderStageFlags::Vertex | rhi::ShaderStageFlags::Fragment, 0,
                                  std::span<const std::byte>(mpb, sizeof(MaskPushBlock)));
                cmd.Draw(m.vertexCount, 1, maskBaseVertex + m.firstVertex, 0);
                cmd.EndRendering();

                const rhi::TextureBarrier bar{
                    .texture = tgt.texture,
                    .srcStage = rhi::PipelineStage::ColorAttachment,
                    .srcAccess = rhi::AccessMask::ColorAttachmentWrite,
                    .dstStage = rhi::PipelineStage::FragmentShader,
                    .dstAccess = rhi::AccessMask::ShaderRead,
                    .oldLayout = rhi::ImageLayout::ColorAttachment,
                    .newLayout = rhi::ImageLayout::ShaderReadOnly
                };
                cmd.Barrier({&bar, 1}, {});
            }
        }

        // Main pass: opened after the mask pre-pass. Clear when the UI owns the surface, else Load to composite over a scene
        // already in the target.
        rhi::ColorAttachment color{};
        color.view = _impl->target;
        color.layout = rhi::ImageLayout::ColorAttachment;
        color.loadOp = _impl->clearColor ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
        color.clearColor = _impl->clearColor.value_or(rhi::Color{});
        color.storeOp = rhi::StoreOp::Store;
        rhi::RenderingInfo mainInfo{};
        mainInfo.renderArea = {{0, 0}, {_impl->extent.width, _impl->extent.height}};
        mainInfo.colorAttachments = {&color, 1};
        cmd.BeginRendering(mainInfo);
        cmd.SetViewport({
            0.f, 0.f, static_cast<float>(_impl->extent.width),
            static_cast<float>(_impl->extent.height), 0.f, 1.f
        });
        cmd.SetScissor({{0, 0}, {_impl->extent.width, _impl->extent.height}});

        if (!batches.empty() && vbuf.IsValid())
        {
            const std::span<const TextureRef> textures = _impl->canvas.Textures();

            // The R8 clip mask for a batch: the arbitrary-clip target for Mask batches, else 1×1 white.
            const auto maskViewFor = [&](const ClipRef& c) -> rhi::TextureViewHandle
            {
                if (c.mode == ClipMode::Mask && c.maskId != 0 && c.maskId <= _impl->maskTargets.size())
                {
                    const Impl::Plane& p = _impl->maskTargets[c.maskId - 1];
                    if (p.view.IsValid()) { return p.view; }
                }
                return _impl->whiteMask.view;
            };

            bool boundBuffers = false;
            bool havePipeline = false;
            PipelineKind lastPipeline{};
            bool haveScissor = false;
            ScissorRect lastScissor{};
            for (const Batch& b : batches)
            {
                const rhi::PipelineHandle pipe = _impl->pipelines.Get(b.pipeline);
                if (!pipe.IsValid())
                {
                    _impl->WarnMissing(b.pipeline);
                    continue;
                }
                if (!boundBuffers)
                {
                    cmd.BindVertexBuffer(0, vbuf, 0);
                    if (_impl->quadIndexBuffer.IsValid())
                    {
                        cmd.BindIndexBuffer(_impl->quadIndexBuffer, 0, rhi::IndexType::Uint32);
                    }
                    boundBuffers = true;
                }
                if (!havePipeline || lastPipeline != b.pipeline)
                {
                    cmd.BindPipeline(pipe);
                    havePipeline = true;
                    lastPipeline = b.pipeline;
                }

                // Every pipeline samples the clip mask; Textured also samples its content at binding 0.
                const rhi::TextureViewHandle maskView = maskViewFor(b.clip);
                const rhi::DescriptorWrite maskWrite{
                    .binding = (b.pipeline == PipelineKind::Textured) ? 1u : 0u,
                    .type = rhi::DescriptorType::CombinedImageSampler,
                    .imageInfo = {
                        .view = maskView, .layout = rhi::ImageLayout::ShaderReadOnly,
                        .sampler = _impl->linearSampler
                    }
                };

                PushBlock push = _impl->pushBase;
                if (b.pipeline == PipelineKind::Textured)
                {
                    const TextureRef ref = (b.textureKey < textures.size())
                                               ? textures[b.textureKey]
                                               : TextureRef{};
                    const bool valid = ref.view.IsValid();
                    push.mode = (valid && !ref.alphaMask) ? 1u : 0u;
                    const rhi::DescriptorWrite content{
                        .binding = 0,
                        .type = rhi::DescriptorType::CombinedImageSampler,
                        .imageInfo = {
                            .view = valid ? ref.view : _impl->whiteMask.view,
                            .layout = rhi::ImageLayout::ShaderReadOnly,
                            .sampler = valid ? ref.sampler : _impl->linearSampler
                        }
                    };
                    const std::array<rhi::DescriptorWrite, 2> writes{content, maskWrite};
                    cmd.PushDescriptors(0, writes);
                }
                else
                {
                    cmd.PushDescriptors(0, {&maskWrite, 1});
                }

                if (b.pipeline == PipelineKind::Rect)
                {
                    push.radius = b.rectRadius;
                    push.borderWidth = b.rectBorderWidth;
                    push.borderColor = b.rectBorderColor;
                    push.halfSize[0] = b.rectHalf.x;
                    push.halfSize[1] = b.rectHalf.y;
                    push.borderDash = b.rectBorderDash;
                }
                else if (b.pipeline == PipelineKind::Sdf)
                {
                    // The Sdf block diverges from PushBlock past offset 20 but shares the clip tail at 48.
                    SdfPushBlock sp;
                    sp.scale[0] = push.scale[0];
                    sp.scale[1] = push.scale[1];
                    sp.translate[0] = push.translate[0];
                    sp.translate[1] = push.translate[1];
                    sp.shapeKind = b.sdfKind;
                    sp.radius = b.sdfRadius;
                    sp.halfWidth = b.sdfHalfWidth;
                    sp.apertureSinCos[0] = b.sdfApertureSinCos.x;
                    sp.apertureSinCos[1] = b.sdfApertureSinCos.y;
                    sp.halfSize[0] = b.sdfHalf.x;
                    sp.halfSize[1] = b.sdfHalf.y;
                    FillClipTail(sp.clip, b.clip);
                    const auto* sdfBytes = reinterpret_cast<const std::byte*>(&sp);
                    cmd.PushConstants(rhi::ShaderStageFlags::Vertex | rhi::ShaderStageFlags::Fragment, 0,
                                      std::span<const std::byte>(sdfBytes, sizeof(SdfPushBlock)));
                    if (!haveScissor || !(lastScissor == b.scissor))
                    {
                        cmd.SetScissor({{b.scissor.x, b.scissor.y}, {b.scissor.w, b.scissor.h}});
                        haveScissor = true;
                        lastScissor = b.scissor;
                    }
                    cmd.DrawIndexed(b.indexCount, 1, 0, static_cast<int32_t>(b.firstVertex), 0);
                    continue;
                }
                FillClipTail(push.clip, b.clip);
                const auto* pushBytes = reinterpret_cast<const std::byte*>(&push);
                cmd.PushConstants(rhi::ShaderStageFlags::Vertex | rhi::ShaderStageFlags::Fragment, 0,
                                  std::span<const std::byte>(pushBytes, sizeof(PushBlock)));
                if (!haveScissor || !(lastScissor == b.scissor))
                {
                    cmd.SetScissor({{b.scissor.x, b.scissor.y}, {b.scissor.w, b.scissor.h}});
                    haveScissor = true;
                    lastScissor = b.scissor;
                }
                if (b.pipeline == PipelineKind::Tris)
                {
                    cmd.Draw(b.vertexCount, 1, b.firstVertex, 0);
                }
                else
                {
                    cmd.DrawIndexed(b.indexCount, 1, 0, static_cast<int32_t>(b.firstVertex), 0);
                }
            }
        }

        cmd.EndRendering();
        _impl->cmd = nullptr;
    }

    void RHIPainter::SetContentScale(float scale)
    {
        const float newScale = scale > 0.f ? scale : 1.f;
        if (_impl->contentScale != newScale)
        {
            _impl->contentScale = newScale;
            _impl->rings.Invalidate();
        }
    }

    void RHIPainter::DrawRhiImage(rhi::TextureViewHandle view, const Rect& dst, rhi::SamplerHandle sampler)
    {
        _impl->canvas.DrawExternal(view, sampler.IsValid() ? sampler : _impl->linearSampler, dst);
    }
} // namespace lumora
