#pragma once

#include "BatchBuilder.h" // PipelineKind

#include <lumora/rhi/RHIPainter.h>

#include <lumora/rhi/Formats.h>
#include <lumora/rhi/Handles.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace lumora::rhi { class RHI; }

namespace lumora {

// Holds the UI pipelines, indexed by PipelineKind. Shaders are HLSL compiled to SPIR-V offline; this only loads the precompiled bytes.
class LumoraPipelines {
public:
    /// Loads the precompiled SPIR-V from `spirvDir` and creates the pipelines plus the textured set layout for `colorFormat`.
    /// Returns false (draws stay gated) if any .spv is missing or a pipeline fails.
    bool Create(rhi::RHI& rhi, std::string_view spirvDir, rhi::Format colorFormat, const SpirvLoader& loader = {});
    void Destroy(rhi::RHI& rhi);

    [[nodiscard]] rhi::PipelineHandle Get(PipelineKind kind) const noexcept {
        return _pipelines[static_cast<std::size_t>(kind)];
    }
    [[nodiscard]] rhi::DescriptorLayoutHandle TexturedLayout() const noexcept { return _texturedLayout; }
    [[nodiscard]] bool Valid() const noexcept { return _pipelines[0].IsValid(); }

private:
    std::array<rhi::PipelineHandle, 5> _pipelines{}; // indexed by PipelineKind (Rect..Mask)
    rhi::DescriptorLayoutHandle        _texturedLayout{}; // binding 0 content + binding 1 clip mask
    rhi::DescriptorLayoutHandle        _maskLayout{};     // binding 0 clip mask (Rect/Tris/Sdf)
};

} // namespace lumora
