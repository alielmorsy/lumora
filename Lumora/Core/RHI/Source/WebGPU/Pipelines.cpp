#include "WebGPURHI.h"

#include <algorithm>
#include <string>

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

DescriptorLayoutHandle RHIDescriptors::CreateDescriptorLayout(const DescriptorLayoutDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    // CombinedImageSampler at binding N becomes texture 2N + sampler 2N+1; the rest keep binding N.
    std::vector<WGPUBindGroupLayoutEntry> entries;
    for (const DescriptorBinding& b : desc.bindings) {
        if (b.count != 1)
            LOG_WARN("Descriptor arrays are not supported on WebGPU (binding {})", b.binding);
        WGPUBindGroupLayoutEntry e{};
        e.binding    = b.binding;
        e.visibility = detail::ToWgpu(b.stages);
        switch (b.type) {
        case DescriptorType::CombinedImageSampler: {
            e.binding                 = 2 * b.binding;
            e.texture.sampleType      = WGPUTextureSampleType_Float;
            e.texture.viewDimension   = WGPUTextureViewDimension_2D;
            entries.push_back(e);
            WGPUBindGroupLayoutEntry s{};
            s.binding      = 2 * b.binding + 1;
            s.visibility   = e.visibility;
            s.sampler.type = WGPUSamplerBindingType_Filtering;
            entries.push_back(s);
            continue;
        }
        case DescriptorType::Sampler: e.sampler.type = WGPUSamplerBindingType_Filtering; break;
        case DescriptorType::SampledImage:
            e.texture.sampleType    = WGPUTextureSampleType_Float;
            e.texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case DescriptorType::StorageImage:
            // ponytail: format fixed to rgba8unorm; carry the format on DescriptorBinding if storage images get used.
            e.storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
            e.storageTexture.format        = WGPUTextureFormat_RGBA8Unorm;
            e.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case DescriptorType::UniformBuffer: e.buffer.type = WGPUBufferBindingType_Uniform; break;
        case DescriptorType::StorageBuffer: e.buffer.type = WGPUBufferBindingType_Storage; break;
        }
        entries.push_back(e);
    }

    WGPUBindGroupLayoutDescriptor ld{};
    ld.label      = detail::Str(desc.debugName);
    ld.entryCount = entries.size();
    ld.entries    = entries.data();

    detail::DescriptorLayout dl{};
    dl.layout = wgpuDeviceCreateBindGroupLayout(n.device, &ld);
    dl.bindings.assign(desc.bindings.begin(), desc.bindings.end());
    return n.descriptorLayouts.Add(std::move(dl));
}

void RHIDescriptors::Destroy(DescriptorLayoutHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::DescriptorLayout dl = n.descriptorLayouts.Remove(h);
    if (!dl.layout)
        return;
    n.ClearBindGroups();
    n.Defer([layout = dl.layout]() { wgpuBindGroupLayoutRelease(layout); });
}

PipelineHandle RHIPipelines::CreatePipeline(const PipelineDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    uint32_t pushEnd = 0;
    for (const PushConstantRange& p : desc.pushConstants)
        pushEnd = std::max(pushEnd, p.offset + p.size);
    const uint32_t pushSize = (pushEnd + 15) & ~15u;

    std::vector<WGPUBindGroupLayout> groups;
    for (DescriptorLayoutHandle h : desc.setLayouts) {
        const detail::DescriptorLayout* dl = n.descriptorLayouts.Get(h);
        groups.push_back(dl ? dl->layout : nullptr);
    }
    if (pushSize)
        groups.push_back(n.pushLayout);

    WGPUPipelineLayoutDescriptor pld{};
    pld.label                = detail::Str(desc.debugName);
    pld.bindGroupLayoutCount = groups.size();
    pld.bindGroupLayouts     = groups.data();
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(n.device, &pld);

    // One module per distinct source: both stages usually pass the same WGSL text.
    std::vector<std::pair<std::span<const std::byte>, WGPUShaderModule>> modules;
    const auto moduleFor = [&](const ShaderStageDesc& s) {
        for (auto& [code, m] : modules)
            if (code.data() == s.code.data() && code.size() == s.code.size())
                return m;
        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code        = {reinterpret_cast<const char*>(s.code.data()), s.code.size()};
        WGPUShaderModuleDescriptor md{};
        md.nextInChain = &wgsl.chain;
        md.label       = detail::Str(s.debugName.empty() ? desc.debugName : s.debugName);
        WGPUShaderModule m = wgpuDeviceCreateShaderModule(n.device, &md);
        modules.emplace_back(s.code, m);
        return m;
    };

    // Slots are indexed by binding number; gaps stay zeroed (undefined step mode = unused slot).
    uint32_t slotCount = 0;
    for (const VertexBinding& vb : desc.vertex.bindings)
        slotCount = std::max(slotCount, vb.binding + 1);
    std::vector<std::vector<WGPUVertexAttribute>> attrs(slotCount);
    std::vector<WGPUVertexBufferLayout>           buffers(slotCount);
    for (const VertexBinding& vb : desc.vertex.bindings) {
        WGPUVertexBufferLayout& l = buffers[vb.binding];
        l.stepMode    = vb.perInstance ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
        l.arrayStride = vb.stride;
    }
    for (const VertexAttribute& va : desc.vertex.attributes) {
        if (va.binding >= slotCount)
            continue;
        WGPUVertexAttribute a{};
        a.format         = detail::ToWgpuVertex(va.format);
        a.offset         = va.offset;
        a.shaderLocation = va.location;
        attrs[va.binding].push_back(a);
    }
    for (uint32_t i = 0; i < slotCount; ++i) {
        buffers[i].attributeCount = attrs[i].size();
        buffers[i].attributes     = attrs[i].data();
    }

    std::vector<WGPUBlendState>       blends(desc.colorFormats.size());
    std::vector<WGPUColorTargetState> targets(desc.colorFormats.size());
    for (size_t i = 0; i < desc.colorFormats.size(); ++i) {
        targets[i].format    = detail::ToWgpu(desc.colorFormats[i]);
        targets[i].writeMask = WGPUColorWriteMask_All;
        if (i < desc.blend.attachments.size()) {
            const BlendAttachment& a = desc.blend.attachments[i];
            targets[i].writeMask     = detail::ToWgpu(a.writeMask);
            if (a.blendEnable) {
                blends[i].color = {detail::ToWgpu(a.colorBlendOp), detail::ToWgpu(a.srcColor), detail::ToWgpu(a.dstColor)};
                blends[i].alpha = {detail::ToWgpu(a.alphaBlendOp), detail::ToWgpu(a.srcAlpha), detail::ToWgpu(a.dstAlpha)};
                targets[i].blend = &blends[i];
            }
        }
    }

    std::string vsEntry, fsEntry;
    WGPUFragmentState fragment{};
    WGPURenderPipelineDescriptor rpd{};
    rpd.label  = detail::Str(desc.debugName);
    rpd.layout = layout;
    for (const ShaderStageDesc& s : desc.stages) {
        if (s.stage == ShaderStageFlags::Vertex) {
            vsEntry               = s.entryPoint;
            rpd.vertex.module     = moduleFor(s);
            rpd.vertex.entryPoint = detail::Str(vsEntry);
        } else if (s.stage == ShaderStageFlags::Fragment) {
            fsEntry             = s.entryPoint;
            fragment.module     = moduleFor(s);
            fragment.entryPoint = detail::Str(fsEntry);
            rpd.fragment        = &fragment;
        }
    }
    rpd.vertex.bufferCount = buffers.size();
    rpd.vertex.buffers     = buffers.data();
    fragment.targetCount   = targets.size();
    fragment.targets       = targets.data();

    rpd.primitive.topology  = detail::ToWgpu(desc.topology);
    rpd.primitive.frontFace = desc.rasterizer.frontFace == FrontFace::Clockwise ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
    rpd.primitive.cullMode  = detail::ToWgpu(desc.rasterizer.cullMode);
    if (desc.rasterizer.polygonMode == PolygonMode::Line)
        LOG_WARN("PolygonMode::Line is not supported on WebGPU; filling '{}'", desc.debugName);

    WGPUDepthStencilState ds{};
    if (desc.depthFormat != Format::Undefined) {
        ds.format            = detail::ToWgpu(desc.depthFormat);
        ds.depthWriteEnabled = desc.depthStencil.depthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare      = desc.depthStencil.depthTestEnable ? detail::ToWgpu(desc.depthStencil.depthCompareOp)
                                                                 : WGPUCompareFunction_Always;
        ds.stencilFront      = {WGPUCompareFunction_Always, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep,
                                WGPUStencilOperation_Keep};
        ds.stencilBack       = ds.stencilFront;
        ds.stencilReadMask   = 0xFFFFFFFF;
        ds.stencilWriteMask  = 0xFFFFFFFF;
        rpd.depthStencil     = &ds;
    }
    rpd.multisample.count = static_cast<uint32_t>(desc.samples);
    rpd.multisample.mask  = 0xFFFFFFFF;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(n.device, &rpd);
    for (auto& [code, m] : modules)
        wgpuShaderModuleRelease(m);
    wgpuPipelineLayoutRelease(layout);
    if (!pipeline)
        return {};

    detail::Pipeline p{};
    p.pipeline = pipeline;
    p.setLayouts.assign(desc.setLayouts.begin(), desc.setLayouts.end());
    p.pushSize = pushSize;
    return n.pipelines.Add(std::move(p));
}

void RHIPipelines::Destroy(PipelineHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Pipeline p = n.pipelines.Remove(h);
    if (!p.pipeline)
        return;
    n.Defer([pipeline = p.pipeline]() { wgpuRenderPipelineRelease(pipeline); });
}

} // namespace lumora::rhi
