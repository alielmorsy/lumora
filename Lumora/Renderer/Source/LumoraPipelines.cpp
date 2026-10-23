#include "LumoraPipelines.h"

#include <lumora/log/Log.h>
#include <lumora/rhi/RHI.h>

#include <fstream>
#include <span>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("Lumora.RHI")

namespace lumora {

namespace {

[[nodiscard]] std::vector<std::byte> LoadSpv(std::string_view dir, std::string_view name) {
    std::string path(dir);
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }
    path += name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_ERROR("missing SPIR-V '{}'", path);
        return {};
    }
    const auto             size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

} // namespace

bool LumoraPipelines::Create(rhi::RHI& rhi, std::string_view spirvDir, rhi::Format colorFormat,
                             const SpirvLoader& loader) {
    using namespace rhi;

    // Textured samples content at binding 0 and the clip mask at binding 1; the analytic pipelines
    // (Rect/Tris/Sdf) sample only the clip mask, at binding 0. Both push-descriptor sets.
    const std::array<DescriptorBinding, 2> texBindings{
        DescriptorBinding{.binding = 0, .type = DescriptorType::CombinedImageSampler, .count = 1,
                          .stages = ShaderStageFlags::Fragment},
        DescriptorBinding{.binding = 1, .type = DescriptorType::CombinedImageSampler, .count = 1,
                          .stages = ShaderStageFlags::Fragment}};
    _texturedLayout = rhi.Descriptors().CreateDescriptorLayout(
        {.bindings = texBindings, .pushDescriptor = true, .debugName = "Lumora.TexturedSet"});

    const DescriptorBinding maskBinding{.binding = 0,
                                        .type    = DescriptorType::CombinedImageSampler,
                                        .count   = 1,
                                        .stages  = ShaderStageFlags::Fragment};
    _maskLayout = rhi.Descriptors().CreateDescriptorLayout(
        {.bindings = {&maskBinding, 1}, .pushDescriptor = true, .debugName = "Lumora.MaskSet"});

    const std::array<VertexBinding, 1>   bindings{VertexBinding{.binding = 0, .stride = 20, .perInstance = false}};
    const std::array<VertexAttribute, 3> attributes{
        VertexAttribute{.location = 0, .binding = 0, .format = Format::R32G32_Sfloat, .offset = 0},
        VertexAttribute{.location = 1, .binding = 0, .format = Format::R32G32_Sfloat, .offset = 8},
        VertexAttribute{.location = 2, .binding = 0, .format = Format::R8G8B8A8_Unorm, .offset = 16}};
    // Mask and Tris SPIR-V drops the uv input, and declaring it anyway makes the driver report an unconsumed attribute; same
    // vertex struct and stride.
    const std::array<VertexAttribute, 2> posColAttributes{attributes[0], attributes[2]};
    const VertexLayout vertex{.bindings = bindings, .attributes = attributes};
    const VertexLayout posColVertex{.bindings = bindings, .attributes = posColAttributes};

    const std::array<BlendAttachment, 1> blendAtt{
        BlendAttachment{.blendEnable  = true,
                        .srcColor     = BlendFactor::SrcAlpha,
                        .dstColor     = BlendFactor::OneMinusSrcAlpha,
                        .colorBlendOp = BlendOp::Add,
                        .srcAlpha     = BlendFactor::One,
                        .dstAlpha     = BlendFactor::OneMinusSrcAlpha,
                        .alphaBlendOp = BlendOp::Add,
                        .writeMask    = ColorComponentFlags::RGBA}};
    const BlendState        blend{.attachments = blendAtt};
    // The mask pass unions overlapping coverage with Max blend into one R8 channel; cleared to 0 each mask.
    const std::array<BlendAttachment, 1> maskAtt{
        BlendAttachment{.blendEnable  = true,
                        .srcColor     = BlendFactor::One,
                        .dstColor     = BlendFactor::One,
                        .colorBlendOp = BlendOp::Max,
                        .srcAlpha     = BlendFactor::One,
                        .dstAlpha     = BlendFactor::One,
                        .alphaBlendOp = BlendOp::Max,
                        .writeMask    = ColorComponentFlags::R}};
    const BlendState        maskBlend{.attachments = maskAtt};
    const DepthStencilState depthStencil{.depthTestEnable  = false,
                                         .depthWriteEnable = false,
                                         .depthCompareOp   = CompareOp::Always};
    const RasterizerState   rasterizer{.polygonMode = PolygonMode::Fill, .cullMode = CullMode::None};
    // Every push block ends with the shared 32-byte clip tail at offset 48, so ranges are 80 B (under the Vulkan 128 B minimum).
    const std::array<PushConstantRange, 1> basePush{
        PushConstantRange{.stages = ShaderStageFlags::Vertex | ShaderStageFlags::Fragment,
                          .offset = 0,
                          .size   = 80}};
    const std::array<PushConstantRange, 1>& rectPush = basePush;
    const std::array<PushConstantRange, 1>& sdfPush  = basePush;
    // The mask pipeline only needs scale/translate/mode (20 B) - no clip tail, no descriptors.
    const std::array<PushConstantRange, 1> maskPush{
        PushConstantRange{.stages = ShaderStageFlags::Vertex | ShaderStageFlags::Fragment,
                          .offset = 0,
                          .size   = 20}};
    const std::array<DescriptorLayoutHandle, 1> texSets{_texturedLayout};
    const std::array<DescriptorLayoutHandle, 1> maskSet{_maskLayout};

    // Vulkan: <shader>.{vert,frag}.spv. WebGPU: one <shader>.wgsl holding both entry points.
    const auto build = [&](std::string_view shader,
                           std::span<const DescriptorLayoutHandle> sets,
                           std::span<const PushConstantRange> push, const BlendState& blendIn,
                           const VertexLayout& vtx, Format fmt,
                           std::string_view name) -> PipelineHandle {
        const auto load = [&](std::string_view file) {
            if (!loader) {
                return LoadSpv(spirvDir, file);
            }
            std::vector<std::byte> bytes = loader(file);
            if (bytes.empty()) {
                LOG_ERROR("missing SPIR-V '{}'", file);
            }
            return bytes;
        };
#if defined(__EMSCRIPTEN__)
        const std::vector<std::byte> vs = load(std::string(shader) + ".wgsl");
        const std::vector<std::byte> ps = vs;
#else
        const std::vector<std::byte> vs = load(std::string(shader) + ".vert.spv");
        const std::vector<std::byte> ps = load(std::string(shader) + ".frag.spv");
#endif
        if (vs.empty() || ps.empty()) {
            return {};
        }
        const std::array<ShaderStageDesc, 2> stages{
            ShaderStageDesc{.stage = ShaderStageFlags::Vertex, .code = vs, .entryPoint = "VSMain"},
            ShaderStageDesc{.stage = ShaderStageFlags::Fragment, .code = ps, .entryPoint = "PSMain"}};
        const std::array<Format, 1> cf{fmt};
        PipelineDescription desc{};
        desc.type          = PipelineType::Graphics;
        desc.stages        = stages;
        desc.vertex        = vtx;
        desc.topology      = PrimitiveTopology::TriangleList;
        desc.blend         = blendIn;
        desc.depthStencil  = depthStencil;
        desc.rasterizer    = rasterizer;
        desc.setLayouts    = sets;
        desc.pushConstants = push;
        desc.colorFormats  = cf;
        desc.depthFormat   = Format::Undefined;
        desc.debugName     = name;
        return rhi.Pipelines().CreatePipeline(desc);
    };

    _pipelines[static_cast<std::size_t>(PipelineKind::Rect)] =
        build("LumoraRect", maskSet, rectPush, blend, vertex, colorFormat,
              "Lumora.Rect");
    _pipelines[static_cast<std::size_t>(PipelineKind::Textured)] =
        build("LumoraTextured", texSets, basePush, blend, vertex, colorFormat,
              "Lumora.Textured");
    _pipelines[static_cast<std::size_t>(PipelineKind::Tris)] =
        build("LumoraTris", maskSet, basePush, blend, posColVertex, colorFormat,
              "Lumora.Tris");
    _pipelines[static_cast<std::size_t>(PipelineKind::Sdf)] =
        build("LumoraSDF", maskSet, sdfPush, blend, vertex, colorFormat, "Lumora.Sdf");
    _pipelines[static_cast<std::size_t>(PipelineKind::Mask)] =
        build("LumoraMask", {}, maskPush, maskBlend, posColVertex, Format::R8_Unorm,
              "Lumora.Mask");

    const bool ok = _pipelines[0].IsValid() && _pipelines[1].IsValid() && _pipelines[2].IsValid() &&
        _pipelines[3].IsValid() && _pipelines[4].IsValid();
    if (ok) {
        LOG_INFO("Lumora pipelines created (color format {})", static_cast<std::uint32_t>(colorFormat));
    } else {
        LOG_WARN("Lumora pipelines incomplete; draws stay gated");
    }
    return ok;
}

void LumoraPipelines::Destroy(rhi::RHI& rhi) {
    for (auto& p : _pipelines) {
        if (p.IsValid()) {
            rhi.Pipelines().Destroy(p);
        }
        p = {};
    }
    if (_texturedLayout.IsValid()) {
        rhi.Descriptors().Destroy(_texturedLayout);
        _texturedLayout = {};
    }
    if (_maskLayout.IsValid()) {
        rhi.Descriptors().Destroy(_maskLayout);
        _maskLayout = {};
    }
}

} // namespace lumora
