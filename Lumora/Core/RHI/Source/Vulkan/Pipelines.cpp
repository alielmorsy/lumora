#include <lumora/rhi/RHI.h>
#include "VulkanRHI.h"
#include "Translate.h"

#include <string>
#include <vector>

namespace lumora::rhi {

PipelineHandle RHIPipelines::CreatePipeline(const PipelineDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    std::vector<VkDescriptorSetLayout> vkSetLayouts;
    vkSetLayouts.reserve(desc.setLayouts.size());
    for (DescriptorLayoutHandle h : desc.setLayouts) {
        if (const detail::DescriptorLayout* dl = n.descriptorLayouts.Get(h))
            vkSetLayouts.push_back(dl->layout);
        else
            vkSetLayouts.push_back(VK_NULL_HANDLE);
    }

    std::vector<VkPushConstantRange> vkRanges;
    vkRanges.reserve(desc.pushConstants.size());
    for (const auto& p : desc.pushConstants) {
        VkPushConstantRange r{};
        r.stageFlags = detail::ToVk(p.stages);
        r.offset     = p.offset;
        r.size       = p.size;
        vkRanges.push_back(r);
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = static_cast<uint32_t>(vkSetLayouts.size());
    plci.pSetLayouts            = vkSetLayouts.data();
    plci.pushConstantRangeCount = static_cast<uint32_t>(vkRanges.size());
    plci.pPushConstantRanges    = vkRanges.data();

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreatePipelineLayout(n.device, &plci, nullptr, &pipelineLayout));

    std::vector<VkShaderModule> modules;
    std::vector<VkPipelineShaderStageCreateInfo> stageInfos;
    std::vector<std::string> entryPoints;
    modules.reserve(desc.stages.size());
    stageInfos.reserve(desc.stages.size());
    entryPoints.reserve(desc.stages.size());

    for (const auto& s : desc.stages) {
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = s.code.size();
        smci.pCode    = reinterpret_cast<const uint32_t*>(s.code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        LUMORA_VK_CHECK(vkCreateShaderModule(n.device, &smci, nullptr, &mod));
        modules.push_back(mod);

        entryPoints.emplace_back(s.entryPoint.empty() ? "main" : s.entryPoint);

        VkPipelineShaderStageCreateInfo ssci{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ssci.stage  = static_cast<VkShaderStageFlagBits>(detail::ToVk(s.stage));
        ssci.module = mod;
        ssci.pName  = entryPoints.back().c_str();
        stageInfos.push_back(ssci);
    }

    std::vector<VkVertexInputBindingDescription> vbds;
    vbds.reserve(desc.vertex.bindings.size());
    for (const auto& vb : desc.vertex.bindings) {
        VkVertexInputBindingDescription b{};
        b.binding   = vb.binding;
        b.stride    = vb.stride;
        b.inputRate = vb.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        vbds.push_back(b);
    }

    std::vector<VkVertexInputAttributeDescription> vads;
    vads.reserve(desc.vertex.attributes.size());
    for (const auto& va : desc.vertex.attributes) {
        VkVertexInputAttributeDescription a{};
        a.location = va.location;
        a.binding  = va.binding;
        a.format   = detail::ToVk(va.format);
        a.offset   = va.offset;
        vads.push_back(a);
    }

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(vbds.size());
    vi.pVertexBindingDescriptions      = vbds.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(vads.size());
    vi.pVertexAttributeDescriptions    = vads.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology               = detail::ToVk(desc.topology);
    ia.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    const VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.depthClampEnable        = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode             = detail::ToVk(desc.rasterizer.polygonMode);
    rs.cullMode                = detail::ToVk(desc.rasterizer.cullMode);
    rs.frontFace               = detail::ToVk(desc.rasterizer.frontFace);
    rs.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = detail::ToVk(desc.samples);

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable       = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable      = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp        = detail::ToVk(desc.depthStencil.depthCompareOp);
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable     = VK_FALSE;

    std::vector<VkPipelineColorBlendAttachmentState> attachments;
    attachments.reserve(desc.blend.attachments.size());
    for (const auto& a : desc.blend.attachments) {
        VkPipelineColorBlendAttachmentState ba{};
        ba.blendEnable         = a.blendEnable ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = detail::ToVk(a.srcColor);
        ba.dstColorBlendFactor = detail::ToVk(a.dstColor);
        ba.colorBlendOp        = detail::ToVk(a.colorBlendOp);
        ba.srcAlphaBlendFactor = detail::ToVk(a.srcAlpha);
        ba.dstAlphaBlendFactor = detail::ToVk(a.dstAlpha);
        ba.alphaBlendOp        = detail::ToVk(a.alphaBlendOp);
        ba.colorWriteMask      = detail::ToVk(a.writeMask);
        attachments.push_back(ba);
    }

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = static_cast<uint32_t>(attachments.size());
    cb.pAttachments    = attachments.data();

    std::vector<VkFormat> colorVkFormats;
    colorVkFormats.reserve(desc.colorFormats.size());
    for (Format f : desc.colorFormats)
        colorVkFormats.push_back(detail::ToVk(f));

    VkPipelineRenderingCreateInfoKHR renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    renderingInfo.colorAttachmentCount    = static_cast<uint32_t>(colorVkFormats.size());
    renderingInfo.pColorAttachmentFormats = colorVkFormats.data();
    renderingInfo.depthAttachmentFormat   = detail::ToVk(desc.depthFormat);

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (!n.adapter.dynamicRendering) {
        const VkFormat rpFmt = desc.colorFormats.empty() ? VK_FORMAT_UNDEFINED : detail::ToVk(desc.colorFormats[0]);
        renderPass = n.GetRenderPass(rpFmt, LoadOp::Load);
    }

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.pNext               = n.adapter.dynamicRendering ? &renderingInfo : nullptr;
    gpci.stageCount          = static_cast<uint32_t>(stageInfos.size());
    gpci.pStages             = stageInfos.data();
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = &ds;
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = pipelineLayout;
    gpci.renderPass          = renderPass;
    gpci.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult r = vkCreateGraphicsPipelines(n.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline);

    for (VkShaderModule m : modules)
        vkDestroyShaderModule(n.device, m, nullptr);

    LUMORA_VK_CHECK(r);

    if (!desc.debugName.empty())
        n.Name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline), desc.debugName);

    detail::Pipeline p{};
    p.pipeline   = pipeline;
    p.layout     = pipelineLayout;
    p.setLayouts.assign(desc.setLayouts.begin(), desc.setLayouts.end());

    return n.pipelines.Add(std::move(p));
}

void RHIPipelines::Destroy(PipelineHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Pipeline p = n.pipelines.Remove(h);
    if (!p.pipeline && !p.layout)
        return;
    n.Defer([device = n.device, pipeline = p.pipeline, layout = p.layout]() {
        if (pipeline)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (layout)
            vkDestroyPipelineLayout(device, layout, nullptr);
    });
}

} // namespace lumora::rhi
