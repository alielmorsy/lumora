#include <lumora/rhi/CommandBuffer.h>

#include "Translate.h"
#include "VulkanRHI.h"

#include <algorithm>
#include <cassert>
#include <vector>

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

namespace {

#define LOG_INVALID_HANDLE_ONCE(type, h)                                             \
    do {                                                                             \
        static bool s_logged = false;                                                \
        if (!s_logged) {                                                             \
            s_logged = true;                                                         \
            LOG_WARN("Invalid " type " handle: ({}, {})", (h).index, (h).generation); \
        }                                                                            \
    } while (false)

void StageAccessFromLayout(ImageLayout layout, VkPipelineStageFlags& stage, VkAccessFlags& access) noexcept
{
    switch (layout) {
    case ImageLayout::Undefined:
        stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        access = 0;
        return;
    case ImageLayout::General:
        stage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        return;
    case ImageLayout::ColorAttachment:
        stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        return;
    case ImageLayout::DepthStencilAttachment:
        stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        return;
    case ImageLayout::ShaderReadOnly:
        stage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        access = VK_ACCESS_SHADER_READ_BIT;
        return;
    case ImageLayout::TransferSrc:
        stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
        access = VK_ACCESS_TRANSFER_READ_BIT;
        return;
    case ImageLayout::TransferDst:
        stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
        access = VK_ACCESS_TRANSFER_WRITE_BIT;
        return;
    case ImageLayout::PresentSrc:
        stage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        access = 0;
        return;
    }
    stage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
}

// Attachment view: explicit view if valid, else the texture's default view.
const detail::TextureView* ResolveView(detail::NativeRHI& rhi, TextureHandle texture, TextureViewHandle view)
{
    if (view.IsValid())
        return rhi.views.Get(view);
    const detail::Texture* t = rhi.textures.Get(texture);
    return t ? rhi.views.Get(t->defaultView) : nullptr;
}

} // namespace

namespace detail {

VkRenderPass NativeRHI::GetRenderPass(VkFormat format, LoadOp load)
{
    for (const auto& rp : renderPasses) {
        if (rp.format == format && rp.load == load)
            return rp.pass;
    }

    VkAttachmentDescription attDesc{};
    attDesc.format         = format;
    attDesc.samples        = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp         = ToVk(load);
    attDesc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attDesc.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attDesc.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &attDesc;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    VkRenderPass pass = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &pass));
    renderPasses.push_back({format, load, pass});
    return pass;
}

} // namespace detail

void CommandBuffer::Barrier(std::span<const TextureBarrier> textures, std::span<const BufferBarrier> buffers)
{
    if (!_rhi || !_cmd)
        return;

    std::vector<VkImageMemoryBarrier> imgBarriers;
    imgBarriers.reserve(textures.size());
    VkPipelineStageFlags srcStages = 0;
    VkPipelineStageFlags dstStages = 0;

    for (const auto& tb : textures) {
        detail::Texture* t = _rhi->textures.Get(tb.texture);
        if (!t) {
            LOG_INVALID_HANDLE_ONCE("texture", tb.texture);
            continue;
        }
        srcStages |= detail::ToVk(tb.srcStage);
        dstStages |= detail::ToVk(tb.dstStage);

        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask                   = detail::ToVk(tb.srcAccess);
        b.dstAccessMask                   = detail::ToVk(tb.dstAccess);
        b.oldLayout                       = detail::ToVk(tb.oldLayout);
        b.newLayout                       = detail::ToVk(tb.newLayout);
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = t->image;
        b.subresourceRange.aspectMask     = detail::AspectOf(t->desc.format);
        b.subresourceRange.baseMipLevel   = tb.subresource.baseMipLevel;
        b.subresourceRange.levelCount     = tb.subresource.levelCount == 0 ? VK_REMAINING_MIP_LEVELS : tb.subresource.levelCount;
        b.subresourceRange.baseArrayLayer = tb.subresource.baseArrayLayer;
        b.subresourceRange.layerCount     = tb.subresource.layerCount == 0 ? VK_REMAINING_ARRAY_LAYERS : tb.subresource.layerCount;
        imgBarriers.push_back(b);
    }

    std::vector<VkBufferMemoryBarrier> bufBarriers;
    bufBarriers.reserve(buffers.size());
    for (const auto& bb : buffers) {
        detail::Buffer* b = _rhi->buffers.Get(bb.buffer);
        if (!b) {
            LOG_INVALID_HANDLE_ONCE("buffer", bb.buffer);
            continue;
        }
        srcStages |= detail::ToVk(bb.srcStage);
        dstStages |= detail::ToVk(bb.dstStage);

        VkBufferMemoryBarrier mb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        mb.srcAccessMask       = detail::ToVk(bb.srcAccess);
        mb.dstAccessMask       = detail::ToVk(bb.dstAccess);
        mb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        mb.buffer              = b->buffer;
        mb.offset              = bb.offset;
        mb.size                = bb.size == ~0ull ? VK_WHOLE_SIZE : bb.size;
        bufBarriers.push_back(mb);
    }

    if (imgBarriers.empty() && bufBarriers.empty())
        return;

    if (srcStages == 0)
        srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (dstStages == 0)
        dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(_cmd), srcStages, dstStages, 0, 0, nullptr,
                         static_cast<uint32_t>(bufBarriers.size()), bufBarriers.data(),
                         static_cast<uint32_t>(imgBarriers.size()), imgBarriers.data());
}

void CommandBuffer::TransitionTexture(TextureHandle texture, ImageLayout oldLayout, ImageLayout newLayout)
{
    if (!_rhi || !_cmd)
        return;
    detail::Texture* t = _rhi->textures.Get(texture);
    if (!t) {
        LOG_INVALID_HANDLE_ONCE("texture", texture);
        return;
    }

    VkPipelineStageFlags srcStage  = 0;
    VkAccessFlags        srcAccess = 0;
    StageAccessFromLayout(oldLayout, srcStage, srcAccess);

    VkPipelineStageFlags dstStage  = 0;
    VkAccessFlags        dstAccess = 0;
    StageAccessFromLayout(newLayout, dstStage, dstAccess);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask                   = srcAccess;
    b.dstAccessMask                   = dstAccess;
    b.oldLayout                       = detail::ToVk(oldLayout);
    b.newLayout                       = detail::ToVk(newLayout);
    b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    b.image                           = t->image;
    b.subresourceRange.aspectMask     = detail::AspectOf(t->desc.format);
    b.subresourceRange.baseMipLevel   = 0;
    b.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(_cmd), srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void CommandBuffer::BeginRendering(const RenderingInfo& info)
{
    if (!_rhi || !_cmd)
        return;

    if (_rhi->cmdBeginRendering) {
        std::vector<VkRenderingAttachmentInfoKHR> colorAtts;
        colorAtts.reserve(info.colorAttachments.size());
        for (const auto& att : info.colorAttachments) {
            const detail::TextureView* tv = ResolveView(*_rhi, att.texture, att.view);
            if (!tv) {
                LOG_INVALID_HANDLE_ONCE("color attachment", att.view);
                continue;
            }
            VkImageView view = tv->view;

            VkRenderingAttachmentInfoKHR rai{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
            rai.imageView                   = view;
            rai.imageLayout                 = detail::ToVk(att.layout);
            rai.loadOp                      = detail::ToVk(att.loadOp);
            rai.storeOp                     = detail::ToVk(att.storeOp);
            rai.clearValue.color.float32[0] = att.clearColor.r;
            rai.clearValue.color.float32[1] = att.clearColor.g;
            rai.clearValue.color.float32[2] = att.clearColor.b;
            rai.clearValue.color.float32[3] = att.clearColor.a;
            colorAtts.push_back(rai);
        }

        VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
        ri.renderArea.offset    = {info.renderArea.offset.x, info.renderArea.offset.y};
        ri.renderArea.extent    = {info.renderArea.extent.width, info.renderArea.extent.height};
        ri.layerCount           = 1;
        ri.colorAttachmentCount = static_cast<uint32_t>(colorAtts.size());
        ri.pColorAttachments    = colorAtts.data();

        _rhi->cmdBeginRendering(static_cast<VkCommandBuffer>(_cmd), &ri);
        _inLegacyPass = false;
        return;
    }

    // Fallback: render pass + framebuffer
    if (info.colorAttachments.empty())
        return;
    assert(info.colorAttachments.size() == 1 && "Fallback render pass supports only 1 color attachment");

    const auto& att = info.colorAttachments[0];
    const detail::TextureView* tv = ResolveView(*_rhi, att.texture, att.view);
    detail::Texture* t = tv ? _rhi->textures.Get(tv->texture) : nullptr;
    if (!t) {
        LOG_INVALID_HANDLE_ONCE("color attachment", att.view);
        return;
    }

    VkRenderPass pass = _rhi->GetRenderPass(t->format, att.loadOp);
    // Framebuffer covers the whole image; renderArea may be a sub-rect.
    const uint32_t w = t->desc.extent.width;
    const uint32_t h = t->desc.extent.height;

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass      = pass;
    fci.attachmentCount = 1;
    fci.pAttachments    = &tv->view;
    fci.width           = w;
    fci.height          = h;
    fci.layers          = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateFramebuffer(_rhi->device, &fci, nullptr, &fb));
    VkDevice dev = _rhi->device;
    _rhi->Defer([dev, fb]() {
        vkDestroyFramebuffer(dev, fb, nullptr);
    });

    VkClearValue cv{};
    cv.color.float32[0] = att.clearColor.r;
    cv.color.float32[1] = att.clearColor.g;
    cv.color.float32[2] = att.clearColor.b;
    cv.color.float32[3] = att.clearColor.a;

    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass        = pass;
    rpbi.framebuffer       = fb;
    rpbi.renderArea.offset = {info.renderArea.offset.x, info.renderArea.offset.y};
    rpbi.renderArea.extent = {info.renderArea.extent.width, info.renderArea.extent.height};
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &cv;

    vkCmdBeginRenderPass(static_cast<VkCommandBuffer>(_cmd), &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    _inLegacyPass = true;
}

void CommandBuffer::EndRendering()
{
    if (!_rhi || !_cmd)
        return;

    if (_inLegacyPass) {
        vkCmdEndRenderPass(static_cast<VkCommandBuffer>(_cmd));
        _inLegacyPass = false;
    } else if (_rhi->cmdEndRendering) {
        _rhi->cmdEndRendering(static_cast<VkCommandBuffer>(_cmd));
    }
}

void CommandBuffer::SetViewport(const Viewport& vp)
{
    if (!_cmd)
        return;
    VkViewport v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
    vkCmdSetViewport(static_cast<VkCommandBuffer>(_cmd), 0, 1, &v);
}

void CommandBuffer::SetScissor(const Rect2D& rect)
{
    if (!_cmd)
        return;
    VkRect2D s{{rect.offset.x, rect.offset.y}, {rect.extent.width, rect.extent.height}};
    vkCmdSetScissor(static_cast<VkCommandBuffer>(_cmd), 0, 1, &s);
}

void CommandBuffer::BindPipeline(PipelineHandle pipeline)
{
    if (!_rhi || !_cmd)
        return;
    detail::Pipeline* p = _rhi->pipelines.Get(pipeline);
    if (!p) {
        LOG_INVALID_HANDLE_ONCE("pipeline", pipeline);
        return;
    }
    _bound = pipeline;
    vkCmdBindPipeline(static_cast<VkCommandBuffer>(_cmd), VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
}

void CommandBuffer::BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset)
{
    if (!_rhi || !_cmd)
        return;
    detail::Buffer* b = _rhi->buffers.Get(buffer);
    if (!b) {
        LOG_INVALID_HANDLE_ONCE("buffer", buffer);
        return;
    }
    VkBuffer     vkBuf    = b->buffer;
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(static_cast<VkCommandBuffer>(_cmd), binding, 1, &vkBuf, &vkOffset);
}

void CommandBuffer::BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type)
{
    if (!_rhi || !_cmd)
        return;
    detail::Buffer* b = _rhi->buffers.Get(buffer);
    if (!b) {
        LOG_INVALID_HANDLE_ONCE("buffer", buffer);
        return;
    }
    vkCmdBindIndexBuffer(static_cast<VkCommandBuffer>(_cmd), b->buffer, offset, detail::ToVk(type));
}

void CommandBuffer::PushConstants(ShaderStageFlags stages, uint32_t offset, std::span<const std::byte> data)
{
    if (!_rhi || !_cmd || data.empty())
        return;
    detail::Pipeline* p = _rhi->pipelines.Get(_bound);
    if (!p) {
        LOG_INVALID_HANDLE_ONCE("bound pipeline", _bound);
        return;
    }
    vkCmdPushConstants(static_cast<VkCommandBuffer>(_cmd), p->layout, detail::ToVk(stages), offset,
                       static_cast<uint32_t>(data.size()), data.data());
}

void CommandBuffer::PushDescriptors(uint32_t set, std::span<const DescriptorWrite> writes)
{
    if (!_rhi || !_cmd || writes.empty())
        return;
    detail::Pipeline* p = _rhi->pipelines.Get(_bound);
    if (!p) {
        LOG_INVALID_HANDLE_ONCE("bound pipeline", _bound);
        return;
    }
    if (set >= p->setLayouts.size()) {
        LOG_WARN("PushDescriptors: set index {} out of range (pipeline has {})", set, p->setLayouts.size());
        return;
    }
    DescriptorLayoutHandle layHandle = p->setLayouts[set];
    detail::DescriptorLayout* layout = _rhi->descriptorLayouts.Get(layHandle);
    if (!layout) {
        LOG_INVALID_HANDLE_ONCE("descriptor layout", layHandle);
        return;
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo>  imageInfos;
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());

    struct PendingWrite {
        size_t               bufIdx = ~0ull;
        size_t               imgIdx = ~0ull;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    };
    std::vector<PendingWrite> pending;
    pending.reserve(writes.size());

    for (const auto& w : writes) {
        PendingWrite pw{};
        pw.write.dstBinding      = w.binding;
        pw.write.dstArrayElement = w.arrayElement;
        pw.write.descriptorCount = 1;
        pw.write.descriptorType  = detail::ToVk(w.type);

        if (w.type == DescriptorType::UniformBuffer || w.type == DescriptorType::StorageBuffer) {
            detail::Buffer* b = _rhi->buffers.Get(w.bufferInfo.buffer);
            VkDescriptorBufferInfo bi{};
            bi.buffer = b ? b->buffer : VK_NULL_HANDLE;
            bi.offset = w.bufferInfo.offset;
            bi.range  = (w.bufferInfo.range == ~0ull) ? VK_WHOLE_SIZE : w.bufferInfo.range;
            pw.bufIdx = bufferInfos.size();
            bufferInfos.push_back(bi);
        } else if (w.type == DescriptorType::Sampler) {
            SamplerHandle sh = w.samplerOnly.IsValid() ? w.samplerOnly : w.imageInfo.sampler;
            detail::Sampler* s = _rhi->samplers.Get(sh);
            VkDescriptorImageInfo ii{};
            ii.sampler = s ? s->sampler : VK_NULL_HANDLE;
            pw.imgIdx  = imageInfos.size();
            imageInfos.push_back(ii);
        } else {
            detail::Texture* t = _rhi->textures.Get(w.imageInfo.texture);
            TextureViewHandle vh =
                w.imageInfo.view.IsValid() ? w.imageInfo.view : (t ? t->defaultView : TextureViewHandle{});
            detail::TextureView* tv = _rhi->views.Get(vh);
            VkDescriptorImageInfo ii{};
            if (w.type == DescriptorType::CombinedImageSampler) {
                detail::Sampler* s = _rhi->samplers.Get(w.imageInfo.sampler);
                ii.sampler         = s ? s->sampler : VK_NULL_HANDLE;
            }
            ii.imageView   = tv ? tv->view : VK_NULL_HANDLE;
            ii.imageLayout = detail::ToVk(w.imageInfo.layout);
            pw.imgIdx      = imageInfos.size();
            imageInfos.push_back(ii);
        }
        pending.push_back(pw);
    }

    std::vector<VkWriteDescriptorSet> vkWrites;
    vkWrites.reserve(pending.size());
    for (auto& pw : pending) {
        if (pw.bufIdx != ~0ull)
            pw.write.pBufferInfo = &bufferInfos[pw.bufIdx];
        if (pw.imgIdx != ~0ull)
            pw.write.pImageInfo = &imageInfos[pw.imgIdx];
        vkWrites.push_back(pw.write);
    }

    if (layout->push && _rhi->cmdPushDescriptorSet) {
        _rhi->cmdPushDescriptorSet(static_cast<VkCommandBuffer>(_cmd), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   p->layout, set, static_cast<uint32_t>(vkWrites.size()), vkWrites.data());
        return;
    }

    // Fallback: allocate descriptor set from frame pool
    if (_frameSlot >= _rhi->frames.size() || _rhi->frames[_frameSlot].descPool == VK_NULL_HANDLE) {
        LOG_WARN("PushDescriptors fallback: no descriptor pool for frame slot {}", _frameSlot);
        return;
    }

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = _rhi->frames[_frameSlot].descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout->layout;
    VkDescriptorSet dset  = VK_NULL_HANDLE;
    const VkResult  r     = vkAllocateDescriptorSets(_rhi->device, &ai, &dset);
    if (r != VK_SUCCESS) {
        LOG_ERROR("PushDescriptors fallback: vkAllocateDescriptorSets failed: {}", static_cast<int>(r));
        return;
    }

    for (auto& w : vkWrites)
        w.dstSet = dset;

    vkUpdateDescriptorSets(_rhi->device, static_cast<uint32_t>(vkWrites.size()), vkWrites.data(), 0, nullptr);
    vkCmdBindDescriptorSets(static_cast<VkCommandBuffer>(_cmd), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            p->layout, set, 1, &dset, 0, nullptr);
}

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    if (!_cmd)
        return;
    vkCmdDraw(static_cast<VkCommandBuffer>(_cmd), vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                uint32_t firstInstance)
{
    if (!_cmd)
        return;
    vkCmdDrawIndexed(static_cast<VkCommandBuffer>(_cmd), indexCount, instanceCount, firstIndex, vertexOffset,
                     firstInstance);
}

void CommandBuffer::CopyBufferToImage(BufferHandle src, TextureHandle dst, ImageLayout dstLayout,
                                      std::span<const BufferImageCopy> regions)
{
    if (!_rhi || !_cmd || regions.empty())
        return;
    detail::Buffer* b = _rhi->buffers.Get(src);
    if (!b) {
        LOG_INVALID_HANDLE_ONCE("buffer", src);
        return;
    }
    detail::Texture* t = _rhi->textures.Get(dst);
    if (!t) {
        LOG_INVALID_HANDLE_ONCE("texture", dst);
        return;
    }

    std::vector<VkBufferImageCopy> vkRegions;
    vkRegions.reserve(regions.size());
    const VkImageAspectFlags aspect = detail::AspectOf(t->desc.format);

    for (const auto& r : regions) {
        VkBufferImageCopy bic{};
        bic.bufferOffset                    = r.bufferOffset;
        bic.bufferRowLength                 = r.bufferRowLength;
        bic.bufferImageHeight               = r.bufferImageHeight;
        bic.imageSubresource.aspectMask     = aspect;
        bic.imageSubresource.mipLevel       = r.mipLevel;
        bic.imageSubresource.baseArrayLayer = r.baseArrayLayer;
        bic.imageSubresource.layerCount     = r.layerCount;
        bic.imageOffset                     = {r.imageOffset.x, r.imageOffset.y, r.imageOffset.z};
        bic.imageExtent                     = {r.imageExtent.width, r.imageExtent.height, r.imageExtent.depth};
        vkRegions.push_back(bic);
    }

    vkCmdCopyBufferToImage(static_cast<VkCommandBuffer>(_cmd), b->buffer, t->image, detail::ToVk(dstLayout),
                           static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
}

void CommandBuffer::BeginDebugLabel(std::string_view name, Color color)
{
    if (!_rhi || !_cmd || !_rhi->debugLabels || !_rhi->cmdBeginLabel)
        return;
    const std::string str(name);
    VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    l.pLabelName = str.c_str();
    l.color[0]   = color.r;
    l.color[1]   = color.g;
    l.color[2]   = color.b;
    l.color[3]   = color.a;
    _rhi->cmdBeginLabel(static_cast<VkCommandBuffer>(_cmd), &l);
}

void CommandBuffer::EndDebugLabel()
{
    if (!_rhi || !_cmd || !_rhi->debugLabels || !_rhi->cmdEndLabel)
        return;
    _rhi->cmdEndLabel(static_cast<VkCommandBuffer>(_cmd));
}

} // namespace lumora::rhi
